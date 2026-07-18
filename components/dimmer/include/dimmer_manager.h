/**
 * @file dimmer_manager.h
 * @brief Unified dimmer management API
 *
 * Provides a single interface for all dimmer types (GPIO, I2C, ESP-NOW).
 * Automatically dispatches calls to the appropriate backend based on dimmer ID.
 */

#ifndef DIMMER_MANAGER_H
#define DIMMER_MANAGER_H

#include "dimmer_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Manager Initialization
// ============================================================

/**
 * @brief Initialize the dimmer manager
 *
 * Must be called before any other dimmer functions.
 * Initializes the internal dimmer array and loads configuration from NVS.
 *
 * @return ESP_OK on success
 */
esp_err_t dimmer_manager_init(void);

/**
 * @brief Check if manager is initialized
 */
bool dimmer_manager_is_initialized(void);

// ============================================================
// Dimmer Access
// ============================================================

/**
 * @brief Get dimmer by ID
 * @param id Dimmer ID (0-63)
 * @return Pointer to dimmer structure, or NULL if invalid ID
 */
dimmer_t* dimmer_get(uint8_t id);

/**
 * @brief Get dimmer by ID (const version)
 */
const dimmer_t* dimmer_get_const(uint8_t id);

/**
 * @brief Get total dimmer count
 */
uint8_t dimmer_get_max_count(void);

/**
 * @brief Get count of enabled dimmers
 */
uint8_t dimmer_get_enabled_count(void);

/**
 * @brief Get count of initialized (active) dimmers
 */
uint8_t dimmer_get_active_count(void);

// ============================================================
// Level Control (Unified API)
// ============================================================

/**
 * @brief Set dimmer level immediately
 *
 * Automatically dispatches to the correct backend (GPIO, I2C, ESP-NOW).
 *
 * @param id Dimmer ID
 * @param percent Level 0-100%
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG if invalid ID
 *         ESP_ERR_INVALID_STATE if dimmer not enabled/initialized
 */
esp_err_t dimmer_set_level(uint8_t id, uint8_t percent);

/**
 * @brief Set dimmer level with smooth transition
 *
 * @param id Dimmer ID
 * @param percent Target level 0-100%
 * @param transition_ms Transition time in milliseconds (max 5000)
 * @return ESP_OK on success
 */
esp_err_t dimmer_set_level_smooth(uint8_t id, uint8_t percent, uint32_t transition_ms);

/**
 * @brief Get current dimmer level
 * @param id Dimmer ID
 * @return Level 0-100, or 0 if invalid
 */
uint8_t dimmer_get_level(uint8_t id);

/**
 * @brief Get target level (during transition)
 */
uint8_t dimmer_get_target_level(uint8_t id);

/**
 * @brief Set level for all enabled dimmers
 * @param percent Level 0-100%
 * @return Number of dimmers updated
 */
uint8_t dimmer_set_level_all(uint8_t percent);

/**
 * @brief Turn off all dimmers (emergency stop)
 */
void dimmer_emergency_stop_all(void);

// ============================================================
// State Control
// ============================================================

/**
 * @brief Enable or disable a dimmer
 *
 * Disabled dimmers are ignored by level control functions.
 *
 * @param id Dimmer ID
 * @param enabled Enable flag
 * @return ESP_OK on success
 */
esp_err_t dimmer_set_enabled(uint8_t id, bool enabled);

/**
 * @brief Check if dimmer is enabled
 */
bool dimmer_is_enabled(uint8_t id);

/**
 * @brief Check if dimmer hardware is initialized
 */
bool dimmer_is_initialized(uint8_t id);

/**
 * @brief Set dimmer operating mode
 */
esp_err_t dimmer_set_mode(uint8_t id, dimmer_mode_t mode);

/**
 * @brief Get dimmer operating mode
 */
dimmer_mode_t dimmer_get_mode(uint8_t id);

/**
 * @brief Set dimmer power curve
 */
esp_err_t dimmer_set_curve(uint8_t id, dimmer_curve_t curve);

/**
 * @brief Get dimmer power curve
 */
dimmer_curve_t dimmer_get_curve(uint8_t id);

/**
 * @brief Get dimmer state
 */
dimmer_state_t dimmer_get_state(uint8_t id);

/**
 * @brief Turn all dimmers off (set level to 0)
 */
void dimmer_all_off(void);

// ============================================================
// Configuration
// ============================================================

/**
 * @brief Set dimmer name
 * @param id Dimmer ID
 * @param name Name string (max 15 chars, will be truncated)
 */
esp_err_t dimmer_set_name(uint8_t id, const char* name);

/**
 * @brief Get dimmer name
 */
const char* dimmer_get_name(uint8_t id);

/**
 * @brief Set nominal power rating
 */
esp_err_t dimmer_set_nominal_power(uint8_t id, uint16_t watts);

/**
 * @brief Get nominal power rating
 */
uint16_t dimmer_get_nominal_power(uint8_t id);

/**
 * @brief Set priority for AUTO mode
 * @param id Dimmer ID
 * @param priority Priority value (0-255, 0=highest)
 */
esp_err_t dimmer_set_priority(uint8_t id, uint8_t priority);

/**
 * @brief Get priority for AUTO mode
 * @param id Dimmer ID
 * @return Priority value (0-255, 0=highest)
 */
uint8_t dimmer_get_priority(uint8_t id);

/**
 * @brief Link dimmer to a current sensor
 * @param id Dimmer ID
 * @param sensor_id Current sensor ID (-1 = unlink)
 */
esp_err_t dimmer_set_current_sensor(uint8_t id, int8_t sensor_id);

/**
 * @brief Get linked current sensor ID
 */
int8_t dimmer_get_current_sensor(uint8_t id);

// ============================================================
// DimmerLink Output Binding
// ============================================================

/**
 * @brief Bind a DimmerLink (I2C) device to a dimmer slot and enable it, so the
 * RouterController drives it. Reuses an existing slot for the same bus+addr, else
 * allocates a free I2C slot. Transport-agnostic output — an ESP-NOW output-node
 * plugs into the same abstraction later (DIMMER_TYPE_ESPNOW).
 * @return dimmer id on success, or -1 if no free slot.
 */
int dimmer_bind_i2c(uint8_t bus, uint8_t addr);

/**
 * @brief Bind a discovered ESP-NOW output node (by MAC) to a dimmer slot and enable
 * it, so the RouterController drives it transport-agnostically. Reuses an existing
 * slot for the same MAC, else allocates a free ESP-NOW slot. dimmer_set_level then
 * dispatches to esp_now_source (SET_OUTPUT), just like the I2C path dispatches to
 * DimmerLink. Requires CONFIG_ACROUTER_ESPNOW_SOURCE for the actual send.
 * @return dimmer id on success, or -1 if no free slot.
 */
int dimmer_bind_espnow(const uint8_t mac[6]);

// ============================================================
// Status Reporting
// ============================================================

/**
 * @brief Get full status structure for a dimmer
 */
esp_err_t dimmer_get_status(uint8_t id, dimmer_status_t* status);

/**
 * @brief Print dimmer status to log
 */
void dimmer_log_status(uint8_t id);

/**
 * @brief Print all dimmers status to log
 */
void dimmer_log_status_all(void);

// ============================================================
// NVS Operations
// ============================================================

/**
 * @brief Save single dimmer configuration to NVS
 */
esp_err_t dimmer_save_config(uint8_t id);

/**
 * @brief Load single dimmer configuration from NVS
 */
esp_err_t dimmer_load_config(uint8_t id);

/**
 * @brief Save all dimmer configurations to NVS
 */
esp_err_t dimmer_save_all(void);

/**
 * @brief Load all dimmer configurations from NVS
 */
esp_err_t dimmer_load_all(void);

/**
 * @brief Erase all dimmer configurations from NVS
 */
esp_err_t dimmer_erase_all(void);

// ============================================================
// Iteration Helpers
// ============================================================

/**
 * @brief Callback type for dimmer iteration
 */
typedef void (*dimmer_iterator_cb_t)(dimmer_t* dimmer, void* user_data);

/**
 * @brief Iterate over all dimmers
 */
void dimmer_foreach(dimmer_iterator_cb_t callback, void* user_data);

/**
 * @brief Iterate over dimmers of specific type
 */
void dimmer_foreach_type(dimmer_type_t type, dimmer_iterator_cb_t callback, void* user_data);

/**
 * @brief Iterate over enabled dimmers only
 */
void dimmer_foreach_enabled(dimmer_iterator_cb_t callback, void* user_data);

#ifdef __cplusplus
}
#endif

#endif // DIMMER_MANAGER_H
