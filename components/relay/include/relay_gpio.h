/**
 * @file relay_gpio.h
 * @brief GPIO relay backend
 *
 * Implements GPIO relay control for local relay outputs (ID 0-3).
 * Uses ESP-IDF GPIO driver for direct pin control.
 */

#ifndef RELAY_GPIO_H
#define RELAY_GPIO_H

#include "relay_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// GPIO Backend Initialization
// ============================================================

/**
 * @brief Initialize GPIO relay backend
 *
 * Must be called before using GPIO relays.
 * This function doesn't initialize individual GPIO pins - that happens
 * when each relay is enabled via relay_gpio_begin().
 *
 * @return ESP_OK on success
 */
esp_err_t relay_gpio_init(void);

/**
 * @brief Check if GPIO backend is initialized
 */
bool relay_gpio_is_initialized(void);

// ============================================================
// GPIO Relay Operations
// ============================================================

/**
 * @brief Initialize a single GPIO relay
 *
 * Configures the GPIO pin as output and sets initial state to OFF.
 * Called automatically by relay_manager when relay is enabled.
 *
 * @param relay Pointer to relay structure
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG if relay is NULL or not GPIO type
 *         ESP_ERR_INVALID_STATE if GPIO pin not configured
 */
esp_err_t relay_gpio_begin(relay_t* relay);

/**
 * @brief Turn GPIO relay ON
 *
 * @param relay Pointer to relay structure
 * @return ESP_OK on success
 */
esp_err_t relay_gpio_turn_on(relay_t* relay);

/**
 * @brief Turn GPIO relay OFF
 *
 * @param relay Pointer to relay structure
 * @return ESP_OK on success
 */
esp_err_t relay_gpio_turn_off(relay_t* relay);

/**
 * @brief Get GPIO relay state
 *
 * Reads actual GPIO pin state.
 *
 * @param relay Pointer to relay structure
 * @return true if ON, false if OFF
 */
bool relay_gpio_get_state(const relay_t* relay);

/**
 * @brief Deinitialize GPIO relay
 *
 * Turns off and releases the GPIO pin.
 *
 * @param relay Pointer to relay structure
 * @return ESP_OK on success
 */
esp_err_t relay_gpio_deinit(relay_t* relay);

#ifdef __cplusplus
}
#endif

#endif // RELAY_GPIO_H
