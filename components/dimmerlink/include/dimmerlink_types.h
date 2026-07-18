/**
 * @file dimmerlink_types.h
 * @brief DimmerLink data structures
 *
 * Parsed data from DimmerLink I2C registers.
 * All values are in SI units (mA, V, °C, Hz).
 */

#ifndef DIMMERLINK_TYPES_H
#define DIMMERLINK_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Device role in the ACRouter system
 */
typedef enum {
    DL_ROLE_NONE            = 0,    ///< Not assigned
    DL_ROLE_CURRENT_GRID    = 1,    ///< Grid current sensor
    DL_ROLE_CURRENT_SOLAR   = 2,    ///< Solar current sensor
    DL_ROLE_CURRENT_LOAD    = 3,    ///< Load current sensor
    DL_ROLE_VOLTAGE         = 4,    ///< Voltage sensor
    DL_ROLE_DIMMER          = 5,    ///< TRIAC dimmer
    DL_ROLE_RELAY           = 6,    ///< Relay output
    DL_ROLE_TEMPERATURE     = 7,    ///< Temperature sensor only
} dl_role_t;

/**
 * @brief Current sensor snapshot (parsed from registers 0x60-0x77)
 */
typedef struct {
    uint16_t rms_ma;            ///< RMS current in milliamps
    uint16_t peak_ma;           ///< Peak current in milliamps
    int8_t   direction;         ///< +1=positive (consuming), -1=negative (exporting), 0=zero
    uint8_t  period_idx;        ///< Monotonic period index (wraps 0-255)
    uint32_t duration_ms;       ///< Measurement duration in ms
    uint32_t sample_count;      ///< Number of ADC samples
    uint16_t min_adc;           ///< Min ADC value
    uint16_t max_adc;           ///< Max ADC value
    int16_t  dc_offset;         ///< DC offset in ADC units
    uint16_t crest_factor;      ///< Crest factor x100 (141 = pure sine)
    uint8_t  acc_number;        ///< Accumulator number (0-7)
    bool     valid;             ///< Data valid flag
    bool     rt_mode;           ///< Real-time mode (200ms period)
} dl_current_snapshot_t;

/**
 * @brief Voltage sensor data (parsed from registers 0x78-0x7D)
 */
typedef struct {
    float    rms_v;             ///< RMS voltage in Volts (converted from 0.1V units)
    uint16_t peak_raw;          ///< Peak voltage (raw 0.1V units)
    uint8_t  ratio;             ///< Transformer ratio
    bool     available;         ///< Hardware present (VS_STATUS bit7 clear)
    bool     data_ready;        ///< Data ready (VS_STATUS bit0 set)
} dl_voltage_snapshot_t;

/**
 * @brief Thermal status (parsed from registers 0x40-0x45)
 */
typedef struct {
    int8_t   temperature_c;     ///< Current temperature (°C), converted from +50 offset
    uint8_t  state;             ///< Thermal state (0=NORMAL..5=SENSOR_FAULT)
    uint8_t  max_level;         ///< Max allowed power level (%)
    uint8_t  flags;             ///< Status flags bitmap
    int8_t   peak_c;            ///< Peak temperature (°C)
    int8_t   rate_cs;           ///< Rate of change (°C/s), converted from +128 offset
    bool     available;         ///< Thermal module present
} dl_thermal_status_t;

/**
 * @brief Dimmer status (parsed from dimmer registers)
 */
typedef struct {
    uint8_t  level_percent;     ///< Current level 0-100%
    uint8_t  curve;             ///< Curve type (0=LINEAR, 1=RMS, 2=LOG)
    uint8_t  fade_time;         ///< Fade time in 100ms units
    uint8_t  ac_freq_hz;        ///< AC frequency (50 or 60)
} dl_dimmer_status_t;

/**
 * @brief Device info (parsed from control registers)
 */
typedef struct {
    uint8_t  version;           ///< Firmware version
    uint8_t  status;            ///< STATUS register raw
    uint8_t  error;             ///< ERROR register raw
    bool     ready;             ///< STATUS.READY flag
    bool     has_error;         ///< STATUS.ERROR flag
} dl_device_info_t;

/**
 * @brief Complete device configuration for DimmerLink manager
 */
typedef struct {
    uint8_t  i2c_addr;          ///< I2C slave address
    uint8_t  i2c_bus;           ///< I2C bus number (0 or 1)
    dl_role_t role;             ///< Role in ACRouter system
    bool     enabled;           ///< Device enabled
    char     name[16];          ///< User-friendly name
} dl_device_config_t;

/**
 * @brief Complete cached state of one DimmerLink device
 */
typedef struct {
    dl_device_config_t  config;         ///< Device configuration
    dl_device_info_t    info;           ///< Device info (version, status)
    dl_current_snapshot_t current;      ///< Latest current snapshot
    dl_voltage_snapshot_t voltage;      ///< Latest voltage snapshot
    dl_thermal_status_t thermal;        ///< Latest thermal status
    dl_dimmer_status_t  dimmer;         ///< Dimmer status
    bool                online;         ///< Device responding on I2C
    uint32_t            last_poll_ms;   ///< Timestamp of last successful poll
    uint32_t            error_count;    ///< Consecutive I2C error count
} dl_device_state_t;

#ifdef __cplusplus
}
#endif

#endif /* DIMMERLINK_TYPES_H */
