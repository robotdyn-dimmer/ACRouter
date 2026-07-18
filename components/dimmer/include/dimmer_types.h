/**
 * @file dimmer_types.h
 * @brief Dimmer type definitions and structures
 *
 * Pure C / ESP-IDF implementation for dimmer management.
 * Supports multiple dimmer types: GPIO, I2C (future), ESP-NOW (future)
 *
 * NVS Storage Schema:
 *   Namespace: "dimmer"
 *   Keys per dimmer (only for configured dimmers):
 *     d{id}_type   - u8  - dimmer_type_t
 *     d{id}_en     - u8  - enabled flag
 *     d{id}_name   - str - user name (max 15 chars)
 *     d{id}_pwr    - u16 - nominal power watts
 *     d{id}_sens   - i8  - current sensor ID (-1 = none)
 *     d{id}_curve  - u8  - dimmer_curve_t
 *     d{id}_mode   - u8  - dimmer_mode_t
 *     d{id}_min    - u8  - minimum level %
 *     d{id}_max    - u8  - maximum level %
 *     d{id}_def    - u8  - default level %
 *     d{id}_ramp   - u16 - ramp time ms
 *     d{id}_gpio   - i8  - GPIO pin (GPIO type only)
 *     d{id}_inv    - u8  - inverted output (GPIO type only)
 *     d{id}_i2c_a  - u8  - I2C address (I2C type only)
 *     d{id}_i2c_b  - u8  - I2C bus (I2C type only)
 *     d{id}_i2c_c  - u8  - I2C channel (I2C type only)
 *     d{id}_mac    - blob[6] - MAC address (ESPNOW type only)
 *     d{id}_wifi_c - u8  - WiFi channel (ESPNOW type only)
 *     d{id}_tout   - u16 - timeout ms (ESPNOW type only)
 *     d{id}_retry  - u8  - retry count (ESPNOW type only)
 *   Global keys:
 *     version      - u16 - NVS format version
 */

#ifndef DIMMER_TYPES_H
#define DIMMER_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// NVS Version
// ============================================================

/**
 * @brief NVS data format version
 *
 * Increment when changing NVS structure.
 * Version history:
 *   1: Initial version (basic fields)
 *   2: Added min/max/default levels, ramp time, inverted flag
 */
#define DIMMER_NVS_VERSION  1

// ============================================================
// Dimmer ID Ranges
// ============================================================

#define DIMMER_MAX_COUNT        64

#define DIMMER_GPIO_START       0
#define DIMMER_GPIO_COUNT       4

#define DIMMER_I2C_START        4
#define DIMMER_I2C_COUNT        8

#define DIMMER_ESPNOW_START     12
#define DIMMER_ESPNOW_COUNT     52

// Convenience macros
#define DIMMER_GPIO_END         (DIMMER_GPIO_START + DIMMER_GPIO_COUNT)
#define DIMMER_I2C_END          (DIMMER_I2C_START + DIMMER_I2C_COUNT)
#define DIMMER_ESPNOW_END       (DIMMER_ESPNOW_START + DIMMER_ESPNOW_COUNT)

// ID validation macros
#define DIMMER_ID_IS_GPIO(id)   ((id) >= DIMMER_GPIO_START && (id) < DIMMER_GPIO_END)
#define DIMMER_ID_IS_I2C(id)    ((id) >= DIMMER_I2C_START && (id) < DIMMER_I2C_END)
#define DIMMER_ID_IS_ESPNOW(id) ((id) >= DIMMER_ESPNOW_START && (id) < DIMMER_ESPNOW_END)
#define DIMMER_ID_IS_VALID(id)  ((id) < DIMMER_MAX_COUNT)

// ============================================================
// Enumerations
// ============================================================

/**
 * @brief Dimmer hardware type
 */
typedef enum {
    DIMMER_TYPE_NONE = 0,       ///< Not configured
    // value 1 = legacy DIMMER_TYPE_GPIO (removed in v2.0: GPIO/TRIAC dimming gone).
    // Values pinned so NVS-persisted type codes stay stable (I2C=2, ESPNOW=3).
    DIMMER_TYPE_I2C = 2,        ///< DimmerLink over I2C
    DIMMER_TYPE_ESPNOW = 3,     ///< DimmerLink over ESP-NOW (remote node)
} dimmer_type_t;

/**
 * @brief Dimmer runtime state
 */
typedef enum {
    DIMMER_STATE_OFF = 0,       ///< Output at 0%
    DIMMER_STATE_ON,            ///< Output > 0%
    DIMMER_STATE_TRANSITIONING, ///< During smooth transition
    DIMMER_STATE_ERROR,         ///< Hardware error
    DIMMER_STATE_DISCONNECTED,  ///< Remote device offline (ESP-NOW/BLE)
} dimmer_state_t;

/**
 * @brief Dimmer operating mode
 */
typedef enum {
    DIMMER_MODE_AUTO = 0,       ///< Controlled by RouterController
    DIMMER_MODE_MANUAL_ON,      ///< Forced ON (100%)
    DIMMER_MODE_MANUAL_OFF,     ///< Forced OFF (0%)
    DIMMER_MODE_SCHEDULE,       ///< Controlled by scheduler (future)
} dimmer_mode_t;

/**
 * @brief Dimmer power curve type
 */
typedef enum {
    DIMMER_CURVE_LINEAR = 0,    ///< Linear mapping (0-100% = 0-100% power)
    DIMMER_CURVE_RMS,           ///< RMS compensation for resistive loads
    DIMMER_CURVE_LOGARITHMIC,   ///< Logarithmic curve for perceived brightness
} dimmer_curve_t;

// ============================================================
// Structures
// ============================================================

/**
 * @brief Dimmer configuration and state structure
 *
 * Represents a single dimmer slot in the system.
 * Slot state:
 *   - type=DIMMER_TYPE_NONE: not configured
 *   - type=GPIO + enabled=false: configured but disabled
 *   - type=GPIO + enabled=true + initialized=true: active
 *
 * Fields marked [NVS] are persisted to NVS storage.
 * Fields marked [RUNTIME] exist only in RAM.
 */
typedef struct {
    // ===== Identification =====
    uint8_t id;                 ///< [RUNTIME] 0-based index (0-63)
    dimmer_type_t type;         ///< [NVS] Hardware type

    // ===== Common Configuration (saved to NVS) =====
    bool enabled;               ///< [NVS] Enable/disable flag
    char name[16];              ///< [NVS] User-friendly name (max 15 chars + null)
    uint16_t nominal_power_w;   ///< [NVS] Rated power in watts
    int8_t current_sensor_id;   ///< [NVS] Linked current sensor (-1 = none)
    dimmer_curve_t curve;       ///< [NVS] Power curve type
    dimmer_mode_t mode;         ///< [NVS] Operating mode

    // ===== Level Limits (saved to NVS) =====
    uint8_t min_level;          ///< [NVS] Minimum allowed level % (0-100)
    uint8_t max_level;          ///< [NVS] Maximum allowed level % (0-100)
    uint8_t default_level;      ///< [NVS] Level to set on boot % (0-100)
    uint16_t ramp_time_ms;      ///< [NVS] Default smooth transition time (0-10000)
    uint8_t priority;           ///< [NVS] Priority for AUTO mode (0-255, 0=highest)

    // ===== GPIO-specific configuration =====
    int8_t gpio_pin;            ///< [NVS] GPIO pin number (-1 = not assigned)
    bool gpio_inverted;         ///< [NVS] Invert output signal

    // ===== I2C-specific configuration (future) =====
    uint8_t i2c_address;        ///< [NVS] I2C device address (0x00-0x7F)
    uint8_t i2c_bus;            ///< [NVS] I2C bus number (0 or 1)
    uint8_t i2c_channel;        ///< [NVS] Channel on I2C device (0-15)

    // ===== ESP-NOW-specific configuration (future) =====
    uint8_t espnow_mac[6];      ///< [NVS] Remote device MAC address
    uint8_t espnow_wifi_channel;///< [NVS] WiFi channel (1-13, 0=auto)
    uint16_t espnow_timeout_ms; ///< [NVS] Response timeout (100-10000)
    uint8_t espnow_retry_count; ///< [NVS] Retry count (0-10)

    // ===== Runtime state (NOT saved to NVS) =====
    uint8_t level_percent;      ///< [RUNTIME] Current output level (0-100)
    uint8_t target_percent;     ///< [RUNTIME] Target level during transition
    dimmer_state_t state;       ///< [RUNTIME] Current operational state
    bool initialized;           ///< [RUNTIME] Hardware initialized flag
    uint32_t last_update_ms;    ///< [RUNTIME] Last state update timestamp
    uint32_t last_cmd_ms;       ///< [RUNTIME] Last command timestamp (for timeout detection)

    // ===== Hardware handle (internal use) =====
    void* hw_handle;            ///< [RUNTIME] Type-specific handle (e.g., rbdimmer_channel_t*)

} dimmer_t;

/**
 * @brief Dimmer status snapshot (for API responses)
 */
typedef struct {
    uint8_t id;
    dimmer_type_t type;
    bool enabled;
    bool initialized;
    dimmer_state_t state;
    dimmer_mode_t mode;
    dimmer_curve_t curve;
    uint8_t level_percent;
    uint8_t target_percent;
    uint8_t min_level;
    uint8_t max_level;
    uint16_t nominal_power_w;
    int8_t gpio_pin;
    // Transport addressing (for API serialization): valid per `type`.
    uint8_t i2c_address;        ///< I2C device address (type=I2C)
    uint8_t i2c_bus;            ///< I2C bus number (type=I2C)
    uint8_t espnow_mac[6];      ///< Remote MAC (type=ESPNOW)
    char name[16];
} dimmer_status_t;

/**
 * @brief Dimmer configuration for save/load (persistent fields only)
 */
typedef struct {
    dimmer_type_t type;
    bool enabled;
    char name[16];
    uint16_t nominal_power_w;
    int8_t current_sensor_id;
    dimmer_curve_t curve;
    dimmer_mode_t mode;
    uint8_t min_level;
    uint8_t max_level;
    uint8_t default_level;
    uint16_t ramp_time_ms;

    // GPIO
    int8_t gpio_pin;
    bool gpio_inverted;

    // I2C
    uint8_t i2c_address;
    uint8_t i2c_bus;
    uint8_t i2c_channel;

    // ESP-NOW
    uint8_t espnow_mac[6];
    uint8_t espnow_wifi_channel;
    uint16_t espnow_timeout_ms;
    uint8_t espnow_retry_count;
} dimmer_config_t;

// ============================================================
// Helper Functions (inline)
// ============================================================

/**
 * @brief Get expected dimmer type by ID
 * @param id Dimmer ID (0-63)
 * @return Expected dimmer type based on ID range
 */
static inline dimmer_type_t dimmer_type_from_id(uint8_t id) {
    // v2.0: GPIO id range (0-3) is reserved-empty; no GPIO type anymore.
    if (DIMMER_ID_IS_I2C(id)) return DIMMER_TYPE_I2C;
    if (DIMMER_ID_IS_ESPNOW(id)) return DIMMER_TYPE_ESPNOW;
    return DIMMER_TYPE_NONE;
}

/**
 * @brief Get dimmer type name string
 */
static inline const char* dimmer_type_str(dimmer_type_t type) {
    switch (type) {
        case DIMMER_TYPE_NONE:   return "NONE";
        case DIMMER_TYPE_I2C:    return "I2C";
        case DIMMER_TYPE_ESPNOW: return "ESPNOW";
        default:                 return "UNKNOWN";
    }
}

/**
 * @brief Get dimmer state name string
 */
static inline const char* dimmer_state_str(dimmer_state_t state) {
    switch (state) {
        case DIMMER_STATE_OFF:          return "OFF";
        case DIMMER_STATE_ON:           return "ON";
        case DIMMER_STATE_TRANSITIONING: return "TRANSITIONING";
        case DIMMER_STATE_ERROR:        return "ERROR";
        case DIMMER_STATE_DISCONNECTED: return "DISCONNECTED";
        default:                        return "UNKNOWN";
    }
}

/**
 * @brief Get dimmer mode name string
 */
static inline const char* dimmer_mode_str(dimmer_mode_t mode) {
    switch (mode) {
        case DIMMER_MODE_AUTO:       return "AUTO";
        case DIMMER_MODE_MANUAL_ON:  return "MANUAL_ON";
        case DIMMER_MODE_MANUAL_OFF: return "MANUAL_OFF";
        case DIMMER_MODE_SCHEDULE:   return "SCHEDULE";
        default:                     return "UNKNOWN";
    }
}

/**
 * @brief Get dimmer curve name string
 */
static inline const char* dimmer_curve_str(dimmer_curve_t curve) {
    switch (curve) {
        case DIMMER_CURVE_LINEAR:      return "LINEAR";
        case DIMMER_CURVE_RMS:         return "RMS";
        case DIMMER_CURVE_LOGARITHMIC: return "LOG";
        default:                       return "UNKNOWN";
    }
}

#ifdef __cplusplus
}
#endif

#endif // DIMMER_TYPES_H
