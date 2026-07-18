/**
 * @file GPIORelay.h
 * @brief GPIO-based relay implementation
 *
 * Implements IRelayInterface for local GPIO relays.
 * Features:
 * - On/Off control
 * - Debounce protection (min on/off times)
 * - Active high/low configuration
 */

#ifndef GPIO_RELAY_H
#define GPIO_RELAY_H

#include "IRelayInterface.h"
#include <Arduino.h>

namespace ACRouter {

// Forward declaration
class RelayManager;

/**
 * @brief GPIO configuration for relay
 */
struct GPIORelayConfig {
    uint8_t id;
    int8_t gpio_pin;
    bool enabled;
    bool active_high;             // true = HIGH=ON, false = LOW=ON
    char name[16];
    uint16_t nominal_power_w;
    int8_t current_sensor_id;
    uint16_t min_on_time_s;       // Minimum time to stay ON (debounce)
    uint16_t min_off_time_s;      // Minimum time to stay OFF (debounce)
    OutputMode mode;

    GPIORelayConfig() :
        id(0),
        gpio_pin(-1),
        enabled(false),
        active_high(true),
        nominal_power_w(0),
        current_sensor_id(-1),
        min_on_time_s(60),        // Default: 1 minute
        min_off_time_s(60),       // Default: 1 minute
        mode(OutputMode::AUTO)
    {
        name[0] = '\0';
    }
};

/**
 * @brief GPIO-based relay implementation
 */
class GPIORelay : public IRelayInterface {
public:
    /**
     * @brief Constructor
     * @param id Relay ID (0-3)
     */
    explicit GPIORelay(uint8_t id);
    ~GPIORelay() override;

    // Prevent copying
    GPIORelay(const GPIORelay&) = delete;
    GPIORelay& operator=(const GPIORelay&) = delete;

    // ========== IOutputInterface implementation ==========

    bool begin() override;
    OutputType getType() const override { return OutputType::GPIO_RELAY; }
    uint8_t getId() const override { return m_config.id; }
    const char* getName() const override { return m_config.name; }
    void setName(const char* name) override;
    bool isEnabled() const override { return m_config.enabled; }
    void setEnabled(bool enabled) override;
    OutputState getState() const override;
    OutputMode getMode() const override { return m_config.mode; }
    void setMode(OutputMode mode) override;
    uint16_t getNominalPower() const override { return m_config.nominal_power_w; }
    void setNominalPower(uint16_t power_w) override { m_config.nominal_power_w = power_w; }
    int8_t getCurrentSensorId() const override { return m_config.current_sensor_id; }
    void setCurrentSensorId(int8_t sensor_id) override { m_config.current_sensor_id = sensor_id; }
    bool saveConfig() override;
    bool loadConfig() override;

    // ========== IRelayInterface implementation ==========

    bool turnOn(bool force = false) override;
    bool turnOffRelay(bool force = false) override;
    bool toggle(bool force = false) override;
    bool isOn() const override { return m_is_on; }
    bool isDebounceActive() const override;
    uint16_t getDebounceRemaining() const override;
    void setMinOnTime(uint16_t seconds) override { m_config.min_on_time_s = seconds; }
    uint16_t getMinOnTime() const override { return m_config.min_on_time_s; }
    void setMinOffTime(uint16_t seconds) override { m_config.min_off_time_s = seconds; }
    uint16_t getMinOffTime() const override { return m_config.min_off_time_s; }
    uint32_t getOnDuration() const override;
    void setActiveHigh(bool active_high) override;
    bool isActiveHigh() const override { return m_config.active_high; }
    int8_t getGpioPin() const override { return m_config.gpio_pin; }
    void setGpioPin(int8_t pin) override;
    RelayStatus getStatus() const override;
    void update() override;

    // ========== Additional methods ==========

    /**
     * @brief Set full configuration
     */
    void setConfig(const GPIORelayConfig& config);

    /**
     * @brief Get current configuration
     */
    const GPIORelayConfig& getConfig() const { return m_config; }

    /**
     * @brief Check if hardware is initialized
     */
    bool isInitialized() const { return m_initialized; }

private:
    GPIORelayConfig m_config;
    bool m_is_on;
    bool m_initialized;
    uint32_t m_last_switch_ms;    // Timestamp of last state change
    bool m_pending_on;            // Pending turn ON after debounce
    bool m_pending_off;           // Pending turn OFF after debounce

    /**
     * @brief Apply physical GPIO state based on m_is_on and active_high
     */
    void applyGpioState();

    /**
     * @brief Check if can switch based on debounce timing
     */
    bool canSwitch() const;

    /**
     * @brief Get NVS key prefix for this relay
     */
    String getNvsPrefix() const;
};

} // namespace ACRouter

#endif // GPIO_RELAY_H
