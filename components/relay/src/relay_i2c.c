/**
 * @file relay_i2c.c
 * @brief DimmerLink I2C backend for relay manager
 */

#include "relay_i2c.h"
#include "dimmerlink_device.h"
#include "i2c_bus.h"
#include "esp_log.h"

static const char* TAG = "RelayI2C";

esp_err_t relay_i2c_init(void) {
    if (!i2c_bus_is_initialized(0)) {
        ESP_LOGW(TAG, "I2C bus 0 not initialized — I2C relays unavailable");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "I2C relay backend ready");
    return ESP_OK;
}

esp_err_t relay_i2c_begin(relay_t* r) {
    if (!r) return ESP_ERR_INVALID_ARG;

    /* Relay uses bus 0 (i2c_channel not used for DimmerLink single-channel modules) */
    esp_err_t err = dl_device_probe(0, r->i2c_addr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Relay %d: DimmerLink not found at addr=0x%02X", r->id, r->i2c_addr);
        return ESP_ERR_NOT_FOUND;
    }

    /* Ensure relay starts OFF */
    dl_device_set_dimmer_level(0, r->i2c_addr, 0);
    r->is_on = false;

    ESP_LOGI(TAG, "Relay %d I2C initialized (addr=0x%02X)", r->id, r->i2c_addr);
    return ESP_OK;
}

esp_err_t relay_i2c_turn_on(relay_t* r) {
    if (!r) return ESP_ERR_INVALID_ARG;
    esp_err_t err = dl_device_set_dimmer_level(0, r->i2c_addr, 100);
    if (err == ESP_OK) r->is_on = true;
    return err;
}

esp_err_t relay_i2c_turn_off(relay_t* r) {
    if (!r) return ESP_ERR_INVALID_ARG;
    esp_err_t err = dl_device_set_dimmer_level(0, r->i2c_addr, 0);
    if (err == ESP_OK) r->is_on = false;
    return err;
}

bool relay_i2c_get_state(const relay_t* r) {
    /* Return cached state — reading from device on every call would be slow */
    return r ? r->is_on : false;
}

esp_err_t relay_i2c_deinit(relay_t* r) {
    if (!r) return ESP_ERR_INVALID_ARG;
    dl_device_set_dimmer_level(0, r->i2c_addr, 0);
    r->is_on = false;
    return ESP_OK;
}
