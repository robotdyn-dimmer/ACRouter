/**
 * @file device_registry.c
 * @brief L2 device identification (VERSION-gate). See device_registry.h.
 */
#include "device_registry.h"
#include "i2c_bus.h"
#include "rbamp_source.h"
#include "dimmerlink_manager.h"
#include "dimmer_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static const char* TAG = "DevReg";
#define DEVREG_NVS_NS   "devreg"
#define DEVREG_NVS_KEY  "devs"

/* Register map (rbamp-dev VERSION-gate protocol). NOTE: on legacy (version<=0x03)
 * 0x54 is CS_CONFIG (default 0x01), NOT PRODUCT_ID — reading it as product-id is
 * exactly the trap that mis-classified legacy DimmerLink as rbAmp. Gate on VERSION. */
#define REG_VERSION       0x03
#define REG_DIM0_LEVEL    0x10
#define REG_PRODUCT_ID    0x54  /* valid only when VERSION>=0x04 */
#define REG_HW_VARIANT    0x55
#define REG_UID           0x5C  /* 12 bytes, 0x5C-0x67 */
#define PRODUCT_ID_RBAMP     0x01
#define PRODUCT_ID_RBDIMMER  0x02

const char* device_family_name(device_family_t f) {
    switch (f) {
        case DEV_FAMILY_RBAMP:         return "rbAmp";
        case DEV_FAMILY_RBDIMMER:      return "rbDimmer";
        case DEV_FAMILY_LEGACY_DIMMER: return "DimmerLink(legacy)";
        case DEV_FAMILY_LEGACY_SENSOR: return "legacy-sensor";
        default:                       return "unknown";
    }
}

/* rbAmp HW_VARIANT -> current-channel count. 1=UI1,2=UI2,3=UI3,4=I1,5=I2,6=I3
 * -> 1,2,3,1,2,3. (7ch/UI7 = future F040; 0 = unknown.) */
static uint8_t rbamp_variant_channels(uint8_t variant) {
    if (variant >= 1 && variant <= 6) return ((variant - 1) % 3) + 1;
    return 0;
}

esp_err_t device_identify(uint8_t bus, uint8_t addr, device_ident_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->bus  = bus;
    out->addr = addr;

    uint8_t ver = 0;
    /* Warm-up: the legacy DimmerLink returns a stale/garbled first read after the
     * bus has been idle — a throwaway read lets the decisive one land clean. */
    (void)i2c_bus_read_reg_stop(bus, addr, REG_VERSION, &ver, 1);
    esp_err_t err = i2c_bus_read_reg_stop(bus, addr, REG_VERSION, &ver, 1);
    if (err != ESP_OK) return err;
    out->version = ver;

    /* v1.3+ identity block is valid ONLY for a PLAUSIBLE version. 0xFF / 0x00 are
     * garbled reads (legacy DimmerLink flakiness) — NOT "version 255": routing
     * them through the v1.3+ path would read CS_CONFIG(0x54)=0x01 as product-id and
     * misclassify a legacy DimmerLink as rbAmp. Plausible v1.3+ = 0x04..0x3F. */
    if (ver >= 0x04 && ver <= 0x3F) {
        /* v1.3+ identity block is valid. */
        uint8_t pid = 0, variant = 0;
        i2c_bus_read_reg_stop(bus, addr, REG_PRODUCT_ID, &pid, 1);
        i2c_bus_read_reg_stop(bus, addr, REG_HW_VARIANT, &variant, 1);
        i2c_bus_read_reg_stop(bus, addr, REG_UID, out->uid, sizeof(out->uid));
        out->product_id = pid;
        out->hw_variant = variant;
        out->has_uid = true;
        if (pid == PRODUCT_ID_RBAMP) {
            out->family = DEV_FAMILY_RBAMP;
            out->channels = rbamp_variant_channels(variant);
        } else if (pid == PRODUCT_ID_RBDIMMER) {
            out->family = DEV_FAMILY_RBDIMMER;
            out->channels = 0;  /* multi-ch count part of rbDimmer (not built) */
        } else {
            out->family = DEV_FAMILY_UNKNOWN;
        }
    } else {
        /* Legacy (VERSION<=0x03). rbamp-dev's behavioral DIM0 write+readback split
         * is unreliable here — the legacy DimmerLink firmware's reads are too flaky
         * (readback lag/garbage, bench-confirmed even on a quiescent bus). Classify
         * by CONTEXT instead: in ACRouter v2.0 all sensors are v1.3+ rbAmp (clean
         * VERSION>=0x04 reads), so a legacy device on the bus is a DimmerLink dimmer.
         * Non-destructive: no probe writes. HW_VARIANT best-effort. */
        out->family   = DEV_FAMILY_LEGACY_DIMMER;
        out->channels = 1;  /* legacy = single-channel; multi-ch is future rbDimmer */
        i2c_bus_read_reg_stop(bus, addr, REG_HW_VARIANT, &out->hw_variant, 1);
    }
    return ESP_OK;
}

/* ================================================================
 * Registry
 * ================================================================ */

static device_entry_t s_devices[DEVREG_MAX_DEVICES];

static int devreg_find_i2c(uint8_t bus, uint8_t addr) {
    for (int i = 0; i < DEVREG_MAX_DEVICES; i++) {
        if (s_devices[i].valid && s_devices[i].transport == DEV_TRANSPORT_I2C &&
            s_devices[i].bus == bus && s_devices[i].addr == addr) {
            return i;
        }
    }
    return -1;
}

static device_role_t seed_role_from_driver(device_family_t family, uint8_t addr);  /* fwd */

static int devreg_free_slot(void) {
    for (int i = 0; i < DEVREG_MAX_DEVICES; i++) {
        if (!s_devices[i].valid) return i;
    }
    return -1;
}

esp_err_t devreg_save(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(DEVREG_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, DEVREG_NVS_KEY, s_devices, sizeof(s_devices));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

esp_err_t devreg_init(void) {
    memset(s_devices, 0, sizeof(s_devices));
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(DEVREG_NVS_NS, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved registry (first boot)");
        return ESP_OK;
    }
    if (err != ESP_OK) return err;
    size_t sz = sizeof(s_devices);
    err = nvs_get_blob(nvs, DEVREG_NVS_KEY, s_devices, &sz);
    nvs_close(nvs);
    if (err == ESP_OK && sz == sizeof(s_devices)) {
        for (int i = 0; i < DEVREG_MAX_DEVICES; i++) {
            s_devices[i].online = false;   /* refreshed on next scan */
        }
        ESP_LOGI(TAG, "Registry loaded (%u entries)", (unsigned)devreg_count());
    } else if (err == ESP_OK) {
        /* Stored blob is a different size (struct changed across an update) — the
         * entry stride no longer matches, so the data is unusable. Reset; a rescan
         * re-populates identity, and roles/names start clean rather than corrupt. */
        memset(s_devices, 0, sizeof(s_devices));
        ESP_LOGW(TAG, "Registry blob size mismatch (%u vs %u) — reset, rescan needed",
                 (unsigned)sz, (unsigned)sizeof(s_devices));
    }
    return ESP_OK;
}

esp_err_t devreg_scan_i2c(uint8_t bus) {
    if (!i2c_bus_is_initialized(bus)) return ESP_ERR_INVALID_STATE;

    /* Quiescent bus: pause polling and let in-flight transactions drain, so the
     * legacy DimmerLink's reads aren't corrupted by the concurrent rbAmp poll. */
    rbamp_source_pause(true);
    dl_manager_pause(true);
    vTaskDelay(pdMS_TO_TICKS(300));

    /* Mark all known devices offline; re-marked online if seen this scan. */
    for (int i = 0; i < DEVREG_MAX_DEVICES; i++) {
        if (s_devices[i].valid) s_devices[i].online = false;
    }

    uint8_t found[24];
    uint8_t n = 0;
    i2c_bus_scan(bus, found, sizeof(found), &n);

    size_t added = 0, refreshed = 0;
    for (uint8_t k = 0; k < n; k++) {
        device_ident_t id;
        if (device_identify(bus, found[k], &id) != ESP_OK) continue;

        int idx = devreg_find_i2c(bus, found[k]);
        bool is_new = (idx < 0);
        if (idx < 0) {
            idx = devreg_free_slot();
            if (idx < 0) { ESP_LOGW(TAG, "registry full, skip 0x%02X", found[k]); continue; }
            memset(&s_devices[idx], 0, sizeof(s_devices[idx]));
            s_devices[idx].valid     = true;
            s_devices[idx].transport = DEV_TRANSPORT_I2C;
            s_devices[idx].bus       = bus;
            s_devices[idx].addr      = found[k];
            added++;
        } else {
            refreshed++;   /* KEEP roles/config — refresh identity + online only */
        }
        s_devices[idx].family     = id.family;
        s_devices[idx].hw_variant = id.hw_variant;
        s_devices[idx].channels   = id.channels;
        s_devices[idx].has_uid    = id.has_uid;
        if (id.has_uid) memcpy(s_devices[idx].uid, id.uid, sizeof(id.uid));
        s_devices[idx].online     = true;
        /* Seed a new entry's role from the driver so sync_roles doesn't wipe it. */
        if (is_new) {
            s_devices[idx].roles[0] = (uint8_t)seed_role_from_driver(id.family, found[k]);
        }
    }

    devreg_save();
    rbamp_source_pause(false);
    dl_manager_pause(false);
    ESP_LOGI(TAG, "Scan bus %u: %u present, +%u new, %u refreshed (missing kept offline)",
             bus, n, (unsigned)added, (unsigned)refreshed);
    return ESP_OK;
}

size_t devreg_count(void) {
    size_t c = 0;
    for (int i = 0; i < DEVREG_MAX_DEVICES; i++) if (s_devices[i].valid) c++;
    return c;
}

const device_entry_t* devreg_get(size_t idx) {
    size_t c = 0;
    for (int i = 0; i < DEVREG_MAX_DEVICES; i++) {
        if (s_devices[i].valid) {
            if (c == idx) return &s_devices[i];
            c++;
        }
    }
    return NULL;
}

const char* device_role_name(device_role_t r) {
    switch (r) {
        case DEV_ROLE_GRID:    return "grid";
        case DEV_ROLE_SOLAR:   return "solar";
        case DEV_ROLE_LOAD:    return "load";
        case DEV_ROLE_VOLTAGE: return "voltage";
        case DEV_ROLE_DIMMER:  return "dimmer";
        case DEV_ROLE_RELAY:   return "relay";
        default:               return "none";
    }
}

device_role_t device_role_parse(const char* s) {
    if (!s) return DEV_ROLE_NONE;
    if (!strcmp(s, "grid"))    return DEV_ROLE_GRID;
    if (!strcmp(s, "solar"))   return DEV_ROLE_SOLAR;
    if (!strcmp(s, "load"))    return DEV_ROLE_LOAD;
    if (!strcmp(s, "voltage")) return DEV_ROLE_VOLTAGE;
    if (!strcmp(s, "dimmer"))  return DEV_ROLE_DIMMER;
    if (!strcmp(s, "relay"))   return DEV_ROLE_RELAY;
    return DEV_ROLE_NONE;
}

/* Roles are FAMILY-SPECIFIC — a sensor measures (grid/solar/load/voltage), a
 * dimmer/relay is an output (role implied by family). Not one flat list. */
size_t device_family_valid_roles(device_family_t f, device_role_t* out, size_t max) {
    static const device_role_t SENSOR[] = {
        DEV_ROLE_NONE, DEV_ROLE_GRID, DEV_ROLE_SOLAR, DEV_ROLE_LOAD, DEV_ROLE_VOLTAGE };
    static const device_role_t DIMMER[] = { DEV_ROLE_DIMMER };
    static const device_role_t RELAY[]  = { DEV_ROLE_RELAY };
    const device_role_t* set; size_t n;
    switch (f) {
        case DEV_FAMILY_RBAMP:
        case DEV_FAMILY_LEGACY_SENSOR:  set = SENSOR; n = 5; break;
        case DEV_FAMILY_RBDIMMER:
        case DEV_FAMILY_LEGACY_DIMMER:  set = DIMMER; n = 1; break;
        default: if (max >= 1) { out[0] = DEV_ROLE_NONE; } return (max >= 1) ? 1 : 0;
    }
    size_t c = 0;
    for (; c < n && c < max; c++) out[c] = set[c];
    return c;
}

bool device_role_valid_for_family(device_family_t f, device_role_t role) {
    device_role_t roles[8];
    size_t n = device_family_valid_roles(f, roles, 8);
    for (size_t i = 0; i < n; i++) if (roles[i] == role) return true;
    return false;
}

/* Map a unified sensor role to rbamp_source_role_t; returns false for non-sensor
 * roles (dimmer/relay) which don't bridge to rbAmp. */
static bool role_to_rbamp(device_role_t r, rbamp_source_role_t* out) {
    switch (r) {
        case DEV_ROLE_NONE:    *out = RBAMP_ROLE_NONE;    return true;
        case DEV_ROLE_GRID:    *out = RBAMP_ROLE_GRID;    return true;
        case DEV_ROLE_SOLAR:   *out = RBAMP_ROLE_SOLAR;   return true;
        case DEV_ROLE_LOAD:    *out = RBAMP_ROLE_LOAD;    return true;
        case DEV_ROLE_VOLTAGE: *out = RBAMP_ROLE_VOLTAGE; return true;
        default:               return false;
    }
}

/* On first discovery, inherit the sensor role already configured in the driver
 * (e.g. rbamp_source seeds 0x51=grid from NVS/Kconfig) so a fresh registry entry
 * isn't role=none — otherwise devreg_sync_roles() pushes none back and WIPES the
 * driver's working role, killing sensing. Only for NEW entries; existing configs kept. */
static device_role_t seed_role_from_driver(device_family_t family, uint8_t addr) {
    /* Output families have their role implied by family (a dimmer is a dimmer — the only
     * valid role per device_family_valid_roles). Seed it at discovery so a from-scratch
     * DimmerLink module auto-binds to a dimmer output (bridge_role -> dimmer_bind_i2c)
     * without a manual role step. (No relay device family today; relays are local GPIO.) */
    if (family == DEV_FAMILY_RBDIMMER || family == DEV_FAMILY_LEGACY_DIMMER) {
        return DEV_ROLE_DIMMER;
    }
    if (family != DEV_FAMILY_RBAMP) return DEV_ROLE_NONE;
    rbamp_source_module_cfg_t cfg[RBAMP_SOURCE_MAX_MODULES];
    size_t n = 0;
    if (rbamp_source_get_roles(cfg, RBAMP_SOURCE_MAX_MODULES, &n) != ESP_OK) return DEV_ROLE_NONE;
    for (size_t i = 0; i < n; i++) {
        if (cfg[i].i2c_addr != addr) continue;
        switch (cfg[i].role) {
            case RBAMP_ROLE_GRID:    return DEV_ROLE_GRID;
            case RBAMP_ROLE_SOLAR:   return DEV_ROLE_SOLAR;
            case RBAMP_ROLE_LOAD:    return DEV_ROLE_LOAD;
            case RBAMP_ROLE_VOLTAGE: return DEV_ROLE_VOLTAGE;
            default:                 return DEV_ROLE_NONE;
        }
    }
    return DEV_ROLE_NONE;
}

/* Bridge one entry's channel-0 role to its driver. */
static void bridge_role(const device_entry_t* d) {
    if (d->family == DEV_FAMILY_RBAMP) {
        rbamp_source_role_t rr;
        if (role_to_rbamp((device_role_t)d->roles[0], &rr)) {
            rbamp_source_set_role(d->addr, rr);
        }
    } else if (d->family == DEV_FAMILY_LEGACY_DIMMER || d->family == DEV_FAMILY_RBDIMMER) {
        /* A dimmer with role=dimmer becomes an I2C dimmer the RouterController
         * drives (transport-agnostic output abstraction). */
        if ((device_role_t)d->roles[0] == DEV_ROLE_DIMMER) {
            dimmer_bind_i2c(d->bus, d->addr);
        }
    }
    /* relay role wiring: relays are local GPIO today (Tier-1), joins later. */
}

esp_err_t devreg_set_role(uint8_t bus, uint8_t addr, uint8_t channel, device_role_t role) {
    if (channel >= DEVREG_MAX_CH) return ESP_ERR_INVALID_ARG;
    int idx = devreg_find_i2c(bus, addr);
    if (idx < 0) return ESP_ERR_NOT_FOUND;
    if (!device_role_valid_for_family(s_devices[idx].family, role)) {
        return ESP_ERR_INVALID_ARG;   /* e.g. a sensor role on a dimmer */
    }
    s_devices[idx].roles[channel] = (uint8_t)role;
    bridge_role(&s_devices[idx]);
    if (s_devices[idx].family == DEV_FAMILY_RBAMP) rbamp_source_save_config();
    devreg_save();
    return ESP_OK;
}

esp_err_t devreg_set_name(uint8_t bus, uint8_t addr, uint8_t channel, const char* name) {
    if (channel >= DEVREG_MAX_CH || !name) return ESP_ERR_INVALID_ARG;
    int idx = devreg_find_i2c(bus, addr);
    if (idx < 0) return ESP_ERR_NOT_FOUND;
    strncpy(s_devices[idx].name[channel], name, DEVREG_NAME_LEN - 1);
    s_devices[idx].name[channel][DEVREG_NAME_LEN - 1] = '\0';
    devreg_save();
    return ESP_OK;
}

void devreg_sync_roles(void) {
    for (int i = 0; i < DEVREG_MAX_DEVICES; i++) {
        if (!s_devices[i].valid) continue;
        /* Don't push a none role at boot — it would wipe a role the driver already
         * holds (e.g. rbamp_source's Kconfig-seeded grid), killing sensing. An explicit
         * clear-to-none from the user still goes through devreg_set_role(). */
        if (s_devices[i].roles[0] == DEV_ROLE_NONE) continue;
        bridge_role(&s_devices[i]);
    }
}
