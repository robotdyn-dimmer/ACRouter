/**
 * @file MQTTManager.cpp
 * @brief MQTT client manager implementation
 */

#include "MQTTManager.h"
#include "RouterController.h"
#include "sensor_hub.h"
#include "ConfigManager.h"
#include "sdkconfig.h"
#include <math.h>
#if CONFIG_ACROUTER_RBAMP_SOURCE
#include "rbamp_source.h"
#endif
#if CONFIG_ACROUTER_ESPNOW_SOURCE
#include "esp_now_source.h"
#endif

// New dimmer manager (pure C API)
extern "C" {
#include "dimmer_manager.h"
#include "device_registry.h"
}

// New relay manager (pure C API)
extern "C" {
#include "relay_manager.h"
#include "relay_gpio.h"
}
#include "esp_wifi.h"
#include "esp_netif.h"
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"           // esp_restart()
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_app_desc.h>

using namespace ACRouter;

static const char* TAG = "MQTT";

// Helper functions for WiFi (replaces Arduino WiFi calls)
static bool isWiFiConnected() {
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}

static void getWiFiMAC(uint8_t* mac) {
    esp_wifi_get_mac(WIFI_IF_STA, mac);
}

static String getWiFiMACString() {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static int8_t getWiFiRSSI() {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

static String getWiFiLocalIP() {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            char buf[16];
            snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip_info.ip));
            return String(buf);
        }
    }
    return String("0.0.0.0");
}

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
    , _pendingInitialPublish(false)
    , _router(nullptr)
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
    memset(_lastDimmers, 255, sizeof(_lastDimmers));
    memset(_lastRelays, -1, sizeof(_lastRelays));  // -1 sentinel: force first publish even for boot-OFF relays
}

MQTTManager::~MQTTManager() {
    end();
}

// ============================================================================
// Lifecycle
// ============================================================================

void MQTTManager::begin(RouterController* router, ConfigManager* config) {
    _router = router;
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

    // Initial post-connect publish, deferred out of the CONNECTED event callback so
    // the esp-mqtt task keeps servicing inbound commands + keepalive during the burst.
    if (_pendingInitialPublish) {
        _pendingInitialPublish = false;
        publishAll();
    }

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
        publishDimmersStatus();
        publishRelaysStatus();
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
    if (!isWiFiConnected()) {
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

    // Keep esp-mqtt buffers modest — heap is very tight on the C2 (~13 KB free) and
    // large MQTT buffers starve the HTTP server under load. The publish-burst flap was
    // actually fixed by switching publish() to esp_mqtt_client_enqueue (non-blocking
    // outbox), not by oversizing buffers, so the defaults are fine here.
    mqtt_cfg.buffer.out_size      = 1536;   // outbox headroom for the per-cycle burst
    mqtt_cfg.network.reconnect_timeout_ms = 4000;

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
    getWiFiMAC(mac);
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
        applyBootstrap();   // fresh device: seed broker+enable if the profile bootstraps
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
    applyBootstrap();   // seed broker+enable if NVS left it unconfigured (C2-MQTT)
    return true;
}

// Headless C2-MQTT has no HTTP UI to set the broker, so when NVS is unconfigured
// (fresh device, or a board provisioned under a non-MQTT profile) seed the broker
// from the build-time bootstrap and enable MQTT. NVS always wins when it has a
// broker; if both NVS and the bootstrap broker are empty, MQTT stays disabled
// (safe-idle). docs/18 §5. No-op unless the build sets ACROUTER_MQTT_BOOTSTRAP.
void MQTTManager::applyBootstrap() {
#if CONFIG_ACROUTER_MQTT_BOOTSTRAP
    if (_config.broker[0] != '\0') {
        return;  // NVS already carries a broker — respect it
    }
    strlcpy(_config.broker, CONFIG_ACROUTER_MQTT_BOOTSTRAP_BROKER, sizeof(_config.broker));
    if (_config.broker[0] != '\0') {
        _config.enabled = true;
        ESP_LOGW(TAG, "MQTT bootstrap: NVS unconfigured -> broker=%s, MQTT enabled",
                 _config.broker);
    } else {
        ESP_LOGW(TAG, "MQTT bootstrap set but broker empty -> staying disabled (safe-idle)");
    }
#endif
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

    // Use enqueue (non-blocking, store=true) rather than the synchronous publish: the
    // hub emits a burst (~25 topics) each status cycle, and esp_mqtt_client_publish
    // writes qos0 to the socket in-line — the burst overran the C2 TCP send buffer
    // (errno=11 EAGAIN) and esp-mqtt dropped into a reconnect loop. Enqueue queues to
    // the outbox and the MQTT task drains it at the link's pace.
    int msg_id = esp_mqtt_client_enqueue(_client, topic, payload, 0, qos, retain ? 1 : 0, true);
    if (msg_id >= 0) {
        _messagesPublished++;
        return true;
    }

    ESP_LOGW(TAG, "Publish enqueue failed (outbox full?): %s", topic);
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

    static const char* modeNames[] = {"off", "auto", "eco", "offgrid", "manual", "boost", "grid_limit"};
    static const char* stateNames[] = {"idle", "increasing", "decreasing", "at_max", "at_min", "error"};
    uint8_t modeIdx = static_cast<uint8_t>(status.mode);
    uint8_t stateIdx = static_cast<uint8_t>(status.state);

    // Per-topic scalars are for Home Assistant entities; the dashboard consumes the
    // json aggregate below. Publish them only when HA discovery is on, so the default
    // (dashboard) cycle stays a handful of messages the C2 link can drain (see the
    // ha_discovery note) — otherwise the QoS1 burst starves inbound commands.
    if (_config.ha_discovery) {
        if (modeIdx < 7) {
            publish(buildTopic("status", "mode").c_str(), modeNames[modeIdx], true, 1);
        }
        if (stateIdx < 6) {
            publish(buildTopic("status", "state").c_str(), stateNames[stateIdx], true, 1);
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", status.dimmer_percent);
        publish(buildTopic("status", "dimmer").c_str(), buf, true, 1);
        snprintf(buf, sizeof(buf), "%d", getWiFiRSSI());
        publish(buildTopic("status", "wifi_rssi").c_str(), buf, true, 1);
    }

    // Update tracking
    _lastMode = modeIdx;
    _lastState = stateIdx;
    _lastDimmer = status.dimmer_percent;

    if (modeIdx >= 7) modeIdx = 0;
    if (stateIdx >= 6) stateIdx = 0;

    // JSON aggregated status
    JsonDocument doc;
    doc["mode"] = modeNames[modeIdx];
    doc["state"] = stateNames[stateIdx];
    doc["dimmer"] = status.dimmer_percent;
    doc["wifi_rssi"] = getWiFiRSSI();
    doc["valid"] = status.valid;

    String json;
    serializeJson(doc, json);
    publish(buildTopic("json", "status").c_str(), json.c_str(), true, 1);
}

// ============================================================================
// Publishing - Dimmers Status
// ============================================================================

void MQTTManager::publishDimmersStatus() {
    if (!_connected) return;
    if (!dimmer_manager_is_initialized()) return;

    char buf[32];
    char topicName[32];

    // Per-dimmer scalar topics are HA-entity state; gated on ha_discovery so the
    // default dashboard cycle stays light (json/dimmers below carries the same data).
    if (_config.ha_discovery) {
        // DimmerLink outputs only (I2C 4-11 + ESP-NOW 12+). v2.0: legacy GPIO dimming gone.
        for (uint8_t i = DIMMER_I2C_START; i < DIMMER_ESPNOW_END; i++) {
            dimmer_status_t status;
            if (dimmer_get_status(i, &status) != ESP_OK) continue;
            if (status.type == DIMMER_TYPE_NONE || !status.enabled) continue;

            uint8_t level = status.level_percent;

            // Only publish if changed
            if (level != _lastDimmers[i]) {
                _lastDimmers[i] = level;

                // Level topic
                snprintf(topicName, sizeof(topicName), "dimmer/%d", i);
                snprintf(buf, sizeof(buf), "%d", level);
                publish(buildTopic("status", topicName).c_str(), buf, true, 1);

                // State topic (ON/OFF for HA light)
                snprintf(topicName, sizeof(topicName), "dimmer/%d/state", i);
                publish(buildTopic("status", topicName).c_str(), level > 0 ? "ON" : "OFF", true, 1);
            }

            // Priority topic (always publish for HA number entity state)
            snprintf(topicName, sizeof(topicName), "dimmer/%d/priority", i);
            snprintf(buf, sizeof(buf), "%d", dimmer_get_priority(i));
            publish(buildTopic("status", topicName).c_str(), buf, true, 1);
        }
    }

    // JSON aggregated dimmers
    JsonDocument doc;
    JsonArray dimmers = doc["dimmers"].to<JsonArray>();

    // DimmerLink outputs only (I2C 4-11 + ESP-NOW 12+). v2.0: legacy GPIO dimming gone.
    for (uint8_t i = DIMMER_I2C_START; i < DIMMER_ESPNOW_END; i++) {
        dimmer_status_t status;
        if (dimmer_get_status(i, &status) != ESP_OK) continue;
        if (status.type == DIMMER_TYPE_NONE || !status.enabled) continue;

        JsonObject obj = dimmers.add<JsonObject>();
        obj["id"] = i;
        obj["type"] = (status.type == DIMMER_TYPE_ESPNOW) ? "espnow" : "i2c";
        obj["enabled"] = status.enabled;
        obj["level"] = status.level_percent;
        obj["name"] = status.name;
        obj["priority"] = dimmer_get_priority(i);
        obj["state"] = status.level_percent > 0 ? "on" : "off";
    }

    String json;
    serializeJson(doc, json);
    publish(buildTopic("json", "dimmers").c_str(), json.c_str(), true, 1);
}

// ============================================================================
// Publishing - Relays Status
// ============================================================================

void MQTTManager::publishRelaysStatus() {
    if (!_connected) return;

    char topicName[32];

    // Per-relay scalar topics are HA-entity state; gated on ha_discovery (json/relays
    // below carries the same data for the dashboard).
    if (_config.ha_discovery) {
        for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
            if (!relay_is_enabled(i)) continue;

            bool isOn = relay_is_on(i);

            // Only publish if changed (-1 sentinel forces the first publish on connect)
            if ((int8_t)isOn != _lastRelays[i]) {
                _lastRelays[i] = (int8_t)isOn;

                snprintf(topicName, sizeof(topicName), "relay/%d", i);
                publish(buildTopic("status", topicName).c_str(), isOn ? "ON" : "OFF", true, 1);
            }

            // Priority topic (always publish for HA number entity state)
            char buf[8];
            snprintf(topicName, sizeof(topicName), "relay/%d/priority", i);
            snprintf(buf, sizeof(buf), "%d", relay_get_priority(i));
            publish(buildTopic("status", topicName).c_str(), buf, true, 1);
        }
    }

    // JSON aggregated relays
    JsonDocument doc;
    JsonArray relays = doc["relays"].to<JsonArray>();

    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        relay_status_t status;
        if (relay_get_status(i, &status) != ESP_OK) {
            continue;
        }

        // Skip unconfigured relays
        if (status.type == RELAY_TYPE_NONE && !status.enabled) {
            continue;
        }

        JsonObject obj = relays.add<JsonObject>();
        obj["id"] = i;
        obj["enabled"] = status.enabled;
        obj["state"] = status.is_on ? "on" : "off";
        obj["name"] = status.name;
        obj["priority"] = relay_get_priority(i);
        if (status.enabled) {
            obj["power"] = status.nominal_power_w;
            obj["debounce_active"] = (status.state == RELAY_STATE_DEBOUNCE);
        }
    }

    String json;
    serializeJson(doc, json);
    publish(buildTopic("json", "relays").c_str(), json.c_str(), true, 1);
}

// ============================================================================
// Publishing - Metrics
// ============================================================================

// Mains frequency + grid power factor come per-module from the smart sources
// (not carried in the merged Sensor Hub slots). System frequency = any online
// module's frequency; grid PF = the grid-role module's, else any online module.
// Returns false when no module reports the value (feature off / no modules).
static bool readModuleFreqPf(float* freq_hz, float* grid_pf) {
    bool got_f = false, got_pf = false;
    float f = 0.0f, pf = 0.0f;
#if CONFIG_ACROUTER_RBAMP_SOURCE
    {
        rbamp_source_module_info_t mods[4];
        size_t n = 0;
        rbamp_source_get_modules(mods, 4, &n);
        for (size_t i = 0; i < n; i++) {
            if (!mods[i].online) continue;
            if (!got_f && mods[i].frequency > 0.0f) { f = mods[i].frequency; got_f = true; }
            if (mods[i].role == RBAMP_ROLE_GRID) { pf = mods[i].power_factor; got_pf = true; }
            else if (!got_pf) { pf = mods[i].power_factor; got_pf = true; }
        }
    }
#endif
#if CONFIG_ACROUTER_ESPNOW_SOURCE
    if (!got_f || !got_pf) {
        esp_now_source_node_info_t nd[4];
        size_t n = 0;
        esp_now_source_get_nodes(nd, 4, &n);
        for (size_t i = 0; i < n; i++) {
            if (!nd[i].online) continue;
            if (!got_f && nd[i].frequency > 0.0f) { f = nd[i].frequency; got_f = true; }
            if (!got_pf) { pf = nd[i].power_factor; got_pf = true; }
        }
    }
#endif
    if (freq_hz) *freq_hz = f;
    if (grid_pf) *grid_pf = pf;
    return got_f || got_pf;
}

void MQTTManager::publishMetrics() {
    if (!_connected) return;

    // Source-agnostic: read merged measurements from the Sensor Hub, so metrics
    // publish regardless of which source (rbAmp I2C / ESP-NOW) provided the data.
    sensor_hub_state_t hub;
    sensor_hub_get_state(&hub);
    const sh_slot_state_t& sv = hub.slots[SH_SLOT_VOLTAGE];
    const sh_slot_state_t& sg = hub.slots[SH_SLOT_GRID];
    const sh_slot_state_t& ss = hub.slots[SH_SLOT_SOLAR];
    const sh_slot_state_t& sld = hub.slots[SH_SLOT_LOAD];

    char buf[32];

    // Voltage
    if (sv.valid) {
        snprintf(buf, sizeof(buf), "%.1f", sv.value);
        publish(buildTopic("metrics", "voltage").c_str(), buf, false, 0);
    }

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

    // Currents (from Sensor Hub slots)
    if (sg.valid) {
        snprintf(buf, sizeof(buf), "%.2f", sg.value);
        publish(buildTopic("metrics", "current_grid").c_str(), buf, false, 0);
    }
    if (ss.valid) {
        snprintf(buf, sizeof(buf), "%.2f", ss.value);
        publish(buildTopic("metrics", "current_solar").c_str(), buf, false, 0);
    }
    if (sld.valid) {
        snprintf(buf, sizeof(buf), "%.2f", sld.value);
        publish(buildTopic("metrics", "current_load").c_str(), buf, false, 0);
    }

    // Direction (grid slot) — only when the grid slot actually has data,
    // else a fabricated "consuming" (enum default 0) would be published.
    static const char* dirNames[] = {"consuming", "supplying", "balanced", "unknown"};
    uint8_t dirIdx = static_cast<uint8_t>(sg.direction);
    bool dir_ok = sg.valid && dirIdx < 4;
    if (dir_ok) {
        publish(buildTopic("metrics", "direction").c_str(), dirNames[dirIdx], false, 0);
    }

    // Mains frequency + grid power factor (per-module, from smart sources).
    // Both can be NaN even on an online current-only module (no AC ref), so
    // guard against publishing "nan" to a numeric HA sensor.
    float freq_hz = 0.0f, grid_pf = 0.0f;
    bool has_fpf = readModuleFreqPf(&freq_hz, &grid_pf);
    bool freq_ok = has_fpf && !isnan(freq_hz) && freq_hz > 0.0f;
    bool pf_ok   = has_fpf && !isnan(grid_pf);
    if (freq_ok) {
        snprintf(buf, sizeof(buf), "%.2f", freq_hz);
        publish(buildTopic("metrics", "frequency").c_str(), buf, false, 0);
    }
    if (pf_ok) {
        snprintf(buf, sizeof(buf), "%.3f", grid_pf);
        publish(buildTopic("metrics", "power_factor").c_str(), buf, false, 0);
    }

    // JSON aggregated metrics
    JsonDocument doc;
    if (sv.valid) doc["voltage"] = sv.value;
    if (_router) {
        const RouterStatus& status = _router->getStatus();
        doc["power_grid"] = status.power_grid;
        doc["power_solar"] = status.power_solar;
        doc["power_load"] = status.power_load;
    }
    if (sg.valid)  doc["current_grid"]  = sg.value;
    if (ss.valid)  doc["current_solar"] = ss.value;
    if (sld.valid) doc["current_load"]  = sld.value;
    if (dir_ok) doc["direction"] = dirNames[dirIdx];
    if (freq_ok) doc["frequency"] = freq_hz;
    if (pf_ok) doc["power_factor"] = grid_pf;

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
    publish(buildTopic("system", "ip").c_str(), getWiFiLocalIP().c_str(), true, 1);

    // MAC address
    publish(buildTopic("system", "mac").c_str(), getWiFiMACString().c_str(), true, 1);

    // Uptime
    snprintf(buf, sizeof(buf), "%lu", millis() / 1000);
    publish(buildTopic("system", "uptime").c_str(), buf, true, 1);

    // Free heap
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)ESP.getFreeHeap());
    publish(buildTopic("system", "free_heap").c_str(), buf, true, 1);

    // JSON aggregated system info
    JsonDocument doc;
    doc["version"] = app_desc->version;
    doc["ip"] = getWiFiLocalIP();
    doc["mac"] = getWiFiMACString();
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
    publishDimmersStatus();
    publishRelaysStatus();
    publishMetrics();
    publishConfig();
    publishConfigState();  // retained whole-config on connect, so a Remote-UI client
                           // reads the current config immediately without a config/set
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
    doc["configuration_url"] = String("http://") + getWiFiLocalIP();

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
    device["configuration_url"] = String("http://") + getWiFiLocalIP();

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

void MQTTManager::publishHASwitch(const char* name, const char* uniqueId, const char* stateTopic,
                                   const char* commandTopic) {
    String discoveryTopic = String("homeassistant/switch/acrouter_") + _config.device_id + "/" + uniqueId + "/config";

    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = String("acrouter_") + _config.device_id + "_" + uniqueId;
    doc["state_topic"] = stateTopic;
    doc["command_topic"] = commandTopic;
    doc["availability_topic"] = buildTopic("status", "online");
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
    doc["state_on"] = "ON";
    doc["state_off"] = "OFF";

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

void MQTTManager::publishHALight(const char* name, const char* uniqueId, const char* stateTopic,
                                  const char* brightnessStateTopic, const char* commandTopic,
                                  const char* brightnessCommandTopic) {
    String discoveryTopic = String("homeassistant/light/acrouter_") + _config.device_id + "/" + uniqueId + "/config";

    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = String("acrouter_") + _config.device_id + "_" + uniqueId;
    doc["state_topic"] = stateTopic;
    doc["command_topic"] = commandTopic;
    doc["brightness_state_topic"] = brightnessStateTopic;
    doc["brightness_command_topic"] = brightnessCommandTopic;
    doc["availability_topic"] = buildTopic("status", "online");
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
    doc["state_value_template"] = "{{ value }}";
    doc["brightness_scale"] = 100;
    doc["on_command_type"] = "brightness";

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

    // Per-role currents (from smart modules via Sensor Hub)
    publishHASensor("Grid Current", "current_grid",
                    buildTopic("metrics", "current_grid").c_str(),
                    "A", "current", "measurement");
    publishHASensor("Solar Current", "current_solar",
                    buildTopic("metrics", "current_solar").c_str(),
                    "A", "current", "measurement");
    publishHASensor("Load Current", "current_load",
                    buildTopic("metrics", "current_load").c_str(),
                    "A", "current", "measurement");

    // Mains frequency + grid power factor (smart-module measurements)
    publishHASensor("Frequency", "frequency",
                    buildTopic("metrics", "frequency").c_str(),
                    "Hz", "frequency", "measurement");
    publishHASensor("Power Factor", "power_factor",
                    buildTopic("metrics", "power_factor").c_str(),
                    nullptr, "power_factor", "measurement");

    publishHASensor("Dimmer Level", "dimmer",
                    buildTopic("status", "dimmer").c_str(),
                    "%", nullptr, "measurement");

    publishHASensor("WiFi Signal", "wifi_rssi",
                    buildTopic("status", "wifi_rssi").c_str(),
                    "dBm", "signal_strength", "measurement");

    // Mode select
    static const char* modeOptions[] = {"off", "auto", "eco", "offgrid", "manual", "boost", "grid_limit"};
    publishHASelect("Router Mode", "mode",
                    buildTopic("status", "mode").c_str(),
                    buildTopic("command", "mode").c_str(),
                    modeOptions, 7);

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

    // Register enabled dimmers as HA lights
    if (dimmer_manager_is_initialized()) {
        uint8_t registered = 0;
        // v2.0 dimmers are DimmerLink I2C (4-11) + ESP-NOW (12+); the legacy GPIO
        // range (0-3) is always TYPE_NONE. Iterate the real range (matches
        // publishDimmersStatus) so DimmerLink outputs get HA light entities (#32).
        for (uint8_t i = DIMMER_I2C_START; i < DIMMER_ESPNOW_END; i++) {
            dimmer_status_t status;
            if (dimmer_get_status(i, &status) != ESP_OK) continue;
            if (!status.enabled) continue;

            char uniqueId[32];
            char stateTopic[64];
            char brightnessTopic[64];
            char commandTopic[64];
            char brightnessCommandTopic[64];
            char displayName[48];

            snprintf(uniqueId, sizeof(uniqueId), "dimmer_%d", i);
            snprintf(stateTopic, sizeof(stateTopic), "%s/status/dimmer/%d/state",
                     _topicPrefix.c_str(), i);
            snprintf(brightnessTopic, sizeof(brightnessTopic), "%s/status/dimmer/%d",
                     _topicPrefix.c_str(), i);
            snprintf(commandTopic, sizeof(commandTopic), "%s/command/dimmer/%d",
                     _topicPrefix.c_str(), i);
            snprintf(brightnessCommandTopic, sizeof(brightnessCommandTopic), "%s/command/dimmer/%d/brightness",
                     _topicPrefix.c_str(), i);

            // Use custom name or default
            if (strlen(status.name) > 0) {
                snprintf(displayName, sizeof(displayName), "%s", status.name);
            } else {
                snprintf(displayName, sizeof(displayName), "Dimmer %d", i + 1);
            }

            publishHALight(displayName, uniqueId, stateTopic, brightnessTopic,
                          commandTopic, brightnessCommandTopic);
            registered++;
        }
        ESP_LOGI(TAG, "Registered %d dimmers with Home Assistant", registered);
    }

    // Register enabled relays as HA switches (using new C API)
    {
        uint8_t registered = 0;
        for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
            if (!relay_is_enabled(i)) continue;

            relay_status_t status;
            if (relay_get_status(i, &status) != ESP_OK) continue;

            char uniqueId[32];
            char stateTopic[64];
            char commandTopic[64];
            char displayName[48];

            snprintf(uniqueId, sizeof(uniqueId), "relay_%d", i);
            snprintf(stateTopic, sizeof(stateTopic), "%s/status/relay/%d",
                     _topicPrefix.c_str(), i);
            snprintf(commandTopic, sizeof(commandTopic), "%s/command/relay/%d",
                     _topicPrefix.c_str(), i);

            // Use custom name or default
            if (strlen(status.name) > 0) {
                snprintf(displayName, sizeof(displayName), "%s", status.name);
            } else {
                snprintf(displayName, sizeof(displayName), "Relay %d", i + 1);
            }

            publishHASwitch(displayName, uniqueId, stateTopic, commandTopic);
            registered++;
        }
        ESP_LOGI(TAG, "Registered %d relays with Home Assistant", registered);
    }

    // Register priority number entities for dimmers
    if (dimmer_manager_is_initialized()) {
        uint8_t registered = 0;
        // Real dimmer range (DimmerLink I2C + ESP-NOW), not the dead GPIO 0-3 (#32).
        for (uint8_t i = DIMMER_I2C_START; i < DIMMER_ESPNOW_END; i++) {
            dimmer_status_t status;
            if (dimmer_get_status(i, &status) != ESP_OK) continue;
            if (!status.enabled) continue;

            char uniqueId[32];
            char stateTopic[64];
            char commandTopic[64];
            char displayName[48];

            snprintf(uniqueId, sizeof(uniqueId), "dimmer_%d_priority", i);
            snprintf(stateTopic, sizeof(stateTopic), "%s/status/dimmer/%d/priority",
                     _topicPrefix.c_str(), i);
            snprintf(commandTopic, sizeof(commandTopic), "%s/command/dimmer/%d/priority",
                     _topicPrefix.c_str(), i);

            // Use custom name or default
            if (strlen(status.name) > 0) {
                snprintf(displayName, sizeof(displayName), "%s Priority", status.name);
            } else {
                snprintf(displayName, sizeof(displayName), "Dimmer %d Priority", i + 1);
            }

            publishHANumber(displayName, uniqueId, stateTopic, commandTopic, 0, 255, 1);

            // Publish current priority value
            char priorityValue[4];
            snprintf(priorityValue, sizeof(priorityValue), "%d", dimmer_get_priority(i));
            publish(stateTopic, priorityValue, true, 1);

            registered++;
        }
        ESP_LOGI(TAG, "Registered %d dimmer priority controls with Home Assistant", registered);
    }

    // Register priority number entities for relays
    {
        uint8_t registered = 0;
        for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
            if (!relay_is_enabled(i)) continue;

            relay_status_t status;
            if (relay_get_status(i, &status) != ESP_OK) continue;

            char uniqueId[32];
            char stateTopic[64];
            char commandTopic[64];
            char displayName[48];

            snprintf(uniqueId, sizeof(uniqueId), "relay_%d_priority", i);
            snprintf(stateTopic, sizeof(stateTopic), "%s/status/relay/%d/priority",
                     _topicPrefix.c_str(), i);
            snprintf(commandTopic, sizeof(commandTopic), "%s/command/relay/%d/priority",
                     _topicPrefix.c_str(), i);

            // Use custom name or default
            if (strlen(status.name) > 0) {
                snprintf(displayName, sizeof(displayName), "%s Priority", status.name);
            } else {
                snprintf(displayName, sizeof(displayName), "Relay %d Priority", i + 1);
            }

            publishHANumber(displayName, uniqueId, stateTopic, commandTopic, 0, 255, 1);

            // Publish current priority value
            char priorityValue[4];
            snprintf(priorityValue, sizeof(priorityValue), "%d", relay_get_priority(i));
            publish(stateTopic, priorityValue, true, 1);

            registered++;
        }
        ESP_LOGI(TAG, "Registered %d relay priority controls with Home Assistant", registered);
    }

    ESP_LOGI(TAG, "Home Assistant discovery published");
}

// ============================================================================
// Subscriptions & Message Handling
// ============================================================================

void MQTTManager::setupSubscriptions() {
    // Subscribe to commands
    String topic = _topicPrefix + "/command/#";
    subscribe(topic.c_str(), 1);

    // Subscribe to config set requests (per-value HA number entities)
    topic = _topicPrefix + "/config/+/set";
    subscribe(topic.c_str(), 1);

    // §7.1 config-over-MQTT: whole-config provisioning (docs/18) — the C2-MQTT profile
    // has no HTTP config UI, so roles/addresses/control/dimmers arrive here.
    topic = _topicPrefix + "/config/set";
    subscribe(topic.c_str(), 1);
    topic = _topicPrefix + "/config/get";
    subscribe(topic.c_str(), 1);
}

void MQTTManager::handleMessage(const char* topic, const char* payload, int len) {
    _messagesReceived++;

    // §7.1 config-over-MQTT — handle the (possibly large) whole-config blob straight from
    // the RAW payload, BEFORE the 256B truncation below. Exact-topic match so it doesn't
    // collide with the per-value config/<name>/set path.
    {
        String t = String(topic);
        if (t == _topicPrefix + "/config/set") { handleConfigBlob(payload, len); return; }
        if (t == _topicPrefix + "/config/get") { publishConfigState(); return; }
    }

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
        else if (strcmp(payload, "grid_limit") == 0) mode = RouterMode::GRID_LIMIT;
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
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    else if (strcmp(command, "refresh") == 0) {
        publishAll();
    }
    // Handle dimmer commands: dimmer/{id} or dimmer/{id}/brightness or dimmer/{id}/priority
    else if (strncmp(command, "dimmer/", 7) == 0) {
        const char* rest = command + 7;
        int id = atoi(rest);
        // Accept the full dimmer id space (DimmerLink I2C 4-11 + ESP-NOW 12+), not the
        // dead legacy GPIO range 0-3; the handler checks enabled/type per id (#32).
        if (id >= 0 && id < (int)DIMMER_MAX_COUNT) {
            // Check if it's a brightness command
            const char* slash = strchr(rest, '/');
            if (slash && strcmp(slash, "/brightness") == 0) {
                // Brightness set command
                handleDimmerCommand(id, payload);
            } else if (slash && strcmp(slash, "/priority") == 0) {
                // Priority set command
                int priority = atoi(payload);
                if (priority >= 0 && priority <= 255) {
                    dimmer_set_priority(id, priority);
                    dimmer_save_config(id);  // Save to NVS
                    ESP_LOGI(TAG, "Dimmer %d priority set to %d (saved to NVS)", id, priority);
                    publishDimmersStatus();
                    // Refresh RouterController priority map
                    if (_router) {
                        _router->refreshPriorityMap();
                    }
                } else {
                    ESP_LOGW(TAG, "Invalid priority value: %d (must be 0-255)", priority);
                }
            } else {
                // ON/OFF command
                handleDimmerCommand(id, payload);
            }
        }
    }
    // Handle relay commands: relay/{id} or relay/{id}/priority
    else if (strncmp(command, "relay/", 6) == 0) {
        const char* rest = command + 6;
        int id = atoi(rest);
        if (id >= 0 && id < RELAY_MAX_COUNT) {
            // Check if it's a priority command
            const char* slash = strchr(rest, '/');
            if (slash && strcmp(slash, "/priority") == 0) {
                // Priority set command
                int priority = atoi(payload);
                if (priority >= 0 && priority <= 255) {
                    relay_set_priority(id, priority);
                    relay_save_config(id);  // Save to NVS
                    ESP_LOGI(TAG, "Relay %d priority set to %d (saved to NVS)", id, priority);
                    publishRelaysStatus();
                    // Refresh RouterController priority map
                    if (_router) {
                        _router->refreshPriorityMap();
                    }
                } else {
                    ESP_LOGW(TAG, "Invalid priority value: %d (must be 0-255)", priority);
                }
            } else {
                // ON/OFF/TOGGLE command
                handleRelayCommand(id, payload);
            }
        }
    }
}

void MQTTManager::handleDimmerCommand(uint8_t id, const char* payload) {
    if (!dimmer_manager_is_initialized()) return;
    if (!dimmer_is_enabled(id)) {
        ESP_LOGW(TAG, "Dimmer %d not available", id);
        return;
    }

    // Handle ON/OFF
    if (strcasecmp(payload, "ON") == 0) {
        // Turn on to last level or 100%
        uint8_t current = dimmer_get_level(id);
        uint8_t level = current > 0 ? current : 100;
        dimmer_set_level(id, level);
        ESP_LOGI(TAG, "Dimmer %d turned ON to %d%%", id, level);
    }
    else if (strcasecmp(payload, "OFF") == 0) {
        dimmer_set_level(id, 0);
        ESP_LOGI(TAG, "Dimmer %d turned OFF", id);
    }
    else {
        // Numeric value = brightness percentage
        int level = atoi(payload);
        if (level >= 0 && level <= 100) {
            dimmer_set_level(id, level);
            ESP_LOGI(TAG, "Dimmer %d set to %d%%", id, level);
        }
    }

    // Force publish update
    _lastDimmers[id] = 255;  // Reset to force publish
    publishDimmersStatus();
}

void MQTTManager::handleRelayCommand(uint8_t id, const char* payload) {
    if (id >= RELAY_MAX_COUNT) {
        ESP_LOGW(TAG, "Invalid relay ID: %d", id);
        return;
    }

    if (!relay_is_enabled(id)) {
        ESP_LOGW(TAG, "Relay %d not available", id);
        return;
    }

    if (strcasecmp(payload, "ON") == 0) {
        esp_err_t result = relay_turn_on(id, false);  // respect debounce
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Relay %d turned ON", id);
        } else {
            ESP_LOGW(TAG, "Relay %d cannot turn ON (debounce)", id);
        }
    }
    else if (strcasecmp(payload, "OFF") == 0) {
        esp_err_t result = relay_turn_off(id, false);  // respect debounce
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Relay %d turned OFF", id);
        } else {
            ESP_LOGW(TAG, "Relay %d cannot turn OFF (debounce)", id);
        }
    }
    else if (strcasecmp(payload, "TOGGLE") == 0) {
        esp_err_t result = relay_toggle(id, false);  // respect debounce
        if (result == ESP_OK) {
            bool is_on = relay_is_on(id);
            ESP_LOGI(TAG, "Relay %d toggled to %s", id, is_on ? "ON" : "OFF");
        }
    }

    // Force publish update
    _lastRelays[id] = !relay_is_on(id);  // Reset to force publish
    publishRelaysStatus();
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
            if (threshold >= 0 && threshold <= 100) {   // unified with ConfigManager/REST (0-100)
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

void MQTTManager::handleConfigBlob(const char* payload, int len) {
    JsonDocument doc;
    DeserializationError jerr = deserializeJson(doc, payload, len);
    if (jerr) {
        ESP_LOGW(TAG, "config/set: JSON parse failed (%s) — payload fragmented or too large?",
                 jerr.c_str());
        return;
    }
    ESP_LOGI(TAG, "config/set: applying whole-config blob (%d bytes)", len);

    // control{} — apply live (_router) + persist (_config).
    if (doc["control"].is<JsonObject>() && _router && _configMgr) {
        JsonObject c = doc["control"];
        if (!c["control_gain"].isNull()) {
            float g = c["control_gain"].as<float>();
            _configMgr->setControlGain(g); _router->setControlGain(g);
        }
        if (!c["balance_threshold"].isNull()) {
            float t = c["balance_threshold"].as<float>();
            _configMgr->setBalanceThreshold(t); _router->setBalanceThreshold(t);
        }
        if (!c["grid_current_limit_a"].isNull()) {
            float a = c["grid_current_limit_a"].as<float>();
            _configMgr->setGridCurrentLimit(a); _router->setGridCurrentLimit(a);
        }
    }

    // modules[] — role + name persist to NVS via devreg. ct_model: TODO v1.1 (CT catalog code lookup).
    if (doc["modules"].is<JsonArray>()) {
        for (JsonObject m : doc["modules"].as<JsonArray>()) {
            uint8_t addr = m["addr"].is<const char*>()
                             ? (uint8_t)strtol(m["addr"].as<const char*>(), nullptr, 16)
                             : (uint8_t)(m["addr"] | 0);
            uint8_t ch = m["channel"] | 0;
            if (!m["role"].isNull()) {
                devreg_set_role(0, addr, ch, device_role_parse(m["role"] | "none"));
            }
            if (m["name"].is<const char*>()) {
                devreg_set_name(0, addr, ch, m["name"].as<const char*>());
            }
        }
    }

    // dimmers[] — priority/power/name; feeds the RouterController priority map.
    if (doc["dimmers"].is<JsonArray>()) {
        bool touched = false;
        for (JsonObject d : doc["dimmers"].as<JsonArray>()) {
            if (d["id"].isNull()) continue;
            uint8_t id = d["id"].as<uint8_t>();
            if (!d["priority"].isNull())        { dimmer_set_priority(id, d["priority"].as<uint8_t>());        touched = true; }
            if (!d["nominal_power_w"].isNull()) { dimmer_set_nominal_power(id, d["nominal_power_w"].as<uint16_t>()); touched = true; }
            if (d["name"].is<const char*>())    { dimmer_set_name(id, d["name"].as<const char*>());            touched = true; }
        }
        if (touched && _router) _router->refreshPriorityMap();
    }

    publishConfig();       // refresh the per-value HA config topics (control_gain, ...)
    publishConfigState();  // §7.1 whole-config state (retained)
}

void MQTTManager::publishConfigState() {
    JsonDocument doc;
    if (_configMgr) {
        JsonObject c = doc["control"].to<JsonObject>();
        c["control_gain"]         = _configMgr->getControlGain();
        c["balance_threshold"]    = _configMgr->getBalanceThreshold();
        c["grid_current_limit_a"] = _configMgr->getGridCurrentLimit();
    }
    // v1.1: also emit modules[]/dimmers[] current state (role/addr/priority) once the
    // devreg/dimmer iteration is wired — the control roundtrip is enough for the first e2e.
    String json;
    serializeJson(doc, json);
    publish(buildTopic("config", "state").c_str(), json.c_str(), true, 1);
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

            // Setup subscriptions (enqueues SUBSCRIBE — safe from the event task).
            setupSubscriptions();

            // Do NOT publishAll() here: this callback runs in the esp-mqtt task, and
            // enqueuing the full ~25-topic burst from inside it stalls that task so it
            // stops servicing inbound (commands never reach MQTT_EVENT_DATA) and keep-
            // alive. Defer the initial publish to loop() (main task, off-callback).
            _pendingInitialPublish = true;
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from broker");
            _connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "SUBACK received, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGD(TAG, "Unsubscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "Published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA: {
            ESP_LOGD(TAG, "DATA event: topic_len=%d data_len=%d", event->topic_len, event->data_len);
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
