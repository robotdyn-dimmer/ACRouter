/**
 * @file acrouter_events.h
 * @brief ACRouter event bus definitions
 *
 * Uses ESP-IDF default event loop for decoupled inter-component communication.
 * Components post events; any number of listeners can subscribe.
 *
 * The default event loop is created by WiFiManager (esp_event_loop_create_default).
 * All events run in the context of the default event loop task.
 */

#ifndef ACROUTER_EVENTS_H
#define ACROUTER_EVENTS_H

#include "esp_event.h"
#include "acrouter_measurements.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ACRouter event base declaration
 *
 * All ACRouter events share this base. Register with:
 *   esp_event_handler_register(ACROUTER_EVENT, event_id, handler, arg)
 */
ESP_EVENT_DECLARE_BASE(ACROUTER_EVENT);

/**
 * @brief ACRouter event IDs
 */
typedef enum {
    /**
     * @brief Power measurement update from any single source
     *
     * Posted by: rbAmp (I2C), DimmerLink, ESP-NOW sources
     * Data: acrouter_measurements_t*
     * Rate: ~200ms per source
     */
    ACROUTER_EVENT_POWER_UPDATE = 0,

    /**
     * @brief Merged measurement update from Sensor Hub
     *
     * Posted by: Sensor Hub (after merging all sources with priority logic)
     * Data: acrouter_measurements_t*
     * Rate: ~200ms
     * Subscribers: RouterController, MQTTManager, WebServerManager
     */
    ACROUTER_EVENT_MERGED_UPDATE,

    /**
     * @brief Router operating mode changed
     *
     * Posted by: RouterController
     * Data: acrouter_mode_event_t*
     */
    ACROUTER_EVENT_MODE_CHANGE,

    /**
     * @brief Output device state changed (dimmer level, relay on/off)
     *
     * Posted by: dimmer_manager, relay_manager
     * Data: acrouter_device_event_t*
     */
    ACROUTER_EVENT_DEVICE_STATE,

} acrouter_event_id_t;

/**
 * @brief Mode change event data
 */
typedef struct {
    uint8_t old_mode;       ///< Previous RouterMode value
    uint8_t new_mode;       ///< New RouterMode value
} acrouter_mode_event_t;

/**
 * @brief Device type for events
 */
typedef enum {
    ACROUTER_DEVICE_DIMMER = 0,
    ACROUTER_DEVICE_RELAY  = 1,
} acrouter_device_type_t;

/**
 * @brief Device state change event data
 */
typedef struct {
    acrouter_device_type_t type;    ///< Device type
    uint8_t id;                     ///< Device ID
    uint8_t level;                  ///< Current level (0-100 for dimmers, 0/100 for relays)
    bool is_on;                     ///< true if device is active
} acrouter_device_event_t;

#ifdef __cplusplus
}
#endif

#endif // ACROUTER_EVENTS_H
