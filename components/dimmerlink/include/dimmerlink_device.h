/**
 * @file dimmerlink_device.h
 * @brief DimmerLink single device I2C operations
 *
 * Low-level API for reading/writing DimmerLink registers.
 * All functions are stateless — they just perform I2C transactions.
 */

#ifndef DIMMERLINK_DEVICE_H
#define DIMMERLINK_DEVICE_H

#include "esp_err.h"
#include "dimmerlink_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Probe device presence on I2C bus
 *
 * @param bus   I2C bus number
 * @param addr  Device address
 * @return ESP_OK if device responds
 */
esp_err_t dl_device_probe(uint8_t bus, uint8_t addr);

/**
 * @brief Read device info (STATUS, ERROR, VERSION)
 */
esp_err_t dl_device_read_info(uint8_t bus, uint8_t addr, dl_device_info_t* info);

/**
 * @brief Read current sensor snapshot (24 bytes from 0x60-0x77)
 *
 * Reads the entire current sensor register window atomically.
 * The snapshot is parsed from little-endian raw bytes.
 */
esp_err_t dl_device_read_current(uint8_t bus, uint8_t addr, dl_current_snapshot_t* snap);

/**
 * @brief Read voltage sensor data (0x78-0x7D)
 */
esp_err_t dl_device_read_voltage(uint8_t bus, uint8_t addr, dl_voltage_snapshot_t* snap);

/**
 * @brief Read thermal status (0x40-0x45)
 */
esp_err_t dl_device_read_thermal(uint8_t bus, uint8_t addr, dl_thermal_status_t* status);

/**
 * @brief Read dimmer status (level, curve, fade, AC freq)
 */
esp_err_t dl_device_read_dimmer(uint8_t bus, uint8_t addr, dl_dimmer_status_t* status);

/**
 * @brief Set dimmer level (immediate)
 *
 * @param bus    I2C bus number
 * @param addr   Device address
 * @param percent Level 0-100%
 */
esp_err_t dl_device_set_dimmer_level(uint8_t bus, uint8_t addr, uint8_t percent);

/**
 * @brief Set dimmer level with fade
 *
 * @param bus       I2C bus number
 * @param addr      Device address
 * @param percent   Target level 0-100%
 * @param fade_100ms Fade time in 100ms units (e.g., 10 = 1 second)
 */
esp_err_t dl_device_set_dimmer_fade(uint8_t bus, uint8_t addr,
                                     uint8_t percent, uint8_t fade_100ms);

/**
 * @brief Set dimmer curve type
 */
esp_err_t dl_device_set_dimmer_curve(uint8_t bus, uint8_t addr, uint8_t curve);

/**
 * @brief Send command to device
 *
 * @param cmd Command code (DL_CMD_*)
 */
esp_err_t dl_device_send_command(uint8_t bus, uint8_t addr, uint8_t cmd);

/**
 * @brief Select accumulator for reading
 *
 * @param acc_num Accumulator number (0-7)
 */
esp_err_t dl_device_select_accumulator(uint8_t bus, uint8_t addr, uint8_t acc_num);

/**
 * @brief Commit (latch) accumulator snapshot
 *
 * @param acc_num Accumulator number to commit (0-7)
 */
esp_err_t dl_device_commit_accumulator(uint8_t bus, uint8_t addr, uint8_t acc_num);

#ifdef __cplusplus
}
#endif

#endif /* DIMMERLINK_DEVICE_H */
