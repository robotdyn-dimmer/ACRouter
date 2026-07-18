#include "OTAManager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"          // esp_restart()
#include "RouterController.h"
#include "WebServerManager.h"   // shared auth: gate raw /ota/upload with the same Bearer

// New dimmer manager (pure C API)
extern "C" {
#include "dimmer_manager.h"
}
#include <HTTPClient.h>
#include <WiFiClient.h>

static const char* TAG = "OTA";

// Detached restart: let the HTTP response flush, then reboot — off the web task so
// the upload handler returns immediately (no blocking delay in the httpd task).
static void ota_restart_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

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

bool OTAManager::begin(HttpdServer* server) {
    if (!server) {
        ESP_LOGE(TAG, "server pointer is null!");
        return false;
    }

    if (_running) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }

    _server = server;

    // Register OTA endpoints on the shim. The upload handler streams the raw request
    // body straight into Update (see handleOTAUpload) — one request, chunked recv.
    // On-device OTA page removed (external web app owns the UI). Raw firmware upload only.
    _server->on("/ota/upload", HTTP_POST, [this]() { handleOTAUpload(); });

    _running = true;
    ESP_LOGI(TAG, "OTA Manager started - URL: http://<device-ip>/ota");

    return true;
}

void OTAManager::stop() {
    if (!_running) return;

    _running = false;
    ESP_LOGI(TAG, "Stopped");
}

// POST /ota/upload — the request body is the RAW firmware binary (not multipart).
// Streams it straight into Update via chunked httpd_req_recv, then restarts.
void OTAManager::handleOTAUpload() {
    // Auth gate (open while ACROUTER_AUTH_ENFORCE=0 — checkAuth() returns true without
    // touching request state; when re-enabled this reads the shim's current headers).
    if (!WebServerManager::getInstance().checkAuth()) {
        _server->sendHeader("WWW-Authenticate", "Bearer");
        _server->send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    httpd_req_t* req = _server->currentReq();
    if (!req) { _server->send(500, "text/plain", "no request"); return; }

    int total = req->content_len;
    if (total <= 0) { _server->send(400, "text/plain", "empty body"); return; }

    ESP_LOGI(TAG, "OTA upload: %d bytes — suspending critical tasks...", total);
    _updating = true;
    _progress = 0;
    _upload_size = 0;

    // Stop control + outputs before touching flash.
    RouterController& router = RouterController::getInstance();
    if (router.isInitialized()) router.emergencyStop();
    dimmer_set_level_all(0);
    vTaskDelay(pdMS_TO_TICKS(100));

    if (!Update.begin((size_t)total)) {   // exact size → erase only what's needed
        Update.printError(Serial);
        _updating = false;
        _server->send(500, "text/plain", "Update begin failed");
        return;
    }

    // Stream the body straight into flash.
    // NOTE: large OTA-over-WiFi is impractical on the ESP32-C2 — the tight heap/LWIP
    // can't buffer a sustained inbound transfer, so it stalls after ~one TCP window
    // (~8 KB). Use serial flash on the C2; OTA-over-WiFi is an ESP32-tier feature.
    uint8_t buf[1024];
    int remaining = total, last_pct = 0;
    while (remaining > 0) {
        int r = httpd_req_recv(req, (char*)buf, remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) { ESP_LOGW(TAG, "OTA recv timeout at %d/%d", total - remaining, total); continue; }
        if (r <= 0) { ESP_LOGE(TAG, "OTA recv ret %d at %d/%d", r, total - remaining, total); Update.abort(); _updating = false; _server->send(500, "text/plain", "recv error"); return; }
        if (Update.write(buf, r) != (size_t)r) {
            Update.printError(Serial); Update.abort(); _updating = false;
            _server->send(500, "text/plain", "Update write failed"); return;
        }
        _upload_size += r;
        remaining    -= r;
        int pct = (int)((total - remaining) * 100L / total);
        if (pct >= last_pct + 10) { ESP_LOGI(TAG, "OTA progress: %d%%", pct); last_pct = pct; }
    }

    _updating = false;
    if (!Update.end(true)) {
        Update.printError(Serial);
        _server->send(500, "text/plain", "Update failed");
        return;
    }

    _progress = 100;
    ESP_LOGI(TAG, "Update success: %u bytes — restarting shortly", (unsigned)_upload_size);
    _server->send(200, "text/plain", "Update successful");
    // Restart off the web task so the 200 flushes and the handler returns.
    xTaskCreate(ota_restart_task, "ota_restart", 2048, nullptr, 5, nullptr);
}

bool OTAManager::updateFromURL(const char* url) {
#if CONFIG_IDF_TARGET_ESP32C2
    // ESP32-C2: HTTPClient/TLS trimmed (see GitHubOTAChecker) — remote URL OTA is
    // unavailable on this SoC. Local OTA (web upload via handleOTAUpload) still works.
    (void)url;
    ESP_LOGW(TAG, "Remote-URL OTA unavailable on esp32c2 (no HTTP client); use local upload");
    return false;
#else
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

    // Turn off all dimmer outputs
    uint8_t count = dimmer_set_level_all(0);
    ESP_LOGI(TAG, "Dimmer outputs disabled (%d channels)", count);

    // Small delay to ensure all tasks have stopped
    vTaskDelay(pdMS_TO_TICKS(100));

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
        vTaskDelay(1);   // yield during the download loop
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
#endif  // CONFIG_IDF_TARGET_ESP32C2
}
