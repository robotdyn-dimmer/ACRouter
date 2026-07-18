/**
 * @file esp_now_source.h
 * @brief ESP-NOW wireless measurement source for ACRouter v2.0.
 *
 * Receives rbAmp measurements from wireless nodes over ESP-NOW (rbgrid wire
 * protocol, espnow_proto.h) and feeds them into the Sensor Hub as
 * ACROUTER_SOURCE_ESPNOW events (priority 0), exactly like rbamp_source does
 * for the I2C path — the merge/routing layer is transport-agnostic.
 *
 * Scope (minimal, per operator): RX rbAmp measurements only. NO master/PTP
 * time-sync, NO period/billing, NO beacon time-master. Dimmer control TX to the
 * node is a later addition (with the deferred dimmer work).
 *
 * Bring-up runs OPEN (unencrypted): ESP-NOW delivers unicast/broadcast frames
 * from any sender to the recv callback without a registered peer, so no keys are
 * needed to receive. CCMP + pairing is a later phase.
 *
 * Coexistence: attaches to ACRouter's already-running WiFi (AP or STA). The node
 * must be on ACRouter's channel — for the bench, set the node's channel to match
 * (ACRouter in AP mode uses CONFIG_ACROUTER_ESPNOW_CHANNEL / its AP channel).
 *
 * Gated by CONFIG_ACROUTER_ESPNOW_SOURCE (default off). Safe with no node.
 */
#ifndef ESP_NOW_SOURCE_H
#define ESP_NOW_SOURCE_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Max wireless nodes tracked (kept small for Sensor Hub cache headroom). */
#define ESP_NOW_SOURCE_MAX_NODES 4

/** Measurement role of a node's primary channel → Sensor Hub slot. */
typedef enum {
    ESPNOW_ROLE_NONE    = 0,  ///< Seen/logged only, not mapped to a slot
    ESPNOW_ROLE_GRID    = 1,
    ESPNOW_ROLE_SOLAR   = 2,
    ESPNOW_ROLE_LOAD    = 3,
    ESPNOW_ROLE_VOLTAGE = 4,
} esp_now_source_role_t;

/** Live view of a seen node. */
typedef struct {
    uint8_t               mac[6];
    esp_now_source_role_t role;
    bool                  online;       ///< a REALTIME frame arrived recently
    float                 voltage;      ///< V (NaN/0 if none)
    float                 current;      ///< A (primary channel)
    float                 power;        ///< W (signed)
    float                 power_factor; ///< -1..+1
    float                 frequency;    ///< Hz
} esp_now_source_node_info_t;

/**
 * @brief Initialize the ESP-NOW source (does not start polling/inject).
 * Must be called after WiFi is up (esp_wifi started). Loads the node→role
 * registry from NVS.
 * @return ESP_OK, or a propagated esp_now_init error.
 */
esp_err_t esp_now_source_init(void);

/** @brief Start receiving + the inject task (posts to the Sensor Hub). */
esp_err_t esp_now_source_start(void);

/** @brief Stop the source (deinit ESP-NOW, stop inject task). */
void esp_now_source_stop(void);

/**
 * @brief Assign a Sensor Hub role to a node by MAC (commissioning), persisted.
 * @param mac   6-byte node MAC.
 * @param role  Role (NONE removes the mapping).
 * @return ESP_OK, or ESP_ERR_NO_MEM if the table is full.
 */
esp_err_t esp_now_source_set_role(const uint8_t mac[6], esp_now_source_role_t role);

/** @brief List currently seen nodes (identity + role + last snapshot). */
esp_err_t esp_now_source_get_nodes(esp_now_source_node_info_t *out, size_t max, size_t *n);

/** @brief Persist / load the node→role registry (NVS). */
esp_err_t esp_now_source_save_config(void);
esp_err_t esp_now_source_load_config(void);

/** @brief Number of nodes seen since start. */
size_t esp_now_source_seen_count(void);

/* ================================================================
 * OUTPUT NODES (dimmer / relay over ESP-NOW) — hub side.
 * Discovery = HELLO (node broadcasts family + per-output capability); control =
 * SET_OUTPUT (hub->node unicast); the hub re-asserts each desired output on a
 * keep-alive cadence (<= RBN_OUTPUT_FAILSAFE_MS/2) so the node never falls to its
 * failsafe while the link is healthy. Transport-agnostic mate of the I2C dimmer
 * path: RouterController sets a level, the dispatcher routes it here for ESP-NOW.
 * ================================================================ */

#define ESP_NOW_SOURCE_OUT_NODES_MAX 4   ///< tracked output nodes
#define ESP_NOW_SOURCE_OUT_PER_NODE  4   ///< outputs per node

/** Per-output snapshot within an output node. */
typedef struct {
    uint8_t  output_id;
    uint8_t  kind;          ///< RBN_OUT_KIND_DIMMER / RBN_OUT_KIND_RELAY
    uint16_t range_min;
    uint16_t range_max;
    uint16_t desired;       ///< hub-commanded value (‰ dimmer / 0|1 relay)
    bool     desired_set;   ///< the hub is actively driving this output
    uint16_t applied;       ///< last value the node reported applied
    uint8_t  result;        ///< last OUTPUT_STATE result (0 OK/1 clamped/2 unknown/3 kind)
} esp_now_source_output_info_t;

/** Live view of an output node (discovered via HELLO). */
typedef struct {
    uint8_t  mac[6];
    uint8_t  family;        ///< RBN_FAMILY_DIMMER / RBN_FAMILY_RELAY
    uint16_t hw_model;
    uint8_t  out_count;
    bool     online;        ///< HELLO or OUTPUT_STATE seen recently
    bool     failsafe;      ///< node reported a failsafe trip
    esp_now_source_output_info_t outputs[ESP_NOW_SOURCE_OUT_PER_NODE];
} esp_now_source_output_node_info_t;

/**
 * @brief Drive an output on a node: record the desired value (hub-authoritative,
 * re-asserted by keep-alive) and send a SET_OUTPUT immediately. Idempotent.
 * @param mac        node MAC (from HELLO).
 * @param output_id  target output on that node.
 * @param kind       RBN_OUT_KIND_* (must match the output; node rejects mismatch).
 * @param value      dimmer 0..1000‰ / relay 0|1 (node clamps to range).
 * @param ramp_ms    dimmer fade (0=immediate); ignored for relay.
 * @return ESP_OK; ESP_ERR_NOT_FOUND if no such node/output; propagated send error.
 */
esp_err_t esp_now_source_set_output(const uint8_t mac[6], uint8_t output_id,
                                    uint8_t kind, uint16_t value, uint16_t ramp_ms);

/** @brief List discovered output nodes (identity + per-output desired/applied). */
esp_err_t esp_now_source_get_output_nodes(esp_now_source_output_node_info_t *out,
                                          size_t max, size_t *n);

#ifdef __cplusplus
}
#endif

#endif /* ESP_NOW_SOURCE_H */
