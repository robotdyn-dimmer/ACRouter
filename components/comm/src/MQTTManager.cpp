/**
 * @file MQTTManager.cpp
 * @brief MQTT client manager implementation
 */

#include "MQTTManager.h"
#include "RouterController.h"
#include "PowerMeterADC.h"
#include "ConfigManager.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_app_desc.h>

static const char* TAG = "MQTT";

// NVS namespace for MQTT config
static const char* NVS_NAMESPACE = "mqtt";

// Topic templates
static const char* TOPIC_BASE = "acrouter";

// Reconnect timing
static const uint32_t RECONNECT_INTERVAL_MS = 5000;
static const uint32_t SYSTEM_PUBLISH_INTERVAL_MS = 300000;  // 5 minutes

// ============================================================================
// Singleton Instance
// ============================================================================

MQTTManager& MQTTManager::getInstance() {
    static MQTTManager instance;
    return instance;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

MQTTManager::MQTTManager()
    : _client(nullptr)
    , _connected(false)
    , _initialized(false)
    , _clientStarted(false)
    , _router(nullptr)
    , _powerMeter(nullptr)
    , _configMgr(nullptr)
    , _lastMetricsPublish(0)
    , _lastStatusPublish(0)
    , _lastSystemPublish(0)
    , _connectionStartTime(0)
    , _lastReconnectAttempt(0)
    , _messagesPublished(0)
    , _messagesReceived(0)
    , _reconnectCount(0)
    , _lastMode(255)
    , _lastState(255)
    , _lastDimmer(255)
{
    memset(_lastError, 0, sizeof(_lastError));
}

MQTTManager::~MQTTManager() {
    end();
}

// ============================================================================
// Lifecycle
// ============================================================================

void MQTTManager::begin(RouterController* router, PowerMeterADC* powerMeter, ConfigManager* config) {
    _router = router;
    _powerMeter = powerMeter;
    _configMgr = config;

    // Load configuration from NVS
    loadConfig();

    // Generate device ID if not set
    if (strlen(_config.device_id) == 0) {
        generateDeviceId();
    }

    // Build topic prefix
    _topicPrefix = String(TOPIC_BASE) + "/" + String(_config.device_id);

    _initialized = true;
    ESP_LOGI(TAG, "Initialized - Device ID: %s, Broker: %s, Enabled: %s",
             _config.device_id,
             _config.broker,
             _config.enabled ? "yes" : "no");

    // Auto-connect if enabled and broker is configured
    if (_config.enabled && strlen(_config.broker) > 0) {
        connect();
    }
}

void MQTTManager::loop() {
    if (!_initialized) return;

    uint32_t now = millis();

    // Handle reconnection
    if (_config.enabled && !_connected && strlen(_config.broker) > 0) {
        if (now - _lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
            _lastReconnectAttempt = now;
            connect();
        }
        return;
    }

    if (!_connected) return;

    // Periodic metrics publishing
    if (now - _lastMetricsPublish >= _config.publish_interval) {
        _lastMetricsPublish = now;
        publishMetrics();
    }

    // Periodic status publishing (or on change)
    bool statusChanged = false;
    if (_router) {
        const RouterStatus& status = _router->getStatus();
        statusChanged = (static_cast<uint8_t>(status.mode) != _lastMode ||
                        static_cast<uint8_t>(status.state) != _lastState ||
                        status.dimmer_percent != _lastDimmer);
    }

    if (statusChanged || now - _lastStatusPublish >= _config.status_interval) {
        _lastStatusPublish = now;
        publishStatus();
    }

    // Periodic system info publishing
    if (now - _lastSystemPublish >= SYSTEM_PUBLISH_INTERVAL_MS) {
        _lastSystemPublish = now;
        publishSystemInfo();
    }
}

void MQTTManager::end() {
    if (_client) {
        disconnect();
        esp_mqtt_client_destroy(_client);
        _client = nullptr;
    }
    _initialized = false;
}

// ============================================================================
// Connection Management
// ============================================================================

bool MQTTManager::connect() {
    if (!_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    if (_connected) {
        ESP_LOGD(TAG, "Already connected");
        return true;
    }

    // Check WiFi connection first
    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGD(TAG, "WiFi not connected, waiting...");
        return false;
    }

    if (strlen(_config.broker) == 0) {
        ESP_LOGE(TAG, "Broker URL not configured");
        strncpy(_lastError, "Broker not configured", sizeof(_lastError) - 1);
        return false;
    }

    ESP_LOGI(TAG, "Connecting to %s...", _config.broker);

    // Build LWT topic
    String lwtTopic = _topicPrefix + "/status/online";

    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = _config.broker;

    if (strlen(_config.username) > 0) {
        mqtt_cfg.credentials.username = _config.username;
    }
    if (strlen(_config.password) > 0) {
        mqtt_cfg.credentials.authentication.password = _config.password;
    }

    // Client ID
    if (strlen(_config.client_id) > 0) {
        mqtt_cfg.credentials.client_id = _config.client_id;
    } else {
        // Auto-generate from device ID
        static char auto_client_id[48];
        snprintf(auto_client_id, sizeof(auto_client_id), "acrouter_%s", _config.device_id);
        mqtt_cfg.credentials.client_id = auto_client_id;
    }

    // Last Will & Testament
    static char lwt_topic_buf[64];
    strncpy(lwt_topic_buf, lwtTopic.c_str(), sizeof(lwt_topic_buf) - 1);
    mqtt_cfg.session.last_will.topic = lwt_topic_buf;
    mqtt_cfg.session.last_will.msg = "offline";
    mqtt_cfg.session.last_will.qos = 1;
    mqtt_cfg.session.last_will.retain = true;

    // Keepalive
    mqtt_cfg.session.keepalive = 60;

    // Create client if not exists
    if (_client == nullptr) {
        _client = esp_mqtt_client_init(&mqtt_cfg);
        if (_client == nullptr) {
            ESP_LOGE(TAG, "Failed to create MQTT client");
            strncpy(_lastError, "Client creation failed", sizeof(_lastError) - 1);
            return false;
        }

        // Register event handler
        esp_mqtt_client_register_event(_client, MQTT_EVENT_ANY, mqttEventHandler, this);
        _clientStarted = false;
    } else {
        // Update config for existing client
        esp_mqtt_set_config(_client, &mqtt_cfg);
    }

    // Start client (only if not already started)
    if (!_clientStarted) {
        esp_err_t err = esp_mqtt_client_start(_client);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
            snprintf(_lastError, sizeof(_lastError), "Start failed: %s", esp_err_to_name(err));
            return false;
        }
        _clientStarted = true;
    }

    return true;  // Connection is async, will be set in event handler
}

void MQTTManager::disconnect() {
    if (_client) {
        ESP_LOGI(TAG, "Disconnecting...");

        // Publish offline status before disconnecting (if connected)
        if (_connected) {
            String topic = _topicPrefix + "/status/online";
            esp_mqtt_client_publish(_client, topic.c_str(), "offline", 0, 1, true);
        }

        // Stop the client completely (allows restart)
        esp_mqtt_client_stop(_client);
        _connected = false;
        _clientStarted = false;
    }
}

void MQTTManager::reconnect() {
    disconnect();
    _lastReconnectAttempt = 0;  // Force immediate reconnect
}

// ============================================================================
// Configuration
// ============================================================================

void MQTTManager::generateDeviceId() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(_config.device_id, sizeof(_config.device_id), "%02X%02X%02X",
             mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Generated device ID: %s", _config.device_id);
}

void MQTTManager::setBroker(const char* broker) {
    strncpy(_config.broker, broker, sizeof(_config.broker) - 1);
    saveConfig();
    if (_connected) reconnect();
}

void MQTTManager::setCredentials(const char* username, const char* password) {
    if (username) strncpy(_config.username, username, sizeof(_config.username) - 1);
    if (password) strncpy(_config.password, password, sizeof(_config.password) - 1);
    saveConfig();
    if (_connected) reconnect();
}

void MQTTManager::setDeviceId(const char* deviceId) {
    if (deviceId && strlen(deviceId) > 0) {
        strncpy(_config.device_id, deviceId, sizeof(_config.device_id) - 1);
        _topicPrefix = String(TOPIC_BASE) + "/" + String(_config.device_id);
        saveConfig();
        if (_connected) reconnect();
    }
}

void MQTTManager::setDeviceName(const char* name) {
    if (name) {
        strncpy(_config.device_name, name, sizeof(_config.device_name) - 1);
        saveConfig();
    }
}

void MQTTManager::setPublishInterval(uint32_t intervalMs) {
    if (intervalMs >= 1000 && intervalMs <= 60000) {
        _config.publish_interval = intervalMs;
        saveConfig();
    }
}

void MQTTManager::setHADiscovery(bool enabled) {
    _config.ha_discovery = enabled;
    saveConfig();
    if (_connected && enabled) {
        publishHADiscovery();
    }
}

void MQTTManager::setEnabled(bool enabled) {
    _config.enabled = enabled;
    saveConfig();
    if (enabled && !_connected && strlen(_config.broker) > 0) {
        connect();
    } else if (!enabled && _connected) {
        disconnect();
    }
}

bool MQTTManager::loadConfig() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found, using defaults");
        return false;
    }

    size_t len;

    len = sizeof(_config.broker);
    nvs_get_str(nvs, "broker", _config.broker, &len);

    len = sizeof(_config.username);
    nvs_get_str(nvs, "username", _config.username, &len);

    len = sizeof(_config.password);
    nvs_get_str(nvs, "password", _config.password, &len);

    len = sizeof(_config.device_id);
    nvs_get_str(nvs, "device_id", _config.device_id, &len);

    len = sizeof(_config.device_name);
    nvs_get_str(nvs, "device_name", _config.device_name, &len);

    nvs_get_u32(nvs, "pub_interval", &_config.publish_interval);

    uint8_t val;
    if (nvs_get_u8(nvs, "ha_discovery", &val) == ESP_OK) {
        _config.ha_discovery = (val != 0);
    }
    if (nvs_get_u8(nvs, "enabled", &val) == ESP_OK) {
        _config.enabled = (val != 0);
    }

    nvs_close(nvs);
    ESP_LOGI(TAG, "Configuration loaded from NVS");
    return true;
}

bool MQTTManager::saveConfig() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    nvs_set_str(nvs, "broker", _config.broker);
    nvs_set_str(nvs, "username", _config.username);
    nvs_set_str(nvs, "password", _config.password);
    nvs_set_str(nvs, "device_id", _config.device_id);
    nvs_set_str(nvs, "device_name", _config.device_name);
    nvs_set_u32(nvs, "pub_interval", _config.publish_interval);
    nvs_set_u8(nvs, "ha_discovery", _config.ha_discovery ? 1 : 0);
    nvs_set_u8(nvs, "enabled", _config.enabled ? 1 : 0);

    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Configuration saved to NVS");
    return true;
}

// ============================================================================
// Publishing - Helper Functions
// ============================================================================

String MQTTManager::buildTopic(const char* category, const char* name) {
    return _topicPrefix + "/" + category + "/" + name;
}

bool MQTTManager::publish(const char* topic, const char* payload, bool retain, int qos) {
    if (!_connected || !_client) return false;

    int msg_id = esp_mqtt_client_publish(_client, topic, payload, 0, qos, retain ? 1 : 0);
    if (msg_id >= 0) {
        _messagesPublished++;
        return true;
    }

    ESP_LOGW(TAG, "Publish failed: %s", topic);
    return false;
}

bool MQTTManager::subscribe(const char* topic, int qos) {
    if (!_connected || !_client) return false;

    int msg_id = esp_mqtt_client_subscribe(_client, topic, qos);
    if (msg_id >= 0) {
        ESP_LOGI(TAG, "Subscribed to: %s", topic);
        return true;
    }

    ESP_LOGW(TAG, "Subscribe failed: %s", topic);
    return false;
}

// ============================================================================
// Publishing - Status
// ============================================================================

void MQTTManager::publishStatus() {
    if (!_connected || !_router) return;

    const RouterStatus& status = _router->getStatus();

    // Mode
    static const char* modeNames[] = {"off", "auto", "eco", "offgrid", "manual", "boost"};
    uint8_t modeIdx = static_cast<uint8_t>(status.mode);
    if (modeIdx < 6) {
        publish(buildTopic("status", "mode").c_str(), modeNames[modeIdx], true, 1);
    }

    // State
    static const char* stateNames[] = {"idle", "increasing", "decreasing", "at_max", "at_min", "error"};
    uint8_t stateIdx = static_cast<uint8_t>(status.state);
    if (stateIdx < 6) {
        publish(buildTopic("status", "state").c_str(), stateNames[stateIdx], true, 1);
    }

    // Dimmer level
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", status.dimmer_percent);
    publish(buildTopic("status", "dimmer").c_str(), buf, true, 1);

    // WiFi RSSI
    snprintf(buf, sizeof(buf), "%d", WiFi.RSSI());
    publish(buildTopic("status", "wifi_rssi").c_str(), buf, true, 1);

    // Update tracking
    _lastMode = modeIdx;
    _lastState = stateIdx;
    _lastDimmer = status.dimmer_percent;

    // JSON aggregated status
    JsonDocument doc;
    doc["mode"] = modeNames[modeIdx];
    doc["state"] = stateNames[stateIdx];
    doc["dimmer"] = status.dimmer_percent;
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["valid"] = status.valid;

    String json;
    serializeJson(doc, json);
    publish(buildTopic("json", "status").c_str(), json.c_str(), true, 1);
}

// ============================================================================
// Publishing - Metrics
// ============================================================================

void MQTTManager::publishMetrics() {
    if (!_connected || !_powerMeter) return;

    const PowerMeterADC::Measurements& m = _powerMeter->getMeasurements();
    if (!m.valid) return;

    char buf[32];

    // Voltage
    snprintf(buf, sizeof(buf), "%.1f", m.voltage_rms);
    publish(buildTopic("metrics", "voltage").c_str(), buf, false, 0);

    // Power values (from RouterController for consistency)
    if (_router) {
        const RouterStatus& status = _router->getStatus();

        snprintf(buf, sizeof(buf), "%.1f", status.power_grid);
        publish(buildTopic("metrics", "power_grid").c_str(), buf, false, 0);

        snprintf(buf, sizeof(buf), "%.1f", status.power_solar);
        publish(buildTopic("metrics", "power_solar").c_str(), buf, false, 0);

        snprintf(buf, sizeof(buf), "%.1f", status.power_load);
        publish(buildTopic("metrics", "power_load").c_str(), buf, false, 0);
    }

    // Currents
    snprintf(buf, sizeof(buf), "%.2f", m.current_rms[PowerMeterADC::CURRENT_GRID]);
    publish(buildTopic("metrics", "current_grid").c_str(), buf, false, 0);

    snprintf(buf, sizeof(buf), "%.2f", m.current_rms[PowerMeterADC::CURRENT_SOLAR]);
    publish(buildTopic("metrics", "current_solar").c_str(), buf, false, 0);

    snprintf(buf, sizeof(buf), "%.2f", m.current_rms[PowerMeterADC::CURRENT_LOAD]);
    publish(buildTopic("metrics", "current_load").c_str(), buf, false, 0);

    // Direction
    static const char* dirNames[] = {"consuming", "supplying", "balanced", "unknown"};
    uint8_t dirIdx = static_cast<uint8_t>(m.direction[PowerMeterADC::CURRENT_GRID]);
    if (dirIdx < 4) {
        publish(buildTopic("metrics", "direction").c_str(), dirNames[dirIdx], false, 0);
    }

    // JSON aggregated metrics
    JsonDocument doc;
    doc["voltage"] = m.voltage_rms;
    if (_router) {
        const RouterStatus& status = _router->getStatus();
        doc["power_grid"] = status.power_grid;
        doc["power_solar"] = status.power_solar;
        doc["power_load"] = status.power_load;
    }
    doc["current_grid"] = m.current_rms[PowerMeterADC::CURRENT_GRID];
    doc["current_solar"] = m.current_rms[PowerMeterADC::CURRENT_SOLAR];
    doc["current_load"] = m.current_rms[PowerMeterADC::CURRENT_LOAD];
    doc["direction"] = dirNames[dirIdx];

    String json;
    serializeJson(doc, json);
    publish(buildTopic("json", "metrics").c_str(), json.c_str(), false, 0);
}

// ============================================================================
// Publishing - Config
// ============================================================================

void MQTTManager::publishConfig() {
    if (!_connected) return;

    char buf[32];

    if (_router) {
        snprintf(buf, sizeof(buf), "%.1f", _router->getControlGain());
        publish(buildTopic("config", "control_gain").c_str(), buf, true, 1);

        snprintf(buf, sizeof(buf), "%.1f", _router->getBalanceThreshold());
        publish(buildTopic("config", "balance_threshold").c_str(), buf, true, 1);

        snprintf(buf, sizeof(buf), "%d", _router->getManualLevel());
        publish(buildTopic("config", "manual_level").c_str(), buf, true, 1);
    }

    snprintf(buf, sizeof(buf), "%lu", _config.publish_interval);
    publish(buildTopic("config", "publish_interval").c_str(), buf, true, 1);
}

// ============================================================================
// Publishing - System Info
// ============================================================================

void MQTTManager::publishSystemInfo() {
    if (!_connected) return;

    char buf[64];

    // Version from app description
    const esp_app_desc_t* app_desc = esp_app_get_description();
    publish(buildTopic("system", "version").c_str(), app_desc->version, true, 1);

    // IP address
    publish(buildTopic("system", "ip").c_str(), WiFi.localIP().toString().c_str(), true, 1);

    // MAC address
    publish(buildTopic("system", "mac").c_str(), WiFi.macAddress().c_str(), true, 1);

    // Uptime
    snprintf(buf, sizeof(buf), "%lu", millis() / 1000);
    publish(buildTopic("system", "uptime").c_str(), buf, true, 1);

    // Free heap
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)ESP.getFreeHeap());
    publish(buildTopic("system", "free_heap").c_str(), buf, true, 1);

    // JSON aggregated system info
    JsonDocument doc;
    doc["version"] = app_desc->version;
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();

    String json;
    serializeJson(doc, json);
    publish(buildTopic("json", "system").c_str(), json.c_str(), true, 1);
}

void MQTTManager::publishOnline() {
    publish(buildTopic("status", "online").c_str(), "online", true, 1);
}

void MQTTManager::publishAll() {
    publishOnline();
    publishStatus();
    publishMetrics();
    publishConfig();
    publishSystemInfo();
    if (_config.ha_discovery) {
        publishHADiscovery();
    }
}

// ============================================================================
// Home Assistant Discovery
// ============================================================================

String MQTTManager::getHADeviceInfo() {
    const esp_app_desc_t* app_desc = esp_app_get_description();

    JsonDocument doc;
    JsonArray identifiers = doc["identifiers"].to<JsonArray>();
    identifiers.add(String("acrouter_") + _config.device_id);

    doc["name"] = strlen(_config.device_name) > 0 ? _config.device_name : "ACRouter Solar";
    doc["model"] = "ACRouter";
    doc["manufacturer"] = "RobotDyn";
    doc["sw_version"] = app_desc->version;
    doc["configuration_url"] = String("http://") + WiFi.localIP().toString();

    String json;
    serializeJson(doc, json);
    return json;
}

void MQTTManager::publishHASensor(const char* name, const char* uniqueId, const char* stateTopic,
                                   const char* unit, const char* deviceClass, const char* stateClass) {
    String discoveryTopic = String("homeassistant/sensor/acrouter_") + _config.device_id + "/" + uniqueId + "/config";

    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = String("acrouter_") + _config.device_id + "_" + uniqueId;
    doc["state_topic"] = stateTopic;
    doc["availability_topic"] = buildTopic("status", "online");

    if (unit) doc["unit_of_measurement"] = unit;
    if (deviceClass) doc["device_class"] = deviceClass;
    if (stateClass) doc["state_class"] = stateClass;

    // Add device info
    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add(String("acrouter_") + _config.device_id);
    device["name"] = strlen(_config.device_name) > 0 ? _config.device_name : "ACRouter Solar";
    device["model"] = "ACRouter";
    device["manufacturer"] = "RobotDyn";
    const esp_app_desc_t* app_desc = esp_app_get_description();
    device["sw_version"] = app_desc->version;
    device["configuration_url"] = String("http://") + WiFi.localIP().toString();

    String json;
    serializeJson(doc, json);
    publish(discoveryTopic.c_str(), json.c_str(), true, 1);
}

void MQTTManager::publishHASelect(const char* name, const char* uniqueId, const char* stateTopic,
                                   const char* commandTopic, const char* options[], int optionCount) {
    String discoveryTopic = String("homeassistant/select/acrouter_") + _config.device_id + "/" + uniqueId + "/config";

    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = String("acrouter_") + _config.device_id + "_" + uniqueId;
    doc["state_topic"] = stateTopic;
    doc["command_topic"] = commandTopic;
    doc["availability_topic"] = buildTopic("status", "online");

    JsonArray opts = doc["options"].to<JsonArray>();
    for (int i = 0; i < optionCount; i++) {
        opts.add(options[i]);
    }

    // Add device info
    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add(String("acrouter_") + _config.device_id);
    device["name"] = strlen(_config.device_name) > 0 ? _config.device_name : "ACRouter Solar";
    device["model"] = "ACRouter";
    device["manufacturer"] = "RobotDyn";
    const esp_app_desc_t* app_desc = esp_app_get_description();
    device["sw_version"] = app_desc->version;

    String json;
    serializeJson(doc, json);
    publish(discoveryTopic.c_str(), json.c_str(), true, 1);
}

void MQTTManager::publishHANumber(const char* name, const char* uniqueId, const char* stateTopic,
                                   const char* commandTopic, float min, float max, float step,
                                   const char* unit) {
    String discoveryTopic = String("homeassistant/number/acrouter_") + _config.device_id + "/" + uniqueId + "/config";

    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = String("acrouter_") + _config.device_id + "_" + uniqueId;
    doc["state_topic"] = stateTopic;
    doc["command_topic"] = commandTopic;
    doc["availability_topic"] = buildTopic("status", "online");
    doc["min"] = min;
    doc["max"] = max;
    doc["step"] = step;
    if (unit) doc["unit_of_measurement"] = unit;

    // Add device info
    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add(String("acrouter_") + _config.device_id);
    device["name"] = strlen(_config.device_name) > 0 ? _config.device_name : "ACRouter Solar";
    device["model"] = "ACRouter";
    device["manufacturer"] = "RobotDyn";
    const esp_app_desc_t* app_desc = esp_app_get_description();
    device["sw_version"] = app_desc->version;

    String json;
    serializeJson(doc, json);
    publish(discoveryTopic.c_str(), json.c_str(), true, 1);
}

void MQTTManager::publishHAButton(const char* name, const char* uniqueId, const char* commandTopic) {
    String discoveryTopic = String("homeassistant/button/acrouter_") + _config.device_id + "/" + uniqueId + "/config";

    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = String("acrouter_") + _config.device_id + "_" + uniqueId;
    doc["command_topic"] = commandTopic;
    doc["availability_topic"] = buildTopic("status", "online");
    doc["payload_press"] = "1";

    // Add device info
    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add(String("acrouter_") + _config.device_id);
    device["name"] = strlen(_config.device_name) > 0 ? _config.device_name : "ACRouter Solar";
    device["model"] = "ACRouter";
    device["manufacturer"] = "RobotDyn";
    const esp_app_desc_t* app_desc = esp_app_get_description();
    device["sw_version"] = app_desc->version;

    String json;
    serializeJson(doc, json);
    publish(discoveryTopic.c_str(), json.c_str(), true, 1);
}

void MQTTManager::publishHADiscovery() {
    if (!_connected) return;

    ESP_LOGI(TAG, "Publishing Home Assistant discovery...");

    // Sensors
    publishHASensor("Grid Power", "power_grid",
                    buildTopic("metrics", "power_grid").c_str(),
                    "W", "power", "measurement");

    publishHASensor("Solar Power", "power_solar",
                    buildTopic("metrics", "power_solar").c_str(),
                    "W", "power", "measurement");

    publishHASensor("Load Power", "power_load",
                    buildTopic("metrics", "power_load").c_str(),
                    "W", "power", "measurement");

    publishHASensor("Voltage", "voltage",
                    buildTopic("metrics", "voltage").c_str(),
                    "V", "voltage", "measurement");

    publishHASensor("Dimmer Level", "dimmer",
                    buildTopic("status", "dimmer").c_str(),
                    "%", nullptr, "measurement");

    publishHASensor("WiFi Signal", "wifi_rssi",
                    buildTopic("status", "wifi_rssi").c_str(),
                    "dBm", "signal_strength", "measurement");

    // Mode select
    static const char* modeOptions[] = {"off", "auto", "eco", "offgrid", "manual", "boost"};
    publishHASelect("Router Mode", "mode",
                    buildTopic("status", "mode").c_str(),
                    buildTopic("command", "mode").c_str(),
                    modeOptions, 6);

    // Number controls
    publishHANumber("Control Gain", "control_gain",
                    buildTopic("config", "control_gain").c_str(),
                    buildTopic("config", "control_gain/set").c_str(),
                    10, 1000, 10);

    publishHANumber("Balance Threshold", "balance_threshold",
                    buildTopic("config", "balance_threshold").c_str(),
                    buildTopic("config", "balance_threshold/set").c_str(),
                    0, 1000, 1, "W");

    publishHANumber("Manual Level", "manual_level",
                    buildTopic("config", "manual_level").c_str(),
                    buildTopic("config", "manual_level/set").c_str(),
                    0, 100, 1, "%");

    // Buttons
    publishHAButton("Emergency Stop", "emergency_stop",
                    buildTopic("command", "emergency_stop").c_str());

    publishHAButton("Reboot", "reboot",
                    buildTopic("command", "reboot").c_str());

    ESP_LOGI(TAG, "Home Assistant discovery published");
}

// ============================================================================
// Subscriptions & Message Handling
// ============================================================================

void MQTTManager::setupSubscriptions() {
    // Subscribe to commands
    String topic = _topicPrefix + "/command/#";
    subscribe(topic.c_str(), 1);

    // Subscribe to config set requests
    topic = _topicPrefix + "/config/+/set";
    subscribe(topic.c_str(), 1);
}

void MQTTManager::handleMessage(const char* topic, const char* payload, int len) {
    _messagesReceived++;

    // Create null-terminated payload string
    // IMPORTANT: payload is NOT null-terminated, we must copy with length
    char payloadBuf[256];  // Buffer for payload
    int copyLen = (len < (int)sizeof(payloadBuf) - 1) ? len : (int)sizeof(payloadBuf) - 1;
    if (payload && len > 0) {
        memcpy(payloadBuf, payload, copyLen);
    }
    payloadBuf[copyLen] = '\0';
    String payloadStr = payloadBuf;

    ESP_LOGD(TAG, "Message: %s = %s", topic, payloadStr.c_str());

    String topicStr = String(topic);

    // Handle command topics
    String commandPrefix = _topicPrefix + "/command/";
    if (topicStr.startsWith(commandPrefix)) {
        String command = topicStr.substring(commandPrefix.length());
        handleCommand(command.c_str(), payloadStr.c_str());
        return;
    }

    // Handle config set topics
    String configPrefix = _topicPrefix + "/config/";
    String configSuffix = "/set";
    if (topicStr.startsWith(configPrefix) && topicStr.endsWith(configSuffix)) {
        String param = topicStr.substring(configPrefix.length());
        param = param.substring(0, param.length() - configSuffix.length());
        handleConfigSet(param.c_str(), payloadStr.c_str());
        return;
    }
}

void MQTTManager::handleCommand(const char* command, const char* payload) {
    ESP_LOGI(TAG, "Command: %s = %s", command, payload);

    if (strcmp(command, "mode") == 0) {
        if (!_router) return;

        RouterMode mode;
        if (strcmp(payload, "off") == 0) mode = RouterMode::OFF;
        else if (strcmp(payload, "auto") == 0) mode = RouterMode::AUTO;
        else if (strcmp(payload, "eco") == 0) mode = RouterMode::ECO;
        else if (strcmp(payload, "offgrid") == 0) mode = RouterMode::OFFGRID;
        else if (strcmp(payload, "manual") == 0) mode = RouterMode::MANUAL;
        else if (strcmp(payload, "boost") == 0) mode = RouterMode::BOOST;
        else {
            ESP_LOGW(TAG, "Unknown mode: %s", payload);
            return;
        }

        _router->setMode(mode);
        publishStatus();  // Confirm the change
    }
    else if (strcmp(command, "dimmer") == 0) {
        if (!_router) return;

        int level = atoi(payload);
        if (level >= 0 && level <= 100) {
            _router->setManualLevel(level);
            publishStatus();
        }
    }
    else if (strcmp(command, "emergency_stop") == 0) {
        if (_router) {
            _router->emergencyStop();
            publishStatus();
        }
    }
    else if (strcmp(command, "reboot") == 0) {
        ESP_LOGI(TAG, "Reboot requested via MQTT");
        delay(1000);
        ESP.restart();
    }
    else if (strcmp(command, "refresh") == 0) {
        publishAll();
    }
}

void MQTTManager::handleConfigSet(const char* param, const char* value) {
    ESP_LOGI(TAG, "Config set: %s = %s", param, value);

    if (strcmp(param, "control_gain") == 0) {
        if (_router) {
            float gain = atof(value);
            if (gain >= 10 && gain <= 1000) {
                _router->setControlGain(gain);
                publishConfig();
            }
        }
    }
    else if (strcmp(param, "balance_threshold") == 0) {
        if (_router) {
            float threshold = atof(value);
            if (threshold >= 0 && threshold <= 1000) {
                _router->setBalanceThreshold(threshold);
                publishConfig();
            }
        }
    }
    else if (strcmp(param, "manual_level") == 0) {
        if (_router) {
            int level = atoi(value);
            if (level >= 0 && level <= 100) {
                _router->setManualLevel(level);
                publishConfig();
            }
        }
    }
    else if (strcmp(param, "publish_interval") == 0) {
        uint32_t interval = atoi(value);
        if (interval >= 1000 && interval <= 60000) {
            setPublishInterval(interval);
            publishConfig();
        }
    }
}

// ============================================================================
// Status Methods
// ============================================================================

const char* MQTTManager::getConnectionState() const {
    if (!_initialized) return "Not initialized";
    if (!_config.enabled) return "Disabled";
    if (strlen(_config.broker) == 0) return "Not configured";
    if (_connected) return "Connected";
    return "Disconnected";
}

uint32_t MQTTManager::getConnectionUptime() const {
    if (!_connected || _connectionStartTime == 0) return 0;
    return (millis() - _connectionStartTime) / 1000;
}

// ============================================================================
// ESP-MQTT Event Handler
// ============================================================================

void MQTTManager::mqttEventHandler(void* handlerArgs, esp_event_base_t base,
                                    int32_t eventId, void* eventData) {
    MQTTManager* instance = static_cast<MQTTManager*>(handlerArgs);
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(eventData);
    instance->onMqttEvent(event);
}

void MQTTManager::onMqttEvent(esp_mqtt_event_handle_t event) {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to broker");
            _connected = true;
            _connectionStartTime = millis();
            _reconnectCount++;
            memset(_lastError, 0, sizeof(_lastError));

            // Setup subscriptions
            setupSubscriptions();

            // Publish initial data
            publishAll();
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from broker");
            _connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "Subscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGD(TAG, "Unsubscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "Published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA: {
            // IMPORTANT: event->topic is NOT null-terminated!
            // We must copy it with the correct length
            char* topic_buf = (char*)malloc(event->topic_len + 1);
            if (topic_buf) {
                memcpy(topic_buf, event->topic, event->topic_len);
                topic_buf[event->topic_len] = '\0';
                handleMessage(topic_buf, event->data, event->data_len);
                free(topic_buf);
            }
            break;
        }

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TCP transport error");
                snprintf(_lastError, sizeof(_lastError), "TCP transport error");
            }
            break;

        default:
            ESP_LOGD(TAG, "Other event: %d", event->event_id);
            break;
    }
}
