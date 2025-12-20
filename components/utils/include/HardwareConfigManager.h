/**
 * @file HardwareConfigManager.h
 * @brief Hardware Configuration Manager with NVS persistence
 *
 * Manages hardware configuration (GPIO pins, sensor types, dimmer/relay settings)
 * with automatic NVS storage. Allows full device configuration without recompilation.
 *
 * Features:
 * - ADC channel configuration (GPIO pins, sensor types, calibration)
 * - Dimmer channel configuration (GPIO pins, enable/disable)
 * - Relay configuration (GPIO pins, polarity, enable/disable)
 * - Zero-cross detector pin configuration
 * - NVS persistence (survives reboots)
 * - Validation (pin conflicts, valid GPIO, etc.)
 * - Factory defaults
 */

#ifndef HARDWARE_CONFIG_MANAGER_H
#define HARDWARE_CONFIG_MANAGER_H

#include <Arduino.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "SensorTypes.h"

// ============================================================
// Version Management
// ============================================================

// Current NVS data format version
// Increment this when changing NVS structure (adding/removing/changing fields)
// Version history:
//   1: Initial release (ZMPT107 hardcoded multiplier)
//   2: Added voltage_driver and nominal_vdc fields to ADCChannelConfig
constexpr uint16_t HW_CONFIG_VERSION = 2;

// ============================================================
// Configuration Keys (NVS keys, max 15 chars)
// ============================================================

namespace HardwareConfigKeys {
    // Version
    constexpr const char* CONFIG_VERSION    = "cfg_version";  // NVS data format version

    // ADC Channels (4 channels max)
    constexpr const char* ADC_CH0_GPIO      = "adc0_gpio";
    constexpr const char* ADC_CH0_TYPE      = "adc0_type";
    constexpr const char* ADC_CH0_MULT      = "adc0_mult";
    constexpr const char* ADC_CH0_OFFSET    = "adc0_offset";
    constexpr const char* ADC_CH0_ENABLED   = "adc0_en";
    constexpr const char* ADC_CH0_VDRV      = "adc0_vdrv";    // Voltage driver type
    constexpr const char* ADC_CH0_NVDC      = "adc0_nvdc";    // Nominal VDC
    constexpr const char* ADC_CH0_CDRV      = "adc0_cdrv";    // Current driver type

    constexpr const char* ADC_CH1_GPIO      = "adc1_gpio";
    constexpr const char* ADC_CH1_TYPE      = "adc1_type";
    constexpr const char* ADC_CH1_MULT      = "adc1_mult";
    constexpr const char* ADC_CH1_OFFSET    = "adc1_offset";
    constexpr const char* ADC_CH1_ENABLED   = "adc1_en";
    constexpr const char* ADC_CH1_VDRV      = "adc1_vdrv";
    constexpr const char* ADC_CH1_NVDC      = "adc1_nvdc";
    constexpr const char* ADC_CH1_CDRV      = "adc1_cdrv";    // Current driver type

    constexpr const char* ADC_CH2_GPIO      = "adc2_gpio";
    constexpr const char* ADC_CH2_TYPE      = "adc2_type";
    constexpr const char* ADC_CH2_MULT      = "adc2_mult";
    constexpr const char* ADC_CH2_OFFSET    = "adc2_offset";
    constexpr const char* ADC_CH2_ENABLED   = "adc2_en";
    constexpr const char* ADC_CH2_VDRV      = "adc2_vdrv";
    constexpr const char* ADC_CH2_NVDC      = "adc2_nvdc";
    constexpr const char* ADC_CH2_CDRV      = "adc2_cdrv";    // Current driver type

    constexpr const char* ADC_CH3_GPIO      = "adc3_gpio";
    constexpr const char* ADC_CH3_TYPE      = "adc3_type";
    constexpr const char* ADC_CH3_MULT      = "adc3_mult";
    constexpr const char* ADC_CH3_OFFSET    = "adc3_offset";
    constexpr const char* ADC_CH3_ENABLED   = "adc3_en";
    constexpr const char* ADC_CH3_VDRV      = "adc3_vdrv";
    constexpr const char* ADC_CH3_NVDC      = "adc3_nvdc";
    constexpr const char* ADC_CH3_CDRV      = "adc3_cdrv";    // Current driver type

    // Dimmer Configuration
    constexpr const char* DIMMER_CH1_GPIO   = "dim1_gpio";
    constexpr const char* DIMMER_CH1_EN     = "dim1_en";
    constexpr const char* DIMMER_CH2_GPIO   = "dim2_gpio";
    constexpr const char* DIMMER_CH2_EN     = "dim2_en";

    // Zero-Cross Detector
    constexpr const char* ZEROCROSS_GPIO    = "zc_gpio";
    constexpr const char* ZEROCROSS_EN      = "zc_en";

    // Relay Configuration
    constexpr const char* RELAY_CH1_GPIO    = "rel1_gpio";
    constexpr const char* RELAY_CH1_POL     = "rel1_pol";    // 0=active_low, 1=active_high
    constexpr const char* RELAY_CH1_EN      = "rel1_en";
    constexpr const char* RELAY_CH2_GPIO    = "rel2_gpio";
    constexpr const char* RELAY_CH2_POL     = "rel2_pol";
    constexpr const char* RELAY_CH2_EN      = "rel2_en";

    // LED Configuration
    constexpr const char* LED_STATUS_GPIO   = "led_st_gpio";
    constexpr const char* LED_LOAD_GPIO     = "led_ld_gpio";

    // NVS Namespace
    constexpr const char* NVS_NAMESPACE     = "hw_config";
}

// ============================================================
// Hardware Configuration Structures
// ============================================================

/**
 * @brief Dimmer channel configuration
 */
struct DimmerChannelConfig {
    uint8_t gpio;           ///< GPIO pin number
    bool enabled;           ///< Channel enabled

    DimmerChannelConfig() : gpio(0), enabled(false) {}
    DimmerChannelConfig(uint8_t pin, bool en = true) : gpio(pin), enabled(en) {}
};

/**
 * @brief Relay channel configuration
 */
struct RelayChannelConfig {
    uint8_t gpio;           ///< GPIO pin number
    bool active_high;       ///< true = active HIGH, false = active LOW
    bool enabled;           ///< Channel enabled

    RelayChannelConfig() : gpio(0), active_high(true), enabled(false) {}
    RelayChannelConfig(uint8_t pin, bool polarity = true, bool en = false)
        : gpio(pin), active_high(polarity), enabled(en) {}
};

/**
 * @brief Complete hardware configuration
 */
struct HardwareConfig {
    // ADC Channels (4 max for PowerMeterADC)
    ADCChannelConfig adc_channels[4];

    // Dimmer Channels
    DimmerChannelConfig dimmer_ch1;
    DimmerChannelConfig dimmer_ch2;

    // Zero-Cross Detector
    uint8_t zerocross_gpio;
    bool zerocross_enabled;

    // Relay Channels
    RelayChannelConfig relay_ch1;
    RelayChannelConfig relay_ch2;

    // LED Indicators
    uint8_t led_status_gpio;
    uint8_t led_load_gpio;

    /**
     * @brief Initialize with factory defaults
     */
    void setDefaults();

    /**
     * @brief Validate configuration (check for GPIO conflicts, valid pins, etc.)
     * @return true if configuration is valid
     */
    bool validate(String* error_msg = nullptr) const;
};

// ============================================================
// HardwareConfigManager Class
// ============================================================

/**
 * @brief Hardware configuration manager with NVS persistence
 *
 * Singleton class that manages hardware configuration.
 * Loads from NVS on init, saves individual parameters on change.
 *
 * Usage Example:
 * @code
 * HardwareConfigManager& hw = HardwareConfigManager::getInstance();
 * hw.begin();
 *
 * // Get current config
 * const HardwareConfig& config = hw.getConfig();
 *
 * // Modify ADC channel
 * ADCChannelConfig adc0 = config.adc_channels[0];
 * adc0.gpio = 35;
 * adc0.type = SensorType::VOLTAGE_AC;
 * hw.setADCChannel(0, adc0);  // Automatically saves to NVS
 *
 * // Validate and apply
 * String error;
 * if (!hw.validate(&error)) {
 *     Serial.println("Invalid config: " + error);
 * }
 * @endcode
 */
class HardwareConfigManager {
public:
    /**
     * @brief Get singleton instance
     */
    static HardwareConfigManager& getInstance();

    // Prevent copying
    HardwareConfigManager(const HardwareConfigManager&) = delete;
    HardwareConfigManager& operator=(const HardwareConfigManager&) = delete;

    /**
     * @brief Initialize NVS and load configuration
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Get current configuration (read-only)
     */
    const HardwareConfig& getConfig() const { return m_config; }

    /**
     * @brief Get mutable configuration reference
     * @warning Changes are NOT automatically saved. Use set*() methods or call saveAll()
     */
    HardwareConfig& config() { return m_config; }

    // ============================================================
    // ADC Channel Configuration
    // ============================================================

    /**
     * @brief Set ADC channel configuration
     * @param channel Channel index (0-3)
     * @param config Channel configuration
     * @return true if saved successfully
     */
    bool setADCChannel(uint8_t channel, const ADCChannelConfig& config);

    /**
     * @brief Get ADC channel configuration
     * @param channel Channel index (0-3)
     * @return Channel configuration (or default if invalid index)
     */
    ADCChannelConfig getADCChannel(uint8_t channel) const;

    // ============================================================
    // Dimmer Configuration
    // ============================================================

    /**
     * @brief Set dimmer channel configuration
     * @param channel Channel number (1 or 2)
     * @param config Dimmer configuration
     * @return true if saved successfully
     */
    bool setDimmerChannel(uint8_t channel, const DimmerChannelConfig& config);

    /**
     * @brief Get dimmer channel configuration
     * @param channel Channel number (1 or 2)
     */
    DimmerChannelConfig getDimmerChannel(uint8_t channel) const;

    // ============================================================
    // Zero-Cross Configuration
    // ============================================================

    /**
     * @brief Set zero-cross detector pin
     * @param gpio GPIO pin number
     * @param enabled Enable zero-cross detection
     * @return true if saved successfully
     */
    bool setZeroCross(uint8_t gpio, bool enabled = true);

    // ============================================================
    // Relay Configuration
    // ============================================================

    /**
     * @brief Set relay channel configuration
     * @param channel Channel number (1 or 2)
     * @param config Relay configuration
     * @return true if saved successfully
     */
    bool setRelayChannel(uint8_t channel, const RelayChannelConfig& config);

    /**
     * @brief Get relay channel configuration
     * @param channel Channel number (1 or 2)
     */
    RelayChannelConfig getRelayChannel(uint8_t channel) const;

    // ============================================================
    // LED Configuration
    // ============================================================

    /**
     * @brief Set status LED GPIO
     * @param gpio GPIO pin number
     * @return true if saved successfully
     */
    bool setStatusLED(uint8_t gpio);

    /**
     * @brief Set load LED GPIO
     * @param gpio GPIO pin number
     * @return true if saved successfully
     */
    bool setLoadLED(uint8_t gpio);

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
     * @brief Reset all parameters to factory defaults and save
     * @return true if successful
     */
    bool resetToDefaults();

    /**
     * @brief Validate current configuration
     * @param error_msg Optional pointer to receive error message
     * @return true if configuration is valid
     */
    bool validate(String* error_msg = nullptr) const;

    /**
     * @brief Print current configuration to Serial
     */
    void printConfig() const;

    /**
     * @brief Check if NVS is initialized
     */
    bool isInitialized() const { return m_initialized; }

    // ============================================================
    // Version Management
    // ============================================================

    /**
     * @brief Get NVS data format version stored in NVS
     * @return Version number (0 if not found)
     */
    uint16_t getNVSVersion() const;

    /**
     * @brief Get current firmware's NVS data format version
     * @return Version number
     */
    uint16_t getCurrentVersion() const { return HW_CONFIG_VERSION; }

    /**
     * @brief Check if system is in safe mode (version mismatch)
     * @return true if in safe mode
     */
    bool isSafeMode() const { return m_safe_mode; }

    /**
     * @brief Get safe mode reason
     * @return Description of why safe mode was activated
     */
    String getSafeModeReason() const { return m_safe_mode_reason; }

    /**
     * @brief Erase all NVS data and reset to factory defaults
     * @warning This will delete all stored configuration!
     * @return true if successful
     */
    bool eraseAndReset();

private:
    HardwareConfigManager();
    ~HardwareConfigManager() = default;

    // NVS helpers
    bool openNVS(nvs_open_mode_t mode);
    void closeNVS();

    // Load/Save individual parameters
    bool loadU8(const char* key, uint8_t& value, uint8_t defaultValue);
    bool loadFloat(const char* key, float& value, float defaultValue);
    bool loadBool(const char* key, bool& value, bool defaultValue);
    bool saveU8(const char* key, uint8_t value);
    bool saveFloat(const char* key, float value);
    bool saveBool(const char* key, bool value);

    // ADC Channel save/load
    bool saveADCChannel(uint8_t channel);
    bool loadADCChannel(uint8_t channel);

    // Version helpers
    bool loadU16(const char* key, uint16_t& value, uint16_t defaultValue);
    bool saveU16(const char* key, uint16_t value);

    // State
    HardwareConfig m_config;
    nvs_handle_t m_nvs_handle;
    bool m_initialized;
    bool m_safe_mode;           ///< Safe mode flag (version mismatch)
    String m_safe_mode_reason;  ///< Reason for safe mode

    static const char* TAG;
};

#endif // HARDWARE_CONFIG_MANAGER_H
