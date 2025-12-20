#pragma once

#include <Arduino.h>
#include <time.h>

/**
 * @brief NTP Time Synchronization Manager
 *
 * Manages NTP time synchronization for accurate system time.
 * Automatically syncs with NTP servers when WiFi is connected.
 */
class NTPManager {
public:
    static NTPManager& getInstance();

    // Initialization
    bool begin(const char* ntp_server = "pool.ntp.org",
               const char* timezone = "UTC-0",
               int gmt_offset_sec = 0,
               int daylight_offset_sec = 0);

    void handle();
    void stop();

    // Time operations
    bool isTimeSynced() const;
    time_t getTime() const;
    String getTimeString(const char* format = "%Y-%m-%d %H:%M:%S") const;

    // Configuration
    void setTimezone(const char* timezone, int gmt_offset_sec = 0, int daylight_offset_sec = 0);
    void setNTPServer(const char* server);
    void forceSync();

    // Status
    uint32_t getLastSyncTime() const { return _last_sync_time; }
    bool isRunning() const { return _running; }

private:
    NTPManager();
    ~NTPManager();
    NTPManager(const NTPManager&) = delete;
    NTPManager& operator=(const NTPManager&) = delete;

    void checkSync();

    bool _running;
    bool _time_synced;
    uint32_t _last_sync_time;
    uint32_t _last_check_time;

    String _ntp_server;
    String _timezone;
    int _gmt_offset_sec;
    int _daylight_offset_sec;

    static constexpr uint32_t SYNC_CHECK_INTERVAL = 60000;     // 1 minute (check sync status)
    static constexpr uint32_t RESYNC_INTERVAL = 3600000;      // 1 hour (ESP-IDF SNTP minimum practical interval)
};
