/**
 * @file dimmerlink_manager.h
 * @brief DimmerLink device manager with polling task
 *
 * Manages up to DL_MAX_DEVICES DimmerLink modules on I2C bus.
 * Runs a FreeRTOS task that polls registered devices at configurable interval
 * and posts ACROUTER_EVENT_POWER_UPDATE events with measurement data.
 *
 * Usage:
 * 1. dl_manager_init()
 * 2. dl_manager_register(slot, &config) for each device
 * 3. dl_manager_start_polling(200) — poll every 200ms
 * 4. Events flow to RouterController via event bus
 */

#ifndef DIMMERLINK_MANAGER_H
#define DIMMERLINK_MANAGER_H

#include "esp_err.h"
#include "dimmerlink_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of DimmerLink devices */
#define DL_MAX_DEVICES          8

/** Default polling interval (ms) */
#define DL_DEFAULT_POLL_MS      200

/** Max consecutive errors before marking device offline */
#define DL_MAX_ERRORS           5

/**
 * @brief Initialize DimmerLink manager
 *
 * Clears device table. Does not start polling.
 * Call after i2c_bus_init().
 *
 * @return ESP_OK on success
 */
esp_err_t dl_manager_init(void);

/**
 * @brief Check if manager is initialized
 */
bool dl_manager_is_initialized(void);

/**
 * @brief Register a DimmerLink device
 *
 * @param slot      Slot number (0 to DL_MAX_DEVICES-1)
 * @param config    Device configuration (copied)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if slot out of range
 */
esp_err_t dl_manager_register(uint8_t slot, const dl_device_config_t* config);

/**
 * @brief Unregister a device
 *
 * @param slot  Slot number
 * @return ESP_OK on success
 */
esp_err_t dl_manager_unregister(uint8_t slot);

/**
 * @brief Start polling task
 *
 * Creates a FreeRTOS task that polls all enabled devices at the specified interval.
 * Posts ACROUTER_EVENT_POWER_UPDATE for each device with sensor role.
 *
 * @param interval_ms Polling interval in milliseconds (default: 200)
 * @return ESP_OK on success
 */
esp_err_t dl_manager_start_polling(uint16_t interval_ms);

/**
 * @brief Stop polling task
 */
esp_err_t dl_manager_stop_polling(void);

/**
 * @brief Check if polling is active
 */
bool dl_manager_is_polling(void);

/**
 * @brief Get device state (read-only)
 *
 * @param slot  Slot number
 * @return Pointer to device state, or NULL if slot invalid
 */
const dl_device_state_t* dl_manager_get_device(uint8_t slot);

/**
 * @brief Get number of active (enabled + online) devices
 */
uint8_t dl_manager_get_active_count(void);

/**
 * @brief Get number of registered (enabled) devices
 */
uint8_t dl_manager_get_enabled_count(void);

/**
 * @brief Save all device configs to NVS
 */
esp_err_t dl_manager_save_config(void);

/**
 * @brief Change a DimmerLink device's I2C address (stage 0x30 + RESET to apply).
 *
 * Validates range (0x08..0x77) and best-effort refuses if @p new_addr already
 * answers. Stages the new address and issues DL_CMD_RESET; the legacy firmware
 * latches the address on reset, so the module re-enumerates at @p new_addr
 * (async — "applies on reset"). If the device is registered, its config addr is
 * migrated and persisted.
 *
 * @return ESP_OK (staged, reset issued); ESP_ERR_INVALID_ARG (bad new_addr);
 *         ESP_ERR_INVALID_STATE (target address already in use); transport err.
 */
esp_err_t dl_manager_change_address(uint8_t cur_addr, uint8_t new_addr);

/** @brief Pause/resume DimmerLink polling (quiescent bus for discovery). */
void dl_manager_pause(bool pause);

/**
 * @brief Load device configs from NVS
 */
esp_err_t dl_manager_load_config(void);

/**
 * @brief Poll-cycle I2C timing (for the `timing` debug readout).
 * Any pointer may be NULL. last/avg in microseconds, count = cycles completed.
 */
void dl_manager_get_timing(uint32_t *last_us, uint32_t *avg_us, uint32_t *count);

#ifdef __cplusplus
}
#endif

#endif /* DIMMERLINK_MANAGER_H */
