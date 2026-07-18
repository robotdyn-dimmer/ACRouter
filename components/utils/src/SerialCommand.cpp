/**
 * @file SerialCommand.cpp
 * @brief Serial command processor implementation
 */

#include "SerialCommand.h"
#include "ConfigManager.h"
#include "HardwareConfigManager.h"
#include "RouterController.h"

// New dimmer manager (pure C API)
extern "C" {
#include "dimmer_manager.h"
}

// New relay manager (pure C API)
extern "C" {
#include "relay_manager.h"
#include "relay_gpio.h"
#include "i2c_bus.h"
#include "dimmerlink_manager.h"
#include "device_registry.h"
#include "sensor_hub.h"
#include "rbamp_source.h"
#include "esp_now_source.h"
#include "espnow_proto.h"
#include "acrouter_events.h"
#include "acrouter_measurements.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
}

#include "VoltageSensorDrivers.h"
#include "CurrentSensorDrivers.h"
#include "WiFiManager.h"
#include "WebServerManager.h"
#include "NTPManager.h"
#include "OTAManager.h"
#include "GitHubOTAChecker.h"
#include "MQTTManager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <esp_ota_ops.h>
#include <cstring>
#include <cstdlib>

const char* SerialCommand::TAG = "SerialCmd";

// ============================================================
// Singleton Instance
// ============================================================

SerialCommand& SerialCommand::getInstance() {
    static SerialCommand instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================

SerialCommand::SerialCommand()
    : m_config(nullptr)
    , m_router(nullptr)
    , m_buffer_pos(0)
{
    memset(m_buffer, 0, BUFFER_SIZE);
}

// ============================================================
// Initialization
// ============================================================

void SerialCommand::begin(ConfigManager* config, RouterController* router) {
    m_config = config;
    m_router = router;
    // Note: ESP_LOGI removed - causes crash during early init
    // Terminal is ready after this point
}

// ============================================================
// Process Serial Input
// ============================================================

void SerialCommand::process() {
    while (Serial.available()) {
        char c = Serial.read();

        // Handle line terminators
        if (c == '\n' || c == '\r') {
            if (m_buffer_pos > 0) {
                m_buffer[m_buffer_pos] = '\0';
                executeCommand(m_buffer);
                m_buffer_pos = 0;
            }
            continue;
        }

        // Handle backspace
        if (c == '\b' || c == 127) {
            if (m_buffer_pos > 0) {
                m_buffer_pos--;
                // Note: Character erase echo disabled (was Serial.print)
            }
            continue;
        }

        // Add character to buffer
        if (m_buffer_pos < BUFFER_SIZE - 1) {
            m_buffer[m_buffer_pos++] = c;
            // Note: Character echo disabled (was Serial.print)
        }
    }
}

// ============================================================
// Execute Command
// ============================================================

void SerialCommand::executeCommand(const char* line) {
    // Skip leading whitespace
    while (*line == ' ') line++;

    // Empty line
    if (*line == '\0') return;

    // Parse command and arguments
    char cmd[32];
    char arg[64];  // Increased for multiple args
    cmd[0] = '\0';
    arg[0] = '\0';

    // Split at first space
    const char* space = strchr(line, ' ');
    if (space) {
        size_t cmd_len = space - line;
        if (cmd_len >= sizeof(cmd)) cmd_len = sizeof(cmd) - 1;
        strncpy(cmd, line, cmd_len);
        cmd[cmd_len] = '\0';

        // Skip spaces to get argument
        const char* arg_start = space + 1;
        while (*arg_start == ' ') arg_start++;
        strncpy(arg, arg_start, sizeof(arg) - 1);
        arg[sizeof(arg) - 1] = '\0';
    } else {
        strncpy(cmd, line, sizeof(cmd) - 1);
        cmd[sizeof(cmd) - 1] = '\0';
    }

    // Convert command to lowercase
    for (char* p = cmd; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p += 32;
    }

    // Built-in commands
    if (strcmp(cmd, "help") == 0) {
        printHelp();
        return;
    }

    if (strcmp(cmd, "status") == 0) {
        printStatus();
        return;
    }

    // Try config commands
    if (handleConfigCommand(cmd, arg[0] ? arg : nullptr)) {
        return;
    }

    // Try router commands
    if (handleRouterCommand(cmd, arg[0] ? arg : nullptr)) {
        return;
    }

    // WiFi commands
    WiFiManager& wifi = WiFiManager::getInstance();

    if (strcmp(cmd, "wifi-status") == 0) {
        const WiFiStatus& ws = wifi.getStatus();
        ESP_LOGI(TAG, "\n=== WiFi Status ===");
        ESP_LOGI(TAG, "State: %s",
            ws.state == WiFiState::IDLE ? "IDLE" :
            ws.state == WiFiState::AP_ONLY ? "AP_ONLY" :
            ws.state == WiFiState::STA_CONNECTING ? "CONNECTING" :
            ws.state == WiFiState::STA_CONNECTED ? "STA_CONNECTED" :
            ws.state == WiFiState::AP_STA ? "AP+STA" : "STA_FAILED");
        if (ws.ap_active) {
            ESP_LOGI(TAG, "AP SSID: %s", ws.ap_ssid.c_str());
            ESP_LOGI(TAG, "AP IP:   %s", ws.ap_ip.toString().c_str());
            ESP_LOGI(TAG, "Clients: %d", ws.sta_clients);
        }
        if (ws.sta_connected) {
            ESP_LOGI(TAG, "STA SSID: %s", ws.sta_ssid.c_str());
            ESP_LOGI(TAG, "STA IP:   %s", ws.sta_ip.toString().c_str());
            ESP_LOGI(TAG, "RSSI:     %d dBm", ws.rssi);
        }
        ESP_LOGI(TAG, "MAC: %s", wifi.getMACAddress().c_str());
        ESP_LOGI(TAG, "Saved credentials: %s", wifi.hasCredentials() ? "Yes" : "No");
        ESP_LOGI(TAG, "===================\n");
        return;
    }

    if (strcmp(cmd, "wifi-connect") == 0) {
        if (arg[0] == '\0') {
            ESP_LOGI(TAG, "Usage: wifi-connect <ssid> [password]");
            ESP_LOGI(TAG, "Examples:");
            ESP_LOGI(TAG, "  wifi-connect MyNetwork MyPassword123");
            ESP_LOGI(TAG, "  wifi-connect \"My Home Network\" MyPassword123");
            ESP_LOGI(TAG, "  wifi-connect \"SSID with spaces\" \"Pass with spaces\"");
        } else {
            // Parse SSID and password from arg
            // Supports quoted strings for SSID/password with spaces
            char ssid[33];
            char password[65] = "";
            const char* p = arg;

            // Skip leading spaces
            while (*p == ' ') p++;

            // Parse SSID
            size_t ssid_len = 0;
            if (*p == '"') {
                // Quoted SSID - find closing quote
                p++; // Skip opening quote
                while (*p && *p != '"' && ssid_len < sizeof(ssid) - 1) {
                    ssid[ssid_len++] = *p++;
                }
                if (*p == '"') p++; // Skip closing quote
            } else {
                // Unquoted SSID - read until space or end
                while (*p && *p != ' ' && ssid_len < sizeof(ssid) - 1) {
                    ssid[ssid_len++] = *p++;
                }
            }
            ssid[ssid_len] = '\0';

            // Skip spaces between SSID and password
            while (*p == ' ') p++;

            // Parse password (if present)
            size_t pass_len = 0;
            if (*p) {
                if (*p == '"') {
                    // Quoted password - find closing quote
                    p++; // Skip opening quote
                    while (*p && *p != '"' && pass_len < sizeof(password) - 1) {
                        password[pass_len++] = *p++;
                    }
                    if (*p == '"') p++; // Skip closing quote
                } else {
                    // Unquoted password - read until end
                    while (*p && pass_len < sizeof(password) - 1) {
                        password[pass_len++] = *p++;
                    }
                }
                password[pass_len] = '\0';
            }

            ESP_LOGI(TAG, "Connecting to: %s", ssid);
            if (password[0]) {
                ESP_LOGI(TAG, "Password: ***");
            } else {
                ESP_LOGI(TAG, "No password (open network)");
            }
            wifi.connectSTA(ssid, password[0] ? password : nullptr);
        }
        return;
    }

    if (strcmp(cmd, "wifi-disconnect") == 0) {
        wifi.disconnectSTA();
        ESP_LOGI(TAG, "Disconnected from STA network");
        return;
    }

    if (strcmp(cmd, "wifi-forget") == 0) {
        if (wifi.clearCredentials()) {
            ESP_LOGI(TAG, "WiFi credentials cleared from NVS");
        } else {
            ESP_LOGE(TAG, "Failed to clear credentials");
        }
        return;
    }

    if (strcmp(cmd, "wifi-scan") == 0) {
        ESP_LOGI(TAG, "Scanning WiFi networks...");

        // Configure scan
        wifi_scan_config_t scan_config = {};
        scan_config.ssid = nullptr;
        scan_config.bssid = nullptr;
        scan_config.channel = 0;
        scan_config.show_hidden = false;
        scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        scan_config.scan_time.active.min = 100;
        scan_config.scan_time.active.max = 300;

        // Start blocking scan
        esp_err_t err = esp_wifi_scan_start(&scan_config, true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(err));
            return;
        }

        // Get results
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);

        if (ap_count == 0) {
            ESP_LOGI(TAG, "No networks found");
        } else {
            if (ap_count > 20) ap_count = 20;

            wifi_ap_record_t* ap_records = new wifi_ap_record_t[ap_count];
            esp_wifi_scan_get_ap_records(&ap_count, ap_records);

            ESP_LOGI(TAG, "Found %d networks:", ap_count);
            for (int i = 0; i < ap_count; i++) {
                ESP_LOGI(TAG, "  %d: %s (%d dBm) %s ch%d",
                    i + 1,
                    (const char*)ap_records[i].ssid,
                    ap_records[i].rssi,
                    ap_records[i].authmode == WIFI_AUTH_OPEN ? "open" : "secured",
                    ap_records[i].primary);
            }

            delete[] ap_records;
        }

        esp_wifi_scan_stop();
        return;
    }

    // WebServer commands
    WebServerManager& webserver = WebServerManager::getInstance();

    if (strcmp(cmd, "web-status") == 0) {
        ESP_LOGI(TAG, "\n=== WebServer Status ===");
        ESP_LOGI(TAG, "Running: %s", webserver.isRunning() ? "Yes" : "No");
        if (webserver.isRunning()) {
            ESP_LOGI(TAG, "HTTP Port: %d", webserver.getHttpPort());
            ESP_LOGI(TAG, "WS Port:   %d", webserver.getWsPort());
            const WiFiStatus& ws = wifi.getStatus();
            ESP_LOGI(TAG, "\nAccess URLs:");
            if (ws.ap_active) {
                ESP_LOGI(TAG, "  AP:  http://%s", ws.ap_ip.toString().c_str());
            }
            if (ws.sta_connected) {
                ESP_LOGI(TAG, "  STA: http://%s", ws.sta_ip.toString().c_str());
            }
            ESP_LOGI(TAG, "\nEndpoints:");
            ESP_LOGI(TAG, "  GET  /api/status");
            ESP_LOGI(TAG, "  GET  /api/metrics");
            ESP_LOGI(TAG, "  GET  /api/config");
            ESP_LOGI(TAG, "  GET  /api/info");
            ESP_LOGI(TAG, "  POST /api/config");
            ESP_LOGI(TAG, "  POST /api/mode");
            ESP_LOGI(TAG, "  POST /api/dimmer");
            ESP_LOGI(TAG, "  POST /api/manual");
            ESP_LOGI(TAG, "  GET  /wifi - WiFi configuration page");
        }
        ESP_LOGI(TAG, "========================\n");
        return;
    }

    if (strcmp(cmd, "web-start") == 0) {
        if (webserver.isRunning()) {
            ESP_LOGW(TAG, "WebServer already running");
        } else {
            if (webserver.begin()) {
                ESP_LOGI(TAG, "WebServer started");
            } else {
                ESP_LOGE(TAG, "Failed to start WebServer");
            }
        }
        return;
    }

    if (strcmp(cmd, "web-stop") == 0) {
        if (!webserver.isRunning()) {
            ESP_LOGW(TAG, "WebServer not running");
        } else {
            webserver.stop();
            ESP_LOGI(TAG, "WebServer stopped");
        }
        return;
    }

    if (strcmp(cmd, "web-urls") == 0) {
        const WiFiStatus& ws = wifi.getStatus();
        ESP_LOGI(TAG, "\n=== Web Interface URLs ===");
        if (ws.ap_active) {
            ESP_LOGI(TAG, "AP Network:  http://%s", ws.ap_ip.toString().c_str());
            ESP_LOGI(TAG, "  WiFi Config: http://%s/wifi", ws.ap_ip.toString().c_str());
            ESP_LOGI(TAG, "  OTA Update:  http://%s/ota", ws.ap_ip.toString().c_str());
        }
        if (ws.sta_connected) {
            ESP_LOGI(TAG, "STA Network: http://%s", ws.sta_ip.toString().c_str());
            ESP_LOGI(TAG, "  WiFi Config: http://%s/wifi", ws.sta_ip.toString().c_str());
            ESP_LOGI(TAG, "  OTA Update:  http://%s/ota", ws.sta_ip.toString().c_str());
        }
        if (!ws.ap_active && !ws.sta_connected) {
            ESP_LOGW(TAG, "No network active");
        }
        ESP_LOGI(TAG, "==========================\n");
        return;
    }

    // NTP/Time commands
    NTPManager& ntp = NTPManager::getInstance();

    if (strcmp(cmd, "time-status") == 0) {
        ESP_LOGI(TAG, "\n=== NTP Time Status ===");
        ESP_LOGI(TAG, "Running: %s", ntp.isRunning() ? "Yes" : "No");
        ESP_LOGI(TAG, "Synced:  %s", ntp.isTimeSynced() ? "Yes" : "No");
        if (ntp.isTimeSynced()) {
            ESP_LOGI(TAG, "Time:    %s", ntp.getTimeString().c_str());
            uint32_t last_sync = ntp.getLastSyncTime();
            if (last_sync > 0) {
                ESP_LOGI(TAG, "Last sync: %lu s ago", (millis() - last_sync) / 1000);
            }
        }
        ESP_LOGI(TAG, "=======================\n");
        return;
    }

    if (strcmp(cmd, "time-sync") == 0) {
        ntp.forceSync();
        ESP_LOGI(TAG, "NTP sync requested");
        return;
    }

    // OTA commands
    OTAManager& ota = OTAManager::getInstance();

    if (strcmp(cmd, "ota-status") == 0) {
        ESP_LOGI(TAG, "\n=== OTA Update Status ===");
        ESP_LOGI(TAG, "Running:  %s", ota.isRunning() ? "Yes" : "No");
        ESP_LOGI(TAG, "Updating: %s", ota.isUpdating() ? "Yes" : "No");
        if (ota.isUpdating()) {
            ESP_LOGI(TAG, "Progress: %d%%", ota.getProgress());
        }
        const WiFiStatus& ws = wifi.getStatus();
        ESP_LOGI(TAG, "\nOTA URLs:");
        if (ws.ap_active) {
            ESP_LOGI(TAG, "  AP:  http://%s/ota", ws.ap_ip.toString().c_str());
        }
        if (ws.sta_connected) {
            ESP_LOGI(TAG, "  STA: http://%s/ota", ws.sta_ip.toString().c_str());
        }
        if (!ws.ap_active && !ws.sta_connected) {
            ESP_LOGW(TAG, "  No network active");
        }
        ESP_LOGI(TAG, "=========================\n");
        return;
    }

    if (strcmp(cmd, "ota-check") == 0) {
        ESP_LOGI(TAG, "Checking for updates on GitHub...");

        GitHubOTAChecker& checker = GitHubOTAChecker::getInstance();
        GitHubRelease release;

        // Try to check for updates (returns update availability, not success)
        bool hasUpdate = checker.checkForUpdate(release);

        // Check if we got valid release info (tag_name not empty = successful check)
        if (release.tag_name.isEmpty()) {
            ESP_LOGE(TAG, "Failed to check for updates - no release info received");
            return;
        }

        if (hasUpdate) {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "======================================================");
            ESP_LOGI(TAG, "  Update Available!");
            ESP_LOGI(TAG, "======================================================");
            ESP_LOGI(TAG, "Latest Version:  %s", release.tag_name.c_str());
            ESP_LOGI(TAG, "Release Name:    %s", release.name.c_str());
            ESP_LOGI(TAG, "Published:       %s", release.published_at.c_str());
            ESP_LOGI(TAG, "File:            %s (%d bytes)", release.asset_name.c_str(), release.asset_size);
            ESP_LOGI(TAG, "------------------------------------------------------");
            ESP_LOGI(TAG, "Changelog:");

            // Print changelog (first 500 chars)
            String changelog = release.body;
            if (changelog.length() > 500) {
                changelog = changelog.substring(0, 500) + "...";
            }

            // Split by newlines and print
            int start = 0;
            int end = changelog.indexOf('\n');
            while (end != -1) {
                ESP_LOGI(TAG, "  %s", changelog.substring(start, end).c_str());
                start = end + 1;
                end = changelog.indexOf('\n', start);
            }
            if (start < changelog.length()) {
                ESP_LOGI(TAG, "  %s", changelog.substring(start).c_str());
            }

            ESP_LOGI(TAG, "------------------------------------------------------");
            ESP_LOGI(TAG, "To update, run: ota-update-github");
            ESP_LOGI(TAG, "======================================================");
            ESP_LOGI(TAG, "");
        } else {
            // No update available - show current status
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "======================================================");
            ESP_LOGI(TAG, "  No Update Available");
            ESP_LOGI(TAG, "======================================================");
            ESP_LOGI(TAG, "Current Version:  %s", checker.getCurrentVersion());
            ESP_LOGI(TAG, "Latest Release:   %s", release.tag_name.c_str());
            ESP_LOGI(TAG, "Published:        %s", release.published_at.c_str());

            // Determine status message
            int cmp = GitHubOTAChecker::compareVersions(checker.getCurrentVersion(), release.tag_name.c_str());
            if (cmp == 0) {
                ESP_LOGI(TAG, "Status:           Up to date");
            } else {
                ESP_LOGI(TAG, "Status:           Development version (ahead of release)");
            }
            ESP_LOGI(TAG, "======================================================");
            ESP_LOGI(TAG, "");
        }

        return;
    }

    if (strcmp(cmd, "ota-update-github") == 0) {
        ESP_LOGI(TAG, "Starting OTA update from GitHub...");

        // Get latest release info
        GitHubOTAChecker& checker = GitHubOTAChecker::getInstance();
        GitHubRelease release;

        if (!checker.checkForUpdate(release)) {
            ESP_LOGE(TAG, "Failed to get release info");
            return;
        }

        if (!checker.isUpdateAvailable()) {
            ESP_LOGI(TAG, "No update available");
            return;
        }

        ESP_LOGI(TAG, "Downloading: %s", release.asset_url.c_str());
        ESP_LOGI(TAG, "Size: %d bytes", release.asset_size);

        // Perform OTA update
        bool success = ota.updateFromURL(release.asset_url.c_str());

        if (success) {
            ESP_LOGI(TAG, "Update successful! Rebooting...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Update failed");
        }

        return;
    }

    if (strcmp(cmd, "ota-update-url") == 0) {
        if (strlen(arg) == 0) {
            ESP_LOGE(TAG, "Usage: ota-update-url <url>");
            return;
        }

        ESP_LOGI(TAG, "Starting OTA update from URL: %s", arg);

        bool success = ota.updateFromURL(arg);

        if (success) {
            ESP_LOGI(TAG, "Update successful! Rebooting...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Update failed");
        }

        return;
    }

    if (strcmp(cmd, "ota-rollback") == 0) {
        ESP_LOGI(TAG, "Rolling back to previous firmware...");

        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* rollback = esp_ota_get_next_update_partition(running);

        if (rollback == NULL) {
            ESP_LOGE(TAG, "No rollback partition available");
            return;
        }

        ESP_LOGI(TAG, "Current:  %s", running->label);
        ESP_LOGI(TAG, "Rollback: %s", rollback->label);

        esp_err_t err = esp_ota_set_boot_partition(rollback);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Rollback failed: %s", esp_err_to_name(err));
            return;
        }

        ESP_LOGI(TAG, "Rollback configured. Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();

        return;
    }

    if (strcmp(cmd, "ota-info") == 0) {
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* next = esp_ota_get_next_update_partition(running);

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "======================================================");
        ESP_LOGI(TAG, "  OTA Partition Information");
        ESP_LOGI(TAG, "======================================================");
        ESP_LOGI(TAG, "Running Partition:");
        ESP_LOGI(TAG, "  Label:   %s", running->label);
        ESP_LOGI(TAG, "  Address: 0x%08x", running->address);
        ESP_LOGI(TAG, "  Size:    %d KB", running->size / 1024);
        ESP_LOGI(TAG, "");

        if (next) {
            ESP_LOGI(TAG, "Update Target:");
            ESP_LOGI(TAG, "  Label:   %s", next->label);
            ESP_LOGI(TAG, "  Address: 0x%08x", next->address);
            ESP_LOGI(TAG, "  Size:    %d KB", next->size / 1024);
        } else {
            ESP_LOGI(TAG, "No update target available");
        }

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "Free Heap:   %d KB", ESP.getFreeHeap() / 1024);
        ESP_LOGI(TAG, "Flash Size:  %d MB", ESP.getFlashChipSize() / (1024 * 1024));
        ESP_LOGI(TAG, "======================================================");
        ESP_LOGI(TAG, "");

        return;
    }

    // ================================================================
    // MQTT Commands
    // ================================================================

    if (strcmp(cmd, "mqtt-status") == 0) {
        MQTTManager& mqtt = MQTTManager::getInstance();
        const MQTTConfig& cfg = mqtt.getConfig();

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "======================================================");
        ESP_LOGI(TAG, "  MQTT Status");
        ESP_LOGI(TAG, "======================================================");
        ESP_LOGI(TAG, "State:         %s", mqtt.getConnectionState());
        ESP_LOGI(TAG, "Enabled:       %s", cfg.enabled ? "Yes" : "No");
        ESP_LOGI(TAG, "Broker:        %s", strlen(cfg.broker) > 0 ? cfg.broker : "(not configured)");
        ESP_LOGI(TAG, "Device ID:     %s", cfg.device_id);
        ESP_LOGI(TAG, "Device Name:   %s", strlen(cfg.device_name) > 0 ? cfg.device_name : "(default)");
        ESP_LOGI(TAG, "HA Discovery:  %s", cfg.ha_discovery ? "Enabled" : "Disabled");
        ESP_LOGI(TAG, "Pub Interval:  %lu ms", cfg.publish_interval);

        if (mqtt.isConnected()) {
            ESP_LOGI(TAG, "------------------------------------------------------");
            ESP_LOGI(TAG, "Uptime:        %lu sec", mqtt.getConnectionUptime());
            ESP_LOGI(TAG, "Published:     %lu messages", mqtt.getMessagesPublished());
            ESP_LOGI(TAG, "Received:      %lu messages", mqtt.getMessagesReceived());
        }

        if (strlen(mqtt.getLastError()) > 0) {
            ESP_LOGI(TAG, "Last Error:    %s", mqtt.getLastError());
        }

        ESP_LOGI(TAG, "======================================================");
        ESP_LOGI(TAG, "");
        return;
    }

    if (strcmp(cmd, "mqtt-config") == 0) {
        MQTTManager& mqtt = MQTTManager::getInstance();
        const MQTTConfig& cfg = mqtt.getConfig();

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "=== MQTT Configuration ===");
        ESP_LOGI(TAG, "Enabled:       %s", cfg.enabled ? "Yes" : "No");
        ESP_LOGI(TAG, "Broker:        %s", cfg.broker);
        ESP_LOGI(TAG, "Username:      %s", strlen(cfg.username) > 0 ? cfg.username : "(none)");
        ESP_LOGI(TAG, "Password:      %s", strlen(cfg.password) > 0 ? "****" : "(none)");
        ESP_LOGI(TAG, "Device ID:     %s", cfg.device_id);
        ESP_LOGI(TAG, "Device Name:   %s", cfg.device_name);
        ESP_LOGI(TAG, "Pub Interval:  %lu ms", cfg.publish_interval);
        ESP_LOGI(TAG, "HA Discovery:  %s", cfg.ha_discovery ? "Enabled" : "Disabled");
        ESP_LOGI(TAG, "===========================");
        return;
    }

    if (strcmp(cmd, "mqtt-broker") == 0) {
        if (strlen(arg) == 0) {
            MQTTManager& mqtt = MQTTManager::getInstance();
            ESP_LOGI(TAG, "Current broker: %s", mqtt.getConfig().broker);
            ESP_LOGI(TAG, "Usage: mqtt-broker <url>");
            ESP_LOGI(TAG, "Example: mqtt-broker mqtt://192.168.1.10:1883");
            return;
        }

        MQTTManager& mqtt = MQTTManager::getInstance();
        mqtt.setBroker(arg);
        ESP_LOGI(TAG, "MQTT broker set to: %s", arg);
        return;
    }

    if (strcmp(cmd, "mqtt-user") == 0) {
        MQTTManager& mqtt = MQTTManager::getInstance();
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Current username: %s",
                     strlen(mqtt.getConfig().username) > 0 ? mqtt.getConfig().username : "(none)");
            return;
        }

        mqtt.setCredentials(arg, nullptr);
        ESP_LOGI(TAG, "MQTT username set to: %s", arg);
        return;
    }

    if (strcmp(cmd, "mqtt-pass") == 0) {
        MQTTManager& mqtt = MQTTManager::getInstance();
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Password: %s",
                     strlen(mqtt.getConfig().password) > 0 ? "****" : "(none)");
            return;
        }

        mqtt.setCredentials(nullptr, arg);
        ESP_LOGI(TAG, "MQTT password updated");
        return;
    }

    if (strcmp(cmd, "mqtt-device-id") == 0) {
        MQTTManager& mqtt = MQTTManager::getInstance();
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Current device ID: %s", mqtt.getConfig().device_id);
            return;
        }

        mqtt.setDeviceId(arg);
        ESP_LOGI(TAG, "MQTT device ID set to: %s", arg);
        return;
    }

    if (strcmp(cmd, "mqtt-device-name") == 0) {
        MQTTManager& mqtt = MQTTManager::getInstance();
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Current device name: %s",
                     strlen(mqtt.getConfig().device_name) > 0 ? mqtt.getConfig().device_name : "(default)");
            return;
        }

        mqtt.setDeviceName(arg);
        ESP_LOGI(TAG, "MQTT device name set to: %s", arg);
        return;
    }

    if (strcmp(cmd, "mqtt-interval") == 0) {
        MQTTManager& mqtt = MQTTManager::getInstance();
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Current publish interval: %lu ms", mqtt.getConfig().publish_interval);
            ESP_LOGI(TAG, "Usage: mqtt-interval <ms> (1000-60000)");
            return;
        }

        uint32_t interval = atoi(arg);
        if (interval < 1000 || interval > 60000) {
            ESP_LOGE(TAG, "Invalid interval. Must be 1000-60000 ms");
            return;
        }

        mqtt.setPublishInterval(interval);
        ESP_LOGI(TAG, "MQTT publish interval set to: %lu ms", interval);
        return;
    }

    if (strcmp(cmd, "mqtt-ha-discovery") == 0) {
        MQTTManager& mqtt = MQTTManager::getInstance();
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Home Assistant discovery: %s",
                     mqtt.getConfig().ha_discovery ? "Enabled" : "Disabled");
            ESP_LOGI(TAG, "Usage: mqtt-ha-discovery <0|1>");
            return;
        }

        bool enable = (strcmp(arg, "1") == 0 || strcmp(arg, "true") == 0);
        mqtt.setHADiscovery(enable);
        ESP_LOGI(TAG, "Home Assistant discovery: %s", enable ? "Enabled" : "Disabled");
        return;
    }

    if (strcmp(cmd, "mqtt-enable") == 0) {
        MQTTManager& mqtt = MQTTManager::getInstance();
        mqtt.setEnabled(true);
        ESP_LOGI(TAG, "MQTT enabled");

        if (strlen(mqtt.getConfig().broker) == 0) {
            ESP_LOGW(TAG, "Note: Broker not configured. Use mqtt-broker to set.");
        }
        return;
    }

    if (strcmp(cmd, "mqtt-disable") == 0) {
        MQTTManager& mqtt = MQTTManager::getInstance();
        mqtt.setEnabled(false);
        ESP_LOGI(TAG, "MQTT disabled");
        return;
    }

    if (strcmp(cmd, "mqtt-reconnect") == 0) {
        MQTTManager& mqtt = MQTTManager::getInstance();
        if (!mqtt.isEnabled()) {
            ESP_LOGE(TAG, "MQTT is disabled. Use mqtt-enable first.");
            return;
        }

        ESP_LOGI(TAG, "Forcing MQTT reconnection...");
        mqtt.reconnect();
        return;
    }

    if (strcmp(cmd, "mqtt-publish") == 0) {
        MQTTManager& mqtt = MQTTManager::getInstance();
        if (!mqtt.isConnected()) {
            ESP_LOGE(TAG, "MQTT not connected");
            return;
        }

        ESP_LOGI(TAG, "Publishing all data...");
        mqtt.publishAll();
        ESP_LOGI(TAG, "Done");
        return;
    }

    // ================================================================
    // Relay Commands (new C API)
    // ================================================================

    if (strcmp(cmd, "relay-list") == 0) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "======================================================");
        ESP_LOGI(TAG, "  Relay Status");
        ESP_LOGI(TAG, "======================================================");

        uint8_t enabled_count = 0;
        uint8_t on_count = 0;
        uint32_t total_power = 0;

        for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
            relay_status_t status;
            if (relay_get_status(i, &status) != ESP_OK) {
                continue;
            }

            // Skip unconfigured relays
            if (status.type == RELAY_TYPE_NONE && !status.enabled) {
                continue;
            }

            if (status.enabled) {
                enabled_count++;
                const char* state_str = status.is_on ? "ON " : "OFF";
                const char* debounce_str = (status.state == RELAY_STATE_DEBOUNCE) ? " (debounce)" : "";
                ESP_LOGI(TAG, "[%d] %-12s GPIO%-2d  %s%s  %dW",
                         i, status.name, status.gpio_pin, state_str, debounce_str, status.nominal_power_w);
                if (status.is_on) {
                    on_count++;
                    total_power += status.nominal_power_w;
                }
            } else {
                ESP_LOGI(TAG, "[%d] %-12s (disabled)", i, status.name[0] ? status.name : "Relay");
            }
        }

        ESP_LOGI(TAG, "------------------------------------------------------");
        ESP_LOGI(TAG, "Enabled: %d  |  ON: %d  |  Total Power: %lu W",
                 enabled_count, on_count, total_power);
        ESP_LOGI(TAG, "======================================================");
        ESP_LOGI(TAG, "");
        return;
    }

    // Unified relay control command: relay <id> <on|off|toggle> [force]
    if (strcmp(cmd, "relay") == 0) {
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Usage: relay <id> <on|off|toggle> [force]");
            ESP_LOGI(TAG, "  id: 0-%d", RELAY_MAX_COUNT-1);
            ESP_LOGI(TAG, "  action: on, off, toggle");
            ESP_LOGI(TAG, "  force: bypass debounce protection (optional)");
            ESP_LOGI(TAG, "Examples:");
            ESP_LOGI(TAG, "  relay 0 on");
            ESP_LOGI(TAG, "  relay 1 off force");
            ESP_LOGI(TAG, "  relay 2 toggle");
            return;
        }

        // Parse: relay <id> <action> [force]
        char id_str[4], action[8];
        int parsed = sscanf(arg, "%3s %7s", id_str, action);

        if (parsed < 2) {
            ESP_LOGE(TAG, "Invalid arguments. Usage: relay <id> <on|off|toggle> [force]");
            return;
        }

        uint8_t id = (uint8_t)atoi(id_str);
        bool force = (strstr(arg, "force") != nullptr);

        if (id >= RELAY_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid relay ID (0-%d)", RELAY_MAX_COUNT-1);
            return;
        }

        esp_err_t result = ESP_OK;
        const char* action_str = "";

        if (strcmp(action, "on") == 0) {
            result = relay_turn_on(id, force);
            action_str = "ON";
        } else if (strcmp(action, "off") == 0) {
            result = relay_turn_off(id, force);
            action_str = "OFF";
        } else if (strcmp(action, "toggle") == 0) {
            result = relay_toggle(id, force);
            // Get current state after toggle
            relay_status_t status;
            if (relay_get_status(id, &status) == ESP_OK) {
                action_str = status.is_on ? "ON" : "OFF";
            }
        } else {
            ESP_LOGE(TAG, "Invalid action: %s. Use: on, off, or toggle", action);
            return;
        }

        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Relay %d: %s%s", id, action_str, force ? " (forced)" : "");
        } else {
            relay_status_t status;
            if (relay_get_status(id, &status) == ESP_OK && status.state == RELAY_STATE_DEBOUNCE) {
                ESP_LOGW(TAG, "Relay %d: debounce active", id);
            } else {
                ESP_LOGE(TAG, "Failed to %s relay %d", action, id);
            }
        }
        return;
    }

    if (strcmp(cmd, "relay-all-off") == 0) {
        relay_all_off(false);  // Don't force, respect debounce
        ESP_LOGI(TAG, "All relays: OFF");
        return;
    }

    // Hardware configuration commands (hw-relay-*)

    if (strcmp(cmd, "hw-relay-enable") == 0) {
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Usage: hw-relay-enable <id> <on|off>");
            ESP_LOGI(TAG, "Example: hw-relay-enable 0 on");
            return;
        }

        char id_str[4], state[8];
        if (sscanf(arg, "%3s %7s", id_str, state) < 2) {
            ESP_LOGE(TAG, "Invalid arguments");
            return;
        }

        uint8_t id = (uint8_t)atoi(id_str);
        if (id >= RELAY_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid relay ID (0-%d)", RELAY_MAX_COUNT-1);
            return;
        }

        bool enable;
        if (strcmp(state, "on") == 0) {
            enable = true;
        } else if (strcmp(state, "off") == 0) {
            enable = false;
        } else {
            ESP_LOGE(TAG, "Invalid state. Use: on or off");
            return;
        }

        if (relay_set_enabled(id, enable) == ESP_OK) {
            ESP_LOGI(TAG, "Relay %d: %s", id, enable ? "enabled" : "disabled");
        } else {
            ESP_LOGE(TAG, "Failed to %s relay %d", enable ? "enable" : "disable", id);
        }
        return;
    }

    if (strcmp(cmd, "hw-relay-gpio") == 0) {
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Usage: hw-relay-gpio <id> <pin>");
            ESP_LOGI(TAG, "  pin: 0-39, or -1 to disable");
            ESP_LOGI(TAG, "Example: hw-relay-gpio 0 15");
            return;
        }

        char id_str[4], pin_str[8];
        if (sscanf(arg, "%3s %7s", id_str, pin_str) < 2) {
            ESP_LOGE(TAG, "Invalid arguments");
            return;
        }

        uint8_t id = (uint8_t)atoi(id_str);
        int8_t pin = (int8_t)atoi(pin_str);

        if (id >= RELAY_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid relay ID (0-%d)", RELAY_MAX_COUNT-1);
            return;
        }

        if (relay_set_gpio(id, pin) == ESP_OK) {
            ESP_LOGI(TAG, "Relay %d GPIO = %d", id, pin);
        } else {
            ESP_LOGE(TAG, "Failed to set GPIO for relay %d", id);
        }
        return;
    }

    if (strcmp(cmd, "hw-relay-name") == 0) {
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Usage: hw-relay-name <id> <name>");
            ESP_LOGI(TAG, "Example: hw-relay-name 0 Heater");
            return;
        }

        char id_str[4], name[32];
        if (sscanf(arg, "%3s %31[^\n]", id_str, name) < 2) {
            ESP_LOGE(TAG, "Invalid arguments");
            return;
        }

        uint8_t id = (uint8_t)atoi(id_str);

        if (id >= RELAY_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid relay ID (0-%d)", RELAY_MAX_COUNT-1);
            return;
        }

        if (relay_set_name(id, name) == ESP_OK) {
            ESP_LOGI(TAG, "Relay %d name = %s", id, name);
        } else {
            ESP_LOGE(TAG, "Failed to set name for relay %d", id);
        }
        return;
    }

    if (strcmp(cmd, "hw-relay-power") == 0) {
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Usage: hw-relay-power <id> <watts>");
            ESP_LOGI(TAG, "Example: hw-relay-power 0 2000");
            return;
        }

        char id_str[4], power_str[8];
        if (sscanf(arg, "%3s %7s", id_str, power_str) < 2) {
            ESP_LOGE(TAG, "Invalid arguments");
            return;
        }

        uint8_t id = (uint8_t)atoi(id_str);
        uint16_t power = (uint16_t)atoi(power_str);

        if (id >= RELAY_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid relay ID (0-%d)", RELAY_MAX_COUNT-1);
            return;
        }

        if (relay_set_nominal_power(id, power) == ESP_OK) {
            ESP_LOGI(TAG, "Relay %d power = %d W", id, power);
        } else {
            ESP_LOGE(TAG, "Failed to set power for relay %d", id);
        }
        return;
    }

    if (strcmp(cmd, "relay-priority") == 0) {
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Usage: relay-priority <id> <0-255>");
            ESP_LOGI(TAG, "Example: relay-priority 0 0  (0=highest priority)");
            return;
        }

        char id_str[4], priority_str[4];
        if (sscanf(arg, "%3s %3s", id_str, priority_str) < 2) {
            ESP_LOGE(TAG, "Invalid arguments");
            return;
        }

        uint8_t id = (uint8_t)atoi(id_str);
        uint8_t priority = (uint8_t)atoi(priority_str);

        if (id >= RELAY_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid relay ID (0-%d)", RELAY_MAX_COUNT-1);
            return;
        }

        if (relay_set_priority(id, priority) == ESP_OK) {
            ESP_LOGI(TAG, "Relay %d priority = %d", id, priority);
        } else {
            ESP_LOGE(TAG, "Failed to set priority for relay %d", id);
        }
        return;
    }

    if (strcmp(cmd, "hw-relay-active") == 0) {
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Usage: hw-relay-active <id> <high|low>");
            ESP_LOGI(TAG, "Example: hw-relay-active 0 high");
            return;
        }

        char id_str[4], level[8];
        if (sscanf(arg, "%3s %7s", id_str, level) < 2) {
            ESP_LOGE(TAG, "Invalid arguments");
            return;
        }

        uint8_t id = (uint8_t)atoi(id_str);

        if (id >= RELAY_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid relay ID (0-%d)", RELAY_MAX_COUNT-1);
            return;
        }

        bool active_high;
        if (strcmp(level, "high") == 0) {
            active_high = true;
        } else if (strcmp(level, "low") == 0) {
            active_high = false;
        } else {
            ESP_LOGE(TAG, "Invalid level. Use: high or low");
            return;
        }

        if (relay_set_active_high(id, active_high) == ESP_OK) {
            ESP_LOGI(TAG, "Relay %d active = %s", id, active_high ? "HIGH" : "LOW");
        } else {
            ESP_LOGE(TAG, "Failed to set active level for relay %d", id);
        }
        return;
    }

    if (strcmp(cmd, "hw-relay-debounce") == 0) {
        if (strlen(arg) == 0) {
            ESP_LOGI(TAG, "Usage: hw-relay-debounce <id> <min_on_sec> <min_off_sec>");
            ESP_LOGI(TAG, "Example: hw-relay-debounce 0 120 120");
            return;
        }

        char id_str[4], on_str[8], off_str[8];
        if (sscanf(arg, "%3s %7s %7s", id_str, on_str, off_str) < 3) {
            ESP_LOGE(TAG, "Invalid arguments");
            return;
        }

        uint8_t id = (uint8_t)atoi(id_str);
        uint16_t min_on = (uint16_t)atoi(on_str);
        uint16_t min_off = (uint16_t)atoi(off_str);

        if (id >= RELAY_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid relay ID (0-%d)", RELAY_MAX_COUNT-1);
            return;
        }

        if (relay_set_min_on_time(id, min_on) == ESP_OK &&
            relay_set_min_off_time(id, min_off) == ESP_OK) {
            ESP_LOGI(TAG, "Relay %d debounce = %d/%d sec (on/off)", id, min_on, min_off);
        } else {
            ESP_LOGE(TAG, "Failed to set debounce for relay %d", id);
        }
        return;
    }

    if (strcmp(cmd, "hw-relay-status") == 0) {
        if (strlen(arg) == 0) {
            // Show all relays status (use new C API)
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "======================================================");
            ESP_LOGI(TAG, "  Relay Hardware Configuration");
            ESP_LOGI(TAG, "======================================================");

            for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
                relay_status_t status;
                if (relay_get_status(i, &status) != ESP_OK) {
                    continue;
                }

                // Only show configured relays
                if (status.type == RELAY_TYPE_NONE && !status.enabled) {
                    continue;
                }

                ESP_LOGI(TAG, "");
                ESP_LOGI(TAG, "--- Relay %d ---", i);
                ESP_LOGI(TAG, "  Name:      %s", status.name[0] ? status.name : "(none)");
                ESP_LOGI(TAG, "  Enabled:   %s", status.enabled ? "Yes" : "No");
                ESP_LOGI(TAG, "  Type:      %s", relay_type_str(status.type));
                ESP_LOGI(TAG, "  GPIO:      %d", status.gpio_pin);
                ESP_LOGI(TAG, "  Active:    %s", status.active_high ? "HIGH" : "LOW");
                ESP_LOGI(TAG, "  Power:     %d W", status.nominal_power_w);
                ESP_LOGI(TAG, "  Priority:  %d", status.priority);
                ESP_LOGI(TAG, "  Debounce:  %d/%d sec", status.min_on_time_s, status.min_off_time_s);
            }
            ESP_LOGI(TAG, "======================================================");
            ESP_LOGI(TAG, "");
            return;
        }

        // Show specific relay
        uint8_t id = (uint8_t)atoi(arg);
        if (id >= RELAY_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid relay ID (0-%d)", RELAY_MAX_COUNT-1);
            return;
        }

        // Use new C API to get relay status
        relay_status_t status;
        if (relay_get_status(id, &status) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get relay %d status", id);
            return;
        }

        ESP_LOGI(TAG, "Relay %d [%s]: %s, %s, state=%s, gpio=%d, active=%s, power=%dW, priority=%d, debounce=%d/%ds, name='%s'",
                 id,
                 relay_type_str(status.type),
                 status.enabled ? "enabled" : "disabled",
                 status.initialized ? "init" : "not-init",
                 relay_state_str(status.state),
                 status.gpio_pin,
                 status.active_high ? "HIGH" : "LOW",
                 status.nominal_power_w,
                 status.priority,
                 status.min_on_time_s,
                 status.min_off_time_s,
                 status.name[0] ? status.name : "(none)");
        return;
    }

    if (strcmp(cmd, "hw-relay-save") == 0) {
        if (strlen(arg) == 0) {
            // Save all relays using new C API
            if (relay_save_all() == ESP_OK) {
                ESP_LOGI(TAG, "All relay configurations saved to NVS");
            } else {
                ESP_LOGE(TAG, "Failed to save relay configurations");
            }
            return;
        }

        // Save specific relay using new C API
        uint8_t id = (uint8_t)atoi(arg);
        if (id >= RELAY_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid relay ID (0-%d)", RELAY_MAX_COUNT - 1);
            return;
        }

        if (relay_save_config(id) == ESP_OK) {
            ESP_LOGI(TAG, "Relay %d configuration saved to NVS", id);
        } else {
            ESP_LOGE(TAG, "Failed to save relay %d configuration", id);
        }
        return;
    }

    // ================================================================
    // v2.0: i2c-scan - scan I2C bus for devices
    // ================================================================
    if (strcmp(cmd, "i2c-scan") == 0) {
        int bus = 0;
        if (arg[0]) sscanf(arg, "%d", &bus);
        if (!i2c_bus_is_initialized((uint8_t)bus)) {
            ESP_LOGW(TAG, "I2C bus %d not initialized", bus);
        } else {
            uint8_t found[16];
            uint8_t count = 0;
            i2c_bus_scan((uint8_t)bus, found, 16, &count);
            ESP_LOGI(TAG, "I2C bus %d scan: %d device(s) found", bus, count);
            for (uint8_t i = 0; i < count; i++) {
                ESP_LOGI(TAG, "  [%d] 0x%02X", i, found[i]);
            }
        }
        return;
    }

    // i2c-init <bus> <sda> <scl> [freq] - bring up a second I2C bus at runtime
    // (e.g. DimmerLink on its own bus to avoid a 0x50 address clash with rbAmp).
    if (strcmp(cmd, "i2c-init") == 0) {
        int bus = -1, sda = -1, scl = -1; unsigned freq = 50000;
        if (!arg[0] || sscanf(arg, "%d %d %d %u", &bus, &sda, &scl, &freq) < 3) {
            ESP_LOGI(TAG, "Usage: i2c-init <bus> <sda> <scl> [freq]  (e.g. i2c-init 1 19 23 50000)");
            return;
        }
        esp_err_t err = i2c_bus_init((uint8_t)bus, sda, scl, freq);
        ESP_LOGI(TAG, "i2c-init bus %d sda=%d scl=%d freq=%u -> %s",
                 bus, sda, scl, freq, esp_err_to_name(err));
        return;
    }

    // dev-identify <bus> <addr> - identify a device via the VERSION-gate protocol
    // (family/model/channels/uid). Core of auto-discovery.
    if (strcmp(cmd, "dev-identify") == 0) {
        unsigned bus = 0, addr = 0;
        if (!arg[0] || sscanf(arg, "%u %i", &bus, &addr) != 2) {
            ESP_LOGI(TAG, "Usage: dev-identify <bus> <addr>  (e.g. dev-identify 0 0x51)");
            return;
        }
        device_ident_t id;
        esp_err_t err = device_identify((uint8_t)bus, (uint8_t)addr, &id);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "dev-identify 0x%02X: %s", addr, esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "dev-identify 0x%02X: family=%s ver=0x%02X pid=0x%02X variant=%u channels=%u uid=%s",
                 addr, device_family_name(id.family), id.version, id.product_id,
                 id.hw_variant, id.channels, id.has_uid ? "present" : "none");
        return;
    }

    // dev-scan [bus] - on-demand quiescent scan + non-destructive reconcile.
    if (strcmp(cmd, "dev-scan") == 0) {
        unsigned bus = 0;
        if (arg[0]) sscanf(arg, "%u", &bus);
        esp_err_t err = devreg_scan_i2c((uint8_t)bus);
        ESP_LOGI(TAG, "dev-scan bus %u: %s (registry now %u entries)",
                 bus, esp_err_to_name(err), (unsigned)devreg_count());
        return;
    }

    // dev-list - show the unified device registry.
    if (strcmp(cmd, "dev-list") == 0) {
        size_t nc = devreg_count();
        ESP_LOGI(TAG, "=== Device Registry (%u) ===", (unsigned)nc);
        for (size_t i = 0; i < nc; i++) {
            const device_entry_t* d = devreg_get(i);
            if (!d) continue;
            ESP_LOGI(TAG, "  [%u] i2c bus%u 0x%02X %s ch=%u %s%s role0=%s",
                     (unsigned)i, d->bus, d->addr, device_family_name(d->family),
                     d->channels, d->online ? "online" : "offline",
                     d->has_uid ? " uid" : "", device_role_name((device_role_t)d->roles[0]));
        }
        return;
    }

    // dev-role <addr> <channel> <role> - assign a role via the registry (SoT).
    if (strcmp(cmd, "dev-role") == 0) {
        unsigned addr = 0, ch = 0; char role[16] = {};
        if (!arg[0] || sscanf(arg, "%i %u %15s", &addr, &ch, role) < 3) {
            ESP_LOGI(TAG, "Usage: dev-role <addr> <channel> <role>  (grid|solar|load|voltage|dimmer|relay|none)");
            return;
        }
        esp_err_t err = devreg_set_role(0, (uint8_t)addr, (uint8_t)ch, device_role_parse(role));
        ESP_LOGI(TAG, "dev-role 0x%02X ch%u %s: %s", addr, ch, role, esp_err_to_name(err));
        return;
    }

    // i2c-reinit <bus> <sda> <scl> [freq] - deinit then re-init a bus on new pins
    // at runtime (an already-initialized bus can't be re-init'd otherwise). Handy
    // for bench pin/swap testing without a rebuild, e.g. i2c-reinit 0 6 5.
    if (strcmp(cmd, "i2c-reinit") == 0) {
        int bus = -1, sda = -1, scl = -1; unsigned freq = 100000;
        if (!arg[0] || sscanf(arg, "%d %d %d %u", &bus, &sda, &scl, &freq) < 3) {
            ESP_LOGI(TAG, "Usage: i2c-reinit <bus> <sda> <scl> [freq]  (e.g. i2c-reinit 0 6 5)");
            return;
        }
        i2c_bus_deinit((uint8_t)bus);
        esp_err_t err = i2c_bus_init((uint8_t)bus, sda, scl, freq);
        ESP_LOGI(TAG, "i2c-reinit bus %d sda=%d scl=%d freq=%u -> %s",
                 bus, sda, scl, freq, esp_err_to_name(err));
        return;
    }

    // hw-bus1 <sda> <scl> [khz] [en] - persist the optional second I2C bus (bus 1)
    // config. Lets a module (DimmerLink / rbAmp) run on a dedicated bus to avoid
    // shared-bus read contention. Takes effect on reboot.
    if (strcmp(cmd, "hw-bus1") == 0) {
        int sda = -1, scl = -1; unsigned khz = 100, en = 1;
        if (!arg[0] || sscanf(arg, "%d %d %u %u", &sda, &scl, &khz, &en) < 2) {
            ESP_LOGI(TAG, "Usage: hw-bus1 <sda> <scl> [khz=100] [en=1]  (e.g. hw-bus1 19 23 100 1)");
            return;
        }
        bool ok = HardwareConfigManager::getInstance().setI2CBus1(
            (uint8_t)sda, (uint8_t)scl, (uint32_t)khz * 1000, en != 0);
        ESP_LOGI(TAG, "hw-bus1 sda=%d scl=%d %ukHz en=%u -> %s (reboot to apply)",
                 sda, scl, khz, en, ok ? "saved" : "FAILED");
        return;
    }

    // hw-rbamp-bus <0|1> - assign the rbAmp fleet to an I2C bus (persisted).
    if (strcmp(cmd, "hw-rbamp-bus") == 0) {
        int bus = -1;
        if (!arg[0] || sscanf(arg, "%d", &bus) != 1) {
            ESP_LOGI(TAG, "Usage: hw-rbamp-bus <0|1>");
            return;
        }
        bool ok = HardwareConfigManager::getInstance().setRbAmpBus((uint8_t)bus);
        ESP_LOGI(TAG, "hw-rbamp-bus %d -> %s (reboot to apply)",
                 bus, ok ? "saved" : "FAILED (bus must be 0 or 1)");
        return;
    }

    // hw-rbamp-drdy <gpio|-1> - enable rbAmp DRDY interrupt-driven polling on a
    // GPIO (persisted). -1 = fixed-cadence timer poll. Single critical module;
    // a multi-module fleet has unsynchronised DRDY, use the timer poll there.
    if (strcmp(cmd, "hw-rbamp-drdy") == 0) {
        int gpio = -2;
        if (!arg[0] || sscanf(arg, "%d", &gpio) != 1) {
            ESP_LOGI(TAG, "Usage: hw-rbamp-drdy <gpio|-1>  (e.g. hw-rbamp-drdy 33)");
            return;
        }
        bool ok = HardwareConfigManager::getInstance().setRbAmpDrdy(gpio);
        ESP_LOGI(TAG, "hw-rbamp-drdy %d -> %s (reboot to apply)",
                 gpio, ok ? "saved" : "FAILED");
        return;
    }

    // i2c-write [bus] <addr> <reg> <val> - low-level register poke (hex or dec).
    // bus defaults to 0. e.g. i2c-write 1 0x50 0x30 0x60
    if (strcmp(cmd, "i2c-write") == 0) {
        unsigned x0 = 0, x1 = 0, x2 = 0, x3 = 0;
        int n = arg[0] ? sscanf(arg, "%i %i %i %i", &x0, &x1, &x2, &x3) : 0;
        uint8_t bus = 0, a, r, v;
        if (n == 4)      { bus = (uint8_t)x0; a = (uint8_t)x1; r = (uint8_t)x2; v = (uint8_t)x3; }
        else if (n == 3) { bus = 0;           a = (uint8_t)x0; r = (uint8_t)x1; v = (uint8_t)x2; }
        else { ESP_LOGI(TAG, "Usage: i2c-write [bus] <addr> <reg> <val>"); return; }
        esp_err_t err = i2c_bus_write_byte(bus, a, r, v);
        ESP_LOGI(TAG, "i2c-write bus%d 0x%02X reg 0x%02X = 0x%02X -> %s",
                 bus, a, r, v, esp_err_to_name(err));
        return;
    }

    // pin-read <gpio> [samples] - sample a GPIO input level (bus-line health probe).
    // Idle I2C line should read ~100% high (pulled up). STUCK LOW = a device is
    // clamping the bus; that kills all I2C. Use on SDA(25)/SCL(26).
    if (strcmp(cmd, "pin-read") == 0) {
        int gpio = -1, n = 500;
        if (arg[0]) sscanf(arg, "%d %d", &gpio, &n);
        if (gpio < 0) { ESP_LOGI(TAG, "Usage: pin-read <gpio> [samples]  (e.g. pin-read 25)"); return; }
        int highs = 0;
        for (int i = 0; i < n; i++) {
            if (gpio_get_level((gpio_num_t)gpio)) highs++;
            esp_rom_delay_us(50);
        }
        const char* verdict = (highs == 0) ? "STUCK LOW (clamped!)"
                            : (highs == n) ? "idle HIGH (ok)" : "toggling";
        ESP_LOGI(TAG, "pin %d: %d/%d high -> %s", gpio, highs, n, verdict);
        return;
    }

    // i2c-read [bus] <addr> <reg> - read one register (hex or dec). bus default 0.
    if (strcmp(cmd, "i2c-read") == 0) {
        unsigned x0 = 0, x1 = 0, x2 = 0;
        int n = arg[0] ? sscanf(arg, "%i %i %i", &x0, &x1, &x2) : 0;
        uint8_t bus = 0, a, r;
        if (n == 3)      { bus = (uint8_t)x0; a = (uint8_t)x1; r = (uint8_t)x2; }
        else if (n == 2) { bus = 0;           a = (uint8_t)x0; r = (uint8_t)x1; }
        else { ESP_LOGI(TAG, "Usage: i2c-read [bus] <addr> <reg>"); return; }
        uint8_t val = 0;
        esp_err_t err = i2c_bus_read_byte(bus, a, r, &val);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "i2c-read bus%d 0x%02X reg 0x%02X = 0x%02X (%d)", bus, a, r, val, val);
        } else {
            ESP_LOGE(TAG, "i2c-read bus%d 0x%02X reg 0x%02X -> %s", bus, a, r, esp_err_to_name(err));
        }
        return;
    }

    // i2c-reads [bus] <addr> <reg> - read using SEPARATE transactions
    // (write(reg)+STOP, then read). For legacy DimmerLink that latches the
    // register pointer on STOP (combined read returns stale/0xFF).
    if (strcmp(cmd, "i2c-reads") == 0) {
        unsigned x0 = 0, x1 = 0, x2 = 0;
        int n = arg[0] ? sscanf(arg, "%i %i %i", &x0, &x1, &x2) : 0;
        uint8_t bus = 0, a, r;
        if (n == 3)      { bus = (uint8_t)x0; a = (uint8_t)x1; r = (uint8_t)x2; }
        else if (n == 2) { bus = 0;           a = (uint8_t)x0; r = (uint8_t)x1; }
        else { ESP_LOGI(TAG, "Usage: i2c-reads [bus] <addr> <reg>"); return; }
        uint8_t val = 0;
        esp_err_t err = i2c_bus_read_reg_stop(bus, a, r, &val, 1);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "i2c-reads bus%d 0x%02X reg 0x%02X = 0x%02X (%d)", bus, a, r, val, val);
        } else {
            ESP_LOGE(TAG, "i2c-reads bus%d 0x%02X reg 0x%02X -> %s", bus, a, r, esp_err_to_name(err));
        }
        return;
    }

    // ================================================================
    // v2.0: dl-status [slot] - show DimmerLink device status
    // ================================================================
    if (strcmp(cmd, "dl-status") == 0) {
        if (!dl_manager_is_initialized()) {
            ESP_LOGW(TAG, "DimmerLink manager not initialized");
            return;
        }
        int target = -1;  /* -1 = all */
        if (arg[0]) target = atoi(arg);

        for (uint8_t i = 0; i < DL_MAX_DEVICES; i++) {
            if (target >= 0 && i != (uint8_t)target) continue;
            const dl_device_state_t* dev = dl_manager_get_device(i);
            if (!dev || !dev->config.enabled) continue;
            ESP_LOGI(TAG, "[%d] %s addr=0x%02X role=%d online=%s",
                     i, dev->config.name, dev->config.i2c_addr,
                     dev->config.role, dev->online ? "Y" : "N");
            if (dev->online && dev->current.valid) {
                ESP_LOGI(TAG, "     I=%.3fA dir=%+d crest=%.2f",
                         (float)dev->current.rms_ma / 1000.0f,
                         dev->current.direction,
                         dev->current.crest_factor / 100.0f);
            }
            if (dev->thermal.available) {
                ESP_LOGI(TAG, "     T=%d°C state=%d maxlvl=%d%%",
                         dev->thermal.temperature_c,
                         dev->thermal.state,
                         dev->thermal.max_level);
            }
        }
        return;
    }

    // ================================================================
    // v2.0: dl-config <slot> <addr_hex> <role>
    // Usage: dl-config 0 0x50 current_grid
    // ================================================================
    if (strcmp(cmd, "dl-config") == 0) {
        char slot_str[4] = {}, addr_str[8] = {}, role_str[24] = {};
        if (!arg[0] || sscanf(arg, "%3s %7s %23s", slot_str, addr_str, role_str) < 3) {
            ESP_LOGI(TAG, "Usage: dl-config <slot> <addr_hex> <role>");
            ESP_LOGI(TAG, "  roles: current_grid current_solar current_load voltage dimmer relay");
            return;
        }
        uint8_t slot = (uint8_t)atoi(slot_str);
        uint8_t addr = (uint8_t)strtol(addr_str, NULL, 16);

        dl_device_config_t cfg = {};
        cfg.i2c_addr = addr;
        cfg.i2c_bus  = 0;
        cfg.enabled  = true;
        if (strcmp(role_str, "current_grid") == 0)       cfg.role = DL_ROLE_CURRENT_GRID;
        else if (strcmp(role_str, "current_solar") == 0) cfg.role = DL_ROLE_CURRENT_SOLAR;
        else if (strcmp(role_str, "current_load") == 0)  cfg.role = DL_ROLE_CURRENT_LOAD;
        else if (strcmp(role_str, "voltage") == 0)       cfg.role = DL_ROLE_VOLTAGE;
        else if (strcmp(role_str, "dimmer") == 0)        cfg.role = DL_ROLE_DIMMER;
        else if (strcmp(role_str, "relay") == 0)         cfg.role = DL_ROLE_RELAY;
        else { ESP_LOGE(TAG, "Unknown role: %s", role_str); return; }

        snprintf(cfg.name, sizeof(cfg.name), "DL%d", slot);
        if (dl_manager_register(slot, &cfg) == ESP_OK) {
            dl_manager_save_config();
            ESP_LOGI(TAG, "DimmerLink slot %d: 0x%02X role=%s", slot, addr, role_str);
        }
        return;
    }

#if CONFIG_ACROUTER_RBAMP_SOURCE
    // ================================================================
    // v2.0: rbamp-status | rbamp-config <addr_hex> <role>
    // ================================================================
    if (strcmp(cmd, "rbamp-status") == 0) {
        rbamp_source_module_cfg_t roles[4];
        size_t n = 0;
        rbamp_source_get_roles(roles, 4, &n);
        const char* role_names[] = {"none", "grid", "solar", "load", "voltage"};
        ESP_LOGI(TAG, "=== rbAmp Source ===");
        ESP_LOGI(TAG, "  alive modules: %u", (unsigned)rbamp_source_alive_count());
        ESP_LOGI(TAG, "  configured roles: %u", (unsigned)n);
        for (size_t i = 0; i < n; i++) {
            uint8_t r = (uint8_t)roles[i].role;
            ESP_LOGI(TAG, "  [%u] 0x%02X -> %s", (unsigned)i, roles[i].i2c_addr,
                     r < 5 ? role_names[r] : "?");
        }
        return;
    }

    if (strcmp(cmd, "rbamp-rescan") == 0) {
        esp_err_t rr = rbamp_source_rescan();
        if (rr == ESP_OK) {
            ESP_LOGI(TAG, "rbAmp rescan requested (picks up modules added after boot)");
        } else if (rr == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "rbAmp rescan disabled in this build (ACROUTER_I2C_AUTODISCOVERY=n)");
        } else {
            ESP_LOGW(TAG, "rbAmp source not initialized");
        }
        return;
    }

    // rbamp-address <cur> <new> - re-address an rbAmp module (hex or dec).
    // Async: two-phase commit runs in the poll task; module re-appears at new addr.
    if (strcmp(cmd, "rbamp-address") == 0) {
        unsigned cur = 0, neu = 0;
        if (!arg[0] || sscanf(arg, "%i %i", &cur, &neu) != 2) {
            ESP_LOGI(TAG, "Usage: rbamp-address <cur_addr> <new_addr>  (e.g. rbamp-address 0x50 0x52)");
            return;
        }
        esp_err_t err = rbamp_source_request_address_change((uint8_t)cur, (uint8_t)neu);
        ESP_LOGI(TAG, "rbamp-address 0x%02X -> 0x%02X: %s%s", cur, neu, esp_err_to_name(err),
                 err == ESP_OK ? " (queued; re-appears at new addr after reset)" : "");
        return;
    }

    // rbamp-ct-model <addr> <code> - set SCT-013 CT model (preset code) on ch0.
    // Codes (v1.3): 1=5A 2=10A 6=20A 3=30A 4=50A (60A/100A not implemented).
    if (strcmp(cmd, "rbamp-ct-model") == 0) {
        unsigned addr = 0, code = 0;
        if (!arg[0] || sscanf(arg, "%i %u", &addr, &code) != 2) {
            ESP_LOGI(TAG, "Usage: rbamp-ct-model <addr> <code>  (1=5A 2=10A 6=20A 3=30A 4=50A)");
            return;
        }
        esp_err_t err = rbamp_source_request_ct_model((uint8_t)addr, (uint8_t)code);
        ESP_LOGI(TAG, "rbamp-ct-model 0x%02X code %u: %s%s", addr, code, esp_err_to_name(err),
                 err == ESP_OK ? " (queued; verify-then-set)" : "");
        return;
    }

    // dl-address <cur> <new> - re-address a DimmerLink device (hex or dec).
    // Stages 0x30 + RESET; applies on reset, re-appears at new addr.
    if (strcmp(cmd, "dl-address") == 0) {
        unsigned cur = 0, neu = 0;
        if (!arg[0] || sscanf(arg, "%i %i", &cur, &neu) != 2) {
            ESP_LOGI(TAG, "Usage: dl-address <cur_addr> <new_addr>  (e.g. dl-address 0x51 0x50)");
            return;
        }
        esp_err_t err = dl_manager_change_address((uint8_t)cur, (uint8_t)neu);
        ESP_LOGI(TAG, "dl-address 0x%02X -> 0x%02X: %s%s", cur, neu, esp_err_to_name(err),
                 err == ESP_OK ? " (applies on reset)" : "");
        return;
    }

    if (strcmp(cmd, "rbamp-config") == 0) {
        char addr_str[8] = {}, role_str[16] = {};
        if (!arg[0] || sscanf(arg, "%7s %15s", addr_str, role_str) < 2) {
            ESP_LOGI(TAG, "Usage: rbamp-config <addr_hex> <role>");
            ESP_LOGI(TAG, "  roles: grid solar load voltage none");
            return;
        }
        uint8_t addr = (uint8_t)strtol(addr_str, NULL, 16);
        rbamp_source_role_t role;
        if      (strcmp(role_str, "grid") == 0)    role = RBAMP_ROLE_GRID;
        else if (strcmp(role_str, "solar") == 0)   role = RBAMP_ROLE_SOLAR;
        else if (strcmp(role_str, "load") == 0)    role = RBAMP_ROLE_LOAD;
        else if (strcmp(role_str, "voltage") == 0) role = RBAMP_ROLE_VOLTAGE;
        else if (strcmp(role_str, "none") == 0)    role = RBAMP_ROLE_NONE;
        else { ESP_LOGE(TAG, "Unknown role: %s (grid|solar|load|voltage|none)", role_str); return; }
        if (rbamp_source_set_role(addr, role) == ESP_OK) {
            rbamp_source_save_config();
            ESP_LOGI(TAG, "rbAmp 0x%02X -> %s (saved to NVS)", addr, role_str);
        } else {
            ESP_LOGE(TAG, "Failed to set role (table full?)");
        }
        return;
    }
#endif

#if CONFIG_ACROUTER_ESPNOW_SOURCE
    // ================================================================
    // v2.0: espnow-status | espnow-config <mac> <role>
    // ================================================================
    if (strcmp(cmd, "espnow-status") == 0) {
        esp_now_source_node_info_t nodes[4];
        size_t n = 0;
        esp_now_source_get_nodes(nodes, 4, &n);
        const char* role_names[] = {"none", "grid", "solar", "load", "voltage"};
        ESP_LOGI(TAG, "=== ESP-NOW Source ===");
        ESP_LOGI(TAG, "  seen nodes: %u", (unsigned)esp_now_source_seen_count());
        for (size_t i = 0; i < n; i++) {
            const uint8_t* m = nodes[i].mac;
            uint8_t r = (uint8_t)nodes[i].role;
            ESP_LOGI(TAG, "  %02X:%02X:%02X:%02X:%02X:%02X %s %s V=%.1f I=%.3f P=%.1f f=%.2f",
                     m[0], m[1], m[2], m[3], m[4], m[5], r < 5 ? role_names[r] : "?",
                     nodes[i].online ? "online" : "offline",
                     (double)nodes[i].voltage, (double)nodes[i].current,
                     (double)nodes[i].power, (double)nodes[i].frequency);
        }
        return;
    }

    if (strcmp(cmd, "espnow-config") == 0) {
        unsigned mac[6];
        char role_str[16] = {};
        if (!arg[0] || sscanf(arg, "%x:%x:%x:%x:%x:%x %15s",
                              &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5], role_str) < 7) {
            ESP_LOGI(TAG, "Usage: espnow-config <AA:BB:CC:DD:EE:FF> <role>");
            ESP_LOGI(TAG, "  roles: grid solar load voltage none");
            return;
        }
        uint8_t m[6];
        for (int i = 0; i < 6; i++) m[i] = (uint8_t)mac[i];
        esp_now_source_role_t role;
        if      (strcmp(role_str, "grid") == 0)    role = ESPNOW_ROLE_GRID;
        else if (strcmp(role_str, "solar") == 0)   role = ESPNOW_ROLE_SOLAR;
        else if (strcmp(role_str, "load") == 0)    role = ESPNOW_ROLE_LOAD;
        else if (strcmp(role_str, "voltage") == 0) role = ESPNOW_ROLE_VOLTAGE;
        else if (strcmp(role_str, "none") == 0)    role = ESPNOW_ROLE_NONE;
        else { ESP_LOGE(TAG, "Unknown role: %s (grid|solar|load|voltage|none)", role_str); return; }
        if (esp_now_source_set_role(m, role) == ESP_OK) {
            esp_now_source_save_config();
            ESP_LOGI(TAG, "ESP-NOW %02X:%02X:%02X:%02X:%02X:%02X -> %s (saved to NVS)",
                     m[0], m[1], m[2], m[3], m[4], m[5], role_str);
        } else {
            ESP_LOGE(TAG, "Failed to set role (table full?)");
        }
        return;
    }

    // espnow-out - list discovered ESP-NOW output nodes (dimmer/relay) + per-output state
    if (strcmp(cmd, "espnow-out") == 0) {
        esp_now_source_output_node_info_t nodes[ESP_NOW_SOURCE_OUT_NODES_MAX];
        size_t n = 0;
        esp_now_source_get_output_nodes(nodes, ESP_NOW_SOURCE_OUT_NODES_MAX, &n);
        ESP_LOGI(TAG, "=== ESP-NOW Output Nodes (%u) ===", (unsigned)n);
        for (size_t i = 0; i < n; i++) {
            const uint8_t* m = nodes[i].mac;
            const char* fam = nodes[i].family == RBN_FAMILY_DIMMER ? "dimmer"
                            : nodes[i].family == RBN_FAMILY_RELAY  ? "relay" : "?";
            ESP_LOGI(TAG, "  %02X:%02X:%02X:%02X:%02X:%02X %s %s%s outs=%u",
                     m[0], m[1], m[2], m[3], m[4], m[5], fam,
                     nodes[i].online ? "online" : "offline",
                     nodes[i].failsafe ? " FAILSAFE" : "", nodes[i].out_count);
            for (uint8_t k = 0; k < nodes[i].out_count && k < ESP_NOW_SOURCE_OUT_PER_NODE; k++) {
                const esp_now_source_output_info_t* o = &nodes[i].outputs[k];
                ESP_LOGI(TAG, "    out[%u] kind=%u range %u..%u desired=%u%s applied=%u result=%u",
                         o->output_id, o->kind, o->range_min, o->range_max,
                         o->desired, o->desired_set ? "*" : "", o->applied, o->result);
            }
        }
        return;
    }

    // espnow-bind <mac> - bind an ESP-NOW node to a dimmer slot (RouterController drives it)
    if (strcmp(cmd, "espnow-bind") == 0) {
        unsigned mac[6];
        if (!arg[0] || sscanf(arg, "%x:%x:%x:%x:%x:%x",
                              &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) < 6) {
            ESP_LOGI(TAG, "Usage: espnow-bind <AA:BB:CC:DD:EE:FF>");
            return;
        }
        uint8_t m[6];
        for (int i = 0; i < 6; i++) m[i] = (uint8_t)mac[i];
        int id = dimmer_bind_espnow(m);
        if (id >= 0) ESP_LOGI(TAG, "Bound ESP-NOW node to dimmer %d (RouterController will drive it)", id);
        else         ESP_LOGE(TAG, "bind failed (no free ESP-NOW dimmer slot)");
        return;
    }

    // espnow-set <mac> <value> - drive an output directly (wire-path test, bypasses RouterController).
    // Family-aware: looks up the node's output_id 0 kind from HELLO and maps <value> accordingly —
    // dimmer: value = percent 0-100 -> permille; relay: value = 0 (off) / non-zero (on) -> 0|1.
    if (strcmp(cmd, "espnow-set") == 0) {
        unsigned mac[6];
        int val = -1;
        if (!arg[0] || sscanf(arg, "%x:%x:%x:%x:%x:%x %d",
                              &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5], &val) < 7) {
            ESP_LOGI(TAG, "Usage: espnow-set <AA:BB:CC:DD:EE:FF> <value>  (dimmer: 0-100%%; relay: 0/1)");
            return;
        }
        if (val < 0) val = 0;
        uint8_t m[6];
        for (int i = 0; i < 6; i++) m[i] = (uint8_t)mac[i];

        // Resolve kind + output_id from the discovered node (default: dimmer/out 0 if not yet seen).
        uint8_t kind = RBN_OUT_KIND_DIMMER, output_id = 0;
        esp_now_source_output_node_info_t nodes[ESP_NOW_SOURCE_OUT_NODES_MAX];
        size_t n = 0;
        esp_now_source_get_output_nodes(nodes, ESP_NOW_SOURCE_OUT_NODES_MAX, &n);
        for (size_t i = 0; i < n; i++) {
            if (memcmp(nodes[i].mac, m, 6) == 0 && nodes[i].out_count > 0) {
                kind = nodes[i].outputs[0].kind;
                output_id = nodes[i].outputs[0].output_id;
                break;
            }
        }
        uint16_t value = (kind == RBN_OUT_KIND_RELAY) ? (val ? 1 : 0)
                                                      : (uint16_t)((val > 100 ? 100 : val) * 10);
        esp_err_t err = esp_now_source_set_output(m, output_id, kind, value, 0);
        ESP_LOGI(TAG, "espnow-set %02X:%02X:%02X:%02X:%02X:%02X out=%u kind=%s value=%u: %s",
                 m[0], m[1], m[2], m[3], m[4], m[5], output_id,
                 kind == RBN_OUT_KIND_RELAY ? "relay" : "dimmer", value, esp_err_to_name(err));
        return;
    }
#endif

    // ================================================================
    // v2.0: sensor-hub - show merged sensor hub state
    // ================================================================
    if (strcmp(cmd, "sensor-hub") == 0) {
        if (!sensor_hub_is_initialized()) {
            ESP_LOGW(TAG, "Sensor Hub not initialized");
            return;
        }
        sensor_hub_state_t state;
        sensor_hub_get_state(&state);
        const char* slot_names[] = {"voltage", "grid", "solar", "load"};
        const char* src_names[]  = {"none", "adc", "i2c", "espnow", "mqtt"};
        ESP_LOGI(TAG, "=== Sensor Hub (merges=%lu) ===", (unsigned long)state.merge_count);
        ESP_LOGI(TAG, "  I2C active: %s, ADC active: %s",
                 sensor_hub_has_i2c_source() ? "Y" : "N",
                 sensor_hub_is_adc_active()  ? "Y" : "N");
        for (int i = 0; i < SENSOR_HUB_SLOTS; i++) {
            const sh_slot_state_t* s = &state.slots[i];
            if (!s->valid) continue;
            const char* src = (s->source < 5) ? src_names[s->source] : "?";
            if (s->has_power) {
                ESP_LOGI(TAG, "  %-8s %.3f  P=%.1fW  src=%s prio=%d",
                         slot_names[i], s->value, s->power, src, s->priority);
            } else {
                ESP_LOGI(TAG, "  %-8s %.3f  src=%s prio=%d",
                         slot_names[i], s->value, src, s->priority);
            }
        }
        return;
    }

    // timing - I2C poll cadence / CPU-time distribution across modules (Tier-1 debug)
    if (strcmp(cmd, "timing") == 0) {
        uint32_t rb_last = 0, rb_avg = 0, rb_cnt = 0;
        rbamp_source_get_timing(&rb_last, &rb_avg, &rb_cnt);
        uint32_t dl_last = 0, dl_avg = 0, dl_cnt = 0;
        dl_manager_get_timing(&dl_last, &dl_avg, &dl_cnt);
        sensor_hub_state_t st;
        sensor_hub_get_state(&st);
        ESP_LOGI(TAG, "=== Timing / I2C poll cadence ===");
        ESP_LOGI(TAG, "  rbAmp poll:  last=%luus avg=%luus cycles=%lu",
                 (unsigned long)rb_last, (unsigned long)rb_avg, (unsigned long)rb_cnt);
        ESP_LOGI(TAG, "  DimmerLink:  last=%luus avg=%luus cycles=%lu",
                 (unsigned long)dl_last, (unsigned long)dl_avg, (unsigned long)dl_cnt);
        ESP_LOGI(TAG, "  SensorHub:   merges=%lu (control loop runs 1:1 per merge)",
                 (unsigned long)st.merge_count);
        ESP_LOGI(TAG, "  I2C source active: %s", sensor_hub_has_i2c_source() ? "Y" : "N");
        ESP_LOGI(TAG, "  (poll interval target 200ms/5Hz; last/avg = I2C bus time per cycle)");
        return;
    }

    ESP_LOGW(TAG, "Unknown command: %s", cmd);
    ESP_LOGI(TAG, "Type 'help' for available commands");
}

// ============================================================
// Config Commands
// ============================================================

bool SerialCommand::handleConfigCommand(const char* cmd, const char* arg) {
    if (!m_config) return false;

    // config-show - show all configuration
    if (strcmp(cmd, "config-show") == 0) {
        m_config->printConfig();
        return true;
    }

    // config-reset - reset to defaults
    if (strcmp(cmd, "config-reset") == 0) {
        m_config->resetToDefaults();
        ESP_LOGI(TAG, "Configuration reset to defaults");
        if (m_router) {
            const SystemConfig& cfg = m_config->getConfig();
            m_router->setControlGain(cfg.control_gain);
            m_router->setBalanceThreshold(cfg.balance_threshold);
            m_router->setGridCurrentLimit(cfg.grid_current_limit);
            m_router->setMode(static_cast<RouterMode>(cfg.router_mode));
        }
        return true;
    }

    // hardware-reset - reset hardware configuration to factory defaults
    if (strcmp(cmd, "hardware-reset") == 0) {
        ESP_LOGI(TAG, "Resetting hardware configuration to factory defaults...");

        HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
        if (hwConfig.resetToDefaults()) {
            ESP_LOGI(TAG, "Hardware configuration reset successful");
            ESP_LOGI(TAG, "IMPORTANT: Reboot required for changes to take effect!");
            ESP_LOGI(TAG, "Use 'reboot' command to restart");
        } else {
            ESP_LOGE(TAG, "ERROR: Failed to reset hardware configuration");
        }
        return true;
    }

    // hardware-voltage-show - show voltage sensor configuration
    if (strcmp(cmd, "hardware-voltage-show") == 0) {
        HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
        const HardwareConfig& config = hwConfig.getConfig();

        ESP_LOGI(TAG, "\n========== Voltage Sensor Configuration ==========");

        // Find voltage sensor channel
        bool found = false;
        for (int i = 0; i < 4; i++) {
            if (config.adc_channels[i].type == SensorType::VOLTAGE_AC && config.adc_channels[i].enabled) {
                const ADCChannelConfig& ch = config.adc_channels[i];
                ESP_LOGI(TAG, "Channel:     %d", i);
                ESP_LOGI(TAG, "GPIO:        %d", ch.gpio);
                ESP_LOGI(TAG, "Driver:      %s", getVoltageSensorDriverName(ch.voltage_driver));
                ESP_LOGI(TAG, "Nominal VDC: %.2f V", ch.nominal_vdc);
                ESP_LOGI(TAG, "Multiplier:  %.2f", ch.multiplier);
                ESP_LOGI(TAG, "Offset:      %.2f", ch.offset);
                ESP_LOGI(TAG, "Status:      ENABLED");
                found = true;
                break;
            }
        }

        if (!found) {
            ESP_LOGW(TAG, "No voltage sensor configured");
        }

        ESP_LOGI(TAG, "==================================================\n");
        return true;
    }

    // hardware-voltage-config-type <type> - set voltage sensor driver type
    if (strcmp(cmd, "hardware-voltage-config-type") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "Usage: hardware-voltage-config-type <type>");
            ESP_LOGI(TAG, "Available types: ZMPT107, ZMPT101B, CUSTOM");
            return true;
        }

        VoltageSensorDriver driver = parseVoltageSensorType(arg);
        HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
        HardwareConfig& config = hwConfig.config();

        // Find voltage sensor channel
        bool found = false;
        for (int i = 0; i < 4; i++) {
            if (config.adc_channels[i].type == SensorType::VOLTAGE_AC) {
                ADCChannelConfig& ch = config.adc_channels[i];

                // Set driver type
                ch.voltage_driver = driver;

                // Update defaults based on driver
                float nominal_vdc, default_mult, offset;
                getVoltageSensorDefaults(driver, nominal_vdc, default_mult, offset);

                ch.nominal_vdc = nominal_vdc;
                ch.multiplier = default_mult;
                ch.offset = offset;

                // Save to NVS
                if (hwConfig.setADCChannel(i, ch)) {
                    ESP_LOGI(TAG, "Voltage sensor driver set to: %s", getVoltageSensorDriverName(driver));
                    ESP_LOGI(TAG, "Nominal VDC: %.2f V", nominal_vdc);
                    ESP_LOGI(TAG, "Default multiplier (230V): %.2f", default_mult);
                    ESP_LOGI(TAG, "IMPORTANT: Reboot required for changes to take effect!");
                    ESP_LOGI(TAG, "Adjust with 'hardware-voltage-config-multiplier <value>' if needed");
                } else {
                    ESP_LOGE(TAG, "ERROR: Failed to save configuration");
                }

                found = true;
                break;
            }
        }

        if (!found) {
            ESP_LOGE(TAG, "ERROR: No voltage sensor configured");
            ESP_LOGI(TAG, "Configure hardware first via web interface or hardware-config command");
        }

        return true;
    }

    // hardware-voltage-config-port <gpio> - set voltage sensor GPIO port
    if (strcmp(cmd, "hardware-voltage-config-port") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "Usage: hardware-voltage-config-port GPIO<pin>");
            ESP_LOGI(TAG, "Example: hardware-voltage-config-port GPIO35");
            return true;
        }

        // Parse GPIO:<pin> format
        int gpio = -1;
        if (strncasecmp(arg, "GPIO", 4) == 0) {
            gpio = atoi(arg + 4);
        } else {
            gpio = atoi(arg);  // Allow direct number
        }

        if (gpio < 32 || gpio > 39) {
            ESP_LOGE(TAG, "ERROR: Invalid GPIO pin (must be 32-39 for ADC1)");
            return true;
        }

        HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
        HardwareConfig& config = hwConfig.config();

        // Find voltage sensor channel
        bool found = false;
        for (int i = 0; i < 4; i++) {
            if (config.adc_channels[i].type == SensorType::VOLTAGE_AC) {
                ADCChannelConfig& ch = config.adc_channels[i];
                ch.gpio = gpio;

                if (hwConfig.setADCChannel(i, ch)) {
                    ESP_LOGI(TAG, "Voltage sensor GPIO set to: %d", gpio);
                    ESP_LOGI(TAG, "IMPORTANT: Reboot required for changes to take effect!");
                } else {
                    ESP_LOGE(TAG, "ERROR: Failed to save configuration");
                }

                found = true;
                break;
            }
        }

        if (!found) {
            ESP_LOGE(TAG, "ERROR: No voltage sensor configured");
        }

        return true;
    }

    // hardware-voltage-config-multiplier <value> - set voltage multiplier directly
    if (strcmp(cmd, "hardware-voltage-config-multiplier") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "Usage: hardware-voltage-config-multiplier <value>");
            ESP_LOGI(TAG, "Example: hardware-voltage-config-multiplier 332.14");
            return true;
        }

        float multiplier = atof(arg);
        if (multiplier < 0.1f || multiplier > 1000.0f) {
            ESP_LOGE(TAG, "ERROR: Invalid multiplier (valid range: 0.1-1000.0)");
            return true;
        }

        HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
        HardwareConfig& config = hwConfig.config();

        // Find voltage sensor channel
        bool found = false;
        for (int i = 0; i < 4; i++) {
            if (config.adc_channels[i].type == SensorType::VOLTAGE_AC) {
                ADCChannelConfig& ch = config.adc_channels[i];
                ch.multiplier = multiplier;

                if (hwConfig.setADCChannel(i, ch)) {
                    ESP_LOGI(TAG, "Voltage multiplier set to: %.2f", multiplier);
                    ESP_LOGI(TAG, "IMPORTANT: Reboot required for changes to take effect!");
                } else {
                    ESP_LOGE(TAG, "ERROR: Failed to save configuration");
                }

                found = true;
                break;
            }
        }

        if (!found) {
            ESP_LOGE(TAG, "ERROR: No voltage sensor configured");
        }

        return true;
    }

    // hardware-current-config <binding> <sensor_type> GPIO<pin> - configure current sensor
    if (strcmp(cmd, "hardware-current-config") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "Usage: hardware-current-config <binding> <sensor_type> GPIO<pin>");
            ESP_LOGI(TAG, "Bindings: GRID, SOLAR, LOAD_1, LOAD_2, ..., LOAD_8");
            ESP_LOGI(TAG, "Sensor types: SCT013-5A, SCT013-10A, SCT013-20A, SCT013-30A,");
            ESP_LOGI(TAG, "              SCT013-50A, SCT013-60A, SCT013-80A, SCT013-100A,");
            ESP_LOGI(TAG, "              ACS712-5A, ACS712-20A, ACS712-30A");
            ESP_LOGI(TAG, "Examples:");
            ESP_LOGI(TAG, "  hardware-current-config GRID SCT013-50A GPIO36");
            ESP_LOGI(TAG, "  hardware-current-config SOLAR ACS712-30A GPIO39");
            ESP_LOGI(TAG, "  hardware-current-config LOAD_1 SCT013-30A GPIO34");
            return true;
        }

        // Parse arguments: <binding> <sensor_type> GPIO<pin>
        char binding_str[16], type_str[16], gpio_str[16];
        int parsed = sscanf(arg, "%15s %15s %15s", binding_str, type_str, gpio_str);

        if (parsed != 3) {
            ESP_LOGE(TAG, "ERROR: Invalid arguments");
            ESP_LOGI(TAG, "Usage: hardware-current-config <binding> <sensor_type> GPIO<pin>");
            return true;
        }

        // Parse binding
        SensorType binding_type;
        if (!parseCurrentSensorBinding(binding_str, binding_type)) {
            ESP_LOGE(TAG, "ERROR: Invalid binding '%s'", binding_str);
            ESP_LOGI(TAG, "Valid bindings: GRID, SOLAR, LOAD_1..LOAD_8");
            return true;
        }

        // Parse sensor driver type
        CurrentSensorDriver driver;
        if (!parseCurrentSensorType(type_str, driver)) {
            ESP_LOGE(TAG, "ERROR: Invalid sensor type '%s'", type_str);
            ESP_LOGI(TAG, "Valid types: SCT013-5A/10A/20A/30A/50A/60A/80A/100A, ACS712-5A/10A/20A/30A/50A");
            return true;
        }

        // Parse GPIO
        int gpio = -1;
        if (strncasecmp(gpio_str, "GPIO", 4) == 0) {
            gpio = atoi(gpio_str + 4);
        } else {
            gpio = atoi(gpio_str);
        }

        if (gpio < 32 || gpio > 39) {
            ESP_LOGE(TAG, "ERROR: Invalid GPIO pin (must be 32-39 for ADC1)");
            return true;
        }

        // Get default sensor parameters
        float nominal_current, multiplier, offset;
        getCurrentSensorDefaults(driver, nominal_current, multiplier, offset);

        HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
        HardwareConfig& config = hwConfig.config();

        // Find or allocate channel for this binding
        // Priority:
        // 1. Channel with matching GPIO (reconfigure existing sensor on this GPIO)
        // 2. Channel with matching binding type (update existing binding)
        // 3. First free (NONE) channel
        // 4. First channel with any current sensor type (allow migration from old types)
        int channel_idx = -1;

        // Priority 1: Find channel with matching GPIO (reconfigure existing)
        for (int i = 0; i < 4; i++) {
            if (config.adc_channels[i].gpio == gpio && config.adc_channels[i].enabled) {
                channel_idx = i;
                ESP_LOGI(TAG, "Found existing sensor on GPIO%d (channel %d), reconfiguring...", gpio, i);
                break;
            }
        }

        // Priority 2: Find channel with matching binding type
        if (channel_idx == -1) {
            for (int i = 0; i < 4; i++) {
                if (config.adc_channels[i].type == binding_type) {
                    channel_idx = i;
                    break;
                }
            }
        }

        // Priority 3: Find first free channel
        if (channel_idx == -1) {
            for (int i = 0; i < 4; i++) {
                if (config.adc_channels[i].type == SensorType::NONE || !config.adc_channels[i].enabled) {
                    channel_idx = i;
                    break;
                }
            }
        }

        // Priority 4: Allow replacing any current sensor (migration from old types)
        if (channel_idx == -1) {
            for (int i = 0; i < 4; i++) {
                if (isCurrentSensor(config.adc_channels[i].type) && !isVoltageSensor(config.adc_channels[i].type)) {
                    channel_idx = i;
                    ESP_LOGI(TAG, "Migrating channel %d from old current sensor type to %s", i, getCurrentSensorBindingName(binding_type));
                    break;
                }
            }
        }

        if (channel_idx == -1) {
            ESP_LOGE(TAG, "ERROR: No free ADC channel available");
            ESP_LOGI(TAG, "Maximum 4 channels (1 voltage + 3 current)");
            ESP_LOGI(TAG, "All channels are occupied. Use hardware-current-list to see current config.");
            return true;
        }

        // Configure channel
        ADCChannelConfig& ch = config.adc_channels[channel_idx];
        ch.gpio = gpio;
        ch.type = binding_type;
        ch.current_driver = driver;
        ch.multiplier = multiplier;
        ch.offset = offset;
        ch.enabled = true;

        if (hwConfig.setADCChannel(channel_idx, ch)) {
            ESP_LOGI(TAG, "\n========== Current Sensor Configured ==========");
            ESP_LOGI(TAG, "Binding:     %s", getCurrentSensorBindingName(binding_type));
            ESP_LOGI(TAG, "Channel:     %d", channel_idx);
            ESP_LOGI(TAG, "GPIO:        %d", gpio);
            ESP_LOGI(TAG, "Driver:      %s", getCurrentSensorDriverName(driver));
            ESP_LOGI(TAG, "Nominal:     %.1f A", nominal_current);
            ESP_LOGI(TAG, "Multiplier:  %.2f", multiplier);
            ESP_LOGI(TAG, "DC Offset:   %.2f V", offset);
            ESP_LOGI(TAG, "===============================================\n");
            ESP_LOGI(TAG, "IMPORTANT: Reboot required for changes to take effect!");
            ESP_LOGI(TAG, "Use 'reboot' command to restart");

            if (isACS712Sensor(driver)) {
                ESP_LOGI(TAG, "\nNOTE: ACS712 sensors have DC bias (1.65V after divider)");
                ESP_LOGI(TAG, "Use 'hardware-current-calibrate-zero %s' to calibrate zero point", binding_str);
            }
        } else {
            ESP_LOGE(TAG, "ERROR: Failed to save configuration");
        }

        return true;
    }

    // hardware-current-show <binding> - show current sensor configuration
    if (strcmp(cmd, "hardware-current-show") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "Usage: hardware-current-show <binding>");
            ESP_LOGI(TAG, "Example: hardware-current-show GRID");
            return true;
        }

        // Parse binding
        SensorType binding_type;
        if (!parseCurrentSensorBinding(arg, binding_type)) {
            ESP_LOGE(TAG, "ERROR: Invalid binding '%s'", arg);
            ESP_LOGI(TAG, "Valid bindings: GRID, SOLAR, LOAD_1..LOAD_8");
            return true;
        }

        HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
        const HardwareConfig& config = hwConfig.getConfig();

        ESP_LOGI(TAG, "\n========== Current Sensor Configuration ==========");

        bool found = false;
        for (int i = 0; i < 4; i++) {
            if (config.adc_channels[i].type == binding_type && config.adc_channels[i].enabled) {
                const ADCChannelConfig& ch = config.adc_channels[i];
                ESP_LOGI(TAG, "Binding:     %s", getCurrentSensorBindingName(binding_type));
                ESP_LOGI(TAG, "Channel:     %d", i);
                ESP_LOGI(TAG, "GPIO:        %d", ch.gpio);
                ESP_LOGI(TAG, "Driver:      %s", getCurrentSensorDriverName(ch.current_driver));
                ESP_LOGI(TAG, "Multiplier:  %.2f", ch.multiplier);
                ESP_LOGI(TAG, "DC Offset:   %.2f V", ch.offset);
                ESP_LOGI(TAG, "Status:      ENABLED");
                found = true;
                break;
            }
        }

        if (!found) {
            ESP_LOGW(TAG, "No current sensor configured for binding: %s", arg);
        }

        ESP_LOGI(TAG, "===================================================\n");
        return true;
    }

    // hardware-current-list - show all configured current sensors
    if (strcmp(cmd, "hardware-current-list") == 0) {
        HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
        const HardwareConfig& config = hwConfig.getConfig();

        ESP_LOGI(TAG, "\n========== Configured Current Sensors ==========");

        bool found_any = false;
        for (int i = 0; i < 4; i++) {
            const ADCChannelConfig& ch = config.adc_channels[i];
            if (isCurrentSensor(ch.type) && ch.enabled) {
                ESP_LOGI(TAG, "[CH%d] %-10s  GPIO%-2d  %-12s  %.2f A/V  Offset: %.2fV",
                    i,
                    getCurrentSensorBindingName(ch.type),
                    ch.gpio,
                    getCurrentSensorDriverName(ch.current_driver),
                    ch.multiplier,
                    ch.offset);
                found_any = true;
            }
        }

        if (!found_any) {
            ESP_LOGW(TAG, "No current sensors configured");
        }

        ESP_LOGI(TAG, "================================================\n");
        return true;
    }

    // hardware-current-calibrate-zero <binding> - calibrate zero point (for ACS712)
    if (strcmp(cmd, "hardware-current-calibrate-zero") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "Usage: hardware-current-calibrate-zero <binding>");
            ESP_LOGI(TAG, "Example: hardware-current-calibrate-zero GRID");
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "This command calibrates the DC offset (zero point) for ACS712 sensors.");
            ESP_LOGI(TAG, "Ensure NO current is flowing through the sensor before calibration.");
            return true;
        }

        // Parse binding
        SensorType binding_type;
        if (!parseCurrentSensorBinding(arg, binding_type)) {
            ESP_LOGE(TAG, "ERROR: Invalid binding '%s'", arg);
            return true;
        }

        HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
        HardwareConfig& config = hwConfig.config();

        // Find sensor channel
        bool found = false;
        for (int i = 0; i < 4; i++) {
            if (config.adc_channels[i].type == binding_type) {
                ADCChannelConfig& ch = config.adc_channels[i];

                // Check if it's an ACS712 sensor
                if (!isACS712Sensor(ch.current_driver)) {
                    ESP_LOGE(TAG, "ERROR: This sensor is not ACS712 type");
                    ESP_LOGI(TAG, "Zero-point calibration is only needed for ACS712 sensors");
                    ESP_LOGI(TAG, "SCT-013 sensors have no DC bias and don't require calibration");
                    return true;
                }

                ESP_LOGI(TAG, "\n========== Zero-Point Calibration ==========");
                ESP_LOGI(TAG, "Ensure NO current is flowing through sensor!");
                ESP_LOGI(TAG, "Measuring DC offset...");

                // Smart-module firmware handles DC offset compensation.
                ESP_LOGI(TAG, "NOTICE: Auto-calibration not yet implemented");
                ESP_LOGI(TAG, "Smart modules compensate DC offset automatically");
                ESP_LOGI(TAG, "Manual adjustment not required in most cases");
                ESP_LOGI(TAG, "===========================================\n");

                found = true;
                break;
            }
        }

        if (!found) {
            ESP_LOGE(TAG, "ERROR: No current sensor configured for binding: %s", arg);
        }

        return true;
    }

    // hardware-current-delete <binding> - delete current sensor configuration
    if (strcmp(cmd, "hardware-current-delete") == 0) {
        if (!arg || strlen(arg) == 0) {
            ESP_LOGE(TAG, "ERROR: Missing binding parameter");
            ESP_LOGI(TAG, "Usage: hardware-current-delete <binding>");
            ESP_LOGI(TAG, "  Example: hardware-current-delete GRID");
            return true;
        }

        // Parse binding type
        SensorType binding_type;
        if (!parseCurrentSensorBinding(arg, binding_type)) {
            ESP_LOGE(TAG, "ERROR: Invalid binding '%s'", arg);
            ESP_LOGI(TAG, "Valid bindings: GRID, SOLAR, LOAD_1..LOAD_8");
            return true;
        }

        HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
        HardwareConfig& config = hwConfig.config();

        // Find sensor with matching binding type
        bool found = false;
        int channel_idx = -1;
        for (int i = 0; i < 4; i++) {
            if (config.adc_channels[i].type == binding_type && config.adc_channels[i].enabled) {
                channel_idx = i;
                found = true;
                break;
            }
        }

        if (!found) {
            ESP_LOGE(TAG, "ERROR: No sensor configured for binding: %s",
                getCurrentSensorBindingName(binding_type));
            ESP_LOGI(TAG, "Use 'hardware-current-list' to see configured sensors");
            return true;
        }

        // Save configuration details before deletion for confirmation message
        int gpio = config.adc_channels[channel_idx].gpio;
        const char* driver_name = getCurrentSensorDriverName(config.adc_channels[channel_idx].current_driver);

        // Clear channel configuration
        ADCChannelConfig& ch = config.adc_channels[channel_idx];
        ch.type = SensorType::NONE;
        ch.enabled = false;
        ch.gpio = 0;
        ch.multiplier = 0.0f;
        ch.offset = 0.0f;
        ch.current_driver = CurrentSensorDriver::CUSTOM;

        // Save to NVS
        if (hwConfig.setADCChannel(channel_idx, ch)) {
            ESP_LOGI(TAG, "Successfully deleted sensor: %s", getCurrentSensorBindingName(binding_type));
            ESP_LOGI(TAG, "  Channel: %d", channel_idx);
            ESP_LOGI(TAG, "  GPIO: %d", gpio);
            ESP_LOGI(TAG, "  Driver: %s", driver_name);
            ESP_LOGI(TAG, "\nChannel is now free and can be reassigned.");
            ESP_LOGI(TAG, "IMPORTANT: Reboot required for changes to take effect!");
        } else {
            ESP_LOGE(TAG, "ERROR: Failed to save configuration to NVS");
        }

        return true;
    }

    // hw-version-show - show NVS version information
    if (strcmp(cmd, "hw-version-show") == 0) {
        HardwareConfigManager& hw = HardwareConfigManager::getInstance();
        uint16_t nvs_version = hw.getNVSVersion();
        uint16_t fw_version = hw.getCurrentVersion();

        ESP_LOGI(TAG, "=== NVS Version Information ===");
        ESP_LOGI(TAG, "Firmware version: %d", fw_version);
        ESP_LOGI(TAG, "NVS data version: %d", nvs_version);

        if (hw.isSafeMode()) {
            ESP_LOGW(TAG, "\nSTATUS: SAFE MODE");
            ESP_LOGW(TAG, "Reason: %s", hw.getSafeModeReason().c_str());
        } else if (nvs_version == 0) {
            ESP_LOGI(TAG, "\nSTATUS: Fresh install (no NVS data)");
        } else if (nvs_version == fw_version) {
            ESP_LOGI(TAG, "\nSTATUS: OK (versions match)");
        } else {
            ESP_LOGW(TAG, "\nSTATUS: WARNING (version mismatch)");
        }

        ESP_LOGI(TAG, "==============================");
        return true;
    }

    // hw-erase-nvs - erase NVS and reset to factory defaults
    if (strcmp(cmd, "hw-erase-nvs") == 0) {
        ESP_LOGW(TAG, "WARNING: Erasing ALL hardware configuration from NVS...");
        ESP_LOGW(TAG, "This will reset to factory defaults!");

        HardwareConfigManager& hw = HardwareConfigManager::getInstance();

        if (hw.eraseAndReset()) {
            ESP_LOGI(TAG, "\nSUCCESS: NVS erased and factory defaults saved");
            ESP_LOGI(TAG, "Please reboot the device for changes to take effect");
            ESP_LOGI(TAG, "Use 'reboot' command or power cycle");
        } else {
            ESP_LOGE(TAG, "\nERROR: Failed to erase NVS");
        }

        return true;
    }

    // auth-token set <token> | clear | show  — bearer token for write/OTA endpoints
    if (strcmp(cmd, "auth-token") == 0) {
        WebServerManager& ws = WebServerManager::getInstance();
        if (strncmp(arg, "set ", 4) == 0 && arg[4]) {
            ws.setAuthToken(arg + 4);
            ESP_LOGI(TAG, "Auth token set — write/OTA endpoints now require 'Authorization: Bearer <token>'");
        } else if (strcmp(arg, "clear") == 0) {
            ws.setAuthToken("");
            ESP_LOGI(TAG, "Auth token cleared — write endpoints OPEN (dev mode)");
        } else if (strcmp(arg, "show") == 0 || !arg[0]) {
            ESP_LOGI(TAG, "Auth: %s", ws.isAuthEnforced() ? "ENFORCED (token set)" : "open (no token)");
        } else {
            ESP_LOGI(TAG, "Usage: auth-token set <token> | clear | show");
        }
        return true;
    }

    // web-url set <url> | clear | show  — external web-app URL (/ redirect + CORS origin)
    if (strcmp(cmd, "web-url") == 0) {
        WebServerManager& ws = WebServerManager::getInstance();
        if (strncmp(arg, "set ", 4) == 0 && arg[4]) {
            ws.setWebAppUrl(arg + 4);
            ESP_LOGI(TAG, "Web app URL set — / redirects there + CORS allows it");
        } else if (strcmp(arg, "clear") == 0) {
            ws.setWebAppUrl("");
            ESP_LOGI(TAG, "Web app URL cleared — open (CORS *, / shows stub)");
        } else if (strcmp(arg, "show") == 0 || !arg[0]) {
            String u = ws.getWebAppUrl();
            ESP_LOGI(TAG, "Web app URL: %s", u.length() ? u.c_str() : "(not set — open)");
        } else {
            ESP_LOGI(TAG, "Usage: web-url set <url> | clear | show");
        }
        return true;
    }

    // reboot - restart ESP32
    if (strcmp(cmd, "reboot") == 0) {
        ESP_LOGI(TAG, "Rebooting in 1 second...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        return true;
    }

    // config-gain <value> - set control gain
    if (strcmp(cmd, "config-gain") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "gain = %.1f", m_config->getControlGain());
        } else {
            float value = atof(arg);
            if (m_config->setControlGain(value)) {
                ESP_LOGI(TAG, "gain = %.1f (saved)", m_config->getControlGain());
                if (m_router) m_router->setControlGain(value);
            } else {
                ESP_LOGE(TAG, "Failed to save gain");
            }
        }
        return true;
    }

    // config-threshold <value> - set balance threshold
    if (strcmp(cmd, "config-threshold") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "threshold = %.1f W", m_config->getBalanceThreshold());
        } else {
            float value = atof(arg);
            if (m_config->setBalanceThreshold(value)) {
                ESP_LOGI(TAG, "threshold = %.1f W (saved)", m_config->getBalanceThreshold());
                if (m_router) m_router->setBalanceThreshold(value);
            } else {
                ESP_LOGE(TAG, "Failed to save threshold");
            }
        }
        return true;
    }

    // config-manual <0-100> - set manual level
    if (strcmp(cmd, "config-manual") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "manual = %d%%", m_config->getManualLevel());
        } else {
            uint8_t value = (uint8_t)atoi(arg);
            if (m_config->setManualLevel(value)) {
                ESP_LOGI(TAG, "manual = %d%% (saved)", m_config->getManualLevel());
                if (m_router) m_router->setManualLevel(value);
            } else {
                ESP_LOGE(TAG, "Failed to save manual level");
            }
        }
        return true;
    }

    // config-vcoef / config-icoef retired in v2.0 (rbAmp factory-calibrated, no on-chip
    // ADC to scale) — commands and their coefficients removed; no consumer existed.

    // config-ithresh <value> - current threshold
    if (strcmp(cmd, "config-ithresh") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "ithresh = %.2f A", m_config->getCurrentThreshold());
        } else {
            float value = atof(arg);
            if (m_config->setCurrentThreshold(value)) {
                ESP_LOGI(TAG, "ithresh = %.2f A (saved)", m_config->getCurrentThreshold());
            } else {
                ESP_LOGE(TAG, "Failed to save ithresh");
            }
        }
        return true;
    }

    // config-pthresh <value> - power threshold
    if (strcmp(cmd, "config-pthresh") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "pthresh = %.1f W", m_config->getPowerThreshold());
        } else {
            float value = atof(arg);
            if (m_config->setPowerThreshold(value)) {
                ESP_LOGI(TAG, "pthresh = %.1f W (saved)", m_config->getPowerThreshold());
            } else {
                ESP_LOGE(TAG, "Failed to save pthresh");
            }
        }
        return true;
    }

    return false;
}

// ============================================================
// Router Commands
// ============================================================

bool SerialCommand::handleRouterCommand(const char* cmd, const char* arg) {
    // router-mode <off|auto|eco|offgrid|manual|boost> - set router mode
    if (strcmp(cmd, "router-mode") == 0) {
        if (!arg) {
            if (m_config) {
                const char* modes[] = {"OFF", "AUTO", "ECO", "OFFGRID", "MANUAL", "BOOST"};
                uint8_t mode = m_config->getRouterMode();
                if (mode < 6) {
                    ESP_LOGI(TAG, "mode = %s (%d)", modes[mode], mode);
                } else {
                    ESP_LOGW(TAG, "mode = UNKNOWN (%d)", mode);
                }
            }
        } else {
            uint8_t mode = 0;
            if (strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0) mode = 0;
            else if (strcmp(arg, "auto") == 0 || strcmp(arg, "1") == 0) mode = 1;
            else if (strcmp(arg, "eco") == 0 || strcmp(arg, "2") == 0) mode = 2;
            else if (strcmp(arg, "offgrid") == 0 || strcmp(arg, "3") == 0) mode = 3;
            else if (strcmp(arg, "manual") == 0 || strcmp(arg, "4") == 0) mode = 4;
            else if (strcmp(arg, "boost") == 0 || strcmp(arg, "5") == 0) mode = 5;
            else if (strcmp(arg, "grid_limit") == 0 || strcmp(arg, "6") == 0) mode = 6;
            else {
                ESP_LOGE(TAG, "Invalid mode. Use: off, auto, eco, offgrid, manual, boost, grid_limit");
                return true;
            }

            if (m_config && m_config->setRouterMode(mode)) {
                const char* modes[] = {"OFF", "AUTO", "ECO", "OFFGRID", "MANUAL", "BOOST", "GRID_LIMIT"};
                ESP_LOGI(TAG, "mode = %s (saved)", modes[mode]);
                if (m_router) {
                    m_router->setMode(static_cast<RouterMode>(mode));
                }
            } else {
                ESP_LOGE(TAG, "Failed to save mode");
            }
        }
        return true;
    }

    // router-grid-limit <A> - set grid current limit (A) for GRID_LIMIT mode
    if (strcmp(cmd, "router-grid-limit") == 0) {
        if (!m_router) { ESP_LOGE(TAG, "Router not available"); return true; }
        if (!arg) {
            ESP_LOGI(TAG, "grid-limit = %.2f A", m_router->getGridCurrentLimit());
        } else {
            m_router->setGridCurrentLimit(atof(arg));
            if (m_config) m_config->setGridCurrentLimit(m_router->getGridCurrentLimit());
            ESP_LOGI(TAG, "grid-limit = %.2f A (saved)", m_router->getGridCurrentLimit());
        }
        return true;
    }

    // sim-inject <role> <current_a> [voltage_v] [power_w] - Tier-0 test harness:
    // post a synthetic ACROUTER_EVENT_POWER_UPDATE so the control logic can be
    // exercised with NO hardware / NO AC. For 'voltage' role the 2nd number is
    // the voltage. Call repeatedly to simulate a 5 Hz stream (staleness = 500ms).
    if (strcmp(cmd, "sim-inject") == 0) {
        char role[16] = {0};
        float cur = 0.0f, volt = 0.0f, pwr = 0.0f;
        int n = arg ? sscanf(arg, "%15s %f %f %f", role, &cur, &volt, &pwr) : 0;
        if (n < 2) {
            ESP_LOGI(TAG, "Usage: sim-inject <grid|solar|load> <current_A> [voltage_V] [power_W]");
            ESP_LOGI(TAG, "       sim-inject voltage <voltage_V>");
            return true;
        }
        acrouter_measurements_t m;
        acrouter_measurements_init(&m);
        m.source       = ACROUTER_SOURCE_I2C;
        m.source_id    = 0xEE;  // 'sim' marker id
        m.timestamp_us = esp_timer_get_time();
        bool any = false;
        int ch = -1;
        if      (strcmp(role, "grid")  == 0) ch = ACROUTER_CH_GRID;
        else if (strcmp(role, "solar") == 0) ch = ACROUTER_CH_SOLAR;
        else if (strcmp(role, "load")  == 0) ch = ACROUTER_CH_LOAD;
        else if (strcmp(role, "voltage") == 0) ch = -1;
        else { ESP_LOGE(TAG, "Unknown role: %s (grid|solar|load|voltage)", role); return true; }

        if (ch < 0) {
            // voltage role: 2nd number is the voltage
            if (cur > 0.0f) { m.voltage_rms = cur; m.has_voltage = true; any = true; }
        } else {
            m.current_rms[ch] = cur;
            m.has_current[ch] = true;
            any = true;
            if (n >= 3 && volt > 0.0f) { m.voltage_rms = volt; m.has_voltage = true; }
            if (n >= 4) {
                m.power_active[ch] = pwr;   // signed: + import / - export
                m.has_power[ch]    = true;
                m.direction[ch]    = (pwr >  0.05f) ? ACROUTER_DIR_CONSUMING
                                   : (pwr < -0.05f) ? ACROUTER_DIR_SUPPLYING
                                                    : ACROUTER_DIR_ZERO;
            }
        }
        m.valid = any;
        esp_event_post(ACROUTER_EVENT, ACROUTER_EVENT_POWER_UPDATE, &m, sizeof(m), 0);
        ESP_LOGI(TAG, "sim-inject: role=%s I=%.3f V=%.1f P=%.1f (n=%d) posted", role, cur, volt, pwr, n);
        return true;
    }

    // dimmer <ID> <value> - control dimmer output level
    // Uses 0-based indexing: ID 0-3 for GPIO dimmers
    if (strcmp(cmd, "dimmer") == 0) {
        if (!arg) {
            // Show all dimmer status
            ESP_LOGI(TAG, "Dimmer Status (0-%d):", DIMMER_MAX_COUNT - 1);
            for (uint8_t i = DIMMER_I2C_START; i < DIMMER_ESPNOW_END; i++) {
                const dimmer_t* d = dimmer_get_const(i);
                if (d && d->type != DIMMER_TYPE_NONE) {
                    ESP_LOGI(TAG, "  [%d] %s: %d%%, gpio=%d, %s, %s",
                             i, d->name, d->level_percent, d->gpio_pin,
                             d->enabled ? "enabled" : "disabled",
                             d->initialized ? "init" : "not-init");
                }
            }
            return true;
        }

        // Parse: dimmer <ID|all> <value>
        char id_str[8];
        const char* space = strchr(arg, ' ');
        if (!space) {
            ESP_LOGI(TAG, "Usage: dimmer <0-63|all> <0-100>");
            return true;
        }

        size_t id_len = space - arg;
        if (id_len >= sizeof(id_str)) id_len = sizeof(id_str) - 1;
        strncpy(id_str, arg, id_len);
        id_str[id_len] = '\0';

        // Get value
        uint8_t value = (uint8_t)atoi(space + 1);
        if (value > 100) value = 100;

        // Parse ID (support 0-based IDs and "all")
        if (strcmp(id_str, "all") == 0) {
            // All dimmers
            uint8_t count = dimmer_set_level_all(value);
            ESP_LOGI(TAG, "All dimmers = %d%% (%d updated)", value, count);
        } else {
            uint8_t dimmer_id = (uint8_t)atoi(id_str);

            if (dimmer_id >= DIMMER_MAX_COUNT) {
                ESP_LOGE(TAG, "Invalid dimmer ID. Use: 0-%d or all", DIMMER_MAX_COUNT - 1);
                return true;
            }

            esp_err_t err = dimmer_set_level(dimmer_id, value);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Dimmer %d = %d%%", dimmer_id, value);
            } else {
                ESP_LOGE(TAG, "Failed to set dimmer %d: %s", dimmer_id, esp_err_to_name(err));
            }
        }

        // Also update RouterController to manual mode if available
        if (m_router) {
            m_router->setMode(RouterMode::MANUAL);
        }
        return true;
    }

    // hw-dimmer-curve <linear|rms|log> [ID] - configure dimmer output curve
    if (strcmp(cmd, "hw-dimmer-curve") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "Usage: hw-dimmer-curve <linear|rms|log> [0|1|2|3|all]");
            ESP_LOGI(TAG, "  linear - Linear (direct angle control)");
            ESP_LOGI(TAG, "  rms    - RMS compensated (for resistive loads)");
            ESP_LOGI(TAG, "  log    - Logarithmic (for LED loads)");
            // Show current curves
            for (uint8_t i = DIMMER_I2C_START; i < DIMMER_ESPNOW_END; i++) {
                const dimmer_t* d = dimmer_get_const(i);
                if (d && d->type != DIMMER_TYPE_NONE) {
                    ESP_LOGI(TAG, "  Dimmer %d curve = %s", i, dimmer_curve_str(d->curve));
                }
            }
            return true;
        }

        // Parse curve type
        dimmer_curve_t curve;
        char curve_str[16];
        const char* space = strchr(arg, ' ');
        if (space) {
            size_t len = space - arg;
            if (len >= sizeof(curve_str)) len = sizeof(curve_str) - 1;
            strncpy(curve_str, arg, len);
            curve_str[len] = '\0';
        } else {
            strncpy(curve_str, arg, sizeof(curve_str) - 1);
            curve_str[sizeof(curve_str) - 1] = '\0';
        }

        if (strcmp(curve_str, "linear") == 0) {
            curve = DIMMER_CURVE_LINEAR;
        } else if (strcmp(curve_str, "rms") == 0) {
            curve = DIMMER_CURVE_RMS;
        } else if (strcmp(curve_str, "log") == 0) {
            curve = DIMMER_CURVE_LOGARITHMIC;
        } else {
            ESP_LOGE(TAG, "Invalid curve type: %s", curve_str);
            return true;
        }

        // Parse ID (default: all)
        bool apply_all = true;
        uint8_t dimmer_id = 0;
        if (space) {
            const char* id_str = space + 1;
            if (strcmp(id_str, "all") != 0) {
                apply_all = false;
                dimmer_id = (uint8_t)atoi(id_str);
            }
        }

        // Apply curve
        const char* curve_name = dimmer_curve_str(curve);

        if (apply_all) {
            for (uint8_t i = DIMMER_I2C_START; i < DIMMER_ESPNOW_END; i++) {
                dimmer_set_curve(i, curve);
            }
            ESP_LOGI(TAG, "All dimmers curve = %s", curve_name);
        } else {
            if (dimmer_id >= DIMMER_MAX_COUNT) {
                ESP_LOGE(TAG, "Invalid dimmer ID. Use: 0-%d or all", DIMMER_MAX_COUNT - 1);
                return true;
            }
            dimmer_set_curve(dimmer_id, curve);
            ESP_LOGI(TAG, "Dimmer %d curve = %s", dimmer_id, curve_name);
        }

        return true;
    }

    // hw-dimmer-enable <ID|all> <on|off> - enable/disable dimmer channel
    if (strcmp(cmd, "hw-dimmer-enable") == 0) {
        if (!arg) {
            // Show current status
            ESP_LOGI(TAG, "Dimmer Enable Status:");
            for (uint8_t i = DIMMER_I2C_START; i < DIMMER_ESPNOW_END; i++) {
                const dimmer_t* d = dimmer_get_const(i);
                if (d) {
                    ESP_LOGI(TAG, "  Dimmer %d: %s (%s)",
                             i, d->enabled ? "ENABLED" : "DISABLED",
                             d->initialized ? "initialized" : "not initialized");
                }
            }
            return true;
        }

        // Parse: hw-dimmer-enable <ID|all> <on|off>
        char id_str[8];
        const char* space = strchr(arg, ' ');
        if (!space) {
            ESP_LOGI(TAG, "Usage: hw-dimmer-enable <0|1|2|3|all> <on|off>");
            return true;
        }

        size_t id_len = space - arg;
        if (id_len >= sizeof(id_str)) id_len = sizeof(id_str) - 1;
        strncpy(id_str, arg, id_len);
        id_str[id_len] = '\0';

        const char* state_str = space + 1;

        // Parse state
        bool enable;
        if (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0) {
            enable = true;
        } else if (strcmp(state_str, "off") == 0 || strcmp(state_str, "0") == 0) {
            enable = false;
        } else {
            ESP_LOGE(TAG, "Invalid state. Use: on or off");
            return true;
        }

        // Parse ID and apply
        if (strcmp(id_str, "all") == 0) {
            for (uint8_t i = DIMMER_I2C_START; i < DIMMER_ESPNOW_END; i++) {
                dimmer_set_enabled(i, enable);
            }
            ESP_LOGI(TAG, "All dimmers %s", enable ? "ENABLED" : "DISABLED");
        } else {
            uint8_t dimmer_id = (uint8_t)atoi(id_str);
            if (dimmer_id >= DIMMER_MAX_COUNT) {
                ESP_LOGE(TAG, "Invalid dimmer ID. Use: 0-%d or all", DIMMER_MAX_COUNT - 1);
                return true;
            }
            dimmer_set_enabled(dimmer_id, enable);
            ESP_LOGI(TAG, "Dimmer %d %s", dimmer_id, enable ? "ENABLED" : "DISABLED");
        }

        return true;
    }

    // v2.0: hw-dimmer-gpio removed — legacy GPIO/TRIAC dimming is gone (DimmerLink only).

    // hw-dimmer-status [ID|gpio|all] - show detailed dimmer hardware status
    if (strcmp(cmd, "hw-dimmer-status") == 0) {
        if (!arg || strcmp(arg, "gpio") == 0) {
            // Show all GPIO dimmers
            ESP_LOGI(TAG, "=== GPIO Dimmers (0-%d) [legacy, removed in v2.0] ===", DIMMER_GPIO_COUNT - 1);
            for (uint8_t i = 0; i < DIMMER_GPIO_COUNT; i++) {
                const dimmer_t* d = dimmer_get_const(i);
                if (d) {
                    ESP_LOGI(TAG, "[%d] '%s': gpio=%d, level=%d%%, %s, %s, mode=%s, curve=%s",
                             i, d->name, d->gpio_pin, d->level_percent,
                             d->enabled ? "enabled" : "disabled",
                             d->initialized ? "init" : "no-init",
                             dimmer_mode_str(d->mode),
                             dimmer_curve_str(d->curve));
                }
            }
        } else if (strcmp(arg, "all") == 0) {
            // Show all dimmers (including not-init)
            dimmer_log_status_all();
        } else {
            // Show specific dimmer
            uint8_t dimmer_id = (uint8_t)atoi(arg);
            if (dimmer_id >= DIMMER_MAX_COUNT) {
                ESP_LOGE(TAG, "Invalid dimmer ID");
                return true;
            }
            dimmer_log_status(dimmer_id);
        }
        return true;
    }

    // hw-dimmer-save [ID|all] - save dimmer config to NVS
    if (strcmp(cmd, "hw-dimmer-save") == 0) {
        if (!arg || strcmp(arg, "all") == 0) {
            esp_err_t err = dimmer_save_all();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "All dimmer configs saved to NVS");
            } else {
                ESP_LOGE(TAG, "Failed to save: %s", esp_err_to_name(err));
            }
        } else {
            uint8_t dimmer_id = (uint8_t)atoi(arg);
            esp_err_t err = dimmer_save_config(dimmer_id);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Dimmer %d config saved", dimmer_id);
            } else {
                ESP_LOGE(TAG, "Failed to save: %s", esp_err_to_name(err));
            }
        }
        return true;
    }

    // hw-dimmer-name <ID> <name> - set dimmer name
    if (strcmp(cmd, "hw-dimmer-name") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "Usage: hw-dimmer-name <0|1|2|3> <name>");
            return true;
        }

        // Parse: hw-dimmer-name <ID> <name>
        char id_str[8];
        const char* space = strchr(arg, ' ');
        if (!space) {
            ESP_LOGI(TAG, "Usage: hw-dimmer-name <0|1|2|3> <name>");
            return true;
        }

        size_t id_len = space - arg;
        if (id_len >= sizeof(id_str)) id_len = sizeof(id_str) - 1;
        strncpy(id_str, arg, id_len);
        id_str[id_len] = '\0';

        uint8_t dimmer_id = (uint8_t)atoi(id_str);
        const char* name = space + 1;

        if (dimmer_id >= DIMMER_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid dimmer ID. Use: 0-%d", DIMMER_MAX_COUNT - 1);
            return true;
        }

        dimmer_set_name(dimmer_id, name);
        ESP_LOGI(TAG, "Dimmer %d name = '%s'", dimmer_id, name);
        return true;
    }

    // hw-dimmer-power <ID> <watts> - set dimmer nominal power
    if (strcmp(cmd, "hw-dimmer-power") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "Usage: hw-dimmer-power <0|1|2|3> <watts>");
            ESP_LOGI(TAG, "Example: hw-dimmer-power 0 3000");
            return true;
        }

        char id_str[4], power_str[8];
        if (sscanf(arg, "%3s %7s", id_str, power_str) < 2) {
            ESP_LOGE(TAG, "Invalid arguments");
            return true;
        }

        uint8_t dimmer_id = (uint8_t)atoi(id_str);
        uint16_t power = (uint16_t)atoi(power_str);

        if (dimmer_id >= DIMMER_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid dimmer ID (0-%d)", DIMMER_MAX_COUNT - 1);
            return true;
        }

        if (dimmer_set_nominal_power(dimmer_id, power) == ESP_OK) {
            ESP_LOGI(TAG, "Dimmer %d power = %d W", dimmer_id, power);
        } else {
            ESP_LOGE(TAG, "Failed to set power for dimmer %d", dimmer_id);
        }
        return true;
    }

    // dimmer-priority <ID> <priority> - set dimmer priority for AUTO mode
    if (strcmp(cmd, "dimmer-priority") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "Usage: dimmer-priority <0-63> <0-255>");
            ESP_LOGI(TAG, "Example: dimmer-priority 0 0  (0=highest priority)");
            return true;
        }

        char id_str[4], priority_str[4];
        if (sscanf(arg, "%3s %3s", id_str, priority_str) < 2) {
            ESP_LOGE(TAG, "Invalid arguments");
            return true;
        }

        uint8_t dimmer_id = (uint8_t)atoi(id_str);
        uint8_t priority = (uint8_t)atoi(priority_str);

        if (dimmer_id >= DIMMER_MAX_COUNT) {
            ESP_LOGE(TAG, "Invalid dimmer ID (0-%d)", DIMMER_MAX_COUNT - 1);
            return true;
        }

        if (dimmer_set_priority(dimmer_id, priority) == ESP_OK) {
            ESP_LOGI(TAG, "Dimmer %d priority = %d", dimmer_id, priority);
        } else {
            ESP_LOGE(TAG, "Failed to set priority for dimmer %d", dimmer_id);
        }
        return true;
    }

    // router-status - show detailed router status
    if (strcmp(cmd, "router-status") == 0) {
        printStatus();
        return true;
    }

    // router-calibrate removed in v2.0 — it was a warn-only stub with no implementation;
    // rbAmp modules are factory-calibrated (the only user tuning is the CT model).

    return false;
}

// ============================================================
// Help & Status
// ============================================================

void SerialCommand::printHelp() {
    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "         AC Router Commands");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "GENERAL");
    ESP_LOGI(TAG, "  help                 - Show this help");
    ESP_LOGI(TAG, "  status               - Show router status");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "ROUTER CONTROL");
    ESP_LOGI(TAG, "  router-mode <mode>   - Set router mode");
    ESP_LOGI(TAG, "                         (off|auto|eco|offgrid|manual|boost|grid_limit)");
    ESP_LOGI(TAG, "  router-grid-limit <A>- Set grid current cap for GRID_LIMIT mode (A)");
    ESP_LOGI(TAG, "  router-status        - Show detailed status");
    ESP_LOGI(TAG, "  sim-inject <role> <A> [V] [W]");
    ESP_LOGI(TAG, "                       - TEST: inject synthetic measurement (no HW)");
    ESP_LOGI(TAG, "  timing               - I2C poll cadence / CPU-time per module");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "DIMMER CONTROL (0-based IDs: 0,1,2,3)");
    ESP_LOGI(TAG, "  dimmer <ID|all> <0-100>");
    ESP_LOGI(TAG, "                       - Set dimmer output level");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "DIMMER HARDWARE CONFIG (hw-dimmer-*)");
    ESP_LOGI(TAG, "  hw-dimmer-status [ID|gpio|all]");
    ESP_LOGI(TAG, "                       - Show detailed HW status");
    ESP_LOGI(TAG, "  hw-dimmer-enable <ID|all> <on|off>");
    ESP_LOGI(TAG, "                       - Enable/disable channel");
    ESP_LOGI(TAG, "  hw-dimmer-curve <linear|rms|log> [ID]");
    ESP_LOGI(TAG, "                       - Set output curve");
    ESP_LOGI(TAG, "  hw-dimmer-name <ID> <name>");
    ESP_LOGI(TAG, "                       - Set channel name");
    ESP_LOGI(TAG, "  hw-dimmer-power <ID> <watts>");
    ESP_LOGI(TAG, "                       - Set nominal power");
    ESP_LOGI(TAG, "  dimmer-priority <ID> <0-255>");
    ESP_LOGI(TAG, "                       - Set AUTO mode priority (0=highest)");
    ESP_LOGI(TAG, "  hw-dimmer-save [ID|all]");
    ESP_LOGI(TAG, "                       - Save config to NVS");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "CONFIGURATION (saved to NVS)");
    ESP_LOGI(TAG, "  config-show          - Show all config");
    ESP_LOGI(TAG, "  config-reset         - Reset to defaults");
    ESP_LOGI(TAG, "  hardware-reset       - Reset hardware config (ADC pins, etc.)");
    ESP_LOGI(TAG, "  config-gain [value]  - Control gain (10-1000)");
    ESP_LOGI(TAG, "  config-threshold [value]");
    ESP_LOGI(TAG, "                       - Balance threshold (W)");
    ESP_LOGI(TAG, "  config-manual [value]");
    ESP_LOGI(TAG, "                       - Manual level (0-100%%)");
    ESP_LOGI(TAG, "  config-ithresh [value]");
    ESP_LOGI(TAG, "                       - Current threshold (A)");
    ESP_LOGI(TAG, "  config-pthresh [value]");
    ESP_LOGI(TAG, "                       - Power threshold (W)");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "HARDWARE - VOLTAGE SENSOR");
    ESP_LOGI(TAG, "  hardware-voltage-show");
    ESP_LOGI(TAG, "                       - Show voltage sensor config");
    ESP_LOGI(TAG, "  hardware-voltage-config-type <type>");
    ESP_LOGI(TAG, "                       - Set sensor type (ZMPT107/ZMPT101B/CUSTOM)");
    ESP_LOGI(TAG, "  hardware-voltage-config-port GPIO<pin>");
    ESP_LOGI(TAG, "                       - Set GPIO pin (32-39)");
    ESP_LOGI(TAG, "  hardware-voltage-config-multiplier <value>");
    ESP_LOGI(TAG, "                       - Set multiplier directly (0.1-1000)");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "HARDWARE - CURRENT SENSORS");
    ESP_LOGI(TAG, "  hardware-current-config <binding> <type> GPIO<pin>");
    ESP_LOGI(TAG, "                       - Configure current sensor");
    ESP_LOGI(TAG, "                         Bindings: GRID, SOLAR, LOAD_1..8");
    ESP_LOGI(TAG, "                         Types: SCT013-5A/10A/20A/30A/50A/60A/80A/100A,");
    ESP_LOGI(TAG, "                                ACS712-5A/10A/20A/30A/50A");
    ESP_LOGI(TAG, "  hardware-current-show <binding>");
    ESP_LOGI(TAG, "                       - Show current sensor config");
    ESP_LOGI(TAG, "  hardware-current-list");
    ESP_LOGI(TAG, "                       - List all current sensors");
    ESP_LOGI(TAG, "  hardware-current-delete <binding>");
    ESP_LOGI(TAG, "                       - Delete current sensor configuration");
    ESP_LOGI(TAG, "  hardware-current-calibrate-zero <binding>");
    ESP_LOGI(TAG, "                       - Calibrate zero point (ACS712 only)");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "HARDWARE - VERSION & NVS");
    ESP_LOGI(TAG, "  hw-version-show      - Show NVS version info & safe mode status");
    ESP_LOGI(TAG, "  hardware-reset       - Reset to factory defaults (keeps NVS structure)");
    ESP_LOGI(TAG, "  hw-erase-nvs         - Full NVS erase and factory reset");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "WIFI NETWORK");
    ESP_LOGI(TAG, "  wifi-status          - Show WiFi status");
    ESP_LOGI(TAG, "  wifi-scan            - Scan for networks");
    ESP_LOGI(TAG, "  wifi-connect <ssid> [password]");
    ESP_LOGI(TAG, "                       - Connect & save to NVS");
    ESP_LOGI(TAG, "  wifi-disconnect      - Disconnect from STA");
    ESP_LOGI(TAG, "  wifi-forget          - Clear saved credentials");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "WEB SERVER");
    ESP_LOGI(TAG, "  web-status           - Show server status & URLs");
    ESP_LOGI(TAG, "  web-start            - Start web server");
    ESP_LOGI(TAG, "  web-stop             - Stop web server");
    ESP_LOGI(TAG, "  web-urls             - Show all access URLs");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "TIME SYNC (NTP)");
    ESP_LOGI(TAG, "  time-status          - Show NTP sync status");
    ESP_LOGI(TAG, "  time-sync            - Force NTP sync");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "OTA FIRMWARE UPDATE");
    ESP_LOGI(TAG, "  ota-status           - Show OTA status & URLs");
    ESP_LOGI(TAG, "  ota-check            - Check for updates on GitHub");
    ESP_LOGI(TAG, "  ota-update-github    - Download and install from GitHub");
    ESP_LOGI(TAG, "  ota-update-url <url> - Download and install from custom URL");
    ESP_LOGI(TAG, "  ota-rollback         - Rollback to previous firmware");
    ESP_LOGI(TAG, "  ota-info             - Show OTA partition info");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "MQTT");
    ESP_LOGI(TAG, "  mqtt-status          - Show MQTT connection status");
    ESP_LOGI(TAG, "  mqtt-config          - Show MQTT configuration");
    ESP_LOGI(TAG, "  mqtt-broker <url>    - Set broker (mqtt://host:port)");
    ESP_LOGI(TAG, "  mqtt-user <name>     - Set username");
    ESP_LOGI(TAG, "  mqtt-pass <pass>     - Set password");
    ESP_LOGI(TAG, "  mqtt-device-id <id>  - Set device ID for topics");
    ESP_LOGI(TAG, "  mqtt-device-name <n> - Set device name (for HA)");
    ESP_LOGI(TAG, "  mqtt-interval <ms>   - Set publish interval (1000-60000)");
    ESP_LOGI(TAG, "  mqtt-ha-discovery <0|1> - Enable/disable HA discovery");
    ESP_LOGI(TAG, "  mqtt-enable          - Enable MQTT");
    ESP_LOGI(TAG, "  mqtt-disable         - Disable MQTT");
    ESP_LOGI(TAG, "  mqtt-reconnect       - Force reconnection");
    ESP_LOGI(TAG, "  mqtt-publish         - Force publish all data");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "RELAY CONTROL (0-based IDs: 0,1,2,3)");
    ESP_LOGI(TAG, "  relay <id> <on|off|toggle> [force]");
    ESP_LOGI(TAG, "                       - Control relay state");
    ESP_LOGI(TAG, "  relay-list           - Show all relays status");
    ESP_LOGI(TAG, "  relay-all-off        - Turn all relays OFF");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "RELAY HARDWARE CONFIG (hw-relay-*)");
    ESP_LOGI(TAG, "  hw-relay-status [id] - Show HW configuration");
    ESP_LOGI(TAG, "  hw-relay-enable <id> <on|off>");
    ESP_LOGI(TAG, "                       - Enable/disable relay");
    ESP_LOGI(TAG, "  hw-relay-gpio <id> <pin>");
    ESP_LOGI(TAG, "                       - Set GPIO pin (-1 to disable)");
    ESP_LOGI(TAG, "  hw-relay-name <id> <name>");
    ESP_LOGI(TAG, "                       - Set relay name");
    ESP_LOGI(TAG, "  hw-relay-power <id> <watts>");
    ESP_LOGI(TAG, "                       - Set nominal power");
    ESP_LOGI(TAG, "  relay-priority <id> <0-255>");
    ESP_LOGI(TAG, "                       - Set AUTO mode priority (0=highest)");
    ESP_LOGI(TAG, "  hw-relay-active <id> <high|low>");
    ESP_LOGI(TAG, "                       - Set active level");
    ESP_LOGI(TAG, "  hw-relay-debounce <id> <min_on> <min_off>");
    ESP_LOGI(TAG, "                       - Set debounce times (seconds)");
    ESP_LOGI(TAG, "  hw-relay-save [id|all]");
    ESP_LOGI(TAG, "                       - Save config to NVS");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "I2C / DIMMERLINK (v2.0)");
    ESP_LOGI(TAG, "  i2c-scan             - Scan I2C bus for devices");
    ESP_LOGI(TAG, "  dl-status [slot]     - Show DimmerLink device status");
    ESP_LOGI(TAG, "  dl-config <slot> <addr_hex> <role>");
    ESP_LOGI(TAG, "                       - Configure DimmerLink device");
    ESP_LOGI(TAG, "    roles: current_grid, current_solar, current_load,");
    ESP_LOGI(TAG, "           voltage, dimmer, relay");
    ESP_LOGI(TAG, "    e.g.: dl-config 0 0x50 current_grid");
    ESP_LOGI(TAG, "  sensor-hub           - Show merged sensor hub state");
    ESP_LOGI(TAG, "    (shows which source provides each measurement)");
#if CONFIG_ACROUTER_RBAMP_SOURCE
    ESP_LOGI(TAG, "  rbamp-status         - Show rbAmp modules + roles");
    ESP_LOGI(TAG, "  rbamp-rescan         - Re-scan bus for new rbAmp modules");
    ESP_LOGI(TAG, "  rbamp-config <addr_hex> <role>");
    ESP_LOGI(TAG, "                       - Assign rbAmp role (saved to NVS)");
    ESP_LOGI(TAG, "    roles: grid, solar, load, voltage, none");
    ESP_LOGI(TAG, "    e.g.: rbamp-config 0x50 grid");
#endif
#if CONFIG_ACROUTER_ESPNOW_SOURCE
    ESP_LOGI(TAG, "  espnow-status        - Show ESP-NOW nodes + roles");
    ESP_LOGI(TAG, "  espnow-config <mac> <role>");
    ESP_LOGI(TAG, "                       - Assign ESP-NOW node role (saved to NVS)");
    ESP_LOGI(TAG, "    e.g.: espnow-config AA:BB:CC:DD:EE:FF grid");
    ESP_LOGI(TAG, "  espnow-out           - List ESP-NOW output nodes (dimmer/relay)");
    ESP_LOGI(TAG, "  espnow-bind <mac>    - Bind an output node to a dimmer (RouterController drives it)");
    ESP_LOGI(TAG, "  espnow-set <mac> <pct> - Drive an output directly (wire-path test)");
#endif
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "SYSTEM");
    ESP_LOGI(TAG, "  reboot               - Restart ESP32");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Note: Commands without [value] show current");
    ESP_LOGI(TAG, "      setting. Web UI: http://<IP>/wifi");
    ESP_LOGI(TAG, "========================================\n");
}

void SerialCommand::printStatus() {
    ESP_LOGI(TAG, "\n=== Router Status ===");

    if (m_router) {
        const RouterStatus& st = m_router->getStatus();
        const char* modes[] = {"OFF", "AUTO", "ECO", "OFFGRID", "MANUAL", "BOOST", "GRID_LIMIT"};
        const char* states[] = {"IDLE", "INCREASING", "DECREASING", "AT_MAX", "AT_MIN", "ERROR"};
        int mi = (int)st.mode, si = (int)st.state;

        ESP_LOGI(TAG, "Mode:    %s", (mi >= 0 && mi < 7) ? modes[mi] : "?");
        ESP_LOGI(TAG, "State:   %s", (si >= 0 && si < 6) ? states[si] : "?");
        ESP_LOGI(TAG, "Dimmer:  %d%%", st.dimmer_percent);
        ESP_LOGI(TAG, "Power:   %.1f W", st.power_grid);
    } else {
        ESP_LOGE(TAG, "RouterController not available");
    }

    if (m_config) {
        ESP_LOGI(TAG, "Gain:    %.1f", m_config->getControlGain());
        ESP_LOGI(TAG, "Thresh:  %.1f W", m_config->getBalanceThreshold());
    }

    ESP_LOGI(TAG, "=====================\n");
}
