/**
 * @file dimmerlink_device.c
 * @brief DimmerLink single device I2C operations
 */

#include "dimmerlink_device.h"
#include "dimmerlink_regs.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "DL_Dev";

/* Helper: read uint16 little-endian from buffer */
static inline uint16_t le16(const uint8_t* buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/* Helper: read uint32 little-endian from buffer */
static inline uint32_t le32(const uint8_t* buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

/* Helper: read int16 little-endian from buffer */
static inline int16_t le16s(const uint8_t* buf) {
    return (int16_t)le16(buf);
}

/* ================================================================ */

esp_err_t dl_device_probe(uint8_t bus, uint8_t addr) {
    uint8_t status;
    return i2c_bus_read_byte(bus, addr, DL_REG_STATUS, &status);
}

esp_err_t dl_device_read_info(uint8_t bus, uint8_t addr, dl_device_info_t* info) {
    uint8_t buf[4];
    esp_err_t err = i2c_bus_read_reg(bus, addr, DL_REG_STATUS, buf, 4);
    if (err != ESP_OK) return err;

    info->status    = buf[0];
    info->error     = buf[2];
    info->version   = buf[3];
    info->ready     = (buf[0] & DL_STATUS_READY) != 0;
    info->has_error = (buf[0] & DL_STATUS_ERROR) != 0;
    return ESP_OK;
}

esp_err_t dl_device_read_current(uint8_t bus, uint8_t addr, dl_current_snapshot_t* snap) {
    uint8_t buf[DL_CS_SNAPSHOT_SIZE];
    esp_err_t err = i2c_bus_read_reg(bus, addr, DL_REG_CS0_STATUS, buf, DL_CS_SNAPSHOT_SIZE);
    if (err != ESP_OK) {
        snap->valid = false;
        return err;
    }

    /* Parse snapshot from raw bytes (little-endian) */
    uint8_t status = buf[0];
    snap->valid        = (status & DL_CS_STATUS_VALID) != 0;
    snap->rt_mode      = (status & DL_CS_STATUS_RT_MODE) != 0;
    snap->acc_number   = (status & DL_CS_STATUS_ACC_MASK) >> DL_CS_STATUS_ACC_SHIFT;

    snap->rms_ma       = le16(&buf[1]);     /* 0x61-0x62 */
    snap->peak_ma      = le16(&buf[3]);     /* 0x63-0x64 */
    snap->direction    = (int8_t)buf[5];    /* 0x65 */
    snap->period_idx   = buf[6];            /* 0x66 */
    snap->duration_ms  = le32(&buf[7]);     /* 0x67-0x6A */
    snap->sample_count = le32(&buf[11]);    /* 0x6B-0x6E */
    snap->min_adc      = le16(&buf[15]);    /* 0x6F-0x70 */
    snap->max_adc      = le16(&buf[17]);    /* 0x71-0x72 */
    snap->dc_offset    = le16s(&buf[19]);   /* 0x73-0x74 */
    snap->crest_factor = le16(&buf[21]);    /* 0x75-0x76 */
    /* buf[23] = reserved */

    return ESP_OK;
}

esp_err_t dl_device_read_voltage(uint8_t bus, uint8_t addr, dl_voltage_snapshot_t* snap) {
    uint8_t buf[6];
    esp_err_t err = i2c_bus_read_reg(bus, addr, DL_REG_VS_STATUS, buf, 6);
    if (err != ESP_OK) {
        snap->available = false;
        return err;
    }

    snap->available   = (buf[0] & DL_VS_NO_HW) == 0;
    snap->data_ready  = (buf[0] & DL_VS_DATA_READY) != 0;
    snap->rms_v       = (float)le16(&buf[1]) * 0.1f;  /* 0.1V units → V */
    snap->peak_raw    = le16(&buf[3]);
    snap->ratio       = buf[5];
    return ESP_OK;
}

esp_err_t dl_device_read_thermal(uint8_t bus, uint8_t addr, dl_thermal_status_t* status) {
    uint8_t buf[6];
    esp_err_t err = i2c_bus_read_reg(bus, addr, DL_REG_TEMP_CURRENT, buf, 6);
    if (err != ESP_OK) {
        status->available = false;
        return err;
    }

    status->temperature_c = (int8_t)buf[0] - 50;    /* +50 offset → °C */
    status->state         = buf[1];
    status->max_level     = buf[2];
    status->flags         = buf[3];
    status->peak_c        = (int8_t)buf[4] - 50;
    status->rate_cs       = (int8_t)buf[5] - 128;   /* +128 offset → °C/s */
    status->available     = true;
    return ESP_OK;
}

esp_err_t dl_device_read_dimmer(uint8_t bus, uint8_t addr, dl_dimmer_status_t* status) {
    esp_err_t err;
    uint8_t val;

    err = i2c_bus_read_byte(bus, addr, DL_REG_DIM0_LEVEL, &val);
    if (err != ESP_OK) return err;
    status->level_percent = val;

    err = i2c_bus_read_byte(bus, addr, DL_REG_DIM0_CURVE, &val);
    if (err != ESP_OK) return err;
    status->curve = val;

    err = i2c_bus_read_byte(bus, addr, DL_REG_DIM0_FADE_TIME, &val);
    if (err != ESP_OK) return err;
    status->fade_time = val;

    err = i2c_bus_read_byte(bus, addr, DL_REG_AC_FREQ, &val);
    if (err != ESP_OK) return err;
    status->ac_freq_hz = val;

    return ESP_OK;
}

/* ================================================================
 * Write Operations
 * ================================================================ */

esp_err_t dl_device_set_dimmer_level(uint8_t bus, uint8_t addr, uint8_t percent) {
    if (percent > 100) percent = 100;
    return i2c_bus_write_byte(bus, addr, DL_REG_DIM0_LEVEL, percent);
}

esp_err_t dl_device_set_dimmer_fade(uint8_t bus, uint8_t addr,
                                     uint8_t percent, uint8_t fade_100ms) {
    if (percent > 100) percent = 100;
    /* Set fade time first, then level triggers the fade */
    esp_err_t err = i2c_bus_write_byte(bus, addr, DL_REG_DIM0_FADE_TIME, fade_100ms);
    if (err != ESP_OK) return err;
    return i2c_bus_write_byte(bus, addr, DL_REG_DIM0_LEVEL, percent);
}

esp_err_t dl_device_set_dimmer_curve(uint8_t bus, uint8_t addr, uint8_t curve) {
    return i2c_bus_write_byte(bus, addr, DL_REG_DIM0_CURVE, curve);
}

esp_err_t dl_device_send_command(uint8_t bus, uint8_t addr, uint8_t cmd) {
    return i2c_bus_write_byte(bus, addr, DL_REG_COMMAND, cmd);
}

esp_err_t dl_device_select_accumulator(uint8_t bus, uint8_t addr, uint8_t acc_num) {
    if (acc_num > 7) return ESP_ERR_INVALID_ARG;
    return i2c_bus_write_byte(bus, addr, DL_REG_ACC_SEL, acc_num);
}

esp_err_t dl_device_commit_accumulator(uint8_t bus, uint8_t addr, uint8_t acc_num) {
    if (acc_num > 7) return ESP_ERR_INVALID_ARG;
    return i2c_bus_write_byte(bus, addr, DL_REG_COMMIT, acc_num);
}
