#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <Update.h>

// Forward declarations
class PowerMeterADC;
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
    bool begin(WebServer* server);
    void stop();

    // Status
    bool isRunning() const { return _running; }
    bool isUpdating() const { return _updating; }
    int getProgress() const { return _progress; }

private:
    OTAManager();
    ~OTAManager();
    OTAManager(const OTAManager&) = delete;
    OTAManager& operator=(const OTAManager&) = delete;

    // HTTP handlers
    void handleOTAPage();
    void handleOTAUpload();
    void handleOTAUploadStatus();

    // Upload callback
    static void uploadCallback(HTTPUpload& upload);

    WebServer* _server;
    bool _running;
    bool _updating;
    int _progress;
    size_t _upload_size;

    static OTAManager* _instance;
};
