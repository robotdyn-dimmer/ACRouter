/**
 * @file IOutputInterface.h
 * @brief Base interface for all output devices (dimmers, relays)
 *
 * Provides common interface for output control abstraction layer.
 * Supports future expansion: GPIO, ESP-NOW, BLE, etc.
 */

#ifndef I_OUTPUT_INTERFACE_H
#define I_OUTPUT_INTERFACE_H

#include <stdint.h>

namespace ACRouter {

/**
 * @brief Output device type enumeration
 */
enum class OutputType : uint8_t {
    UNKNOWN = 0,
    GPIO_DIMMER = 1,      // Local GPIO phase-cut dimmer
    GPIO_RELAY = 2,       // Local GPIO relay
    ESPNOW_DIMMER = 10,   // Remote ESP-NOW dimmer (future)
    ESPNOW_RELAY = 11,    // Remote ESP-NOW relay (future)
    BLE_DIMMER = 20,      // Remote BLE dimmer (future)
    BLE_RELAY = 21        // Remote BLE relay (future)
};

/**
 * @brief Output device state
 */
enum class OutputState : uint8_t {
    UNKNOWN = 0,
    OFF = 1,
    ON = 2,
    TRANSITIONING = 3,    // For dimmers during smooth transition
    ERROR = 4,
    DISCONNECTED = 5      // For remote devices
};

/**
 * @brief Output device mode
 */
enum class OutputMode : uint8_t {
    AUTO = 0,             // Controlled by RouterController
    MANUAL_ON = 1,        // Forced ON
    MANUAL_OFF = 2,       // Forced OFF
    SCHEDULE = 3          // Controlled by scheduler (future)
};

/**
 * @brief Base interface for all output devices
 */
class IOutputInterface {
public:
    virtual ~IOutputInterface() = default;

    /**
     * @brief Initialize the output device
     * @return true if initialization successful
     */
    virtual bool begin() = 0;

    /**
     * @brief Get the output type
     */
    virtual OutputType getType() const = 0;

    /**
     * @brief Get the device ID (0-based index)
     */
    virtual uint8_t getId() const = 0;

    /**
     * @brief Get the device name
     */
    virtual const char* getName() const = 0;

    /**
     * @brief Set the device name
     */
    virtual void setName(const char* name) = 0;

    /**
     * @brief Check if device is enabled
     */
    virtual bool isEnabled() const = 0;

    /**
     * @brief Enable or disable the device
     */
    virtual void setEnabled(bool enabled) = 0;

    /**
     * @brief Get current state
     */
    virtual OutputState getState() const = 0;

    /**
     * @brief Get current mode
     */
    virtual OutputMode getMode() const = 0;

    /**
     * @brief Set operating mode
     */
    virtual void setMode(OutputMode mode) = 0;

    /**
     * @brief Get nominal power rating in watts
     */
    virtual uint16_t getNominalPower() const = 0;

    /**
     * @brief Set nominal power rating
     */
    virtual void setNominalPower(uint16_t power_w) = 0;

    /**
     * @brief Get linked current sensor ID (-1 = not linked)
     */
    virtual int8_t getCurrentSensorId() const = 0;

    /**
     * @brief Link to a current sensor for feedback
     */
    virtual void setCurrentSensorId(int8_t sensor_id) = 0;

    /**
     * @brief Turn off the output (emergency or normal)
     */
    virtual void turnOff() = 0;

    /**
     * @brief Save configuration to NVS
     */
    virtual bool saveConfig() = 0;

    /**
     * @brief Load configuration from NVS
     */
    virtual bool loadConfig() = 0;
};

} // namespace ACRouter

#endif // I_OUTPUT_INTERFACE_H
