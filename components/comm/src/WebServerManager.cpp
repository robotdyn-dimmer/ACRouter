#include "WebServerManager.h"
#include <ArduinoJson.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"      // scan-result mutex
#include "sdkconfig.h"

// Comms-plane core (docs/18 §11): web worker tasks live on PRO_CPU (core 0) with WiFi
// on a dual-core target (ESP32) so they cannot preempt the control loop on APP_CPU;
// no affinity on single-core (C2).
#if !CONFIG_FREERTOS_UNICORE
#define ACR_WEB_CORE  0
#else
#define ACR_WEB_CORE  tskNO_AFFINITY
#endif
#include "esp_system.h"           // esp_restart()
#include "esp_timer.h"            // esp_timer_get_time() (scan freshness)
#include "esp_wifi.h"
#include "ConfigManager.h"
#include "HardwareConfigManager.h"
#include "SensorTypes.h"
#include "VoltageSensorDrivers.h"
#include "CurrentSensorDrivers.h"
#include "OTAManager.h"
#include "GitHubOTAChecker.h"
#include "MQTTManager.h"
#include "NTPManager.h"
#include <esp_app_desc.h>
#include <esp_chip_info.h>

// New dimmer manager (pure C API)
extern "C" {
#include "dimmer_manager.h"
}

// New relay manager (pure C API)
extern "C" {
#include "relay_manager.h"
#include "relay_gpio.h"
}

// Event bus (pure C API) — for the sim-inject test harness (POST /api/sim/inject)
extern "C" {
#include "acrouter_events.h"
#include "acrouter_measurements.h"
}

// v2.0: DimmerLink I2C modules and Sensor Hub
extern "C" {
#include "i2c_bus.h"
#include "dimmerlink_regs.h"
#include "dimmerlink_types.h"
#include "dimmerlink_manager.h"
#include "device_registry.h"
// dimmer_gpio.h removed (v2.0: GPIO/TRIAC dimming gone)
#include "sensor_hub.h"
#include "rbamp_source.h"
#include "esp_now_source.h"
#include "espnow_proto.h"
#include "nvs.h"
}

// New Material UI Web Interface
#include "../web/styles/MaterialStyles.h"
#include "../web/components/Layout.h"
#include "../web/pages/WiFiConfigPage.h"
#include "../web/pages/MQTTPage.h"
#include "../web/pages/RelaysPage.h"
// DashboardPage/HardwareConfigPage/OTAPage/DimmersPage removed in v2.0 — those on-device
// pages called routes/fields deleted in the legacy-removal and contradicted the external
// web-app UI model. WiFi/MQTT/Relays on-device pages retained for bootstrap.

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
    _auth_token[0] = '\0';
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

    // Create HTTP server (esp_http_server-backed shim — multi-socket, own task)
    _http_server = new HttpdServer(_http_port);
    if (!_http_server) {
        ESP_LOGE(TAG, "Failed to create HTTP server");
        return false;
    }

    // Auth: load the persisted token + capture the Authorization header (the Arduino
    // WebServer only exposes headers it's told to collect).
    loadAuthToken();
    static const char* authHeaders[] = {"Authorization"};
    _http_server->collectHeaders(authHeaders, 1);

    // External web app: load its URL (redirect target + CORS origin).
    loadWebAppUrl();

    // Setup routes
    setupRoutes();

    // Start server
    _http_server->begin();

    // Initialize OTA Manager — gated per the tiering profile (docs/18 §4): the C2
    // updates via manual UART flash, so OTA (local upload + GitHub) is compiled out.
#if CONFIG_ACROUTER_OTA
    OTAManager& ota = OTAManager::getInstance();
    if (!ota.begin(_http_server)) {
        ESP_LOGE(TAG, "Failed to initialize OTA Manager");
    } else {
        ESP_LOGI(TAG, "OTA Manager initialized");
    }
#else
    ESP_LOGI(TAG, "OTA disabled at build time (ACROUTER_OTA=n) — update via UART");
#endif

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

    // Root: redirect to the external web app (the SPA is NOT hosted on the device).
    // If no app URL is configured, handleDashboard() shows a minimal stub.
    _http_server->on("/", HTTP_GET, [this]() { handleDashboard(); });
    _http_server->on("/index.html", HTTP_GET, [this]() { handleDashboard(); });

    // Web-app URL config (external SPA host = redirect target + CORS origin).
    _http_server->on("/api/web/config", HTTP_GET,  [this]() { handleGetWebConfig(); });
    _http_server->on("/api/web/config", HTTP_POST, [this]() { if (!requireAuth()) return; handleSetWebConfig(); });

    // WiFi configuration
    _http_server->on("/wifi", HTTP_GET, [this]() { handleWiFiPage(); });

    // On-device Hardware-config and OTA pages removed — the external web app owns the UI.
    // OTA firmware upload remains at POST /ota/upload + the /api/ota/* endpoints.

    // Static resources
    _http_server->on("/styles.css", HTTP_GET, [this]() {
        _http_server->send(200, "text/css", MATERIAL_CSS);
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

    _http_server->on("/api/config", HTTP_POST, [this]() { if (!requireAuth()) return; handleSetConfig(); });
    _http_server->on("/api/mode", HTTP_POST, [this]() { if (!requireAuth()) return; handleSetMode(); });

    // Auth (A3): check is open (login-gate probe); POST /api/auth is gated by requireAuth.
    // (OPTIONS preflight for these is registered below, after corsHandler is defined.)
    _http_server->on("/api/auth/check", HTTP_GET,  [this]() { handleAuthCheck(); });
    _http_server->on("/api/auth",       HTTP_POST, [this]() { if (!requireAuth()) return; handleSetAuth(); });
    _http_server->on("/api/dimmer", HTTP_POST, [this]() { if (!requireAuth()) return; handleSetDimmer(); });
    _http_server->on("/api/config/reset", HTTP_POST, [this]() { if (!requireAuth()) return; handleResetConfig(); });

    // ========================================
    // REST API - Control
    // ========================================

    _http_server->on("/api/manual", HTTP_POST, [this]() { if (!requireAuth()) return; handleSetManual(); });
    // Tier-0 test harness: inject a synthetic power measurement over HTTP (mirrors the
    // `sim-inject` serial command) so AUTO/ECO/OFFGRID surplus can be exercised from the
    // web client with no AC / no controllable load and no serial-port contention.
    _http_server->on("/api/sim/inject", HTTP_POST, [this]() { if (!requireAuth()) return; handleSimInject(); });
    _http_server->on("/api/sim/stop", HTTP_POST, [this]() { if (!requireAuth()) return; handleSimStop(); });
    _http_server->on("/api/calibrate", HTTP_POST, [this]() { if (!requireAuth()) return; handleCalibrate(); });

    // ========================================
    // REST API - WiFi Management
    // ========================================

    _http_server->on("/api/wifi/status", HTTP_GET, [this]() { handleWiFiStatus(); });
    _http_server->on("/api/wifi/scan", HTTP_GET, [this]() { handleWiFiScan(); });
    _http_server->on("/api/wifi/connect", HTTP_POST, [this]() { if (!requireAuth()) return; handleWiFiConnect(); });
    _http_server->on("/api/wifi/disconnect", HTTP_POST, [this]() { if (!requireAuth()) return; handleWiFiDisconnect(); });
    _http_server->on("/api/wifi/forget", HTTP_POST, [this]() { if (!requireAuth()) return; handleWiFiForget(); });

    // ========================================
    // REST API - Hardware Configuration
    // ========================================

    _http_server->on("/api/hardware/config", HTTP_GET, [this]() { handleGetHardwareConfig(); });
    _http_server->on("/api/hardware/config", HTTP_POST, [this]() { if (!requireAuth()) return; handleSetHardwareConfig(); });
    _http_server->on("/api/hardware/validate", HTTP_POST, [this]() { if (!requireAuth()) return; handleValidateHardwareConfig(); });
    // v2.0: unified read-only measurement source list (rbAmp I2C + ESP-NOW).
    // Replaces the removed ADC catalog endpoints (sensor-types/voltage-drivers/
    // current-drivers/sensor-profiles) — smart modules are self-calibrated.
    _http_server->on("/api/sensors", HTTP_GET, [this]() { handleGetSensors(); });

    // ========================================
    // REST API - System Control
    // ========================================

    // Support both GET and POST for browser compatibility
    _http_server->on("/api/system/reboot", HTTP_GET, [this]() { handleSystemReboot(); });
    _http_server->on("/api/system/reboot", HTTP_POST, [this]() { if (!requireAuth()) return; handleSystemReboot(); });

    // ========================================
    // REST API - OTA Management
    // ========================================

#if CONFIG_ACROUTER_OTA
    _http_server->on("/api/ota/check-github", HTTP_GET, [this]() { handleOTACheckGitHub(); });
    _http_server->on("/api/ota/update-github", HTTP_POST, [this]() { if (!requireAuth()) return; handleOTAUpdateGitHub(); });
#endif

    // ========================================
    // MQTT Configuration Page & API
    // ========================================

    _http_server->on("/mqtt", HTTP_GET, [this]() { handleMQTTPage(); });
    _http_server->on("/api/mqtt/status", HTTP_GET, [this]() { handleGetMQTTStatus(); });
    _http_server->on("/api/mqtt/config", HTTP_GET, [this]() { handleGetMQTTConfig(); });
    _http_server->on("/api/mqtt/config", HTTP_POST, [this]() { if (!requireAuth()) return; handleSetMQTTConfig(); });
    _http_server->on("/api/mqtt/reconnect", HTTP_POST, [this]() { if (!requireAuth()) return; handleMQTTReconnect(); });
    _http_server->on("/api/mqtt/publish", HTTP_POST, [this]() { if (!requireAuth()) return; handleMQTTPublish(); });

    // ========================================
    // REST API - NTP Configuration
    // ========================================

    _http_server->on("/api/ntp/status", HTTP_GET, [this]() { handleGetNTPStatus(); });
    _http_server->on("/api/ntp/config", HTTP_GET, [this]() { handleGetNTPConfig(); });
    _http_server->on("/api/ntp/config", HTTP_POST, [this]() { if (!requireAuth()) return; handleSetNTPConfig(); });
    _http_server->on("/api/ntp/sync", HTTP_POST, [this]() { if (!requireAuth()) return; handleNTPSync(); });

    // ========================================
    // Relays Page & API
    // ========================================

    _http_server->on("/relays", HTTP_GET, [this]() { handleRelaysPage(); });
    _http_server->on("/api/relays/status", HTTP_GET, [this]() { handleGetRelaysStatus(); });

    // Support both GET and POST for browser compatibility
    _http_server->on("/api/relays/all-on", HTTP_GET, [this]() { handleRelaysAllOn(); });
    _http_server->on("/api/relays/all-on", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelaysAllOn(); });
    _http_server->on("/api/relays/all-off", HTTP_GET, [this]() { handleRelaysAllOff(); });
    _http_server->on("/api/relays/all-off", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelaysAllOff(); });

    // Dynamic relay endpoints (relay ID in URL)
    // Support both GET and POST for browser compatibility
    _http_server->on("/api/relays/0/on", HTTP_GET, [this]() { handleRelayOn(0); });
    _http_server->on("/api/relays/0/on", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelayOn(0); });
    _http_server->on("/api/relays/1/on", HTTP_GET, [this]() { handleRelayOn(1); });
    _http_server->on("/api/relays/1/on", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelayOn(1); });
    _http_server->on("/api/relays/2/on", HTTP_GET, [this]() { handleRelayOn(2); });
    _http_server->on("/api/relays/2/on", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelayOn(2); });
    _http_server->on("/api/relays/3/on", HTTP_GET, [this]() { handleRelayOn(3); });
    _http_server->on("/api/relays/3/on", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelayOn(3); });

    _http_server->on("/api/relays/0/off", HTTP_GET, [this]() { handleRelayOff(0); });
    _http_server->on("/api/relays/0/off", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelayOff(0); });
    _http_server->on("/api/relays/1/off", HTTP_GET, [this]() { handleRelayOff(1); });
    _http_server->on("/api/relays/1/off", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelayOff(1); });
    _http_server->on("/api/relays/2/off", HTTP_GET, [this]() { handleRelayOff(2); });
    _http_server->on("/api/relays/2/off", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelayOff(2); });
    _http_server->on("/api/relays/3/off", HTTP_GET, [this]() { handleRelayOff(3); });
    _http_server->on("/api/relays/3/off", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelayOff(3); });

    _http_server->on("/api/relays/0/config", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelayConfig(0); });
    _http_server->on("/api/relays/1/config", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelayConfig(1); });
    _http_server->on("/api/relays/2/config", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelayConfig(2); });
    _http_server->on("/api/relays/3/config", HTTP_POST, [this]() { if (!requireAuth()) return; handleRelayConfig(3); });

    // ========================================
    // Dimmers Page & API
    // ========================================

    _http_server->on("/api/dimmers/status", HTTP_GET, [this]() { handleGetDimmersStatus(); });

    // Dimmer control - support both GET (with query params) and POST (with JSON body)
    _http_server->on("/api/dimmers/all-on", HTTP_GET, [this]() { handleDimmersAllOn(); });
    _http_server->on("/api/dimmers/all-on", HTTP_POST, [this]() { if (!requireAuth()) return; handleDimmersAllOn(); });
    _http_server->on("/api/dimmers/all-off", HTTP_GET, [this]() { handleDimmersAllOff(); });
    _http_server->on("/api/dimmers/all-off", HTTP_POST, [this]() { if (!requireAuth()) return; handleDimmersAllOff(); });

    // v2.0: legacy per-GPIO-dimmer routes (/api/dimmers/{0-3}/level, /config) REMOVED —
    // dimming is only via DimmerLink. Live level is read from /api/metrics + /api/dimmers/status;
    // actuation is via router modes (RouterController drives the DimmerLink output).

    // ========================================
    // Server Configuration
    // ========================================

    // CORS headers
    // CORS is managed manually (NOT enableCORS(true), which hard-codes "*" on every
    // response and can't reflect a specific origin). Access-Control-Allow-Origin is
    // set to corsOrigin() (the configured web-app URL, or "*" when unset) in
    // sendJsonResponse() for API responses and in corsHandler() for preflight.

    // OPTIONS handlers for CORS preflight requests (main POST endpoints)
    auto corsHandler = [this]() {
        ESP_LOGD(TAG, "OPTIONS preflight request received: %s", _http_server->uri().c_str());
        _http_server->sendHeader("Access-Control-Allow-Origin", corsOrigin());
        _http_server->sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        _http_server->sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
        _http_server->sendHeader("Access-Control-Max-Age", "86400");  // 24 hours cache
        _http_server->send(204);  // No Content
    };

    // Register OPTIONS handlers for main API endpoints
    _http_server->on("/api/config", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/web/config", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/mode", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/auth", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/auth/check", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/dimmer", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/manual", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/sim/inject", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/sim/stop", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/calibrate", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/wifi/connect", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/wifi/disconnect", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/wifi/forget", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/hardware/config", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/hardware/validate", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/system/reboot", HTTP_OPTIONS, corsHandler);
#if CONFIG_ACROUTER_OTA
    _http_server->on("/api/ota/update-github", HTTP_OPTIONS, corsHandler);
#endif
    _http_server->on("/api/mqtt/config", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/mqtt/reconnect", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/mqtt/publish", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/ntp/config", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/ntp/sync", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/config/reset", HTTP_OPTIONS, corsHandler);

    // v2.0: I2C / DimmerLink / Sensor Hub endpoints
    _http_server->on("/api/i2c/status",           HTTP_GET,  [this]() { handleI2CStatus(); });
    _http_server->on("/api/i2c/scan",             HTTP_GET,  [this]() { handleI2CScan(); });
    _http_server->on("/api/dimmerlink/devices",   HTTP_GET,  [this]() { handleDimmerLinkDevices(); });
    _http_server->on("/api/dimmerlink/devices",   HTTP_POST, [this]() { if (!requireAuth()) return; handleSetDimmerLinkDevice(); });
    _http_server->on("/api/rbamp/modules",        HTTP_GET,  [this]() { handleGetRbampModules(); });
    _http_server->on("/api/rbamp/modules",        HTTP_POST, [this]() { if (!requireAuth()) return; handleSetRbampModule(); });
    _http_server->on("/api/rbamp/modules",        HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/rbamp/modules/address",    HTTP_POST, [this]() { if (!requireAuth()) return; handleRbampAddress(); });
    _http_server->on("/api/rbamp/modules/address",    HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/modules",                  HTTP_GET,  [this]() { handleGetModules(); });
    _http_server->on("/api/modules/rescan",           HTTP_POST, [this]() { if (!requireAuth()) return; handleModulesRescan(); });
    _http_server->on("/api/modules/rescan",           HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/modules/role",             HTTP_POST, [this]() { if (!requireAuth()) return; handleModulesRole(); });
    _http_server->on("/api/modules/role",             HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/modules/name",             HTTP_POST, [this]() { if (!requireAuth()) return; handleModulesName(); });
    _http_server->on("/api/modules/name",             HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/rbamp/ct-models",          HTTP_GET,  [this]() { handleGetCtModels(); });
    _http_server->on("/api/rbamp/modules/ct-model",   HTTP_POST, [this]() { if (!requireAuth()) return; handleRbampCtModel(); });
    _http_server->on("/api/rbamp/modules/ct-model",   HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/dimmerlink/devices/address", HTTP_POST, [this]() { if (!requireAuth()) return; handleDimmerLinkAddress(); });
    _http_server->on("/api/dimmerlink/devices/address", HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/rbamp/rescan",         HTTP_POST, [this]() { if (!requireAuth()) return; handleRbampRescan(); });
    _http_server->on("/api/rbamp/rescan",         HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/espnow/nodes",         HTTP_GET,  [this]() { handleGetEspnowNodes(); });
    _http_server->on("/api/espnow/nodes",         HTTP_POST, [this]() { if (!requireAuth()) return; handleSetEspnowNode(); });
    _http_server->on("/api/espnow/nodes",         HTTP_OPTIONS, corsHandler);
    _http_server->on("/api/espnow/outputs",       HTTP_GET,  [this]() { handleGetEspnowOutputs(); });
    for (int i = 0; i < DL_MAX_DEVICES; i++) {
        int slot = i;
        String path = "/api/dimmerlink/" + String(i) + "/status";
        _http_server->on(path.c_str(), HTTP_GET, [this, slot]() { handleDimmerLinkSlot(slot); });
    }
    _http_server->on("/api/sensors/hub", HTTP_GET, [this]() { handleSensorHubStatus(); });

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

    // Real chip model (was hardcoded "ESP32" — wrong on the C2 port).
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    switch (chip_info.model) {
        case CHIP_ESP32:   doc["chip"] = "ESP32";    break;
        case CHIP_ESP32C2: doc["chip"] = "ESP32-C2"; break;
        case CHIP_ESP32C3: doc["chip"] = "ESP32-C3"; break;
        case CHIP_ESP32S3: doc["chip"] = "ESP32-S3"; break;
        default:           doc["chip"] = "ESP32";    break;
    }
    doc["flash_size"] = ESP.getFlashChipSize();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime"] = uptime_sec;          // Legacy field (deprecated)
    doc["uptime_sec"] = uptime_sec;      // New field for clarity

    // Capability flags — let the web UI gate features semantically instead of by
    // chip string. TLS/HTTPS (and thus the GitHub-OTA update check) is trimmed on
    // the ESP32-C2 (mbedTLS too heavy for 272 KB); local OTA upload still works.
#if CONFIG_IDF_TARGET_ESP32C2
    doc["features"]["github_ota"] = false;
    doc["features"]["tls"] = false;
#else
    doc["features"]["github_ota"] = true;
    doc["features"]["tls"] = true;
#endif
    // Interface availability per the C2/ESP32 tiering profile (docs/18): lets the web
    // client hide UI for an interface this build was compiled without (e.g. the MQTT
    // page on a C2-HTTP build where MQTT is gated out). Reflects the compile-time gates.
#if CONFIG_ACROUTER_MQTT_CLIENT
    doc["features"]["mqtt"] = true;
#else
    doc["features"]["mqtt"] = false;
#endif
#if CONFIG_ACROUTER_HTTP_SERVER
    doc["features"]["http"] = true;
#else
    doc["features"]["http"] = false;
#endif
#if CONFIG_ACROUTER_OTA
    doc["features"]["ota"] = true;
#else
    doc["features"]["ota"] = false;   // C2: update via UART, web client hides OTA page
#endif

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
    // NOTE: voltage_coef/current_coef retired in v2.0 (rbAmp modules are factory-
    // calibrated; there is no on-chip ADC to scale). Silently ignore if a legacy
    // client still sends them — no consumer exists.
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
    if (doc["grid_current_limit"].is<float>()) {
        float a = doc["grid_current_limit"];
        cfg.setGridCurrentLimit(a);
        RouterController::getInstance().setGridCurrentLimit(a);  // apply live
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
    } else if (mode == "grid_limit") {
        router.setMode(RouterMode::GRID_LIMIT);
        sendSuccess("Mode set to GRID_LIMIT");
    } else {
        sendError(400, "Invalid mode (use: off, auto, eco, offgrid, manual, boost, grid_limit)");
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
    cfg.setCurrentThreshold(ConfigDefaults::CURRENT_THRESHOLD);
    cfg.setPowerThreshold(ConfigDefaults::POWER_THRESHOLD);
    // MINOR-4: also reset the router-control params, not just the sensor thresholds.
    cfg.setGridCurrentLimit(ConfigDefaults::GRID_CURRENT_LIMIT);
    cfg.setRouterMode(ConfigDefaults::ROUTER_MODE);
    cfg.setManualLevel(ConfigDefaults::MANUAL_LEVEL);

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

// --- Latching sim-inject (Task 17) ---------------------------------------------
// A one-shot POST /api/sim/inject sustains a measurement stream only if the caller
// re-posts at ~5 Hz; under a web-load test the httpd throttles those posts, so the
// measurement rate (not control starvation) becomes the variable. The latch stores
// the last injected measurement per role and an esp_timer re-posts it at 5 Hz from
// the timer task — an HTTP-independent source for isolation / AUTO-surplus tests.
// Slots: 0=load 1=grid 2=solar (ACROUTER_CH_*), 3=voltage.
static acrouter_measurements_t s_sim_latch[4];
static bool                    s_sim_latch_on[4]  = { false, false, false, false };
static esp_timer_handle_t      s_sim_timer        = nullptr;
static portMUX_TYPE            s_sim_mux          = portMUX_INITIALIZER_UNLOCKED;

static void sim_latch_timer_cb(void*) {
    for (int i = 0; i < 4; ++i) {
        acrouter_measurements_t m;
        bool on;
        portENTER_CRITICAL(&s_sim_mux);
        on = s_sim_latch_on[i];
        if (on) m = s_sim_latch[i];   // copy out; post outside the critical section
        portEXIT_CRITICAL(&s_sim_mux);
        if (on) {
            m.timestamp_us = esp_timer_get_time();  // refresh so the merge never drops it as stale
            esp_event_post(ACROUTER_EVENT, ACROUTER_EVENT_POWER_UPDATE, &m, sizeof(m), 0);
        }
    }
}

// Ensure the 5 Hz re-post timer exists and is running (idempotent; also restarts
// it after a prior /api/sim/stop, which stops but keeps the handle).
static void sim_latch_ensure_timer() {
    if (!s_sim_timer) {
        const esp_timer_create_args_t args = {
            .callback = &sim_latch_timer_cb,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "sim_latch",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&args, &s_sim_timer) != ESP_OK) return;
    }
    if (!esp_timer_is_active(s_sim_timer)) {
        esp_timer_start_periodic(s_sim_timer, 200000);  // 200 ms = 5 Hz
    }
}

// POST /api/sim/inject — Tier-0 test harness. Post a synthetic ACROUTER_EVENT_POWER_UPDATE
// so the control logic (AUTO/ECO/OFFGRID surplus routing) can be exercised with NO AC and
// NO controllable load. Mirrors the `sim-inject` serial command (SerialCommand.cpp).
// Body: {"role":"grid|solar|load|voltage","current":<A>,"voltage":<V>,"power":<W signed>}
//   - grid/solar/load: 'current' (A) required; 'voltage'/'power' optional. power +import/-export.
//   - voltage: 'voltage' (V) carries the shared voltage reference.
// Call repeatedly (~5 Hz) to sustain a stream (measurement staleness = 500 ms).
void WebServerManager::handleSimInject() {
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

    const char* role = doc["role"] | "";
    if (!role[0]) {
        sendError(400, "Missing 'role' (grid|solar|load|voltage)");
        return;
    }

    int ch;
    if      (strcmp(role, "grid")    == 0) ch = ACROUTER_CH_GRID;
    else if (strcmp(role, "solar")   == 0) ch = ACROUTER_CH_SOLAR;
    else if (strcmp(role, "load")    == 0) ch = ACROUTER_CH_LOAD;
    else if (strcmp(role, "voltage") == 0) ch = -1;
    else {
        sendError(400, "Unknown role (grid|solar|load|voltage)");
        return;
    }

    acrouter_measurements_t m;
    acrouter_measurements_init(&m);
    m.source       = ACROUTER_SOURCE_I2C;
    m.source_id    = 0xEE;  // 'sim' marker id (same as the serial harness)
    m.timestamp_us = esp_timer_get_time();

    bool any = false;
    if (ch < 0) {
        // voltage role: shared voltage reference
        if (!doc["voltage"].isNull()) {
            float v = doc["voltage"].as<float>();
            if (v > 0.0f) { m.voltage_rms = v; m.has_voltage = true; any = true; }
        }
    } else {
        if (doc["current"].isNull()) {
            sendError(400, "Missing 'current' (A) for grid/solar/load");
            return;
        }
        float cur = doc["current"].as<float>();
        m.current_rms[ch] = cur;
        m.has_current[ch] = true;
        any = true;
        if (!doc["voltage"].isNull()) {
            float v = doc["voltage"].as<float>();
            if (v > 0.0f) { m.voltage_rms = v; m.has_voltage = true; }
        }
        if (!doc["power"].isNull()) {
            float pwr = doc["power"].as<float>();  // signed: + import / - export
            m.power_active[ch] = pwr;
            m.has_power[ch]    = true;
            m.direction[ch]    = (pwr >  0.05f) ? ACROUTER_DIR_CONSUMING
                               : (pwr < -0.05f) ? ACROUTER_DIR_SUPPLYING
                                                : ACROUTER_DIR_ZERO;
        }
    }

    m.valid = any;
    esp_event_post(ACROUTER_EVENT, ACROUTER_EVENT_POWER_UPDATE, &m, sizeof(m), 0);

    // Optional latch: keep re-posting this role at 5 Hz from the timer task, so the
    // stream survives even if HTTP is throttled (Task 17). slot: voltage -> 3, else ch.
    bool latch = doc["latch"] | false;
    if (latch) {
        int slot = (ch < 0) ? 3 : ch;
        portENTER_CRITICAL(&s_sim_mux);
        s_sim_latch[slot]    = m;
        s_sim_latch_on[slot] = true;
        portEXIT_CRITICAL(&s_sim_mux);
        sim_latch_ensure_timer();
        ESP_LOGI(TAG, "sim-inject (HTTP): role=%s LATCHED @5Hz", role);
        sendSuccess("sim measurement latched (re-posting at 5 Hz; POST /api/sim/stop to clear)");
        return;
    }

    ESP_LOGI(TAG, "sim-inject (HTTP): role=%s injected", role);
    sendSuccess("sim measurement injected");
}

// POST /api/sim/stop — clear all latched sim measurements and stop the 5 Hz re-post
// timer. The last-injected values simply age out (500 ms staleness) after this.
void WebServerManager::handleSimStop() {
    portENTER_CRITICAL(&s_sim_mux);
    for (int i = 0; i < 4; ++i) s_sim_latch_on[i] = false;
    portEXIT_CRITICAL(&s_sim_mux);
    if (s_sim_timer) {
        esp_timer_stop(s_sim_timer);   // keep the handle; a later latch restarts it
    }
    ESP_LOGI(TAG, "sim-inject: latches cleared, re-post timer stopped");
    sendSuccess("sim latches cleared");
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
                  (st.mode == RouterMode::BOOST) ? "boost" :
                  (st.mode == RouterMode::GRID_LIMIT) ? "grid_limit" : "unknown";
    doc["state"] = (st.state == RouterState::IDLE) ? "idle" :
                   (st.state == RouterState::INCREASING) ? "increasing" :
                   (st.state == RouterState::DECREASING) ? "decreasing" :
                   (st.state == RouterState::AT_MAXIMUM) ? "at_max" :
                   (st.state == RouterState::AT_MINIMUM) ? "at_min" : "error";

    doc["power_grid"] = st.power_grid;
    doc["dimmer"] = st.dimmer_percent;        // real primary output % (first enabled DimmerLink)
    doc["dimmer_count"] = dimmer_get_active_count();  // # enabled dimmers (per-dimmer detail in /api/dimmers/status)
    doc["target_level"] = st.target_level;
    doc["control_gain"] = st.control_gain;
    doc["balance_threshold"] = st.balance_threshold;
    doc["valid"] = st.valid;

    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();

    // v2.0: sensor source info (additive — old clients ignore unknown fields)
    doc["i2c_active"] = sensor_hub_has_i2c_source();
    doc["adc_active"] = sensor_hub_is_adc_active();
    if (dl_manager_is_initialized()) {
        doc["dimmerlink_count"] = dl_manager_get_active_count();
    }

    String json;
    serializeJson(doc, json);
    return json;
}

// System mains frequency + grid power factor from the smart sources (same
// selection as the MQTT metrics path). NaN when unavailable. Kept local to
// avoid a cross-component public API for two small reads.
static void metricsFreqPf(float* freq_hz, float* grid_pf) {
    float f = NAN, pf = NAN;
#if CONFIG_ACROUTER_RBAMP_SOURCE
    {
        rbamp_source_module_info_t mods[4];
        size_t n = 0;
        rbamp_source_get_modules(mods, 4, &n);
        for (size_t i = 0; i < n; i++) {
            if (!mods[i].online) continue;
            if (isnan(f) && !isnan(mods[i].frequency) && mods[i].frequency > 0.0f) f = mods[i].frequency;
            if (mods[i].role == RBAMP_ROLE_GRID && !isnan(mods[i].power_factor)) pf = mods[i].power_factor;
            else if (isnan(pf) && !isnan(mods[i].power_factor)) pf = mods[i].power_factor;
        }
    }
#endif
#if CONFIG_ACROUTER_ESPNOW_SOURCE
    if (isnan(f) || isnan(pf)) {
        esp_now_source_node_info_t nd[4];
        size_t n = 0;
        esp_now_source_get_nodes(nd, 4, &n);
        for (size_t i = 0; i < n; i++) {
            if (!nd[i].online) continue;
            if (isnan(f) && !isnan(nd[i].frequency) && nd[i].frequency > 0.0f) f = nd[i].frequency;
            if (isnan(pf) && !isnan(nd[i].power_factor)) pf = nd[i].power_factor;
        }
    }
#endif
    if (freq_hz) *freq_hz = f;
    if (grid_pf) *grid_pf = pf;
}

// Liveness of a DimmerLink output dimmer. I2C: match the dimmer's address against the
// DimmerLink manager's polled slots and report its online flag. ESP-NOW: best-effort
// (no per-poll liveness yet) — report initialized. Used by both /api/metrics and
// /api/dimmers/status so the UI can badge an unreachable DimmerLink module offline.
static bool dimmerOutputOnline(const dimmer_status_t& s) {
    if (s.type == DIMMER_TYPE_I2C) {
        for (uint8_t slot = 0; slot < DL_MAX_DEVICES; slot++) {
            const dl_device_state_t* dev = dl_manager_get_device(slot);
            if (dev && dev->config.enabled && dev->config.i2c_addr == s.i2c_address) {
                return dev->online;
            }
        }
        return false;
    }
    return s.initialized;  // ESP-NOW: best-effort until per-node liveness lands
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

    // v2.0: real-time voltage / current / frequency / power-factor / direction,
    // so the dashboard reads a single endpoint. Unavailable numeric fields are
    // null (NaN serializes to null); gated on slot validity / module online.
    sensor_hub_state_t hub;
    sensor_hub_get_state(&hub);
    const sh_slot_state_t& sv  = hub.slots[SH_SLOT_VOLTAGE];
    const sh_slot_state_t& sg  = hub.slots[SH_SLOT_GRID];
    const sh_slot_state_t& ss  = hub.slots[SH_SLOT_SOLAR];
    const sh_slot_state_t& sld = hub.slots[SH_SLOT_LOAD];
    metrics["voltage"]       = sv.valid  ? sv.value  : (float)NAN;
    metrics["current_grid"]  = sg.valid  ? sg.value  : (float)NAN;
    metrics["current_solar"] = ss.valid  ? ss.value  : (float)NAN;
    metrics["current_load"]  = sld.valid ? sld.value : (float)NAN;
    float f_hz = NAN, pf = NAN;
    metricsFreqPf(&f_hz, &pf);
    metrics["frequency"]     = f_hz;   // NaN -> null
    metrics["power_factor"]  = pf;     // NaN -> null
    // direction: grid-centric string, null when the grid slot has no data.
    static const char* dirNames[] = {"consuming", "supplying", "balanced", "unknown"};
    uint8_t di = static_cast<uint8_t>(sg.direction);
    if (sg.valid && di < 4) metrics["direction"] = dirNames[di];
    else                    metrics["direction"] = static_cast<const char*>(nullptr);

    // Dimmers array — DimmerLink outputs only (I2C + ESP-NOW). v2.0: legacy GPIO/direct-TRIAC
    // dimmers removed; dimming is only via DimmerLink. Light live shape for the dashboard
    // stream (level/target/enabled/online/transitioning); config detail is in /api/dimmers/status.
    JsonArray dimmers = doc["dimmers"].to<JsonArray>();
    for (uint8_t i = DIMMER_I2C_START; i < DIMMER_ESPNOW_END; i++) {
        dimmer_status_t status;
        if (dimmer_get_status(i, &status) != ESP_OK) continue;
        if (status.type == DIMMER_TYPE_NONE || !status.enabled) continue;

        JsonObject d = dimmers.add<JsonObject>();
        d["id"] = i;
        d["type"] = (status.type == DIMMER_TYPE_ESPNOW) ? "espnow" : "i2c";
        d["name"] = status.name;
        d["level"] = status.level_percent;
        d["target"] = status.target_percent;
        d["enabled"] = status.enabled;
        d["online"] = dimmerOutputOnline(status);
        d["transitioning"] = (status.state == DIMMER_STATE_TRANSITIONING);
    }

    // Relays array (all relays 0-3)
    JsonArray relays = doc["relays"].to<JsonArray>();
    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        relay_status_t status;
        if (relay_get_status(i, &status) != ESP_OK) continue;
        if (status.type == RELAY_TYPE_NONE && !status.enabled) continue;  // Skip unconfigured

        JsonObject r = relays.add<JsonObject>();
        r["id"] = i;
        r["name"] = status.name;
        r["is_on"] = status.is_on;
        r["enabled"] = status.enabled;
        r["power_w"] = status.nominal_power_w;
    }

    // Current mode for UI logic
    doc["mode"] = (st.mode == RouterMode::OFF) ? "off" :
                  (st.mode == RouterMode::AUTO) ? "auto" :
                  (st.mode == RouterMode::ECO) ? "eco" :
                  (st.mode == RouterMode::OFFGRID) ? "offgrid" :
                  (st.mode == RouterMode::MANUAL) ? "manual" :
                  (st.mode == RouterMode::BOOST) ? "boost" :
                  (st.mode == RouterMode::GRID_LIMIT) ? "grid_limit" : "unknown";

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
    doc["grid_current_limit"] = config.grid_current_limit;
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

// ============================================================================
// Auth (A3): bearer token on write/OTA; GET open; unset token = open (dev).
//
// DISABLED for the current release — open access on LAN. The bearer-token layer
// is kept in the tree (compiled out) and will be re-enabled / reimplemented under
// the esp_http_server migration. Flip to 1 to restore enforcement. With enforce
// off, _auth_token is forced empty (and any stale NVS token erased), so checkAuth/
// requireAuth allow everything and /api/auth/check reports enforced:false.
// ============================================================================
#define ACROUTER_AUTH_ENFORCE 0

void WebServerManager::loadAuthToken() {
    _auth_token[0] = '\0';
#if ACROUTER_AUTH_ENFORCE
    nvs_handle_t nvs;
    if (nvs_open("auth", NVS_READONLY, &nvs) != ESP_OK) return;
    size_t len = sizeof(_auth_token);
    if (nvs_get_str(nvs, "token", _auth_token, &len) != ESP_OK) _auth_token[0] = '\0';
    nvs_close(nvs);
    ESP_LOGI(TAG, "Auth: %s", _auth_token[0] ? "token set (write endpoints enforced)"
                                             : "no token (write open — dev mode)");
#else
    // Open build: erase any stale token so a leftover NVS value can never lock the
    // board out.
    nvs_handle_t nvs;
    if (nvs_open("auth", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, "token");
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGW(TAG, "Auth DISABLED in this build — open access (LAN)");
#endif
}

// Side-effect-free authorization test: true if token unset (open) OR a matching
// Bearer is present. Used where we can't emit a response yet (e.g. mid-upload).
bool WebServerManager::checkAuth() {
    if (_auth_token[0] == '\0') return true;   // auth disabled
    String hdr = _http_server->hasHeader("Authorization") ? _http_server->header("Authorization") : String();
    // Expect "Bearer <token>"
    return hdr.startsWith("Bearer ") && hdr.substring(7) == _auth_token;
}

// True if the request may proceed. Otherwise emits 401 and returns false.
// Wrapped around every mutating route.
bool WebServerManager::requireAuth() {
    if (checkAuth()) return true;
    _http_server->sendHeader("WWW-Authenticate", "Bearer");
    _http_server->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return false;
}

// GET /api/auth/check — login-gate probe. Send the candidate Bearer; 200 if valid or
// auth is disabled, 401 if enforced+invalid.
void WebServerManager::handleAuthCheck() {
    bool enforced = (_auth_token[0] != '\0');
    if (!enforced) { sendJsonResponse(200, "{\"authenticated\":true,\"enforced\":false}"); return; }
    String hdr = _http_server->hasHeader("Authorization") ? _http_server->header("Authorization") : String();
    if (hdr.startsWith("Bearer ") && hdr.substring(7) == _auth_token) {
        sendJsonResponse(200, "{\"authenticated\":true,\"enforced\":true}");
    } else {
        _http_server->sendHeader("WWW-Authenticate", "Bearer");
        _http_server->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    }
}

// POST /api/auth {"new_token":"..."} — set/change/clear the token. Gated by requireAuth
// (so changing requires the current token; first set works while unset). Empty clears.
void WebServerManager::setAuthToken(const char* token) {
#if !ACROUTER_AUTH_ENFORCE
    // Open build: refuse to set a token so the board can't be locked (POST /api/auth
    // + serial `auth-token set` become no-ops). Stays open.
    (void)token;
    _auth_token[0] = '\0';
    ESP_LOGW(TAG, "Auth disabled in this build — token ignored (open access)");
    return;
#else
    if (!token) token = "";
    strncpy(_auth_token, token, sizeof(_auth_token) - 1);
    _auth_token[sizeof(_auth_token) - 1] = '\0';
    nvs_handle_t nvs;
    if (nvs_open("auth", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "token", _auth_token);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGW(TAG, "Auth token %s (write endpoints %s)",
             _auth_token[0] ? "set" : "cleared",
             _auth_token[0] ? "ENFORCED" : "open");
#endif
}

void WebServerManager::handleSetAuth() {
    JsonDocument body;
    if (deserializeJson(body, _http_server->arg("plain"))) { sendError(400, "Invalid JSON"); return; }
    const char* nt = body["new_token"] | "";
    if (strlen(nt) >= sizeof(_auth_token)) { sendError(400, "token too long"); return; }
    setAuthToken(nt);
    sendSuccess(_auth_token[0] ? "Auth token set" : "Auth token cleared");
}

void WebServerManager::sendJsonResponse(int code, const String& json) {
    // CORS: reflect the configured web-app origin (or "*" when unset). This is the
    // single choke point for every /api JSON response. Methods/Headers are
    // preflight-only — the OPTIONS corsHandler supplies them.
    _http_server->sendHeader("Access-Control-Allow-Origin", corsOrigin());
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

// --- WiFi scan runs in a worker task, never in the web task ---
// esp_wifi_scan_start(block=true) takes 2-4s; on the C2 (1 request in flight) that
// froze the whole UI. Instead a worker does the blocking scan off the httpd task and
// caches results; the handler returns the cache immediately with a `scanning` flag.
struct WifiScanEntry { char ssid[33]; int8_t rssi; uint8_t channel; bool secured; };
static WifiScanEntry     s_wscan[20];
static uint8_t           s_wscan_count   = 0;
static volatile bool     s_wscan_busy    = false;
static int64_t           s_wscan_done_us = 0;          // last completion (esp_timer_get_time)
static SemaphoreHandle_t s_wscan_mtx     = nullptr;

static void wifi_scan_task(void*) {
    wifi_scan_config_t cfg = {};
    cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    cfg.scan_time.active.min = 100;
    cfg.scan_time.active.max = 300;
    if (esp_wifi_scan_start(&cfg, true) == ESP_OK) {   // blocking — but in THIS task, off httpd
        uint16_t n = 0;
        esp_wifi_scan_get_ap_num(&n);
        if (n > 20) n = 20;
        wifi_ap_record_t recs[20];
        uint16_t got = n;
        if (n > 0) esp_wifi_scan_get_ap_records(&got, recs); else got = 0;
        xSemaphoreTake(s_wscan_mtx, portMAX_DELAY);
        s_wscan_count = (uint8_t)got;
        for (uint16_t i = 0; i < got; i++) {
            strlcpy(s_wscan[i].ssid, (const char*)recs[i].ssid, sizeof(s_wscan[i].ssid));
            s_wscan[i].rssi    = recs[i].rssi;
            s_wscan[i].channel = recs[i].primary;
            s_wscan[i].secured = (recs[i].authmode != WIFI_AUTH_OPEN);
        }
        xSemaphoreGive(s_wscan_mtx);
    }
    esp_wifi_scan_stop();
    s_wscan_done_us = esp_timer_get_time();
    s_wscan_busy = false;
    vTaskDelete(nullptr);
}

// GET /api/wifi/scan — kicks a background scan (if idle + cache stale >10s) and
// returns cached results immediately with `scanning`. Client polls until
// scanning=false. Never blocks the web task.
void WebServerManager::handleWiFiScan() {
    if (!s_wscan_mtx) s_wscan_mtx = xSemaphoreCreateMutex();

    bool stale = (s_wscan_count == 0) ||
                 (esp_timer_get_time() - s_wscan_done_us > 10LL * 1000 * 1000);
    if (!s_wscan_busy && stale) {
        s_wscan_busy = true;
        if (xTaskCreatePinnedToCore(wifi_scan_task, "wifi_scan", 4096, nullptr, 5, nullptr, ACR_WEB_CORE) != pdPASS) {
            s_wscan_busy = false;   // couldn't spawn → report not scanning
        }
    }

    JsonDocument doc;
    doc["scanning"] = (bool)s_wscan_busy;
    JsonArray networks = doc["networks"].to<JsonArray>();
    if (s_wscan_mtx) xSemaphoreTake(s_wscan_mtx, portMAX_DELAY);
    for (uint8_t i = 0; i < s_wscan_count; i++) {
        JsonObject n = networks.add<JsonObject>();
        n["ssid"]       = s_wscan[i].ssid;
        n["rssi"]       = s_wscan[i].rssi;
        n["encryption"] = s_wscan[i].secured ? "secured" : "open";
        n["channel"]    = s_wscan[i].channel;
    }
    doc["count"] = s_wscan_count;
    if (s_wscan_mtx) xSemaphoreGive(s_wscan_mtx);

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

// The web app is hosted externally (not on the device). Load its URL from NVS;
// empty => open mode (CORS "*", / shows a stub). Set => / redirects there and CORS
// reflects it as the allowed origin.
void WebServerManager::loadWebAppUrl() {
    _web_app_url = "";
    nvs_handle_t nvs;
    if (nvs_open("web", NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "Web app URL: not set (open — CORS *, / stub)");
        return;
    }
    char buf[128] = {0};
    size_t len = sizeof(buf);
    if (nvs_get_str(nvs, "app_url", buf, &len) == ESP_OK) _web_app_url = buf;
    nvs_close(nvs);
    ESP_LOGI(TAG, "Web app URL: %s", _web_app_url.length() ? _web_app_url.c_str()
                                                           : "not set (open)");
}

// Persist/change/clear the external web-app URL (also used by the `web-url` serial
// command). Empty clears (back to open).
void WebServerManager::setWebAppUrl(const char* url) {
    _web_app_url = url ? url : "";
    nvs_handle_t nvs;
    if (nvs_open("web", NVS_READWRITE, &nvs) == ESP_OK) {
        if (_web_app_url.length()) nvs_set_str(nvs, "app_url", _web_app_url.c_str());
        else                       nvs_erase_key(nvs, "app_url");
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGW(TAG, "Web app URL %s%s", _web_app_url.length() ? "set: " : "cleared (open)",
             _web_app_url.c_str());
}

// CORS origin to advertise: the configured web-app URL, or "*" when unset.
String WebServerManager::corsOrigin() {
    return _web_app_url.length() ? _web_app_url : String("*");
}

// GET /api/web/config — report the external web-app URL (open if empty).
void WebServerManager::handleGetWebConfig() {
    JsonDocument doc;
    doc["app_url"] = _web_app_url;
    doc["open"]    = (_web_app_url.length() == 0);
    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

// POST /api/web/config {"app_url":"http://host:port"} — set/clear the external app URL.
void WebServerManager::handleSetWebConfig() {
    JsonDocument body;
    if (deserializeJson(body, _http_server->arg("plain"))) { sendError(400, "Invalid JSON"); return; }
    const char* url = body["app_url"] | "";
    if (strlen(url) >= 128) { sendError(400, "url too long"); return; }
    setWebAppUrl(url);
    sendSuccess(_web_app_url.length() ? "Web app URL set" : "Web app URL cleared");
}

void WebServerManager::handleDashboard() {
    // The SPA is NOT on the device. If an external app URL is configured, redirect
    // the browser there (pass the device IP so the app can target this device).
    // Otherwise show a minimal stub explaining where the UI lives.
    if (_web_app_url.length()) {
        String loc = _web_app_url + "?device=" + WiFiManager::getInstance().getSTAIP().toString();
        _http_server->sendHeader("Location", loc);
        _http_server->send(302, "text/plain", "");
        return;
    }
    String html = "<!doctype html><meta charset=utf-8><title>ACRouter</title>"
                  "<body style='font-family:sans-serif;max-width:32rem;margin:3rem auto'>"
                  "<h2>ACRouter</h2><p>The web UI is hosted externally. Configure its URL via "
                  "<code>POST /api/web/config {\"app_url\":\"http://host:port\"}</code> "
                  "or serial <code>web-url set &lt;url&gt;</code>.</p>"
                  "<p>Device API is live at <code>/api/*</code>.</p></body>";
    _http_server->send(200, "text/html", html);
}

void WebServerManager::handleWiFiPage() {
    String html = getWiFiConfigPage();
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

    // v2.0: adc_channels, dimmer_ch1/ch2 and zerocross_* are no longer parsed —
    // on-chip ADC and direct GPIO/TRIAC dimming were removed (dimming is DimmerLink-
    // only, commissioned via /api/dimmers). Silently ignored for backward compat.
    // Sensing is commissioned per module via /api/rbamp/modules and /api/espnow/nodes.

    // Parse I2C (Tier-1 transport). Nested i2c{ bus0{sda,scl,freq_khz,enabled},
    // bus1{...}, rbamp_bus, rbamp_drdy_gpio }.
    if (doc["i2c"].is<JsonObject>()) {
        JsonObject i2c = doc["i2c"];
        if (i2c["bus0"].is<JsonObject>()) {
            JsonObject b = i2c["bus0"];
            if (b.containsKey("sda"))      config.i2c_sda_gpio = b["sda"] | config.i2c_sda_gpio;
            if (b.containsKey("scl"))      config.i2c_scl_gpio = b["scl"] | config.i2c_scl_gpio;
            if (b.containsKey("freq_khz")) config.i2c_freq_hz  = (uint32_t)((uint16_t)(b["freq_khz"] | 100)) * 1000;
            if (b.containsKey("enabled"))  config.i2c_enabled  = b["enabled"] | config.i2c_enabled;
        }
        if (i2c["bus1"].is<JsonObject>()) {
            JsonObject b = i2c["bus1"];
            if (b.containsKey("sda"))      config.i2c1_sda_gpio = b["sda"] | config.i2c1_sda_gpio;
            if (b.containsKey("scl"))      config.i2c1_scl_gpio = b["scl"] | config.i2c1_scl_gpio;
            if (b.containsKey("freq_khz")) config.i2c1_freq_hz  = (uint32_t)((uint16_t)(b["freq_khz"] | 100)) * 1000;
            if (b.containsKey("enabled"))  config.i2c1_enabled  = b["enabled"] | config.i2c1_enabled;
        }
        if (i2c.containsKey("rbamp_bus"))       config.rbamp_i2c_bus  = i2c["rbamp_bus"] | config.rbamp_i2c_bus;
        if (i2c.containsKey("rbamp_drdy_gpio")) config.rbamp_drdy_gpio = (int8_t)(i2c["rbamp_drdy_gpio"] | config.rbamp_drdy_gpio);
    }

    // Parse ESP-NOW transport (Tier-1 hardware)
    if (doc.containsKey("espnow_channel")) {
        config.espnow_channel = doc["espnow_channel"] | 1;
    }
    if (doc.containsKey("espnow_enabled")) {
        config.espnow_enabled = doc["espnow_enabled"] | false;
    }

    // Parse relay channels
    // Per-key merge: fall back to the CURRENT value for any missing subkey, not a
    // hardcoded default — otherwise a partial PATCH of one relay field silently resets the
    // others (gpio/active_high/enabled footgun). Matches the i2c bus merge behaviour.
    if (doc.containsKey("relay_ch1")) {
        config.relay_ch1.gpio = doc["relay_ch1"]["gpio"] | config.relay_ch1.gpio;
        config.relay_ch1.active_high = doc["relay_ch1"]["active_high"] | config.relay_ch1.active_high;
        config.relay_ch1.enabled = doc["relay_ch1"]["enabled"] | config.relay_ch1.enabled;
    }

    if (doc.containsKey("relay_ch2")) {
        config.relay_ch2.gpio = doc["relay_ch2"]["gpio"] | config.relay_ch2.gpio;
        config.relay_ch2.active_high = doc["relay_ch2"]["active_high"] | config.relay_ch2.active_high;
        config.relay_ch2.enabled = doc["relay_ch2"]["enabled"] | config.relay_ch2.enabled;
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

// Detached one-shot: let the HTTP response flush, then restart. Runs off the web
// task so the handler returns immediately (a blocking delay in the single httpd
// task would freeze the whole UI on the C2 — see coding standard).
static void reboot_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

void WebServerManager::handleSystemReboot() {
    ESP_LOGW(TAG, "Reboot requested via API");

    JsonDocument response;
    response["success"] = true;
    response["message"] = "Rebooting shortly...";

    String json;
    serializeJson(response, json);
    sendJsonResponse(200, json);

    // Defer the restart to a detached task; the handler returns now.
    xTaskCreatePinnedToCore(reboot_task, "reboot", 2048, nullptr, 5, nullptr, ACR_WEB_CORE);
}

// ============================================================================
// Helper Functions for Hardware Config API
// ============================================================================

static const char* getInterfaceType(dimmer_type_t type) {
    switch (type) {
        case DIMMER_TYPE_I2C: return "I2C";
        case DIMMER_TYPE_ESPNOW: return "ESP-NOW";
        default: return "NONE";
    }
}

static const char* getInterfaceType(relay_type_t type) {
    switch (type) {
        case RELAY_TYPE_GPIO: return "GPIO";
        case RELAY_TYPE_I2C: return "I2C";
        case RELAY_TYPE_ESPNOW: return "ESP-NOW";
        default: return "NONE";
    }
}

// Resolve a device's current_sensor_id (v2.0 = rbAmp module I2C address, -1 =
// none) to that module's live RMS current. Returns NaN (serialized as null)
// when unassigned or the module isn't currently discovered.
#if CONFIG_ACROUTER_RBAMP_SOURCE
static float resolve_sensor_current(int8_t sensor_id) {
    if (sensor_id < 0) return NAN;
    rbamp_source_module_info_t mods[4];
    size_t n = 0;
    rbamp_source_get_modules(mods, 4, &n);
    for (size_t i = 0; i < n; i++) {
        if (mods[i].i2c_addr == (uint8_t)sensor_id) return mods[i].current;
    }
    return NAN;
}
#else
static float resolve_sensor_current(int8_t) { return NAN; }
#endif

// ============================================================================
// Utility - Build Hardware Config JSON
// ============================================================================

String WebServerManager::buildHardwareConfigJson() {
    HardwareConfigManager& hw = HardwareConfigManager::getInstance();
    const HardwareConfig& config = hw.getConfig();

    JsonDocument doc;

    // v2.0: the internal-ADC `sensors[]` field is fully removed. Sensing is
    // module-centric — see /api/sensors, /api/rbamp/modules, /api/espnow/nodes.

    // ========================================
    // I2C Section (Tier-1 transport: buses + per-family bus assignment)
    // ========================================
    JsonObject i2c = doc["i2c"].to<JsonObject>();
    JsonObject bus0 = i2c["bus0"].to<JsonObject>();
    bus0["sda"] = config.i2c_sda_gpio;
    bus0["scl"] = config.i2c_scl_gpio;
    bus0["freq_khz"] = (uint16_t)(config.i2c_freq_hz / 1000);
    bus0["enabled"] = config.i2c_enabled;
    JsonObject bus1 = i2c["bus1"].to<JsonObject>();
    bus1["sda"] = config.i2c1_sda_gpio;
    bus1["scl"] = config.i2c1_scl_gpio;
    bus1["freq_khz"] = (uint16_t)(config.i2c1_freq_hz / 1000);
    bus1["enabled"] = config.i2c1_enabled;
    i2c["rbamp_bus"] = config.rbamp_i2c_bus;         // 0|1
    i2c["rbamp_drdy_gpio"] = config.rbamp_drdy_gpio;  // -1 = timer poll

    // ========================================
    // Dimmers Section
    // ========================================
    JsonArray dimmers = doc["dimmers"].to<JsonArray>();
    // v2.0 dimmers live in the DimmerLink I2C (4-11) + ESP-NOW (12+) range; the legacy
    // GPIO 0-3 slots are always TYPE_NONE. Iterate the real range (#32).
    for (uint8_t i = DIMMER_I2C_START; i < DIMMER_ESPNOW_END; i++) {
        dimmer_status_t status;
        if (dimmer_get_status(i, &status) != ESP_OK) continue;

        // Skip unconfigured dimmers
        if (status.type == DIMMER_TYPE_NONE) continue;

        JsonObject dimmer = dimmers.add<JsonObject>();
        dimmer["id"] = i;
        dimmer["type"] = dimmer_type_str(status.type);
        dimmer["interface"] = getInterfaceType(status.type);
        dimmer["enabled"] = status.enabled;
        dimmer["name"] = status.name;

        // Common fields - configuration only (runtime state in /api/dimmers/status)
        dimmer["nominal_power_w"] = status.nominal_power_w;
        int8_t d_sensor = dimmer_get_current_sensor(i);
        dimmer["current_sensor_id"] = d_sensor;           // v2.0: rbAmp module addr (-1=none)
        dimmer["current_sensor_a"]  = resolve_sensor_current(d_sensor);  // live A, null if unavailable
        dimmer["curve"] = static_cast<uint8_t>(status.curve);
        // Note: 'mode' is runtime control state, available in GET /api/dimmers/status
        dimmer["min_level"] = status.min_level;
        dimmer["max_level"] = status.max_level;
        dimmer["default_level"] = 0;  // TODO: get from config
        dimmer["ramp_time_ms"] = 1000;  // TODO: get from config
        dimmer["priority"] = dimmer_get_priority(i);
    }

    // ========================================
    // Relays Section
    // ========================================
    JsonArray relays = doc["relays"].to<JsonArray>();
    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        relay_status_t status;
        if (relay_get_status(i, &status) != ESP_OK) continue;

        // Skip unconfigured relays
        if (status.type == RELAY_TYPE_NONE) continue;

        JsonObject rel = relays.add<JsonObject>();
        rel["id"] = i;
        rel["type"] = relay_type_str(status.type);
        rel["interface"] = getInterfaceType(status.type);
        rel["enabled"] = status.enabled;
        rel["name"] = status.name;

        // GPIO-specific
        if (status.type == RELAY_TYPE_GPIO) {
            rel["gpio"] = status.gpio_pin;
            rel["active_high"] = status.active_high;
        }

        // Common fields - configuration only (runtime state in /api/relays/status)
        rel["nominal_power_w"] = status.nominal_power_w;
        rel["current_sensor_id"] = status.current_sensor_id;              // v2.0: rbAmp module addr (-1=none)
        rel["current_sensor_a"]  = resolve_sensor_current(status.current_sensor_id);  // live A, null if unavailable
        // Note: 'mode' is runtime control state, available in GET /api/relays/status
        rel["min_on_time_s"] = status.min_on_time_s;
        rel["min_off_time_s"] = status.min_off_time_s;
        rel["priority"] = status.priority;
    }

    // ========================================
    // System Section
    // ========================================
    JsonObject system = doc["system"].to<JsonObject>();
    // zerocross_* removed here in v2.0 — direct GPIO/TRIAC dimming is gone (DimmerLink
    // only), so the zero-cross detector no longer exists. Not surfaced.
    system["led_status_gpio"] = config.led_status_gpio;
    system["led_load_gpio"] = config.led_load_gpio;
    system["espnow_channel"] = config.espnow_channel;
    system["espnow_enabled"] = config.espnow_enabled;

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

// Detached worker for URL-based OTA: downloads + flashes off the web task, then
// restarts on success. `arg` is a strdup'd URL owned by this task.
static void ota_url_task(void* arg) {
    char* url = static_cast<char*>(arg);
    ESP_LOGI("WebServer", "OTA-from-URL task: %s", url);
    bool ok = OTAManager::getInstance().updateFromURL(url);
    free(url);
    if (ok) {
        ESP_LOGI("WebServer", "OTA from URL ok — restarting");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    ESP_LOGE("WebServer", "OTA from URL failed");
    vTaskDelete(nullptr);
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

    // Send immediate response, then do the (long, blocking) download+flash in a
    // detached task — it must NOT run in the httpd task (would freeze the web UI).
    JsonDocument responseDoc;
    responseDoc["success"] = true;
    responseDoc["message"] = "OTA update started. Device will reboot after download.";

    String json;
    serializeJson(responseDoc, json);
    sendJsonResponse(200, json);

    char* url_copy = strdup(url.c_str());   // freed by the task
    if (url_copy) {
        xTaskCreatePinnedToCore(ota_url_task, "ota_url", 6144, url_copy, 5, nullptr, ACR_WEB_CORE);
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

// ============================================================================
// NTP API Handlers
// ============================================================================

void WebServerManager::handleGetNTPStatus() {
    NTPManager& ntp = NTPManager::getInstance();

    JsonDocument doc;
    doc["running"] = ntp.isRunning();
    doc["synced"] = ntp.isTimeSynced();

    if (ntp.isTimeSynced()) {
        doc["current_time"] = ntp.getTimeString();
        uint32_t last = ntp.getLastSyncTime();
        if (last > 0) {
            doc["last_sync_ago_sec"] = (millis() - last) / 1000;
        }
    }

    doc["sync_interval_sec"] = 3600; // 1 hour

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleGetNTPConfig() {
    NTPManager& ntp = NTPManager::getInstance();

    JsonDocument doc;
    // Friend access to private members
    doc["ntp_server"] = ntp._ntp_server;
    doc["timezone"] = ntp._timezone;
    doc["gmt_offset_sec"] = ntp._gmt_offset_sec;
    doc["daylight_offset_sec"] = ntp._daylight_offset_sec;

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleSetNTPConfig() {
    ESP_LOGI(TAG, "POST /api/ntp/config - Setting NTP configuration");

    if (!_http_server->hasArg("plain")) {
        ESP_LOGW(TAG, "Missing request body");
        sendError(400, "Missing request body");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _http_server->arg("plain"));
    if (error) {
        ESP_LOGW(TAG, "Invalid JSON: %s", error.c_str());
        sendError(400, "Invalid JSON");
        return;
    }

    NTPManager& ntp = NTPManager::getInstance();
    bool changed = false;

    // Update NTP server
    if (doc["ntp_server"].is<const char*>()) {
        const char* server = doc["ntp_server"].as<const char*>();
        ESP_LOGI(TAG, "Setting NTP server: %s", server);
        ntp.setNTPServer(server);
        changed = true;
    }

    // Update timezone
    if (doc["timezone"].is<const char*>()) {
        const char* tz = doc["timezone"].as<const char*>();
        int gmt = doc.containsKey("gmt_offset_sec") ? doc["gmt_offset_sec"].as<int>() : 0;
        int dst = doc.containsKey("daylight_offset_sec") ? doc["daylight_offset_sec"].as<int>() : 0;
        ESP_LOGI(TAG, "Setting timezone: %s (GMT offset: %d, DST offset: %d)", tz, gmt, dst);
        ntp.setTimezone(tz, gmt, dst);
        changed = true;
    }

    if (changed) {
        ESP_LOGI(TAG, "NTP configuration updated and saved to NVS");
        sendSuccess("NTP configuration updated and saved");
    } else {
        ESP_LOGW(TAG, "No valid configuration fields provided");
        sendError(400, "No valid configuration fields provided");
    }
}

void WebServerManager::handleNTPSync() {
    NTPManager& ntp = NTPManager::getInstance();
    ntp.forceSync();
    sendSuccess("NTP sync requested");
}

// ============================================================================
// Relays API Handlers
// ============================================================================

void WebServerManager::handleRelaysPage() {
    String html = getRelaysPage();
    _http_server->send(200, "text/html", html);
}

void WebServerManager::handleGetRelaysStatus() {
    JsonDocument doc;

    // Use new C API to get relay counts
    uint8_t enabled_count = 0;
    uint8_t on_count = 0;
    uint32_t total_power = 0;

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

        JsonObject r = relays.add<JsonObject>();
        r["id"] = i;
        r["name"] = status.name;
        r["enabled"] = status.enabled;
        r["type"] = relay_type_str(status.type);
        r["gpio"] = status.gpio_pin;
        r["active_high"] = status.active_high;
        r["power_w"] = status.nominal_power_w;
        r["min_on"] = status.min_on_time_s;
        r["min_off"] = status.min_off_time_s;
        r["is_on"] = status.is_on;
        r["initialized"] = status.initialized;
        r["state"] = relay_state_str(status.state);
        r["priority"] = relay_get_priority(i);

        if (status.enabled) {
            enabled_count++;
            if (status.is_on) {
                on_count++;
                total_power += status.nominal_power_w;
            }
        }
    }

    doc["initialized"] = true;  // Always initialized with new C API
    doc["enabled_count"] = enabled_count;
    doc["on_count"] = on_count;
    doc["total_power_w"] = total_power;

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleRelaysAllOn() {
    ESP_LOGW(TAG, "All relays ON requested via API");

    JsonDocument doc;
    doc["success"] = true;
    JsonArray relays = doc["relays"].to<JsonArray>();

    // Turn on all enabled relays using new C API
    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        if (!relay_is_enabled(i)) continue;

        esp_err_t result = relay_turn_on(i, false);

        JsonObject r = relays.add<JsonObject>();
        r["relay_id"] = i;

        relay_status_t status;
        if (relay_get_status(i, &status) == ESP_OK) {
            if (status.is_on) {
                r["state"] = "on";
            } else if (status.state == RELAY_STATE_DEBOUNCE) {
                r["state"] = "debounce";
            } else {
                r["state"] = "off";
            }
        }

        r["success"] = (result == ESP_OK);
    }

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleRelaysAllOff() {
    ESP_LOGW(TAG, "All relays OFF requested via API");

    JsonDocument doc;
    doc["success"] = true;
    JsonArray relays = doc["relays"].to<JsonArray>();

    // Turn off all relays (force mode) using new C API
    relay_all_off(true); // Force off all

    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        if (!relay_is_enabled(i)) continue;

        JsonObject r = relays.add<JsonObject>();
        r["relay_id"] = i;
        r["state"] = "off";
        r["success"] = true;
    }

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleRelayOn(uint8_t id) {
    if (id >= RELAY_MAX_COUNT) {
        sendError(400, "Invalid relay ID");
        return;
    }

    // Check if force flag is set
    bool force = false;
    if (_http_server->hasArg("plain")) {
        JsonDocument doc;
        if (!deserializeJson(doc, _http_server->arg("plain"))) {
            force = doc["force"] | false;
        }
    }

    // Use new C API to turn on relay
    esp_err_t result = relay_turn_on(id, force);

    if (result == ESP_OK) {
        JsonDocument response;
        response["relay_id"] = id;
        response["state"] = "on";
        response["success"] = true;

        String json;
        serializeJson(response, json);
        sendJsonResponse(200, json);
    } else {
        relay_status_t status;
        if (relay_get_status(id, &status) == ESP_OK && status.state == RELAY_STATE_DEBOUNCE) {
            JsonDocument response;
            response["relay_id"] = id;
            response["state"] = "debounce";
            response["success"] = false;
            response["error"] = "Debounce active";

            String json;
            serializeJson(response, json);
            sendJsonResponse(400, json);
        } else {
            sendError(500, "Failed to turn on relay");
        }
    }
}

void WebServerManager::handleRelayOff(uint8_t id) {
    if (id >= RELAY_MAX_COUNT) {
        sendError(400, "Invalid relay ID");
        return;
    }

    // Check if force flag is set
    bool force = false;
    if (_http_server->hasArg("plain")) {
        JsonDocument doc;
        if (!deserializeJson(doc, _http_server->arg("plain"))) {
            force = doc["force"] | false;
        }
    }

    // Use new C API to turn off relay
    esp_err_t result = relay_turn_off(id, force);

    if (result == ESP_OK) {
        JsonDocument response;
        response["relay_id"] = id;
        response["state"] = "off";
        response["success"] = true;

        String json;
        serializeJson(response, json);
        sendJsonResponse(200, json);
    } else {
        relay_status_t status;
        if (relay_get_status(id, &status) == ESP_OK && status.state == RELAY_STATE_DEBOUNCE) {
            JsonDocument response;
            response["relay_id"] = id;
            response["state"] = "debounce";
            response["success"] = false;
            response["error"] = "Debounce active";

            String json;
            serializeJson(response, json);
            sendJsonResponse(400, json);
        } else {
            sendError(500, "Failed to turn off relay");
        }
    }
}

void WebServerManager::handleRelayConfig(uint8_t id) {
    if (id >= RELAY_MAX_COUNT) {
        sendError(400, "Invalid relay ID");
        return;
    }

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

    // Update configuration using new C API
    bool success = true;

    if (doc["name"].is<const char*>()) {
        if (relay_set_name(id, doc["name"].as<const char*>()) != ESP_OK) {
            success = false;
        }
    }

    if (doc["gpio"].is<int>()) {
        if (relay_set_gpio(id, doc["gpio"].as<int8_t>()) != ESP_OK) {
            success = false;
        }
    }

    if (doc["power_w"].is<int>()) {
        if (relay_set_nominal_power(id, doc["power_w"].as<uint16_t>()) != ESP_OK) {
            success = false;
        }
    }

    if (doc["min_on"].is<int>()) {
        if (relay_set_min_on_time(id, doc["min_on"].as<uint16_t>()) != ESP_OK) {
            success = false;
        }
    }

    if (doc["min_off"].is<int>()) {
        if (relay_set_min_off_time(id, doc["min_off"].as<uint16_t>()) != ESP_OK) {
            success = false;
        }
    }

    if (doc["active_high"].is<bool>()) {
        if (relay_set_active_high(id, doc["active_high"].as<bool>()) != ESP_OK) {
            success = false;
        }
    }

    if (doc["enabled"].is<bool>()) {
        if (relay_set_enabled(id, doc["enabled"].as<bool>()) != ESP_OK) {
            success = false;
        }
    }

    if (doc["priority"].is<int>()) {
        if (relay_set_priority(id, doc["priority"].as<uint8_t>()) != ESP_OK) {
            success = false;
        }
    }

    // v2.0: current_sensor_id = rbAmp module I2C address, or -1 = none.
    if (doc["current_sensor_id"].is<int>()) {
        int v = doc["current_sensor_id"].as<int>();
        if (v == -1 || (v >= 0x08 && v <= 0x77)) {
            if (relay_set_current_sensor(id, (int8_t)v) != ESP_OK) success = false;
        } else {
            ESP_LOGW(TAG, "Relay %d: ignoring current_sensor_id=%d (not -1 or 0x08-0x77)", id, v);
        }
    }

    if (success) {
        // Save to NVS
        relay_save_config(id);

        ESP_LOGI(TAG, "Relay %d configuration updated via API", id);
        // Relay enable/priority/power feed the RouterController priority map — rebuild
        // so the web change applies now, matching the MQTT path (D5).
        RouterController::getInstance().refreshPriorityMap();
        sendSuccess("Relay configuration saved");
    } else {
        sendError(500, "Failed to configure relay");
    }
}

// ============================================================================
// Dimmers API Handlers
// ============================================================================


void WebServerManager::handleGetDimmersStatus() {
    JsonDocument doc;

    doc["initialized"] = dimmer_manager_is_initialized();
    doc["enabled_count"] = dimmer_get_active_count();
    doc["mains_frequency"] = 0;  // v2.0: GPIO zero-cross removed; mains freq is per-module in /api/metrics & /api/sensors

    JsonArray dimmers = doc["dimmers"].to<JsonArray>();

    // DimmerLink outputs only (I2C 4-11 + ESP-NOW 12+). v2.0: legacy GPIO/direct-TRIAC
    // dimmers (0-3) removed — dimming is only via DimmerLink. Full config shape for Settings.
    for (uint8_t i = DIMMER_I2C_START; i < DIMMER_ESPNOW_END; i++) {
        dimmer_status_t status;
        if (dimmer_get_status(i, &status) != ESP_OK) continue;
        if (status.type == DIMMER_TYPE_NONE || !status.enabled) continue;

        JsonObject d = dimmers.add<JsonObject>();
        d["id"] = i;
        const bool is_espnow = (status.type == DIMMER_TYPE_ESPNOW);
        d["type"] = is_espnow ? "espnow" : "i2c";
        if (is_espnow) {
            char mac[18];
            snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                     status.espnow_mac[0], status.espnow_mac[1], status.espnow_mac[2],
                     status.espnow_mac[3], status.espnow_mac[4], status.espnow_mac[5]);
            d["mac"] = mac;
        } else {
            char addr[8];
            snprintf(addr, sizeof(addr), "0x%02X", status.i2c_address);
            d["addr"] = addr;
            d["bus"] = status.i2c_bus;
        }
        d["name"] = status.name;
        d["enabled"] = status.enabled;
        d["online"] = dimmerOutputOnline(status);
        d["power_w"] = status.nominal_power_w;
        d["curve"] = static_cast<uint8_t>(status.curve);
        d["level"] = status.level_percent;
        d["target"] = status.target_percent;
        d["initialized"] = status.initialized;
        d["transitioning"] = (status.state == DIMMER_STATE_TRANSITIONING);
        d["priority"] = dimmer_get_priority(i);
    }

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleDimmersAllOn() {
    ESP_LOGW(TAG, "All dimmers to 100%% requested via API");

    JsonDocument doc;
    doc["success"] = true;
    JsonArray dimmers = doc["dimmers"].to<JsonArray>();

    // Set all enabled dimmers to 100% — across ALL transports (GPIO/I2C/ESP-NOW), not
    // just the GPIO range, so a bound ESP-NOW output node is driven too.
    for (uint8_t i = 0; i < dimmer_get_max_count(); i++) {
        if (!dimmer_is_enabled(i)) continue;

        esp_err_t err = dimmer_set_level(i, 100);

        JsonObject d = dimmers.add<JsonObject>();
        d["dimmer_id"] = i;
        d["level"] = 100;
        d["success"] = (err == ESP_OK);
    }

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleDimmersAllOff() {
    ESP_LOGW(TAG, "All dimmers OFF requested via API");

    JsonDocument doc;
    doc["success"] = true;
    JsonArray dimmers = doc["dimmers"].to<JsonArray>();

    // Set all enabled dimmers to 0% — across ALL transports (incl. bound ESP-NOW nodes).
    for (uint8_t i = 0; i < dimmer_get_max_count(); i++) {
        if (!dimmer_is_enabled(i)) continue;

        dimmer_set_level(i, 0);

        JsonObject d = dimmers.add<JsonObject>();
        d["dimmer_id"] = i;
        d["level"] = 0;
        d["success"] = true;
    }

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleDimmerLevel(uint8_t id) {
    if (id >= DIMMER_GPIO_COUNT) {
        sendError(400, "Invalid dimmer ID");
        return;
    }

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

    if (!doc["level"].is<int>()) {
        sendError(400, "Missing 'level' field");
        return;
    }

    uint8_t level = doc["level"].as<uint8_t>();
    if (level > 100) {
        sendError(400, "Level must be 0-100");
        return;
    }

    // Check for smooth transition
    uint32_t transition_ms = doc["transition_ms"] | 0;

    esp_err_t err;
    if (transition_ms > 0) {
        err = dimmer_set_level_smooth(id, level, transition_ms);
    } else {
        err = dimmer_set_level(id, level);
    }

    if (err == ESP_OK) {
        JsonDocument response;
        response["success"] = true;
        response["dimmer_id"] = id;
        response["level"] = level;

        String json;
        serializeJson(response, json);
        sendJsonResponse(200, json);
    } else {
        sendError(500, "Failed to set dimmer level");
    }
}

void WebServerManager::handleDimmerConfig(uint8_t id) {
    if (id >= DIMMER_GPIO_COUNT) {
        sendError(400, "Invalid dimmer ID");
        return;
    }

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

    dimmer_t* dimmer = dimmer_get(id);
    if (!dimmer) {
        sendError(500, "Dimmer not available");
        return;
    }

    // Update configuration from JSON
    if (doc["name"].is<const char*>()) {
        dimmer_set_name(id, doc["name"].as<const char*>());
    }

    if (doc["power_w"].is<int>()) {
        dimmer_set_nominal_power(id, doc["power_w"].as<uint16_t>());
    }

    if (doc["curve"].is<int>()) {
        dimmer_set_curve(id, static_cast<dimmer_curve_t>(doc["curve"].as<uint8_t>()));
    }

    if (doc["enabled"].is<bool>()) {
        dimmer_set_enabled(id, doc["enabled"].as<bool>());
    }

    if (doc["priority"].is<int>()) {
        dimmer_set_priority(id, doc["priority"].as<uint8_t>());
    }

    // v2.0: current_sensor_id = rbAmp module I2C address, or -1 = none.
    if (doc["current_sensor_id"].is<int>()) {
        int v = doc["current_sensor_id"].as<int>();
        if (v == -1 || (v >= 0x08 && v <= 0x77)) {
            dimmer_set_current_sensor(id, (int8_t)v);
        } else {
            ESP_LOGW(TAG, "Dimmer %d: ignoring current_sensor_id=%d (not -1 or 0x08-0x77)", id, v);
        }
    }

    // Save to NVS
    if (dimmer_save_config(id) == ESP_OK) {
        ESP_LOGI(TAG, "Dimmer %d configuration updated via API", id);
        // enabled/priority/power affect the RouterController priority map — rebuild it
        // so a web change takes effect immediately, matching the MQTT path (D5).
        RouterController::getInstance().refreshPriorityMap();
        sendSuccess("Dimmer configuration saved");
    } else {
        sendError(500, "Failed to save dimmer configuration");
    }
}

// GET handler for dimmer level: /api/dimmers/X/level?value=N
void WebServerManager::handleDimmerLevelGet(uint8_t id) {
    if (id >= DIMMER_GPIO_COUNT) {
        sendError(400, "Invalid dimmer ID");
        return;
    }

    // Check for value parameter
    if (!_http_server->hasArg("value")) {
        // No value - return current level
        dimmer_status_t status;
        if (dimmer_get_status(id, &status) == ESP_OK) {
            JsonDocument response;
            response["dimmer_id"] = id;
            response["level"] = status.level_percent;
            response["target"] = status.target_percent;
            response["enabled"] = status.enabled;
            response["initialized"] = status.initialized;

            String json;
            serializeJson(response, json);
            sendJsonResponse(200, json);
        } else {
            sendError(500, "Failed to get dimmer status");
        }
        return;
    }

    // Parse value and set level
    int level = _http_server->arg("value").toInt();
    if (level < 0 || level > 100) {
        sendError(400, "Level must be 0-100");
        return;
    }

    esp_err_t err = dimmer_set_level(id, (uint8_t)level);

    if (err == ESP_OK) {
        JsonDocument response;
        response["success"] = true;
        response["dimmer_id"] = id;
        response["level"] = level;

        String json;
        serializeJson(response, json);
        sendJsonResponse(200, json);
    } else {
        sendError(500, "Failed to set dimmer level");
    }
}

// GET handler for dimmer config: /api/dimmers/X/config
void WebServerManager::handleDimmerConfigGet(uint8_t id) {
    if (id >= DIMMER_GPIO_COUNT) {
        sendError(400, "Invalid dimmer ID");
        return;
    }

    const dimmer_t* dimmer = dimmer_get_const(id);
    if (!dimmer) {
        sendError(500, "Dimmer not available");
        return;
    }

    JsonDocument response;
    response["id"] = id;
    response["name"] = dimmer->name;
    response["enabled"] = dimmer->enabled;
    response["gpio"] = dimmer->gpio_pin;
    response["power_w"] = dimmer->nominal_power_w;
    response["curve"] = (uint8_t)dimmer->curve;
    response["min_level"] = dimmer->min_level;
    response["max_level"] = dimmer->max_level;
    response["initialized"] = dimmer->initialized;
    response["priority"] = dimmer_get_priority(id);

    String json;
    serializeJson(response, json);
    sendJsonResponse(200, json);
}

// ============================================================================
// v2.0: I2C / DimmerLink / Sensor Hub API Handlers
// ============================================================================

static const char* dl_role_name(uint8_t role) {
    switch (role) {
        case DL_ROLE_CURRENT_GRID:  return "current_grid";
        case DL_ROLE_CURRENT_SOLAR: return "current_solar";
        case DL_ROLE_CURRENT_LOAD:  return "current_load";
        case DL_ROLE_VOLTAGE:       return "voltage";
        case DL_ROLE_DIMMER:        return "dimmer";
        case DL_ROLE_RELAY:         return "relay";
        case DL_ROLE_TEMPERATURE:   return "temperature";
        default:                    return "none";
    }
}

void WebServerManager::handleI2CStatus() {
    JsonDocument doc;
    doc["bus"] = 0;
    doc["initialized"] = i2c_bus_is_initialized(0);
    doc["speed_hz"] = DL_I2C_SPEED_HZ;
    if (dl_manager_is_initialized()) {
        doc["dimmerlink_enabled"] = dl_manager_get_enabled_count();
        doc["dimmerlink_active"] = dl_manager_get_active_count();
    }
    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleI2CScan() {
    if (!i2c_bus_is_initialized(0)) {
        sendError(503, "I2C bus not initialized");
        return;
    }
    uint8_t found[16];
    uint8_t count = 0;
    i2c_bus_scan(0, found, 16, &count);

    JsonDocument doc;
    doc["bus"] = 0;
    JsonArray arr = doc["devices"].to<JsonArray>();
    for (uint8_t i = 0; i < count; i++) {
        JsonObject dev = arr.add<JsonObject>();
        char addr_str[8];
        snprintf(addr_str, sizeof(addr_str), "0x%02X", found[i]);
        dev["addr"] = addr_str;
        dev["addr_dec"] = found[i];
    }
    doc["count"] = count;
    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleDimmerLinkDevices() {
    JsonDocument doc;
    JsonArray arr = doc["devices"].to<JsonArray>();
    for (uint8_t i = 0; i < DL_MAX_DEVICES; i++) {
        const dl_device_state_t* dev = dl_manager_get_device(i);
        if (!dev) continue;
        JsonObject obj = arr.add<JsonObject>();
        obj["slot"] = i;
        obj["enabled"] = dev->config.enabled;
        if (!dev->config.enabled) continue;
        char addr_str[8];
        snprintf(addr_str, sizeof(addr_str), "0x%02X", dev->config.i2c_addr);
        obj["addr"] = addr_str;
        obj["bus"] = dev->config.i2c_bus;
        obj["role"] = dl_role_name(dev->config.role);
        obj["name"] = dev->config.name;
        obj["online"] = dev->online;
        obj["last_poll_ms"] = dev->last_poll_ms;
        obj["error_count"] = dev->error_count;
    }
    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleSetDimmerLinkDevice() {
    JsonDocument body;
    if (deserializeJson(body, _http_server->arg("plain"))) {
        sendError(400, "Invalid JSON");
        return;
    }
    uint8_t slot = body["slot"] | 255;
    if (slot >= DL_MAX_DEVICES) {
        sendError(400, "slot out of range (0-7)");
        return;
    }
    dl_device_config_t cfg = {};
    // addr accepted as hex string ("0x50") or integer (mirrors handleSetRbampModule).
    // Without this, a hex-string addr silently falls back to DL_DEFAULT_ADDR (| operator
    // returns the default when the variant isn't an int) — registering the wrong device.
    uint8_t addr;
    if (body["addr"].is<const char*>()) {
        addr = (uint8_t)strtol(body["addr"].as<const char*>(), nullptr, 16);
    } else {
        addr = body["addr"] | DL_DEFAULT_ADDR;
    }
    if (addr < 0x08 || addr > 0x77) {
        sendError(400, "addr out of range (0x08-0x77)");
        return;
    }
    uint8_t bus = body["bus"] | 0;
    if (bus > 1) {
        sendError(400, "bus out of range (0-1)");
        return;
    }
    cfg.i2c_addr = addr;
    cfg.i2c_bus  = bus;
    cfg.enabled  = body["enabled"] | true;

    // Parse role string
    const char* role_str = body["role"] | "none";
    if (strcmp(role_str, "current_grid") == 0)  cfg.role = DL_ROLE_CURRENT_GRID;
    else if (strcmp(role_str, "current_solar") == 0) cfg.role = DL_ROLE_CURRENT_SOLAR;
    else if (strcmp(role_str, "current_load") == 0)  cfg.role = DL_ROLE_CURRENT_LOAD;
    else if (strcmp(role_str, "voltage") == 0)   cfg.role = DL_ROLE_VOLTAGE;
    else if (strcmp(role_str, "dimmer") == 0)    cfg.role = DL_ROLE_DIMMER;
    else if (strcmp(role_str, "relay") == 0)     cfg.role = DL_ROLE_RELAY;
    else cfg.role = DL_ROLE_NONE;

    const char* name = body["name"] | "";
    strncpy(cfg.name, name, sizeof(cfg.name) - 1);

    esp_err_t err = dl_manager_register(slot, &cfg);
    if (err != ESP_OK) {
        sendError(500, esp_err_to_name(err));
        return;
    }
    dl_manager_save_config();
    sendSuccess("Device registered");
}

static const char* rbamp_role_name(rbamp_source_role_t r) {
    switch (r) {
        case RBAMP_ROLE_GRID:    return "grid";
        case RBAMP_ROLE_SOLAR:   return "solar";
        case RBAMP_ROLE_LOAD:    return "load";
        case RBAMP_ROLE_VOLTAGE: return "voltage";
        default:                 return "none";
    }
}

void WebServerManager::handleGetRbampModules() {
    JsonDocument doc;
    doc["alive"] = rbamp_source_alive_count();

    // Discovered modules (fleet) with live identity + assigned role.
    rbamp_source_module_info_t mods[4];
    size_t nm = 0;
    rbamp_source_get_modules(mods, 4, &nm);
    JsonArray marr = doc["modules"].to<JsonArray>();
    for (size_t i = 0; i < nm; i++) {
        JsonObject o = marr.add<JsonObject>();
        char addr_str[8];
        snprintf(addr_str, sizeof(addr_str), "0x%02X", mods[i].i2c_addr);
        o["addr"]         = addr_str;
        o["role"]         = rbamp_role_name(mods[i].role);
        o["channels"]     = mods[i].channels;
        o["has_voltage"]  = mods[i].has_voltage;
        o["online"]       = mods[i].online;
        // Last snapshot (primary channel). NaN -> null (field unavailable).
        o["voltage"]      = mods[i].voltage;
        o["current"]      = mods[i].current;
        o["power"]        = mods[i].power;
        o["power_factor"] = mods[i].power_factor;
        o["frequency"]    = mods[i].frequency;
        // Applied CT model (poll-task cache): catalog id, or null if unset.
        uint8_t ct_code = 0;
        rbamp_source_get_ct_model(mods[i].i2c_addr, &ct_code);
        if (ct_code == 0) {
            o["ct_model"] = nullptr;
        } else {
            size_t nc = 0;
            const rbamp_ct_model_t* cat = rbamp_source_ct_catalog(&nc);
            o["ct_model"] = nullptr;
            for (size_t k = 0; k < nc; k++) {
                if (cat[k].code == ct_code) { o["ct_model"] = cat[k].id; break; }
            }
        }
    }

    // Persisted role assignments (may include not-currently-online addresses).
    rbamp_source_module_cfg_t roles[4];
    size_t nr = 0;
    rbamp_source_get_roles(roles, 4, &nr);
    JsonArray rarr = doc["roles"].to<JsonArray>();
    for (size_t i = 0; i < nr; i++) {
        JsonObject o = rarr.add<JsonObject>();
        char addr_str[8];
        snprintf(addr_str, sizeof(addr_str), "0x%02X", roles[i].i2c_addr);
        o["addr"] = addr_str;
        o["role"] = rbamp_role_name((rbamp_source_role_t)roles[i].role);
    }

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleRbampRescan() {
    esp_err_t err = rbamp_source_rescan();
    if (err == ESP_ERR_NOT_SUPPORTED) {
        // Runtime rescan compiled out (ACROUTER_I2C_AUTODISCOVERY=n, e.g. the C2):
        // not a server fault — the module set is fixed from the boot scan.
        sendError(501, "Bus rescan not supported on this build");
        return;
    }
    if (err != ESP_OK) {
        sendError(500, esp_err_to_name(err));
        return;
    }
    sendSuccess("Rescan requested");
}

void WebServerManager::handleSetRbampModule() {
    JsonDocument body;
    if (deserializeJson(body, _http_server->arg("plain"))) {
        sendError(400, "Invalid JSON");
        return;
    }
    // addr accepted as hex string ("0x50") or integer.
    uint8_t addr = 0;
    if (body["addr"].is<const char*>()) {
        addr = (uint8_t)strtol(body["addr"].as<const char*>(), nullptr, 16);
    } else {
        addr = body["addr"] | 0;
    }
    if (addr < 0x08 || addr > 0x77) {
        sendError(400, "addr out of range (0x08-0x77)");
        return;
    }
    const char* role_str = body["role"] | "none";
    rbamp_source_role_t role;
    if      (strcmp(role_str, "grid") == 0)    role = RBAMP_ROLE_GRID;
    else if (strcmp(role_str, "solar") == 0)   role = RBAMP_ROLE_SOLAR;
    else if (strcmp(role_str, "load") == 0)    role = RBAMP_ROLE_LOAD;
    else if (strcmp(role_str, "voltage") == 0) role = RBAMP_ROLE_VOLTAGE;
    else if (strcmp(role_str, "none") == 0)    role = RBAMP_ROLE_NONE;
    else { sendError(400, "Invalid role (grid|solar|load|voltage|none)"); return; }

    esp_err_t err = rbamp_source_set_role(addr, role);
    if (err != ESP_OK) {
        sendError(500, esp_err_to_name(err));
        return;
    }
    rbamp_source_save_config();
    sendSuccess("Role saved");
}

// POST /api/rbamp/modules/address  body {"addr":"0x51","new_addr":"0x52"}
// Async: 202 {success,pending:true,new_addr,message}. Two-phase reassign runs in
// the poll task; the module re-appears at new_addr on the next discovery.
void WebServerManager::handleRbampAddress() {
    JsonDocument body;
    if (deserializeJson(body, _http_server->arg("plain"))) {
        sendError(400, "Invalid JSON");
        return;
    }
    // addr / new_addr accepted as hex string ("0x51") or integer.
    uint8_t cur = body["addr"].is<const char*>()
                    ? (uint8_t)strtol(body["addr"].as<const char*>(), nullptr, 16)
                    : (uint8_t)(body["addr"] | 0);
    uint8_t neu = body["new_addr"].is<const char*>()
                    ? (uint8_t)strtol(body["new_addr"].as<const char*>(), nullptr, 16)
                    : (uint8_t)(body["new_addr"] | 0);
    if (neu < 0x08 || neu > 0x77) { sendError(400, "new_addr out of range (0x08-0x77)"); return; }

    esp_err_t err = rbamp_source_request_address_change(cur, neu);
    if (err == ESP_ERR_NOT_FOUND)     { sendError(404, "no module at addr"); return; }
    if (err == ESP_ERR_INVALID_ARG)   { sendError(400, "new_addr invalid (range or == addr)"); return; }
    if (err == ESP_ERR_INVALID_STATE) { sendError(409, "address in use or a change already pending"); return; }
    if (err != ESP_OK)                { sendError(500, esp_err_to_name(err)); return; }

    char hex[8];
    snprintf(hex, sizeof(hex), "0x%02X", neu);
    JsonDocument doc;
    doc["success"]  = true;
    doc["pending"]  = true;
    doc["new_addr"] = hex;
    doc["message"]  = "applies after reset; module re-appears at new_addr";
    String json;
    serializeJson(doc, json);
    sendJsonResponse(202, json);
}

// POST /api/dimmerlink/devices/address  body {"addr":"0x50","new_addr":"0x51"}
// Stages the address + issues RESET; applies on reset. 202 {pending:true,...}.
void WebServerManager::handleDimmerLinkAddress() {
    JsonDocument body;
    if (deserializeJson(body, _http_server->arg("plain"))) {
        sendError(400, "Invalid JSON");
        return;
    }
    uint8_t cur = body["addr"].is<const char*>()
                    ? (uint8_t)strtol(body["addr"].as<const char*>(), nullptr, 16)
                    : (uint8_t)(body["addr"] | 0);
    uint8_t neu = body["new_addr"].is<const char*>()
                    ? (uint8_t)strtol(body["new_addr"].as<const char*>(), nullptr, 16)
                    : (uint8_t)(body["new_addr"] | 0);
    if (neu < 0x08 || neu > 0x77) { sendError(400, "new_addr out of range (0x08-0x77)"); return; }

    esp_err_t err = dl_manager_change_address(cur, neu);
    if (err == ESP_ERR_INVALID_ARG)   { sendError(400, "new_addr invalid (range or == addr)"); return; }
    if (err == ESP_ERR_INVALID_STATE) { sendError(409, "address in use"); return; }
    if (err != ESP_OK)                { sendError(500, esp_err_to_name(err)); return; }

    char hex[8];
    snprintf(hex, sizeof(hex), "0x%02X", neu);
    JsonDocument doc;
    doc["success"]  = true;
    doc["pending"]  = true;
    doc["new_addr"] = hex;
    doc["message"]  = "applies after reset; device re-appears at new_addr";
    String json;
    serializeJson(doc, json);
    sendJsonResponse(202, json);
}

// GET /api/rbamp/ct-models — SCT-013 catalog (firmware source of truth).
void WebServerManager::handleGetCtModels() {
    size_t n = 0;
    const rbamp_ct_model_t* cat = rbamp_source_ct_catalog(&n);
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (size_t i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["id"]        = cat[i].id;
        o["name"]      = cat[i].name;
        o["rated_a"]   = cat[i].rated_a;
        o["code"]      = cat[i].code;
        o["available"] = cat[i].available;
    }
    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

// POST /api/rbamp/modules/ct-model  body {"addr":"0x51","channel":0,"ct_model":"sct013-030"}
// Verify-then-set (protects factory gain cal). 202 {pending} on accept.
// CT model is a per-CHANNEL property (each CT input has its own sensor). Today the
// pipeline is single-channel, so only channel 0 is addressable; the param is part of
// the stable contract so the UI keys CT-config by (addr, channel) without later churn.
void WebServerManager::handleRbampCtModel() {
    JsonDocument body;
    if (deserializeJson(body, _http_server->arg("plain"))) {
        sendError(400, "Invalid JSON");
        return;
    }
    uint8_t addr = body["addr"].is<const char*>()
                     ? (uint8_t)strtol(body["addr"].as<const char*>(), nullptr, 16)
                     : (uint8_t)(body["addr"] | 0);
    uint8_t channel = (uint8_t)(body["channel"] | 0);
    if (channel != 0) { sendError(409, "per-channel CT not yet supported (single-channel only)"); return; }
    const char* model_id = body["ct_model"] | "";

    size_t n = 0;
    const rbamp_ct_model_t* cat = rbamp_source_ct_catalog(&n);
    const rbamp_ct_model_t* m = nullptr;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(cat[i].id, model_id) == 0) { m = &cat[i]; break; }
    }
    if (!m)             { sendError(400, "unknown ct_model"); return; }
    if (!m->available)  { sendError(409, "ct_model not available on this firmware"); return; }

    esp_err_t err = rbamp_source_request_ct_model(addr, m->code);
    if (err == ESP_ERR_NOT_FOUND)     { sendError(404, "no module at addr"); return; }
    if (err == ESP_ERR_INVALID_STATE) { sendError(409, "a change already pending"); return; }
    if (err != ESP_OK)                { sendError(500, esp_err_to_name(err)); return; }

    JsonDocument doc;
    doc["success"]  = true;
    doc["pending"]  = true;
    doc["channel"]  = channel;
    doc["ct_model"] = m->id;
    doc["message"]  = "applies shortly; re-writing the same model is skipped to protect gain cal";
    String json;
    serializeJson(doc, json);
    sendJsonResponse(202, json);
}

// GET /api/modules — unified device registry (all transports/families).
// Set by the async modules-rescan worker; surfaced in GET /api/modules so the client
// can poll for scan completion.
static volatile bool s_rescan_busy = false;

void WebServerManager::handleGetModules() {
    JsonDocument doc;
    doc["scanning"] = (bool)s_rescan_busy;
    JsonArray arr = doc["modules"].to<JsonArray>();
    size_t nc = devreg_count();
    for (size_t i = 0; i < nc; i++) {
        const device_entry_t* d = devreg_get(i);
        if (!d) continue;
        JsonObject o = arr.add<JsonObject>();
        o["transport"] = (d->transport == DEV_TRANSPORT_I2C) ? "i2c" : "espnow";
        if (d->transport == DEV_TRANSPORT_I2C) {
            o["bus"] = d->bus;
            char addr_str[8];
            snprintf(addr_str, sizeof(addr_str), "0x%02X", d->addr);
            o["addr"] = addr_str;
        }
        o["family"]   = device_family_name(d->family);
        o["channels"] = d->channels;
        o["online"]   = d->online;
        JsonArray roles = o["roles"].to<JsonArray>();
        JsonArray names = o["names"].to<JsonArray>();
        uint8_t nch = d->channels ? d->channels : 1;
        if (nch > DEVREG_MAX_CH) nch = DEVREG_MAX_CH;
        for (uint8_t ch = 0; ch < nch; ch++) {
            roles.add(device_role_name((device_role_t)d->roles[ch]));
            // Per-channel user-facing name (unified across families); "" = unset.
            names.add(d->name[ch]);
        }
        // Family-specific valid roles — the UI must offer ONLY these (sensors get
        // measurement roles; dimmers/relays are outputs, role implied by family).
        device_role_t vr[8];
        size_t nvr = device_family_valid_roles(d->family, vr, 8);
        JsonArray valid = o["valid_roles"].to<JsonArray>();
        for (size_t k = 0; k < nvr; k++) valid.add(device_role_name(vr[k]));
        if (d->has_uid) {
            char uid[25];
            for (int b = 0; b < 12; b++) snprintf(uid + b * 2, 3, "%02X", d->uid[b]);
            o["uid"] = uid;
        }
    }
    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

// POST /api/modules/rescan — on-demand quiescent scan + non-destructive reconcile.
// --- Async I2C rescan: the ~2.5s bus walk runs in a worker task, off the web task
// (on the C2, 1 request in flight, a blocking scan froze the whole UI). POST returns
// 202 {scanning:true} immediately; a second POST while busy → 409. The client polls
// GET /api/modules until its `scanning` flag clears, then the list is fresh.
// (s_rescan_busy is declared above handleGetModules so that handler can report it.)
static void modules_rescan_task(void*) {
    devreg_scan_i2c(0);
    s_rescan_busy = false;
    vTaskDelete(nullptr);
}

void WebServerManager::handleModulesRescan() {
    if (s_rescan_busy) {
        sendJsonResponse(409, "{\"error\":\"busy\",\"operation\":\"rescan\"}");
        return;
    }
    s_rescan_busy = true;
    if (xTaskCreatePinnedToCore(modules_rescan_task, "rescan", 4096, nullptr, 5, nullptr, ACR_WEB_CORE) != pdPASS) {
        s_rescan_busy = false;
        sendError(500, "failed to start scan");
        return;
    }
    sendJsonResponse(202, "{\"scanning\":true}");
}

// POST /api/modules/role  body {"addr":"0x51","channel":0,"role":"grid"}
// Registry is the role source of truth; bridges to the sensing pipeline.
void WebServerManager::handleModulesRole() {
    JsonDocument body;
    if (deserializeJson(body, _http_server->arg("plain"))) { sendError(400, "Invalid JSON"); return; }
    uint8_t addr = body["addr"].is<const char*>()
                     ? (uint8_t)strtol(body["addr"].as<const char*>(), nullptr, 16)
                     : (uint8_t)(body["addr"] | 0);
    uint8_t channel = (uint8_t)(body["channel"] | 0);
    device_role_t role = device_role_parse(body["role"] | "none");

    esp_err_t err = devreg_set_role(0, addr, channel, role);
    if (err == ESP_ERR_NOT_FOUND)   { sendError(404, "no module at addr"); return; }
    if (err == ESP_ERR_INVALID_ARG) { sendError(400, "invalid channel"); return; }
    if (err != ESP_OK)              { sendError(500, esp_err_to_name(err)); return; }
    sendSuccess("Role saved");
}

// POST /api/modules/name  body {"addr":"0x50","channel":0,"name":"Boiler heater"}
// Unified per-channel user-facing name (SoT for all families); "" clears it.
void WebServerManager::handleModulesName() {
    JsonDocument body;
    if (deserializeJson(body, _http_server->arg("plain"))) { sendError(400, "Invalid JSON"); return; }
    uint8_t addr = body["addr"].is<const char*>()
                     ? (uint8_t)strtol(body["addr"].as<const char*>(), nullptr, 16)
                     : (uint8_t)(body["addr"] | 0);
    uint8_t channel = (uint8_t)(body["channel"] | 0);
    const char* name = body["name"] | "";

    esp_err_t err = devreg_set_name(0, addr, channel, name);
    if (err == ESP_ERR_NOT_FOUND)   { sendError(404, "no module at addr"); return; }
    if (err == ESP_ERR_INVALID_ARG) { sendError(400, "invalid channel"); return; }
    if (err != ESP_OK)              { sendError(500, esp_err_to_name(err)); return; }
    sendSuccess("Name saved");
}

static const char* espnow_role_name(esp_now_source_role_t r) {
    switch (r) {
        case ESPNOW_ROLE_GRID:    return "grid";
        case ESPNOW_ROLE_SOLAR:   return "solar";
        case ESPNOW_ROLE_LOAD:    return "load";
        case ESPNOW_ROLE_VOLTAGE: return "voltage";
        default:                  return "none";
    }
}

void WebServerManager::handleGetEspnowNodes() {
    JsonDocument doc;
    doc["seen"] = esp_now_source_seen_count();
    esp_now_source_node_info_t nodes[4];
    size_t n = 0;
    esp_now_source_get_nodes(nodes, 4, &n);
    JsonArray arr = doc["nodes"].to<JsonArray>();
    for (size_t i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 nodes[i].mac[0], nodes[i].mac[1], nodes[i].mac[2],
                 nodes[i].mac[3], nodes[i].mac[4], nodes[i].mac[5]);
        o["mac"]          = mac_str;
        o["role"]         = espnow_role_name(nodes[i].role);
        o["online"]       = nodes[i].online;
        o["voltage"]      = nodes[i].voltage;
        o["current"]      = nodes[i].current;
        o["power"]        = nodes[i].power;
        o["power_factor"] = nodes[i].power_factor;
        o["frequency"]    = nodes[i].frequency;
    }
    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

// GET /api/espnow/outputs — discovered ESP-NOW output nodes (dimmer/relay) with the
// hub-desired vs node-applied value per output. Lets the web E2E assert the full
// control round-trip (web -> RouterController -> ESP-NOW -> node -> DimmerLink) over
// HTTP. Output nodes are NOT in /api/modules (registry is I2C-scan-based).
void WebServerManager::handleGetEspnowOutputs() {
    JsonDocument doc;
    esp_now_source_output_node_info_t nodes[ESP_NOW_SOURCE_OUT_NODES_MAX];
    size_t n = 0;
    esp_now_source_get_output_nodes(nodes, ESP_NOW_SOURCE_OUT_NODES_MAX, &n);
    JsonArray arr = doc["nodes"].to<JsonArray>();
    for (size_t i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 nodes[i].mac[0], nodes[i].mac[1], nodes[i].mac[2],
                 nodes[i].mac[3], nodes[i].mac[4], nodes[i].mac[5]);
        o["mac"]      = mac_str;
        o["family"]   = nodes[i].family == RBN_FAMILY_DIMMER ? "dimmer"
                      : nodes[i].family == RBN_FAMILY_RELAY  ? "relay" : "unknown";
        o["online"]   = nodes[i].online;
        o["failsafe"] = nodes[i].failsafe;
        JsonArray outs = o["outputs"].to<JsonArray>();
        for (uint8_t k = 0; k < nodes[i].out_count && k < ESP_NOW_SOURCE_OUT_PER_NODE; k++) {
            const esp_now_source_output_info_t* out = &nodes[i].outputs[k];
            JsonObject oo = outs.add<JsonObject>();
            oo["output_id"]   = out->output_id;
            oo["kind"]        = out->kind == RBN_OUT_KIND_DIMMER ? "dimmer" : "relay";
            oo["range_min"]   = out->range_min;
            oo["range_max"]   = out->range_max;
            oo["desired"]     = out->desired;      // hub-commanded value
            oo["desired_set"] = out->desired_set;  // hub is actively driving this output
            oo["applied"]     = out->applied;      // value the node reported applied
            oo["result"]      = out->result;       // 0 OK / 1 clamped / 2 unknown / 3 kind
        }
    }
    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleSetEspnowNode() {
    JsonDocument body;
    if (deserializeJson(body, _http_server->arg("plain"))) {
        sendError(400, "Invalid JSON");
        return;
    }
    const char* mac_str = body["mac"] | "";
    unsigned mac[6];
    char tail = 0;
    // %n captures the consumed length so trailing junk (e.g. an extra octet) is
    // rejected; each octet is range-checked to <= 0xFF (sscanf %x would otherwise
    // truncate "1FF" to 0xFF silently).
    int consumed = 0;
    if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x%n%c",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5],
               &consumed, &tail) < 6 || mac_str[consumed] != '\0') {
        sendError(400, "Invalid mac (AA:BB:CC:DD:EE:FF)");
        return;
    }
    uint8_t m[6];
    for (int i = 0; i < 6; i++) {
        if (mac[i] > 0xFF) { sendError(400, "Invalid mac octet (> 0xFF)"); return; }
        m[i] = (uint8_t)mac[i];
    }
    const char* role_str = body["role"] | "none";
    esp_now_source_role_t role;
    if      (strcmp(role_str, "grid") == 0)    role = ESPNOW_ROLE_GRID;
    else if (strcmp(role_str, "solar") == 0)   role = ESPNOW_ROLE_SOLAR;
    else if (strcmp(role_str, "load") == 0)    role = ESPNOW_ROLE_LOAD;
    else if (strcmp(role_str, "voltage") == 0) role = ESPNOW_ROLE_VOLTAGE;
    else if (strcmp(role_str, "none") == 0)    role = ESPNOW_ROLE_NONE;
    else { sendError(400, "Invalid role (grid|solar|load|voltage|none)"); return; }

    esp_err_t err = esp_now_source_set_role(m, role);
    if (err != ESP_OK) {
        sendError(500, esp_err_to_name(err));
        return;
    }
    esp_now_source_save_config();
    sendSuccess("Node role saved");
}

// GET /api/sensors — unified read-only list of all measurement sources
// (rbAmp I2C modules + ESP-NOW nodes) with a `source` discriminator, so the
// web app can render one list instead of merging two shapes. Commissioning is
// still per-source (POST /api/rbamp/modules, POST /api/espnow/nodes).
void WebServerManager::handleGetSensors() {
    JsonDocument doc;
    JsonArray arr = doc["sources"].to<JsonArray>();

#if CONFIG_ACROUTER_RBAMP_SOURCE
    {
        rbamp_source_module_info_t mods[4];
        size_t nm = 0;
        rbamp_source_get_modules(mods, 4, &nm);
        for (size_t i = 0; i < nm; i++) {
            JsonObject o = arr.add<JsonObject>();
            char id[8];
            snprintf(id, sizeof(id), "0x%02X", mods[i].i2c_addr);
            o["source"]       = "i2c";
            o["id"]           = id;
            // Per-channel contract: the UI keys live readings by (id, channel).
            // Today the sensing pipeline is single-role/module, so channel=0; when
            // per-channel sensing lands, each active channel emits its own source.
            o["channel"]      = 0;
            o["role"]         = rbamp_role_name(mods[i].role);
            o["online"]       = mods[i].online;
            o["voltage"]      = mods[i].voltage;
            o["current"]      = mods[i].current;
            o["power"]        = mods[i].power;
            o["power_factor"] = mods[i].power_factor;
            o["frequency"]    = mods[i].frequency;
        }
    }
#endif
#if CONFIG_ACROUTER_ESPNOW_SOURCE
    {
        esp_now_source_node_info_t nodes[4];
        size_t nn = 0;
        esp_now_source_get_nodes(nodes, 4, &nn);
        for (size_t i = 0; i < nn; i++) {
            JsonObject o = arr.add<JsonObject>();
            char id[18];
            snprintf(id, sizeof(id), "%02X:%02X:%02X:%02X:%02X:%02X",
                     nodes[i].mac[0], nodes[i].mac[1], nodes[i].mac[2],
                     nodes[i].mac[3], nodes[i].mac[4], nodes[i].mac[5]);
            o["source"]       = "espnow";
            o["id"]           = id;
            o["channel"]      = 0;
            o["role"]         = espnow_role_name(nodes[i].role);
            o["online"]       = nodes[i].online;
            o["voltage"]      = nodes[i].voltage;
            o["current"]      = nodes[i].current;
            o["power"]        = nodes[i].power;
            o["power_factor"] = nodes[i].power_factor;
            o["frequency"]    = nodes[i].frequency;
        }
    }
#endif

    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

void WebServerManager::handleDimmerLinkSlot(uint8_t slot) {
    const dl_device_state_t* dev = dl_manager_get_device(slot);
    if (!dev || !dev->config.enabled) {
        sendError(404, "Slot not configured");
        return;
    }
    JsonDocument doc;
    doc["slot"]   = slot;
    doc["name"]   = dev->config.name;
    doc["role"]   = dl_role_name(dev->config.role);
    doc["online"] = dev->online;

    if (dev->current.valid) {
        JsonObject cs = doc["current"].to<JsonObject>();
        cs["rms_ma"]       = dev->current.rms_ma;
        cs["rms_a"]        = (float)dev->current.rms_ma / 1000.0f;
        cs["peak_ma"]      = dev->current.peak_ma;
        cs["direction"]    = dev->current.direction;
        cs["crest_factor"] = dev->current.crest_factor / 100.0f;
        cs["period_idx"]   = dev->current.period_idx;
        cs["duration_ms"]  = dev->current.duration_ms;
    }
    if (dev->voltage.available && dev->voltage.data_ready) {
        doc["voltage_rms_v"] = dev->voltage.rms_v;
    }
    if (dev->thermal.available) {
        JsonObject th = doc["thermal"].to<JsonObject>();
        th["temp_c"]    = dev->thermal.temperature_c;
        th["state"]     = dev->thermal.state;
        th["max_level"] = dev->thermal.max_level;
    }
    String json;
    serializeJson(doc, json);
    sendJsonResponse(200, json);
}

String WebServerManager::buildSensorHubJson() {
    sensor_hub_state_t state;
    sensor_hub_get_state(&state);

    JsonDocument doc;
    doc["merge_count"] = state.merge_count;
    doc["i2c_active"]  = sensor_hub_has_i2c_source();
    doc["adc_active"]  = sensor_hub_is_adc_active();

    const char* slot_names[] = {"voltage", "grid", "solar", "load"};
    const char* src_names[]  = {"none", "adc", "i2c", "espnow", "mqtt"};
    JsonObject slots = doc["slots"].to<JsonObject>();
    for (int i = 0; i < SENSOR_HUB_SLOTS; i++) {
        const sh_slot_state_t& s = state.slots[i];
        JsonObject sl = slots[slot_names[i]].to<JsonObject>();
        sl["valid"]    = s.valid;
        sl["value"]    = s.value;
        sl["source"]   = (s.source < 5) ? src_names[s.source] : "unknown";
        sl["priority"] = s.priority;
        if (s.has_power) sl["power_w"] = s.power;
    }
    String json;
    serializeJson(doc, json);
    return json;
}

void WebServerManager::handleSensorHubStatus() {
    sendJsonResponse(200, buildSensorHubJson());
}
