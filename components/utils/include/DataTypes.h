/**
 * @file DataTypes.h
 * @brief Common data types and structures for AC Power Router Controller
 */

#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <Arduino.h>
#include <cstdint>
#include <cmath>

// ============================================================
// System Mode Enumeration
// ============================================================

/**
 * @brief Operating modes of the router controller
 */
enum class SystemMode : uint8_t {
    OFF = 0,        ///< Dimmer disabled (0% power)
    AUTO,           ///< Automatic control based on grid power
    MANUAL,         ///< Fixed dimmer level set by user
    BOOST,          ///< Forced 100% power output
    SCHEDULE        ///< Time-based control [Phase 2]
};

/**
 * Convert SystemMode to string
 */
inline const char* systemModeToString(SystemMode mode) {
    switch (mode) {
        case SystemMode::OFF:       return "OFF";
        case SystemMode::AUTO:      return "AUTO";
        case SystemMode::MANUAL:    return "MANUAL";
        case SystemMode::BOOST:     return "BOOST";
        case SystemMode::SCHEDULE:  return "SCHEDULE";
        default:                    return "UNKNOWN";
    }
}

/**
 * Parse string to SystemMode
 */
inline SystemMode stringToSystemMode(const char* str) {
    if (strcmp(str, "OFF") == 0)        return SystemMode::OFF;
    if (strcmp(str, "AUTO") == 0)       return SystemMode::AUTO;
    if (strcmp(str, "MANUAL") == 0)     return SystemMode::MANUAL;
    if (strcmp(str, "BOOST") == 0)      return SystemMode::BOOST;
    if (strcmp(str, "SCHEDULE") == 0)   return SystemMode::SCHEDULE;
    return SystemMode::OFF; // Default safe state
}

// ============================================================
// Energy Direction
// ============================================================

/**
 * @brief Direction of energy flow at grid connection
 */
enum class EnergyDirection : uint8_t {
    IMPORT,         ///< Consuming from grid (P_grid > +threshold)
    EXPORT,         ///< Feeding to grid (P_grid < -threshold)
    BALANCED        ///< Within balance threshold
};

/**
 * Convert EnergyDirection to string
 */
inline const char* energyDirectionToString(EnergyDirection dir) {
    switch (dir) {
        case EnergyDirection::IMPORT:    return "IMPORT";
        case EnergyDirection::EXPORT:    return "EXPORT";
        case EnergyDirection::BALANCED:  return "BALANCED";
        default:                         return "UNKNOWN";
    }
}

// ============================================================
// Sensor Readings Structure
// ============================================================

/**
 * @brief Raw sensor readings from all channels
 *
 * Values are NAN if sensor is not present/configured
 */
struct SensorReadings {
    float voltage;              ///< AC voltage RMS (V) - NAN if not present
    float current_load;         ///< Load current RMS (A) - NAN if not present
    float current_grid;         ///< Grid current RMS (A) - NAN if not present
    float current_solar;        ///< Solar/inverter current RMS (A) - NAN if not present
    float current_aux[2];       ///< Auxiliary current channels (A)
    float temperature[2];       ///< Temperature readings (°C) [Phase 2]
    float frequency;            ///< AC frequency (Hz) - from zero-cross detection
    uint32_t timestamp;         ///< Timestamp of reading (millis())
    bool valid;                 ///< Data validity flag

    /**
     * Default constructor - initialize with NAN
     */
    SensorReadings() :
        voltage(NAN),
        current_load(NAN),
        current_grid(NAN),
        current_solar(NAN),
        current_aux{NAN, NAN},
        temperature{NAN, NAN},
        frequency(50.0f),
        timestamp(0),
        valid(false)
    {}

    /**
     * Check if voltage sensor is present
     */
    bool hasVoltage() const { return !isnan(voltage); }

    /**
     * Check if load current sensor is present
     */
    bool hasCurrentLoad() const { return !isnan(current_load); }

    /**
     * Check if grid current sensor is present
     */
    bool hasCurrentGrid() const { return !isnan(current_grid); }

    /**
     * Check if solar current sensor is present
     */
    bool hasCurrentSolar() const { return !isnan(current_solar); }
};

// ============================================================
// Power Readings Structure
// ============================================================

/**
 * @brief Calculated power values
 */
struct PowerReadings {
    float power_load;           ///< Power to load (W)
    float power_grid;           ///< Grid power (W) - positive = import, negative = export
    float power_solar;          ///< Solar generation (W)
    float power_house;          ///< House consumption excluding router load (W)
    EnergyDirection direction;  ///< Energy flow direction
    bool valid;                 ///< Calculation validity (enough sensors present)

    /**
     * Default constructor
     */
    PowerReadings() :
        power_load(0.0f),
        power_grid(0.0f),
        power_solar(0.0f),
        power_house(0.0f),
        direction(EnergyDirection::BALANCED),
        valid(false)
    {}
};

// ============================================================
// Energy Counters Structure
// ============================================================

/**
 * @brief Energy accumulation counters
 */
struct EnergyCounters {
    // Today's counters (Wh) - reset at midnight
    float routed_today;         ///< Energy routed to load today (Wh)
    float solar_today;          ///< Solar energy generated today (Wh)
    float imported_today;       ///< Energy imported from grid today (Wh)
    float exported_today;       ///< Energy exported to grid today (Wh)

    // Lifetime counters (kWh) - saved in NVS
    float routed_total;         ///< Total energy routed (kWh)
    float solar_total;          ///< Total solar energy (kWh)
    float imported_total;       ///< Total imported energy (kWh)
    float exported_total;       ///< Total exported energy (kWh)

    // Metadata
    uint32_t last_reset_epoch;  ///< Unix timestamp of last daily reset

    /**
     * Default constructor
     */
    EnergyCounters() :
        routed_today(0.0f),
        solar_today(0.0f),
        imported_today(0.0f),
        exported_today(0.0f),
        routed_total(0.0f),
        solar_total(0.0f),
        imported_total(0.0f),
        exported_total(0.0f),
        last_reset_epoch(0)
    {}

    /**
     * Reset daily counters
     */
    void resetDaily() {
        // Add daily values to totals (convert Wh to kWh)
        routed_total += routed_today / 1000.0f;
        solar_total += solar_today / 1000.0f;
        imported_total += imported_today / 1000.0f;
        exported_total += exported_today / 1000.0f;

        // Reset daily counters
        routed_today = 0.0f;
        solar_today = 0.0f;
        imported_today = 0.0f;
        exported_today = 0.0f;

        last_reset_epoch = time(nullptr);
    }
};

// ============================================================
// System State Structure (for inter-task communication)
// ============================================================

/**
 * @brief Complete system state snapshot
 *
 * Used for communication between ControlTask and CommTask
 */
struct SystemState {
    // Operating mode
    SystemMode mode;

    // Sensor data
    SensorReadings sensors;

    // Power calculations
    PowerReadings power;

    // Energy counters
    EnergyCounters energy;

    // Dimmer state
    uint8_t dimmer_percent;     ///< Current dimmer level (0-100%)

    // Safety flags
    uint8_t safety_flags;       ///< Bitfield of active safety conditions

    // System info
    uint32_t uptime_sec;        ///< System uptime (seconds)
    bool wifi_connected;        ///< WiFi connection status
    int8_t wifi_rssi;           ///< WiFi signal strength (dBm)
    bool ntp_synced;            ///< NTP time synchronization status
    uint32_t free_heap;         ///< Free heap memory (bytes)

    /**
     * Default constructor
     */
    SystemState() :
        mode(SystemMode::OFF),
        sensors(),
        power(),
        energy(),
        dimmer_percent(0),
        safety_flags(0),
        uptime_sec(0),
        wifi_connected(false),
        wifi_rssi(-100),
        ntp_synced(false),
        free_heap(0)
    {}
};

// ============================================================
// Utility Functions
// ============================================================

/**
 * @brief Clamp value between min and max
 * @note Renamed from 'constrain' to avoid conflict with Arduino macro
 */
template<typename T>
inline T clamp(T value, T min_val, T max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

/**
 * @brief Map value from one range to another
 */
inline float map_float(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/**
 * @brief Check if float is approximately equal (within epsilon)
 */
inline bool approxEqual(float a, float b, float epsilon = 0.001f) {
    return fabs(a - b) < epsilon;
}

#endif // DATA_TYPES_H
