#include "OTAManager.h"
#include "esp_log.h"
#include "PowerMeterADC.h"
#include "DimmerHAL.h"
#include "RouterController.h"
#include "../web/pages/OTAPage.h"
#include <HTTPClient.h>
#include <WiFiClient.h>

static const char* TAG = "OTA";

// Static instance pointer for callbacks
OTAManager* OTAManager::_instance = nullptr;

// Singleton instance
OTAManager& OTAManager::getInstance() {
    static OTAManager instance;
    return instance;
}

OTAManager::OTAManager()
    : _server(nullptr)
    , _running(false)
    , _updating(false)
    , _progress(0)
    , _upload_size(0)
{
    _instance = this;
}

OTAManager::~OTAManager() {
    stop();
    _instance = nullptr;
}

bool OTAManager::begin(WebServer* server) {
    if (!server) {
        ESP_LOGE(TAG, "WebServer pointer is null!");
        return false;
    }

    if (_running) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }

    _server = server;

    // Register OTA endpoints
    _server->on("/ota", HTTP_GET, [this]() { handleOTAPage(); });
    _server->on("/ota/upload", HTTP_POST,
        [this]() { handleOTAUploadStatus(); },
        [this]() { handleOTAUpload(); }
    );

    _running = true;
    ESP_LOGI(TAG, "OTA Manager started - URL: http://<device-ip>/ota");

    return true;
}

void OTAManager::stop() {
    if (!_running) return;

    _running = false;
    ESP_LOGI(TAG, "Stopped");
}

void OTAManager::handleOTAPage() {
    String html = getOTAPage();
    _server->send(200, "text/html", html);
}

void OTAManager::handleOTAUpload() {
    HTTPUpload& upload = _server->upload();

    if (upload.status == UPLOAD_FILE_START) {
        _updating = true;
        _progress = 0;
        _upload_size = 0;

        ESP_LOGI(TAG, "Update start: %s", upload.filename.c_str());
        ESP_LOGI(TAG, "Suspending critical tasks for OTA...");

        // Stop RouterController first (high-level control)
        RouterController& router = RouterController::getInstance();
        if (router.isInitialized()) {
            router.emergencyStop();
            ESP_LOGI(TAG, "RouterController stopped");
        }

        // Stop DimmerHAL (turn off TRIAC outputs)
        DimmerHAL& dimmer = DimmerHAL::getInstance();
        if (dimmer.isInitialized()) {
            dimmer.allOff();
            ESP_LOGI(TAG, "DimmerHAL outputs disabled");
        }

        // Stop PowerMeterADC (stop DMA and processing task)
        PowerMeterADC& powerMeter = PowerMeterADC::getInstance();
        if (powerMeter.isRunning()) {
            powerMeter.stop();
            ESP_LOGI(TAG, "PowerMeterADC stopped");
        }

        // Small delay to ensure all tasks have stopped
        delay(100);

        ESP_LOGI(TAG, "All critical tasks suspended, starting update...");

        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            ESP_LOGE(TAG, "Update begin failed");
            Update.printError(Serial);
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            ESP_LOGE(TAG, "Update write failed");
            Update.printError(Serial);
        } else {
            _upload_size += upload.currentSize;
            _progress = (Update.progress() * 100) / Update.size();

            // Log progress every 10%
            static int last_progress = 0;
            if (_progress >= last_progress + 10) {
                ESP_LOGI(TAG, "Progress: %d%%", _progress);
                last_progress = _progress;
            }
        }
    }
    else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            ESP_LOGI(TAG, "Update success: %u bytes", upload.totalSize);
            _progress = 100;
        } else {
            ESP_LOGE(TAG, "Update failed");
            Update.printError(Serial);
        }
        _updating = false;
    }
    else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.end();
        ESP_LOGE(TAG, "Update aborted");
        _updating = false;
    }
}

void OTAManager::handleOTAUploadStatus() {
    if (Update.hasError()) {
        _server->send(500, "text/plain", "Update failed");
        ESP_LOGE(TAG, "Update has error");
    } else {
        _server->send(200, "text/plain", "Update successful");
        ESP_LOGI(TAG, "Restarting in 3 seconds...");

        // Restart after 3 seconds
        delay(3000);
        ESP.restart();
    }
}

bool OTAManager::updateFromURL(const char* url) {
    HTTPClient http;

    ESP_LOGI(TAG, "Connecting to: %s", url);

    http.begin(url);
    http.addHeader("User-Agent", "ACRouter-OTA");

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %d", httpCode);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        ESP_LOGE(TAG, "Invalid content length: %d", contentLength);
        http.end();
        return false;
    }

    ESP_LOGI(TAG, "Content length: %d bytes", contentLength);

    // Suspend critical tasks before OTA
    ESP_LOGI(TAG, "Suspending critical tasks for OTA...");

    // Stop RouterController first (high-level control)
    RouterController& router = RouterController::getInstance();
    if (router.isInitialized()) {
        router.emergencyStop();
        ESP_LOGI(TAG, "RouterController stopped");
    }

    // Stop DimmerHAL (turn off TRIAC outputs)
    DimmerHAL& dimmer = DimmerHAL::getInstance();
    if (dimmer.isInitialized()) {
        dimmer.allOff();
        ESP_LOGI(TAG, "DimmerHAL outputs disabled");
    }

    // Stop PowerMeterADC (stop DMA and processing task)
    PowerMeterADC& powerMeter = PowerMeterADC::getInstance();
    if (powerMeter.isRunning()) {
        powerMeter.stop();
        ESP_LOGI(TAG, "PowerMeterADC stopped");
    }

    // Small delay to ensure all tasks have stopped
    delay(100);

    ESP_LOGI(TAG, "All critical tasks suspended, starting update...");

    // Begin OTA update
    if (!Update.begin(contentLength)) {
        ESP_LOGE(TAG, "OTA begin failed: %s", Update.errorString());
        http.end();
        return false;
    }

    // Download and write firmware
    WiFiClient* stream = http.getStreamPtr();
    size_t written = 0;
    uint8_t buffer[1024];
    int lastProgress = -1;

    while (http.connected() && written < contentLength) {
        size_t available = stream->available();
        if (available) {
            int len = stream->readBytes(buffer, min(available, sizeof(buffer)));

            if (Update.write(buffer, len) != len) {
                ESP_LOGE(TAG, "Write failed");
                Update.abort();
                http.end();
                return false;
            }

            written += len;

            // Progress indicator
            int progress = (written * 100) / contentLength;
            if (progress != lastProgress && progress % 10 == 0) {
                ESP_LOGI(TAG, "Progress: %d%% (%d / %d bytes)", progress, written, contentLength);
                lastProgress = progress;
            }
        }
        delay(1);
    }

    http.end();

    if (written != contentLength) {
        ESP_LOGE(TAG, "Download incomplete: %d / %d bytes", written, contentLength);
        Update.abort();
        return false;
    }

    // Finalize update
    if (!Update.end(true)) {
        ESP_LOGE(TAG, "OTA end failed: %s", Update.errorString());
        return false;
    }

    ESP_LOGI(TAG, "Firmware written successfully");

    return true;
}
