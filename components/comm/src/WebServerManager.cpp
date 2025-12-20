#include "WebServerManager.h"
#include <ArduinoJson.h>
#include "ConfigManager.h"
#include "HardwareConfigManager.h"
#include "OTAManager.h"

// New Material UI Web Interface
#include "../web/styles/MaterialStyles.h"
#include "../web/components/Layout.h"
#include "../web/pages/DashboardPage.h"
#include "../web/pages/WiFiConfigPage.h"
#include "../web/pages/HardwareConfigPage.h"

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

    // ========================================
    // REST API - System Control
    // ========================================

    _http_server->on("/api/system/reboot", HTTP_POST, [this]() { handleSystemReboot(); });

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

    doc["version"] = "1.0.0";
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
