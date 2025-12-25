#pragma once

#include <Arduino.h>
#include <WebServer.h>
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
    void handleCalibrate();

    // Web Page Handlers (Material UI)
    void handleDashboard();          // Main dashboard page
    void handleWiFiPage();           // WiFi configuration page
    void handleHardwareConfigPage(); // Hardware configuration page
    void handleOTAPage();            // OTA firmware update page

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
    void handleGetSensorProfiles();  // GET /api/hardware/sensor-profiles

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

    // Utility
    void sendJsonResponse(int code, const String& json);
    void sendError(int code, const char* message);
    void sendSuccess(const char* message = nullptr);
    String buildStatusJson();
    String buildMetricsJson();
    String buildConfigJson();
    String buildHardwareConfigJson();

    WebServer* _http_server;

    bool _running;
    uint16_t _http_port;
    uint16_t _ws_port;
};
