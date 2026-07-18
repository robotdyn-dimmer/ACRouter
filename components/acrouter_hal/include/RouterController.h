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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "acrouter_events.h"

// Use new dimmer manager (pure C API)
extern "C" {
#include "dimmer_manager.h"
#include "relay_manager.h"
}

namespace RouterConfig {
    // Algorithm parameters
    constexpr float DEFAULT_CONTROL_GAIN = 200.0f;      // Proportional gain (higher = slower response)
    constexpr float DEFAULT_BALANCE_THRESHOLD = 10.0f;  // W, threshold for "balanced" state
    constexpr float MIN_CONTROL_GAIN = 10.0f;           // Minimum allowed gain
    constexpr float MAX_CONTROL_GAIN = 1000.0f;         // Maximum allowed gain

    // GRID_LIMIT mode (current-magnitude cap, no voltage/solar)
    constexpr float DEFAULT_GRID_CURRENT_LIMIT_A = 16.0f; // A, default cap (typical 3.6kW @230V breaker)
    constexpr float GRID_LIMIT_DEADBAND_A = 0.3f;         // A, hold band around the limit
    constexpr float GRID_LIMIT_GAIN = 2.0f;               // A per %-step; higher = slower ramp
    constexpr float MAX_GRID_CURRENT_LIMIT_A = 100.0f;    // A, config sanity ceiling

    // Dimmer limits
    constexpr uint8_t MIN_DIMMER_PERCENT = 0;           // Minimum dimmer level
    constexpr uint8_t MAX_DIMMER_PERCENT = 100;         // Maximum dimmer level

    // Update rate
    constexpr uint32_t UPDATE_INTERVAL_MS = 200;        // Sensor Hub merged-update interval (5 Hz)
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
    BOOST,          ///< Forced 100% power
    GRID_LIMIT      ///< Cap grid draw at a CURRENT limit (A). Current-only, no U, no
                    ///< solar, no export — uses grid current MAGNITUDE (sign not needed).
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
 * @brief Device type for priority management
 */
enum class DeviceType : uint8_t {
    DIMMER = 0,     ///< Dimmer device
    RELAY           ///< Relay device
};

/**
 * @brief Device reference for priority-based control
 */
struct DeviceRef {
    DeviceType type;        ///< Device type (dimmer or relay)
    uint8_t id;             ///< Device ID
    uint16_t power_w;       ///< Nominal power in watts
    float target_level;     ///< Target level (0.0-100.0 for dimmers, 0/100 for relays)

    DeviceRef() : type(DeviceType::DIMMER), id(0), power_w(0), target_level(0.0f) {}
    DeviceRef(DeviceType t, uint8_t device_id, uint16_t pwr)
        : type(t), id(device_id), power_w(pwr), target_level(0.0f) {}
};

/**
 * @brief Priority level containing multiple devices
 */
struct PriorityLevel {
    uint8_t priority;                   ///< Priority value (0-255, 0=highest)
    DeviceType device_type;             ///< Type of devices at this level (all same type)
    DeviceRef* devices;                 ///< Pointer to device array (allocated)
    uint8_t device_count;               ///< Number of devices at this priority
    uint8_t device_capacity;            ///< Allocated capacity
    uint32_t total_power_w;             ///< Sum of all nominal powers

    PriorityLevel() : priority(255), device_type(DeviceType::DIMMER), devices(nullptr),
                      device_count(0), device_capacity(0), total_power_w(0) {}
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
 *
 * // Initialize dimmer manager first
 * dimmer_manager_init();
 * dimmer_gpio_init(PIN_ZERO_CROSS);
 *
 * // Initialize router with dimmer ID
 * router.begin(0);  // Use dimmer ID 0
 * router.setMode(RouterMode::AUTO);
 *
 * // Measurements arrive via the event bus (Sensor Hub -> ACROUTER_EVENT_MERGED_UPDATE);
 * // RouterController subscribes and calls update(const acrouter_measurements_t&).
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
     * @param dimmer_id Dimmer ID to control (0-63)
     * @return true if initialization successful
     */
    bool begin(uint8_t dimmer_id = 0);

    /**
     * @brief Main update function - call from event bus or direct
     *
     * Processes unified measurements and adjusts output devices
     * according to the current mode and algorithm.
     *
     * @param measurements Unified measurements from any source
     */
    void update(const acrouter_measurements_t& measurements);

    /**
     * @brief Subscribe to event bus for measurement updates
     *
     * Registers event handler for ACROUTER_EVENT_POWER_UPDATE.
     * Call after esp_event_loop_create_default() (done by WiFiManager).
     * When Sensor Hub is active, subscribes to ACROUTER_EVENT_MERGED_UPDATE instead.
     *
     * @return ESP_OK on success
     */
    esp_err_t subscribeEvents();

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

    /**
     * @brief Retarget the primary (single-dimmer) actuation to a different dimmer id.
     *
     * v2 dimmers (DimmerLink-I2C, ESP-NOW output nodes) are discovered/auto-bound at
     * RUNTIME (e.g. an ESP-NOW dimmer node binds to slot DIMMER_ESPNOW_START via
     * dimmer_bind_espnow), long after begin(0) fixed m_dimmer_id to the legacy id 0.
     * Without this, every mode's applyDimmerLevel() would keep driving id 0 (which on
     * this build is a disabled no-op GPIO slot) and the real output would never move.
     * Idempotent: a no-op if id is already the primary. Rebuilds the priority map so
     * the AUTO cascade also picks up the newly-active dimmer.
     * @param id Dimmer slot id to drive (e.g. the value returned by dimmer_bind_espnow).
     */
    void setPrimaryDimmer(uint8_t id);

    /**
     * @brief Get the current primary dimmer id.
     */
    uint8_t getPrimaryDimmer() const { return m_dimmer_id; }

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

    /**
     * @brief Set the grid current limit for GRID_LIMIT mode (Amps).
     * @param amps Max grid draw in amperes (clamped to 0..MAX_GRID_CURRENT_LIMIT_A).
     */
    void setGridCurrentLimit(float amps);

    /**
     * @brief Get the grid current limit (Amps) used by GRID_LIMIT mode.
     */
    float getGridCurrentLimit() const { return m_grid_current_limit_a; }

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

    /**
     * @brief Refresh priority map from current device configurations
     *
     * Call this after adding/removing/reconfiguring devices (dimmers or relays)
     * or after initializing relay manager if it wasn't ready during begin().
     */
    void refreshPriorityMap() { rebuildPriorityMap(); }

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
    ~RouterController();
    RouterController(const RouterController&) = delete;
    RouterController& operator=(const RouterController&) = delete;

    /**
     * @brief Process AUTO mode algorithm
     * @param power_grid Grid power in watts (+ import, - export)
     */
    void processAutoMode(float power_grid);

    /**
     * @brief Process relay priority level control
     * @param level Priority level containing relays
     * @param remaining_delta Remaining delta to distribute (updated by function)
     * @param should_log Whether to log debug info
     */
    void processRelayPriority(PriorityLevel& level, float& remaining_delta, bool should_log);

    /**
     * @brief Process ECO mode algorithm
     * @param power_grid Grid power in watts (+ import, - export)
     */
    void processEcoMode(float power_grid);

    /**
     * @brief Failsafe: step the load down toward off when required sensor data is gone.
     *
     * Invoked when a regulating mode has lost its input: AUTO/ECO grid stale/lost,
     * GRID_LIMIT grid-current lost, or ALL sources silent (control task total-silence
     * tick). Defaulting a missing reading to 0 would be misread as "balanced"/"headroom"
     * and hold the last level (or ramp up) on dead data; instead this decays the load a
     * step per call until fresh data returns. Bounded — never an infinite hold.
     */
    void failsafeDecay();

    /**
     * @brief Process GRID_LIMIT mode algorithm (current-magnitude cap).
     * @param grid_current_a Grid RMS current magnitude in amperes (always import; no sign).
     *
     * Ramps the controlled load up to consume available headroom while keeping
     * |I_grid| <= the configured limit; backs off when the limit is exceeded.
     * Needs only grid current (works with a current-only I-module, no voltage).
     */
    void processGridLimitMode(float grid_current_a);

    /**
     * @brief Static event handler for ESP-IDF event loop
     */
    static void onPowerUpdateEvent(void* handler_arg, esp_event_base_t base,
                                   int32_t id, void* event_data);

    /**
     * @brief Dedicated control-loop task (isolation from the shared event-loop).
     *
     * The heavy update() must NOT run in the default event-loop task, where a busy
     * web/MQTT handler could delay the ~5 Hz control cadence. onPowerUpdateEvent()
     * only enqueues the freshest merged measurement into m_ctrl_queue (latest-wins
     * mailbox); this task consumes it and runs update() on its own core (APP_CPU on
     * dual-core, priority-isolated on single-core) with its own Task-WDT.
     */
    static void controlTask(void* arg);

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

    /**
     * @brief Rebuild priority map from current device configurations
     *
     * Scans all enabled dimmers and relays, groups them by priority,
     * and builds m_priority_levels array.
     */
    void rebuildPriorityMap();

    /**
     * @brief Get devices at specific priority level
     * @param priority Priority to query (0-255)
     * @return Pointer to PriorityLevel or nullptr if not found
     */
    const PriorityLevel* getDevicesAtPriority(uint8_t priority) const;

    // === Legacy single-dimmer support (backward compatibility) ===
    uint8_t m_dimmer_id;                ///< Primary dimmer ID (for legacy begin() API)
    float m_target_level;               ///< Legacy target level (for single-dimmer mode)
    uint8_t m_manual_level;             ///< Manual mode setting
    float m_grid_current_limit_a;       ///< GRID_LIMIT mode: max grid draw (A)

    // === Multi-device priority system ===
    // Store only active priority levels (max = total devices)
    static constexpr uint8_t MAX_PRIORITY_LEVELS = 16;  ///< Max different priority levels
    PriorityLevel m_priority_levels[MAX_PRIORITY_LEVELS];  ///< Active priority levels (sorted)
    uint8_t m_active_priority_count;        ///< Number of active priority levels
    bool m_multi_device_mode;               ///< true if using multi-device, false if legacy
    /// Guards m_priority_levels[]/m_active_priority_count: rebuildPriorityMap()
    /// (MQTT/web task) frees + reallocs the device arrays the control loop iterates.
    SemaphoreHandle_t m_priority_mutex;

    // === Isolated control task ===
    /// Length-1 mailbox holding the freshest merged measurement for the control task.
    QueueHandle_t m_ctrl_queue;
    /// Dedicated control task (own core/priority/WDT) — decoupled from the event loop.
    TaskHandle_t  m_ctrl_task;

    // Status
    RouterStatus m_status;
    bool m_initialized;
};

#endif // ROUTER_CONTROLLER_H
