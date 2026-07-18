/**
 * @file relay_i2c.h
 * @brief DimmerLink I2C backend for relay manager
 *
 * Controls relay channels on DimmerLink smart modules.
 * A relay is controlled by setting the TRIAC dimmer level to 100% (ON)
 * or 0% (OFF) — suitable for resistive loads and solenoid relays.
 *
 * Used for relay IDs 4-19 (RELAY_TYPE_I2C range).
 */

#ifndef RELAY_I2C_H
#define RELAY_I2C_H

#include "esp_err.h"
#include "relay_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2C relay subsystem
 */
esp_err_t relay_i2c_init(void);

/**
 * @brief Initialize one I2C relay channel (probe device)
 * @param r  Relay descriptor with i2c_addr, i2c_channel set
 */
esp_err_t relay_i2c_begin(relay_t* r);

/**
 * @brief Turn relay ON (sets DimmerLink level to 100%)
 */
esp_err_t relay_i2c_turn_on(relay_t* r);

/**
 * @brief Turn relay OFF (sets DimmerLink level to 0%)
 */
esp_err_t relay_i2c_turn_off(relay_t* r);

/**
 * @brief Read relay state from device
 */
bool relay_i2c_get_state(const relay_t* r);

/**
 * @brief Deinitialize I2C relay channel
 */
esp_err_t relay_i2c_deinit(relay_t* r);

#ifdef __cplusplus
}
#endif

#endif /* RELAY_I2C_H */
