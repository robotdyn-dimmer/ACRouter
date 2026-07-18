/**
 * @file sensor_hub.h
 * @brief Sensor Hub - unified measurement merging with source priority
 *
 * Subscribes to ACROUTER_EVENT_POWER_UPDATE from all sources:
 *   - Internal ADC (legacy/removed, source=ACROUTER_SOURCE_ADC)
 *   - DimmerLink I2C (dimmerlink_manager, source=ACROUTER_SOURCE_I2C)
 *   - ESP-NOW modules (future, source=ACROUTER_SOURCE_ESPNOW)
 *
 * For each measurement slot (voltage, current_grid, current_solar, current_load),
 * the hub picks the highest-priority fresh source and publishes a merged
 * ACROUTER_EVENT_MERGED_UPDATE event.
 *
 * Priority (lower number = higher priority):
 *   I2C / ESP-NOW: 0  (external smart modules)
 *   Internal ADC:  1  (fallback)
 *
 * Staleness threshold: if a source has no update for SENSOR_HUB_STALE_MS,
 * it is ignored and the next priority source is used.
 *
 * RouterController subscribes to ACROUTER_EVENT_MERGED_UPDATE.
 */

#ifndef SENSOR_HUB_H
#define SENSOR_HUB_H

#include "esp_err.h"
#include "acrouter_measurements.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Staleness timeout — if no update from a source in this time, fall back */
#define SENSOR_HUB_STALE_MS     500

/** Priority for external sources (I2C, ESP-NOW) */
#define SENSOR_HUB_PRIO_EXTERNAL  0

/** Priority for internal ADC */
#define SENSOR_HUB_PRIO_ADC       1

/** Number of tracked measurement slots */
#define SENSOR_HUB_SLOTS    4   /* voltage, grid, solar, load */

/**
 * @brief Slot indices (match acrouter_current_ch_t + voltage)
 */
typedef enum {
    SH_SLOT_VOLTAGE  = 0,
    SH_SLOT_GRID     = 1,
    SH_SLOT_SOLAR    = 2,
    SH_SLOT_LOAD     = 3,
} sh_slot_t;

/**
 * @brief Per-slot source tracking
 */
typedef struct {
    float               value;          ///< Last known value (A or V)
    float               power;          ///< Active power (W), if available
    acrouter_direction_t direction;     ///< Direction
    uint64_t            timestamp_us;   ///< When this slot was last updated
    acrouter_source_t   source;         ///< Which source provided this value
    uint8_t             source_id;      ///< Source instance ID
    uint8_t             priority;       ///< Priority of current source
    bool                has_power;      ///< power field is valid
    bool                valid;          ///< Slot has data
} sh_slot_state_t;

/**
 * @brief Sensor hub state snapshot
 */
typedef struct {
    sh_slot_state_t slots[SENSOR_HUB_SLOTS];
    uint64_t        last_merge_us;  ///< Timestamp of last merge
    uint32_t        merge_count;    ///< Total merges performed
} sensor_hub_state_t;

/**
 * @brief Initialize sensor hub and subscribe to events
 *
 * Must be called after esp_event_loop_create_default() (done by WiFiManager).
 *
 * @return ESP_OK on success
 */
esp_err_t sensor_hub_init(void);

/**
 * @brief Check if sensor hub is initialized
 */
bool sensor_hub_is_initialized(void);

/**
 * @brief Get current hub state (read-only snapshot)
 *
 * Thread-safe: copies state under mutex.
 *
 * @param state Output state structure
 */
void sensor_hub_get_state(sensor_hub_state_t* state);

/**
 * @brief Get the active source for a measurement slot
 *
 * @param slot  Slot index (SH_SLOT_*)
 * @return Source type, or ACROUTER_SOURCE_NONE if no data
 */
acrouter_source_t sensor_hub_get_slot_source(sh_slot_t slot);

/**
 * @brief Check if any I2C source is actively providing data
 */
bool sensor_hub_has_i2c_source(void);

/**
 * @brief Check if internal ADC is being used (as primary or fallback)
 */
bool sensor_hub_is_adc_active(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_HUB_H */
