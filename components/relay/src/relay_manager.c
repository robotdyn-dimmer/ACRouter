/**
 * @file relay_manager.c
 * @brief Unified relay management implementation
 */

#include "relay_manager.h"
#include "relay_gpio.h"
#include "relay_i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include <string.h>

static const char* TAG = "RelayMgr";

// ============================================================
// Internal State
// ============================================================

/** Relay array (all 64 slots) */
static relay_t s_relays[RELAY_MAX_COUNT];

/** Initialization flag */
static bool s_initialized = false;

// ============================================================
// Forward Declarations
// ============================================================

static void relay_init_defaults(relay_t* r, uint8_t id);
static esp_err_t relay_backend_begin(relay_t* r);
static esp_err_t relay_backend_turn_on(relay_t* r);
static esp_err_t relay_backend_turn_off(relay_t* r);
static bool relay_can_switch(const relay_t* r);
static void relay_apply_pending_changes(relay_t* r);

// ============================================================
// Initialization
// ============================================================

/**
 * @brief Initialize relay with default values
 */
static void relay_init_defaults(relay_t* r, uint8_t id) {
    memset(r, 0, sizeof(relay_t));

    r->id = id;
    r->type = RELAY_TYPE_NONE;
    r->gpio_pin = -1;
    r->active_high = true;
    r->enabled = false;
    r->current_sensor_id = -1;
    r->mode = RELAY_MODE_AUTO;
    snprintf(r->name, sizeof(r->name), "Relay %d", id);

    // Debounce defaults
    r->min_on_time_s = RELAY_DEFAULT_MIN_ON_TIME_S;
    r->min_off_time_s = RELAY_DEFAULT_MIN_OFF_TIME_S;
    r->priority = 0;  // Default: highest priority

    // Runtime state
    r->initialized = false;
    r->is_on = false;
    r->last_switch_ms = 0;
    r->pending_on = false;
    r->pending_off = false;
}

esp_err_t relay_manager_init(void) {
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing relay manager (max %d relays)...", RELAY_MAX_COUNT);

    // Initialize all relay slots with defaults
    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        relay_init_defaults(&s_relays[i], i);
    }

    // Initialize GPIO backend
    esp_err_t err = relay_gpio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIO backend: %s", esp_err_to_name(err));
        return err;
    }

    // Load configuration from NVS
    relay_load_all();

    // Auto-initialize enabled relays
    uint8_t init_count = 0;
    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        relay_t* r = &s_relays[i];
        if (r->enabled && r->type != RELAY_TYPE_NONE) {
            err = relay_backend_begin(r);
            if (err == ESP_OK) {
                init_count++;
                ESP_LOGI(TAG, "Relay %d (%s) initialized: type=%s, gpio=%d",
                         i, r->name, relay_type_str(r->type), r->gpio_pin);
            } else {
                ESP_LOGW(TAG, "Failed to initialize relay %d: %s", i, esp_err_to_name(err));
            }
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Relay manager initialized: %d/%d relays active",
             init_count, relay_get_enabled_count());

    return ESP_OK;
}

bool relay_manager_is_initialized(void) {
    return s_initialized;
}

// ============================================================
// Relay Access
// ============================================================

relay_t* relay_get(uint8_t id) {
    if (id >= RELAY_MAX_COUNT) {
        return NULL;
    }
    return &s_relays[id];
}

const relay_t* relay_get_const(uint8_t id) {
    if (id >= RELAY_MAX_COUNT) {
        return NULL;
    }
    return &s_relays[id];
}

uint8_t relay_get_max_count(void) {
    return RELAY_MAX_COUNT;
}

uint8_t relay_get_enabled_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        if (s_relays[i].enabled) {
            count++;
        }
    }
    return count;
}

uint8_t relay_get_active_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        if (s_relays[i].initialized) {
            count++;
        }
    }
    return count;
}

// ============================================================
// Backend Dispatch
// ============================================================

static esp_err_t relay_backend_begin(relay_t* r) {
    if (!r || r->type == RELAY_TYPE_NONE) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (r->type) {
        case RELAY_TYPE_GPIO:
            return relay_gpio_begin(r);
        case RELAY_TYPE_I2C:
            return relay_i2c_begin(r);
        case RELAY_TYPE_ESPNOW:
            return ESP_ERR_NOT_SUPPORTED;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t relay_backend_turn_on(relay_t* r) {
    if (!r || !r->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    switch (r->type) {
        case RELAY_TYPE_GPIO:
            return relay_gpio_turn_on(r);
        case RELAY_TYPE_I2C:
            return relay_i2c_turn_on(r);
        case RELAY_TYPE_ESPNOW:
            return ESP_ERR_NOT_SUPPORTED;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t relay_backend_turn_off(relay_t* r) {
    if (!r || !r->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    switch (r->type) {
        case RELAY_TYPE_GPIO:
            return relay_gpio_turn_off(r);
        case RELAY_TYPE_I2C:
            return relay_i2c_turn_off(r);
        case RELAY_TYPE_ESPNOW:
            return ESP_ERR_NOT_SUPPORTED;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

// ============================================================
// Debounce Logic
// ============================================================

/**
 * @brief Check if relay can switch based on debounce timing
 */
static bool relay_can_switch(const relay_t* r) {
    if (!r) return false;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t elapsed_ms = now_ms - r->last_switch_ms;
    uint32_t elapsed_s = elapsed_ms / 1000;

    if (r->is_on) {
        // Currently ON - check min_on_time
        return elapsed_s >= r->min_on_time_s;
    } else {
        // Currently OFF - check min_off_time
        return elapsed_s >= r->min_off_time_s;
    }
}

/**
 * @brief Apply pending state changes if debounce expired
 */
static void relay_apply_pending_changes(relay_t* r) {
    if (!r || !r->initialized) return;

    if (!relay_can_switch(r)) {
        return; // Still in debounce period
    }

    // Apply pending ON
    if (r->pending_on && !r->is_on) {
        ESP_LOGI(TAG, "Relay %d: applying pending ON", r->id);
        relay_backend_turn_on(r);
        r->pending_on = false;
    }

    // Apply pending OFF
    if (r->pending_off && r->is_on) {
        ESP_LOGI(TAG, "Relay %d: applying pending OFF", r->id);
        relay_backend_turn_off(r);
        r->pending_off = false;
    }
}

// ============================================================
// Relay Control
// ============================================================

esp_err_t relay_turn_on(uint8_t id, bool force) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!r->enabled || !r->initialized) {
        ESP_LOGW(TAG, "Relay %d not enabled/initialized", id);
        return ESP_ERR_INVALID_STATE;
    }

    // Check mode
    if (r->mode == RELAY_MODE_MANUAL_OFF && !force) {
        ESP_LOGW(TAG, "Relay %d in MANUAL_OFF mode", id);
        return ESP_ERR_INVALID_STATE;
    }

    // Already ON?
    if (r->is_on) {
        return ESP_OK;
    }

    // Check debounce
    if (!force && !relay_can_switch(r)) {
        ESP_LOGW(TAG, "Relay %d: debounce active, pending ON", id);
        r->pending_on = true;
        r->pending_off = false;
        return ESP_ERR_INVALID_STATE;
    }

    // Turn ON
    esp_err_t err = relay_backend_turn_on(r);
    if (err == ESP_OK) {
        r->pending_on = false;
        ESP_LOGI(TAG, "Relay %d turned ON%s", id, force ? " (forced)" : "");
    }

    return err;
}

esp_err_t relay_turn_off(uint8_t id, bool force) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!r->enabled || !r->initialized) {
        ESP_LOGW(TAG, "Relay %d not enabled/initialized", id);
        return ESP_ERR_INVALID_STATE;
    }

    // Check mode
    if (r->mode == RELAY_MODE_MANUAL_ON && !force) {
        ESP_LOGW(TAG, "Relay %d in MANUAL_ON mode", id);
        return ESP_ERR_INVALID_STATE;
    }

    // Already OFF?
    if (!r->is_on) {
        return ESP_OK;
    }

    // Check debounce
    if (!force && !relay_can_switch(r)) {
        ESP_LOGW(TAG, "Relay %d: debounce active, pending OFF", id);
        r->pending_off = true;
        r->pending_on = false;
        return ESP_ERR_INVALID_STATE;
    }

    // Turn OFF
    esp_err_t err = relay_backend_turn_off(r);
    if (err == ESP_OK) {
        r->pending_off = false;
        ESP_LOGI(TAG, "Relay %d turned OFF%s", id, force ? " (forced)" : "");
    }

    return err;
}

esp_err_t relay_toggle(uint8_t id, bool force) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    if (r->is_on) {
        return relay_turn_off(id, force);
    } else {
        return relay_turn_on(id, force);
    }
}

bool relay_is_on(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    if (!r) {
        return false;
    }
    return r->is_on;
}

bool relay_is_debounce_active(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    if (!r || !r->initialized) {
        return false;
    }
    return !relay_can_switch(r);
}

uint16_t relay_get_debounce_remaining(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    if (!r || !r->initialized) {
        return 0;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t elapsed_ms = now_ms - r->last_switch_ms;
    uint32_t elapsed_s = elapsed_ms / 1000;

    uint16_t required_s = r->is_on ? r->min_on_time_s : r->min_off_time_s;

    if (elapsed_s >= required_s) {
        return 0;
    }

    return required_s - elapsed_s;
}

uint32_t relay_get_on_duration(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    if (!r || !r->is_on) {
        return 0;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t duration_ms = now_ms - r->last_switch_ms;
    return duration_ms / 1000;
}

void relay_all_off(bool force) {
    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        if (s_relays[i].initialized && s_relays[i].is_on) {
            relay_turn_off(i, force);
        }
    }
}

uint8_t relay_all_on(bool force) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        if (s_relays[i].initialized && !s_relays[i].is_on) {
            if (relay_turn_on(i, force) == ESP_OK) {
                count++;
            }
        }
    }
    return count;
}

// ============================================================
// State Control
// ============================================================

esp_err_t relay_set_enabled(uint8_t id, bool enabled) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    if (r->enabled == enabled) {
        return ESP_OK;
    }

    r->enabled = enabled;

    if (enabled && r->type != RELAY_TYPE_NONE && !r->initialized) {
        // Try to initialize
        relay_backend_begin(r);
    } else if (!enabled && r->initialized) {
        // Turn off and deinitialize
        relay_backend_turn_off(r);
        r->initialized = false;
    }

    ESP_LOGI(TAG, "Relay %d %s", id, enabled ? "enabled" : "disabled");
    return ESP_OK;
}

bool relay_is_enabled(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    return (r && r->enabled);
}

bool relay_is_initialized(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    return (r && r->initialized);
}

esp_err_t relay_set_mode(uint8_t id, relay_mode_t mode) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    relay_mode_t old_mode = r->mode;
    r->mode = mode;

    // Apply mode immediately
    if (r->initialized) {
        switch (mode) {
            case RELAY_MODE_MANUAL_OFF:
                relay_turn_off(id, true);
                break;

            case RELAY_MODE_MANUAL_ON:
                relay_turn_on(id, true);
                break;

            case RELAY_MODE_AUTO:
            case RELAY_MODE_SCHEDULE:
                // Keep current state, controller will manage
                break;
        }
    }

    ESP_LOGI(TAG, "Relay %d mode: %s -> %s", id,
             relay_mode_str(old_mode), relay_mode_str(mode));

    return ESP_OK;
}

relay_mode_t relay_get_mode(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    if (!r) {
        return RELAY_MODE_AUTO;
    }
    return r->mode;
}

relay_state_t relay_get_state(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    if (!r) {
        return RELAY_STATE_ERROR;
    }

    if (!r->initialized) {
        if (r->type == RELAY_TYPE_ESPNOW) {
            return RELAY_STATE_DISCONNECTED;
        }
        return RELAY_STATE_ERROR;
    }

    if (relay_is_debounce_active(id)) {
        return RELAY_STATE_DEBOUNCE;
    }

    return r->is_on ? RELAY_STATE_ON : RELAY_STATE_OFF;
}

void relay_update(uint8_t id) {
    relay_t* r = relay_get(id);
    if (r && r->initialized) {
        relay_apply_pending_changes(r);
    }
}

void relay_update_all(void) {
    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        relay_update(i);
    }
}

// ============================================================
// Configuration
// ============================================================

esp_err_t relay_set_name(uint8_t id, const char* name) {
    relay_t* r = relay_get(id);
    if (!r || !name) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(r->name, name, RELAY_NAME_MAX_LEN - 1);
    r->name[RELAY_NAME_MAX_LEN - 1] = '\0';

    return ESP_OK;
}

const char* relay_get_name(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    return r ? r->name : "Invalid";
}

esp_err_t relay_set_nominal_power(uint8_t id, uint16_t watts) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    r->nominal_power_w = watts;
    return ESP_OK;
}

uint16_t relay_get_nominal_power(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    return r ? r->nominal_power_w : 0;
}

esp_err_t relay_set_priority(uint8_t id, uint8_t priority) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    r->priority = priority;
    return ESP_OK;
}

uint8_t relay_get_priority(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    return r ? r->priority : 0;
}

esp_err_t relay_set_current_sensor(uint8_t id, int8_t sensor_id) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    r->current_sensor_id = sensor_id;
    return ESP_OK;
}

int8_t relay_get_current_sensor(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    return r ? r->current_sensor_id : -1;
}

esp_err_t relay_set_min_on_time(uint8_t id, uint16_t seconds) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    r->min_on_time_s = seconds;
    return ESP_OK;
}

uint16_t relay_get_min_on_time(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    return r ? r->min_on_time_s : 0;
}

esp_err_t relay_set_min_off_time(uint8_t id, uint16_t seconds) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    r->min_off_time_s = seconds;
    return ESP_OK;
}

uint16_t relay_get_min_off_time(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    return r ? r->min_off_time_s : 0;
}

// ============================================================
// GPIO-Specific Configuration
// ============================================================

esp_err_t relay_set_gpio(uint8_t id, int8_t gpio_pin) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    if (id >= RELAY_GPIO_COUNT) {
        ESP_LOGE(TAG, "Relay %d is not in GPIO range (0-%d)", id, RELAY_GPIO_COUNT - 1);
        return ESP_ERR_INVALID_ARG;
    }

    // Deinitialize if already initialized
    if (r->initialized) {
        relay_backend_turn_off(r);
        r->initialized = false;
    }

    r->gpio_pin = gpio_pin;

    if (gpio_pin >= 0) {
        r->type = RELAY_TYPE_GPIO;

        // Auto-initialize if relay is enabled
        if (r->enabled) {
            relay_backend_begin(r);
        }
    } else {
        r->type = RELAY_TYPE_NONE;
    }

    return ESP_OK;
}

int8_t relay_get_gpio(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    return r ? r->gpio_pin : -1;
}

esp_err_t relay_set_active_high(uint8_t id, bool active_high) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    if (r->active_high == active_high) {
        return ESP_OK;
    }

    r->active_high = active_high;

    // Reinitialize if already initialized to apply new polarity
    if (r->initialized) {
        relay_backend_turn_off(r);
        r->initialized = false;
        if (r->enabled && r->type != RELAY_TYPE_NONE) {
            relay_backend_begin(r);
        }
    }

    return ESP_OK;
}

bool relay_get_active_high(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    return r ? r->active_high : true;
}

// ============================================================
// Status Reporting
// ============================================================

esp_err_t relay_get_status(uint8_t id, relay_status_t* status) {
    const relay_t* r = relay_get_const(id);
    if (!r || !status) {
        return ESP_ERR_INVALID_ARG;
    }

    status->id = r->id;
    status->type = r->type;
    status->state = relay_get_state(id);
    status->mode = r->mode;
    status->enabled = r->enabled;
    status->initialized = r->initialized;
    status->is_on = r->is_on;
    status->gpio_pin = r->gpio_pin;
    status->active_high = r->active_high;
    status->name = r->name;
    status->nominal_power_w = r->nominal_power_w;
    status->current_sensor_id = r->current_sensor_id;
    status->priority = r->priority;
    status->min_on_time_s = r->min_on_time_s;
    status->min_off_time_s = r->min_off_time_s;
    status->debounce_remaining_s = relay_get_debounce_remaining(id);
    status->on_duration_s = relay_get_on_duration(id);

    return ESP_OK;
}

uint32_t relay_get_total_on_power(void) {
    uint32_t total = 0;
    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        if (s_relays[i].initialized && s_relays[i].is_on) {
            total += s_relays[i].nominal_power_w;
        }
    }
    return total;
}

void relay_log_status(uint8_t id) {
    relay_status_t status;
    if (relay_get_status(id, &status) != ESP_OK) {
        ESP_LOGE(TAG, "Invalid relay ID: %d", id);
        return;
    }

    ESP_LOGI(TAG, "=== Relay %d Status ===", id);
    ESP_LOGI(TAG, "  Name:  %s", status.name);
    ESP_LOGI(TAG, "  Type:  %s", relay_type_str(status.type));
    ESP_LOGI(TAG, "  State: %s", relay_state_str(status.state));
    ESP_LOGI(TAG, "  Mode:  %s", relay_mode_str(status.mode));
    ESP_LOGI(TAG, "  Enabled: %s", status.enabled ? "YES" : "NO");
    ESP_LOGI(TAG, "  Init: %s", status.initialized ? "YES" : "NO");

    if (status.type == RELAY_TYPE_GPIO) {
        ESP_LOGI(TAG, "  GPIO: %d (%s)", status.gpio_pin,
                 status.active_high ? "active-high" : "active-low");
    }

    ESP_LOGI(TAG, "  Power: %d W", status.nominal_power_w);
    ESP_LOGI(TAG, "  Priority: %d", status.priority);
    ESP_LOGI(TAG, "  Sensor: %d", status.current_sensor_id);
    ESP_LOGI(TAG, "  Debounce: ON=%ds, OFF=%ds, Remaining=%ds",
             status.min_on_time_s, status.min_off_time_s, status.debounce_remaining_s);

    if (status.is_on) {
        ESP_LOGI(TAG, "  ON Duration: %lu s", (unsigned long)status.on_duration_s);
    }
}

void relay_log_status_all(void) {
    ESP_LOGI(TAG, "=== All Relays Status ===");
    ESP_LOGI(TAG, "Total: %d, Enabled: %d, Active: %d",
             RELAY_MAX_COUNT, relay_get_enabled_count(), relay_get_active_count());

    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        if (s_relays[i].type != RELAY_TYPE_NONE) {
            relay_status_t status;
            relay_get_status(i, &status);
            ESP_LOGI(TAG, "  [%2d] %-12s %s %s %s", i, status.name,
                     relay_type_str(status.type),
                     relay_state_str(status.state),
                     relay_mode_str(status.mode));
        }
    }
}

// ============================================================
// NVS Operations
// ============================================================

esp_err_t relay_save_config(uint8_t id) {
    const relay_t* r = relay_get_const(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Saving relay %d: type=%s, gpio=%d, enabled=%s, name=%s",
             id, relay_type_str(r->type), r->gpio_pin,
             r->enabled ? "YES" : "NO", r->name);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("relay", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    char key[16];
    bool success = true;

    // Save type
    snprintf(key, sizeof(key), "r%d_type", id);
    if (nvs_set_u8(nvs, key, (uint8_t)r->type) != ESP_OK) success = false;

    // Save GPIO config
    snprintf(key, sizeof(key), "r%d_gpio", id);
    if (nvs_set_i8(nvs, key, r->gpio_pin) != ESP_OK) success = false;

    snprintf(key, sizeof(key), "r%d_actl", id);
    if (nvs_set_u8(nvs, key, r->active_high ? 1 : 0) != ESP_OK) success = false;

    // Save common config
    snprintf(key, sizeof(key), "r%d_name", id);
    if (nvs_set_str(nvs, key, r->name) != ESP_OK) success = false;

    snprintf(key, sizeof(key), "r%d_en", id);
    if (nvs_set_u8(nvs, key, r->enabled ? 1 : 0) != ESP_OK) success = false;

    snprintf(key, sizeof(key), "r%d_mode", id);
    if (nvs_set_u8(nvs, key, (uint8_t)r->mode) != ESP_OK) success = false;

    snprintf(key, sizeof(key), "r%d_pwr", id);
    if (nvs_set_u16(nvs, key, r->nominal_power_w) != ESP_OK) success = false;

    snprintf(key, sizeof(key), "r%d_sns", id);
    if (nvs_set_i8(nvs, key, r->current_sensor_id) != ESP_OK) success = false;

    snprintf(key, sizeof(key), "r%d_mon", id);
    if (nvs_set_u16(nvs, key, r->min_on_time_s) != ESP_OK) success = false;

    snprintf(key, sizeof(key), "r%d_moff", id);
    if (nvs_set_u16(nvs, key, r->min_off_time_s) != ESP_OK) success = false;

    snprintf(key, sizeof(key), "r%d_pri", id);
    if (nvs_set_u8(nvs, key, r->priority) != ESP_OK) success = false;

    // Commit
    if (success) {
        err = nvs_commit(nvs);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
            success = false;
        }
    }

    nvs_close(nvs);

    if (success) {
        ESP_LOGI(TAG, "Relay %d config saved to NVS", id);
    } else {
        ESP_LOGE(TAG, "Failed to save relay %d config", id);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t relay_load_config(uint8_t id) {
    relay_t* r = relay_get(id);
    if (!r) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("relay", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        // No config namespace exists yet - this is normal on first boot
        ESP_LOGD(TAG, "Relay %d: NVS namespace not found (first boot?)", id);
        return err;
    }

    char key[16];
    uint8_t u8val;
    int8_t i8val;
    uint16_t u16val;
    size_t len;
    bool config_found = false;

    // Load type
    snprintf(key, sizeof(key), "r%d_type", id);
    if (nvs_get_u8(nvs, key, &u8val) == ESP_OK) {
        r->type = (relay_type_t)u8val;
        config_found = true;
        ESP_LOGD(TAG, "Relay %d: loaded type=%d", id, u8val);
    }

    // Load GPIO config
    snprintf(key, sizeof(key), "r%d_gpio", id);
    if (nvs_get_i8(nvs, key, &i8val) == ESP_OK) {
        r->gpio_pin = i8val;
    }

    snprintf(key, sizeof(key), "r%d_actl", id);
    if (nvs_get_u8(nvs, key, &u8val) == ESP_OK) {
        r->active_high = (u8val != 0);
    }

    // Load common config
    snprintf(key, sizeof(key), "r%d_name", id);
    len = RELAY_NAME_MAX_LEN;
    nvs_get_str(nvs, key, r->name, &len);

    snprintf(key, sizeof(key), "r%d_en", id);
    if (nvs_get_u8(nvs, key, &u8val) == ESP_OK) {
        r->enabled = (u8val != 0);
    }

    snprintf(key, sizeof(key), "r%d_mode", id);
    if (nvs_get_u8(nvs, key, &u8val) == ESP_OK) {
        r->mode = (relay_mode_t)u8val;
    }

    snprintf(key, sizeof(key), "r%d_pwr", id);
    if (nvs_get_u16(nvs, key, &u16val) == ESP_OK) {
        r->nominal_power_w = u16val;
    }

    snprintf(key, sizeof(key), "r%d_sns", id);
    if (nvs_get_i8(nvs, key, &i8val) == ESP_OK) {
        r->current_sensor_id = i8val;
    }

    snprintf(key, sizeof(key), "r%d_mon", id);
    if (nvs_get_u16(nvs, key, &u16val) == ESP_OK) {
        r->min_on_time_s = u16val;
    }

    snprintf(key, sizeof(key), "r%d_moff", id);
    if (nvs_get_u16(nvs, key, &u16val) == ESP_OK) {
        r->min_off_time_s = u16val;
    }

    snprintf(key, sizeof(key), "r%d_pri", id);
    if (nvs_get_u8(nvs, key, &u8val) == ESP_OK) {
        r->priority = u8val;
    } else {
        r->priority = 0;  // Default: highest priority
        ESP_LOGD(TAG, "Relay %d: priority not in NVS, using default=0", id);
    }

    nvs_close(nvs);

    if (config_found) {
        ESP_LOGI(TAG, "Relay %d config loaded: %s, GPIO=%d, enabled=%s",
                 id, r->name, r->gpio_pin, r->enabled ? "YES" : "NO");
    }

    return ESP_OK;
}

esp_err_t relay_save_all(void) {
    bool success = true;
    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        if (s_relays[i].type != RELAY_TYPE_NONE) {
            if (relay_save_config(i) != ESP_OK) {
                success = false;
            }
        }
    }
    return success ? ESP_OK : ESP_FAIL;
}

esp_err_t relay_load_all(void) {
    ESP_LOGI(TAG, "Loading relay configurations from NVS...");
    uint8_t loaded_count = 0;

    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        if (relay_load_config(i) == ESP_OK) {
            // Check if relay has valid configuration
            if (s_relays[i].type != RELAY_TYPE_NONE || s_relays[i].enabled) {
                loaded_count++;
            }
        }
    }

    ESP_LOGI(TAG, "Loaded %d relay configurations from NVS", loaded_count);
    return ESP_OK;
}

esp_err_t relay_erase_all(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("relay", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "All relay configs erased from NVS");
    }

    return err;
}

// ============================================================
// Iteration Helpers
// ============================================================

void relay_foreach(relay_iterator_cb_t callback, void* user_data) {
    if (!callback) return;

    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        callback(&s_relays[i], user_data);
    }
}

void relay_foreach_type(relay_type_t type, relay_iterator_cb_t callback, void* user_data) {
    if (!callback) return;

    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        if (s_relays[i].type == type) {
            callback(&s_relays[i], user_data);
        }
    }
}

void relay_foreach_enabled(relay_iterator_cb_t callback, void* user_data) {
    if (!callback) return;

    for (uint8_t i = 0; i < RELAY_MAX_COUNT; i++) {
        if (s_relays[i].enabled) {
            callback(&s_relays[i], user_data);
        }
    }
}
