/**
 * @file system_init.cpp
 * @brief ACRouter system initialization - sequenced phase orchestrator
 */

#include "system_init.h"

#include "sdkconfig.h"
#include "Arduino.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "RouterController.h"
#include "ConfigManager.h"
#include "HardwareConfigManager.h"
#include "SerialCommand.h"
#include "PinDefinitions.h"
#include "SensorTypes.h"
#include "VoltageSensorDrivers.h"
#include "CurrentSensorDrivers.h"

extern "C" {
#include "dimmer_manager.h"
#include "relay_manager.h"
#include "relay_gpio.h"
}

#include "WiFiManager.h"
#include "NTPManager.h"
#include "WebServerManager.h"
#include "MQTTManager.h"
#include "GitHubOTAChecker.h"

extern "C" {
#include "i2c_bus.h"
#include "device_registry.h"
#include "dimmerlink_manager.h"
#include "sensor_hub.h"
#include "rbamp_source.h"
#include "esp_now_source.h"
}

static const char* TAG = "SysInit";

// Module state
static bool s_safe_mode = false;

// Buzzer pin
#define PIN_BUZZER 4

// ================================================================
// Phase 0: Storage
// ================================================================
static esp_err_t init_phase0_storage(void) {
    ESP_LOGI(TAG, "[Phase 0] Storage...");

    // NVS Flash
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    ESP_LOGI(TAG, "NVS Flash initialized");

    // ConfigManager
    ConfigManager& config = ConfigManager::getInstance();
    if (!config.begin()) {
        ESP_LOGE(TAG, "Failed to initialize ConfigManager!");
    }

    // HardwareConfigManager
    HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
    if (!hwConfig.begin()) {
        ESP_LOGE(TAG, "Failed to initialize HardwareConfigManager!");
    }

    s_safe_mode = hwConfig.isSafeMode();
    if (s_safe_mode) {
        ESP_LOGW(TAG, "Safe mode active - some features disabled");
    }

    // Apply sensor driver multipliers
    HardwareConfig& hwCfgMutable = hwConfig.config();
    for (int ch = 0; ch < 4; ch++) {
        ADCChannelConfig& sensor = hwCfgMutable.adc_channels[ch];
        if (!sensor.enabled) continue;

        if (sensor.type == SensorType::CURRENT_GRID ||
            sensor.type == SensorType::CURRENT_SOLAR ||
            sensor.type == SensorType::CURRENT_LOAD_1) {
            float nominal, driver_multiplier, offset;
            getCurrentSensorDefaults(sensor.current_driver, nominal, driver_multiplier, offset);
            if (driver_multiplier > 0 && sensor.multiplier != driver_multiplier) {
                ESP_LOGI(TAG, "CH%d: Applying %s multiplier: %.4f -> %.4f",
                         ch, getCurrentSensorDriverName(sensor.current_driver),
                         sensor.multiplier, driver_multiplier);
                sensor.multiplier = driver_multiplier;
            }
        }
    }

    return ESP_OK;
}

// ================================================================
// Phase 1: Buses (WiFi, I2C future)
// ================================================================
static esp_err_t init_phase1_buses(void) {
    ESP_LOGI(TAG, "[Phase 1] Buses...");

    // WiFi (before timing-critical dimmer GPIO)
    ESP_LOGI(TAG, "Initializing WiFiManager (before dimmer GPIO)...");
    WiFiManager& wifiMgr = WiFiManager::getInstance();
    if (!wifiMgr.begin()) {
        ESP_LOGE(TAG, "Failed to initialize WiFiManager!");
    } else {
        ESP_LOGI(TAG, "WiFiManager initialized (AP: %s)", wifiMgr.getStatus().ap_ssid.c_str());
    }

    // Give WiFi time to stabilize before starting timing-critical components
    ESP_LOGI(TAG, "Waiting for WiFi to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // I2C Bus (for DimmerLink modules)
    const HardwareConfig& hwCfg = HardwareConfigManager::getInstance().getConfig();
    if (hwCfg.i2c_enabled) {
        ESP_LOGI(TAG, "Initializing I2C bus 0 (SDA=%d, SCL=%d, %lu Hz)...",
                 hwCfg.i2c_sda_gpio, hwCfg.i2c_scl_gpio, (unsigned long)hwCfg.i2c_freq_hz);
        esp_err_t i2c_err = i2c_bus_init(0, hwCfg.i2c_sda_gpio, hwCfg.i2c_scl_gpio, hwCfg.i2c_freq_hz);
        if (i2c_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(i2c_err));
        } else {
            // Quick scan to report found devices
            uint8_t found[16];
            uint8_t count = 0;
            i2c_bus_scan(0, found, 16, &count);
        }
    } else {
        ESP_LOGI(TAG, "I2C bus disabled");
    }

    // Optional second I2C bus — lets a module run on a dedicated bus (e.g.
    // DimmerLink off rbAmp's bus) to avoid shared-bus read contention.
    if (hwCfg.i2c1_enabled) {
        ESP_LOGI(TAG, "Initializing I2C bus 1 (SDA=%d, SCL=%d, %lu Hz)...",
                 hwCfg.i2c1_sda_gpio, hwCfg.i2c1_scl_gpio, (unsigned long)hwCfg.i2c1_freq_hz);
        esp_err_t i2c1_err = i2c_bus_init(1, hwCfg.i2c1_sda_gpio, hwCfg.i2c1_scl_gpio, hwCfg.i2c1_freq_hz);
        if (i2c1_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize I2C bus 1: %s", esp_err_to_name(i2c1_err));
        } else {
            uint8_t found[16];
            uint8_t count = 0;
            i2c_bus_scan(1, found, 16, &count);
        }
    }

    return ESP_OK;
}

// ================================================================
// Phase 2: Output Devices
// ================================================================
static esp_err_t init_phase2_outputs(void) {
    ESP_LOGI(TAG, "[Phase 2] Output devices...");

    // v2.0: dimming is offloaded entirely to DimmerLink smart modules (on-module
    // zero-cross). The legacy direct ESP32-GPIO/TRIAC + zero-cross path (rbdimmer)
    // is removed — no ESP32 zero-cross ISR is registered.

    // Dimmer manager (loads NVS config, auto-initializes channels)
    if (dimmer_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Dimmer Manager!");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Dimmer Manager: active=%d (v2.0: dimming via DimmerLink)",
             dimmer_get_active_count());

    // Relay manager (loads NVS config, auto-initializes relays)
    if (relay_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Relay Manager!");
    } else {
        ESP_LOGI(TAG, "Relay Manager initialized: %d/%d relays active",
                 relay_get_active_count(), relay_get_enabled_count());
    }

    return ESP_OK;
}

// ================================================================
// Phase 3: Sensors
// ================================================================
static esp_err_t init_phase3_sensors(void) {
    ESP_LOGI(TAG, "[Phase 3] Sensors...");

    const HardwareConfig& hwCfg = HardwareConfigManager::getInstance().getConfig();
    const ADCChannelConfig* adc_channels = hwCfg.adc_channels;

    // v2.0 clean cut: internal ADC sensing removed. Measurements come from
    // rbAmp (I2C) / ESP-NOW smart modules via the Sensor Hub.

    // Log sensor configuration
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Sensor Configuration:");
    bool has_voltage = false, has_grid = false, has_solar = false, has_load = false;
    for (int ch = 0; ch < 4; ch++) {
        const ADCChannelConfig& sensor = adc_channels[ch];
        if (sensor.enabled) {
            const char* type_str = sensorTypeToString(sensor.type);
            ESP_LOGI(TAG, "  CH%d: GPIO%d, %s, mult=%.4f",
                     ch, sensor.gpio, type_str, sensor.multiplier);
            if (sensor.type == SensorType::VOLTAGE_AC) has_voltage = true;
            if (sensor.type == SensorType::CURRENT_GRID) has_grid = true;
            if (sensor.type == SensorType::CURRENT_SOLAR) has_solar = true;
            if (sensor.type == SensorType::CURRENT_LOAD_1) has_load = true;
        }
    }
    ESP_LOGI(TAG, "Available: V=%s, Grid=%s, Solar=%s, Load=%s",
             has_voltage ? "Y" : "N", has_grid ? "Y" : "N",
             has_solar ? "Y" : "N", has_load ? "Y" : "N");
    ESP_LOGI(TAG, "========================================");

    // DimmerLink I2C sensor modules — devices carry their own i2c_bus, so init
    // the manager if either bus is up.
    if (i2c_bus_is_initialized(0) || i2c_bus_is_initialized(1)) {
        ESP_LOGI(TAG, "Initializing DimmerLink manager...");
        if (dl_manager_init() == ESP_OK) {
            dl_manager_load_config();
            uint8_t enabled = dl_manager_get_enabled_count();
            if (enabled > 0) {
                dl_manager_start_polling(DL_DEFAULT_POLL_MS);
                ESP_LOGI(TAG, "DimmerLink: %d device(s) configured, polling started", enabled);
            } else {
                ESP_LOGI(TAG, "DimmerLink: no devices configured");
            }
        }
    }

#if CONFIG_ACROUTER_RBAMP_SOURCE
    // rbAmp I2C sensor modules (v2.0 sensing offload). Additive: feeds the
    // Sensor Hub as ACROUTER_SOURCE_I2C alongside the internal ADC. Disabled
    // by default (Kconfig); enable for bench validation against ADC readings.
    // Polls on the configured bus (rbamp_i2c_bus), so it can share bus 0 with
    // DimmerLink or run alone on a dedicated bus.
    const uint8_t rbampBus = hwCfg.rbamp_i2c_bus;
    if (i2c_bus_is_initialized(rbampBus)) {
        ESP_LOGI(TAG, "Initializing rbAmp source on bus %u...", (unsigned)rbampBus);
        // Roles: prefer NVS-persisted commissioning (rbamp-config / REST);
        // fall back to the Kconfig seed on a device that was never commissioned.
        // Without a role a module stays diagnostic-only and posts nothing.
        if (rbamp_source_load_config() != ESP_OK) {
            rbamp_source_module_cfg_t rbampRoles[4];
            size_t rbampRoleN = 0;
            if (CONFIG_ACROUTER_RBAMP_GRID_ADDR != 0) {
                rbampRoles[rbampRoleN].i2c_addr = (uint8_t)CONFIG_ACROUTER_RBAMP_GRID_ADDR;
                rbampRoles[rbampRoleN].role     = RBAMP_ROLE_GRID;
                rbampRoleN++;
            }
            if (CONFIG_ACROUTER_RBAMP_SOLAR_ADDR != 0) {
                rbampRoles[rbampRoleN].i2c_addr = (uint8_t)CONFIG_ACROUTER_RBAMP_SOLAR_ADDR;
                rbampRoles[rbampRoleN].role     = RBAMP_ROLE_SOLAR;
                rbampRoleN++;
            }
            if (CONFIG_ACROUTER_RBAMP_LOAD_ADDR != 0) {
                rbampRoles[rbampRoleN].i2c_addr = (uint8_t)CONFIG_ACROUTER_RBAMP_LOAD_ADDR;
                rbampRoles[rbampRoleN].role     = RBAMP_ROLE_LOAD;
                rbampRoleN++;
            }
            if (CONFIG_ACROUTER_RBAMP_VOLTAGE_ADDR != 0) {
                rbampRoles[rbampRoleN].i2c_addr = (uint8_t)CONFIG_ACROUTER_RBAMP_VOLTAGE_ADDR;
                rbampRoles[rbampRoleN].role     = RBAMP_ROLE_VOLTAGE;
                rbampRoleN++;
            }
            if (rbampRoleN > 0) {
                rbamp_source_configure(rbampRoles, rbampRoleN);
                ESP_LOGI(TAG, "rbAmp roles seeded from Kconfig (%u)", (unsigned)rbampRoleN);
            }
        }
        // Optional DRDY interrupt-driven polling (single critical module).
        // -1 (default) = fixed-cadence timer poll, correct for a fleet.
        rbamp_source_set_drdy_gpio(hwCfg.rbamp_drdy_gpio);
        if (rbamp_source_init(rbampBus) == ESP_OK) {
            // Start regardless of boot-time module count: the poll task tolerates
            // an empty fleet, so a module wired/powered slightly late still works
            // once autoscan (re-run on a future rescan) or commissioning adds it.
            rbamp_source_start(CONFIG_ACROUTER_RBAMP_POLL_MS);
            rbamp_source_module_cfg_t rbampCur[4];
            size_t rbampCurN = 0;
            rbamp_source_get_roles(rbampCur, 4, &rbampCurN);
            ESP_LOGI(TAG, "rbAmp source: %u module(s) found, %u role(s) mapped, polling started",
                     (unsigned)rbamp_source_alive_count(), (unsigned)rbampCurN);
        }
    }
#endif

#if CONFIG_ACROUTER_ESPNOW_SOURCE
    // ESP-NOW wireless measurement source (v2.0). Receives rbAmp data from
    // wireless nodes and feeds the Sensor Hub as ACROUTER_SOURCE_ESPNOW.
    // Requires WiFi up (Phase 1). Default off (Kconfig); open (unencrypted).
    ESP_LOGI(TAG, "Initializing ESP-NOW source...");
    if (esp_now_source_init() == ESP_OK) {
        esp_now_source_start();
        ESP_LOGI(TAG, "ESP-NOW source: started (open RX)");
    }
#endif

    // Sensor Hub — merges all measurement sources with priority
    // Must init AFTER event loop exists (WiFiManager creates it in Phase 1)
    // and BEFORE RouterController subscribes in Phase 4
    if (sensor_hub_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Sensor Hub!");
    } else {
        ESP_LOGI(TAG, "Sensor Hub initialized");
    }

    // Device registry (L2) — load persisted unified module inventory + push its
    // roles down to the drivers so the registry is the role source of truth.
    if (devreg_init() == ESP_OK) {
        ESP_LOGI(TAG, "Device registry initialized (%u entries)", (unsigned)devreg_count());
        // First boot (empty registry): run one quiescent I2C scan so /api/modules is
        // populated out of the box instead of empty until a manual rescan. A persisted
        // registry is kept as-is (non-destructive) — the user rescans on demand.
        if (devreg_count() == 0) {
            ESP_LOGI(TAG, "Registry empty — running initial I2C scan");
            devreg_scan_i2c(0);
            ESP_LOGI(TAG, "Registry scan complete: %u entries", (unsigned)devreg_count());
        }
        devreg_sync_roles();
    }

    return ESP_OK;
}

// ================================================================
// Phase 4: Control
// ================================================================
static esp_err_t init_phase4_control(void) {
    ESP_LOGI(TAG, "[Phase 4] Control...");

    RouterController& router = RouterController::getInstance();
    if (!router.begin(0)) {
        ESP_LOGE(TAG, "Failed to initialize RouterController!");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "RouterController initialized");

    // Subscribe to event bus (Sensor Hub merged updates from rbAmp/ESP-NOW)
    router.subscribeEvents();
    ESP_LOGI(TAG, "RouterController subscribed to event bus");

    // Refresh priority map with relay data
    if (relay_manager_is_initialized()) {
        router.refreshPriorityMap();
    }

    // Serial command processor
    SerialCommand& serialCmd = SerialCommand::getInstance();
    serialCmd.begin(&ConfigManager::getInstance(), &router);
    ESP_LOGI(TAG, "SerialCommand initialized");

    return ESP_OK;
}

// ================================================================
// Phase 5: Network Services
// ================================================================
static esp_err_t init_phase5_network(void) {
    ESP_LOGI(TAG, "[Phase 5] Network services...");

    // NTP
    NTPManager& ntpMgr = NTPManager::getInstance();
    if (!ntpMgr.begin("pool.ntp.org", "EET-2EEST,M3.5.0/3,M10.5.0/4", 0, 0)) {
        ESP_LOGE(TAG, "Failed to initialize NTPManager!");
    } else {
        ESP_LOGI(TAG, "NTPManager initialized");
    }

    // WebServer (HTTP/REST + WS) — gated for the C2 tiering profiles (docs/18).
#if CONFIG_ACROUTER_HTTP_SERVER
    WebServerManager& webServer = WebServerManager::getInstance();
    if (!webServer.begin(80, 81)) {
        ESP_LOGE(TAG, "Failed to initialize WebServerManager!");
    } else {
        ESP_LOGI(TAG, "WebServerManager initialized (HTTP:%d, WS:%d)",
                 webServer.getHttpPort(), webServer.getWsPort());
    }
#else
    ESP_LOGI(TAG, "HTTP server disabled at build time (ACROUTER_HTTP_SERVER=n)");
#endif

    // MQTT — gated (esp-mqtt + TLS are heavy on the C2; see docs/18).
#if CONFIG_ACROUTER_MQTT_CLIENT
    MQTTManager& mqttMgr = MQTTManager::getInstance();
    mqttMgr.begin(&RouterController::getInstance(),
                  &ConfigManager::getInstance());
    if (mqttMgr.loadConfig()) {
        ESP_LOGI(TAG, "MQTT config loaded (enabled=%s, broker=%s)",
                 mqttMgr.isEnabled() ? "yes" : "no",
                 mqttMgr.getConfig().broker);
    }
    ESP_LOGI(TAG, "MQTTManager initialized");
#else
    ESP_LOGI(TAG, "MQTT client disabled at build time (ACROUTER_MQTT_CLIENT=n)");
#endif

    // GitHub OTA Checker — gated with OTA (docs/18 §4: no OTA on the C2).
#if CONFIG_ACROUTER_OTA
    const esp_app_desc_t* app_desc = esp_app_get_description();
    GitHubOTAChecker& otaChecker = GitHubOTAChecker::getInstance();
    otaChecker.begin("jvshkv", "ACRouter", app_desc->version);
    ESP_LOGI(TAG, "GitHubOTAChecker initialized (current: %s)", app_desc->version);
#endif

    return ESP_OK;
}

// ================================================================
// Main Orchestrator
// ================================================================
esp_err_t system_init(void) {
    // Disable buzzer
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, HIGH);

    // Serial
    Serial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "AC Power Router Controller v2.0");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "========================================");

    // Run phases in order
    esp_err_t err;

    err = init_phase0_storage();
    if (err != ESP_OK) return err;

    err = init_phase1_buses();
    if (err != ESP_OK) return err;

    err = init_phase2_outputs();
    if (err != ESP_OK) return err;

    err = init_phase3_sensors();
    if (err != ESP_OK) return err;

    err = init_phase4_control();
    if (err != ESP_OK) return err;

    err = init_phase5_network();
    // Network is non-critical, continue even if it fails

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "System ready! Use serial commands.");
    ESP_LOGI(TAG, "========================================");

    return ESP_OK;
}

bool system_is_safe_mode(void) {
    return s_safe_mode;
}
