/**
 * @file dimmerlink_manager.c
 * @brief DimmerLink device manager with polling task
 */

#include "dimmerlink_manager.h"
#include "dimmerlink_device.h"
#include "dimmerlink_regs.h"
#include "i2c_bus.h"
#include "sdkconfig.h"
#include "acrouter_events.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char* TAG = "DL_Mgr";

/* NVS namespace */
#define DL_NVS_NAMESPACE    "dimmerlink"

/* Polling task config */
#define DL_TASK_STACK       4096
#define DL_TASK_PRIORITY    5

/* ================================================================
 * State
 * ================================================================ */

static dl_device_state_t s_devices[DL_MAX_DEVICES];
static bool s_initialized = false;
static TaskHandle_t s_poll_task = NULL;
static uint16_t s_poll_interval_ms = DL_DEFAULT_POLL_MS;
static volatile bool s_poll_running = false;
static volatile bool s_paused = false;  /* quiescent for on-demand discovery */

/* Timing instrumentation: I2C poll-cycle duration (all enabled devices). */
static volatile uint32_t s_poll_last_us = 0;
static volatile uint32_t s_poll_avg_us  = 0;
static volatile uint32_t s_poll_count   = 0;

/* ================================================================
 * Internal: Poll one device
 * ================================================================ */

static void poll_device(uint8_t slot) {
    dl_device_state_t* dev = &s_devices[slot];
    if (!dev->config.enabled) return;

    uint8_t bus = dev->config.i2c_bus;
    uint8_t addr = dev->config.i2c_addr;
    esp_err_t err;

    /* Read current sensor snapshot (primary data) */
    err = dl_device_read_current(bus, addr, &dev->current);
    if (err != ESP_OK) {
        dev->error_count++;
        if (dev->error_count >= DL_MAX_ERRORS && dev->online) {
            dev->online = false;
            ESP_LOGW(TAG, "Device %d (0x%02X) offline after %lu errors",
                     slot, addr, (unsigned long)dev->error_count);
        }
        return;
    }

    /* Device is responding */
    if (!dev->online) {
        ESP_LOGI(TAG, "Device %d (0x%02X) online", slot, addr);
    }
    dev->online = true;
    dev->error_count = 0;
    dev->last_poll_ms = (uint32_t)(esp_timer_get_time() / 1000);

    /* Read voltage if this device has voltage sensor */
    if (dev->config.role == DL_ROLE_VOLTAGE) {
        dl_device_read_voltage(bus, addr, &dev->voltage);
    }

    /* Read thermal if available (non-critical, ignore errors) */
    dl_device_read_thermal(bus, addr, &dev->thermal);

    /* Read dimmer status if this is a dimmer */
    if (dev->config.role == DL_ROLE_DIMMER) {
        dl_device_read_dimmer(bus, addr, &dev->dimmer);
    }

    /* Post event for sensor roles */
    if (dev->current.valid && dev->config.role >= DL_ROLE_CURRENT_GRID
                           && dev->config.role <= DL_ROLE_VOLTAGE) {
        acrouter_measurements_t meas;
        acrouter_measurements_init(&meas);

        meas.source = ACROUTER_SOURCE_I2C;
        meas.source_id = slot;
        meas.timestamp_us = esp_timer_get_time();
        meas.valid = true;

        /* Map role to channel */
        float current_a = (float)dev->current.rms_ma / 1000.0f;

        switch (dev->config.role) {
            case DL_ROLE_CURRENT_GRID:
                meas.current_rms[ACROUTER_CH_GRID] = current_a;
                meas.has_current[ACROUTER_CH_GRID] = true;
                meas.direction[ACROUTER_CH_GRID] =
                    dev->current.direction > 0 ? ACROUTER_DIR_CONSUMING :
                    dev->current.direction < 0 ? ACROUTER_DIR_SUPPLYING :
                    ACROUTER_DIR_ZERO;
                /* If voltage available, calculate power */
                if (dev->voltage.available && dev->voltage.rms_v > 1.0f) {
                    float power = dev->voltage.rms_v * current_a;
                    if (dev->current.direction < 0) power = -power;
                    meas.power_active[ACROUTER_CH_GRID] = power;
                    meas.has_power[ACROUTER_CH_GRID] = true;
                    meas.voltage_rms = dev->voltage.rms_v;
                    meas.has_voltage = true;
                }
                break;

            case DL_ROLE_CURRENT_SOLAR:
                meas.current_rms[ACROUTER_CH_SOLAR] = current_a;
                meas.has_current[ACROUTER_CH_SOLAR] = true;
                meas.direction[ACROUTER_CH_SOLAR] = ACROUTER_DIR_SUPPLYING;
                break;

            case DL_ROLE_CURRENT_LOAD:
                meas.current_rms[ACROUTER_CH_LOAD] = current_a;
                meas.has_current[ACROUTER_CH_LOAD] = true;
                meas.direction[ACROUTER_CH_LOAD] = ACROUTER_DIR_CONSUMING;
                break;

            case DL_ROLE_VOLTAGE:
                if (dev->voltage.available) {
                    meas.voltage_rms = dev->voltage.rms_v;
                    meas.has_voltage = true;
                }
                break;

            default:
                break;
        }

        esp_event_post(ACROUTER_EVENT, ACROUTER_EVENT_POWER_UPDATE,
                       &meas, sizeof(meas), 0);
    }
}

/* ================================================================
 * Polling Task
 * ================================================================ */

static void dl_poll_task(void* param) {
    ESP_LOGI(TAG, "Polling task started (interval=%dms)", s_poll_interval_ms);

    // Subscribe to the Task-WDT so an I2C bus lock on the DimmerLink path
    // triggers auto-reboot instead of hanging this task silently (mirrors
    // rbamp_source). Best-effort: warn and continue if the TWDT isn't enabled.
    bool wdt = (esp_task_wdt_add(NULL) == ESP_OK);
    if (!wdt) {
        ESP_LOGW(TAG, "task-WDT subscribe failed; continuing without it");
    }

    while (s_poll_running) {
        if (s_paused) {
            /* Quiescent bus for on-demand discovery — skip polling. */
            if (wdt) esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        for (uint8_t i = 0; i < DL_MAX_DEVICES && s_poll_running; i++) {
            if (s_devices[i].config.enabled) {
                poll_device(i);
            }
        }
        uint32_t dt = (uint32_t)(esp_timer_get_time() - t0);
        s_poll_last_us = dt;
        s_poll_avg_us  = s_poll_avg_us ? (s_poll_avg_us * 7 + dt) / 8 : dt;  // EMA/8
        s_poll_count++;
        if (wdt) esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(s_poll_interval_ms));
    }

    if (wdt) esp_task_wdt_delete(NULL);
    ESP_LOGI(TAG, "Polling task stopped");
    s_poll_task = NULL;
    vTaskDelete(NULL);
}

/* ================================================================
 * Public API
 * ================================================================ */

esp_err_t dl_manager_init(void) {
    memset(s_devices, 0, sizeof(s_devices));
    s_initialized = true;
    ESP_LOGI(TAG, "DimmerLink manager initialized (%d slots)", DL_MAX_DEVICES);
    return ESP_OK;
}

bool dl_manager_is_initialized(void) {
    return s_initialized;
}

esp_err_t dl_manager_register(uint8_t slot, const dl_device_config_t* config) {
    if (slot >= DL_MAX_DEVICES || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_devices[slot], 0, sizeof(dl_device_state_t));
    memcpy(&s_devices[slot].config, config, sizeof(dl_device_config_t));

    ESP_LOGI(TAG, "Registered device %d: addr=0x%02X, bus=%d, role=%d, name='%s'",
             slot, config->i2c_addr, config->i2c_bus, config->role, config->name);
    return ESP_OK;
}

esp_err_t dl_manager_unregister(uint8_t slot) {
    if (slot >= DL_MAX_DEVICES) return ESP_ERR_INVALID_ARG;
    memset(&s_devices[slot], 0, sizeof(dl_device_state_t));
    return ESP_OK;
}

esp_err_t dl_manager_start_polling(uint16_t interval_ms) {
    if (s_poll_running) {
        ESP_LOGW(TAG, "Polling already running");
        return ESP_ERR_INVALID_STATE;
    }

    s_poll_interval_ms = interval_ms > 0 ? interval_ms : DL_DEFAULT_POLL_MS;
    s_poll_running = true;

    /* Control/sensor plane → APP_CPU (core 1) on dual-core (ESP32), off the WiFi/comms
     * core; no affinity on single-core (C2). Was pinned to core 0 (comms) before. */
#if !CONFIG_FREERTOS_UNICORE
    const BaseType_t dl_core = 1;
#else
    const BaseType_t dl_core = tskNO_AFFINITY;
#endif
    BaseType_t ret = xTaskCreatePinnedToCore(
        dl_poll_task, "dl_poll", DL_TASK_STACK, NULL,
        DL_TASK_PRIORITY, &s_poll_task, dl_core);

    if (ret != pdPASS) {
        s_poll_running = false;
        ESP_LOGE(TAG, "Failed to create polling task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t dl_manager_stop_polling(void) {
    if (!s_poll_running) return ESP_OK;
    s_poll_running = false;
    /* Task will self-delete on next iteration */
    return ESP_OK;
}

bool dl_manager_is_polling(void) {
    return s_poll_running;
}

void dl_manager_get_timing(uint32_t *last_us, uint32_t *avg_us, uint32_t *count) {
    if (last_us) *last_us = s_poll_last_us;
    if (avg_us)  *avg_us  = s_poll_avg_us;
    if (count)   *count   = s_poll_count;
}

const dl_device_state_t* dl_manager_get_device(uint8_t slot) {
    if (slot >= DL_MAX_DEVICES) return NULL;
    return &s_devices[slot];
}

uint8_t dl_manager_get_active_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < DL_MAX_DEVICES; i++) {
        if (s_devices[i].config.enabled && s_devices[i].online) count++;
    }
    return count;
}

uint8_t dl_manager_get_enabled_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < DL_MAX_DEVICES; i++) {
        if (s_devices[i].config.enabled) count++;
    }
    return count;
}

/* ================================================================
 * NVS Persistence
 * ================================================================ */

esp_err_t dl_manager_save_config(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(DL_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    for (uint8_t i = 0; i < DL_MAX_DEVICES; i++) {
        const dl_device_config_t* cfg = &s_devices[i].config;
        char key[16];

        snprintf(key, sizeof(key), "d%d_addr", i);
        nvs_set_u8(nvs, key, cfg->i2c_addr);

        snprintf(key, sizeof(key), "d%d_bus", i);
        nvs_set_u8(nvs, key, cfg->i2c_bus);

        snprintf(key, sizeof(key), "d%d_role", i);
        nvs_set_u8(nvs, key, (uint8_t)cfg->role);

        snprintf(key, sizeof(key), "d%d_en", i);
        nvs_set_u8(nvs, key, cfg->enabled ? 1 : 0);

        snprintf(key, sizeof(key), "d%d_name", i);
        nvs_set_str(nvs, key, cfg->name);
    }

    err = nvs_commit(nvs);
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Configuration saved to NVS");
    }
    return err;
}

esp_err_t dl_manager_change_address(uint8_t cur_addr, uint8_t new_addr) {
    if (new_addr < 0x08 || new_addr > 0x77 || new_addr == cur_addr) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Locate the registered device to learn its bus + migrate its config.
     * Fall back to bus 0 if the target is not (yet) a registered device. */
    int slot = -1;
    for (uint8_t i = 0; i < DL_MAX_DEVICES; i++) {
        if (s_devices[i].config.enabled && s_devices[i].config.i2c_addr == cur_addr) {
            slot = i;
            break;
        }
    }
    uint8_t bus = (slot >= 0) ? s_devices[slot].config.i2c_bus : 0;

    /* Best-effort collision guard: refuse if the target address already answers. */
    uint8_t probe = 0;
    if (i2c_bus_read_byte(bus, new_addr, DL_REG_STATUS, &probe) == ESP_OK) {
        ESP_LOGW(TAG, "address 0x%02X already in use — refusing", new_addr);
        return ESP_ERR_INVALID_STATE;
    }

    /* Stage the new address (0x30), then RESET to apply. Legacy firmware latches
     * 0x30 on reset — the module re-enumerates at new_addr. */
    esp_err_t err = i2c_bus_write_byte(bus, cur_addr, DL_REG_I2C_ADDRESS, new_addr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "staging address 0x30 failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Migrate + persist the config before the reset so we track the new addr. */
    if (slot >= 0) {
        s_devices[slot].config.i2c_addr = new_addr;
        dl_manager_save_config();
    }

    dl_device_send_command(bus, cur_addr, DL_CMD_RESET);
    ESP_LOGI(TAG, "DimmerLink address 0x%02X -> 0x%02X (reset issued; applies on reset)",
             cur_addr, new_addr);
    return ESP_OK;
}

void dl_manager_pause(bool pause) {
    s_paused = pause;
}

esp_err_t dl_manager_load_config(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(DL_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved DimmerLink config found");
        return ESP_OK;  /* Not an error — first boot */
    }
    if (err != ESP_OK) return err;

    uint8_t loaded = 0;
    for (uint8_t i = 0; i < DL_MAX_DEVICES; i++) {
        dl_device_config_t* cfg = &s_devices[i].config;
        char key[16];
        uint8_t u8val;

        snprintf(key, sizeof(key), "d%d_en", i);
        if (nvs_get_u8(nvs, key, &u8val) != ESP_OK) continue;
        cfg->enabled = (u8val != 0);
        if (!cfg->enabled) continue;

        snprintf(key, sizeof(key), "d%d_addr", i);
        nvs_get_u8(nvs, key, &cfg->i2c_addr);

        snprintf(key, sizeof(key), "d%d_bus", i);
        nvs_get_u8(nvs, key, &cfg->i2c_bus);

        snprintf(key, sizeof(key), "d%d_role", i);
        if (nvs_get_u8(nvs, key, &u8val) == ESP_OK) {
            cfg->role = (dl_role_t)u8val;
        }

        snprintf(key, sizeof(key), "d%d_name", i);
        size_t len = sizeof(cfg->name);
        nvs_get_str(nvs, key, cfg->name, &len);

        loaded++;
        ESP_LOGI(TAG, "Loaded device %d: addr=0x%02X, role=%d, name='%s'",
                 i, cfg->i2c_addr, cfg->role, cfg->name);
    }

    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded %d DimmerLink device(s) from NVS", loaded);
    return ESP_OK;
}
