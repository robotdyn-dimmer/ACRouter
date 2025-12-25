#include "WebServerManager.h"
#include <ArduinoJson.h>
#include "ConfigManager.h"
#include "HardwareConfigManager.h"
#include "OTAManager.h"
#include "GitHubOTAChecker.h"
#include "MQTTManager.h"
#include <esp_app_desc.h>

// New Material UI Web Interface
#include "../web/styles/MaterialStyles.h"
#include "../web/components/Layout.h"
#include "../web/pages/DashboardPage.h"
#include "../web/pages/WiFiConfigPage.h"
#include "../web/pages/HardwareConfigPage.h"
#include "../web/pages/OTAPage.h"
#include "../web/pages/MQTTPage.h"

static const char* TAG = "WebServer";

// Singleton instance
WebServerManager& WebServerManager::getInstance() {
    static WebServerManager instance;
    return instance;
}

WebServerManager::WebServerManager()
    : _http_server(nullptr)
    , _running(false)
    , _http_port(80)
    , _ws_port(81)
{
}

WebServerManager::~WebServerManager() {
    stop();
}

bool WebServerManager::begin(uint16_t http_port, uint16_t ws_port) {
    if (_running) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }

    _http_port = http_port;
    _ws_port = ws_port;

    // Create HTTP server
    _http_server = new WebServer(_http_port);
    if (!_http_server) {
        ESP_LOGE(TAG, "Failed to create HTTP server");
        return false;
    }

    // Setup routes
    setupRoutes();

    // Start server
    _http_server->begin();

    // Initialize OTA Manager
    OTAManager& ota = OTAManager::getInstance();
    if (!ota.begin(_http_server)) {
        ESP_LOGE(TAG, "Failed to initialize OTA Manager");
    } else {
        ESP_LOGI(TAG, "OTA Manager initialized");
    }

    _running = true;
    ESP_LOGI(TAG, "Started - HTTP:%d", _http_port);

    return true;
}

void WebServerManager::stop() {
    if (!_running) return;

    if (_http_server) {
        _http_server->stop();
        delete _http_server;
        _http_server = nullptr;
    }

    _running = false;
    ESP_LOGI(TAG, "Stopped");
}

void WebServerManager::handle() {
    if (!_running) return;

    _http_server->handleClient();
}

void WebServerManager::setupRoutes() {
    // ========================================
    // Web Pages (Material UI)
    // ========================================

    // Dashboard (root)
    _http_server->on("/", HTTP_GET, [this]() { handleDashboard(); });
    _http_server->on("/index.html", HTTP_GET, [this]() { handleDashboard(); });

    // WiFi configuration
    _http_server->on("/wifi", HTTP_GET, [this]() { handleWiFiPage(); });

    // Hardware configuration
    _http_server->on("/settings/hardware", HTTP_GET, [this]() { handleHardwareConfigPage(); });

    // OTA page
    _http_server->on("/ota", HTTP_GET, [this]() { handleOTAPage(); });

    // Static resources
    _http_server->on("/styles.css", HTTP_GET, [this]() {
        _http_server->send_P(200, "text/css", MATERIAL_CSS);
    });

    // ========================================
    // REST API - Status & Info
    // ========================================

    _http_server->on("/api/status", HTTP_GET, [this]() { handleGetStatus(); });
    _http_server->on("/api/metrics", HTTP_GET, [this]() { handleGetMetrics(); });
    _http_server->on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
    _http_server->on("/api/info", HTTP_GET, [this]() { handleGetInfo(); });

    // ========================================
    // REST API - Configuration
    // ========================================

    _http_server->on("/api/config", HTTP_POST, [this]() { handleSetConfig(); });
    _http_server->on("/api/mode", HTTP_POST, [this]() { handleSetMode(); });
    _http_server->on("/api/dimmer", HTTP_POST, [this]() { handleSetDimmer(); });
    _http_server->on("/api/config/reset", HTTP_POST, [this]() { handleResetConfig(); });

    // ========================================
    // REST API - Control
    // ========================================

    _http_server->on("/api/manual", HTTP_POST, [this]() { handleSetManual(); });
    _http_server->on("/api/calibrate", HTTP_POST, [this]() { handleCalibrate(); });

    // ========================================
    // REST API - WiFi Management
    // ========================================

    _http_server->on("/api/wifi/status", HTTP_GET, [this]() { handleWiFiStatus(); });
    _http_server->on("/api/wifi/scan", HTTP_GET, [this]() { handleWiFiScan(); });
    _http_server->on("/api/wifi/connect", HTTP_POST, [this]() { handleWiFiConnect(); });
    _http_server->on("/api/wifi/disconnect", HTTP_POST, [this]() { handleWiFiDisconnect(); });
    _http_server->on("/api/wifi/forget", HTTP_POST, [this]() { handleWiFiForget(); });

    // ========================================
    // REST API - Hardware Configuration
    // ========================================

    _http_server->on("/api/hardware/config", HTTP_GET, [this]() { handleGetHardwareConfig(); });
    _http_server->on("/api/hardware/config", HTTP_POST, [this]() { handleSetHardwareConfig(); });
    _http_server->on("/api/hardware/validate", HTTP_POST, [this]() { handleValidateHardwareConfig(); });
    _http_server->on("/api/hardware/sensor-profiles", HTTP_GET, [this]() { handleGetSensorProfiles(); });

    // ========================================
    // REST API - System Control
    // ========================================

    _http_server->on("/api/system/reboot", HTTP_POST, [this]() { handleSystemReboot(); });

    // ========================================
    // REST API - OTA Management
    // ========================================

    _http_server->on("/api/ota/check-github", HTTP_GET, [this]() { handleOTACheckGitHub(); });
    _http_server->on("/api/ota/update-github", HTTP_POST, [this]() { handleOTAUpdateGitHub(); });

    // ========================================
    // MQTT Configuration Page & API
    // ========================================

    _http_server->on("/mqtt", HTTP_GET, [this]() { handleMQTTPage(); });
    _http_server->on("/api/mqtt/status", HTTP_GET, [this]() { handleGetMQTTStatus(); });
    _http_server->on("/api/mqtt/config", HTTP_GET, [this]() { handleGetMQTTConfig(); });
    _http_server->on("/api/mqtt/config", HTTP_POST, [this]() { handleSetMQTTConfig(); });
    _http_server->on("/api/mqtt/reconnect", HTTP_POST, [this]() { handleMQTTReconnect(); });
    _http_server->on("/api/mqtt/publish", HTTP_POST, [this]() { handleMQTTPublish(); });

    // ========================================
    // Server Configuration
    // ========================================

    // CORS headers
    _http_server->enableCORS(true);

    // 404 handler
    _http_server->onNotFound([this]() {
        sendError(404, "Not Found");
    });
}

// ============================================================================
// API Handlers - Status & Info
// ============================================================================

void WebServerManager::handleGetStatus() {
    String json = buildStatusJson();
    sendJsonResponse(200, json);
}

void WebServerManager::handleGetMetrics() {
    String json = buildMetricsJson();
    sendJsonResponse(200, json);
}

void WebServerManager::handleGetConfig() {
    String json = buildConfigJson();
    sendJsonResponse(200, json);
}

void WebServerManager::handleGetInfo() {
    JsonDocument doc;

    uint32_t uptime_sec = millis() / 1000;

    // Get version from app description (set in CMakeLists.txt)
    const esp_app_desc_t* app_desc = esp_app_get_description();
    doc["version"] = app_desc->version;

    doc["chip"] = "ESP32";
    doc["flash_size"] = ESP.getFlashChipSize();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime"] = uptime_sec;          // Legacy field (deprecated)
    doc["uptime_sec"] = uptime_sec;      // New field for clarity

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

// ============================================================================
// API Handlers - Configuration
// ============================================================================

void WebServerManager::handleSetConfig() {
    if (!_http_server->hasArg("plain")) {
        sendError(400, "Missing request body");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _http_server->arg("plain"));

    if (error) {
        sendError(400, "Invalid JSON");
        return;
    }

    ConfigManager& cfg = ConfigManager::getInstance();
    bool changed = false;

    // Update configuration fields
    if (doc["voltage_coef"].is<float>()) {
        cfg.setVoltageCoef(doc["voltage_coef"]);
        changed = true;
    }
    if (doc["current_coef"].is<float>()) {
        cfg.setCurrentCoef(doc["current_coef"]);
        changed = true;
    }
    if (doc["current_threshold"].is<float>()) {
        cfg.setCurrentThreshold(doc["current_threshold"]);
        changed = true;
    }
    if (doc["power_threshold"].is<float>()) {
        cfg.setPowerThreshold(doc["power_threshold"]);
        changed = true;
    }
    if (doc["control_gain"].is<float>()) {
        cfg.setControlGain(doc["control_gain"]);
        changed = true;
    }
    if (doc["balance_threshold"].is<float>()) {
        cfg.setBalanceThreshold(doc["balance_threshold"]);
        changed = true;
    }

    if (changed) {
        sendSuccess("Configuration updated");
    } else {
        sendSuccess("No changes");
    }
}

void WebServerManager::handleSetMode() {
    if (!_http_server->hasArg("plain")) {
        sendError(400, "Missing request body");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _http_server->arg("plain"));

    if (error) {
        sendError(400, "Invalid JSON");
        return;
    }

    if (!doc["mode"].is<const char*>()) {
        sendError(400, "Missing 'mode' field");
        return;
    }

    String mode = doc["mode"].as<String>();
    RouterController& router = RouterController::getInstance();

    if (mode == "off") {
        router.setMode(RouterMode::OFF);
        sendSuccess("Mode set to OFF");
    } else if (mode == "auto") {
        router.setMode(RouterMode::AUTO);
        sendSuccess("Mode set to AUTO");
    } else if (mode == "eco") {
        router.setMode(RouterMode::ECO);
        sendSuccess("Mode set to ECO");
    } else if (mode == "offgrid") {
        router.setMode(RouterMode::OFFGRID);
        sendSuccess("Mode set to OFFGRID");
    } else if (mode == "manual") {
        router.setMode(RouterMode::MANUAL);
        sendSuccess("Mode set to MANUAL");
    } else if (mode == "boost") {
        router.setMode(RouterMode::BOOST);
        sendSuccess("Mode set to BOOST");
    } else {
        sendError(400, "Invalid mode (use: off, auto, eco, offgrid, manual, boost)");
    }
}

void WebServerManager::handleSetDimmer() {
    if (!_http_server->hasArg("plain")) {
        sendError(400, "Missing request body");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _http_server->arg("plain"));

    if (error) {
        sendError(400, "Invalid JSON");
        return;
    }

    if (!doc["value"].is<int>()) {
        sendError(400, "Missing 'value' field");
        return;
    }

    uint8_t value = doc["value"];
    if (value > 100) {
        sendError(400, "Value must be 0-100");
        return;
    }

    RouterController& router = RouterController::getInstance();
    router.setManualLevel(value);

    sendSuccess("Dimmer value set");
}

void WebServerManager::handleResetConfig() {
    ConfigManager& cfg = ConfigManager::getInstance();
    // Reset to defaults by setting each value
    cfg.setControlGain(ConfigDefaults::CONTROL_GAIN);
    cfg.setBalanceThreshold(ConfigDefaults::BALANCE_THRESHOLD);
    cfg.setVoltageCoef(ConfigDefaults::VOLTAGE_COEF);
    cfg.setCurrentCoef(ConfigDefaults::CURRENT_COEF);
    cfg.setCurrentThreshold(ConfigDefaults::CURRENT_THRESHOLD);
    cfg.setPowerThreshold(ConfigDefaults::POWER_THRESHOLD);

    sendSuccess("Configuration reset to defaults");
}

// ============================================================================
// API Handlers - Control
// ============================================================================

void WebServerManager::handleSetManual() {
    if (!_http_server->hasArg("plain")) {
        sendError(400, "Missing request body");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _http_server->arg("plain"));

    if (error) {
        sendError(400, "Invalid JSON");
        return;
    }

    if (!doc["value"].is<int>()) {
        sendError(400, "Missing 'value' field");
        return;
    }

    uint8_t value = doc["value"];
    if (value > 100) {
        sendError(400, "Value must be 0-100");
        return;
    }

    RouterController& router = RouterController::getInstance();
    router.setMode(RouterMode::MANUAL);
    router.setManualLevel(value);

    sendSuccess("Manual control set");
}

void WebServerManager::handleCalibrate() {
    // TODO: Implement calibration procedure
    sendError(501, "Calibration not implemented yet");
}

// ============================================================================
// JSON Builders
// ============================================================================

String WebServerManager::buildStatusJson() {
    JsonDocument doc;

    RouterController& router = RouterController::getInstance();
    const RouterStatus& st = router.getStatus();

    doc["mode"] = (st.mode == RouterMode::OFF) ? "off" :
                  (st.mode == RouterMode::AUTO) ? "auto" :
                  (st.mode == RouterMode::ECO) ? "eco" :
                  (st.mode == RouterMode::OFFGRID) ? "offgrid" :
                  (st.mode == RouterMode::MANUAL) ? "manual" :
                  (st.mode == RouterMode::BOOST) ? "boost" : "unknown";
    doc["state"] = (st.state == RouterState::IDLE) ? "idle" :
                   (st.state == RouterState::INCREASING) ? "increasing" :
                   (st.state == RouterState::DECREASING) ? "decreasing" :
                   (st.state == RouterState::AT_MAXIMUM) ? "at_max" :
                   (st.state == RouterState::AT_MINIMUM) ? "at_min" : "error";

    doc["power_grid"] = st.power_grid;
    doc["dimmer"] = st.dimmer_percent;
    doc["target_level"] = st.target_level;
    doc["control_gain"] = st.control_gain;
    doc["balance_threshold"] = st.balance_threshold;
    doc["valid"] = st.valid;

    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();

    String json;
    serializeJson(doc, json);
    return json;
}

String WebServerManager::buildMetricsJson() {
    JsonDocument doc;

    RouterController& router = RouterController::getInstance();
    const RouterStatus& st = router.getStatus();

    // Power metrics from all sensors
    JsonObject metrics = doc["metrics"].to<JsonObject>();
    metrics["power_grid"] = st.power_grid;
    metrics["power_solar"] = st.power_solar;
    metrics["power_load"] = st.power_load;

    // Dimmer levels (array for multiple dimmers)
    JsonArray dimmers = doc["dimmers"].to<JsonArray>();
    JsonObject dimmer1 = dimmers.add<JsonObject>();
    dimmer1["id"] = 1;
    dimmer1["level"] = st.dimmer_percent;
    dimmer1["enabled"] = true;

    // Current mode for UI logic
    doc["mode"] = (st.mode == RouterMode::OFF) ? "off" :
                  (st.mode == RouterMode::AUTO) ? "auto" :
                  (st.mode == RouterMode::ECO) ? "eco" :
                  (st.mode == RouterMode::OFFGRID) ? "offgrid" :
                  (st.mode == RouterMode::MANUAL) ? "manual" :
                  (st.mode == RouterMode::BOOST) ? "boost" : "unknown";

    doc["timestamp"] = millis();

    String json;
    serializeJson(doc, json);
    return json;
}

String WebServerManager::buildConfigJson() {
    JsonDocument doc;

    ConfigManager& cfg = ConfigManager::getInstance();
    const SystemConfig& config = cfg.getConfig();

    doc["control_gain"] = config.control_gain;
    doc["balance_threshold"] = config.balance_threshold;
    doc["voltage_coef"] = config.voltage_coef;
    doc["current_coef"] = config.current_coef;
    doc["current_threshold"] = config.current_threshold;
    doc["power_threshold"] = config.power_threshold;
    doc["router_mode"] = config.router_mode;
    doc["manual_level"] = config.manual_level;

    String json;
    serializeJson(doc, json);
    return json;
}

// ============================================================================
// Utilities
// ============================================================================

void WebServerManager::sendJsonResponse(int code, const String& json) {
    _http_server->send(code, "application/json", json);
}

void WebServerManager::sendError(int code, const char* message) {
    JsonDocument doc;
    doc["error"] = message;

    String json;
    serializeJson(doc, json);
    sendJsonResponse(code, json);
}

void WebServerManager::sendSuccess(const char* message) {
    JsonDocument doc;
    doc["success"] = true;
    if (message) {
        doc["message"] = message;
    }

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

// ============================================================================
// WiFi Configuration Handlers
// ============================================================================

void WebServerManager::handleWiFiStatus() {
    WiFiManager& wifi = WiFiManager::getInstance();
    const WiFiStatus& ws = wifi.getStatus();

    JsonDocument doc;

    // State
    doc["state"] = ws.state == WiFiState::IDLE ? "IDLE" :
                   ws.state == WiFiState::AP_ONLY ? "AP_ONLY" :
                   ws.state == WiFiState::STA_CONNECTING ? "STA_CONNECTING" :
                   ws.state == WiFiState::STA_CONNECTED ? "STA_CONNECTED" :
                   ws.state == WiFiState::AP_STA ? "AP_STA" : "STA_FAILED";

    // AP info
    doc["ap_active"] = ws.ap_active;
    if (ws.ap_active) {
        doc["ap_ssid"] = ws.ap_ssid;
        doc["ap_ip"] = ws.ap_ip.toString();
        doc["ap_clients"] = ws.sta_clients;
    }

    // STA info
    doc["sta_connected"] = ws.sta_connected;
    if (ws.sta_connected) {
        doc["sta_ssid"] = ws.sta_ssid;
        doc["sta_ip"] = ws.sta_ip.toString();
        doc["rssi"] = ws.rssi;
    }

    // Credentials
    doc["has_saved_credentials"] = wifi.hasCredentials();
    doc["mac"] = wifi.getMACAddress();
    doc["hostname"] = wifi.getHostname();

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleWiFiScan() {
    JsonDocument doc;

    ESP_LOGI("WebServer", "Starting WiFi scan...");
    int n = WiFi.scanNetworks();

    if (n == 0) {
        doc["networks"] = JsonArray();
        doc["count"] = 0;
    } else {
        JsonArray networks = doc["networks"].to<JsonArray>();

        for (int i = 0; i < n; i++) {
            JsonObject network = networks.add<JsonObject>();
            network["ssid"] = WiFi.SSID(i);
            network["rssi"] = WiFi.RSSI(i);
            network["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "secured";
            network["channel"] = WiFi.channel(i);
        }

        doc["count"] = n;
    }

    WiFi.scanDelete();

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleWiFiConnect() {
    // Parse JSON body
    if (!_http_server->hasArg("plain")) {
        sendError(400, "Missing request body");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _http_server->arg("plain"));

    if (error) {
        sendError(400, "Invalid JSON");
        return;
    }

    // Validate required fields
    if (!doc["ssid"].is<const char*>()) {
        sendError(400, "Missing 'ssid' field");
        return;
    }

    const char* ssid = doc["ssid"].as<const char*>();
    const char* password = doc["password"].is<const char*>() ? doc["password"].as<const char*>() : "";

    ESP_LOGI("WebServer", "WiFi connect request: SSID=%s", ssid);

    // Use WiFiManager to connect
    // WiFiManager will:
    // 1. Disconnect current STA if connected
    // 2. Connect to new network
    // 3. Auto-save credentials to NVS on success
    WiFiManager& wifi = WiFiManager::getInstance();

    if (wifi.connectSTA(ssid, password)) {
        JsonDocument response;
        response["success"] = true;
        response["message"] = "Connecting to WiFi... Credentials will be saved on success";
        response["ssid"] = ssid;

        String json;
        serializeJson(response, json);
        sendJsonResponse(200, json);
    } else {
        sendError(500, "Failed to initiate WiFi connection");
    }
}

void WebServerManager::handleWiFiDisconnect() {
    WiFiManager& wifi = WiFiManager::getInstance();

    ESP_LOGI("WebServer", "WiFi disconnect request");

    // Disconnect STA (does not delete credentials from NVS)
    wifi.disconnectSTA();

    sendSuccess("Disconnected from WiFi network");
}

void WebServerManager::handleWiFiForget() {
    WiFiManager& wifi = WiFiManager::getInstance();

    ESP_LOGI("WebServer", "WiFi forget request");

    // Clear credentials from NVS
    if (wifi.clearCredentials()) {
        sendSuccess("WiFi credentials cleared from NVS");
    } else {
        sendError(500, "Failed to clear WiFi credentials");
    }
}

// ============================================================================
// Web Page Handlers (Material UI)
// ============================================================================

void WebServerManager::handleDashboard() {
    String html = getDashboardPage();
    _http_server->send(200, "text/html", html);
}

void WebServerManager::handleWiFiPage() {
    String html = getWiFiConfigPage();
    _http_server->send(200, "text/html", html);
}

void WebServerManager::handleHardwareConfigPage() {
    String html = getHardwareConfigPage();
    _http_server->send(200, "text/html", html);
}

void WebServerManager::handleOTAPage() {
    String html = getOTAPage();
    _http_server->send(200, "text/html", html);
}

// ============================================================================
// Hardware Configuration API Handlers
// ============================================================================

void WebServerManager::handleGetHardwareConfig() {
    String json = buildHardwareConfigJson();
    sendJsonResponse(200, json);
}

void WebServerManager::handleSetHardwareConfig() {
    if (!_http_server->hasArg("plain")) {
        sendError(400, "Missing request body");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _http_server->arg("plain"));

    if (error) {
        sendError(400, "Invalid JSON");
        return;
    }

    HardwareConfigManager& hw = HardwareConfigManager::getInstance();
    HardwareConfig& config = hw.config();

    // Parse and update ADC channels
    if (doc.containsKey("adc_channels")) {
        JsonArray channels = doc["adc_channels"].as<JsonArray>();

        // IMPORTANT: First, disable ALL channels to prevent stale config
        // Web UI sends only enabled channels, so disabled ones must be cleared
        for (int i = 0; i < 4; i++) {
            config.adc_channels[i].enabled = false;
            config.adc_channels[i].gpio = 0;
            config.adc_channels[i].type = SensorType::NONE;
        }

        // Now update only the channels present in the request
        int idx = 0;
        for (JsonObject ch : channels) {
            if (idx >= 4) break;

            ADCChannelConfig& adc = config.adc_channels[idx];

            // Update GPIO (default 0 means disabled)
            adc.gpio = ch["gpio"] | 0;

            // Update type
            if (ch.containsKey("type")) {
                adc.type = static_cast<SensorType>(ch["type"].as<uint8_t>());
            }

            // Update multiplier with smart defaults based on sensor type
            if (ch.containsKey("multiplier")) {
                adc.multiplier = ch["multiplier"].as<float>();
            } else {
                // Set default multiplier based on sensor type
                switch (adc.type) {
                    case SensorType::VOLTAGE_AC:
                        adc.multiplier = 328.57f;  // ZMPT107 default (230V / 0.70V)
                        break;
                    case SensorType::CURRENT_LOAD:
                    case SensorType::CURRENT_GRID:
                    case SensorType::CURRENT_SOLAR:
                        adc.multiplier = 30.0f;    // SCT013-030 default
                        break;
                    default:
                        adc.multiplier = 1.0f;
                        break;
                }
            }

            // Update offset (default 0.0)
            adc.offset = ch["offset"] | 0.0f;

            // Update enabled flag
            adc.enabled = ch["enabled"] | false;

            idx++;
        }
    }

    // Parse dimmer channels
    if (doc.containsKey("dimmer_ch1")) {
        config.dimmer_ch1.gpio = doc["dimmer_ch1"]["gpio"] | 19;
        config.dimmer_ch1.enabled = doc["dimmer_ch1"]["enabled"] | true;
    }

    if (doc.containsKey("dimmer_ch2")) {
        config.dimmer_ch2.gpio = doc["dimmer_ch2"]["gpio"] | 23;
        config.dimmer_ch2.enabled = doc["dimmer_ch2"]["enabled"] | false;
    }

    // Parse zero-cross
    if (doc.containsKey("zerocross_gpio")) {
        config.zerocross_gpio = doc["zerocross_gpio"] | 18;
    }
    if (doc.containsKey("zerocross_enabled")) {
        config.zerocross_enabled = doc["zerocross_enabled"] | true;
    }

    // Parse relay channels
    if (doc.containsKey("relay_ch1")) {
        config.relay_ch1.gpio = doc["relay_ch1"]["gpio"] | 15;
        config.relay_ch1.active_high = doc["relay_ch1"]["active_high"] | true;
        config.relay_ch1.enabled = doc["relay_ch1"]["enabled"] | false;
    }

    if (doc.containsKey("relay_ch2")) {
        config.relay_ch2.gpio = doc["relay_ch2"]["gpio"] | 2;
        config.relay_ch2.active_high = doc["relay_ch2"]["active_high"] | true;
        config.relay_ch2.enabled = doc["relay_ch2"]["enabled"] | false;
    }

    // Parse LED GPIOs
    if (doc.containsKey("led_status_gpio")) {
        config.led_status_gpio = doc["led_status_gpio"] | 17;
    }
    if (doc.containsKey("led_load_gpio")) {
        config.led_load_gpio = doc["led_load_gpio"] | 5;
    }

    // Validate configuration
    String error_msg;
    if (!hw.validate(&error_msg)) {
        JsonDocument response;
        response["success"] = false;
        response["error"] = error_msg;

        String json;
        serializeJson(response, json);
        sendJsonResponse(400, json);
        return;
    }

    // Save to NVS
    if (!hw.saveAll()) {
        sendError(500, "Failed to save configuration to NVS");
        return;
    }

    ESP_LOGI(TAG, "Hardware configuration saved to NVS");

    JsonDocument response;
    response["success"] = true;
    response["message"] = "Configuration saved to NVS (reboot required)";

    String json;
    serializeJson(response, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleValidateHardwareConfig() {
    if (!_http_server->hasArg("plain")) {
        sendError(400, "Missing request body");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _http_server->arg("plain"));

    if (error) {
        sendError(400, "Invalid JSON");
        return;
    }

    // Create temporary config for validation
    HardwareConfig temp_config;

    // Parse configuration (same as handleSetHardwareConfig)
    // ... (simplified for validation, reuse same parsing logic)

    String error_msg;
    bool valid = temp_config.validate(&error_msg);

    JsonDocument response;
    response["valid"] = valid;
    if (!valid) {
        response["error"] = error_msg;
    }

    String json;
    serializeJson(response, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleGetSensorProfiles() {
    JsonDocument doc;
    JsonArray profiles = doc["profiles"].to<JsonArray>();

    // ========================================
    // Voltage Sensor Profiles
    // ========================================

    // ZMPT107
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "ZMPT107_ADC";
        p["name"] = "ZMPT107";
        p["sensorType"] = "VOLTAGE_AC";
        p["category"] = "voltage";
        p["calibrated"] = true;
        p["multiplier"] = VoltageSensorDefaults::ZMPT107_DEFAULT_MULT_230V;
        p["offset"] = VoltageSensorDefaults::ZMPT107_OFFSET;
        p["nominal"] = 250.0f;
        p["notes"] = "AC Voltage transformer, 0.70V RMS @ 230V";
    }

    // ZMPT101B
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "ZMPT101B_ADC";
        p["name"] = "ZMPT101B";
        p["sensorType"] = "VOLTAGE_AC";
        p["category"] = "voltage";
        p["calibrated"] = true;
        p["multiplier"] = VoltageSensorDefaults::ZMPT101B_DEFAULT_MULT_230V;
        p["offset"] = VoltageSensorDefaults::ZMPT101B_OFFSET;
        p["nominal"] = 250.0f;
        p["notes"] = "AC Voltage transformer, 1.0V RMS @ 230V";
    }

    // Custom Voltage
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "CUSTOM_VOLTAGE";
        p["name"] = "Custom Voltage Sensor";
        p["sensorType"] = "VOLTAGE_AC";
        p["category"] = "voltage";
        p["calibrated"] = false;
        p["multiplier"] = 230.0f;
        p["offset"] = 0.0f;
        p["nominal"] = 250.0f;
        p["notes"] = "Custom voltage divider - needs calibration";
    }

    // ========================================
    // SCT-013 Current Sensor Profiles
    // ========================================

    // SCT013-5A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "SCT013_5A";
        p["name"] = "SCT-013-5A";
        p["sensorType"] = "CURRENT";
        p["category"] = "sct013";
        p["calibrated"] = false;
        p["multiplier"] = CurrentSensorDefaults::SCT013_5A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::SCT013_5A_OFFSET;
        p["nominal"] = CurrentSensorDefaults::SCT013_5A_NOMINAL;
    }

    // SCT013-10A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "SCT013_10A";
        p["name"] = "SCT-013-10A";
        p["sensorType"] = "CURRENT";
        p["category"] = "sct013";
        p["calibrated"] = false;
        p["multiplier"] = CurrentSensorDefaults::SCT013_10A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::SCT013_10A_OFFSET;
        p["nominal"] = CurrentSensorDefaults::SCT013_10A_NOMINAL;
    }

    // SCT013-20A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "SCT013_20A";
        p["name"] = "SCT-013-20A";
        p["sensorType"] = "CURRENT";
        p["category"] = "sct013";
        p["calibrated"] = false;
        p["multiplier"] = CurrentSensorDefaults::SCT013_20A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::SCT013_20A_OFFSET;
        p["nominal"] = CurrentSensorDefaults::SCT013_20A_NOMINAL;
    }

    // SCT013-30A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "SCT013_30A";
        p["name"] = "SCT-013-30A";
        p["sensorType"] = "CURRENT";
        p["category"] = "sct013";
        p["calibrated"] = false;
        p["multiplier"] = CurrentSensorDefaults::SCT013_30A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::SCT013_30A_OFFSET;
        p["nominal"] = CurrentSensorDefaults::SCT013_30A_NOMINAL;
    }

    // SCT013-50A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "SCT013_50A";
        p["name"] = "SCT-013-50A";
        p["sensorType"] = "CURRENT";
        p["category"] = "sct013";
        p["calibrated"] = false;
        p["multiplier"] = CurrentSensorDefaults::SCT013_50A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::SCT013_50A_OFFSET;
        p["nominal"] = CurrentSensorDefaults::SCT013_50A_NOMINAL;
    }

    // SCT013-60A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "SCT013_60A";
        p["name"] = "SCT-013-60A";
        p["sensorType"] = "CURRENT";
        p["category"] = "sct013";
        p["calibrated"] = false;
        p["multiplier"] = CurrentSensorDefaults::SCT013_60A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::SCT013_60A_OFFSET;
        p["nominal"] = CurrentSensorDefaults::SCT013_60A_NOMINAL;
    }

    // SCT013-80A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "SCT013_80A";
        p["name"] = "SCT-013-80A";
        p["sensorType"] = "CURRENT";
        p["category"] = "sct013";
        p["calibrated"] = false;
        p["multiplier"] = CurrentSensorDefaults::SCT013_80A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::SCT013_80A_OFFSET;
        p["nominal"] = CurrentSensorDefaults::SCT013_80A_NOMINAL;
    }

    // SCT013-100A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "SCT013_100A";
        p["name"] = "SCT-013-100A";
        p["sensorType"] = "CURRENT";
        p["category"] = "sct013";
        p["calibrated"] = false;
        p["multiplier"] = CurrentSensorDefaults::SCT013_100A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::SCT013_100A_OFFSET;
        p["nominal"] = CurrentSensorDefaults::SCT013_100A_NOMINAL;
    }

    // ========================================
    // ACS712 Current Sensor Profiles
    // ========================================

    // ACS712-5A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "ACS712_5A";
        p["name"] = "ACS712-5A";
        p["sensorType"] = "CURRENT";
        p["category"] = "acs712";
        p["calibrated"] = false;
        p["multiplier"] = CurrentSensorDefaults::ACS712_5A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::ACS712_5A_DC_OFFSET_3V3;
        p["nominal"] = CurrentSensorDefaults::ACS712_5A_NOMINAL;
        p["notes"] = "Theoretical values - not empirically calibrated";
    }

    // ACS712-10A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "ACS712_10A";
        p["name"] = "ACS712-10A";
        p["sensorType"] = "CURRENT";
        p["category"] = "acs712";
        p["calibrated"] = true;
        p["multiplier"] = CurrentSensorDefaults::ACS712_10A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::ACS712_10A_DC_OFFSET_3V3;
        p["nominal"] = CurrentSensorDefaults::ACS712_10A_NOMINAL;
        p["calibration_date"] = "2025-01-15";
        p["accuracy"] = "±2%";
        p["notes"] = "Saturates above 10A - linear range only";
    }

    // ACS712-20A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "ACS712_20A";
        p["name"] = "ACS712-20A";
        p["sensorType"] = "CURRENT";
        p["category"] = "acs712";
        p["calibrated"] = false;
        p["multiplier"] = CurrentSensorDefaults::ACS712_20A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::ACS712_20A_DC_OFFSET_3V3;
        p["nominal"] = CurrentSensorDefaults::ACS712_20A_NOMINAL;
        p["notes"] = "Theoretical values - not empirically calibrated";
    }

    // ACS712-30A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "ACS712_30A";
        p["name"] = "ACS712-30A";
        p["sensorType"] = "CURRENT";
        p["category"] = "acs712";
        p["calibrated"] = true;
        p["multiplier"] = CurrentSensorDefaults::ACS712_30A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::ACS712_30A_DC_OFFSET_3V3;
        p["nominal"] = CurrentSensorDefaults::ACS712_30A_NOMINAL;
        p["calibration_date"] = "2025-01-15";
        p["accuracy"] = "±2%";
        p["notes"] = "Calibrated with voltage divider circuit";
    }

    // ACS712-50A
    {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = "ACS712_50A";
        p["name"] = "ACS712-50A";
        p["sensorType"] = "CURRENT";
        p["category"] = "acs712";
        p["calibrated"] = true;
        p["multiplier"] = CurrentSensorDefaults::ACS712_50A_MULTIPLIER;
        p["offset"] = CurrentSensorDefaults::ACS712_50A_DC_OFFSET_3V3;
        p["nominal"] = CurrentSensorDefaults::ACS712_50A_NOMINAL;
        p["calibration_date"] = "2025-12-22";
        p["notes"] = "Calibrated 4-29A range";
    }

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

// ============================================================================
// System Control API Handlers
// ============================================================================

void WebServerManager::handleSystemReboot() {
    ESP_LOGW(TAG, "Reboot requested via API");

    JsonDocument response;
    response["success"] = true;
    response["message"] = "Rebooting in 3 seconds...";

    String json;
    serializeJson(response, json);
    sendJsonResponse(200, json);

    // Give time for response to be sent
    delay(500);

    // TODO: Stop critical tasks before reboot
    // - Stop PowerMeterADC task
    // - Stop DimmerHAL (set to 0%)
    // - Commit any pending NVS writes

    ESP_LOGI(TAG, "Rebooting NOW");
    delay(3000);
    ESP.restart();
}

// ============================================================================
// Utility - Build Hardware Config JSON
// ============================================================================

String WebServerManager::buildHardwareConfigJson() {
    HardwareConfigManager& hw = HardwareConfigManager::getInstance();
    const HardwareConfig& config = hw.getConfig();

    JsonDocument doc;

    // ADC Channels
    JsonArray adc_array = doc["adc_channels"].to<JsonArray>();
    for (int i = 0; i < 4; i++) {
        const ADCChannelConfig& ch = config.adc_channels[i];

        JsonObject ch_obj = adc_array.add<JsonObject>();
        ch_obj["gpio"] = ch.gpio;
        ch_obj["type"] = static_cast<uint8_t>(ch.type);
        ch_obj["type_name"] = sensorTypeToString(ch.type);
        ch_obj["multiplier"] = ch.multiplier;
        ch_obj["offset"] = ch.offset;
        ch_obj["enabled"] = ch.enabled;
    }

    // Dimmer Channels
    JsonObject dimmer1 = doc["dimmer_ch1"].to<JsonObject>();
    dimmer1["gpio"] = config.dimmer_ch1.gpio;
    dimmer1["enabled"] = config.dimmer_ch1.enabled;

    JsonObject dimmer2 = doc["dimmer_ch2"].to<JsonObject>();
    dimmer2["gpio"] = config.dimmer_ch2.gpio;
    dimmer2["enabled"] = config.dimmer_ch2.enabled;

    // Zero-Cross
    doc["zerocross_gpio"] = config.zerocross_gpio;
    doc["zerocross_enabled"] = config.zerocross_enabled;

    // Relay Channels
    JsonObject relay1 = doc["relay_ch1"].to<JsonObject>();
    relay1["gpio"] = config.relay_ch1.gpio;
    relay1["active_high"] = config.relay_ch1.active_high;
    relay1["enabled"] = config.relay_ch1.enabled;

    JsonObject relay2 = doc["relay_ch2"].to<JsonObject>();
    relay2["gpio"] = config.relay_ch2.gpio;
    relay2["active_high"] = config.relay_ch2.active_high;
    relay2["enabled"] = config.relay_ch2.enabled;

    // LEDs
    doc["led_status_gpio"] = config.led_status_gpio;
    doc["led_load_gpio"] = config.led_load_gpio;

    String json;
    serializeJson(doc, json);

    return json;
}

// ============================================================================
// OTA API Handlers
// ============================================================================

void WebServerManager::handleOTACheckGitHub() {
    ESP_LOGI(TAG, "API: OTA check GitHub");

    GitHubOTAChecker& checker = GitHubOTAChecker::getInstance();
    GitHubRelease release;

    // Check for updates (returns update availability, not success status)
    bool hasUpdate = checker.checkForUpdate(release);

    JsonDocument doc;

    // Determine if the check was successful by validating release data
    bool checkSuccessful = !release.tag_name.isEmpty();

    if (checkSuccessful) {
        doc["success"] = true;
        doc["update_available"] = hasUpdate;
        doc["current_version"] = checker.getCurrentVersion();

        if (hasUpdate) {
            // New version available
            doc["latest_version"] = release.tag_name;
            doc["release_name"] = release.name;
            doc["changelog"] = release.body;
            doc["published_at"] = release.published_at;
            doc["asset_name"] = release.asset_name;
            doc["asset_url"] = release.asset_url;
            doc["asset_size"] = release.asset_size;
            doc["is_prerelease"] = release.is_prerelease;
        } else {
            // Already up to date or development version
            doc["latest_version"] = release.tag_name;
            doc["release_name"] = release.name;
            doc["published_at"] = release.published_at;

            int cmp = GitHubOTAChecker::compareVersions(checker.getCurrentVersion(), release.tag_name.c_str());
            if (cmp == 0) {
                doc["message"] = "You are running the latest version";
            } else {
                doc["message"] = "Development version (ahead of latest release)";
            }
        }
    } else {
        // Failed to check
        doc["success"] = false;
        doc["error"] = "Failed to check for updates. Please check your internet connection.";
    }

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleOTAUpdateGitHub() {
    ESP_LOGI(TAG, "API: OTA update from GitHub");

    // Get update URL from request body
    if (!_http_server->hasArg("plain")) {
        sendError(400, "Missing request body");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _http_server->arg("plain"));

    if (error) {
        sendError(400, "Invalid JSON");
        return;
    }

    // Extract URL from request
    if (!doc["url"].is<String>()) {
        sendError(400, "Missing 'url' field");
        return;
    }

    String url = doc["url"].as<String>();

    // Send immediate response before starting OTA
    JsonDocument responseDoc;
    responseDoc["success"] = true;
    responseDoc["message"] = "OTA update started. Device will reboot after download.";

    String json;
    serializeJson(responseDoc, json);
    sendJsonResponse(200, json);

    // Wait a moment for response to be sent
    delay(500);

    // Start OTA update in background
    ESP_LOGI(TAG, "Starting OTA update from: %s", url.c_str());

    OTAManager& ota = OTAManager::getInstance();
    bool result = ota.updateFromURL(url.c_str());

    if (result) {
        ESP_LOGI(TAG, "OTA update successful, rebooting...");
        delay(1000);
        ESP.restart();
    } else {
        ESP_LOGE(TAG, "OTA update failed");
        // Note: Can't send response here as we already sent one
    }
}

// ============================================================================
// MQTT API Handlers
// ============================================================================

void WebServerManager::handleMQTTPage() {
    String html = getMQTTPage();
    _http_server->send(200, "text/html", html);
}

void WebServerManager::handleGetMQTTStatus() {
    MQTTManager& mqtt = MQTTManager::getInstance();

    JsonDocument doc;

    doc["enabled"] = mqtt.isEnabled();
    doc["connected"] = mqtt.isConnected();
    doc["state"] = mqtt.getConnectionState();

    const MQTTConfig& cfg = mqtt.getConfig();
    doc["broker"] = cfg.broker;
    doc["device_id"] = cfg.device_id;

    doc["uptime"] = mqtt.getConnectionUptime();
    doc["messages_published"] = mqtt.getMessagesPublished();
    doc["messages_received"] = mqtt.getMessagesReceived();
    doc["last_error"] = mqtt.getLastError();

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleGetMQTTConfig() {
    MQTTManager& mqtt = MQTTManager::getInstance();
    const MQTTConfig& cfg = mqtt.getConfig();

    JsonDocument doc;

    doc["enabled"] = cfg.enabled;
    doc["broker"] = cfg.broker;
    doc["username"] = cfg.username;
    // Don't send password
    doc["device_id"] = cfg.device_id;
    doc["device_name"] = cfg.device_name;
    doc["publish_interval"] = cfg.publish_interval;
    doc["ha_discovery"] = cfg.ha_discovery;

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleSetMQTTConfig() {
    if (!_http_server->hasArg("plain")) {
        sendError(400, "Missing request body");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _http_server->arg("plain"));

    if (error) {
        sendError(400, "Invalid JSON");
        return;
    }

    MQTTManager& mqtt = MQTTManager::getInstance();
    bool changed = false;

    // Update broker URL
    if (doc["broker"].is<const char*>()) {
        mqtt.setBroker(doc["broker"].as<const char*>());
        changed = true;
    }

    // Update credentials
    if (doc["username"].is<const char*>() || doc["password"].is<const char*>()) {
        const char* user = doc["username"].is<const char*>() ? doc["username"].as<const char*>() : "";
        const char* pass = doc["password"].is<const char*>() ? doc["password"].as<const char*>() : "";
        // Only update password if it's not empty (don't clear existing password)
        if (strlen(pass) > 0 || strlen(user) > 0) {
            mqtt.setCredentials(user, pass);
            changed = true;
        }
    }

    // Update device ID
    if (doc["device_id"].is<const char*>()) {
        mqtt.setDeviceId(doc["device_id"].as<const char*>());
        changed = true;
    }

    // Update device name
    if (doc["device_name"].is<const char*>()) {
        mqtt.setDeviceName(doc["device_name"].as<const char*>());
        changed = true;
    }

    // Update publish interval
    if (doc["publish_interval"].is<uint32_t>()) {
        mqtt.setPublishInterval(doc["publish_interval"].as<uint32_t>());
        changed = true;
    }

    // Update HA Discovery
    if (doc["ha_discovery"].is<bool>()) {
        mqtt.setHADiscovery(doc["ha_discovery"].as<bool>());
        changed = true;
    }

    // Update enabled state
    if (doc["enabled"].is<bool>()) {
        mqtt.setEnabled(doc["enabled"].as<bool>());
        changed = true;
    }

    // Save configuration to NVS
    if (changed) {
        if (mqtt.saveConfig()) {
            ESP_LOGI(TAG, "MQTT configuration saved");

            // If enabled, try to connect
            if (mqtt.isEnabled() && !mqtt.isConnected()) {
                mqtt.connect();
            }

            sendSuccess("MQTT configuration saved");
        } else {
            sendError(500, "Failed to save configuration");
        }
    } else {
        sendSuccess("No changes");
    }
}

void WebServerManager::handleMQTTReconnect() {
    MQTTManager& mqtt = MQTTManager::getInstance();

    if (!mqtt.isEnabled()) {
        sendError(400, "MQTT is disabled");
        return;
    }

    ESP_LOGI(TAG, "MQTT reconnect requested via API");
    mqtt.reconnect();

    sendSuccess("Reconnection initiated");
}

void WebServerManager::handleMQTTPublish() {
    MQTTManager& mqtt = MQTTManager::getInstance();

    if (!mqtt.isConnected()) {
        sendError(400, "MQTT not connected");
        return;
    }

    ESP_LOGI(TAG, "MQTT publish requested via API");
    mqtt.publishAll();

    sendSuccess("Data published");
}
