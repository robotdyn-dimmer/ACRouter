/**
 * @file HardwareConfigManager.cpp
 * @brief Hardware Configuration Manager Implementation
 */

#include "HardwareConfigManager.h"
#include "PinDefinitions.h"
#include "esp_log.h"
#include <set>

const char* HardwareConfigManager::TAG = "HwConfig";

// ============================================================
// Factory Defaults
// ============================================================

void HardwareConfig::setDefaults() {
    // ADC Channel 0: Voltage sensor (GPIO35)
    adc_channels[0] = ADCChannelConfig(
        35,
        SensorType::VOLTAGE_AC,
        SensorCalibration::ZMPT107_MULTIPLIER,
        SensorCalibration::ZMPT107_OFFSET,
        true
    );

    // ADC Channel 1: Load current (GPIO39)
    adc_channels[1] = ADCChannelConfig(
        39,
        SensorType::CURRENT_LOAD,
        SensorCalibration::SCT013_030_MULTIPLIER,
        SensorCalibration::SCT013_030_OFFSET,
        true
    );

    // ADC Channel 2: Grid current (GPIO36)
    adc_channels[2] = ADCChannelConfig(
        36,
        SensorType::CURRENT_GRID,
        SensorCalibration::SCT013_030_MULTIPLIER,
        SensorCalibration::SCT013_030_OFFSET,
        true
    );

    // ADC Channel 3: Solar current (GPIO34)
    adc_channels[3] = ADCChannelConfig(
        34,
        SensorType::CURRENT_SOLAR,
        SensorCalibration::SCT013_030_MULTIPLIER,
        SensorCalibration::SCT013_030_OFFSET,
        true
    );

    // Dimmer channels
    dimmer_ch1 = DimmerChannelConfig(PIN_DIMMER_1, true);
    dimmer_ch2 = DimmerChannelConfig(PIN_DIMMER_2, false);  // Phase 2

    // Zero-cross detector
    zerocross_gpio = PIN_ZEROCROSS;
    zerocross_enabled = true;

    // Relays (Phase 2)
    relay_ch1 = RelayChannelConfig(PIN_RELAY_1, true, false);
    relay_ch2 = RelayChannelConfig(PIN_RELAY_2, true, false);

    // LED indicators
    led_status_gpio = PIN_LED_STATUS;
    led_load_gpio = PIN_LED_LOAD;
}

// ============================================================
// Configuration Validation
// ============================================================

bool HardwareConfig::validate(String* error_msg) const {
    std::set<uint8_t> used_pins;

    // Helper lambda to check pin conflicts
    auto checkPin = [&](uint8_t pin, const char* name) -> bool {
        if (pin == 0) return true;  // Pin 0 means disabled

        if (used_pins.count(pin)) {
            if (error_msg) {
                *error_msg = String("GPIO") + String(pin) + " used multiple times (" + String(name) + ")";
            }
            return false;
        }
        used_pins.insert(pin);
        return true;
    };

    // Helper lambda to validate ADC1 pins
    auto isValidADC1 = [](uint8_t pin) -> bool {
        return (pin == 32 || pin == 33 || pin == 34 || pin == 35 ||
                pin == 36 || pin == 37 || pin == 38 || pin == 39);
    };

    // Validate ADC channels
    for (int i = 0; i < 4; i++) {
        const ADCChannelConfig& ch = adc_channels[i];

        if (!ch.enabled || ch.type == SensorType::NONE) {
            continue;  // Skip disabled channels
        }

        // Check if GPIO is valid for ADC1
        if (!isValidADC1(ch.gpio)) {
            if (error_msg) {
                *error_msg = String("ADC") + String(i) + ": GPIO" + String(ch.gpio) + " is not valid for ADC1";
            }
            return false;
        }

        // Check for duplicates
        if (!checkPin(ch.gpio, (String("ADC") + String(i)).c_str())) {
            return false;
        }
    }

    // Validate dimmer channels
    if (dimmer_ch1.enabled) {
        if (!checkPin(dimmer_ch1.gpio, "Dimmer 1")) return false;
        if (IS_INPUT_ONLY_PIN(dimmer_ch1.gpio)) {
            if (error_msg) {
                *error_msg = String("Dimmer 1: GPIO") + String(dimmer_ch1.gpio) + " is input-only";
            }
            return false;
        }
    }

    if (dimmer_ch2.enabled) {
        if (!checkPin(dimmer_ch2.gpio, "Dimmer 2")) return false;
        if (IS_INPUT_ONLY_PIN(dimmer_ch2.gpio)) {
            if (error_msg) {
                *error_msg = String("Dimmer 2: GPIO") + String(dimmer_ch2.gpio) + " is input-only";
            }
            return false;
        }
    }

    // Validate zero-cross
    if (zerocross_enabled) {
        if (!checkPin(zerocross_gpio, "Zero-cross")) return false;
    }

    // Validate relays
    if (relay_ch1.enabled) {
        if (!checkPin(relay_ch1.gpio, "Relay 1")) return false;
        if (IS_INPUT_ONLY_PIN(relay_ch1.gpio)) {
            if (error_msg) {
                *error_msg = String("Relay 1: GPIO") + String(relay_ch1.gpio) + " is input-only";
            }
            return false;
        }
    }

    if (relay_ch2.enabled) {
        if (!checkPin(relay_ch2.gpio, "Relay 2")) return false;
        if (IS_INPUT_ONLY_PIN(relay_ch2.gpio)) {
            if (error_msg) {
                *error_msg = String("Relay 2: GPIO") + String(relay_ch2.gpio) + " is input-only";
            }
            return false;
        }
    }

    // Validate LEDs
    if (led_status_gpio > 0) {
        if (!checkPin(led_status_gpio, "Status LED")) return false;
    }

    if (led_load_gpio > 0) {
        if (!checkPin(led_load_gpio, "Load LED")) return false;
    }

    // All checks passed
    return true;
}

// ============================================================
// Singleton Instance
// ============================================================

HardwareConfigManager& HardwareConfigManager::getInstance() {
    static HardwareConfigManager instance;
    return instance;
}

HardwareConfigManager::HardwareConfigManager()
    : m_nvs_handle(0)
    , m_initialized(false)
    , m_safe_mode(false)
    , m_safe_mode_reason("")
{
    // Initialize with factory defaults
    m_config.setDefaults();
}

// ============================================================
// Initialization
// ============================================================

bool HardwareConfigManager::begin() {
    if (m_initialized) {
        // Note: ESP_LOGW removed - causes crash during early init
        return true;
    }

    // Initialize NVS if not already done
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Note: ESP_LOGW removed - causes crash during early init
        // NVS partition was truncated and needs to be erased - let main() handle it
    }

    // Check if hw_config namespace exists by trying to open it
    nvs_handle_t test_handle;
    err = nvs_open(HardwareConfigKeys::NVS_NAMESPACE, NVS_READONLY, &test_handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Namespace doesn't exist - this is first boot
        ESP_LOGI(TAG, "NVS namespace not found - entering SAFE MODE for first boot");
        // Note: Don't call nvs_close() here - handle is invalid when ESP_ERR_NVS_NOT_FOUND

        // IMPORTANT: Enter safe mode on first boot to prevent potential crashes
        // User must configure hardware via commands and reboot
        m_safe_mode = true;
        m_safe_mode_reason = "First boot - NVS namespace not found. " +
                             String("Use 'hw-erase-nvs' or configure sensors via commands, then reboot.");

        // Note: ESP_LOGW removed - causes crash during early init with Flash string literals
        // Safe mode active - PowerMeterADC will not be initialized

        // Set factory defaults in memory (but don't save yet)
        m_config.setDefaults();
    } else if (err == ESP_OK) {
        // Namespace exists - close test handle and check version
        nvs_close(test_handle);

        // Get stored version
        uint16_t nvs_version = getNVSVersion();
        ESP_LOGI(TAG, "NVS version: %d, Firmware version: %d", nvs_version, HW_CONFIG_VERSION);

        if (nvs_version == 0) {
            // No version stored (old firmware) - assume version 1
            nvs_version = 1;
            // Note: ESP_LOGW removed - causes crash during early init
        }

        if (nvs_version != HW_CONFIG_VERSION) {
            // Version mismatch - enter safe mode
            m_safe_mode = true;
            m_safe_mode_reason = "NVS version mismatch (NVS: " + String(nvs_version) +
                                 ", Firmware: " + String(HW_CONFIG_VERSION) + "). " +
                                 "Use 'hw-erase-nvs' command to reset or update manually.";

            // Note: ESP_LOGW removed - causes crash during early init with Flash string literals
            // Safe mode active due to version mismatch

            // Use factory defaults in memory
            m_config.setDefaults();
        } else {
            // Version matches - load configuration normally
            m_safe_mode = false;

            if (!loadAll()) {
                // Note: ESP_LOGW removed - causes crash during early init
                m_config.setDefaults();
            }
        }
    } else {
        // Other error
        ESP_LOGE(TAG, "Error checking NVS namespace: %s", esp_err_to_name(err));
        m_config.setDefaults();
        m_safe_mode = false;
    }

    m_initialized = true;
    ESP_LOGI(TAG, "Initialized %s", m_safe_mode ? "(SAFE MODE)" : "");

    return true;
}

// ============================================================
// NVS Helpers
// ============================================================

bool HardwareConfigManager::openNVS(nvs_open_mode_t mode) {
    esp_err_t err = nvs_open(HardwareConfigKeys::NVS_NAMESPACE, mode, &m_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void HardwareConfigManager::closeNVS() {
    if (m_nvs_handle) {
        nvs_close(m_nvs_handle);
        m_nvs_handle = 0;
    }
}

bool HardwareConfigManager::loadU8(const char* key, uint8_t& value, uint8_t defaultValue) {
    if (!openNVS(NVS_READONLY)) {
        value = defaultValue;
        return false;
    }

    esp_err_t err = nvs_get_u8(m_nvs_handle, key, &value);
    closeNVS();

    if (err != ESP_OK) {
        value = defaultValue;
        return false;
    }

    return true;
}

bool HardwareConfigManager::loadFloat(const char* key, float& value, float defaultValue) {
    if (!openNVS(NVS_READONLY)) {
        value = defaultValue;
        return false;
    }

    // NVS doesn't support float directly, store as uint32_t
    uint32_t temp;
    esp_err_t err = nvs_get_u32(m_nvs_handle, key, &temp);
    closeNVS();

    if (err != ESP_OK) {
        value = defaultValue;
        return false;
    }

    memcpy(&value, &temp, sizeof(float));
    return true;
}

bool HardwareConfigManager::loadBool(const char* key, bool& value, bool defaultValue) {
    uint8_t temp;
    if (!loadU8(key, temp, defaultValue ? 1 : 0)) {
        value = defaultValue;
        return false;
    }
    value = (temp != 0);
    return true;
}

bool HardwareConfigManager::saveU8(const char* key, uint8_t value) {
    if (!openNVS(NVS_READWRITE)) {
        return false;
    }

    esp_err_t err = nvs_set_u8(m_nvs_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(m_nvs_handle);
    }

    closeNVS();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving %s: %s", key, esp_err_to_name(err));
        return false;
    }

    return true;
}

bool HardwareConfigManager::saveFloat(const char* key, float value) {
    if (!openNVS(NVS_READWRITE)) {
        return false;
    }

    // NVS doesn't support float directly, store as uint32_t
    uint32_t temp;
    memcpy(&temp, &value, sizeof(float));

    esp_err_t err = nvs_set_u32(m_nvs_handle, key, temp);
    if (err == ESP_OK) {
        err = nvs_commit(m_nvs_handle);
    }

    closeNVS();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving %s: %s", key, esp_err_to_name(err));
        return false;
    }

    return true;
}

bool HardwareConfigManager::saveBool(const char* key, bool value) {
    return saveU8(key, value ? 1 : 0);
}

bool HardwareConfigManager::loadU16(const char* key, uint16_t& value, uint16_t defaultValue) {
    if (!openNVS(NVS_READONLY)) {
        value = defaultValue;
        return false;
    }

    esp_err_t err = nvs_get_u16(m_nvs_handle, key, &value);
    closeNVS();

    if (err != ESP_OK) {
        value = defaultValue;
        return false;
    }

    return true;
}

bool HardwareConfigManager::saveU16(const char* key, uint16_t value) {
    if (!openNVS(NVS_READWRITE)) {
        return false;
    }

    esp_err_t err = nvs_set_u16(m_nvs_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(m_nvs_handle);
    }

    closeNVS();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving %s: %s", key, esp_err_to_name(err));
        return false;
    }

    return true;
}

// ============================================================
// ADC Channel Configuration
// ============================================================

bool HardwareConfigManager::saveADCChannel(uint8_t channel) {
    if (channel >= 4) {
        ESP_LOGE(TAG, "Invalid ADC channel: %d", channel);
        return false;
    }

    const ADCChannelConfig& ch = m_config.adc_channels[channel];

    // Determine keys based on channel
    const char* gpio_key;
    const char* type_key;
    const char* mult_key;
    const char* offset_key;
    const char* enabled_key;
    const char* vdrv_key;
    const char* nvdc_key;
    const char* cdrv_key;

    switch (channel) {
        case 0:
            gpio_key = HardwareConfigKeys::ADC_CH0_GPIO;
            type_key = HardwareConfigKeys::ADC_CH0_TYPE;
            mult_key = HardwareConfigKeys::ADC_CH0_MULT;
            offset_key = HardwareConfigKeys::ADC_CH0_OFFSET;
            enabled_key = HardwareConfigKeys::ADC_CH0_ENABLED;
            vdrv_key = HardwareConfigKeys::ADC_CH0_VDRV;
            nvdc_key = HardwareConfigKeys::ADC_CH0_NVDC;
            cdrv_key = HardwareConfigKeys::ADC_CH0_CDRV;
            break;
        case 1:
            gpio_key = HardwareConfigKeys::ADC_CH1_GPIO;
            type_key = HardwareConfigKeys::ADC_CH1_TYPE;
            mult_key = HardwareConfigKeys::ADC_CH1_MULT;
            offset_key = HardwareConfigKeys::ADC_CH1_OFFSET;
            enabled_key = HardwareConfigKeys::ADC_CH1_ENABLED;
            vdrv_key = HardwareConfigKeys::ADC_CH1_VDRV;
            nvdc_key = HardwareConfigKeys::ADC_CH1_NVDC;
            cdrv_key = HardwareConfigKeys::ADC_CH1_CDRV;
            break;
        case 2:
            gpio_key = HardwareConfigKeys::ADC_CH2_GPIO;
            type_key = HardwareConfigKeys::ADC_CH2_TYPE;
            mult_key = HardwareConfigKeys::ADC_CH2_MULT;
            offset_key = HardwareConfigKeys::ADC_CH2_OFFSET;
            enabled_key = HardwareConfigKeys::ADC_CH2_ENABLED;
            vdrv_key = HardwareConfigKeys::ADC_CH2_VDRV;
            nvdc_key = HardwareConfigKeys::ADC_CH2_NVDC;
            cdrv_key = HardwareConfigKeys::ADC_CH2_CDRV;
            break;
        case 3:
            gpio_key = HardwareConfigKeys::ADC_CH3_GPIO;
            type_key = HardwareConfigKeys::ADC_CH3_TYPE;
            mult_key = HardwareConfigKeys::ADC_CH3_MULT;
            offset_key = HardwareConfigKeys::ADC_CH3_OFFSET;
            enabled_key = HardwareConfigKeys::ADC_CH3_ENABLED;
            vdrv_key = HardwareConfigKeys::ADC_CH3_VDRV;
            nvdc_key = HardwareConfigKeys::ADC_CH3_NVDC;
            cdrv_key = HardwareConfigKeys::ADC_CH3_CDRV;
            break;
        default:
            return false;
    }

    // Save all fields
    bool success = true;
    success &= saveU8(gpio_key, ch.gpio);
    success &= saveU8(type_key, static_cast<uint8_t>(ch.type));
    success &= saveFloat(mult_key, ch.multiplier);
    success &= saveFloat(offset_key, ch.offset);
    success &= saveBool(enabled_key, ch.enabled);
    success &= saveU8(vdrv_key, static_cast<uint8_t>(ch.voltage_driver));
    success &= saveFloat(nvdc_key, ch.nominal_vdc);
    success &= saveU8(cdrv_key, static_cast<uint8_t>(ch.current_driver));

    return success;
}

bool HardwareConfigManager::loadADCChannel(uint8_t channel) {
    if (channel >= 4) {
        return false;
    }

    ADCChannelConfig& ch = m_config.adc_channels[channel];
    ADCChannelConfig defaults = DefaultADCConfig::getStandardConfig(channel);

    // Initialize with defaults first (important for new fields not in old NVS)
    ch = defaults;

    // Determine keys based on channel
    const char* gpio_key;
    const char* type_key;
    const char* mult_key;
    const char* offset_key;
    const char* enabled_key;
    const char* vdrv_key;
    const char* nvdc_key;
    const char* cdrv_key;

    switch (channel) {
        case 0:
            gpio_key = HardwareConfigKeys::ADC_CH0_GPIO;
            type_key = HardwareConfigKeys::ADC_CH0_TYPE;
            mult_key = HardwareConfigKeys::ADC_CH0_MULT;
            offset_key = HardwareConfigKeys::ADC_CH0_OFFSET;
            enabled_key = HardwareConfigKeys::ADC_CH0_ENABLED;
            vdrv_key = HardwareConfigKeys::ADC_CH0_VDRV;
            nvdc_key = HardwareConfigKeys::ADC_CH0_NVDC;
            cdrv_key = HardwareConfigKeys::ADC_CH0_CDRV;
            break;
        case 1:
            gpio_key = HardwareConfigKeys::ADC_CH1_GPIO;
            type_key = HardwareConfigKeys::ADC_CH1_TYPE;
            mult_key = HardwareConfigKeys::ADC_CH1_MULT;
            offset_key = HardwareConfigKeys::ADC_CH1_OFFSET;
            enabled_key = HardwareConfigKeys::ADC_CH1_ENABLED;
            vdrv_key = HardwareConfigKeys::ADC_CH1_VDRV;
            nvdc_key = HardwareConfigKeys::ADC_CH1_NVDC;
            cdrv_key = HardwareConfigKeys::ADC_CH1_CDRV;
            break;
        case 2:
            gpio_key = HardwareConfigKeys::ADC_CH2_GPIO;
            type_key = HardwareConfigKeys::ADC_CH2_TYPE;
            mult_key = HardwareConfigKeys::ADC_CH2_MULT;
            offset_key = HardwareConfigKeys::ADC_CH2_OFFSET;
            enabled_key = HardwareConfigKeys::ADC_CH2_ENABLED;
            vdrv_key = HardwareConfigKeys::ADC_CH2_VDRV;
            nvdc_key = HardwareConfigKeys::ADC_CH2_NVDC;
            cdrv_key = HardwareConfigKeys::ADC_CH2_CDRV;
            break;
        case 3:
            gpio_key = HardwareConfigKeys::ADC_CH3_GPIO;
            type_key = HardwareConfigKeys::ADC_CH3_TYPE;
            mult_key = HardwareConfigKeys::ADC_CH3_MULT;
            offset_key = HardwareConfigKeys::ADC_CH3_OFFSET;
            enabled_key = HardwareConfigKeys::ADC_CH3_ENABLED;
            vdrv_key = HardwareConfigKeys::ADC_CH3_VDRV;
            nvdc_key = HardwareConfigKeys::ADC_CH3_NVDC;
            cdrv_key = HardwareConfigKeys::ADC_CH3_CDRV;
            break;
        default:
            return false;
    }

    // Load all fields
    loadU8(gpio_key, ch.gpio, defaults.gpio);

    uint8_t type_u8;
    loadU8(type_key, type_u8, static_cast<uint8_t>(defaults.type));
    ch.type = static_cast<SensorType>(type_u8);

    loadFloat(mult_key, ch.multiplier, defaults.multiplier);
    loadFloat(offset_key, ch.offset, defaults.offset);
    loadBool(enabled_key, ch.enabled, defaults.enabled);

    uint8_t vdrv_u8;
    loadU8(vdrv_key, vdrv_u8, static_cast<uint8_t>(defaults.voltage_driver));
    ch.voltage_driver = static_cast<VoltageSensorDriver>(vdrv_u8);

    loadFloat(nvdc_key, ch.nominal_vdc, defaults.nominal_vdc);

    uint8_t cdrv_u8;
    loadU8(cdrv_key, cdrv_u8, static_cast<uint8_t>(defaults.current_driver));
    ch.current_driver = static_cast<CurrentSensorDriver>(cdrv_u8);

    return true;
}

bool HardwareConfigManager::setADCChannel(uint8_t channel, const ADCChannelConfig& config) {
    if (channel >= 4) {
        ESP_LOGE(TAG, "Invalid ADC channel: %d", channel);
        return false;
    }

    m_config.adc_channels[channel] = config;
    return saveADCChannel(channel);
}

ADCChannelConfig HardwareConfigManager::getADCChannel(uint8_t channel) const {
    if (channel >= 4) {
        return ADCChannelConfig();  // Return disabled channel
    }
    return m_config.adc_channels[channel];
}

// ============================================================
// Dimmer Configuration
// ============================================================

bool HardwareConfigManager::setDimmerChannel(uint8_t channel, const DimmerChannelConfig& config) {
    if (channel == 1) {
        m_config.dimmer_ch1 = config;
        saveU8(HardwareConfigKeys::DIMMER_CH1_GPIO, config.gpio);
        saveBool(HardwareConfigKeys::DIMMER_CH1_EN, config.enabled);
        return true;
    } else if (channel == 2) {
        m_config.dimmer_ch2 = config;
        saveU8(HardwareConfigKeys::DIMMER_CH2_GPIO, config.gpio);
        saveBool(HardwareConfigKeys::DIMMER_CH2_EN, config.enabled);
        return true;
    }

    ESP_LOGE(TAG, "Invalid dimmer channel: %d", channel);
    return false;
}

DimmerChannelConfig HardwareConfigManager::getDimmerChannel(uint8_t channel) const {
    if (channel == 1) {
        return m_config.dimmer_ch1;
    } else if (channel == 2) {
        return m_config.dimmer_ch2;
    }
    return DimmerChannelConfig();  // Return disabled
}

// ============================================================
// Zero-Cross Configuration
// ============================================================

bool HardwareConfigManager::setZeroCross(uint8_t gpio, bool enabled) {
    m_config.zerocross_gpio = gpio;
    m_config.zerocross_enabled = enabled;

    saveU8(HardwareConfigKeys::ZEROCROSS_GPIO, gpio);
    saveBool(HardwareConfigKeys::ZEROCROSS_EN, enabled);

    return true;
}

// ============================================================
// Relay Configuration
// ============================================================

bool HardwareConfigManager::setRelayChannel(uint8_t channel, const RelayChannelConfig& config) {
    if (channel == 1) {
        m_config.relay_ch1 = config;
        saveU8(HardwareConfigKeys::RELAY_CH1_GPIO, config.gpio);
        saveBool(HardwareConfigKeys::RELAY_CH1_POL, config.active_high);
        saveBool(HardwareConfigKeys::RELAY_CH1_EN, config.enabled);
        return true;
    } else if (channel == 2) {
        m_config.relay_ch2 = config;
        saveU8(HardwareConfigKeys::RELAY_CH2_GPIO, config.gpio);
        saveBool(HardwareConfigKeys::RELAY_CH2_POL, config.active_high);
        saveBool(HardwareConfigKeys::RELAY_CH2_EN, config.enabled);
        return true;
    }

    ESP_LOGE(TAG, "Invalid relay channel: %d", channel);
    return false;
}

RelayChannelConfig HardwareConfigManager::getRelayChannel(uint8_t channel) const {
    if (channel == 1) {
        return m_config.relay_ch1;
    } else if (channel == 2) {
        return m_config.relay_ch2;
    }
    return RelayChannelConfig();  // Return disabled
}

// ============================================================
// LED Configuration
// ============================================================

bool HardwareConfigManager::setStatusLED(uint8_t gpio) {
    m_config.led_status_gpio = gpio;
    return saveU8(HardwareConfigKeys::LED_STATUS_GPIO, gpio);
}

bool HardwareConfigManager::setLoadLED(uint8_t gpio) {
    m_config.led_load_gpio = gpio;
    return saveU8(HardwareConfigKeys::LED_LOAD_GPIO, gpio);
}

// ============================================================
// Bulk Operations
// ============================================================

bool HardwareConfigManager::saveAll() {
    bool success = true;

    // Save version first
    success &= saveU16(HardwareConfigKeys::CONFIG_VERSION, HW_CONFIG_VERSION);

    // Save ADC channels
    for (int i = 0; i < 4; i++) {
        success &= saveADCChannel(i);
    }

    // Save dimmer channels
    success &= saveU8(HardwareConfigKeys::DIMMER_CH1_GPIO, m_config.dimmer_ch1.gpio);
    success &= saveBool(HardwareConfigKeys::DIMMER_CH1_EN, m_config.dimmer_ch1.enabled);
    success &= saveU8(HardwareConfigKeys::DIMMER_CH2_GPIO, m_config.dimmer_ch2.gpio);
    success &= saveBool(HardwareConfigKeys::DIMMER_CH2_EN, m_config.dimmer_ch2.enabled);

    // Save zero-cross
    success &= saveU8(HardwareConfigKeys::ZEROCROSS_GPIO, m_config.zerocross_gpio);
    success &= saveBool(HardwareConfigKeys::ZEROCROSS_EN, m_config.zerocross_enabled);

    // Save relays
    success &= saveU8(HardwareConfigKeys::RELAY_CH1_GPIO, m_config.relay_ch1.gpio);
    success &= saveBool(HardwareConfigKeys::RELAY_CH1_POL, m_config.relay_ch1.active_high);
    success &= saveBool(HardwareConfigKeys::RELAY_CH1_EN, m_config.relay_ch1.enabled);
    success &= saveU8(HardwareConfigKeys::RELAY_CH2_GPIO, m_config.relay_ch2.gpio);
    success &= saveBool(HardwareConfigKeys::RELAY_CH2_POL, m_config.relay_ch2.active_high);
    success &= saveBool(HardwareConfigKeys::RELAY_CH2_EN, m_config.relay_ch2.enabled);

    // Save LEDs
    success &= saveU8(HardwareConfigKeys::LED_STATUS_GPIO, m_config.led_status_gpio);
    success &= saveU8(HardwareConfigKeys::LED_LOAD_GPIO, m_config.led_load_gpio);

    if (success) {
        ESP_LOGI(TAG, "All configuration saved to NVS");
    } else {
        ESP_LOGW(TAG, "Some configuration parameters failed to save");
    }

    return success;
}

bool HardwareConfigManager::loadAll() {
    // Load ADC channels
    for (int i = 0; i < 4; i++) {
        loadADCChannel(i);
    }

    // Load dimmer channels
    loadU8(HardwareConfigKeys::DIMMER_CH1_GPIO, m_config.dimmer_ch1.gpio, PIN_DIMMER_1);
    loadBool(HardwareConfigKeys::DIMMER_CH1_EN, m_config.dimmer_ch1.enabled, true);
    loadU8(HardwareConfigKeys::DIMMER_CH2_GPIO, m_config.dimmer_ch2.gpio, PIN_DIMMER_2);
    loadBool(HardwareConfigKeys::DIMMER_CH2_EN, m_config.dimmer_ch2.enabled, false);

    // Load zero-cross
    loadU8(HardwareConfigKeys::ZEROCROSS_GPIO, m_config.zerocross_gpio, PIN_ZEROCROSS);
    loadBool(HardwareConfigKeys::ZEROCROSS_EN, m_config.zerocross_enabled, true);

    // Load relays
    loadU8(HardwareConfigKeys::RELAY_CH1_GPIO, m_config.relay_ch1.gpio, PIN_RELAY_1);
    loadBool(HardwareConfigKeys::RELAY_CH1_POL, m_config.relay_ch1.active_high, true);
    loadBool(HardwareConfigKeys::RELAY_CH1_EN, m_config.relay_ch1.enabled, false);
    loadU8(HardwareConfigKeys::RELAY_CH2_GPIO, m_config.relay_ch2.gpio, PIN_RELAY_2);
    loadBool(HardwareConfigKeys::RELAY_CH2_POL, m_config.relay_ch2.active_high, true);
    loadBool(HardwareConfigKeys::RELAY_CH2_EN, m_config.relay_ch2.enabled, false);

    // Load LEDs
    loadU8(HardwareConfigKeys::LED_STATUS_GPIO, m_config.led_status_gpio, PIN_LED_STATUS);
    loadU8(HardwareConfigKeys::LED_LOAD_GPIO, m_config.led_load_gpio, PIN_LED_LOAD);

    ESP_LOGI(TAG, "Configuration loaded from NVS");
    return true;
}

bool HardwareConfigManager::resetToDefaults() {
    ESP_LOGI(TAG, "Resetting to factory defaults");

    m_config.setDefaults();

    return saveAll();
}

bool HardwareConfigManager::validate(String* error_msg) const {
    return m_config.validate(error_msg);
}

void HardwareConfigManager::printConfig() const {
    ESP_LOGI(TAG, "=== Hardware Configuration ===");

    // ADC Channels
    ESP_LOGI(TAG, "ADC Channels:");
    for (int i = 0; i < 4; i++) {
        const ADCChannelConfig& ch = m_config.adc_channels[i];
        ESP_LOGI(TAG, "  CH%d: GPIO%-2d  Type: %-15s  Mult: %.2f  Offset: %.2f  %s",
                      i, ch.gpio, sensorTypeToString(ch.type),
                      ch.multiplier, ch.offset,
                      ch.enabled ? "ENABLED" : "DISABLED");
    }

    // Dimmer Channels
    ESP_LOGI(TAG, "Dimmer Channels:");
    ESP_LOGI(TAG, "  CH1: GPIO%-2d  %s", m_config.dimmer_ch1.gpio,
                  m_config.dimmer_ch1.enabled ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "  CH2: GPIO%-2d  %s", m_config.dimmer_ch2.gpio,
                  m_config.dimmer_ch2.enabled ? "ENABLED" : "DISABLED");

    // Zero-Cross
    ESP_LOGI(TAG, "Zero-Cross Detector:");
    ESP_LOGI(TAG, "  GPIO: %d  %s", m_config.zerocross_gpio,
                  m_config.zerocross_enabled ? "ENABLED" : "DISABLED");

    // Relay Channels
    ESP_LOGI(TAG, "Relay Channels:");
    ESP_LOGI(TAG, "  CH1: GPIO%-2d  %s  %s", m_config.relay_ch1.gpio,
                  m_config.relay_ch1.active_high ? "ACTIVE_HIGH" : "ACTIVE_LOW",
                  m_config.relay_ch1.enabled ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "  CH2: GPIO%-2d  %s  %s", m_config.relay_ch2.gpio,
                  m_config.relay_ch2.active_high ? "ACTIVE_HIGH" : "ACTIVE_LOW",
                  m_config.relay_ch2.enabled ? "ENABLED" : "DISABLED");

    // LEDs
    ESP_LOGI(TAG, "LED Indicators:");
    ESP_LOGI(TAG, "  Status LED: GPIO%d", m_config.led_status_gpio);
    ESP_LOGI(TAG, "  Load LED:   GPIO%d", m_config.led_load_gpio);

    ESP_LOGI(TAG, "=============================");
}

// ============================================================
// Version Management
// ============================================================

uint16_t HardwareConfigManager::getNVSVersion() const {
    uint16_t version = 0;

    // Use const_cast to call non-const openNVS (safe because we're in const method)
    HardwareConfigManager* self = const_cast<HardwareConfigManager*>(this);

    if (!self->openNVS(NVS_READONLY)) {
        return 0;
    }

    esp_err_t err = nvs_get_u16(self->m_nvs_handle, HardwareConfigKeys::CONFIG_VERSION, &version);
    self->closeNVS();

    if (err != ESP_OK) {
        return 0;  // Version not found
    }

    return version;
}

bool HardwareConfigManager::eraseAndReset() {
    ESP_LOGW(TAG, "Erasing NVS namespace '%s'...", HardwareConfigKeys::NVS_NAMESPACE);

    // Open NVS in readwrite mode
    if (!openNVS(NVS_READWRITE)) {
        ESP_LOGE(TAG, "Failed to open NVS for erase");
        return false;
    }

    // Erase all keys in namespace
    esp_err_t err = nvs_erase_all(m_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(err));
        closeNVS();
        return false;
    }

    // Commit erase
    err = nvs_commit(m_nvs_handle);
    closeNVS();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS erase: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "NVS namespace erased successfully");

    // Reset to factory defaults
    m_config.setDefaults();
    m_safe_mode = false;
    m_safe_mode_reason = "";

    // Save factory defaults with current version
    if (!saveAll()) {
        ESP_LOGE(TAG, "Failed to save factory defaults after erase");
        return false;
    }

    ESP_LOGI(TAG, "Factory defaults saved (version %d)", HW_CONFIG_VERSION);
    return true;
}
