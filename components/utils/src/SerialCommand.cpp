/**
 * @file SerialCommand.cpp
 * @brief Serial command processor implementation
 */

#include "SerialCommand.h"
#include "ConfigManager.h"
#include "HardwareConfigManager.h"
#include "RouterController.h"
#include "PowerMeterADC.h"
#include "VoltageSensorDrivers.h"
#include "CurrentSensorDrivers.h"
#include "WiFiManager.h"
#include "WebServerManager.h"
#include "NTPManager.h"
#include "OTAManager.h"
#include "esp_log.h"
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
                const char* start = p;
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
        int n = WiFi.scanNetworks();
        if (n == 0) {
            ESP_LOGI(TAG, "No networks found");
        } else {
            ESP_LOGI(TAG, "Found %d networks:", n);
            for (int i = 0; i < n; i++) {
                ESP_LOGI(TAG, "  %d: %s (%ld dBm) %s",
                    i + 1,
                    WiFi.SSID(i).c_str(),
                    (long)WiFi.RSSI(i),
                    WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
            }
        }
        WiFi.scanDelete();
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
            m_router->setMode(static_cast<RouterMode>(cfg.router_mode));
        }
        return true;
    }

    // hardware-reset - reset hardware configuration to factory defaults
    if (strcmp(cmd, "hardware-reset") == 0) {
        ESP_LOGI(TAG, "Resetting hardware configuration to factory defaults...");

        // CRITICAL: Stop PowerMeterADC DMA before NVS operations
        // This prevents cache conflicts during NVS write
        PowerMeterADC& powerMeter = PowerMeterADC::getInstance();
        if (powerMeter.isRunning()) {
            ESP_LOGI(TAG, "Stopping PowerMeterADC...");
            powerMeter.stop();
            delay(100);  // Allow DMA to fully stop
        }

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
                    ESP_LOGI(TAG, "Use 'hardware-voltage-calibrate <VAC>' to calibrate for your grid voltage");
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

    // hardware-voltage-calibrate <VAC> - calibrate voltage sensor (auto-measure VDC and calculate multiplier)
    if (strcmp(cmd, "hardware-voltage-calibrate") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "Usage: hardware-voltage-calibrate <measured_VAC>");
            ESP_LOGI(TAG, "Example: hardware-voltage-calibrate 232.5");
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "Steps:");
            ESP_LOGI(TAG, "  1. Measure grid voltage with multimeter (AC RMS)");
            ESP_LOGI(TAG, "  2. Enter the measured voltage");
            ESP_LOGI(TAG, "  3. System will auto-measure sensor VDC and calculate multiplier");
            ESP_LOGI(TAG, "  4. Reboot to apply changes");
            return true;
        }

        float measured_vac = atof(arg);
        if (measured_vac < 50.0f || measured_vac > 300.0f) {
            ESP_LOGE(TAG, "ERROR: Invalid voltage (valid range: 50-300V)");
            return true;
        }

        // Get PowerMeterADC instance to read raw VDC
        PowerMeterADC& powerMeter = PowerMeterADC::getInstance();

        if (!powerMeter.isRunning()) {
            ESP_LOGE(TAG, "ERROR: PowerMeterADC is not running!");
            ESP_LOGI(TAG, "Make sure the system is not in safe mode");
            return true;
        }

        HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
        HardwareConfig& config = hwConfig.config();

        // Find voltage sensor channel
        bool found = false;
        for (int i = 0; i < 4; i++) {
            if (config.adc_channels[i].type == SensorType::VOLTAGE_AC) {
                ADCChannelConfig& ch = config.adc_channels[i];

                ESP_LOGI(TAG, "\n========== Voltage Calibration ==========");
                ESP_LOGI(TAG, "Measuring sensor VDC output...");

                // Wait a bit to ensure fresh measurement
                delay(300);

                // Get raw VDC from sensor (before multiplier applied)
                float measured_vdc = powerMeter.getRawVoltageVDC();

                if (measured_vdc < 0.1f) {
                    ESP_LOGE(TAG, "ERROR: Sensor VDC too low (< 0.1V)");
                    ESP_LOGI(TAG, "Check sensor connection and grid voltage");
                    return true;
                }

                // Calculate multiplier: V_grid / V_sensor
                float multiplier = measured_vac / measured_vdc;

                ESP_LOGI(TAG, "Measured grid voltage:  %.2f V AC (from multimeter)", measured_vac);
                ESP_LOGI(TAG, "Measured sensor VDC:    %.3f V (auto-measured)", measured_vdc);
                ESP_LOGI(TAG, "Calculated multiplier:  %.2f", multiplier);
                ESP_LOGI(TAG, "=========================================\n");

                // Update nominal_vdc to actual measured value for future reference
                ch.nominal_vdc = measured_vdc;
                ch.multiplier = multiplier;

                if (hwConfig.setADCChannel(i, ch)) {
                    ESP_LOGI(TAG, "Calibration saved successfully!");
                    ESP_LOGI(TAG, "Updated:");
                    ESP_LOGI(TAG, "  - Nominal VDC: %.3f V", measured_vdc);
                    ESP_LOGI(TAG, "  - Multiplier:  %.2f", multiplier);
                    ESP_LOGI(TAG, "");
                    ESP_LOGI(TAG, "IMPORTANT: Reboot required for changes to take effect!");
                    ESP_LOGI(TAG, "Use 'reboot' command to restart");
                } else {
                    ESP_LOGE(TAG, "ERROR: Failed to save calibration");
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
            ESP_LOGI(TAG, "Valid types: SCT013-5A/10A/20A/30A/50A/60A/80A/100A, ACS712-5A/20A/30A");
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

                // TODO: Get actual DC offset from PowerMeterADC
                // For now, just inform the user
                ESP_LOGI(TAG, "NOTICE: Auto-calibration not yet implemented");
                ESP_LOGI(TAG, "PowerMeterADC automatically compensates DC offset");
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

        // CRITICAL: Stop PowerMeterADC DMA before NVS operations
        // This prevents cache conflicts during nvs_erase_all()
        PowerMeterADC& powerMeter = PowerMeterADC::getInstance();
        if (powerMeter.isRunning()) {
            ESP_LOGI(TAG, "Stopping PowerMeterADC...");
            powerMeter.stop();
            delay(100);  // Allow DMA to fully stop
        }

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

    // reboot - restart ESP32
    if (strcmp(cmd, "reboot") == 0) {
        ESP_LOGI(TAG, "Rebooting in 1 second...");
        delay(1000);
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

    // config-vcoef <value> - voltage coefficient
    if (strcmp(cmd, "config-vcoef") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "vcoef = %.1f", m_config->getVoltageCoef());
        } else {
            float value = atof(arg);
            if (m_config->setVoltageCoef(value)) {
                ESP_LOGI(TAG, "vcoef = %.1f (saved)", m_config->getVoltageCoef());
            } else {
                ESP_LOGE(TAG, "Failed to save vcoef");
            }
        }
        return true;
    }

    // config-icoef <value> - current coefficient
    if (strcmp(cmd, "config-icoef") == 0) {
        if (!arg) {
            ESP_LOGI(TAG, "icoef = %.1f A/V", m_config->getCurrentCoef());
        } else {
            float value = atof(arg);
            if (m_config->setCurrentCoef(value)) {
                ESP_LOGI(TAG, "icoef = %.1f A/V (saved)", m_config->getCurrentCoef());
            } else {
                ESP_LOGE(TAG, "Failed to save icoef");
            }
        }
        return true;
    }

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
            else {
                ESP_LOGE(TAG, "Invalid mode. Use: off, auto, eco, offgrid, manual, boost");
                return true;
            }

            if (m_config && m_config->setRouterMode(mode)) {
                const char* modes[] = {"OFF", "AUTO", "ECO", "OFFGRID", "MANUAL", "BOOST"};
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

    // router-dimmer <ID> <value> - control specific dimmer
    if (strcmp(cmd, "router-dimmer") == 0) {
        if (!arg) {
            if (m_router) {
                ESP_LOGI(TAG, "dimmer = %d%%", m_router->getStatus().dimmer_percent);
            }
        } else {
            // Parse: router-dimmer <ID> <value>
            char id_str[8];
            const char* space = strchr(arg, ' ');
            if (!space) {
                ESP_LOGI(TAG, "Usage: router-dimmer <1|2|all> <0-100>");
                return true;
            }

            size_t id_len = space - arg;
            if (id_len >= sizeof(id_str)) id_len = sizeof(id_str) - 1;
            strncpy(id_str, arg, id_len);
            id_str[id_len] = '\0';

            // Parse ID (support "1", "2", "all")
            uint8_t dimmer_id;
            if (strcmp(id_str, "all") == 0) {
                dimmer_id = 0;  // 0 = all dimmers
            } else {
                dimmer_id = (uint8_t)atoi(id_str);
                if (dimmer_id < 1 || dimmer_id > 2) {
                    ESP_LOGE(TAG, "Invalid dimmer ID. Use: 1, 2, or all");
                    return true;
                }
            }

            // Get value
            uint8_t value = (uint8_t)atoi(space + 1);
            if (value > 100) value = 100;

            if (m_router) {
                // Set manual mode and level
                m_router->setMode(RouterMode::MANUAL);
                m_router->setManualLevel(value);
                if (dimmer_id == 0) {
                    ESP_LOGI(TAG, "All dimmers = %d%% (manual mode)", value);
                } else {
                    ESP_LOGI(TAG, "Dimmer %d = %d%% (manual mode)", dimmer_id, value);
                }
                // Note: Multi-dimmer support depends on RouterController implementation
                // Currently only single dimmer is supported
            }
        }
        return true;
    }

    // router-status - show detailed router status
    if (strcmp(cmd, "router-status") == 0) {
        printStatus();
        return true;
    }

    // router-calibrate - run calibration
    if (strcmp(cmd, "router-calibrate") == 0) {
        if (m_router) {
            ESP_LOGI(TAG, "Starting calibration...");
            // Note: Calibration feature depends on RouterController implementation
            ESP_LOGW(TAG, "Calibration not implemented yet");
        } else {
            ESP_LOGE(TAG, "RouterController not available");
        }
        return true;
    }

    // debug-adc <period> - enable/disable ADC debug logging
    if (strcmp(cmd, "debug-adc") == 0) {
        PowerMeterADC& powerMeter = PowerMeterADC::getInstance();

        if (!arg) {
            // Show current debug period
            uint32_t current_period = powerMeter.getDebugPeriod();
            if (current_period == 0) {
                ESP_LOGI(TAG, "debug-adc = DISABLED");
            } else {
                ESP_LOGI(TAG, "debug-adc = %lu seconds", current_period);
            }
        } else {
            // Set debug period
            uint32_t period = (uint32_t)atoi(arg);
            powerMeter.setDebugPeriod(period);

            if (period == 0) {
                ESP_LOGI(TAG, "debug-adc = DISABLED");
            } else {
                ESP_LOGI(TAG, "debug-adc = %lu seconds (enabled)", period);
            }
        }
        return true;
    }

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
    ESP_LOGI(TAG, "                         (off|auto|eco|offgrid|manual|boost)");
    ESP_LOGI(TAG, "  router-dimmer <ID> <value>");
    ESP_LOGI(TAG, "                       - Control dimmer (ID: 1,2,all)");
    ESP_LOGI(TAG, "                         Value: 0-100%%");
    ESP_LOGI(TAG, "  router-status        - Show detailed status");
    ESP_LOGI(TAG, "  router-calibrate     - Run calibration");
    ESP_LOGI(TAG, "  debug-adc <period>   - Enable/disable ADC debug logging");
    ESP_LOGI(TAG, "                         period=0 disables, >0 = period in seconds");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "CONFIGURATION (saved to NVS)");
    ESP_LOGI(TAG, "  config-show          - Show all config");
    ESP_LOGI(TAG, "  config-reset         - Reset to defaults");
    ESP_LOGI(TAG, "  hardware-reset       - Reset hardware config (ADC pins, etc.)");
    ESP_LOGI(TAG, "  config-gain [value]  - Control gain (1-1000)");
    ESP_LOGI(TAG, "  config-threshold [value]");
    ESP_LOGI(TAG, "                       - Balance threshold (W)");
    ESP_LOGI(TAG, "  config-manual [value]");
    ESP_LOGI(TAG, "                       - Manual level (0-100%%)");
    ESP_LOGI(TAG, "  config-vcoef [value] - Voltage coefficient");
    ESP_LOGI(TAG, "  config-icoef [value] - Current coef (A/V)");
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
    ESP_LOGI(TAG, "  hardware-voltage-calibrate <VAC>");
    ESP_LOGI(TAG, "                       - Calibrate with measured voltage");
    ESP_LOGI(TAG, "  hardware-voltage-config-multiplier <value>");
    ESP_LOGI(TAG, "                       - Set multiplier directly (0.1-1000)");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "HARDWARE - CURRENT SENSORS");
    ESP_LOGI(TAG, "  hardware-current-config <binding> <type> GPIO<pin>");
    ESP_LOGI(TAG, "                       - Configure current sensor");
    ESP_LOGI(TAG, "                         Bindings: GRID, SOLAR, LOAD_1..8");
    ESP_LOGI(TAG, "                         Types: SCT013-5A/10A/20A/30A/50A/60A/80A/100A,");
    ESP_LOGI(TAG, "                                ACS712-5A/20A/30A");
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
        const char* modes[] = {"OFF", "AUTO", "ECO", "OFFGRID", "MANUAL", "BOOST"};
        const char* states[] = {"IDLE", "INCREASING", "DECREASING", "AT_MAX", "AT_MIN", "ERROR"};

        ESP_LOGI(TAG, "Mode:    %s", modes[(int)st.mode]);
        ESP_LOGI(TAG, "State:   %s", states[(int)st.state]);
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
