/**
 * @file dimmerlink_regs.h
 * @brief DimmerLink I2C register map (CIU32-compatible)
 *
 * Complete register definitions for DimmerLink smart dimmer modules.
 * All multi-byte registers are LITTLE-ENDIAN.
 */

#ifndef DIMMERLINK_REGS_H
#define DIMMERLINK_REGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Default I2C Configuration
 * ================================================================ */
#define DL_DEFAULT_ADDR         0x50    /* Default slave address */
#define DL_I2C_SPEED_HZ        100000  /* 100 kHz Standard Mode */

/* ================================================================
 * Control Registers (0x00-0x03)
 * ================================================================ */
#define DL_REG_STATUS           0x00    /* R   uint8: Bit0=READY, Bit1=ERROR */
#define DL_REG_COMMAND          0x01    /* W   uint8: Command code */
#define DL_REG_ERROR            0x02    /* R   uint8: Error code */
#define DL_REG_VERSION          0x03    /* R   uint8: Firmware version */

/* Status register bits */
#define DL_STATUS_READY         (1 << 0)
#define DL_STATUS_ERROR         (1 << 1)

/* Command codes */
#define DL_CMD_NOP              0x00
#define DL_CMD_RESET            0x01
#define DL_CMD_RECALIBRATE      0x02
#define DL_CMD_SWITCH_UART      0x03
#define DL_CMD_CHARGE_RESET     0x05
#define DL_CMD_FACTORY_RESET    0xAA

/* Error codes */
#define DL_ERR_OK               0x00
#define DL_ERR_PARAM            0xFE
#define DL_ERR_NOT_READY        0xFC

/* ================================================================
 * Dimmer Control (0x10-0x18)
 * ================================================================ */
#define DL_REG_DIM0_LEVEL       0x10    /* RW  uint8: 0-100% */
#define DL_REG_DIM0_CURVE       0x11    /* RW  uint8: 0=LINEAR, 1=RMS, 2=LOG */
#define DL_REG_DIM0_FADE_TIME   0x18    /* RW  uint8: fade time in 100ms units */

/* Dimmer curve types */
#define DL_CURVE_LINEAR         0
#define DL_CURVE_RMS            1
#define DL_CURVE_LOG            2

/* ================================================================
 * AC Line Info (0x20-0x23)
 * ================================================================ */
#define DL_REG_AC_FREQ          0x20    /* R   uint8: 50 or 60 Hz */
#define DL_REG_AC_PERIOD_L      0x21    /* R   uint8: half-period low byte (µs) */
#define DL_REG_AC_PERIOD_H      0x22    /* R   uint8: half-period high byte (µs) */
#define DL_REG_CALIBRATION      0x23    /* R   uint8: 0=in progress, 1=done */

/* ================================================================
 * I2C Configuration (0x30)
 * ================================================================ */
#define DL_REG_I2C_ADDRESS      0x30    /* RW  uint8: slave address (0x08-0x77) */

/* ================================================================
 * Thermal Configuration (0x36-0x3B)
 * ================================================================ */
#define DL_REG_TEMP_T_WARN      0x36    /* RW  uint8: warning threshold (°C) */
#define DL_REG_TEMP_T_DERATE    0x37    /* RW  uint8: derating threshold (°C) */
#define DL_REG_TEMP_T_CRIT      0x38    /* RW  uint8: critical threshold (°C) */
#define DL_REG_TEMP_T_SHUTDOWN  0x39    /* RW  uint8: shutdown threshold (°C) */
#define DL_REG_TEMP_HYST        0x3A    /* RW  uint8: hysteresis (°C) */
#define DL_REG_TEMP_CONFIG      0x3B    /* RW  uint8: Bit0=enable, Bit1=Celsius */

/* ================================================================
 * Thermal Status (0x40-0x45)
 * Temperature value = register_value - 50 (°C)
 * ================================================================ */
#define DL_REG_TEMP_CURRENT     0x40    /* R   uint8: temp + 50 offset */
#define DL_REG_TEMP_STATE       0x41    /* R   uint8: thermal state enum */
#define DL_REG_TEMP_MAX_LEVEL   0x42    /* R   uint8: max allowed power (%) */
#define DL_REG_TEMP_FLAGS       0x43    /* R   uint8: status flags bitmap */
#define DL_REG_TEMP_PEAK        0x44    /* R   uint8: peak temp + 50 offset */
#define DL_REG_TEMP_RATE        0x45    /* R   uint8: rate °C/s + 128 offset */

/* Thermal state enum */
#define DL_THERMAL_NORMAL       0
#define DL_THERMAL_WARNING      1
#define DL_THERMAL_DERATING     2
#define DL_THERMAL_CRITICAL     3
#define DL_THERMAL_SHUTDOWN     4
#define DL_THERMAL_SENSOR_FAULT 5

/* Thermal flags bitmap */
#define DL_TFLAG_STABLE         (1 << 0)
#define DL_TFLAG_COOLING        (1 << 1)
#define DL_TFLAG_SENSOR_OK      (1 << 2)
#define DL_TFLAG_FAN_FAIL       (1 << 3)
#define DL_TFLAG_WARNING        (1 << 4)
#define DL_TFLAG_DERATED        (1 << 5)
#define DL_TFLAG_CRITICAL       (1 << 6)
#define DL_TFLAG_SHUTDOWN       (1 << 7)

/* ================================================================
 * Fan Control (0x50-0x53)
 * ================================================================ */
#define DL_REG_FAN_SPEED        0x50    /* R   uint8: current speed (%) */
#define DL_REG_FAN_TARGET       0x51    /* RW  uint8: manual target PWM (%) */
#define DL_REG_FAN_MODE         0x52    /* RW  uint8: 0=OFF,1=AUTO,2=MANUAL,3=HYBRID,4=FULL */
#define DL_REG_FAN_STATUS       0x53    /* R   uint8: status flags */

/* Fan modes */
#define DL_FAN_OFF              0
#define DL_FAN_AUTO             1
#define DL_FAN_MANUAL           2
#define DL_FAN_HYBRID           3
#define DL_FAN_FULL             4

/* ================================================================
 * Current Sensor Configuration (0x54-0x5F)
 * ================================================================ */
#define DL_REG_CS_CONFIG        0x54    /* RW  uint8: Bit0=CH0 enable */
#define DL_REG_CS0_SENSOR_TYPE  0x57    /* RW  uint8: sensitivity mV/A (66=ACS712-30A) */
#define DL_REG_ACC_SEL          0x58    /* RW  uint8: select accumulator 0-7 */
#define DL_REG_COMMIT           0x59    /* W   uint8: commit accumulator N */
#define DL_REG_CS0_MODE         0x5A    /* RW  uint8: 0=current, 1=voltage */
#define DL_REG_CS0_NOISE_FLOOR  0x5B    /* RW  uint8: ADC noise floor */

/* ================================================================
 * Current Sensor Snapshot Window (0x60-0x77) — 24 bytes
 * All multi-byte values are LITTLE-ENDIAN
 * ================================================================ */
#define DL_REG_CS0_STATUS       0x60    /* R   uint8: Bit0=valid, Bit1=rt_mode, Bits7:5=acc_n */
#define DL_REG_CS0_RMS_L        0x61    /* R   uint8: RMS mA low byte */
#define DL_REG_CS0_RMS_H        0x62    /* R   uint8: RMS mA high byte */
#define DL_REG_CS0_PEAK_L       0x63    /* R   uint8: Peak mA low byte */
#define DL_REG_CS0_PEAK_H       0x64    /* R   uint8: Peak mA high byte */
#define DL_REG_CS0_DIR          0x65    /* R   int8:  +1=positive, 0=zero, -1=negative */
#define DL_REG_CS0_PERIOD_IDX   0x66    /* R   uint8: monotonic period index (wraps 0-255) */
#define DL_REG_CS0_DUR_0        0x67    /* R   uint8: duration ms byte 0 (LSB) */
#define DL_REG_CS0_DUR_1        0x68    /* R   uint8: duration ms byte 1 */
#define DL_REG_CS0_DUR_2        0x69    /* R   uint8: duration ms byte 2 */
#define DL_REG_CS0_DUR_3        0x6A    /* R   uint8: duration ms byte 3 (MSB) */
#define DL_REG_CS0_SMPL_0       0x6B    /* R   uint8: sample count byte 0 (LSB) */
#define DL_REG_CS0_SMPL_1       0x6C    /* R   uint8: sample count byte 1 */
#define DL_REG_CS0_SMPL_2       0x6D    /* R   uint8: sample count byte 2 */
#define DL_REG_CS0_SMPL_3       0x6E    /* R   uint8: sample count byte 3 (MSB) */
#define DL_REG_CS0_MIN_L        0x6F    /* R   uint8: min ADC low byte */
#define DL_REG_CS0_MIN_H        0x70    /* R   uint8: min ADC high byte */
#define DL_REG_CS0_MAX_L        0x71    /* R   uint8: max ADC low byte */
#define DL_REG_CS0_MAX_H        0x72    /* R   uint8: max ADC high byte */
#define DL_REG_CS0_DC_L         0x73    /* R   uint8: DC offset low (signed) */
#define DL_REG_CS0_DC_H         0x74    /* R   uint8: DC offset high (signed) */
#define DL_REG_CS0_CREST_L      0x75    /* R   uint8: crest factor x100 low */
#define DL_REG_CS0_CREST_H      0x76    /* R   uint8: crest factor x100 high */
#define DL_REG_CS0_RESERVED     0x77    /* R   uint8: reserved */

/* Snapshot size */
#define DL_CS_SNAPSHOT_SIZE     24      /* Bytes from 0x60 to 0x77 inclusive */

/* CS0_STATUS bits */
#define DL_CS_STATUS_VALID      (1 << 0)
#define DL_CS_STATUS_RT_MODE    (1 << 1)
#define DL_CS_STATUS_ACC_MASK   0xE0    /* Bits 7:5 = accumulator number */
#define DL_CS_STATUS_ACC_SHIFT  5

/* ================================================================
 * Voltage Sensor (0x78-0x7D)
 * ================================================================ */
#define DL_REG_VS_STATUS        0x78    /* R   uint8: Bit7=NO_HW, Bit0=data_ready */
#define DL_REG_VS_RMS_L         0x79    /* R   uint8: voltage RMS low (0.1V units) */
#define DL_REG_VS_RMS_H         0x7A    /* R   uint8: voltage RMS high */
#define DL_REG_VS_PEAK_L        0x7B    /* R   uint8: peak voltage low */
#define DL_REG_VS_PEAK_H        0x7C    /* R   uint8: peak voltage high */
#define DL_REG_VS_RATIO         0x7D    /* RW  uint8: transformer ratio (1-255) */

/* VS_STATUS bits */
#define DL_VS_NO_HW             (1 << 7)
#define DL_VS_DATA_READY        (1 << 0)

/* ================================================================
 * Charge Accumulator (0x7E-0x85)
 * ================================================================ */
#define DL_REG_CHARGE_Q_0       0x7E    /* R   uint8: charge Q byte 0 (LSB, 0.1 mA·h) */
#define DL_REG_CHARGE_Q_1       0x7F
#define DL_REG_CHARGE_Q_2       0x80
#define DL_REG_CHARGE_Q_3       0x81    /* R   uint8: charge Q byte 3 (MSB) */
#define DL_REG_CHARGE_N_0       0x82    /* R   uint8: period count byte 0 (LSB) */
#define DL_REG_CHARGE_N_1       0x83
#define DL_REG_CHARGE_N_2       0x84
#define DL_REG_CHARGE_N_3       0x85    /* R   uint8: period count byte 3 (MSB) */

#ifdef __cplusplus
}
#endif

#endif /* DIMMERLINK_REGS_H */
