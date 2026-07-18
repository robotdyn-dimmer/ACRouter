/**
 * @file device_registry.h
 * @brief L2 device-entity layer — identify + register connected modules.
 *
 * Transport-agnostic (I2C now, ESP-NOW later). This first piece is the
 * VERSION-gate identifier (rbamp-dev protocol) that classifies a device at an
 * I2C address into a family/model/channels — the core of auto-discovery.
 * See MODULE_ARCHITECTURE.md.
 */
#ifndef DEVICE_REGISTRY_H
#define DEVICE_REGISTRY_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DEV_FAMILY_UNKNOWN = 0,
    DEV_FAMILY_RBAMP,          /**< smart sensor, PRODUCT_ID 0x01 (v1.3+) */
    DEV_FAMILY_RBDIMMER,       /**< smart multi-ch dimmer module, PRODUCT_ID 0x02 (v1.3+) — not built yet */
    DEV_FAMILY_LEGACY_DIMMER,  /**< legacy DimmerLink single-channel dimmer (VERSION<=0x03) */
    DEV_FAMILY_LEGACY_SENSOR,  /**< legacy sensor (VERSION<=0x03) */
} device_family_t;

typedef struct {
    uint8_t         bus;
    uint8_t         addr;
    uint8_t         version;     /**< REG_VERSION 0x03 */
    device_family_t family;
    uint8_t         product_id;  /**< 0x54 — authoritative only when version>=0x04 */
    uint8_t         hw_variant;  /**< 0x55 */
    uint8_t         channels;    /**< derived (rbAmp variant; legacy dimmer=1) */
    bool            has_uid;
    uint8_t         uid[12];     /**< 0x5C-0x67 — 96-bit chip serial (version>=0x04) */
} device_ident_t;

/** @brief Human-readable family name. */
const char* device_family_name(device_family_t f);

/**
 * @brief Identify an I2C device via the VERSION-gate protocol (rbamp-dev).
 *
 * Reads VERSION (0x03) FIRST. version>=0x04 -> v1.3+ identity block is valid:
 * PRODUCT_ID (0x54, 0x01=rbAmp/0x02=rbDimmer), HW_VARIANT (0x55), UID (0x5C, 12B).
 * version<=0x03 -> LEGACY (0x54 is CS_CONFIG, NOT product-id): classify behaviorally
 * via DIM0_LEVEL (0x10) write+readback -> dimmer, else sensor. Uses STOP-protocol
 * reads; safe as the single bus master.
 *
 * @return ESP_OK, or a transport error if the device does not answer VERSION.
 */
esp_err_t device_identify(uint8_t bus, uint8_t addr, device_ident_t *out);

/* ================================================================
 * Registry — unified device inventory (entities) with persisted config.
 * ================================================================ */

#define DEVREG_MAX_DEVICES 16
#define DEVREG_MAX_CH      7
#define DEVREG_NAME_LEN    24   /* per-channel user-facing name (incl. NUL) */

typedef enum {
    DEV_TRANSPORT_I2C    = 0,
    DEV_TRANSPORT_ESPNOW = 1,   /* future */
} device_transport_t;

typedef struct {
    bool               valid;
    device_transport_t transport;
    uint8_t            bus;        /* i2c */
    uint8_t            addr;       /* i2c */
    bool               has_uid;
    uint8_t            uid[12];
    device_family_t    family;
    uint8_t            hw_variant;
    uint8_t            channels;
    uint8_t            roles[DEVREG_MAX_CH]; /* per-channel role (user config, preserved) */
    bool               online;     /* runtime, refreshed on scan */
    /* per-channel user-facing name (e.g. "Boiler heater"); empty => UI falls back to
     * role/identity. User config, preserved across reconcile. Kept LAST so a struct
     * grow stays backward-detectable in NVS. */
    char               name[DEVREG_MAX_CH][DEVREG_NAME_LEN];
} device_entry_t;

/** @brief Load the persisted registry from NVS (entries start offline). */
esp_err_t devreg_init(void);

/**
 * @brief On-demand I2C scan + NON-DESTRUCTIVE reconcile (operator model).
 *
 * Pauses module polling (quiescent bus), scans, identifies each device, then:
 * present at same transport+addr → keep config, refresh identity+online; new →
 * add; missing → mark OFFLINE but KEEP (never deletes/overwrites). Persists +
 * resumes polling.
 */
esp_err_t devreg_scan_i2c(uint8_t bus);

/** @brief Number of registry entries (valid, incl. offline). */
size_t devreg_count(void);

/** @brief Entry by index (0..devreg_count()-1), or NULL. */
const device_entry_t* devreg_get(size_t idx);

/** Unified role enum (maps to rbamp_source_role_t / dl_role_t under the hood). */
typedef enum {
    DEV_ROLE_NONE = 0,
    DEV_ROLE_GRID,
    DEV_ROLE_SOLAR,
    DEV_ROLE_LOAD,
    DEV_ROLE_VOLTAGE,
    DEV_ROLE_DIMMER,
    DEV_ROLE_RELAY,
} device_role_t;

/** @brief Role name / parse. */
const char* device_role_name(device_role_t r);
device_role_t device_role_parse(const char* s);   /* DEV_ROLE_NONE on unknown */

/**
 * @brief Roles VALID for a family (roles are family-specific, not one flat list):
 *   sensors (rbAmp / legacy-sensor) → none|grid|solar|load|voltage (what it measures);
 *   dimmers (rbDimmer / DimmerLink)  → dimmer (output — role implied by family);
 *   relay                            → relay.
 * @return count written to @p out.
 */
size_t device_family_valid_roles(device_family_t f, device_role_t* out, size_t max);

/** @brief Is @p role valid for @p family? */
bool device_role_valid_for_family(device_family_t f, device_role_t role);

/**
 * @brief Assign a per-channel role in the registry (SoT) and bridge it to the
 * driver (rbAmp sensor roles → rbamp_source). Persists. channel 0 = the module
 * role for single-channel devices.
 * @return ESP_OK; ESP_ERR_NOT_FOUND (no device at addr); ESP_ERR_INVALID_ARG.
 */
esp_err_t devreg_set_role(uint8_t bus, uint8_t addr, uint8_t channel, device_role_t role);

/**
 * @brief Set a per-channel user-facing name in the registry (SoT for ALL families).
 * Unified naming: DimmerLink/rbDimmer/relay/rbAmp all name their channels here,
 * keyed by (addr, channel) like roles. Persisted; preserved across reconcile.
 * channel 0 = the device name for single-channel modules. Empty name clears it.
 * @return ESP_OK; ESP_ERR_NOT_FOUND (no device at addr); ESP_ERR_INVALID_ARG.
 */
esp_err_t devreg_set_name(uint8_t bus, uint8_t addr, uint8_t channel, const char* name);

/** @brief Push all persisted registry roles down to the drivers (call at boot,
 * after the drivers init, so the registry is authoritative). */
void devreg_sync_roles(void);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGISTRY_H */
