/**
 * @file sensor_hub.c
 * @brief Sensor Hub implementation
 *
 * Merges measurements from multiple sources (ADC, I2C, ESP-NOW) with
 * priority-based selection and staleness detection.
 * Publishes ACROUTER_EVENT_MERGED_UPDATE after each merge.
 */

#include "sensor_hub.h"
#include "acrouter_events.h"
#include <math.h>          // isfinite() — drop NaN/Inf from a glitching source
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "SensorHub";

/* ================================================================
 * Internal per-source cache
 * Tracks the latest measurement from each source separately
 * ================================================================ */

#define MAX_SOURCES     8   /* max concurrent source instances */
/* A source-cache slot older than this many stale-windows is reclaimed, so a
 * re-addressed or removed module (its old (source,source_id) key abandoned) never
 * permanently occupies a slot and exhausts the cache (D10). */
#define SENSOR_HUB_REAP_FACTOR 10

typedef struct {
    acrouter_measurements_t meas;
    uint64_t    received_us;
    bool        in_use;
} source_cache_t;

static source_cache_t s_sources[MAX_SOURCES];
static sensor_hub_state_t s_state;
static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;

/* ================================================================
 * Priority helper
 * ================================================================ */

static uint8_t source_priority(acrouter_source_t src) {
    switch (src) {
        case ACROUTER_SOURCE_I2C:
        case ACROUTER_SOURCE_ESPNOW:
            return SENSOR_HUB_PRIO_EXTERNAL;
        case ACROUTER_SOURCE_ADC:
        default:
            return SENSOR_HUB_PRIO_ADC;
    }
}

/* ================================================================
 * Merge logic
 *
 * For each slot: iterate all fresh sources, pick the one with the
 * lowest priority number (highest priority). If tie, prefer the
 * most recent timestamp.
 * ================================================================ */

static void do_merge(void) {
    uint64_t now_us = esp_timer_get_time();
    uint64_t stale_threshold_us = (uint64_t)SENSOR_HUB_STALE_MS * 1000;

    /* Build merged measurement */
    acrouter_measurements_t merged;
    acrouter_measurements_init(&merged);
    merged.timestamp_us = now_us;
    merged.source = ACROUTER_SOURCE_NONE;
    merged.valid = false;

    /* Track which source wins each slot */
    uint8_t           slot_best_prio[SENSOR_HUB_SLOTS];
    uint64_t          slot_best_ts[SENSOR_HUB_SLOTS];
    acrouter_source_t slot_best_source[SENSOR_HUB_SLOTS];
    uint8_t           slot_best_source_id[SENSOR_HUB_SLOTS];
    for (int s = 0; s < SENSOR_HUB_SLOTS; s++) {
        slot_best_prio[s] = 255;
        slot_best_ts[s] = 0;
        slot_best_source[s] = ACROUTER_SOURCE_NONE;
        slot_best_source_id[s] = 0;
    }

    /* Iterate all cached sources */
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (!s_sources[i].in_use) continue;

        /* Check staleness */
        if ((now_us - s_sources[i].received_us) > stale_threshold_us) continue;

        const acrouter_measurements_t* m = &s_sources[i].meas;
        if (!m->valid) continue;

        uint8_t prio = source_priority(m->source);

        /* Voltage slot — drop non-finite (NaN/Inf from a driver glitch) so it never
         * reaches the merged state / control loop / telemetry (MAJOR-7). */
        if (m->has_voltage && isfinite(m->voltage_rms)) {
            if (prio < slot_best_prio[SH_SLOT_VOLTAGE] ||
                (prio == slot_best_prio[SH_SLOT_VOLTAGE] && s_sources[i].received_us > slot_best_ts[SH_SLOT_VOLTAGE])) {
                slot_best_prio[SH_SLOT_VOLTAGE] = prio;
                slot_best_ts[SH_SLOT_VOLTAGE] = s_sources[i].received_us;
                slot_best_source[SH_SLOT_VOLTAGE] = m->source;
                slot_best_source_id[SH_SLOT_VOLTAGE] = m->source_id;
                merged.voltage_rms = m->voltage_rms;
                merged.has_voltage = true;
            }
        }

        /* Current/power slots: grid=1, solar=2, load=0 */
        const struct { int ch; sh_slot_t slot; } ch_map[] = {
            { ACROUTER_CH_LOAD,  SH_SLOT_LOAD  },
            { ACROUTER_CH_GRID,  SH_SLOT_GRID  },
            { ACROUTER_CH_SOLAR, SH_SLOT_SOLAR },
        };

        for (int k = 0; k < 3; k++) {
            int ch = ch_map[k].ch;
            sh_slot_t sl = ch_map[k].slot;
            if (!m->has_current[ch] || !isfinite(m->current_rms[ch])) continue;  /* drop NaN/Inf */

            if (prio < slot_best_prio[sl] ||
                (prio == slot_best_prio[sl] && s_sources[i].received_us > slot_best_ts[sl])) {
                slot_best_prio[sl] = prio;
                slot_best_ts[sl] = s_sources[i].received_us;
                slot_best_source[sl] = m->source;
                slot_best_source_id[sl] = m->source_id;
                merged.current_rms[ch]  = m->current_rms[ch];
                merged.direction[ch]    = m->direction[ch];
                merged.has_current[ch]  = true;
                if (m->has_power[ch] && isfinite(m->power_active[ch])) {
                    merged.power_active[ch] = m->power_active[ch];
                    merged.has_power[ch] = true;
                }
            }
        }
    }

    /* merged is valid if at least one slot has data */
    merged.valid = merged.has_voltage ||
                   merged.has_current[ACROUTER_CH_GRID] ||
                   merged.has_current[ACROUTER_CH_SOLAR] ||
                   merged.has_current[ACROUTER_CH_LOAD];

    if (!merged.valid) return;

    /* Mark source as mixed (multiple sources possible) */
    merged.source = ACROUTER_SOURCE_ADC;  /* will be overwritten below */
    /* Use highest-priority source that contributed */
    for (int s = 0; s < MAX_SOURCES; s++) {
        if (!s_sources[s].in_use) continue;
        if ((now_us - s_sources[s].received_us) > stale_threshold_us) continue;
        if (source_priority(s_sources[s].meas.source) < source_priority(merged.source)) {
            merged.source = s_sources[s].meas.source;
        }
    }

    /* Update slot state under mutex */
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (int s = 0; s < SENSOR_HUB_SLOTS; s++) {
        s_state.slots[s].valid = false;
    }

    if (merged.has_voltage) {
        s_state.slots[SH_SLOT_VOLTAGE].value        = merged.voltage_rms;
        s_state.slots[SH_SLOT_VOLTAGE].valid        = true;
        s_state.slots[SH_SLOT_VOLTAGE].timestamp_us = now_us;
        s_state.slots[SH_SLOT_VOLTAGE].priority     = slot_best_prio[SH_SLOT_VOLTAGE];
        s_state.slots[SH_SLOT_VOLTAGE].source       = slot_best_source[SH_SLOT_VOLTAGE];
        s_state.slots[SH_SLOT_VOLTAGE].source_id    = slot_best_source_id[SH_SLOT_VOLTAGE];
    }

    const struct { int ch; sh_slot_t sl; } ch_map2[] = {
        { ACROUTER_CH_LOAD,  SH_SLOT_LOAD  },
        { ACROUTER_CH_GRID,  SH_SLOT_GRID  },
        { ACROUTER_CH_SOLAR, SH_SLOT_SOLAR },
    };
    for (int k = 0; k < 3; k++) {
        int ch = ch_map2[k].ch;
        sh_slot_t sl = ch_map2[k].sl;
        if (!merged.has_current[ch]) continue;
        s_state.slots[sl].value        = merged.current_rms[ch];
        s_state.slots[sl].direction    = merged.direction[ch];
        s_state.slots[sl].valid        = true;
        s_state.slots[sl].timestamp_us = now_us;
        s_state.slots[sl].priority     = slot_best_prio[sl];
        s_state.slots[sl].source       = slot_best_source[sl];
        s_state.slots[sl].source_id    = slot_best_source_id[sl];
        if (merged.has_power[ch]) {
            s_state.slots[sl].power     = merged.power_active[ch];
            s_state.slots[sl].has_power = true;
        }
    }

    s_state.last_merge_us = now_us;
    s_state.merge_count++;

    xSemaphoreGive(s_mutex);

    /* Post merged event */
    esp_event_post(ACROUTER_EVENT, ACROUTER_EVENT_MERGED_UPDATE,
                   &merged, sizeof(merged), 0);
}

/* ================================================================
 * Event handler - called from ESP-IDF event loop task
 * ================================================================ */

static void on_power_update(void* arg, esp_event_base_t base,
                            int32_t id, void* event_data) {
    const acrouter_measurements_t* m = (const acrouter_measurements_t*)event_data;
    if (!m || !m->valid) return;

    const uint64_t now_us = esp_timer_get_time();
    const uint64_t reap_us = (uint64_t)SENSOR_HUB_STALE_MS * 1000ULL * SENSOR_HUB_REAP_FACTOR;

    /* Find/allocate the slot under s_mutex so the cross-task readers
     * (sensor_hub_has_i2c_source/is_adc_active) never see a torn write (D6).
     * do_merge() runs after the unlock — it is on this same task, so the s_sources[]
     * it then reads cannot be mid-written, and it takes s_mutex itself for s_state. */
    int free_slot = -1;
    int found_slot = -1;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (s_sources[i].in_use &&
            s_sources[i].meas.source == m->source &&
            s_sources[i].meas.source_id == m->source_id) {
            found_slot = i;
            break;
        }
        /* Reclaim a long-dead slot (re-addressed/removed module) — D10. */
        if (s_sources[i].in_use && (now_us - s_sources[i].received_us) > reap_us) {
            s_sources[i].in_use = false;
        }
        if (!s_sources[i].in_use && free_slot < 0) free_slot = i;
    }

    int slot = (found_slot >= 0) ? found_slot : free_slot;
    if (slot >= 0) {
        s_sources[slot].meas = *m;
        s_sources[slot].received_us = now_us;
        s_sources[slot].in_use = true;
    }
    xSemaphoreGive(s_mutex);

    if (slot < 0) {
        ESP_LOGW(TAG, "Source cache full, dropping update");
        return;
    }

    /* Merge all sources and publish */
    do_merge();
}

/* ================================================================
 * Public API
 * ================================================================ */

esp_err_t sensor_hub_init(void) {
    if (s_initialized) return ESP_OK;

    memset(s_sources, 0, sizeof(s_sources));
    memset(&s_state, 0, sizeof(s_state));

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Subscribe to raw power updates from all sources */
    esp_err_t err = esp_event_handler_register(
        ACROUTER_EVENT, ACROUTER_EVENT_POWER_UPDATE,
        on_power_update, NULL);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Sensor Hub initialized (stale=%dms, sources=%d)",
             SENSOR_HUB_STALE_MS, MAX_SOURCES);
    return ESP_OK;
}

bool sensor_hub_is_initialized(void) {
    return s_initialized;
}

void sensor_hub_get_state(sensor_hub_state_t* out) {
    if (!out) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(out, &s_state, sizeof(sensor_hub_state_t));
    xSemaphoreGive(s_mutex);
}

acrouter_source_t sensor_hub_get_slot_source(sh_slot_t slot) {
    if (slot >= SENSOR_HUB_SLOTS) return ACROUTER_SOURCE_NONE;
    acrouter_source_t src = ACROUTER_SOURCE_NONE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_state.slots[slot].valid) {
        src = s_state.slots[slot].source;
    }
    xSemaphoreGive(s_mutex);
    return src;
}

bool sensor_hub_has_i2c_source(void) {
    if (!s_mutex) return false;
    uint64_t now_us = esp_timer_get_time();
    uint64_t threshold_us = (uint64_t)SENSOR_HUB_STALE_MS * 1000;
    bool found = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);   /* mutually exclusive with the s_sources[] write (D6) */
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (!s_sources[i].in_use) continue;
        if ((now_us - s_sources[i].received_us) > threshold_us) continue;
        if (s_sources[i].meas.source == ACROUTER_SOURCE_I2C ||
            s_sources[i].meas.source == ACROUTER_SOURCE_ESPNOW) {
            found = true;
            break;
        }
    }
    xSemaphoreGive(s_mutex);
    return found;
}

bool sensor_hub_is_adc_active(void) {
    if (!s_mutex) return false;
    uint64_t now_us = esp_timer_get_time();
    uint64_t threshold_us = (uint64_t)SENSOR_HUB_STALE_MS * 1000;
    bool found = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);   /* mutually exclusive with the s_sources[] write (D6) */
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (!s_sources[i].in_use) continue;
        if ((now_us - s_sources[i].received_us) > threshold_us) continue;
        if (s_sources[i].meas.source == ACROUTER_SOURCE_ADC) { found = true; break; }
    }
    xSemaphoreGive(s_mutex);
    return found;
}
