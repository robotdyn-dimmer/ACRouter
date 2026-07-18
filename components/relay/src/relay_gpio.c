/**
 * @file relay_gpio.c
 * @brief GPIO relay backend implementation
 */

#include "relay_gpio.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "RelayGPIO";

/** GPIO backend initialization flag */
static bool s_gpio_initialized = false;

// ============================================================
// GPIO Backend Initialization
// ============================================================

esp_err_t relay_gpio_init(void) {
    if (s_gpio_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing GPIO relay backend");

    // No global initialization needed - GPIO pins are configured
    // individually when each relay is enabled

    s_gpio_initialized = true;
    ESP_LOGI(TAG, "GPIO relay backend initialized");

    return ESP_OK;
}

bool relay_gpio_is_initialized(void) {
    return s_gpio_initialized;
}

// ============================================================
// GPIO Relay Operations
// ============================================================

esp_err_t relay_gpio_begin(relay_t* relay) {
    if (!relay) {
        return ESP_ERR_INVALID_ARG;
    }

    if (relay->type != RELAY_TYPE_GPIO) {
        ESP_LOGE(TAG, "Relay %d is not GPIO type", relay->id);
        return ESP_ERR_INVALID_ARG;
    }

    if (relay->gpio_pin < 0) {
        ESP_LOGE(TAG, "Relay %d: GPIO pin not configured", relay->id);
        return ESP_ERR_INVALID_STATE;
    }

    // Configure GPIO as output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << relay->gpio_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s",
                 relay->gpio_pin, esp_err_to_name(err));
        return err;
    }

    // Set initial state to OFF
    int level = relay->active_high ? 0 : 1;  // OFF state
    gpio_set_level((gpio_num_t)relay->gpio_pin, level);

    // Update relay state
    relay->initialized = true;
    relay->is_on = false;
    relay->last_switch_ms = (uint32_t)(esp_timer_get_time() / 1000);

    ESP_LOGI(TAG, "Relay %d initialized on GPIO %d (%s)",
             relay->id, relay->gpio_pin,
             relay->active_high ? "active-high" : "active-low");

    return ESP_OK;
}

esp_err_t relay_gpio_turn_on(relay_t* relay) {
    if (!relay || !relay->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (relay->type != RELAY_TYPE_GPIO) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set GPIO to ON state
    int level = relay->active_high ? 1 : 0;
    esp_err_t err = gpio_set_level((gpio_num_t)relay->gpio_pin, level);

    if (err == ESP_OK) {
        relay->is_on = true;
        relay->last_switch_ms = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGD(TAG, "Relay %d (GPIO %d) turned ON", relay->id, relay->gpio_pin);
    }

    return err;
}

esp_err_t relay_gpio_turn_off(relay_t* relay) {
    if (!relay || !relay->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (relay->type != RELAY_TYPE_GPIO) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set GPIO to OFF state
    int level = relay->active_high ? 0 : 1;
    esp_err_t err = gpio_set_level((gpio_num_t)relay->gpio_pin, level);

    if (err == ESP_OK) {
        relay->is_on = false;
        relay->last_switch_ms = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGD(TAG, "Relay %d (GPIO %d) turned OFF", relay->id, relay->gpio_pin);
    }

    return err;
}

bool relay_gpio_get_state(const relay_t* relay) {
    if (!relay || !relay->initialized) {
        return false;
    }

    if (relay->type != RELAY_TYPE_GPIO) {
        return false;
    }

    // Read actual GPIO state
    int level = gpio_get_level((gpio_num_t)relay->gpio_pin);

    // Convert to ON/OFF based on active_high setting
    if (relay->active_high) {
        return (level == 1);
    } else {
        return (level == 0);
    }
}

esp_err_t relay_gpio_deinit(relay_t* relay) {
    if (!relay) {
        return ESP_ERR_INVALID_ARG;
    }

    if (relay->type != RELAY_TYPE_GPIO || !relay->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Turn off first
    relay_gpio_turn_off(relay);

    // Reset GPIO to input (safer state)
    gpio_reset_pin((gpio_num_t)relay->gpio_pin);

    relay->initialized = false;

    ESP_LOGI(TAG, "Relay %d (GPIO %d) deinitialized", relay->id, relay->gpio_pin);

    return ESP_OK;
}
