/**
 * @file relay_manager.h
 * @brief Unified relay management API
 *
 * Provides a single interface for all relay types (GPIO, I2C, ESP-NOW).
 * Automatically dispatches calls to the appropriate backend based on relay ID.
 *
 * Architecture (similar to dimmer_manager):
 *   relay_manager.c   - Main manager, unified API
 *   relay_gpio.c      - GPIO backend (ID 0-3)
 *   relay_i2c.c       - I2C backend (ID 4-19, future)
 *   relay_espnow.c    - ESP-NOW backend (ID 20-63, future)
 */

#ifndef RELAY_MANAGER_H
#define RELAY_MANAGER_H

#include "relay_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Manager Initialization
// ============================================================

/**
 * @brief Initialize the relay manager
 *
 * Must be called before any other relay functions.
 * Initializes the internal relay array and loads configuration from NVS.
 *
 * @return ESP_OK on success
 */
esp_err_t relay_manager_init(void);

/**
 * @brief Check if manager is initialized
 */
bool relay_manager_is_initialized(void);

// ============================================================
// Relay Access
// ============================================================

/**
 * @brief Get relay by ID
 * @param id Relay ID (0-63)
 * @return Pointer to relay structure, or NULL if invalid ID
 */
relay_t* relay_get(uint8_t id);

/**
 * @brief Get relay by ID (const version)
 */
const relay_t* relay_get_const(uint8_t id);

/**
 * @brief Get total relay count
 */
uint8_t relay_get_max_count(void);

/**
 * @brief Get count of enabled relays
 */
uint8_t relay_get_enabled_count(void);

/**
 * @brief Get count of initialized (active) relays
 */
uint8_t relay_get_active_count(void);

// ============================================================
// Relay Control (Unified API)
// ============================================================

/**
 * @brief Turn relay ON
 *
 * Automatically dispatches to the correct backend (GPIO, I2C, ESP-NOW).
 * Respects debounce protection unless force=true.
 *
 * @param id Relay ID
 * @param force Bypass debounce protection
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG if invalid ID
 *         ESP_ERR_INVALID_STATE if relay not enabled/initialized or debounce active
 */
esp_err_t relay_turn_on(uint8_t id, bool force);

/**
 * @brief Turn relay OFF
 *
 * @param id Relay ID
 * @param force Bypass debounce protection
 * @return ESP_OK on success
 */
esp_err_t relay_turn_off(uint8_t id, bool force);

/**
 * @brief Toggle relay state
 *
 * @param id Relay ID
 * @param force Bypass debounce protection
 * @return ESP_OK on success
 */
esp_err_t relay_toggle(uint8_t id, bool force);

/**
 * @brief Get relay state
 * @param id Relay ID
 * @return true if ON, false if OFF or invalid
 */
bool relay_is_on(uint8_t id);

/**
 * @brief Check if debounce is active
 */
bool relay_is_debounce_active(uint8_t id);

/**
 * @brief Get debounce remaining time
 * @return Seconds remaining, or 0 if no debounce
 */
uint16_t relay_get_debounce_remaining(uint8_t id);

/**
 * @brief Get relay ON duration
 * @return Seconds since last turned ON, or 0 if OFF
 */
uint32_t relay_get_on_duration(uint8_t id);

/**
 * @brief Turn all relays OFF (emergency stop)
 * @param force Bypass debounce protection
 */
void relay_all_off(bool force);

/**
 * @brief Turn all enabled relays ON
 * @param force Bypass debounce protection
 * @return Number of relays turned ON
 */
uint8_t relay_all_on(bool force);

// ============================================================
// State Control
// ============================================================

/**
 * @brief Enable or disable a relay
 *
 * Disabled relays are ignored by control functions.
 *
 * @param id Relay ID
 * @param enabled Enable flag
 * @return ESP_OK on success
 */
esp_err_t relay_set_enabled(uint8_t id, bool enabled);

/**
 * @brief Check if relay is enabled
 */
bool relay_is_enabled(uint8_t id);

/**
 * @brief Check if relay hardware is initialized
 */
bool relay_is_initialized(uint8_t id);

/**
 * @brief Set relay operating mode
 */
esp_err_t relay_set_mode(uint8_t id, relay_mode_t mode);

/**
 * @brief Get relay operating mode
 */
relay_mode_t relay_get_mode(uint8_t id);

/**
 * @brief Get relay state
 */
relay_state_t relay_get_state(uint8_t id);

/**
 * @brief Update relay state (call periodically for debounce)
 *
 * Should be called every 100ms from main loop.
 * Handles debounce timers and pending state changes.
 */
void relay_update(uint8_t id);

/**
 * @brief Update all relays
 */
void relay_update_all(void);

// ============================================================
// Configuration
// ============================================================

/**
 * @brief Set relay name
 * @param id Relay ID
 * @param name Name string (max 15 chars, will be truncated)
 */
esp_err_t relay_set_name(uint8_t id, const char* name);

/**
 * @brief Get relay name
 */
const char* relay_get_name(uint8_t id);

/**
 * @brief Set nominal power rating
 */
esp_err_t relay_set_nominal_power(uint8_t id, uint16_t watts);

/**
 * @brief Get nominal power rating
 */
uint16_t relay_get_nominal_power(uint8_t id);

/**
 * @brief Set priority for AUTO mode
 * @param id Relay ID
 * @param priority Priority value (0-255, 0=highest)
 */
esp_err_t relay_set_priority(uint8_t id, uint8_t priority);

/**
 * @brief Get priority for AUTO mode
 * @param id Relay ID
 * @return Priority value (0-255, 0=highest)
 */
uint8_t relay_get_priority(uint8_t id);

/**
 * @brief Link relay to a current sensor
 * @param id Relay ID
 * @param sensor_id Current sensor ID (-1 = unlink)
 */
esp_err_t relay_set_current_sensor(uint8_t id, int8_t sensor_id);

/**
 * @brief Get linked current sensor ID
 */
int8_t relay_get_current_sensor(uint8_t id);

/**
 * @brief Set minimum ON time (debounce)
 */
esp_err_t relay_set_min_on_time(uint8_t id, uint16_t seconds);

/**
 * @brief Get minimum ON time
 */
uint16_t relay_get_min_on_time(uint8_t id);

/**
 * @brief Set minimum OFF time (debounce)
 */
esp_err_t relay_set_min_off_time(uint8_t id, uint16_t seconds);

/**
 * @brief Get minimum OFF time
 */
uint16_t relay_get_min_off_time(uint8_t id);

// ============================================================
// GPIO-Specific Configuration
// ============================================================

/**
 * @brief Configure GPIO pin for a relay
 *
 * Only valid for GPIO relays (ID 0-3).
 * Must be called before enabling the relay.
 *
 * @param id Relay ID (0-3)
 * @param gpio_pin GPIO pin number (-1 to disable)
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG if ID is not in GPIO range
 */
esp_err_t relay_set_gpio(uint8_t id, int8_t gpio_pin);

/**
 * @brief Get GPIO pin for a relay
 */
int8_t relay_get_gpio(uint8_t id);

/**
 * @brief Set active level (HIGH or LOW)
 */
esp_err_t relay_set_active_high(uint8_t id, bool active_high);

/**
 * @brief Get active level
 */
bool relay_get_active_high(uint8_t id);

// ============================================================
// Status Reporting
// ============================================================

/**
 * @brief Get full status structure for a relay
 */
esp_err_t relay_get_status(uint8_t id, relay_status_t* status);

/**
 * @brief Get total ON power for all active relays
 */
uint32_t relay_get_total_on_power(void);

/**
 * @brief Print relay status to log
 */
void relay_log_status(uint8_t id);

/**
 * @brief Print all relays status to log
 */
void relay_log_status_all(void);

// ============================================================
// NVS Operations
// ============================================================

/**
 * @brief Save single relay configuration to NVS
 */
esp_err_t relay_save_config(uint8_t id);

/**
 * @brief Load single relay configuration from NVS
 */
esp_err_t relay_load_config(uint8_t id);

/**
 * @brief Save all relay configurations to NVS
 */
esp_err_t relay_save_all(void);

/**
 * @brief Load all relay configurations from NVS
 */
esp_err_t relay_load_all(void);

/**
 * @brief Erase all relay configurations from NVS
 */
esp_err_t relay_erase_all(void);

// ============================================================
// Iteration Helpers
// ============================================================

/**
 * @brief Callback type for relay iteration
 */
typedef void (*relay_iterator_cb_t)(relay_t* relay, void* user_data);

/**
 * @brief Iterate over all relays
 */
void relay_foreach(relay_iterator_cb_t callback, void* user_data);

/**
 * @brief Iterate over relays of specific type
 */
void relay_foreach_type(relay_type_t type, relay_iterator_cb_t callback, void* user_data);

/**
 * @brief Iterate over enabled relays only
 */
void relay_foreach_enabled(relay_iterator_cb_t callback, void* user_data);

#ifdef __cplusplus
}
#endif

#endif // RELAY_MANAGER_H
