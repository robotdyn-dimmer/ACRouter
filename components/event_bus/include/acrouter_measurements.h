/**
 * @file acrouter_measurements.h
 * @brief Unified measurement data structure for ACRouter
 *
 * This is the universal measurement currency of the system.
 * All measurement sources (internal ADC, DimmerLink I2C, ESP-NOW)
 * produce data in this format. The Sensor Hub merges multiple
 * sources and RouterController consumes the merged result.
 *
 * Replaces the earlier per-source measurement structs.
 */

#ifndef ACROUTER_MEASUREMENTS_H
#define ACROUTER_MEASUREMENTS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Measurement source type
 */
typedef enum {
    ACROUTER_SOURCE_NONE    = 0,   ///< No source / invalid
    ACROUTER_SOURCE_ADC     = 1,   ///< Internal ESP32 ADC (legacy, removed in v2.0)
    ACROUTER_SOURCE_I2C     = 2,   ///< I2C DimmerLink module
    ACROUTER_SOURCE_ESPNOW  = 3,   ///< ESP-NOW wireless module
    ACROUTER_SOURCE_MQTT    = 4,   ///< External MQTT source (future)
} acrouter_source_t;

/**
 * @brief Current direction indicator
 */
typedef enum {
    ACROUTER_DIR_CONSUMING = 0,    ///< Consuming power from grid (+)
    ACROUTER_DIR_SUPPLYING = 1,    ///< Supplying power to grid (-)
    ACROUTER_DIR_ZERO      = 2,    ///< Zero or negligible
    ACROUTER_DIR_UNKNOWN   = 3,    ///< Not enough data
} acrouter_direction_t;

/**
 * @brief Current channel indices
 */
typedef enum {
    ACROUTER_CH_LOAD  = 0,         ///< Load current
    ACROUTER_CH_GRID  = 1,         ///< Grid current
    ACROUTER_CH_SOLAR = 2,         ///< Solar current
    ACROUTER_CH_COUNT = 3,         ///< Number of current channels
} acrouter_current_ch_t;

/**
 * @brief Unified measurement data structure
 *
 * All measurement sources fill this structure.
 * Fields that a source cannot provide should be left at 0 with valid=false
 * or with corresponding has_* flags cleared.
 */
typedef struct {
    // RMS values
    float voltage_rms;                              ///< RMS voltage (V), 0 if not available
    float current_rms[ACROUTER_CH_COUNT];           ///< RMS current (A) per channel
    float power_active[ACROUTER_CH_COUNT];          ///< Active power (W), + import / - export

    // Direction
    acrouter_direction_t direction[ACROUTER_CH_COUNT]; ///< Current direction per channel

    // Metadata
    uint64_t timestamp_us;                          ///< Measurement timestamp (esp_timer_get_time)
    acrouter_source_t source;                       ///< Data source type
    uint8_t source_id;                              ///< Source instance ID (e.g., DimmerLink slot)
    bool valid;                                     ///< Overall data validity

    // Availability flags (which fields are populated)
    bool has_voltage;                               ///< voltage_rms is valid
    bool has_current[ACROUTER_CH_COUNT];            ///< current_rms[ch] is valid
    bool has_power[ACROUTER_CH_COUNT];              ///< power_active[ch] is valid
} acrouter_measurements_t;

/**
 * @brief Initialize measurements struct to safe defaults
 */
static inline void acrouter_measurements_init(acrouter_measurements_t* m) {
    m->voltage_rms = 0.0f;
    for (int i = 0; i < ACROUTER_CH_COUNT; i++) {
        m->current_rms[i] = 0.0f;
        m->power_active[i] = 0.0f;
        m->direction[i] = ACROUTER_DIR_UNKNOWN;
        m->has_current[i] = false;
        m->has_power[i] = false;
    }
    m->timestamp_us = 0;
    m->source = ACROUTER_SOURCE_NONE;
    m->source_id = 0;
    m->valid = false;
    m->has_voltage = false;
}

#ifdef __cplusplus
}
#endif

#endif // ACROUTER_MEASUREMENTS_H
