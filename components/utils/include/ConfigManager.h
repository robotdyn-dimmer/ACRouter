/**
 * @file ConfigManager.h
 * @brief Configuration Manager with NVS persistence
 *
 * Manages system configuration parameters with automatic NVS storage.
 * Parameters are loaded from NVS on startup, or use defaults if not set.
 * Can be configured via terminal commands for runtime adjustment.
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include "nvs_flash.h"
#include "nvs.h"

// ============================================================
// Configuration Parameter Keys (NVS keys, max 15 chars)
// ============================================================

namespace ConfigKeys {
    // Router parameters
    constexpr const char* ROUTER_MODE       = "router_mode";
    constexpr const char* CONTROL_GAIN      = "ctrl_gain";
    constexpr const char* BALANCE_THRESHOLD = "bal_thresh";
    constexpr const char* MANUAL_LEVEL      = "manual_lvl";
    constexpr const char* GRID_CURRENT_LIMIT = "grid_lim_a";

    // Sensor calibration
    constexpr const char* CURRENT_THRESHOLD = "curr_thresh";
    constexpr const char* POWER_THRESHOLD   = "pwr_thresh";

    // NVS namespace
    constexpr const char* NVS_NAMESPACE     = "acrouter";
}

// ============================================================
// Default Values
// ============================================================

namespace ConfigDefaults {
    constexpr uint8_t ROUTER_MODE           = 1;        // AUTO mode
    constexpr float CONTROL_GAIN            = 200.0f;   // Proportional gain
    constexpr float BALANCE_THRESHOLD       = 10.0f;    // Watts
    constexpr float GRID_CURRENT_LIMIT      = 16.0f;    // Amps (GRID_LIMIT mode cap)
    constexpr uint8_t MANUAL_LEVEL          = 0;        // 0%

    constexpr float CURRENT_THRESHOLD       = 1.0f;     // Minimum current (A)
    constexpr float POWER_THRESHOLD         = 5.0f;     // Minimum power (W)
}

// ============================================================
// Configuration Structure
// ============================================================

/**
 * @brief System configuration parameters
 */
struct SystemConfig {
    // Router parameters
    uint8_t router_mode;        ///< RouterMode enum value
    float control_gain;         ///< Proportional control gain
    float balance_threshold;    ///< Balance threshold in Watts
    uint8_t manual_level;       ///< Manual dimmer level (0-100%)
    float grid_current_limit;   ///< GRID_LIMIT mode cap (Amps)

    // Sensor calibration
    float current_threshold;    ///< Minimum current threshold (A)
    float power_threshold;      ///< Minimum power threshold (W)

    /**
     * @brief Initialize with default values
     */
    void setDefaults() {
        router_mode = ConfigDefaults::ROUTER_MODE;
        control_gain = ConfigDefaults::CONTROL_GAIN;
        balance_threshold = ConfigDefaults::BALANCE_THRESHOLD;
        manual_level = ConfigDefaults::MANUAL_LEVEL;
        grid_current_limit = ConfigDefaults::GRID_CURRENT_LIMIT;

        current_threshold = ConfigDefaults::CURRENT_THRESHOLD;
        power_threshold = ConfigDefaults::POWER_THRESHOLD;
    }
};

// ============================================================
// ConfigManager Class
// ============================================================

/**
 * @brief Configuration manager with NVS persistence
 *
 * Singleton class that manages system configuration.
 * Loads from NVS on init, saves individual parameters on change.
 */
class ConfigManager {
public:
    /**
     * @brief Get singleton instance
     */
    static ConfigManager& getInstance();

    // Prevent copying
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    /**
     * @brief Initialize NVS and load configuration
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Get current configuration (read-only)
     */
    const SystemConfig& getConfig() const { return m_config; }

    /**
     * @brief Get mutable configuration reference
     * @note Changes are NOT automatically saved. Call save*() methods.
     */
    SystemConfig& config() { return m_config; }

    // ============================================================
    // Getters (convenience methods)
    // ============================================================

    uint8_t getRouterMode() const { return m_config.router_mode; }
    float getControlGain() const { return m_config.control_gain; }
    float getGridCurrentLimit() const { return m_config.grid_current_limit; }
    float getBalanceThreshold() const { return m_config.balance_threshold; }
    uint8_t getManualLevel() const { return m_config.manual_level; }
    float getCurrentThreshold() const { return m_config.current_threshold; }
    float getPowerThreshold() const { return m_config.power_threshold; }

    // ============================================================
    // Setters with automatic NVS save
    // ============================================================

    bool setRouterMode(uint8_t mode);
    bool setControlGain(float gain);
    bool setGridCurrentLimit(float amps);
    bool setBalanceThreshold(float threshold);
    bool setManualLevel(uint8_t level);
    bool setCurrentThreshold(float threshold);
    bool setPowerThreshold(float threshold);

    // ============================================================
    // Bulk Operations
    // ============================================================

    /**
     * @brief Save all parameters to NVS
     * @return true if all saves successful
     */
    bool saveAll();

    /**
     * @brief Load all parameters from NVS (or use defaults)
     * @return true if successful
     */
    bool loadAll();

    /**
     * @brief Reset all parameters to defaults and save
     * @return true if successful
     */
    bool resetToDefaults();

    /**
     * @brief Print current configuration to Serial
     */
    void printConfig() const;

    /**
     * @brief Check if NVS is initialized
     */
    bool isInitialized() const { return m_initialized; }

private:
    ConfigManager();
    ~ConfigManager() = default;

    // NVS helpers
    bool openNVS(nvs_open_mode_t mode);
    void closeNVS();

    // Load/Save individual parameters
    bool loadU8(const char* key, uint8_t& value, uint8_t defaultValue);
    bool loadFloat(const char* key, float& value, float defaultValue);
    bool saveU8(const char* key, uint8_t value);
    bool saveFloat(const char* key, float value);

    // State
    SystemConfig m_config;
    nvs_handle_t m_nvs_handle;
    bool m_initialized;

    static const char* TAG;
};

#endif // CONFIG_MANAGER_H
