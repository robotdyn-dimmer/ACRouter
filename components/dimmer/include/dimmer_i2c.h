/**
 * @file dimmer_i2c.h
 * @brief DimmerLink I2C backend for dimmer manager
 *
 * Implements dimmer control via DimmerLink smart dimmer modules.
 * Used for dimmer IDs 4-11 (DIMMER_TYPE_I2C range).
 */

#ifndef DIMMER_I2C_H
#define DIMMER_I2C_H

#include "esp_err.h"
#include "dimmer_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2C dimmer subsystem
 * @return ESP_OK if i2c_bus is available
 */
esp_err_t dimmer_i2c_init(void);

/**
 * @brief Initialize one I2C dimmer channel (probe device, set defaults)
 * @param d  Dimmer descriptor with i2c_address, i2c_bus, i2c_channel set
 */
esp_err_t dimmer_i2c_channel_init(dimmer_t* d);

/**
 * @brief Set dimmer level immediately (0-100%)
 */
esp_err_t dimmer_i2c_set_level(dimmer_t* d, uint8_t percent);

/**
 * @brief Set dimmer level with fade transition
 * @param ms  Fade duration in milliseconds (rounded to 100ms units)
 */
esp_err_t dimmer_i2c_set_level_smooth(dimmer_t* d, uint8_t percent, uint32_t ms);

/**
 * @brief Set dimmer curve type
 */
esp_err_t dimmer_i2c_set_curve(dimmer_t* d, dimmer_curve_t curve);

/**
 * @brief Deinitialize I2C dimmer channel
 */
esp_err_t dimmer_i2c_deinit(dimmer_t* d);

#ifdef __cplusplus
}
#endif

#endif /* DIMMER_I2C_H */
