/**
 * @file ConfigManager.cpp
 * @brief Configuration Manager implementation
 */

#include "ConfigManager.h"
#include "esp_log.h"

const char* ConfigManager::TAG = "ConfigMgr";

// ============================================================
// Singleton Instance
// ============================================================

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================

ConfigManager::ConfigManager()
    : m_nvs_handle(0)
    , m_initialized(false)
{
    m_config.setDefaults();
}

// ============================================================
// Initialization
// ============================================================

bool ConfigManager::begin() {
    if (m_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    // Initialize NVS flash
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "NVS initialized");

    // Load configuration from NVS
    if (!loadAll()) {
        ESP_LOGW(TAG, "Failed to load some parameters, using defaults");
    }

    m_initialized = true;
    ESP_LOGI(TAG, "Configuration loaded");
    printConfig();

    return true;
}

// ============================================================
// NVS Helpers
// ============================================================

bool ConfigManager::openNVS(nvs_open_mode_t mode) {
    esp_err_t err = nvs_open(ConfigKeys::NVS_NAMESPACE, mode, &m_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void ConfigManager::closeNVS() {
    if (m_nvs_handle != 0) {
        nvs_close(m_nvs_handle);
        m_nvs_handle = 0;
    }
}

bool ConfigManager::loadU8(const char* key, uint8_t& value, uint8_t defaultValue) {
    esp_err_t err = nvs_get_u8(m_nvs_handle, key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value = defaultValue;
        ESP_LOGI(TAG, "  %s: not found, using default %u", key, defaultValue);
        return true;  // Not an error, just use default
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "  %s: read error %s", key, esp_err_to_name(err));
        value = defaultValue;
        return false;
    }
    ESP_LOGI(TAG, "  %s: loaded %u", key, value);
    return true;
}

bool ConfigManager::loadFloat(const char* key, float& value, float defaultValue) {
    // NVS doesn't have native float support, store as uint32_t (bit cast)
    uint32_t raw;
    esp_err_t err = nvs_get_u32(m_nvs_handle, key, &raw);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value = defaultValue;
        ESP_LOGI(TAG, "  %s: not found, using default %.2f", key, defaultValue);
        return true;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "  %s: read error %s", key, esp_err_to_name(err));
        value = defaultValue;
        return false;
    }
    // Bit cast uint32_t to float
    memcpy(&value, &raw, sizeof(float));
    ESP_LOGI(TAG, "  %s: loaded %.2f", key, value);
    return true;
}

bool ConfigManager::saveU8(const char* key, uint8_t value) {
    if (!openNVS(NVS_READWRITE)) return false;

    esp_err_t err = nvs_set_u8(m_nvs_handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save %s: %s", key, esp_err_to_name(err));
        closeNVS();
        return false;
    }

    err = nvs_commit(m_nvs_handle);
    closeNVS();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit %s: %s", key, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Saved %s = %u", key, value);
    return true;
}

bool ConfigManager::saveFloat(const char* key, float value) {
    if (!openNVS(NVS_READWRITE)) return false;

    // Bit cast float to uint32_t
    uint32_t raw;
    memcpy(&raw, &value, sizeof(float));

    esp_err_t err = nvs_set_u32(m_nvs_handle, key, raw);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save %s: %s", key, esp_err_to_name(err));
        closeNVS();
        return false;
    }

    err = nvs_commit(m_nvs_handle);
    closeNVS();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit %s: %s", key, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Saved %s = %.2f", key, value);
    return true;
}

// ============================================================
// Setters with Auto-Save
// ============================================================

bool ConfigManager::setRouterMode(uint8_t mode) {
    if (mode > 3) mode = 0;  // Clamp to valid range
    m_config.router_mode = mode;
    return saveU8(ConfigKeys::ROUTER_MODE, mode);
}

bool ConfigManager::setControlGain(float gain) {
    if (gain < 1.0f) gain = 1.0f;
    if (gain > 1000.0f) gain = 1000.0f;
    m_config.control_gain = gain;
    return saveFloat(ConfigKeys::CONTROL_GAIN, gain);
}

bool ConfigManager::setBalanceThreshold(float threshold) {
    if (threshold < 0.0f) threshold = 0.0f;
    if (threshold > 100.0f) threshold = 100.0f;
    m_config.balance_threshold = threshold;
    return saveFloat(ConfigKeys::BALANCE_THRESHOLD, threshold);
}

bool ConfigManager::setManualLevel(uint8_t level) {
    if (level > 100) level = 100;
    m_config.manual_level = level;
    return saveU8(ConfigKeys::MANUAL_LEVEL, level);
}

bool ConfigManager::setVoltageCoef(float coef) {
    if (coef < 1.0f) coef = 1.0f;
    if (coef > 1000.0f) coef = 1000.0f;
    m_config.voltage_coef = coef;
    return saveFloat(ConfigKeys::VOLTAGE_COEF, coef);
}

bool ConfigManager::setCurrentCoef(float coef) {
    if (coef < 1.0f) coef = 1.0f;
    if (coef > 200.0f) coef = 200.0f;
    m_config.current_coef = coef;
    return saveFloat(ConfigKeys::CURRENT_COEF, coef);
}

bool ConfigManager::setCurrentThreshold(float threshold) {
    if (threshold < 0.0f) threshold = 0.0f;
    if (threshold > 10.0f) threshold = 10.0f;
    m_config.current_threshold = threshold;
    return saveFloat(ConfigKeys::CURRENT_THRESHOLD, threshold);
}

bool ConfigManager::setPowerThreshold(float threshold) {
    if (threshold < 0.0f) threshold = 0.0f;
    if (threshold > 100.0f) threshold = 100.0f;
    m_config.power_threshold = threshold;
    return saveFloat(ConfigKeys::POWER_THRESHOLD, threshold);
}

// ============================================================
// Bulk Operations
// ============================================================

bool ConfigManager::loadAll() {
    if (!openNVS(NVS_READONLY)) {
        // NVS namespace doesn't exist yet - use defaults
        ESP_LOGI(TAG, "NVS namespace not found, using defaults");
        m_config.setDefaults();
        return true;
    }

    ESP_LOGI(TAG, "Loading configuration from NVS:");

    bool success = true;

    // Router parameters
    success &= loadU8(ConfigKeys::ROUTER_MODE, m_config.router_mode, ConfigDefaults::ROUTER_MODE);
    success &= loadFloat(ConfigKeys::CONTROL_GAIN, m_config.control_gain, ConfigDefaults::CONTROL_GAIN);
    success &= loadFloat(ConfigKeys::BALANCE_THRESHOLD, m_config.balance_threshold, ConfigDefaults::BALANCE_THRESHOLD);
    success &= loadU8(ConfigKeys::MANUAL_LEVEL, m_config.manual_level, ConfigDefaults::MANUAL_LEVEL);

    // Sensor calibration
    success &= loadFloat(ConfigKeys::VOLTAGE_COEF, m_config.voltage_coef, ConfigDefaults::VOLTAGE_COEF);
    success &= loadFloat(ConfigKeys::CURRENT_COEF, m_config.current_coef, ConfigDefaults::CURRENT_COEF);
    success &= loadFloat(ConfigKeys::CURRENT_THRESHOLD, m_config.current_threshold, ConfigDefaults::CURRENT_THRESHOLD);
    success &= loadFloat(ConfigKeys::POWER_THRESHOLD, m_config.power_threshold, ConfigDefaults::POWER_THRESHOLD);

    closeNVS();
    return success;
}

bool ConfigManager::saveAll() {
    ESP_LOGI(TAG, "Saving all configuration to NVS");

    bool success = true;

    success &= saveU8(ConfigKeys::ROUTER_MODE, m_config.router_mode);
    success &= saveFloat(ConfigKeys::CONTROL_GAIN, m_config.control_gain);
    success &= saveFloat(ConfigKeys::BALANCE_THRESHOLD, m_config.balance_threshold);
    success &= saveU8(ConfigKeys::MANUAL_LEVEL, m_config.manual_level);

    success &= saveFloat(ConfigKeys::VOLTAGE_COEF, m_config.voltage_coef);
    success &= saveFloat(ConfigKeys::CURRENT_COEF, m_config.current_coef);
    success &= saveFloat(ConfigKeys::CURRENT_THRESHOLD, m_config.current_threshold);
    success &= saveFloat(ConfigKeys::POWER_THRESHOLD, m_config.power_threshold);

    return success;
}

bool ConfigManager::resetToDefaults() {
    ESP_LOGW(TAG, "Resetting configuration to defaults");
    m_config.setDefaults();
    return saveAll();
}

// ============================================================
// Debug Output
// ============================================================

void ConfigManager::printConfig() const {
    const char* mode_names[] = {"OFF", "AUTO", "MANUAL", "BOOST"};

    ESP_LOGI(TAG, "=== Current Configuration ===");
    ESP_LOGI(TAG, "Router:");
    ESP_LOGI(TAG, "  mode:             %s (%u)", mode_names[m_config.router_mode & 0x03], m_config.router_mode);
    ESP_LOGI(TAG, "  control_gain:     %.1f", m_config.control_gain);
    ESP_LOGI(TAG, "  balance_threshold: %.1f W", m_config.balance_threshold);
    ESP_LOGI(TAG, "  manual_level:     %u%%", m_config.manual_level);
    ESP_LOGI(TAG, "Sensors:");
    ESP_LOGI(TAG, "  voltage_coef:     %.1f", m_config.voltage_coef);
    ESP_LOGI(TAG, "  current_coef:     %.1f A/V", m_config.current_coef);
    ESP_LOGI(TAG, "  current_threshold: %.2f A", m_config.current_threshold);
    ESP_LOGI(TAG, "  power_threshold:  %.1f W", m_config.power_threshold);
    ESP_LOGI(TAG, "=============================");
}
