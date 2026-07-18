/**
 * @file relay_types.h
 * @brief Relay type definitions and structures
 *
 * Defines all relay types, states, modes, and configuration structures.
 * Used by relay_manager and backend implementations (GPIO, I2C, ESP-NOW).
 *
 * NVS Storage Format (per relay):
 *   Key prefix: "relay_{id}_"
 *   Example keys for relay 0:
 *     r0_type   - u8  - relay_type_t
 *     r0_gpio   - i8  - GPIO pin (-1 = not set)
 *     r0_name   - str - Relay name (max 15 chars)
 *     r0_en     - u8  - Enabled flag
 *     r0_mode   - u8  - relay_mode_t
 *     r0_actl   - u8  - Active level (1=high, 0=low)
 *     r0_pwr    - u16 - Nominal power (watts)
 *     r0_sns    - i8  - Current sensor ID (-1 = none)
 *     r0_mon    - u16 - Min ON time (seconds)
 *     r0_moff   - u16 - Min OFF time (seconds)
 */

#ifndef RELAY_TYPES_H
#define RELAY_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Constants
// ============================================================

/** Maximum number of relays in system */
#define RELAY_MAX_COUNT    64

/** Maximum GPIO relays (ID 0-3) */
#define RELAY_GPIO_COUNT   4

/** Maximum I2C relays (ID 4-19) */
#define RELAY_I2C_COUNT    16

/** Maximum ESP-NOW relays (ID 20-63) */
#define RELAY_ESPNOW_COUNT 44

/** Maximum relay name length (including null terminator) */
#define RELAY_NAME_MAX_LEN 16

/** Default debounce times */
#define RELAY_DEFAULT_MIN_ON_TIME_S  60   // 1 minute
#define RELAY_DEFAULT_MIN_OFF_TIME_S 60   // 1 minute

// ============================================================
// Enumerations
// ============================================================

/**
 * @brief Relay hardware type
 *
 * Determines which backend handles the relay.
 * ID ranges:
 *   GPIO:    0-3   (local GPIO pins)
 *   I2C:     4-19  (PCF8574, MCP23017, etc.)
 *   ESP-NOW: 20-63 (wireless relays)
 */
typedef enum {
    RELAY_TYPE_NONE = 0,        ///< Slot not configured
    RELAY_TYPE_GPIO,            ///< Local GPIO relay (0-3)
    RELAY_TYPE_I2C,             ///< I2C expander relay (4-19) - future
    RELAY_TYPE_ESPNOW,          ///< ESP-NOW wireless relay (20-63) - future
} relay_type_t;

/**
 * @brief Relay runtime state
 */
typedef enum {
    RELAY_STATE_OFF = 0,        ///< Relay is OFF
    RELAY_STATE_ON,             ///< Relay is ON
    RELAY_STATE_DEBOUNCE,       ///< In debounce period (can't switch)
    RELAY_STATE_ERROR,          ///< Hardware error
    RELAY_STATE_DISCONNECTED,   ///< Remote device offline (ESP-NOW)
} relay_state_t;

/**
 * @brief Relay operating mode
 */
typedef enum {
    RELAY_MODE_AUTO = 0,        ///< Controlled by RouterController
    RELAY_MODE_MANUAL_ON,       ///< Forced ON
    RELAY_MODE_MANUAL_OFF,      ///< Forced OFF
    RELAY_MODE_SCHEDULE,        ///< Controlled by scheduler (future)
} relay_mode_t;

// ============================================================
// Structures
// ============================================================

/**
 * @brief Relay configuration and state structure
 *
 * Represents a single relay slot in the system.
 * Slot state:
 *   - type=RELAY_TYPE_NONE: not configured
 *   - type=GPIO + enabled=false: configured but disabled
 *   - type=GPIO + enabled=true + initialized=true: active
 *
 * Fields marked [NVS] are persisted to NVS storage.
 * Fields marked [RUNTIME] exist only in RAM.
 */
typedef struct {
    // ===== Identification =====
    uint8_t id;                 ///< [RUNTIME] 0-based index (0-63)
    relay_type_t type;          ///< [NVS] Hardware type

    // ===== GPIO Configuration (for GPIO relays only) =====
    int8_t gpio_pin;            ///< [NVS] GPIO pin number (-1 = not set)
    bool active_high;           ///< [NVS] true = HIGH=ON, false = LOW=ON

    // ===== I2C Configuration (for I2C relays only) =====
    uint8_t i2c_addr;           ///< [NVS] I2C address (future)
    uint8_t i2c_channel;        ///< [NVS] Channel on I2C expander (future)

    // ===== ESP-NOW Configuration (for ESP-NOW relays only) =====
    uint8_t espnow_mac[6];      ///< [NVS] Remote device MAC (future)
    uint8_t espnow_channel;     ///< [NVS] Remote relay channel (future)

    // ===== Common Configuration =====
    char name[RELAY_NAME_MAX_LEN];  ///< [NVS] User-friendly name
    bool enabled;               ///< [NVS] Enable flag
    relay_mode_t mode;          ///< [NVS] Operating mode
    uint16_t nominal_power_w;   ///< [NVS] Rated power in watts
    int8_t current_sensor_id;   ///< [NVS] Linked current sensor (-1 = none)

    // ===== Debounce Protection =====
    uint16_t min_on_time_s;     ///< [NVS] Minimum ON time (seconds)
    uint16_t min_off_time_s;    ///< [NVS] Minimum OFF time (seconds)
    uint8_t priority;           ///< [NVS] Priority for AUTO mode (0-255, 0=highest)

    // ===== Runtime State =====
    bool initialized;           ///< [RUNTIME] Hardware initialized
    bool is_on;                 ///< [RUNTIME] Current state (ON/OFF)
    uint32_t last_switch_ms;    ///< [RUNTIME] Timestamp of last switch
    bool pending_on;            ///< [RUNTIME] Pending turn ON after debounce
    bool pending_off;           ///< [RUNTIME] Pending turn OFF after debounce
} relay_t;

// ============================================================
// Status Structures
// ============================================================

/**
 * @brief Relay status for reporting
 */
typedef struct {
    uint8_t id;
    relay_type_t type;
    relay_state_t state;
    relay_mode_t mode;
    bool enabled;
    bool initialized;
    bool is_on;
    int8_t gpio_pin;
    bool active_high;
    const char* name;
    uint16_t nominal_power_w;
    int8_t current_sensor_id;
    uint8_t priority;
    uint16_t min_on_time_s;
    uint16_t min_off_time_s;
    uint16_t debounce_remaining_s;
    uint32_t on_duration_s;
} relay_status_t;

/**
 * @brief Relay statistics
 */
typedef struct {
    uint32_t on_count;          ///< Number of ON transitions
    uint32_t off_count;         ///< Number of OFF transitions
    uint32_t total_on_time_s;   ///< Total time spent ON (seconds)
    uint32_t debounce_blocks;   ///< Times switch blocked by debounce
} relay_stats_t;

// ============================================================
// Helper Functions
// ============================================================

/**
 * @brief Get string name for relay type
 */
static inline const char* relay_type_str(relay_type_t type) {
    switch (type) {
        case RELAY_TYPE_NONE:    return "NONE";
        case RELAY_TYPE_GPIO:    return "GPIO";
        case RELAY_TYPE_I2C:     return "I2C";
        case RELAY_TYPE_ESPNOW:  return "ESP-NOW";
        default:                 return "UNKNOWN";
    }
}

/**
 * @brief Get string name for relay state
 */
static inline const char* relay_state_str(relay_state_t state) {
    switch (state) {
        case RELAY_STATE_OFF:          return "OFF";
        case RELAY_STATE_ON:           return "ON";
        case RELAY_STATE_DEBOUNCE:     return "DEBOUNCE";
        case RELAY_STATE_ERROR:        return "ERROR";
        case RELAY_STATE_DISCONNECTED: return "DISCONNECTED";
        default:                       return "UNKNOWN";
    }
}

/**
 * @brief Get string name for relay mode
 */
static inline const char* relay_mode_str(relay_mode_t mode) {
    switch (mode) {
        case RELAY_MODE_AUTO:       return "AUTO";
        case RELAY_MODE_MANUAL_ON:  return "MANUAL_ON";
        case RELAY_MODE_MANUAL_OFF: return "MANUAL_OFF";
        case RELAY_MODE_SCHEDULE:   return "SCHEDULE";
        default:                    return "UNKNOWN";
    }
}

/**
 * @brief Determine relay type from ID
 */
static inline relay_type_t relay_id_to_type(uint8_t id) {
    if (id < RELAY_GPIO_COUNT) {
        return RELAY_TYPE_GPIO;
    } else if (id < RELAY_GPIO_COUNT + RELAY_I2C_COUNT) {
        return RELAY_TYPE_I2C;
    } else if (id < RELAY_MAX_COUNT) {
        return RELAY_TYPE_ESPNOW;
    }
    return RELAY_TYPE_NONE;
}

#ifdef __cplusplus
}
#endif

#endif // RELAY_TYPES_H
