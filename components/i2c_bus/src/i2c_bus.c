/**
 * @file i2c_bus.c
 * @brief Shared I2C bus manager implementation
 *
 * Uses ESP-IDF 5.x i2c_master new driver API.
 * The new driver provides internal bus locking for thread safety.
 */

#include "i2c_bus.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "I2C_Bus";

/** I2C transaction timeout */
#define I2C_TIMEOUT_MS      100
/* Bus-scan probe timeout. A present device ACKs the address phase in microseconds;
 * only EMPTY addresses stall to the timeout. With the full 0x08..0x77 walk (112
 * addrs) each probed twice (bare probe + DimmerLink register-write fallback),
 * 100ms/addr would freeze the caller ~20s. 10ms keeps a full scan ~2s while still
 * being ample for any real slave to respond. */
#define I2C_SCAN_PROBE_TIMEOUT_MS  10
#define I2C_MAX_WRITE_LEN   32   /* max register-write payload; bounds the write buffer */

/** Bus state */
typedef struct {
    i2c_master_bus_handle_t bus_handle;
    bool initialized;
    int sda_pin;
    int scl_pin;
    uint32_t freq_hz;
} i2c_bus_state_t;

static i2c_bus_state_t s_buses[I2C_BUS_MAX] = {0};

/**
 * @brief Add a device to the bus (internal helper)
 *
 * Creates a temporary device handle for a single transaction.
 * The new i2c_master driver requires device registration.
 */
static esp_err_t add_device(uint8_t bus_num, uint8_t dev_addr,
                            i2c_master_dev_handle_t* dev_handle) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = s_buses[bus_num].freq_hz,
    };
    return i2c_master_bus_add_device(s_buses[bus_num].bus_handle, &dev_cfg, dev_handle);
}

// ================================================================
// Lifecycle
// ================================================================

esp_err_t i2c_bus_init(uint8_t bus_num, int sda_pin, int scl_pin, uint32_t freq_hz) {
    if (bus_num >= I2C_BUS_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_buses[bus_num].initialized) {
        ESP_LOGW(TAG, "Bus %d already initialized", bus_num);
        return ESP_ERR_INVALID_STATE;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = (i2c_port_num_t)bus_num,
        .sda_io_num = (gpio_num_t)sda_pin,
        .scl_io_num = (gpio_num_t)scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_buses[bus_num].bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus %d: %s", bus_num, esp_err_to_name(err));
        return err;
    }

    s_buses[bus_num].initialized = true;
    s_buses[bus_num].sda_pin = sda_pin;
    s_buses[bus_num].scl_pin = scl_pin;
    s_buses[bus_num].freq_hz = freq_hz;

    ESP_LOGI(TAG, "I2C bus %d initialized: SDA=%d, SCL=%d, %lu Hz",
             bus_num, sda_pin, scl_pin, (unsigned long)freq_hz);
    return ESP_OK;
}

esp_err_t i2c_bus_deinit(uint8_t bus_num) {
    if (bus_num >= I2C_BUS_MAX || !s_buses[bus_num].initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2c_del_master_bus(s_buses[bus_num].bus_handle);
    if (err == ESP_OK) {
        s_buses[bus_num].initialized = false;
        s_buses[bus_num].bus_handle = NULL;
        ESP_LOGI(TAG, "I2C bus %d deinitialized", bus_num);
    }
    return err;
}

bool i2c_bus_is_initialized(uint8_t bus_num) {
    if (bus_num >= I2C_BUS_MAX) return false;
    return s_buses[bus_num].initialized;
}

i2c_master_bus_handle_t i2c_bus_get_handle(uint8_t bus_num) {
    if (bus_num >= I2C_BUS_MAX || !s_buses[bus_num].initialized) {
        return NULL;
    }
    return s_buses[bus_num].bus_handle;
}

// ================================================================
// Read/Write Operations
// ================================================================

esp_err_t i2c_bus_read_reg(uint8_t bus_num, uint8_t dev_addr, uint8_t reg,
                           uint8_t* data, size_t len) {
    if (bus_num >= I2C_BUS_MAX || !s_buses[bus_num].initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t dev;
    esp_err_t err = add_device(bus_num, dev_addr, &dev);
    if (err != ESP_OK) return err;

    // Write register address, then read data (with repeated START)
    err = i2c_master_transmit_receive(dev, &reg, 1, data, len, I2C_TIMEOUT_MS);

    i2c_master_bus_rm_device(dev);
    return err;
}

esp_err_t i2c_bus_read_reg_stop(uint8_t bus_num, uint8_t dev_addr, uint8_t reg,
                                uint8_t* data, size_t len) {
    if (bus_num >= I2C_BUS_MAX || !s_buses[bus_num].initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t dev;
    esp_err_t err = add_device(bus_num, dev_addr, &dev);
    if (err != ESP_OK) return err;

    // Separate transactions: write(reg)+STOP, then START+read. Some slave
    // firmwares (e.g. legacy DimmerLink) latch the register pointer only on a
    // full STOP, not on a repeated-START — a combined transmit_receive returns
    // the previous/uninitialized register there. This reads correctly.
    err = i2c_master_transmit(dev, &reg, 1, I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        err = i2c_master_receive(dev, data, len, I2C_TIMEOUT_MS);
    }

    i2c_master_bus_rm_device(dev);
    return err;
}

esp_err_t i2c_bus_write_reg(uint8_t bus_num, uint8_t dev_addr, uint8_t reg,
                            const uint8_t* data, size_t len) {
    if (bus_num >= I2C_BUS_MAX || !s_buses[bus_num].initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    // Bound the payload before touching the bus — a caller-sized stack VLA with no
    // upper bound could overrun the (4 KB) poll-task stacks, and len==SIZE_MAX wraps
    // to a 0-length buffer (D9). Checked before add_device so no device handle leaks.
    if (len > I2C_MAX_WRITE_LEN || (len > 0 && !data)) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t dev;
    esp_err_t err = add_device(bus_num, dev_addr, &dev);
    if (err != ESP_OK) return err;

    // Combine register address + data into one write (fixed, bounded buffer)
    uint8_t buf[I2C_MAX_WRITE_LEN + 1];
    buf[0] = reg;
    if (data && len > 0) {
        memcpy(&buf[1], data, len);
    }

    err = i2c_master_transmit(dev, buf, len + 1, I2C_TIMEOUT_MS);

    i2c_master_bus_rm_device(dev);
    return err;
}

esp_err_t i2c_bus_write_byte(uint8_t bus_num, uint8_t dev_addr, uint8_t reg,
                             uint8_t value) {
    return i2c_bus_write_reg(bus_num, dev_addr, reg, &value, 1);
}

esp_err_t i2c_bus_read_byte(uint8_t bus_num, uint8_t dev_addr, uint8_t reg,
                            uint8_t* value) {
    return i2c_bus_read_reg(bus_num, dev_addr, reg, value, 1);
}

// ================================================================
// Bus Scan
// ================================================================

esp_err_t i2c_bus_scan(uint8_t bus_num, uint8_t* found_addrs, uint8_t max_addrs,
                       uint8_t* found_count) {
    if (bus_num >= I2C_BUS_MAX || !s_buses[bus_num].initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!found_addrs || !found_count) {
        return ESP_ERR_INVALID_ARG;
    }

    *found_count = 0;
    ESP_LOGI(TAG, "Scanning I2C bus %d...", bus_num);

    for (uint8_t addr = 0x08; addr <= 0x77 && *found_count < max_addrs; addr++) {
        esp_err_t err = i2c_master_probe(s_buses[bus_num].bus_handle, addr, I2C_SCAN_PROBE_TIMEOUT_MS);
        if (err != ESP_OK) {
            // Fallback: some legacy slaves (e.g. the DimmerLink firmware) do not
            // ACK a bare zero-length probe cleanly on every SoC, yet ACK a real
            // transaction. Retry with a 1-byte register-pointer write (harmless:
            // just sets the read pointer to reg 0) and treat an ACK as present.
            i2c_master_dev_handle_t dev;
            if (add_device(bus_num, addr, &dev) == ESP_OK) {
                uint8_t reg0 = 0x00;
                err = i2c_master_transmit(dev, &reg0, 1, I2C_SCAN_PROBE_TIMEOUT_MS);
                i2c_master_bus_rm_device(dev);
            }
        }
        if (err == ESP_OK) {
            found_addrs[*found_count] = addr;
            (*found_count)++;
            ESP_LOGI(TAG, "  Found device at 0x%02X", addr);
        }
    }

    ESP_LOGI(TAG, "Scan complete: %d device(s) found", *found_count);
    return ESP_OK;
}
