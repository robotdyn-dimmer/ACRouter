#pragma once

#include <Arduino.h>
#include "HttpdServer.h"          // esp_http_server-backed shim (was Arduino <WebServer.h>)
#include "RouterController.h"
#include "ConfigManager.h"
#include "WiFiManager.h"

/**
 * @brief Web Server Manager for AC Router
 *
 * Provides:
 * - REST API for configuration and control
 * - WiFi configuration web interface
 * - JSON-based responses
 * - Static file serving for web interface
 */
class WebServerManager {
public:
    static WebServerManager& getInstance();

    // Initialization
    bool begin(uint16_t http_port = 80, uint16_t ws_port = 81);
    void handle();
    void stop();

    // Getters
    bool isRunning() const { return _running; }

    // Auth token management (also used by the `auth-token` serial command).
    void setAuthToken(const char* token);   // "" or nullptr clears (disables auth)
    bool isAuthEnforced() const { return _auth_token[0] != '\0'; }
    bool checkAuth();                        // side-effect-free auth test (used by OTAManager mid-upload)
    // External web-app URL (also used by the `web-url` serial command). The app is
    // hosted externally; the device only redirects / to it and allows it via CORS.
    void setWebAppUrl(const char* url);      // "" or nullptr clears (open: CORS *, / shows stub)
    String getWebAppUrl() const { return _web_app_url; }
    uint16_t getHttpPort() const { return _http_port; }
    uint16_t getWsPort() const { return _ws_port; }

private:
    WebServerManager();
    ~WebServerManager();
    WebServerManager(const WebServerManager&) = delete;
    WebServerManager& operator=(const WebServerManager&) = delete;

    // HTTP Server setup
    void setupRoutes();

    // API Handlers - Status & Info
    void handleGetStatus();
    void handleGetMetrics();
    void handleGetConfig();
    void handleGetInfo();

    // API Handlers - Configuration
    void handleSetConfig();
    void handleSetMode();
    void handleSetDimmer();
    void handleResetConfig();

    // API Handlers - Control
    void handleSetManual();
    void handleSimInject();  // POST /api/sim/inject — Tier-0 synthetic-measurement harness
    void handleSimStop();    // POST /api/sim/stop — clear latched sim measurements (Task 17)
    void handleCalibrate();

    // External-web-app hosting: the SPA lives on an external server. The device
    // redirects / to it and reflects it as the CORS origin (empty => open: CORS *).
    void loadWebAppUrl();            // read persisted URL from NVS into _web_app_url
    String corsOrigin();             // _web_app_url if set, else "*"
    void handleGetWebConfig();       // GET  /api/web/config
    void handleSetWebConfig();       // POST /api/web/config {app_url}

    // Web Page Handlers (Material UI)
    void handleDashboard();          // GET / — redirect to external app (or stub if unset)
    void handleWiFiPage();           // WiFi configuration page

    // WiFi API Handlers
    void handleWiFiStatus();         // GET /api/wifi/status
    void handleWiFiScan();           // GET /api/wifi/scan
    void handleWiFiConnect();        // POST /api/wifi/connect
    void handleWiFiDisconnect();     // POST /api/wifi/disconnect
    void handleWiFiForget();         // POST /api/wifi/forget

    // Hardware Config API Handlers
    void handleGetHardwareConfig();  // GET /api/hardware/config
    void handleSetHardwareConfig();  // POST /api/hardware/config
    void handleValidateHardwareConfig(); // POST /api/hardware/validate
    void handleGetSensors();         // GET /api/sensors (rbAmp + ESP-NOW union)

    // System Control API Handlers
    void handleSystemReboot();       // POST /api/system/reboot

    // OTA API Handlers
    void handleOTACheckGitHub();     // GET /api/ota/check-github
    void handleOTAUpdateGitHub();    // POST /api/ota/update-github

    // MQTT API Handlers
    void handleMQTTPage();           // GET /mqtt
    void handleGetMQTTStatus();      // GET /api/mqtt/status
    void handleGetMQTTConfig();      // GET /api/mqtt/config
    void handleSetMQTTConfig();      // POST /api/mqtt/config
    void handleMQTTReconnect();      // POST /api/mqtt/reconnect
    void handleMQTTPublish();        // POST /api/mqtt/publish

    // NTP API Handlers
    void handleGetNTPStatus();       // GET /api/ntp/status
    void handleGetNTPConfig();       // GET /api/ntp/config
    void handleSetNTPConfig();       // POST /api/ntp/config
    void handleNTPSync();            // POST /api/ntp/sync

    // Relays API Handlers
    void handleRelaysPage();         // GET /relays
    void handleGetRelaysStatus();    // GET /api/relays/status
    void handleRelaysAllOn();        // GET/POST /api/relays/all-on
    void handleRelaysAllOff();       // GET/POST /api/relays/all-off
    void handleRelayOn(uint8_t id);  // GET/POST /api/relays/{id}/on
    void handleRelayOff(uint8_t id); // GET/POST /api/relays/{id}/off
    void handleRelayConfig(uint8_t id); // POST /api/relays/{id}/config

    // Dimmers API Handlers
    void handleGetDimmersStatus();   // GET /api/dimmers/status
    void handleDimmersAllOn();       // GET/POST /api/dimmers/all-on
    void handleDimmersAllOff();      // GET/POST /api/dimmers/all-off
    void handleDimmerLevel(uint8_t id);     // POST /api/dimmers/{id}/level (JSON body)
    void handleDimmerLevelGet(uint8_t id);  // GET /api/dimmers/{id}/level?value=N
    void handleDimmerConfig(uint8_t id);    // POST /api/dimmers/{id}/config (JSON body)
    void handleDimmerConfigGet(uint8_t id); // GET /api/dimmers/{id}/config

    // I2C / DimmerLink API (v2.0)
    void handleI2CStatus();          // GET /api/i2c/status
    void handleI2CScan();            // GET /api/i2c/scan
    void handleDimmerLinkDevices();  // GET /api/dimmerlink/devices
    void handleSetDimmerLinkDevice();// POST /api/dimmerlink/devices
    void handleDimmerLinkSlot(uint8_t slot); // GET /api/dimmerlink/{slot}/status
    void handleSensorHubStatus();    // GET /api/sensors/hub
    void handleGetRbampModules();    // GET /api/rbamp/modules
    void handleSetRbampModule();     // POST /api/rbamp/modules
    void handleRbampAddress();       // POST /api/rbamp/modules/address
    void handleDimmerLinkAddress();  // POST /api/dimmerlink/devices/address
    void handleGetCtModels();        // GET  /api/rbamp/ct-models
    void handleRbampCtModel();       // POST /api/rbamp/modules/ct-model
    void handleGetModules();         // GET  /api/modules
    void handleModulesRescan();      // POST /api/modules/rescan
    void handleModulesRole();        // POST /api/modules/role
    void handleModulesName();        // POST /api/modules/name
    void handleRbampRescan();        // POST /api/rbamp/rescan
    void handleGetEspnowNodes();     // GET /api/espnow/nodes
    void handleSetEspnowNode();      // POST /api/espnow/nodes
    void handleGetEspnowOutputs();   // GET /api/espnow/outputs

    // --- Auth (A3: bearer token on write/OTA; GET open; unset = open dev mode) ---
    void loadAuthToken();            // read persisted token from NVS into _auth_token
    bool requireAuth();              // true if allowed (unset, or valid Bearer); else sends 401
    void handleAuthCheck();          // GET  /api/auth/check
    void handleSetAuth();            // POST /api/auth {new_token}

    // Utility
    void sendJsonResponse(int code, const String& json);
    void sendError(int code, const char* message);
    void sendSuccess(const char* message = nullptr);
    String buildStatusJson();
    String buildMetricsJson();
    String buildConfigJson();
    String buildHardwareConfigJson();
    String buildSensorHubJson();

    HttpdServer* _http_server;
    char _auth_token[65];            // "" = auth disabled (open, dev). Set => Bearer enforced.
    String _web_app_url;             // external SPA URL; "" = open (CORS *, / stub). Set => redirect + CORS allowlist

    bool _running;
    uint16_t _http_port;
    uint16_t _ws_port;
};
