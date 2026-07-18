/**
 * @file dimmer_manager.c
 * @brief Unified dimmer management implementation
 */

#include "dimmer_manager.h"
#include "dimmer_i2c.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include "sdkconfig.h"

#if CONFIG_ACROUTER_ESPNOW_SOURCE
#include "esp_now_source.h"
#include "espnow_proto.h"
#endif

static const char* TAG = "dimmer_mgr";

/* Transport-agnostic ESP-NOW dimmer write: hub % (0..100) → wire ‰ (0..1000),
 * addressed by the node MAC bound to this slot. output_id 0 = the node's single
 * dimmer output (DimmerLink-over-ESP-NOW node). The node re-quantizes ‰→integer %
 * and reports the applied (quantized) value in its OUTPUT_STATE ACK. */
#if CONFIG_ACROUTER_ESPNOW_SOURCE
static esp_err_t dimmer_espnow_set(dimmer_t* d, uint8_t percent, uint16_t ramp_ms) {
    if (percent > 100) percent = 100;
    uint16_t permille = (uint16_t)percent * 10;   /* 0..1000‰ */
    return esp_now_source_set_output(d->espnow_mac, 0 /*output_id*/,
                                     RBN_OUT_KIND_DIMMER, permille, ramp_ms);
}
#endif

// NVS namespace
static const char* NVS_NAMESPACE = "dimmer";

// ============================================================
// Static Data
// ============================================================

static dimmer_t s_dimmers[DIMMER_MAX_COUNT];
static bool s_manager_initialized = false;

// ============================================================
// Internal Helpers
// ============================================================

static void dimmer_init_slot(uint8_t id) {
    dimmer_t* d = &s_dimmers[id];
    memset(d, 0, sizeof(dimmer_t));

    // Identity
    d->id = id;
    d->type = DIMMER_TYPE_NONE;

    // Common config defaults
    d->enabled = false;
    d->current_sensor_id = -1;
    d->curve = DIMMER_CURVE_RMS;
    d->mode = DIMMER_MODE_AUTO;
    snprintf(d->name, sizeof(d->name), "Dimmer %d", id);

    // Level limits defaults
    d->min_level = 0;
    d->max_level = 100;
    d->default_level = 0;
    d->ramp_time_ms = 0;
    d->priority = 0;  // Default: highest priority

    // GPIO defaults
    d->gpio_pin = -1;
    d->gpio_inverted = false;

    // I2C defaults
    d->i2c_address = 0;
    d->i2c_bus = 0;
    d->i2c_channel = 0;

    // ESP-NOW defaults
    memset(d->espnow_mac, 0, 6);
    d->espnow_wifi_channel = 0;
    d->espnow_timeout_ms = 1000;
    d->espnow_retry_count = 3;

    // Runtime state
    d->state = DIMMER_STATE_OFF;
    d->initialized = false;
    d->level_percent = 0;
    d->target_percent = 0;
    d->last_update_ms = 0;
    d->last_cmd_ms = 0;
    d->hw_handle = NULL;
}

/* v2.0: direct ESP32-GPIO/TRIAC dimming (rbdimmer) removed — DimmerLink (I2C)
 * only. A DIMMER_TYPE_GPIO channel is now a no-op (NOT_SUPPORTED). */
static esp_err_t dimmer_dispatch_set_level(dimmer_t* d, uint8_t percent) {
    switch (d->type) {
        case DIMMER_TYPE_I2C:
            return dimmer_i2c_set_level(d, percent);
        case DIMMER_TYPE_ESPNOW:
#if CONFIG_ACROUTER_ESPNOW_SOURCE
            return dimmer_espnow_set(d, percent, 0);
#else
            return ESP_ERR_NOT_SUPPORTED;
#endif
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

static esp_err_t dimmer_dispatch_set_level_smooth(dimmer_t* d, uint8_t percent, uint32_t ms) {
    switch (d->type) {
        case DIMMER_TYPE_I2C:
            return dimmer_i2c_set_level_smooth(d, percent, ms);
        case DIMMER_TYPE_ESPNOW:
#if CONFIG_ACROUTER_ESPNOW_SOURCE
            return dimmer_espnow_set(d, percent, (ms > 65535) ? 65535 : (uint16_t)ms);
#else
            return ESP_ERR_NOT_SUPPORTED;
#endif
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

static esp_err_t dimmer_dispatch_set_curve(dimmer_t* d, dimmer_curve_t curve) {
    switch (d->type) {
        case DIMMER_TYPE_I2C:
            return dimmer_i2c_set_curve(d, curve);
        case DIMMER_TYPE_ESPNOW:
            return ESP_OK;   /* curve is fixed on the node (RMS at its init) */
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

static esp_err_t dimmer_dispatch_init(dimmer_t* d) {
    if (!d) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err;
    switch (d->type) {
        case DIMMER_TYPE_I2C:
            err = dimmer_i2c_channel_init(d);
            break;
        case DIMMER_TYPE_ESPNOW:
            /* No hub-side hardware init; the ESP-NOW peer is added lazily on the
             * first SET_OUTPUT. The node is discovered/kept-alive by esp_now_source. */
            err = ESP_OK;
            break;
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
    /* Mark the channel initialized on any successful backend init — dimmer_set_level
     * gates on this flag, so without it every level write returned INVALID_STATE and
     * NO dimmer (I2C or ESP-NOW) was ever driven. */
    if (err == ESP_OK) {
        d->initialized = true;
    }
    return err;
}

// ============================================================
// Manager Initialization
// ============================================================

esp_err_t dimmer_manager_init(void) {
    if (s_manager_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing dimmer manager");

    // Initialize all slots
    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        dimmer_init_slot(i);
    }

    // Load configuration from NVS
    esp_err_t err = dimmer_load_all();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load config from NVS: %s", esp_err_to_name(err));
    }

    s_manager_initialized = true;

    // Deferred hardware initialization to avoid flickering at startup
    // Initializing rbdimmer channels at level=0 can cause visible flickering
    // Hardware will be auto-initialized when dimmer level is first set > 0
    uint8_t enabled_count = dimmer_get_enabled_count();

    ESP_LOGI(TAG, "Dimmer manager initialized: %d enabled (deferred HW init)", enabled_count);
    if (enabled_count > 0) {
        ESP_LOGI(TAG, "Hardware will be initialized on first dimmer_set_level() call");
    }

    return ESP_OK;
}

bool dimmer_manager_is_initialized(void) {
    return s_manager_initialized;
}

// ============================================================
// Dimmer Access
// ============================================================

dimmer_t* dimmer_get(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return NULL;
    }
    return &s_dimmers[id];
}

const dimmer_t* dimmer_get_const(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return NULL;
    }
    return &s_dimmers[id];
}

uint8_t dimmer_get_max_count(void) {
    return DIMMER_MAX_COUNT;
}

uint8_t dimmer_get_enabled_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        if (s_dimmers[i].enabled) {
            count++;
        }
    }
    return count;
}

uint8_t dimmer_get_active_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        if (s_dimmers[i].enabled && s_dimmers[i].initialized) {
            count++;
        }
    }
    return count;
}

// ============================================================
// Level Control
// ============================================================

esp_err_t dimmer_set_level(uint8_t id, uint8_t percent) {
    if (id >= DIMMER_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    dimmer_t* d = &s_dimmers[id];

    if (!d->enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    // Auto-initialize hardware if not yet initialized
    if (!d->initialized && d->type != DIMMER_TYPE_NONE) {
        ESP_LOGI(TAG, "Auto-initializing dimmer %d hardware...", id);
        esp_err_t err = dimmer_dispatch_init(d);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to auto-init dimmer %d: %s", id, esp_err_to_name(err));
            return err;
        }
    }

    if (!d->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Clamp to 0-100
    if (percent > 100) {
        percent = 100;
    }

    // Respect per-dimmer configured limits (min_level/max_level from NVS). 0 = off is
    // always allowed; a non-zero setpoint is held within [min_level, max_level] so a
    // control command cannot drive the load outside its configured safe/effective range
    // (MAJOR-6). Guarded against a mis-set max < min.
    if (percent > 0 && d->max_level >= d->min_level) {
        if (percent < d->min_level) percent = d->min_level;
        if (percent > d->max_level) percent = d->max_level;
    }

    // Check mode restrictions
    if (d->mode == DIMMER_MODE_MANUAL_OFF && percent > 0) {
        return ESP_ERR_NOT_ALLOWED;
    }
    if (d->mode == DIMMER_MODE_MANUAL_ON && percent < 100) {
        return ESP_ERR_NOT_ALLOWED;
    }

    return dimmer_dispatch_set_level(d, percent);
}

esp_err_t dimmer_set_level_smooth(uint8_t id, uint8_t percent, uint32_t transition_ms) {
    if (id >= DIMMER_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    dimmer_t* d = &s_dimmers[id];

    if (!d->enabled || !d->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Clamp values
    if (percent > 100) percent = 100;
    if (transition_ms > 5000) transition_ms = 5000;

    // Check mode restrictions
    if (d->mode == DIMMER_MODE_MANUAL_OFF && percent > 0) {
        return ESP_ERR_NOT_ALLOWED;
    }
    if (d->mode == DIMMER_MODE_MANUAL_ON && percent < 100) {
        return ESP_ERR_NOT_ALLOWED;
    }

    return dimmer_dispatch_set_level_smooth(d, percent, transition_ms);
}

uint8_t dimmer_get_level(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return 0;
    }
    return s_dimmers[id].level_percent;
}

uint8_t dimmer_get_target_level(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return 0;
    }
    return s_dimmers[id].target_percent;
}

uint8_t dimmer_set_level_all(uint8_t percent) {
    uint8_t count = 0;

    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        if (s_dimmers[i].enabled && s_dimmers[i].initialized) {
            if (dimmer_set_level(i, percent) == ESP_OK) {
                count++;
            }
        }
    }

    return count;
}

void dimmer_emergency_stop_all(void) {
    ESP_LOGW(TAG, "EMERGENCY STOP ALL DIMMERS");

    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        if (s_dimmers[i].initialized) {
            dimmer_dispatch_set_level(&s_dimmers[i], 0);
            s_dimmers[i].level_percent = 0;
            s_dimmers[i].target_percent = 0;
            s_dimmers[i].state = DIMMER_STATE_OFF;
        }
    }
}

// ============================================================
// State Control
// ============================================================

esp_err_t dimmer_set_enabled(uint8_t id, bool enabled) {
    if (id >= DIMMER_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    dimmer_t* d = &s_dimmers[id];

    if (enabled) {
        // Enabling dimmer - initialize hardware if needed
        d->enabled = true;

        if (!d->initialized && d->type != DIMMER_TYPE_NONE) {
            // Try to initialize hardware
            esp_err_t err = dimmer_dispatch_init(d);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to initialize dimmer %d hardware: %s", id, esp_err_to_name(err));
                // Keep enabled flag set - user can configure GPIO and try again
            } else {
                ESP_LOGI(TAG, "Dimmer %d enabled and initialized", id);
            }
        } else {
            ESP_LOGI(TAG, "Dimmer %d enabled", id);
        }
    } else {
        // Disabling dimmer - turn off first if initialized
        if (d->initialized) {
            dimmer_dispatch_set_level(d, 0);
        }
        d->enabled = false;
        ESP_LOGI(TAG, "Dimmer %d disabled", id);
    }

    return ESP_OK;
}

bool dimmer_is_enabled(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return false;
    }
    return s_dimmers[id].enabled;
}

bool dimmer_is_initialized(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return false;
    }
    return s_dimmers[id].initialized;
}

esp_err_t dimmer_set_mode(uint8_t id, dimmer_mode_t mode) {
    if (id >= DIMMER_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    dimmer_t* d = &s_dimmers[id];
    d->mode = mode;

    // Apply mode-specific behavior
    if (d->initialized) {
        switch (mode) {
            case DIMMER_MODE_MANUAL_OFF:
                dimmer_dispatch_set_level(d, 0);
                break;
            case DIMMER_MODE_MANUAL_ON:
                dimmer_dispatch_set_level(d, 100);
                break;
            default:
                break;
        }
    }

    ESP_LOGI(TAG, "Dimmer %d mode set to %s", id, dimmer_mode_str(mode));
    return ESP_OK;
}

dimmer_mode_t dimmer_get_mode(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return DIMMER_MODE_AUTO;
    }
    return s_dimmers[id].mode;
}

esp_err_t dimmer_set_curve(uint8_t id, dimmer_curve_t curve) {
    if (id >= DIMMER_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    dimmer_t* d = &s_dimmers[id];
    d->curve = curve;

    // Apply to hardware if initialized
    if (d->initialized) {
        dimmer_dispatch_set_curve(d, curve);
    }

    ESP_LOGI(TAG, "Dimmer %d curve set to %s", id, dimmer_curve_str(curve));
    return ESP_OK;
}

dimmer_curve_t dimmer_get_curve(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return DIMMER_CURVE_RMS;
    }
    return s_dimmers[id].curve;
}

dimmer_state_t dimmer_get_state(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return DIMMER_STATE_ERROR;
    }
    return s_dimmers[id].state;
}

void dimmer_all_off(void) {
    ESP_LOGI(TAG, "All dimmers OFF");
    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        if (s_dimmers[i].enabled && s_dimmers[i].initialized) {
            dimmer_set_level(i, 0);
        }
    }
}

// ============================================================
// Configuration
// ============================================================

esp_err_t dimmer_set_name(uint8_t id, const char* name) {
    if (id >= DIMMER_MAX_COUNT || !name) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_dimmers[id].name, name, sizeof(s_dimmers[id].name) - 1);
    s_dimmers[id].name[sizeof(s_dimmers[id].name) - 1] = '\0';

    return ESP_OK;
}

const char* dimmer_get_name(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return "";
    }
    return s_dimmers[id].name;
}

esp_err_t dimmer_set_nominal_power(uint8_t id, uint16_t watts) {
    if (id >= DIMMER_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_dimmers[id].nominal_power_w = watts;
    return ESP_OK;
}

uint16_t dimmer_get_nominal_power(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return 0;
    }
    return s_dimmers[id].nominal_power_w;
}

esp_err_t dimmer_set_priority(uint8_t id, uint8_t priority) {
    if (id >= DIMMER_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_dimmers[id].priority = priority;
    return ESP_OK;
}

uint8_t dimmer_get_priority(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return 0;
    }
    return s_dimmers[id].priority;
}

esp_err_t dimmer_set_current_sensor(uint8_t id, int8_t sensor_id) {
    if (id >= DIMMER_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_dimmers[id].current_sensor_id = sensor_id;
    return ESP_OK;
}

int8_t dimmer_get_current_sensor(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return -1;
    }
    return s_dimmers[id].current_sensor_id;
}

// v2.0: legacy GPIO/TRIAC dimmer config (dimmer_set_gpio / dimmer_get_gpio) removed —
// dimming is only via DimmerLink (dimmer_bind_i2c / dimmer_bind_espnow).

int dimmer_bind_i2c(uint8_t bus, uint8_t addr) {
    // Reuse a dimmer slot already bound to this DimmerLink device.
    for (uint8_t id = DIMMER_I2C_START; id < DIMMER_I2C_END; id++) {
        dimmer_t* d = &s_dimmers[id];
        if (d->type == DIMMER_TYPE_I2C && d->i2c_bus == bus && d->i2c_address == addr) {
            d->enabled = true;
            dimmer_dispatch_init(d);
            return (int)id;
        }
    }
    // Allocate a free I2C dimmer slot.
    for (uint8_t id = DIMMER_I2C_START; id < DIMMER_I2C_END; id++) {
        dimmer_t* d = &s_dimmers[id];
        if (d->type == DIMMER_TYPE_NONE || (!d->enabled && d->i2c_address == 0)) {
            d->type        = DIMMER_TYPE_I2C;
            d->i2c_bus     = bus;
            d->i2c_address = addr;
            d->enabled     = true;
            dimmer_dispatch_init(d);
            ESP_LOGI(TAG, "Bound DimmerLink 0x%02X (bus %u) to dimmer %u (I2C)", addr, bus, id);
            return (int)id;
        }
    }
    ESP_LOGW(TAG, "No free I2C dimmer slot for DimmerLink 0x%02X", addr);
    return -1;
}

int dimmer_bind_espnow(const uint8_t mac[6]) {
    if (!mac) return -1;
    // Reuse a dimmer slot already bound to this node MAC.
    for (uint8_t id = DIMMER_ESPNOW_START; id < DIMMER_ESPNOW_END; id++) {
        dimmer_t* d = &s_dimmers[id];
        if (d->type == DIMMER_TYPE_ESPNOW && memcmp(d->espnow_mac, mac, 6) == 0) {
            d->enabled = true;
            dimmer_dispatch_init(d);
            return (int)id;
        }
    }
    // Allocate a free ESP-NOW dimmer slot.
    for (uint8_t id = DIMMER_ESPNOW_START; id < DIMMER_ESPNOW_END; id++) {
        dimmer_t* d = &s_dimmers[id];
        bool free_slot = (d->type == DIMMER_TYPE_NONE) ||
                         (!d->enabled && d->espnow_mac[0] == 0 && d->espnow_mac[1] == 0 &&
                          d->espnow_mac[2] == 0 && d->espnow_mac[3] == 0 &&
                          d->espnow_mac[4] == 0 && d->espnow_mac[5] == 0);
        if (free_slot) {
            d->type    = DIMMER_TYPE_ESPNOW;
            memcpy(d->espnow_mac, mac, 6);
            d->enabled = true;
            dimmer_dispatch_init(d);
            ESP_LOGI(TAG, "Bound ESP-NOW node %02X:%02X:%02X:%02X:%02X:%02X to dimmer %u",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], id);
            return (int)id;
        }
    }
    ESP_LOGW(TAG, "No free ESP-NOW dimmer slot for node %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return -1;
}

// ============================================================
// Status Reporting
// ============================================================

esp_err_t dimmer_get_status(uint8_t id, dimmer_status_t* status) {
    if (id >= DIMMER_MAX_COUNT || !status) {
        return ESP_ERR_INVALID_ARG;
    }

    const dimmer_t* d = &s_dimmers[id];

    status->id = d->id;
    status->type = d->type;
    status->enabled = d->enabled;
    status->initialized = d->initialized;
    status->state = d->state;
    status->mode = d->mode;
    status->curve = d->curve;
    status->level_percent = d->level_percent;
    status->target_percent = d->target_percent;
    status->min_level = d->min_level;
    status->max_level = d->max_level;
    status->nominal_power_w = d->nominal_power_w;
    status->gpio_pin = d->gpio_pin;
    status->i2c_address = d->i2c_address;
    status->i2c_bus = d->i2c_bus;
    memcpy(status->espnow_mac, d->espnow_mac, sizeof(status->espnow_mac));
    strncpy(status->name, d->name, sizeof(status->name));
    status->name[sizeof(status->name) - 1] = '\0';

    return ESP_OK;
}

void dimmer_log_status(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return;
    }

    const dimmer_t* d = &s_dimmers[id];

    if (d->type == DIMMER_TYPE_NONE) {
        ESP_LOGI(TAG, "Dimmer %d: not configured", id);
        return;
    }

    ESP_LOGI(TAG, "Dimmer %d [%s]: %s, %s, level=%d%%, gpio=%d, mode=%s, curve=%s, power=%dW, priority=%d, name='%s'",
             id,
             dimmer_type_str(d->type),
             d->enabled ? "enabled" : "disabled",
             d->initialized ? "init" : "not-init",
             d->level_percent,
             d->gpio_pin,
             dimmer_mode_str(d->mode),
             dimmer_curve_str(d->curve),
             d->nominal_power_w,
             d->priority,
             d->name);
}

void dimmer_log_status_all(void) {
    ESP_LOGI(TAG, "=== Dimmer Status ===");
    ESP_LOGI(TAG, "Total: %d, Enabled: %d, Active: %d",
             DIMMER_MAX_COUNT, dimmer_get_enabled_count(), dimmer_get_active_count());

    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        if (s_dimmers[i].type != DIMMER_TYPE_NONE || s_dimmers[i].enabled) {
            dimmer_log_status(i);
        }
    }
}

// ============================================================
// NVS Operations
// ============================================================

// Legacy NVS namespace (HardwareConfigManager)
static const char* NVS_LEGACY_NAMESPACE = "hw_config";

static void nvs_key(char* buf, size_t size, uint8_t id, const char* suffix) {
    snprintf(buf, size, "d%d_%s", id, suffix);
}

/**
 * @brief Migrate dimmer config from legacy hw_config namespace
 *
 * Previous versions stored dimmer config in hw_config namespace:
 *   dim1_gpio, dim1_en -> d0
 *   dim2_gpio, dim2_en -> d1
 *
 * This function reads from hw_config and populates s_dimmers[0] and s_dimmers[1]
 * Called only if new "dimmer" namespace doesn't exist yet.
 */
static bool dimmer_migrate_from_legacy(void) {
    nvs_handle_t hw_handle;

    // Try to open legacy namespace
    esp_err_t err = nvs_open(NVS_LEGACY_NAMESPACE, NVS_READONLY, &hw_handle);
    if (err != ESP_OK) {
        return false;  // No legacy config
    }

    // v2.0: legacy v1 dimmers were GPIO/TRIAC — dimming is now only via DimmerLink, so
    // there is nothing to migrate (a migrated GPIO dimmer would just be a phantom that
    // drives nothing). The legacy NVS keys (dim1_gpio/dim2_gpio) are simply ignored.
    nvs_close(hw_handle);
    return false;
}

esp_err_t dimmer_save_config(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    const dimmer_t* d = &s_dimmers[id];
    char key[16];
    bool success = true;

    // Common fields
    nvs_key(key, sizeof(key), id, "type");
    success &= (nvs_set_u8(handle, key, (uint8_t)d->type) == ESP_OK);

    nvs_key(key, sizeof(key), id, "en");
    success &= (nvs_set_u8(handle, key, d->enabled ? 1 : 0) == ESP_OK);

    nvs_key(key, sizeof(key), id, "name");
    success &= (nvs_set_str(handle, key, d->name) == ESP_OK);

    nvs_key(key, sizeof(key), id, "pwr");
    success &= (nvs_set_u16(handle, key, d->nominal_power_w) == ESP_OK);

    nvs_key(key, sizeof(key), id, "sens");
    success &= (nvs_set_i8(handle, key, d->current_sensor_id) == ESP_OK);

    nvs_key(key, sizeof(key), id, "curve");
    success &= (nvs_set_u8(handle, key, (uint8_t)d->curve) == ESP_OK);

    nvs_key(key, sizeof(key), id, "mode");
    success &= (nvs_set_u8(handle, key, (uint8_t)d->mode) == ESP_OK);

    // Level limits
    nvs_key(key, sizeof(key), id, "min");
    success &= (nvs_set_u8(handle, key, d->min_level) == ESP_OK);

    nvs_key(key, sizeof(key), id, "max");
    success &= (nvs_set_u8(handle, key, d->max_level) == ESP_OK);

    nvs_key(key, sizeof(key), id, "def");
    success &= (nvs_set_u8(handle, key, d->default_level) == ESP_OK);

    nvs_key(key, sizeof(key), id, "ramp");
    success &= (nvs_set_u16(handle, key, d->ramp_time_ms) == ESP_OK);

    nvs_key(key, sizeof(key), id, "pri");
    success &= (nvs_set_u8(handle, key, d->priority) == ESP_OK);

    // Type-specific fields
    switch (d->type) {

        case DIMMER_TYPE_I2C:
            nvs_key(key, sizeof(key), id, "i2c_a");
            success &= (nvs_set_u8(handle, key, d->i2c_address) == ESP_OK);

            nvs_key(key, sizeof(key), id, "i2c_b");
            success &= (nvs_set_u8(handle, key, d->i2c_bus) == ESP_OK);

            nvs_key(key, sizeof(key), id, "i2c_c");
            success &= (nvs_set_u8(handle, key, d->i2c_channel) == ESP_OK);
            break;

        case DIMMER_TYPE_ESPNOW:
            nvs_key(key, sizeof(key), id, "mac");
            success &= (nvs_set_blob(handle, key, d->espnow_mac, 6) == ESP_OK);

            nvs_key(key, sizeof(key), id, "wifi_c");
            success &= (nvs_set_u8(handle, key, d->espnow_wifi_channel) == ESP_OK);

            nvs_key(key, sizeof(key), id, "tout");
            success &= (nvs_set_u16(handle, key, d->espnow_timeout_ms) == ESP_OK);

            nvs_key(key, sizeof(key), id, "retry");
            success &= (nvs_set_u8(handle, key, d->espnow_retry_count) == ESP_OK);
            break;

        default:
            break;
    }

    if (success) {
        nvs_commit(handle);
        ESP_LOGI(TAG, "Dimmer %d config saved", id);
    } else {
        ESP_LOGE(TAG, "Failed to save dimmer %d config", id);
    }

    nvs_close(handle);
    return success ? ESP_OK : ESP_FAIL;
}

esp_err_t dimmer_load_config(uint8_t id) {
    if (id >= DIMMER_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    dimmer_t* d = &s_dimmers[id];
    char key[16];
    uint8_t u8val;
    int8_t i8val;
    uint16_t u16val;
    char str[16];
    size_t len;

    // Common fields
    nvs_key(key, sizeof(key), id, "type");
    if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
        d->type = (dimmer_type_t)u8val;
    }

    nvs_key(key, sizeof(key), id, "en");
    if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
        d->enabled = (u8val != 0);
    }

    nvs_key(key, sizeof(key), id, "name");
    len = sizeof(str);
    if (nvs_get_str(handle, key, str, &len) == ESP_OK) {
        strncpy(d->name, str, sizeof(d->name) - 1);
        d->name[sizeof(d->name) - 1] = '\0';
    }

    nvs_key(key, sizeof(key), id, "pwr");
    if (nvs_get_u16(handle, key, &u16val) == ESP_OK) {
        d->nominal_power_w = u16val;
    }

    nvs_key(key, sizeof(key), id, "sens");
    if (nvs_get_i8(handle, key, &i8val) == ESP_OK) {
        d->current_sensor_id = i8val;
    }

    nvs_key(key, sizeof(key), id, "curve");
    if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
        d->curve = (dimmer_curve_t)u8val;
    }

    nvs_key(key, sizeof(key), id, "mode");
    if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
        d->mode = (dimmer_mode_t)u8val;
    }

    // Level limits
    nvs_key(key, sizeof(key), id, "min");
    if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
        d->min_level = u8val;
    }

    nvs_key(key, sizeof(key), id, "max");
    if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
        d->max_level = u8val;
    }

    nvs_key(key, sizeof(key), id, "def");
    if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
        d->default_level = u8val;
    }

    nvs_key(key, sizeof(key), id, "ramp");
    if (nvs_get_u16(handle, key, &u16val) == ESP_OK) {
        d->ramp_time_ms = u16val;
    }

    nvs_key(key, sizeof(key), id, "pri");
    if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
        d->priority = u8val;
    } else {
        d->priority = 0;  // Default: highest priority
        ESP_LOGD(TAG, "Dimmer %d: priority not in NVS, using default=0", id);
    }

    // Type-specific fields (load based on type already read)
    switch (d->type) {
        case DIMMER_TYPE_I2C:
            nvs_key(key, sizeof(key), id, "i2c_a");
            if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
                d->i2c_address = u8val;
            }

            nvs_key(key, sizeof(key), id, "i2c_b");
            if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
                d->i2c_bus = u8val;
            }

            nvs_key(key, sizeof(key), id, "i2c_c");
            if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
                d->i2c_channel = u8val;
            }
            break;

        case DIMMER_TYPE_ESPNOW:
            nvs_key(key, sizeof(key), id, "mac");
            len = 6;
            nvs_get_blob(handle, key, d->espnow_mac, &len);

            nvs_key(key, sizeof(key), id, "wifi_c");
            if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
                d->espnow_wifi_channel = u8val;
            }

            nvs_key(key, sizeof(key), id, "tout");
            if (nvs_get_u16(handle, key, &u16val) == ESP_OK) {
                d->espnow_timeout_ms = u16val;
            }

            nvs_key(key, sizeof(key), id, "retry");
            if (nvs_get_u8(handle, key, &u8val) == ESP_OK) {
                d->espnow_retry_count = u8val;
            }
            break;

        default:
            break;
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t dimmer_save_all(void) {
    ESP_LOGI(TAG, "Saving all dimmer configs");

    esp_err_t result = ESP_OK;
    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        // Only save configured dimmers
        if (s_dimmers[i].type != DIMMER_TYPE_NONE) {
            esp_err_t err = dimmer_save_config(i);
            if (err != ESP_OK) {
                result = err;
            }
        }
    }

    return result;
}

esp_err_t dimmer_load_all(void) {
    ESP_LOGI(TAG, "Loading all dimmer configs");

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved dimmer config in '%s' namespace", NVS_NAMESPACE);

            // Try to migrate from legacy hw_config namespace
            if (dimmer_migrate_from_legacy()) {
                ESP_LOGI(TAG, "Successfully migrated from legacy hw_config");
                return ESP_OK;
            }

            ESP_LOGI(TAG, "No legacy config found either - using defaults");
        }
        return err;
    }
    nvs_close(handle);

    // Load from new namespace
    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        dimmer_load_config(i);
    }

    return ESP_OK;
}

esp_err_t dimmer_erase_all(void) {
    ESP_LOGW(TAG, "Erasing all dimmer configs");

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);

    // Reinitialize slots
    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        dimmer_init_slot(i);
    }

    return ESP_OK;
}

// ============================================================
// Iteration Helpers
// ============================================================

void dimmer_foreach(dimmer_iterator_cb_t callback, void* user_data) {
    if (!callback) return;

    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        callback(&s_dimmers[i], user_data);
    }
}

void dimmer_foreach_type(dimmer_type_t type, dimmer_iterator_cb_t callback, void* user_data) {
    if (!callback) return;

    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        if (s_dimmers[i].type == type) {
            callback(&s_dimmers[i], user_data);
        }
    }
}

void dimmer_foreach_enabled(dimmer_iterator_cb_t callback, void* user_data) {
    if (!callback) return;

    for (uint8_t i = 0; i < DIMMER_MAX_COUNT; i++) {
        if (s_dimmers[i].enabled) {
            callback(&s_dimmers[i], user_data);
        }
    }
}
