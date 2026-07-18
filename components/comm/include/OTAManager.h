#pragma once

#include <Arduino.h>
#include "HttpdServer.h"     // esp_http_server-backed shim (was Arduino <WebServer.h>)
#include <Update.h>

// Forward declarations
class DimmerHAL;
class RouterController;

/**
 * @brief OTA (Over-The-Air) Update Manager
 *
 * Provides firmware update functionality via HTTP upload.
 * Includes a web interface for uploading firmware from local PC.
 * Safely suspends critical tasks during update to prevent watchdog resets.
 */
class OTAManager {
public:
    static OTAManager& getInstance();

    // Initialization
    bool begin(HttpdServer* server);
    void stop();

    // Status
    bool isRunning() const { return _running; }
    bool isUpdating() const { return _updating; }
    int getProgress() const { return _progress; }

    // OTA update from URL
    bool updateFromURL(const char* url);

private:
    OTAManager();
    ~OTAManager();
    OTAManager(const OTAManager&) = delete;
    OTAManager& operator=(const OTAManager&) = delete;

    // HTTP handlers
    // Raw firmware upload (POST /ota/upload). Streams the request body straight into
    // Update via the shim's current httpd_req_t. Body is the RAW binary
    // (Content-Type: application/octet-stream), NOT multipart form-data.
    void handleOTAUpload();

    HttpdServer* _server;
    bool _running;
    bool _updating;
    int _progress;
    size_t _upload_size;

    static OTAManager* _instance;
};
