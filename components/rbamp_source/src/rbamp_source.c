/**
 * @file rbamp_source.c
 * @brief rbAmp I2C sensor source — feeds Sensor Hub (v2.0 sensing offload).
 *
 * See rbamp_source.h. Mirrors dimmerlink_manager's poll→post pattern so the
 * Sensor Hub and RouterController stay source-agnostic.
 */
#include "rbamp_source.h"

#include "sdkconfig.h"
#include "i2c_bus.h"
#include "acrouter_events.h"
#include "acrouter_measurements.h"

#include "rbamp.h"
#include "rbamp_fleet.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "nvs.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "rbamp_source";

/* Diagnostic log cadence: at 5 Hz, every 25 cycles ≈ 5 s. */
#define RBAMP_SRC_LOG_EVERY 25

/* NVS namespace for persisted address→role commissioning. */
#define RBAMP_SRC_NVS_NS "rbamp_src"

/* ---- state ---- */
static rbamp_fleet_t s_fleet       = NULL;
static bool          s_initialized = false;
static size_t        s_alive       = 0;

static rbamp_source_module_cfg_t s_role_cfg[RBAMP_SOURCE_MAX_MODULES];
static size_t                    s_role_cfg_count = 0;
/* Guards s_role_cfg[]/s_role_cfg_count: the web task mutates+compacts it while the
 * poll task reads it via role_for_addr() every cycle (D8). Pure memory ops — a
 * short spinlock is enough (no blocking calls inside the critical sections). */
static portMUX_TYPE              s_cfg_mux = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t  s_poll_task     = NULL;

/* Optional DRDY-driven polling. rbAmp DRDY (open-drain, active-low) asserts when
 * a fresh measurement set is ready. When a DRDY GPIO is wired, the poll task
 * waits on an ISR notification instead of a fixed delay, so it reads exactly
 * when data is ready (lowest latency, no double/mid-update reads). The poll
 * interval then acts as a fallback ceiling if DRDY is silent. -1 = disabled
 * (fixed-cadence timer poll, the default / correct choice for a multi-module
 * fleet whose DRDY lines are NOT synchronised). */
static int s_drdy_gpio = -1;
static bool s_drdy_isr_installed = false;

static void IRAM_ATTR rbamp_drdy_isr(void *arg)
{
    (void)arg;
    TaskHandle_t t = s_poll_task;
    if (t) {
        BaseType_t hpw = pdFALSE;
        vTaskNotifyGiveFromISR(t, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}
static volatile bool s_poll_running  = false;
static volatile bool s_paused        = false;  /* quiescent for on-demand discovery */
static uint32_t      s_poll_interval = 200;

/* Poll buffers — static to keep the task stack small. */
static rbamp_snapshot_t   s_snaps[RBAMP_SOURCE_MAX_MODULES];
static rbamp_fleet_poll_t s_status[RBAMP_SOURCE_MAX_MODULES];

/* Last snapshot + online flag per fleet index (for status / REST). Lock-free
 * best-effort: the poll task writes, a REST reader may see a one-cycle-torn
 * value — acceptable for display. */
static rbamp_snapshot_t s_last_snap[RBAMP_SOURCE_MAX_MODULES];
static bool             s_last_ok[RBAMP_SOURCE_MAX_MODULES];

/* Set by rbamp_source_rescan(); consumed by the poll task between cycles. */
static volatile bool s_rescan_requested = false;

/* Address-change request — consumed by the poll task between cycles (same as
 * rescan) so the two-phase reassign never mutates the fleet while poll_all is
 * iterating it. One in flight at a time. */
static volatile bool      s_addr_change_req    = false;
static uint8_t            s_addr_change_cur    = 0;
static uint8_t            s_addr_change_new    = 0;
static volatile bool      s_addr_change_done   = false;
static volatile esp_err_t s_addr_change_result = ESP_OK;

/* SCT-013 CT-model catalog — firmware source of truth. Codes authoritative per
 * rbamp-dev (ct_presets.c, v1.3): 60A/100A codes are ASSIGNED but their preset
 * rows are not implemented, so applying them fails — marked available=false. */
static const rbamp_ct_model_t s_ct_catalog[] = {
    { "sct013-005", "SCT-013-005", 5,   1, true  },
    { "sct013-010", "SCT-013-010", 10,  2, true  },
    { "sct013-020", "SCT-013-020", 20,  6, true  },
    { "sct013-030", "SCT-013-030", 30,  3, true  },
    { "sct013-050", "SCT-013-050", 50,  4, true  },
    { "sct013-060", "SCT-013-060", 60,  7, false }, /* preset row TBD (v1.3) */
    { "sct013-100", "SCT-013-100", 100, 5, false }, /* "Phase A TBD", no row  */
};

/* CT-model change request — consumed by the poll task (like address change) so
 * the device handle isn't touched concurrently with poll_all. */
static volatile bool      s_ct_req    = false;
static uint8_t            s_ct_addr   = 0;
static uint8_t            s_ct_code   = 0;
static volatile bool      s_ct_done   = false;
static volatile esp_err_t s_ct_result = ESP_OK;

/* Applied CT-model code cache (addr-keyed), refreshed by the poll task; the REST
 * GET reads this instead of touching the handle. 0 = unset/unknown. */
static uint8_t s_ct_cache_addr[RBAMP_SOURCE_MAX_MODULES];
static uint8_t s_ct_cache_code[RBAMP_SOURCE_MAX_MODULES];

/* Timing instrumentation: I2C poll-cycle duration (rbamp_fleet_poll_all). */
static volatile uint32_t s_poll_last_us = 0;   /* last cycle I2C time (us) */
static volatile uint32_t s_poll_avg_us  = 0;   /* EMA of cycle I2C time (us) */
static volatile uint32_t s_poll_count   = 0;   /* completed poll cycles */

/* ---- helpers ---- */

static rbamp_source_role_t role_for_addr(uint8_t addr)
{
    rbamp_source_role_t r = RBAMP_ROLE_NONE;
    portENTER_CRITICAL(&s_cfg_mux);
    for (size_t i = 0; i < s_role_cfg_count; i++) {
        if (s_role_cfg[i].i2c_addr == addr) {
            r = s_role_cfg[i].role;
            break;
        }
    }
    portEXIT_CRITICAL(&s_cfg_mux);
    return r;
}

static int slot_channel_for_role(rbamp_source_role_t role)
{
    switch (role) {
        case RBAMP_ROLE_GRID:  return ACROUTER_CH_GRID;
        case RBAMP_ROLE_SOLAR: return ACROUTER_CH_SOLAR;
        case RBAMP_ROLE_LOAD:  return ACROUTER_CH_LOAD;
        default:               return -1;
    }
}

/**
 * Build an acrouter_measurements_t from one module's snapshot and post it.
 *
 * Maps the module's primary channel (current[0]/power[0]) to its role's slot.
 * rbAmp provides signed real power directly, so direction is sign(power).
 * Voltage is forwarded whenever the module has voltage hardware.
 */
static void publish_snapshot(const rbamp_snapshot_t *s,
                             rbamp_source_role_t role, uint8_t addr)
{
    /* Uncommissioned modules (role NONE) are diagnostic-only per the header
     * contract — they must NOT feed the Sensor Hub, or their voltage/current
     * would override the ADC at priority 0. */
    if (role == RBAMP_ROLE_NONE) {
        return;
    }

    acrouter_measurements_t meas;
    acrouter_measurements_init(&meas);
    meas.source       = ACROUTER_SOURCE_I2C;
    /* rbAmp addresses are >=0x08, so source_id never collides with DimmerLink
     * slot ids (0..7) even though both stamp ACROUTER_SOURCE_I2C. */
    meas.source_id    = addr;
    meas.timestamp_us = esp_timer_get_time();

    bool any = false;

    const int ch = slot_channel_for_role(role);
    if (ch >= 0 && s->channels >= 1) {
        const float i0 = s->current[0];
        const float p0 = s->power[0];

        if (!isnan(i0)) {
            meas.current_rms[ch]  = i0;
            meas.has_current[ch]  = true;
            any = true;
        }
        /* Real power is only valid on voltage-capable (UI*) modules; a
         * current-only (I*) module has no real power even if the library
         * returns 0.0 rather than NaN. Gate has_power on the hardware flag so
         * a bogus 0 W never wins the slot over a real ADC power reading. */
        if (s->has_voltage_hw && !isnan(p0)) {
            meas.power_active[ch] = p0;          /* signed: + import, - export */
            meas.has_power[ch]    = true;
            meas.direction[ch]    = (p0 >  0.05f) ? ACROUTER_DIR_CONSUMING
                                  : (p0 < -0.05f) ? ACROUTER_DIR_SUPPLYING
                                                  : ACROUTER_DIR_ZERO;
            any = true;
        } else if (!isnan(i0)) {
            /* Current without signed power: direction is genuinely unknown —
             * do not fabricate it from the role. */
            meas.direction[ch] = ACROUTER_DIR_UNKNOWN;
        }
    }

    /* Voltage: forward only from the grid feed or a dedicated voltage module.
     * Every UI* module carries voltage; forwarding all of them would make the
     * equal-priority voltage slot flip-flop (tie broken by recency) between
     * slightly different readings. */
    if ((role == RBAMP_ROLE_GRID || role == RBAMP_ROLE_VOLTAGE) &&
        s->has_voltage_hw && !isnan(s->voltage) && s->voltage > 1.0f) {
        meas.voltage_rms = s->voltage;
        meas.has_voltage = true;
        any = true;
    }

    meas.valid = any;
    if (any) {
        esp_event_post(ACROUTER_EVENT, ACROUTER_EVENT_POWER_UPDATE,
                       &meas, sizeof(meas), 0);
    }
}

static void log_snapshot(const rbamp_snapshot_t *s, uint8_t addr,
                         rbamp_source_role_t role)
{
    ESP_LOGI(TAG,
             "0x%02X role=%d ch=%u V=%.1f I0=%.3f P0=%.1f PF0=%.2f f=%.2f%s",
             addr, (int)role, s->channels,
             (double)s->voltage, (double)s->current[0],
             (double)s->power[0], (double)s->power_factor[0],
             (double)s->frequency,
             s->implausible ? " [implausible]" : "");
}

/* ---- poll task ---- */

/* Re-scan the bus and adopt any newly-found modules. Must run in the poll-task
 * context (or before the task starts) so it never mutates the fleet while
 * rbamp_fleet_poll_all is iterating it. */
static esp_err_t do_fleet_rescan(void)
{
    if (s_fleet == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t added = 0;
    esp_err_t err = rbamp_fleet_scan(s_fleet, /*match_product=*/true, &added);
    s_alive = rbamp_fleet_count(s_fleet);
    ESP_LOGI(TAG, "Rescan: +%u module(s) (now %u)", (unsigned)added, (unsigned)s_alive);
    return err;
}

/* Execute a queued address change in the poll-task context. Uses the library's
 * two-phase reassign (prepare+commit+reset+verify, on-bus conflict checks), then
 * migrates any role mapping to the new address and persists it. */
static void do_address_change(void)
{
    const uint8_t cur = s_addr_change_cur;
    const uint8_t neu = s_addr_change_new;
    esp_err_t err = ESP_ERR_NOT_FOUND;
    rbamp_handle_t dev = (s_fleet != NULL) ? rbamp_fleet_find(s_fleet, cur) : NULL;
    if (dev != NULL) {
        err = rbamp_fleet_assign_address(s_fleet, dev, neu);
    }
    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_cfg_mux);   /* re-key under the lock vs web readers (D8) */
        for (size_t i = 0; i < s_role_cfg_count; i++) {
            if (s_role_cfg[i].i2c_addr == cur) {
                s_role_cfg[i].i2c_addr = neu;
            }
        }
        portEXIT_CRITICAL(&s_cfg_mux);
        rbamp_source_save_config();
        ESP_LOGI(TAG, "address change 0x%02X -> 0x%02X ok", cur, neu);
    } else {
        ESP_LOGW(TAG, "address change 0x%02X -> 0x%02X failed: %s",
                 cur, neu, esp_err_to_name(err));
    }
    s_addr_change_result = err;
    s_addr_change_done   = true;
}

/* Execute a queued CT-model change in the poll-task context. VERIFY-THEN-SET:
 * read the applied code first and write ONLY if different. Writing a preset
 * reloads its gain into the channel gain register, OVERWRITING per-unit factory
 * gain calibration (rbamp-dev, v1.3 architectural conflict) — so re-applying the
 * same code (e.g. every boot) must be avoided. */
static void do_ct_model(void)
{
    const uint8_t addr = s_ct_addr;
    const uint8_t code = s_ct_code;
    esp_err_t err = ESP_ERR_NOT_FOUND;
    rbamp_handle_t dev = (s_fleet != NULL) ? rbamp_fleet_find(s_fleet, addr) : NULL;
    if (dev != NULL) {
        uint8_t cur = 0;
        (void)rbamp_read_ct_model_ch(dev, 0, &cur);
        if (cur == code) {
            err = ESP_OK;   /* already applied — no-op, preserves gain cal */
        } else {
            err = rbamp_set_sensor_class(dev, RBAMP_SENSOR_SCT013);
            if (err == ESP_OK) {
                err = rbamp_set_ct_model_ch(dev, 0, code);
            }
            if (err == ESP_OK) {
                /* Refresh the cache so the GET reflects it immediately. */
                for (size_t i = 0; i < RBAMP_SOURCE_MAX_MODULES; i++) {
                    if (s_ct_cache_addr[i] == addr) { s_ct_cache_code[i] = code; break; }
                }
            }
        }
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "CT-model 0x%02X ch0 -> code %u ok", addr, code);
    } else {
        ESP_LOGW(TAG, "CT-model 0x%02X -> code %u failed: %s", addr, code, esp_err_to_name(err));
    }
    s_ct_result = err;
    s_ct_done   = true;
}

static void rbamp_poll_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Poll task started (interval=%ums)", (unsigned)s_poll_interval);

    bool wdt = false;
#if CONFIG_ACROUTER_RBAMP_TASK_WDT
    wdt = (esp_task_wdt_add(NULL) == ESP_OK);
    if (!wdt) {
        ESP_LOGW(TAG, "task-WDT subscribe failed; continuing without it");
    }
#endif

    uint32_t cycle = 0;
    while (s_poll_running) {
        if (s_rescan_requested) {
            s_rescan_requested = false;
            do_fleet_rescan();
        }
        if (s_addr_change_req) {
            s_addr_change_req = false;
            do_address_change();
        }
        if (s_ct_req) {
            s_ct_req = false;
            do_ct_model();
        }
        if (s_paused) {
            /* Quiescent bus for on-demand discovery — skip polling. */
            if (wdt) esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        size_t count = rbamp_fleet_count(s_fleet);
        if (count > RBAMP_SOURCE_MAX_MODULES) {
            count = RBAMP_SOURCE_MAX_MODULES;
        }

        if (count > 0) {
            size_t n_ok = 0;
            int64_t t0 = esp_timer_get_time();
            esp_err_t err = rbamp_fleet_poll_all(s_fleet, s_snaps, s_status,
                                                 RBAMP_SOURCE_MAX_MODULES, &n_ok);
            uint32_t dt = (uint32_t)(esp_timer_get_time() - t0);
            s_poll_last_us = dt;
            s_poll_avg_us  = s_poll_avg_us ? (s_poll_avg_us * 7 + dt) / 8 : dt;  // EMA/8
            s_poll_count++;
            if (wdt) {
                esp_task_wdt_reset();
            }
            if (err == ESP_OK) {
                /* Cache last snapshot + online status for status/REST getters. */
                for (size_t i = 0; i < count; i++) {
                    s_last_ok[i] = s_status[i].ok;
                    if (s_status[i].ok) {
                        s_last_snap[i] = s_snaps[i];
                        /* Cache the applied CT-model code so the REST GET reads
                         * it here instead of racing the handle. */
                        rbamp_handle_t d = rbamp_fleet_find(s_fleet, s_status[i].addr);
                        uint8_t cm = 0;
                        if (d && rbamp_read_ct_model_ch(d, 0, &cm) == ESP_OK) {
                            s_ct_cache_addr[i] = s_status[i].addr;
                            s_ct_cache_code[i] = cm;
                        }
                    }
                }
                const bool do_log = (cycle % RBAMP_SRC_LOG_EVERY) == 0;
                for (size_t i = 0; i < count; i++) {
                    if (!s_status[i].ok) {
                        continue;
                    }
                    const uint8_t addr = s_status[i].addr;
                    const rbamp_source_role_t role = role_for_addr(addr);
                    publish_snapshot(&s_snaps[i], role, addr);
                    if (do_log) {
                        log_snapshot(&s_snaps[i], addr, role);
                    }
                }
            } else {
                ESP_LOGW(TAG, "fleet poll_all: %s", esp_err_to_name(err));
            }
        } else if (wdt) {
            esp_task_wdt_reset();
        }

        cycle++;

        /* Wait for the next poll trigger. Wake every <=100 ms to re-check the
         * stop flag (so rbamp_source_stop() returns promptly even with a
         * multi-second interval) and to feed the task WDT well under its timeout.
         * When a DRDY GPIO is wired, a DRDY edge cuts the wait short and we poll
         * immediately; the interval is then just a fallback ceiling. */
        uint32_t slept = 0;
        while (s_poll_running && slept < s_poll_interval) {
            uint32_t remain = s_poll_interval - slept;
            uint32_t chunk  = (remain > 100) ? 100 : remain;
            if (s_drdy_gpio >= 0) {
                if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(chunk)) > 0) {
                    if (wdt) {
                        esp_task_wdt_reset();
                    }
                    break;  /* DRDY asserted → fresh data ready, poll now */
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(chunk));
            }
            if (wdt) {
                esp_task_wdt_reset();
            }
            slept += chunk;
        }
    }

#if CONFIG_ACROUTER_RBAMP_TASK_WDT
    if (wdt) {
        esp_task_wdt_delete(NULL);
    }
#endif
    ESP_LOGI(TAG, "Poll task stopped");
    s_poll_task = NULL;
    vTaskDelete(NULL);
}

/* ---- public API ---- */

esp_err_t rbamp_source_configure(const rbamp_source_module_cfg_t *mods, size_t n)
{
    if (!mods || n > RBAMP_SOURCE_MAX_MODULES) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_cfg_mux);
    memcpy(s_role_cfg, mods, n * sizeof(s_role_cfg[0]));
    s_role_cfg_count = n;
    portEXIT_CRITICAL(&s_cfg_mux);
    ESP_LOGI(TAG, "Configured %u module role(s)", (unsigned)n);
    return ESP_OK;
}

esp_err_t rbamp_source_set_role(uint8_t addr, rbamp_source_role_t role)
{
    esp_err_t rc = ESP_OK;
    portENTER_CRITICAL(&s_cfg_mux);
    /* Update existing entry (or remove it if role == NONE). */
    for (size_t i = 0; i < s_role_cfg_count; i++) {
        if (s_role_cfg[i].i2c_addr == addr) {
            if (role == RBAMP_ROLE_NONE) {
                for (size_t j = i + 1; j < s_role_cfg_count; j++) {
                    s_role_cfg[j - 1] = s_role_cfg[j];
                }
                s_role_cfg_count--;
            } else {
                s_role_cfg[i].role = role;
            }
            goto done;
        }
    }
    if (role == RBAMP_ROLE_NONE) {
        goto done; /* nothing to remove */
    }
    if (s_role_cfg_count >= RBAMP_SOURCE_MAX_MODULES) {
        rc = ESP_ERR_NO_MEM;
        goto done;
    }
    s_role_cfg[s_role_cfg_count].i2c_addr = addr;
    s_role_cfg[s_role_cfg_count].role     = role;
    s_role_cfg_count++;
done:
    portEXIT_CRITICAL(&s_cfg_mux);
    return rc;
}

esp_err_t rbamp_source_get_roles(rbamp_source_module_cfg_t *mods, size_t max, size_t *n)
{
    if (!mods || !n) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_cfg_mux);
    size_t cnt = (s_role_cfg_count < max) ? s_role_cfg_count : max;
    memcpy(mods, s_role_cfg, cnt * sizeof(mods[0]));
    portEXIT_CRITICAL(&s_cfg_mux);
    *n = cnt;
    return ESP_OK;
}

esp_err_t rbamp_source_get_modules(rbamp_source_module_info_t *out, size_t max, size_t *n)
{
    if (!out || !n) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t cnt = 0;
    if (s_fleet != NULL) {
        size_t fc = rbamp_fleet_count(s_fleet);
        for (size_t i = 0; i < fc && cnt < max; i++) {
            rbamp_handle_t d = rbamp_fleet_get(s_fleet, i);
            if (d == NULL) {
                continue;
            }
            uint8_t addr = rbamp_address(d);
            out[cnt].i2c_addr    = addr;
            out[cnt].channels    = rbamp_channels(d);
            out[cnt].has_voltage = rbamp_has_voltage_hw(d);
            out[cnt].role        = role_for_addr(addr);
            out[cnt].online      = s_last_ok[i];
            out[cnt].voltage      = s_last_snap[i].voltage;
            out[cnt].current      = s_last_snap[i].current[0];
            out[cnt].power        = s_last_snap[i].power[0];
            out[cnt].power_factor = s_last_snap[i].power_factor[0];
            out[cnt].frequency    = s_last_snap[i].frequency;
            cnt++;
        }
    }
    *n = cnt;
    return ESP_OK;
}

esp_err_t rbamp_source_save_config(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(RBAMP_SRC_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open (save): %s", esp_err_to_name(err));
        return err;
    }
    nvs_set_u8(nvs, "n", (uint8_t)s_role_cfg_count);
    char key[8];
    for (size_t i = 0; i < s_role_cfg_count; i++) {
        snprintf(key, sizeof(key), "a%u", (unsigned)i);
        nvs_set_u8(nvs, key, s_role_cfg[i].i2c_addr);
        snprintf(key, sizeof(key), "r%u", (unsigned)i);
        nvs_set_u8(nvs, key, (uint8_t)s_role_cfg[i].role);
    }
    err = nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Saved %u role(s) to NVS", (unsigned)s_role_cfg_count);
    return err;
}

esp_err_t rbamp_source_load_config(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(RBAMP_SRC_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err; /* ESP_ERR_NVS_NOT_FOUND if never saved — caller falls back */
    }
    uint8_t cnt = 0;
    if (nvs_get_u8(nvs, "n", &cnt) != ESP_OK) {
        nvs_close(nvs);
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (cnt > RBAMP_SOURCE_MAX_MODULES) {
        cnt = RBAMP_SOURCE_MAX_MODULES;
    }
    size_t loaded = 0;
    char key[8];
    for (uint8_t i = 0; i < cnt; i++) {
        uint8_t a = 0, r = 0;
        snprintf(key, sizeof(key), "a%u", (unsigned)i);
        if (nvs_get_u8(nvs, key, &a) != ESP_OK) {
            continue;
        }
        snprintf(key, sizeof(key), "r%u", (unsigned)i);
        nvs_get_u8(nvs, key, &r);
        s_role_cfg[loaded].i2c_addr = a;
        s_role_cfg[loaded].role     = (rbamp_source_role_t)r;
        loaded++;
    }
    s_role_cfg_count = loaded;
    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded %u role(s) from NVS", (unsigned)loaded);
    return ESP_OK;
}

esp_err_t rbamp_source_init(uint8_t bus_num)
{
    if (s_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = i2c_bus_get_handle(bus_num);
    if (bus == NULL) {
        ESP_LOGE(TAG, "I2C bus %u not initialized", bus_num);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = rbamp_fleet_create(bus, &s_fleet);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fleet create: %s", esp_err_to_name(err));
        return err;
    }

#if CONFIG_ACROUTER_RBAMP_AUTOSCAN
#if CONFIG_IDF_TARGET_ESP32C2
    /* ESP32-C2 warm-up. On the C2 i2c_master driver the FIRST transaction to a
     * device after the bus has been idle returns stale/0, so the library's
     * one-shot variant/capability read at enumeration misparses as UNKNOWN
     * (the register itself reads correctly once warm). The bus has been idle
     * since the boot scan, so exercise every address right here — the cold
     * transaction is absorbed by this throwaway scan instead of by the variant
     * read microseconds later. Two passes: settle, then the real read is warm. */
    for (int pass = 0; pass < 2; pass++) {
        uint8_t warm[16];
        uint8_t wcount = 0;
        i2c_bus_scan(bus_num, warm, sizeof(warm), &wcount);
    }
#endif

    size_t added = 0;
    err = rbamp_fleet_scan(s_fleet, /*match_product=*/true, &added);
    if (err == ESP_ERR_INVALID_STATE) {
        /* Bus compromised — fleet left empty. Surface, but stay initialized. */
        ESP_LOGE(TAG, "fleet scan aborted: bus compromised (fix wiring)");
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "fleet scan: %s", esp_err_to_name(err));
    }
    size_t excluded = rbamp_fleet_excluded_count(s_fleet);
    if (excluded > 0) {
        ESP_LOGW(TAG, "%u address(es) excluded (address conflict — provision one at a time)",
                 (unsigned)excluded);
    }
#endif

    s_alive = rbamp_fleet_count(s_fleet);
    s_initialized = true;
    ESP_LOGI(TAG, "Initialized on bus %u: %u rbAmp module(s)",
             bus_num, (unsigned)s_alive);
    return ESP_OK;
}

esp_err_t rbamp_source_start(uint32_t interval_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_poll_task != NULL) {
        return ESP_OK; /* already running */
    }
    s_poll_interval = (interval_ms < 100) ? 100 : interval_ms;
    s_poll_running  = true;

    /* Dual-core (ESP32): pin to APP_CPU (core 1) so the sensor plane runs off the
     * WiFi/comms core (0). Single-core (C2): no affinity — isolation is by priority. */
#if !CONFIG_FREERTOS_UNICORE
    const BaseType_t rbamp_core = 1;
#else
    const BaseType_t rbamp_core = tskNO_AFFINITY;
#endif
    BaseType_t ok = xTaskCreatePinnedToCore(rbamp_poll_task, "rbamp_poll", 4096, NULL,
                                5, &s_poll_task, rbamp_core);
    if (ok != pdPASS) {
        s_poll_running = false;
        ESP_LOGE(TAG, "Failed to create poll task");
        return ESP_ERR_NO_MEM;
    }

    /* Arm the DRDY interrupt now that the poll task exists (the ISR notifies it). */
    if (s_drdy_gpio >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << s_drdy_gpio,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,   /* DRDY is open-drain, idles high */
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_NEGEDGE,    /* active-low: asserts on new data */
        };
        esp_err_t e = gpio_config(&io);
        if (e == ESP_OK) {
            e = gpio_install_isr_service(0);
            if (e == ESP_OK || e == ESP_ERR_INVALID_STATE) {   /* may be pre-installed */
                e = gpio_isr_handler_add((gpio_num_t)s_drdy_gpio, rbamp_drdy_isr, NULL);
            }
        }
        if (e == ESP_OK) {
            s_drdy_isr_installed = true;
            ESP_LOGI(TAG, "DRDY-driven polling on GPIO%d (interval %ums = fallback)",
                     s_drdy_gpio, (unsigned)s_poll_interval);
        } else {
            ESP_LOGW(TAG, "DRDY GPIO%d setup failed (%s); using timer poll",
                     s_drdy_gpio, esp_err_to_name(e));
            s_drdy_gpio = -1;
        }
    }
    return ESP_OK;
}

esp_err_t rbamp_source_set_drdy_gpio(int gpio)
{
    if (s_poll_task != NULL) {
        /* Changing DRDY wiring while polling is not supported; stop first. */
        return ESP_ERR_INVALID_STATE;
    }
    s_drdy_gpio = (gpio < 0) ? -1 : gpio;
    return ESP_OK;
}

void rbamp_source_stop(void)
{
    if (s_poll_task == NULL) {
        return;
    }
    s_poll_running = false;
    /* Wait for the task to exit (it self-deletes). */
    for (int i = 0; i < 50 && s_poll_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_drdy_isr_installed) {
        gpio_isr_handler_remove((gpio_num_t)s_drdy_gpio);
        s_drdy_isr_installed = false;
    }
}

size_t rbamp_source_alive_count(void)
{
    return s_alive;
}

void rbamp_source_get_timing(uint32_t *last_us, uint32_t *avg_us, uint32_t *count)
{
    if (last_us) *last_us = s_poll_last_us;
    if (avg_us)  *avg_us  = s_poll_avg_us;
    if (count)   *count   = s_poll_count;
}

esp_err_t rbamp_source_rescan(void)
{
#if !CONFIG_ACROUTER_I2C_AUTODISCOVERY
    /* Runtime bus rescan disabled at build time (docs/18) — the pause+reprobe
     * stalls the single-core C2. Fleet is fixed from the boot scan. */
    ESP_LOGW(TAG, "Rescan not supported (ACROUTER_I2C_AUTODISCOVERY=n)");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_poll_task != NULL) {
        /* Defer to the poll task so the fleet isn't mutated mid-poll. */
        s_rescan_requested = true;
        ESP_LOGI(TAG, "Rescan requested");
        return ESP_OK;
    }
    /* Not polling — safe to scan inline. */
    return do_fleet_rescan();
#endif
}

esp_err_t rbamp_source_request_address_change(uint8_t cur_addr, uint8_t new_addr)
{
    if (!s_initialized || s_fleet == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (new_addr < 0x08 || new_addr > 0x77 || new_addr == cur_addr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_addr_change_req) {
        return ESP_ERR_INVALID_STATE;   /* one change already queued */
    }
    if (rbamp_fleet_find(s_fleet, cur_addr) == NULL) {
        return ESP_ERR_NOT_FOUND;       /* no module at cur_addr */
    }
    if (rbamp_fleet_find(s_fleet, new_addr) != NULL) {
        return ESP_ERR_INVALID_STATE;   /* target already a known rbAmp (sync 409) */
    }
    s_addr_change_cur    = cur_addr;
    s_addr_change_new    = new_addr;
    s_addr_change_done   = false;
    s_addr_change_result = ESP_OK;
    if (s_poll_task != NULL) {
        s_addr_change_req = true;       /* poll task runs it between cycles */
        ESP_LOGI(TAG, "address change 0x%02X -> 0x%02X queued", cur_addr, new_addr);
        return ESP_OK;
    }
    /* Not polling — run inline. */
    do_address_change();
    return s_addr_change_result;
}

bool rbamp_source_addr_change_pending(void)
{
    return s_addr_change_req;
}

void rbamp_source_pause(bool pause)
{
    s_paused = pause;
}

const rbamp_ct_model_t *rbamp_source_ct_catalog(size_t *count)
{
    if (count) *count = sizeof(s_ct_catalog) / sizeof(s_ct_catalog[0]);
    return s_ct_catalog;
}

esp_err_t rbamp_source_request_ct_model(uint8_t addr, uint8_t code)
{
    if (!s_initialized || s_fleet == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ct_req) {
        return ESP_ERR_INVALID_STATE;   /* one change already queued */
    }
    if (rbamp_fleet_find(s_fleet, addr) == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    s_ct_addr   = addr;
    s_ct_code   = code;
    s_ct_done   = false;
    s_ct_result = ESP_OK;
    if (s_poll_task != NULL) {
        s_ct_req = true;                /* poll task runs it between cycles */
        return ESP_OK;
    }
    do_ct_model();
    return s_ct_result;
}

esp_err_t rbamp_source_get_ct_model(uint8_t addr, uint8_t *code)
{
    if (!code) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < RBAMP_SOURCE_MAX_MODULES; i++) {
        if (s_ct_cache_addr[i] == addr) {
            *code = s_ct_cache_code[i];
            return ESP_OK;
        }
    }
    *code = 0;
    return ESP_ERR_NOT_FOUND;
}
