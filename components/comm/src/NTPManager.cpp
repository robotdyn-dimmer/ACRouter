#include "NTPManager.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <WiFi.h>

static const char* TAG = "NTP";

// Singleton instance
NTPManager& NTPManager::getInstance() {
    static NTPManager instance;
    return instance;
}

NTPManager::NTPManager()
    : _running(false)
    , _time_synced(false)
    , _last_sync_time(0)
    , _last_check_time(0)
    , _ntp_server("pool.ntp.org")
    , _timezone("UTC-0")
    , _gmt_offset_sec(0)
    , _daylight_offset_sec(0)
{
}

NTPManager::~NTPManager() {
    stop();
}

bool NTPManager::begin(const char* ntp_server, const char* timezone,
                       int gmt_offset_sec, int daylight_offset_sec) {
    if (_running) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }

    _ntp_server = ntp_server;
    _timezone = timezone;
    _gmt_offset_sec = gmt_offset_sec;
    _daylight_offset_sec = daylight_offset_sec;

    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGW(TAG, "WiFi not connected, NTP will sync when connected");
        _running = true;
        return true;
    }

    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);

    // Set sync interval to 24 hours (in milliseconds)
    esp_sntp_set_sync_interval(RESYNC_INTERVAL);

    esp_sntp_init();

    // Set timezone
    setenv("TZ", timezone, 1);
    tzset();

    _running = true;
    ESP_LOGI(TAG, "Started - Server: %s, TZ: %s", ntp_server, timezone);

    return true;
}

void NTPManager::stop() {
    if (!_running) return;

    esp_sntp_stop();

    _running = false;
    _time_synced = false;
    ESP_LOGI(TAG, "Stopped");
}

void NTPManager::handle() {
    if (!_running) return;

    uint32_t now = millis();

    // Check sync status periodically
    if (now - _last_check_time >= SYNC_CHECK_INTERVAL) {
        _last_check_time = now;
        checkSync();
    }

    // Note: Auto resync is handled by esp_sntp_set_sync_interval(RESYNC_INTERVAL)
    // No need for manual forceSync() - ESP-IDF SNTP does it automatically
}

void NTPManager::checkSync() {
    // Check if WiFi connected
    if (WiFi.status() != WL_CONNECTED) {
        if (_time_synced) {
            ESP_LOGW(TAG, "WiFi disconnected, relying on local time");
        }
        return;
    }

    // Check SNTP sync status
    sntp_sync_status_t sync_status = esp_sntp_get_sync_status();

    if (sync_status == SNTP_SYNC_STATUS_COMPLETED) {
        if (!_time_synced) {
            // First sync after boot or after forceSync()
            ESP_LOGI(TAG, "Time synced: %s", getTimeString().c_str());
            _time_synced = true;
            _last_sync_time = millis();
        }
        // Note: SNTP_SYNC_STATUS_COMPLETED stays set after sync
        // ESP-IDF handles periodic resync internally via esp_sntp_set_sync_interval
    } else if (sync_status == SNTP_SYNC_STATUS_RESET) {
        // SNTP not initialized, reinitialize
        if (WiFi.status() == WL_CONNECTED) {
            ESP_LOGD(TAG, "Reinitializing SNTP...");
            esp_sntp_stop();
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, _ntp_server.c_str());
            esp_sntp_set_sync_interval(RESYNC_INTERVAL);
            esp_sntp_init();
        }
    }
}

bool NTPManager::isTimeSynced() const {
    if (!_time_synced) return false;

    // Check if time is valid (after 2020-01-01)
    time_t now;
    time(&now);
    return (now > 1577836800);  // 2020-01-01 00:00:00 UTC
}

time_t NTPManager::getTime() const {
    time_t now;
    time(&now);
    return now;
}

String NTPManager::getTimeString(const char* format) const {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char buffer[64];
    strftime(buffer, sizeof(buffer), format, &timeinfo);
    return String(buffer);
}

void NTPManager::setTimezone(const char* timezone, int gmt_offset_sec, int daylight_offset_sec) {
    _timezone = timezone;
    _gmt_offset_sec = gmt_offset_sec;
    _daylight_offset_sec = daylight_offset_sec;

    setenv("TZ", timezone, 1);
    tzset();

    ESP_LOGI(TAG, "Timezone updated: %s", timezone);
}

void NTPManager::setNTPServer(const char* server) {
    _ntp_server = server;

    if (_running) {
        esp_sntp_stop();
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, server);
        esp_sntp_set_sync_interval(RESYNC_INTERVAL);
        esp_sntp_init();

        ESP_LOGI(TAG, "NTP server updated: %s", server);
    }
}

void NTPManager::forceSync() {
    if (!_running) {
        ESP_LOGW(TAG, "Not running, cannot force sync");
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGW(TAG, "WiFi not connected, cannot sync");
        return;
    }

    // Restart SNTP to force sync
    esp_sntp_stop();
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, _ntp_server.c_str());
    esp_sntp_set_sync_interval(RESYNC_INTERVAL);
    esp_sntp_init();

    _time_synced = false;
    ESP_LOGI(TAG, "Force sync requested");
}
