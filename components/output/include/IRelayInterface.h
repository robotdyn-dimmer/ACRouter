/**
 * @file IRelayInterface.h
 * @brief Abstract interface for relay devices
 *
 * Extends IOutputInterface with relay-specific functionality:
 * - On/Off control
 * - Debounce protection (min on/off times)
 * - Active high/low configuration
 *
 * Implementations: GPIORelay, ESPNowRelay (future), BLERelay (future)
 */

#ifndef I_RELAY_INTERFACE_H
#define I_RELAY_INTERFACE_H

#include "IOutputInterface.h"

namespace ACRouter {

/**
 * @brief Relay-specific status information
 */
struct RelayStatus {
    uint8_t id;
    bool enabled;
    OutputState state;
    OutputMode mode;
    bool is_on;
    uint16_t nominal_power_w;
    int8_t current_sensor_id;
    uint16_t min_on_time_s;       // Minimum time to stay ON
    uint16_t min_off_time_s;      // Minimum time to stay OFF
    uint32_t last_switch_ms;      // Timestamp of last state change
    uint32_t on_duration_ms;      // How long it has been ON (if ON)
    bool debounce_active;         // True if waiting for debounce
    char name[16];
};

/**
 * @brief Abstract interface for relay devices
 */
class IRelayInterface : public IOutputInterface {
public:
    virtual ~IRelayInterface() = default;

    /**
     * @brief Turn relay ON
     * @param force Ignore debounce protection
     * @return true if successful (or pending due to debounce)
     */
    virtual bool turnOn(bool force = false) = 0;

    /**
     * @brief Turn relay OFF
     * @param force Ignore debounce protection
     * @return true if successful (or pending due to debounce)
     */
    virtual bool turnOffRelay(bool force = false) = 0;

    /**
     * @brief Toggle relay state
     * @param force Ignore debounce protection
     * @return true if successful
     */
    virtual bool toggle(bool force = false) = 0;

    /**
     * @brief Check if relay is ON
     */
    virtual bool isOn() const = 0;

    /**
     * @brief Check if debounce is active (cannot switch yet)
     */
    virtual bool isDebounceActive() const = 0;

    /**
     * @brief Get time until debounce expires (0 if not active)
     * @return Remaining time in seconds
     */
    virtual uint16_t getDebounceRemaining() const = 0;

    /**
     * @brief Set minimum ON time (debounce protection)
     * @param seconds Minimum seconds to stay ON after turning on
     */
    virtual void setMinOnTime(uint16_t seconds) = 0;

    /**
     * @brief Get minimum ON time
     */
    virtual uint16_t getMinOnTime() const = 0;

    /**
     * @brief Set minimum OFF time (debounce protection)
     * @param seconds Minimum seconds to stay OFF after turning off
     */
    virtual void setMinOffTime(uint16_t seconds) = 0;

    /**
     * @brief Get minimum OFF time
     */
    virtual uint16_t getMinOffTime() const = 0;

    /**
     * @brief Get how long the relay has been ON (if ON)
     * @return Duration in seconds, 0 if OFF
     */
    virtual uint32_t getOnDuration() const = 0;

    /**
     * @brief Set active high/low logic
     * @param active_high true = HIGH=ON, false = LOW=ON
     */
    virtual void setActiveHigh(bool active_high) = 0;

    /**
     * @brief Get active high/low logic
     */
    virtual bool isActiveHigh() const = 0;

    /**
     * @brief Get GPIO pin (for GPIO-based relays)
     * @return GPIO pin number, or -1 if not applicable
     */
    virtual int8_t getGpioPin() const = 0;

    /**
     * @brief Set GPIO pin (for GPIO-based relays)
     */
    virtual void setGpioPin(int8_t pin) = 0;

    /**
     * @brief Get full status structure
     */
    virtual RelayStatus getStatus() const = 0;

    /**
     * @brief Update internal state (call periodically for debounce handling)
     */
    virtual void update() = 0;

    /**
     * @brief Turn off implementation (uses force=true for emergency)
     */
    void turnOff() override {
        turnOffRelay(true);
    }
};

} // namespace ACRouter

#endif // I_RELAY_INTERFACE_H
