/**
 * @file i2c_bus.h
 * @brief Shared I2C bus manager for DimmerLink modules
 *
 * Provides a pure C API for shared I2C bus access.
 * Uses ESP-IDF 5.x i2c_master new driver with internal locking.
 *
 * Default configuration:
 * - Bus 0: SDA=21, SCL=22, 100kHz (DimmerLink Standard Mode)
 * - Bus 1: Reserved for future use
 *
 * All DimmerLink modules (sensors, dimmers, relays) share the same bus.
 * Thread-safe: the ESP-IDF i2c_master driver handles bus arbitration.
 */

#ifndef I2C_BUS_H
#define I2C_BUS_H

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of I2C buses */
#define I2C_BUS_MAX         2

/** Default I2C configuration */
#define I2C_BUS_DEFAULT_SDA     21
#define I2C_BUS_DEFAULT_SCL     22
#define I2C_BUS_DEFAULT_FREQ    100000  /* 100 kHz - DimmerLink Standard Mode */

/**
 * @brief Initialize an I2C bus as master
 *
 * @param bus_num   Bus number (0 or 1)
 * @param sda_pin   SDA GPIO pin
 * @param scl_pin   SCL GPIO pin
 * @param freq_hz   Clock frequency in Hz (typically 100000 or 400000)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if bus_num >= I2C_BUS_MAX,
 *         ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t i2c_bus_init(uint8_t bus_num, int sda_pin, int scl_pin, uint32_t freq_hz);

/**
 * @brief Deinitialize an I2C bus
 *
 * @param bus_num   Bus number
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_deinit(uint8_t bus_num);

/**
 * @brief Check if a bus is initialized
 *
 * @param bus_num   Bus number
 * @return true if initialized and ready
 */
bool i2c_bus_is_initialized(uint8_t bus_num);

/**
 * @brief Read registers from an I2C device
 *
 * Performs: START → [addr+W] → [reg] → RESTART → [addr+R] → data... → STOP
 *
 * @param bus_num   Bus number
 * @param dev_addr  7-bit device address
 * @param reg       Register address to read from
 * @param data      Buffer to receive data
 * @param len       Number of bytes to read
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on bus timeout,
 *         ESP_ERR_NOT_FOUND if device does not ACK
 */
esp_err_t i2c_bus_read_reg(uint8_t bus_num, uint8_t dev_addr, uint8_t reg,
                           uint8_t* data, size_t len);

/**
 * @brief Read registers using SEPARATE transactions (write(reg)+STOP, then read).
 *
 * Unlike i2c_bus_read_reg (combined repeated-START), this issues a full STOP
 * after the register pointer write. Required by slave firmware that latches the
 * register pointer only on STOP (e.g. legacy DimmerLink) — the combined form
 * returns the previous/uninitialized register there.
 */
esp_err_t i2c_bus_read_reg_stop(uint8_t bus_num, uint8_t dev_addr, uint8_t reg,
                                uint8_t* data, size_t len);

/**
 * @brief Write data to registers of an I2C device
 *
 * Performs: START → [addr+W] → [reg] → data... → STOP
 *
 * @param bus_num   Bus number
 * @param dev_addr  7-bit device address
 * @param reg       Register address to write to
 * @param data      Data buffer to write
 * @param len       Number of bytes to write
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_write_reg(uint8_t bus_num, uint8_t dev_addr, uint8_t reg,
                            const uint8_t* data, size_t len);

/**
 * @brief Write a single byte to a register
 *
 * @param bus_num   Bus number
 * @param dev_addr  7-bit device address
 * @param reg       Register address
 * @param value     Byte value to write
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_write_byte(uint8_t bus_num, uint8_t dev_addr, uint8_t reg,
                             uint8_t value);

/**
 * @brief Read a single byte from a register
 *
 * @param bus_num   Bus number
 * @param dev_addr  7-bit device address
 * @param reg       Register address
 * @param value     Pointer to receive byte value
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_read_byte(uint8_t bus_num, uint8_t dev_addr, uint8_t reg,
                            uint8_t* value);

/**
 * @brief Scan I2C bus for responding devices
 *
 * Probes addresses 0x08-0x77 and returns list of responding devices.
 *
 * @param bus_num       Bus number
 * @param found_addrs   Buffer to receive found device addresses
 * @param max_addrs     Maximum number of addresses to return
 * @param found_count   Pointer to receive actual number found
 * @return ESP_OK on success (even if no devices found)
 */
esp_err_t i2c_bus_scan(uint8_t bus_num, uint8_t* found_addrs, uint8_t max_addrs,
                       uint8_t* found_count);

/**
 * @brief Get the raw i2c_master bus handle for a bus
 *
 * For libraries that need direct access to the ESP-IDF new-driver bus handle
 * (e.g. the rbAmp component, which manages its own device handles on the
 * shared bus). The bus is owned by i2c_bus — callers must NOT delete it.
 *
 * @param bus_num   Bus number
 * @return Bus handle, or NULL if the bus is not initialized
 */
i2c_master_bus_handle_t i2c_bus_get_handle(uint8_t bus_num);

#ifdef __cplusplus
}
#endif

#endif // I2C_BUS_H
