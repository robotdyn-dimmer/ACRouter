/**
 * @file dimmer_i2c.c
 * @brief DimmerLink I2C backend for dimmer manager
 */

#include "dimmer_i2c.h"
#include "dimmerlink_device.h"
#include "dimmerlink_regs.h"
#include "i2c_bus.h"
#include "esp_log.h"

static const char* TAG = "DimmerI2C";

/* Map dimmer_curve_t to DimmerLink DL_CURVE_* */
static uint8_t map_curve(dimmer_curve_t c) {
    switch (c) {
        case DIMMER_CURVE_RMS:         return DL_CURVE_RMS;
        case DIMMER_CURVE_LOGARITHMIC: return DL_CURVE_LOG;
        default:                       return DL_CURVE_LINEAR;
    }
}

esp_err_t dimmer_i2c_init(void) {
    if (!i2c_bus_is_initialized(0)) {
        ESP_LOGW(TAG, "I2C bus 0 not initialized — I2C dimmers unavailable");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "I2C dimmer backend ready");
    return ESP_OK;
}

esp_err_t dimmer_i2c_channel_init(dimmer_t* d) {
    if (!d) return ESP_ERR_INVALID_ARG;

    uint8_t bus  = d->i2c_bus;
    uint8_t addr = d->i2c_address;

    /* Probe device */
    esp_err_t err = dl_device_probe(bus, addr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Dimmer %d: DimmerLink not found at bus=%d addr=0x%02X",
                 d->id, bus, addr);
        return ESP_ERR_NOT_FOUND;
    }

    /* Read firmware version */
    dl_device_info_t info;
    dl_device_read_info(bus, addr, &info);
    ESP_LOGI(TAG, "Dimmer %d: DimmerLink v%d at 0x%02X (bus %d), ready=%d",
             d->id, info.version, addr, bus, info.ready);

    /* Set initial curve and level */
    dl_device_set_dimmer_curve(bus, addr, map_curve(d->curve));
    dl_device_set_dimmer_level(bus, addr, d->level_percent);

    d->state = DIMMER_STATE_OFF;
    ESP_LOGI(TAG, "Dimmer %d I2C channel initialized (addr=0x%02X)", d->id, addr);
    return ESP_OK;
}

esp_err_t dimmer_i2c_set_level(dimmer_t* d, uint8_t percent) {
    if (!d) return ESP_ERR_INVALID_ARG;
    esp_err_t err = dl_device_set_dimmer_level(d->i2c_bus, d->i2c_address, percent);
    if (err == ESP_OK) {
        d->level_percent = percent;
        d->state = (percent == 0) ? DIMMER_STATE_OFF : DIMMER_STATE_ON;
    }
    return err;
}

esp_err_t dimmer_i2c_set_level_smooth(dimmer_t* d, uint8_t percent, uint32_t ms) {
    if (!d) return ESP_ERR_INVALID_ARG;
    /* DimmerLink fade_time is in 100ms units */
    uint8_t fade_100ms = (uint8_t)((ms + 50) / 100);
    if (fade_100ms == 0) fade_100ms = 1;

    esp_err_t err = dl_device_set_dimmer_fade(d->i2c_bus, d->i2c_address, percent, fade_100ms);
    if (err == ESP_OK) {
        d->target_percent = percent;
        d->state = DIMMER_STATE_TRANSITIONING;
    }
    return err;
}

esp_err_t dimmer_i2c_set_curve(dimmer_t* d, dimmer_curve_t curve) {
    if (!d) return ESP_ERR_INVALID_ARG;
    return dl_device_set_dimmer_curve(d->i2c_bus, d->i2c_address, map_curve(curve));
}

esp_err_t dimmer_i2c_deinit(dimmer_t* d) {
    if (!d) return ESP_ERR_INVALID_ARG;
    /* Set level to 0 for safe shutdown */
    dl_device_set_dimmer_level(d->i2c_bus, d->i2c_address, 0);
    d->state = DIMMER_STATE_OFF;
    d->level_percent = 0;
    return ESP_OK;
}
