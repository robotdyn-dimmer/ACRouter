/**
 * @file RouterController.h
 * @brief Solar Router Controller - Main control algorithm
 *
 * Implements the Solar Router algorithm that automatically redirects
 * excess solar energy to a load (heater) instead of exporting to the grid.
 *
 * @section Algorithm
 * The controller uses a proportional algorithm to maintain P_grid close to 0:
 * - P_grid < 0 (EXPORT): Increase dimmer (redirect to load)
 * - P_grid > 0 (IMPORT): Decrease dimmer (reduce load)
 * - P_grid ≈ 0 (BALANCE): Hold current level
 *
 * @section MinimalConfig
 * Minimum required sensors for Solar Router mode:
 * - 1x Dimmer channel (output)
 * - 1x Current Grid sensor (to detect import/export)
 *
 * Optional sensors:
 * - Voltage sensor (for power calculation, protection)
 * - Current Solar (monitoring generation)
 * - Current Load (monitoring consumption)
 *
 * @author ACRouter Project
 * @date 2024
 */

#ifndef ROUTER_CONTROLLER_H
#define ROUTER_CONTROLLER_H

#include <Arduino.h>
#include "DimmerHAL.h"
#include "PowerMeterADC.h"

namespace RouterConfig {
    // Algorithm parameters
    constexpr float DEFAULT_CONTROL_GAIN = 200.0f;      // Proportional gain (higher = slower response)
    constexpr float DEFAULT_BALANCE_THRESHOLD = 10.0f;  // W, threshold for "balanced" state
    constexpr float MIN_CONTROL_GAIN = 10.0f;           // Minimum allowed gain
    constexpr float MAX_CONTROL_GAIN = 1000.0f;         // Maximum allowed gain

    // Dimmer limits
    constexpr uint8_t MIN_DIMMER_PERCENT = 0;           // Minimum dimmer level
    constexpr uint8_t MAX_DIMMER_PERCENT = 100;         // Maximum dimmer level

    // Update rate
    constexpr uint32_t UPDATE_INTERVAL_MS = 200;        // Matches PowerMeterADC callback interval
}

/**
 * @brief System operating modes
 */
enum class RouterMode : uint8_t {
    OFF = 0,        ///< Dimmer OFF (0%), no control
    AUTO,           ///< Solar Router - automatic control based on P_grid (grid+solar)
    ECO,            ///< Economic - avoid grid import, allow export
    OFFGRID,        ///< Offgrid - autonomous mode (solar/battery)
    MANUAL,         ///< Fixed dimmer level (user-defined)
    BOOST           ///< Forced 100% power
    // SCHEDULE     ///< Phase 2: Time-based control
};

/**
 * @brief Router operating state
 */
enum class RouterState : uint8_t {
    IDLE,           ///< No control action needed
    INCREASING,     ///< Increasing dimmer (exporting to grid)
    DECREASING,     ///< Decreasing dimmer (importing from grid)
    AT_MAXIMUM,     ///< Dimmer at maximum limit
    AT_MINIMUM,     ///< Dimmer at minimum limit
    ERROR           ///< Error state (missing sensors, etc.)
};

/**
 * @brief Router status information
 */
struct RouterStatus {
    RouterMode mode;                    ///< Current operating mode
    RouterState state;                  ///< Current control state
    uint8_t dimmer_percent;             ///< Current dimmer level (0-100%)
    float target_level;                 ///< Internal target level (float for smooth control)
    float power_grid;                   ///< Current grid power (W) - from CURRENT_GRID sensor
    float power_solar;                  ///< Current solar power (W) - from CURRENT_SOLAR sensor
    float power_load;                   ///< Current load power (W) - from CURRENT_LOAD sensor
    float control_gain;                 ///< Current control gain
    float balance_threshold;            ///< Current balance threshold (W)
    uint32_t last_update_ms;            ///< Timestamp of last update
    bool valid;                         ///< True if status is valid

    RouterStatus() :
        mode(RouterMode::OFF),
        state(RouterState::IDLE),
        dimmer_percent(0),
        target_level(0.0f),
        power_grid(0.0f),
        power_solar(0.0f),
        power_load(0.0f),
        control_gain(RouterConfig::DEFAULT_CONTROL_GAIN),
        balance_threshold(RouterConfig::DEFAULT_BALANCE_THRESHOLD),
        last_update_ms(0),
        valid(false)
    {}
};

/**
 * @brief Solar Router Controller
 *
 * Main controller class that implements the Solar Router algorithm.
 * Receives power measurements and controls the dimmer to maintain
 * grid power close to zero (minimize import/export).
 *
 * Usage Example:
 * @code
 * // Get instances
 * RouterController& router = RouterController::getInstance();
 * DimmerHAL& dimmer = DimmerHAL::getInstance();
 * PowerMeterADC& powerMeter = PowerMeterADC::getInstance();
 *
 * // Initialize
 * dimmer.begin();
 * router.begin(&dimmer);
 * router.setMode(RouterMode::AUTO);
 *
 * // In PowerMeterADC callback:
 * powerMeter.setResultsCallback([](const PowerMeterADC::Measurements& m, void* ctx) {
 *     RouterController& router = RouterController::getInstance();
 *     router.update(m);
 * }, nullptr);
 * @endcode
 */
class RouterController {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to the RouterController instance
     */
    static RouterController& getInstance();

    /**
     * @brief Initialize the router controller
     *
     * @param dimmer Pointer to DimmerHAL instance
     * @param channel Dimmer channel to control (default: CHANNEL_1)
     * @return true if initialization successful
     */
    bool begin(DimmerHAL* dimmer, DimmerChannel channel = DimmerChannel::CHANNEL_1);

    /**
     * @brief Main update function - call from PowerMeterADC callback
     *
     * Processes the power measurements and adjusts dimmer level
     * according to the current mode and algorithm.
     *
     * @param measurements Power measurements from PowerMeterADC
     */
    void update(const PowerMeterADC::Measurements& measurements);

    // === Mode Control ===

    /**
     * @brief Set operating mode
     * @param mode New operating mode
     */
    void setMode(RouterMode mode);

    /**
     * @brief Get current operating mode
     * @return Current mode
     */
    RouterMode getMode() const { return m_status.mode; }

    // === Manual Mode Control ===

    /**
     * @brief Set manual dimmer level (only effective in MANUAL mode)
     * @param percent Dimmer level (0-100%)
     */
    void setManualLevel(uint8_t percent);

    /**
     * @brief Get manual dimmer level setting
     * @return Manual level setting (0-100%)
     */
    uint8_t getManualLevel() const { return m_manual_level; }

    // === Algorithm Parameters ===

    /**
     * @brief Set control gain (proportional coefficient)
     *
     * Higher values = slower response, less oscillation
     * Lower values = faster response, more oscillation
     *
     * @param gain Control gain (10-1000, default: 200)
     */
    void setControlGain(float gain);

    /**
     * @brief Get current control gain
     * @return Current gain value
     */
    float getControlGain() const { return m_status.control_gain; }

    /**
     * @brief Set balance threshold
     *
     * Power values within this threshold are considered "balanced".
     *
     * @param threshold_watts Threshold in watts (default: 10W)
     */
    void setBalanceThreshold(float threshold_watts);

    /**
     * @brief Get current balance threshold
     * @return Threshold in watts
     */
    float getBalanceThreshold() const { return m_status.balance_threshold; }

    // === Status ===

    /**
     * @brief Get current router status
     * @return RouterStatus structure
     */
    const RouterStatus& getStatus() const { return m_status; }

    /**
     * @brief Get current dimmer level
     * @return Dimmer level (0-100%)
     */
    uint8_t getDimmerLevel() const { return m_status.dimmer_percent; }

    /**
     * @brief Check if controller is initialized
     * @return true if begin() was called successfully
     */
    bool isInitialized() const { return m_initialized; }

    // === Emergency ===

    /**
     * @brief Emergency stop - immediately turn off dimmer
     */
    void emergencyStop();

    // === Mode Validation ===

    /**
     * @brief Validate if mode is compatible with available sensors
     *
     * Checks sensor configuration and returns whether the requested mode
     * can operate with the current hardware setup.
     *
     * @param mode Mode to validate
     * @param has_grid true if CURRENT_GRID sensor is configured
     * @param has_solar true if CURRENT_SOLAR sensor is configured
     * @return true if mode is compatible with sensors, false otherwise
     */
    static bool validateMode(RouterMode mode, bool has_grid, bool has_solar);

    /**
     * @brief Get human-readable reason why mode validation failed
     *
     * @param mode Mode that failed validation
     * @param has_grid true if CURRENT_GRID sensor is configured
     * @param has_solar true if CURRENT_SOLAR sensor is configured
     * @return String describing why the mode is incompatible
     */
    static const char* getValidationFailureReason(RouterMode mode, bool has_grid, bool has_solar);

private:
    // Singleton
    RouterController();
    ~RouterController() = default;
    RouterController(const RouterController&) = delete;
    RouterController& operator=(const RouterController&) = delete;

    /**
     * @brief Process AUTO mode algorithm
     * @param power_grid Grid power in watts (+ import, - export)
     */
    void processAutoMode(float power_grid);

    /**
     * @brief Process ECO mode algorithm
     * @param power_grid Grid power in watts (+ import, - export)
     */
    void processEcoMode(float power_grid);

    /**
     * @brief Process OFFGRID mode algorithm
     * @param measurements Power measurements (uses solar power)
     */
    void processOffgridMode(const PowerMeterADC::Measurements& measurements);

    /**
     * @brief Apply dimmer level with clamping
     * @param level Target level (will be clamped to 0-100)
     */
    void applyDimmerLevel(float level);

    /**
     * @brief Update router state based on current conditions
     * @param power_grid Current grid power
     */
    void updateState(float power_grid);

    // Components
    DimmerHAL* m_dimmer;
    DimmerChannel m_channel;

    // Status
    RouterStatus m_status;
    bool m_initialized;

    // Manual mode setting
    uint8_t m_manual_level;

    // Internal target (float for smooth control)
    float m_target_level;
};

#endif // ROUTER_CONTROLLER_H
