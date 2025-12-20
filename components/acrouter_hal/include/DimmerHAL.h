/**
 * @file DimmerHAL.h
 * @brief Hardware Abstraction Layer for AC Dimmer Control
 *
 * Provides high-level interface for controlling AC dimmer channels using
 * the RBDimmer library. Manages TRIAC-based phase-cut dimming with
 * zero-crossing detection.
 *
 * @section Features
 * - Dual-channel dimmer control (2 independent loads)
 * - Zero-crossing detection for clean AC waveform cutting
 * - Automatic mains frequency detection (50/60 Hz)
 * - Smooth power level transitions
 * - Thread-safe operations
 * - RMS power curve compensation
 *
 * @section Hardware
 * - Zero-Cross: GPIO 18
 * - Dimmer 1: GPIO 19 (Phase 1)
 * - Dimmer 2: GPIO 23 (Phase 2)
 *
 * @author ACRouter Project
 * @date 2024
 */

#ifndef DIMMER_HAL_H
#define DIMMER_HAL_H

#include <Arduino.h>
#include "rbdimmerESP32.h"
#include "PinDefinitions.h"
#include "DataTypes.h"

namespace DimmerConfig {
    // Hardware Configuration
    constexpr uint8_t PHASE_NUM = 0;                    // Single phase system
    constexpr uint16_t MAINS_FREQUENCY = 0;             // 0 = auto-detect (50/60 Hz)

    // Dimmer Channels
    constexpr uint8_t MAX_CHANNELS = 2;                 // Two dimmer outputs

    // Power Limits (percent)
    constexpr uint8_t MIN_POWER_PERCENT = 0;            // Minimum power level
    constexpr uint8_t MAX_POWER_PERCENT = 100;          // Maximum power level
    constexpr uint8_t DEFAULT_POWER_PERCENT = 0;        // Start with dimmers off

    // Transition Defaults
    constexpr uint32_t DEFAULT_TRANSITION_MS = 500;     // Default smooth transition time
    constexpr uint32_t MAX_TRANSITION_MS = 5000;        // Maximum transition time

    // Safety
    constexpr uint32_t INIT_STABILIZATION_MS = 100;     // Time to wait after init
}

/**
 * @brief Dimmer channel identifier
 */
enum class DimmerChannel : uint8_t {
    CHANNEL_1 = 0,      ///< First dimmer channel (GPIO 19)
    CHANNEL_2 = 1       ///< Second dimmer channel (GPIO 23)
};

/**
 * @brief Dimmer power curve types
 */
enum class DimmerCurve : uint8_t {
    LINEAR = RBDIMMER_CURVE_LINEAR,         ///< Linear power curve
    RMS = RBDIMMER_CURVE_RMS,               ///< RMS-compensated (recommended for resistive loads)
    LOGARITHMIC = RBDIMMER_CURVE_LOGARITHMIC ///< Logarithmic (recommended for LED loads)
};

/**
 * @brief Dimmer channel status information
 */
struct DimmerStatus {
    bool initialized;           ///< True if dimmer is initialized
    bool active;                ///< True if dimmer is actively controlling output
    uint8_t power_percent;      ///< Current power level (0-100%)
    uint8_t target_percent;     ///< Target power level during transition
    DimmerCurve curve;          ///< Current power curve type

    DimmerStatus() :
        initialized(false),
        active(false),
        power_percent(0),
        target_percent(0),
        curve(DimmerCurve::RMS)
    {}
};

/**
 * @brief AC Dimmer Hardware Abstraction Layer
 *
 * Singleton class that manages AC dimmer control using the RBDimmer library.
 * Provides thread-safe control of up to 2 dimmer channels with zero-crossing
 * detection and smooth power transitions.
 *
 * @note This is a singleton - use getInstance() to access the instance.
 *
 * Usage Example:
 * @code
 * // Initialize dimmer system
 * DimmerHAL& dimmer = DimmerHAL::getInstance();
 * if (!dimmer.begin()) {
 *     Serial.println("Failed to initialize dimmer");
 *     return;
 * }
 *
 * // Set dimmer 1 to 50% power
 * dimmer.setPower(DimmerChannel::CHANNEL_1, 50);
 *
 * // Smooth transition to 75% over 1 second
 * dimmer.setPowerSmooth(DimmerChannel::CHANNEL_1, 75, 1000);
 *
 * // Get current status
 * DimmerStatus status = dimmer.getStatus(DimmerChannel::CHANNEL_1);
 * Serial.printf("Power: %d%%\n", status.power_percent);
 * @endcode
 */
class DimmerHAL {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to the DimmerHAL instance
     */
    static DimmerHAL& getInstance();

    /**
     * @brief Initialize dimmer control system
     *
     * Initializes the RBDimmer library, registers zero-crossing detector,
     * and creates dimmer channels with default configuration.
     *
     * @param curve Power curve type to use (default: RMS for resistive loads)
     * @return true if initialization successful, false otherwise
     */
    bool begin(DimmerCurve curve = DimmerCurve::RMS);

    /**
     * @brief Set immediate power level for a channel
     *
     * Sets the dimmer power level immediately without transition.
     * Power level is clamped to valid range (0-100%).
     *
     * @param channel Dimmer channel to control
     * @param power_percent Power level in percent (0-100)
     * @return true if successful, false otherwise
     */
    bool setPower(DimmerChannel channel, uint8_t power_percent);

    /**
     * @brief Set power level with smooth transition
     *
     * Changes dimmer power level gradually over specified time period.
     * Provides smooth, flicker-free transitions between power levels.
     *
     * @param channel Dimmer channel to control
     * @param power_percent Target power level in percent (0-100)
     * @param transition_ms Transition duration in milliseconds
     * @return true if successful, false otherwise
     */
    bool setPowerSmooth(DimmerChannel channel, uint8_t power_percent,
                        uint32_t transition_ms = DimmerConfig::DEFAULT_TRANSITION_MS);

    /**
     * @brief Get current power level for a channel
     *
     * @param channel Dimmer channel to query
     * @return Current power level in percent (0-100), or 0 if channel invalid
     */
    uint8_t getPower(DimmerChannel channel) const;

    /**
     * @brief Get detailed status for a channel
     *
     * @param channel Dimmer channel to query
     * @return DimmerStatus structure with channel information
     */
    DimmerStatus getStatus(DimmerChannel channel) const;

    /**
     * @brief Check if dimmer system is initialized
     *
     * @return true if begin() was called successfully
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Get detected mains frequency
     *
     * @return Detected frequency in Hz (50 or 60), or 0 if not yet detected
     */
    uint16_t getMainsFrequency() const;

    /**
     * @brief Turn off all dimmers immediately
     *
     * Emergency stop - turns off all dimmer channels instantly.
     * Useful for safety shutdowns.
     *
     * @return true if successful, false otherwise
     */
    bool allOff();

    /**
     * @brief Change power curve type for a channel
     *
     * Switches between Linear, RMS, and Logarithmic curves.
     * Use RMS for incandescent/resistive loads, Logarithmic for LEDs.
     *
     * @param channel Dimmer channel to configure
     * @param curve New curve type
     * @return true if successful, false otherwise
     */
    bool setCurve(DimmerChannel channel, DimmerCurve curve);

private:
    // Singleton: Private constructor and deleted copy operations
    DimmerHAL();
    ~DimmerHAL() = default;
    DimmerHAL(const DimmerHAL&) = delete;
    DimmerHAL& operator=(const DimmerHAL&) = delete;

    /**
     * @brief Validate channel index
     * @param channel Channel to validate
     * @return true if channel is valid
     */
    bool isValidChannel(DimmerChannel channel) const;

    /**
     * @brief Clamp power value to valid range
     * @param power_percent Input power value
     * @return Clamped value (0-100)
     */
    uint8_t clampPower(uint8_t power_percent) const;

    // RBDimmer channel handles
    rbdimmer_channel_t* m_channels[DimmerConfig::MAX_CHANNELS];

    // Status tracking
    DimmerStatus m_status[DimmerConfig::MAX_CHANNELS];
    bool m_initialized;
    DimmerCurve m_default_curve;

    // GPIO pin mapping
    static constexpr uint8_t CHANNEL_PINS[DimmerConfig::MAX_CHANNELS] = {
        PIN_DIMMER_1,  // Channel 0 -> GPIO 19
        PIN_DIMMER_2   // Channel 1 -> GPIO 23
    };
};

#endif // DIMMER_HAL_H
