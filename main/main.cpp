/**
 * @file main.cpp
 * @brief AC Power Router Controller - Main Entry Point
 *
 * Main application entry point for the AC Power Router Controller.
 * Initializes all system components and starts FreeRTOS tasks.
 */

#include "Arduino.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "PowerMeterADC.h"
#include "DimmerHAL.h"
#include "RouterController.h"
#include "ConfigManager.h"
#include "HardwareConfigManager.h"
#include "SerialCommand.h"
#include "WiFiManager.h"
#include "WebServerManager.h"
#include "NTPManager.h"
#include "GitHubOTAChecker.h"
#include "MQTTManager.h"
#include "PinDefinitions.h"
#include "SensorTypes.h"
#include "VoltageSensorDrivers.h"
#include "CurrentSensorDrivers.h"

static const char* TAG = "MAIN";

// Router mode names for logging (must match RouterMode enum order)
const char* ROUTER_MODE_NAMES[] = {"OFF", "AUTO", "ECO", "OFFGRID", "MANUAL", "BOOST"};
const char* ROUTER_STATE_NAMES[] = {"IDLE", "INCREASING", "DECREASING", "AT_MAX", "AT_MIN", "ERROR"};

// Buzzer pin
#define PIN_BUZZER 4

extern "C" void app_main()
{
    // Initialize Arduino Core
    initArduino();

    // Disable buzzer
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, HIGH);

    // Setup Serial for debugging
    Serial.begin(115200);
    delay(100);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "AC Power Router Controller");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "========================================");

    // ================================================================
    // CRITICAL: Initialize NVS FIRST before any module initialization
    // ================================================================
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated - erase and retry
        ESP_LOGI(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    ESP_LOGI(TAG, "NVS Flash initialized");

    // ================================================================
    // Initialize ConfigManager (NVS)
    // ================================================================
    ESP_LOGI(TAG, "Initializing ConfigManager...");
    ConfigManager& config = ConfigManager::getInstance();

    if (!config.begin()) {
        ESP_LOGE(TAG, "Failed to initialize ConfigManager!");
        // Note: ESP_LOGW removed - causes crash during early init
    }

    // ================================================================
    // Initialize HardwareConfigManager (NVS)
    // ================================================================
    ESP_LOGI(TAG, "Initializing HardwareConfigManager...");
    HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();

    if (!hwConfig.begin()) {
        ESP_LOGE(TAG, "Failed to initialize HardwareConfigManager!");
        // Note: ESP_LOGW removed - causes crash during early init
    }

    // Check for safe mode (version mismatch)
    // Note: ESP_LOGW removed from safe mode section - causes crash during early init
    // Safe mode detection active but logging deferred until after full init
    bool safe_mode = hwConfig.isSafeMode();

    // ================================================================
    // Hardware Configuration Priority:
    // 1. Code-defined configuration (if uncommented below)
    // 2. NVS stored configuration (user changes via API/commands)
    // 3. Factory defaults (from HardwareConfig::setDefaults)
    // ================================================================

    // Option 1: Override with code-defined configuration (highest priority)
    // Uncomment and modify to force specific hardware pins:
    /*
    HardwareConfig& hwCfg = hwConfig.config();

    // ADC Channels
    hwCfg.adc_channels[0] = ADCChannelConfig(35, SensorType::VOLTAGE_AC, 230.0f, 0.0f, true);
    hwCfg.adc_channels[1] = ADCChannelConfig(39, SensorType::CURRENT_LOAD, 30.0f, 0.0f, true);
    hwCfg.adc_channels[2] = ADCChannelConfig(36, SensorType::CURRENT_GRID, 30.0f, 0.0f, true);
    hwCfg.adc_channels[3] = ADCChannelConfig(34, SensorType::CURRENT_SOLAR, 30.0f, 0.0f, true);

    // Dimmer & Zero-Cross
    hwCfg.dimmer_ch1 = DimmerChannelConfig(19, true);
    hwCfg.dimmer_ch2 = DimmerChannelConfig(23, false);
    hwCfg.zerocross_gpio = 18;
    hwCfg.zerocross_enabled = true;

    // Optionally save to NVS
    // hwConfig.saveAll();

    ESP_LOGI(TAG, "Using code-defined hardware configuration");
    */

    // ================================================================
    // Initialize WiFiManager
    // ================================================================
    ESP_LOGI(TAG, "Initializing WiFiManager...");
    WiFiManager& wifi = WiFiManager::getInstance();
    wifi.setHostname("ACRouter");

    // Two ways to configure WiFi credentials (in priority order):
    //
    // Option 1: Set credentials in code (highest priority)
    //           Uncomment lines below to use hardcoded credentials:
    /*
    WiFiConfig wifiConfig;
    strncpy(wifiConfig.sta_ssid, "MyNetwork", sizeof(wifiConfig.sta_ssid) - 1);
    strncpy(wifiConfig.sta_password, "MyPassword123", sizeof(wifiConfig.sta_password) - 1);
    if (!wifi.begin(wifiConfig)) {
        ESP_LOGE(TAG, "Failed to initialize WiFiManager!");
    }
    */
    //
    // Option 2: Use credentials from NVS (recommended for production)
    //           User configures via serial command: wifi-connect MyNetwork MyPassword
    //           Credentials auto-saved to NVS and loaded on every boot
    //
    // If no credentials (neither in code nor NVS): starts in AP-only mode

    // Start WiFi (auto-selects: code credentials OR NVS OR AP-only)
    if (!wifi.begin()) {
        ESP_LOGE(TAG, "Failed to initialize WiFiManager!");
    } else {
        const WiFiStatus& ws = wifi.getStatus();
        if (ws.ap_active) {
            ESP_LOGI(TAG, "WiFi AP started: %s, IP: %s",
                     ws.ap_ssid.c_str(),
                     wifi.getAPIP().toString().c_str());
        }
        if (ws.sta_connected) {
            ESP_LOGI(TAG, "WiFi STA connected: %s, IP: %s",
                     ws.sta_ssid.c_str(),
                     wifi.getSTAIP().toString().c_str());
        } else if (ws.state == WiFiState::STA_CONNECTING) {
            ESP_LOGI(TAG, "Connecting to STA network...");
        }
    }

    // ================================================================
    // Initialize WebServerManager
    // ================================================================
    ESP_LOGI(TAG, "Initializing WebServerManager...");
    WebServerManager& webserver = WebServerManager::getInstance();

    if (!webserver.begin(80, 81)) {
        ESP_LOGE(TAG, "Failed to initialize WebServerManager!");
    } else {
        ESP_LOGI(TAG, "WebServer started - HTTP:%d, WS:%d",
                 webserver.getHttpPort(), webserver.getWsPort());
    }

    // ================================================================
    // Initialize GitHub OTA Checker
    // ================================================================
    // Configure GitHub repository for OTA updates
    // Repository: https://github.com/robotdyn-dimmer/ACRouter
    #define GITHUB_OWNER "robotdyn-dimmer"
    #define GITHUB_REPO "ACRouter"

    // Get version from app description (set in CMakeLists.txt)
    const esp_app_desc_t* app_desc = esp_app_get_description();

    ESP_LOGI(TAG, "Initializing GitHub OTA Checker...");
    GitHubOTAChecker& otaChecker = GitHubOTAChecker::getInstance();
    otaChecker.begin(GITHUB_OWNER, GITHUB_REPO, app_desc->version);
    ESP_LOGI(TAG, "GitHub OTA Checker initialized (version: %s)", app_desc->version);

    // ================================================================
    // Initialize MQTT Manager
    // ================================================================
    // Note: MQTTManager requires RouterController and PowerMeterADC
    // These will be passed after their initialization
    ESP_LOGI(TAG, "Initializing MQTT Manager...");
    MQTTManager& mqtt = MQTTManager::getInstance();
    // Full initialization will happen after RouterController is ready

    // Get ADC configuration from HardwareConfigManager
    // This loads from NVS or uses factory defaults on first boot
    const HardwareConfig& hwCfg = hwConfig.getConfig();
    const ADCChannelConfig* adc_channels = hwCfg.adc_channels;

    // ================================================================
    // Initialize DimmerHAL
    // ================================================================
    ESP_LOGI(TAG, "Initializing DimmerHAL...");
    DimmerHAL& dimmer = DimmerHAL::getInstance();

    if (!dimmer.begin(DimmerCurve::RMS)) {
        ESP_LOGE(TAG, "Failed to initialize DimmerHAL!");
        ESP_LOGE(TAG, "System halted.");
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    ESP_LOGI(TAG, "DimmerHAL initialized, frequency=%d Hz", dimmer.getMainsFrequency());

    // ================================================================
    // Apply sensor driver multipliers from profiles
    // ================================================================
    // For current sensors, apply multiplier from driver profile
    // This ensures calibrated values from CurrentSensorDrivers.h are used
    HardwareConfig& hwCfgMutable = hwConfig.config();
    for (int ch = 0; ch < 4; ch++) {
        ADCChannelConfig& sensor = hwCfgMutable.adc_channels[ch];
        if (!sensor.enabled) continue;

        // Apply current sensor driver multiplier from profile
        if (isCurrentSensor(sensor.type)) {
            float nominal, driver_multiplier, offset;
            getCurrentSensorDefaults(sensor.current_driver, nominal, driver_multiplier, offset);

            // Only apply if driver has a valid multiplier (not CUSTOM with 0)
            if (driver_multiplier > 0.0f) {
                ESP_LOGI(TAG, "[CH%d] Applying %s driver: multiplier %.2f -> %.2f",
                         ch, getCurrentSensorDriverName(sensor.current_driver),
                         sensor.multiplier, driver_multiplier);
                sensor.multiplier = driver_multiplier;
            }
        }
    }

    // ================================================================
    // Initialize PowerMeterADC (skip in safe mode)
    // ================================================================
    PowerMeterADC& powerMeter = PowerMeterADC::getInstance();
    bool powerMeter_initialized = false;

    if (safe_mode) {
        // Note: ESP_LOGW removed - causes crash during early init with Flash string literals
        // Safe mode: skip PowerMeterADC initialization
        powerMeter_initialized = false;
    } else {
        ESP_LOGI(TAG, "Initializing PowerMeterADC...");

        if (!powerMeter.begin(adc_channels, 4)) {
            ESP_LOGE(TAG, "Failed to initialize PowerMeterADC!");
            // Note: ESP_LOGW removed - causes crash during early init
            // System continues without power measurements
            powerMeter_initialized = false;
        } else {
            powerMeter_initialized = true;
            ESP_LOGI(TAG, "PowerMeterADC initialized successfully");
        }
    }

    // ================================================================
    // Log Sensor Configuration (required for RouterController validation)
    // ================================================================
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Sensor Configuration:");
    ESP_LOGI(TAG, "========================================");

    bool has_voltage = false;
    bool has_grid = false;
    bool has_solar = false;
    bool has_load = false;

    for (int ch = 0; ch < 4; ch++) {
        const ADCChannelConfig& sensor = hwCfg.adc_channels[ch];

        if (!sensor.enabled || sensor.type == SensorType::NONE) {
            ESP_LOGI(TAG, "[CH%d] DISABLED", ch);
            continue;
        }

        const char* type_name = "UNKNOWN";
        const char* driver_name = "UNKNOWN";

        if (sensor.type == SensorType::VOLTAGE_AC) {
            type_name = "VOLTAGE_AC";
            driver_name = getVoltageSensorDriverName(sensor.voltage_driver);
            has_voltage = true;
        } else if (sensor.type == SensorType::CURRENT_GRID) {
            type_name = "CURRENT_GRID";
            driver_name = getCurrentSensorDriverName(sensor.current_driver);
            has_grid = true;
        } else if (sensor.type == SensorType::CURRENT_SOLAR) {
            type_name = "CURRENT_SOLAR";
            driver_name = getCurrentSensorDriverName(sensor.current_driver);
            has_solar = true;
        } else if (sensor.type >= SensorType::CURRENT_LOAD_1 && sensor.type <= SensorType::CURRENT_LOAD_8) {
            type_name = "CURRENT_LOAD";
            driver_name = getCurrentSensorDriverName(sensor.current_driver);
            has_load = true;
        }

        ESP_LOGI(TAG, "[CH%d] %-15s GPIO%-2d %-15s (mult=%.1f, offset=%.1f)",
                 ch,
                 type_name,
                 sensor.gpio,
                 driver_name,
                 sensor.multiplier,
                 sensor.offset);
    }

    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "Available sensors for RouterController:");
    ESP_LOGI(TAG, "  VOLTAGE_AC:     %s", has_voltage ? "YES" : "NO");
    ESP_LOGI(TAG, "  CURRENT_GRID:   %s", has_grid ? "YES" : "NO");
    ESP_LOGI(TAG, "  CURRENT_SOLAR:  %s", has_solar ? "YES" : "NO");
    ESP_LOGI(TAG, "  CURRENT_LOAD:   %s", has_load ? "YES" : "NO");
    ESP_LOGI(TAG, "========================================");

    // ================================================================
    // Initialize RouterController
    // ================================================================
    ESP_LOGI(TAG, "Initializing RouterController...");
    RouterController& router = RouterController::getInstance();

    if (!router.begin(&dimmer, DimmerChannel::CHANNEL_1)) {
        ESP_LOGE(TAG, "Failed to initialize RouterController!");
        ESP_LOGE(TAG, "System halted.");
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Apply algorithm parameters from NVS (or defaults)
    const SystemConfig& cfg = config.getConfig();
    router.setControlGain(cfg.control_gain);
    router.setBalanceThreshold(cfg.balance_threshold);

    // Validate mode against sensor configuration
    RouterMode requested_mode = static_cast<RouterMode>(cfg.router_mode);
    bool mode_valid = RouterController::validateMode(requested_mode, has_grid, has_solar);

    if (mode_valid) {
        // Mode is compatible with sensors
        router.setMode(requested_mode);
        if (cfg.router_mode == 2) {  // MANUAL mode
            router.setManualLevel(cfg.manual_level);
        }
        ESP_LOGI(TAG, "RouterController initialized: mode=%s, gain=%.1f, threshold=%.1f W",
                 ROUTER_MODE_NAMES[cfg.router_mode], cfg.control_gain, cfg.balance_threshold);
    } else {
        // Mode is incompatible with sensors - force OFF mode
        const char* reason = RouterController::getValidationFailureReason(requested_mode, has_grid, has_solar);
        ESP_LOGE(TAG, "RouterController mode validation FAILED!");
        ESP_LOGE(TAG, "  Requested mode: %s", ROUTER_MODE_NAMES[cfg.router_mode]);
        ESP_LOGE(TAG, "  Reason: %s", reason);
        ESP_LOGE(TAG, "  FORCING mode to OFF due to incompatible sensor configuration");

        // Force OFF mode (safe fallback)
        router.setMode(RouterMode::OFF);
        ESP_LOGI(TAG, "RouterController initialized: mode=OFF (forced), gain=%.1f, threshold=%.1f W",
                 cfg.control_gain, cfg.balance_threshold);
    }

    // Register RMS callback and start ADC - only if PowerMeter initialized successfully
    if (powerMeter_initialized) {
        powerMeter.setResultsCallback([](const PowerMeterADC::Measurements& m, void* user_data) {
            // This callback is invoked every 200 ms with fresh RMS data
            // This is the MAIN DRIVER for all system processing

            static uint32_t callback_count = 0;
            callback_count++;

            // === ROUTER CONTROLLER UPDATE ===
            // Feed measurements to RouterController for AUTO mode processing
            RouterController& router = RouterController::getInstance();
            router.update(m);

            // Display results every 5 callbacks (1 second)
        /*
                    if (callback_count % 150 == 0) {
                        const char* dir_str[] = {"CONSUMING", "SUPPLYING", "ZERO", "UNKNOWN"};
                        const char* phase_str[] = {"POS", "NEG", "BAL"};
                        const RouterStatus& status = router.getStatus();

                        ESP_LOGI(TAG, "=== Power Measurement (#%lu) ===", callback_count);
                        ESP_LOGI(TAG, "Voltage: %.1f V (raw VDC: %.3f V)", m.voltage_rms, m.voltage_vdc_raw);

                        // Get hardware configuration to show only active sensors
                        HardwareConfigManager& hwCfg = HardwareConfigManager::getInstance();
                        const HardwareConfig& hwConfig = hwCfg.getConfig();

                        // Log each active current sensor
                        for (int ch = 0; ch < 4; ch++) {
                            const ADCChannelConfig& sensor = hwConfig.adc_channels[ch];

                            // Skip voltage sensors and disabled channels
                            if (!sensor.enabled || sensor.type == SensorType::VOLTAGE_AC || sensor.type == SensorType::NONE) {
                                continue;
                            }

                            // Determine which channel index to use
                            int idx = -1;
                            if (sensor.type == SensorType::CURRENT_GRID) idx = PowerMeterADC::CURRENT_GRID;
                            else if (sensor.type == SensorType::CURRENT_SOLAR) idx = PowerMeterADC::CURRENT_SOLAR;
                            else if (sensor.type >= SensorType::CURRENT_LOAD_1 && sensor.type <= SensorType::CURRENT_LOAD_8) {
                                idx = PowerMeterADC::CURRENT_LOAD;
                            }

                            if (idx >= 0) {
                                // Calculate raw VDC from measured current
                                float vdc_raw = (sensor.multiplier > 0) ? (m.current_rms[idx] / sensor.multiplier) : 0.0f;

                                ESP_LOGI(TAG, "[CH%d] %-8s GPIO%-2d %s: VDC=%.3fV -> %.2f A, %s, %.0f W (V_ph=%s, I_ph=%s)",
                                        ch,
                                        getCurrentSensorBindingName(sensor.type),
                                        sensor.gpio,
                                        getCurrentSensorDriverName(sensor.current_driver),
                                        vdc_raw,
                                        m.current_rms[idx],
                                        dir_str[(int)m.direction[idx]],
                                        m.power_active[idx],
                                        phase_str[(int)m.voltage_phase],
                                        phase_str[(int)m.current_phase[idx]]);
                            }
                        }

                        ESP_LOGI(TAG, "Router:  Mode=%s, State=%s, Dimmer=%d%%",
                                ROUTER_MODE_NAMES[(int)status.mode],
                                ROUTER_STATE_NAMES[(int)status.state],
                                status.dimmer_percent);
                        ESP_LOGI(TAG, "================================");
                    }
        */    
        }, nullptr);

        // Start ADC DMA
        if (!powerMeter.start()) {
            ESP_LOGE(TAG, "Failed to start PowerMeterADC!");
            // Note: ESP_LOGW removed - causes crash during early init
            powerMeter_initialized = false;
        } else {
            ESP_LOGI(TAG, "PowerMeterADC started successfully");
        }
    }

    // ================================================================
    // Initialize Serial Command Processor
    // ================================================================
    SerialCommand& serialCmd = SerialCommand::getInstance();
    serialCmd.begin(&config, &router);

    // ================================================================
    // Complete MQTT Manager Initialization
    // ================================================================
    // Now that RouterController and PowerMeterADC are ready, pass references
    mqtt.begin(&router, powerMeter_initialized ? &powerMeter : nullptr, &config);
    ESP_LOGI(TAG, "MQTT Manager initialized (enabled=%s)", mqtt.isEnabled() ? "YES" : "NO");

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "System initialization complete");
    if (powerMeter_initialized) {
        ESP_LOGI(TAG, "Power measurement: RUNNING (callback-driven)");
    }
    // Note: ESP_LOGW for disabled state removed to prevent early boot crashes
    ESP_LOGI(TAG, "========================================");

    // NTP Manager - will be initialized when WiFi STA connects
    NTPManager& ntp = NTPManager::getInstance();
    bool ntp_initialized = false;

    // Main loop - system is now callback-driven!
    while(1) {
        // Process serial commands
        serialCmd.process();

        // Handle WiFi events
        wifi.handle();

        // Initialize NTP when STA connected and got IP
        const WiFiStatus& ws = wifi.getStatus();
        if (!ntp_initialized && ws.sta_connected && ws.sta_ip != IPAddress(0, 0, 0, 0)) {
            ESP_LOGI(TAG, "WiFi STA connected, initializing NTPManager...");
            // UTC+3, or customize for your location
            if (ntp.begin("pool.ntp.org", "EET-2EEST,M3.5.0/3,M10.5.0/4", 3 * 3600, 3600)) {
                ESP_LOGI(TAG, "NTP started - Server: pool.ntp.org");
                ntp_initialized = true;
            } else {
                ESP_LOGE(TAG, "Failed to initialize NTPManager!");
            }
        }

        // Handle WebServer requests
        webserver.handle();

        // Handle NTP sync (if initialized)
        if (ntp_initialized) {
            ntp.handle();
        }

        // Handle MQTT events and publishing
        mqtt.loop();

        // Display statistics every 10 min
        static uint32_t last_stats = 0;
        uint32_t now = millis();
        if (now - last_stats >= 600000) {
            if (powerMeter_initialized) {
                ESP_LOGI(TAG, "Statistics: Frames=%lu, Dropped=%lu, RMS=%lu, Freq=%dHz",
                         powerMeter.getFramesProcessed(),
                         powerMeter.getFramesDropped(),
                         powerMeter.getRMSUpdateCount(),
                         dimmer.getMainsFrequency());
            } else {
                ESP_LOGI(TAG, "Statistics: PowerMeter=DISABLED, Freq=%dHz", dimmer.getMainsFrequency());
            }

            // WiFi status
            const WiFiStatus& ws = wifi.getStatus();
            if (ws.sta_connected) {
                ESP_LOGI(TAG, "WiFi STA: %s, IP=%s, RSSI=%d",
                         ws.sta_ssid.c_str(),
                         ws.sta_ip.toString().c_str(),
                         ws.rssi);
            }
            if (ws.ap_active) {
                ESP_LOGI(TAG, "WiFi AP: %s, IP=%s, clients=%d",
                         ws.ap_ssid.c_str(),
                         ws.ap_ip.toString().c_str(),
                         ws.sta_clients);
            }

            last_stats = now;
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms for responsive serial input
    }
}