/**
 * @file rbamp_source.h
 * @brief rbAmp I2C sensor source for ACRouter v2.0 sensing offload.
 *
 * Polls a fleet of rbAmp sensor modules over the shared I2C bus and feeds
 * their measurements into the Sensor Hub as ACROUTER_SOURCE_I2C events
 * (priority 0). This is the v2.0 replacement for local ADC sensing — rbAmp
 * modules do the current/voltage/power DSP on-device (5 Hz commit).
 *
 * Design mirrors dimmerlink_manager: each module maps to a measurement role
 * (grid / solar / load / voltage); the poll task builds an
 * acrouter_measurements_t per module and posts ACROUTER_EVENT_POWER_UPDATE.
 * RouterController/sensor_hub need no changes — they are source-agnostic.
 *
 * Lifecycle:
 *   rbamp_source_init(bus)        -- create fleet, discover/begin modules
 *   rbamp_source_configure(...)   -- (optional) assign roles by address
 *   rbamp_source_start(interval)  -- start the 5 Hz poll task
 *   rbamp_source_stop()
 *
 * Gated by CONFIG_ACROUTER_RBAMP_SOURCE (default off). Safe with no hardware.
 */
#ifndef RBAMP_SOURCE_H
#define RBAMP_SOURCE_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum rbAmp modules tracked by this source.
 *  Kept at 4 (covers grid/solar/load/voltage roles) so the shared Sensor Hub
 *  cache (MAX_SOURCES=8) keeps headroom for the ADC + DimmerLink sources and
 *  never hits its "cache full, dropping update" path. */
#define RBAMP_SOURCE_MAX_MODULES 4

/**
 * @brief Measurement role of a module's primary channel → Sensor Hub slot.
 *
 * RBAMP_ROLE_NONE modules are still polled and logged (diagnostic) but do not
 * feed the Sensor Hub until a role is assigned.
 */
typedef enum {
    RBAMP_ROLE_NONE    = 0,  ///< Polled/logged only, not mapped to a slot
    RBAMP_ROLE_GRID    = 1,  ///< current[0]/power[0] -> ACROUTER_CH_GRID
    RBAMP_ROLE_SOLAR   = 2,  ///< current[0]/power[0] -> ACROUTER_CH_SOLAR
    RBAMP_ROLE_LOAD    = 3,  ///< current[0]/power[0] -> ACROUTER_CH_LOAD
    RBAMP_ROLE_VOLTAGE = 4,  ///< voltage -> voltage slot (voltage-only module)
} rbamp_source_role_t;

/** Per-module configuration (address + role). */
typedef struct {
    uint8_t              i2c_addr;  ///< 7-bit rbAmp module address
    rbamp_source_role_t  role;      ///< Sensor Hub role for the primary channel
} rbamp_source_module_cfg_t;

/**
 * @brief Assign Sensor Hub roles to modules by address (commissioning).
 *
 * Updates the role of already-discovered modules whose address matches, and
 * records the mapping for modules discovered later. Call before or after
 * rbamp_source_init(); takes effect on the next poll cycle.
 *
 * @param mods  Array of address→role mappings.
 * @param n     Number of entries (must be <= RBAMP_SOURCE_MAX_MODULES).
 * @return ESP_OK, or ESP_ERR_INVALID_ARG on null/oversized input.
 */
esp_err_t rbamp_source_configure(const rbamp_source_module_cfg_t *mods, size_t n);

/**
 * @brief Assign/replace the role for a single module address (commissioning).
 *
 * Upserts one address→role mapping: updates the role if @p addr is already
 * configured, otherwise appends it. Takes effect on the next poll cycle.
 * Persist across reboot with rbamp_source_save_config().
 *
 * @param addr  7-bit module address.
 * @param role  Role (RBAMP_ROLE_NONE removes the mapping).
 * @return ESP_OK, or ESP_ERR_NO_MEM if the table is full.
 */
esp_err_t rbamp_source_set_role(uint8_t addr, rbamp_source_role_t role);

/**
 * @brief Read the current address→role table (for status / REST).
 *
 * @param mods  Output buffer.
 * @param max   Capacity of @p mods.
 * @param n     Receives the number of entries written.
 * @return ESP_OK.
 */
esp_err_t rbamp_source_get_roles(rbamp_source_module_cfg_t *mods, size_t max, size_t *n);

/** @brief Live identity + last snapshot of a discovered (fleet) module. */
typedef struct {
    uint8_t             i2c_addr;     ///< 7-bit address
    uint8_t             channels;     ///< valid current channels (1..3)
    bool                has_voltage;  ///< voltage hardware detected at begin
    rbamp_source_role_t role;         ///< assigned Sensor Hub role (NONE if unconfigured)
    bool                online;       ///< read OK on the last poll cycle
    // Last snapshot (primary channel [0]); NaN where unavailable.
    float               voltage;      ///< RMS voltage, V
    float               current;      ///< RMS current, A
    float               power;        ///< real power, W (signed: + import / - export)
    float               power_factor; ///< -1..+1
    float               frequency;    ///< mains frequency, Hz
} rbamp_source_module_info_t;

/**
 * @brief List modules currently in the fleet (discovered) with their role.
 *
 * @param out  Output buffer.
 * @param max  Capacity of @p out.
 * @param n    Receives the number written.
 * @return ESP_OK.
 */
esp_err_t rbamp_source_get_modules(rbamp_source_module_info_t *out, size_t max, size_t *n);

/** @brief Persist the address→role table to NVS. */
esp_err_t rbamp_source_save_config(void);

/** @brief Load the address→role table from NVS (no-op if none stored). */
esp_err_t rbamp_source_load_config(void);

/**
 * @brief Re-scan the I2C bus to pick up modules added after boot.
 *
 * Autoscan otherwise runs only at init, so a module wired/powered later is not
 * polled until reboot. When the poll task is running the scan is deferred to it
 * (executed between poll cycles — race-free, since the task owns the fleet);
 * otherwise it runs inline.
 *
 * @return ESP_OK (queued or done), ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t rbamp_source_rescan(void);

/**
 * @brief Initialize the rbAmp source on a bus (does not start polling).
 *
 * Acquires the shared bus handle from i2c_bus, creates an rbAmp fleet, and
 * (if CONFIG_ACROUTER_RBAMP_AUTOSCAN) discovers + begins rbAmp modules on the
 * bus. Applies any roles set via rbamp_source_configure().
 *
 * @param bus_num  I2C bus number (typically 0).
 * @return ESP_OK on success (even if zero modules found),
 *         ESP_ERR_INVALID_STATE if the bus is not initialized,
 *         or a propagated allocation/transport error.
 */
esp_err_t rbamp_source_init(uint8_t bus_num);

/**
 * @brief Start the polling task.
 * @param interval_ms  Poll cadence (>= 100; 200 ms matches rbAmp's 5 Hz floor).
 * @return ESP_OK, ESP_ERR_INVALID_STATE if not initialized, ESP_ERR_NO_MEM.
 */
esp_err_t rbamp_source_start(uint32_t interval_ms);

/**
 * @brief Enable DRDY-driven polling on a GPIO (call BEFORE rbamp_source_start).
 *
 * rbAmp DRDY (open-drain, active-low) asserts when a fresh measurement set is
 * ready. When wired, the poll task waits on a DRDY interrupt instead of a fixed
 * delay — reading exactly when data is ready (lowest latency, no double reads);
 * the poll interval then acts only as a fallback ceiling. Best for a SINGLE
 * critical module: a multi-module fleet has unsynchronised DRDY lines (one GPIO
 * cannot represent the whole fleet), so the fixed-cadence timer poll (gpio < 0,
 * the default) is the correct choice there.
 *
 * @param gpio  DRDY input GPIO, or < 0 to disable (fixed-cadence timer poll).
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if the poll task is already running.
 */
esp_err_t rbamp_source_set_drdy_gpio(int gpio);

/** @brief Stop the polling task (blocks until it exits). */
void rbamp_source_stop(void);

/** @brief Pause/resume polling (quiescent bus for on-demand discovery). */
void rbamp_source_pause(bool pause);

/**
 * @brief Queue a two-phase I2C address change for the module at @p cur_addr.
 *
 * Async by design: validates (range + a module present at cur_addr) synchronously,
 * then the actual reassign runs in the poll-task context (library two-phase
 * commit + reset + on-bus conflict check). The module re-appears at @p new_addr
 * on the next discovery; any role mapping is migrated and persisted. Poll on
 * rbamp_source_get_roles / the module list to observe completion.
 *
 * @return ESP_OK (queued/accepted); ESP_ERR_INVALID_ARG (new_addr out of
 *         0x08..0x77 or == cur); ESP_ERR_NOT_FOUND (no module at cur_addr);
 *         ESP_ERR_INVALID_STATE (not initialized, or a change already queued).
 */
esp_err_t rbamp_source_request_address_change(uint8_t cur_addr, uint8_t new_addr);

/** @brief True while a queued address change is still pending execution. */
bool rbamp_source_addr_change_pending(void);

/** SCT-013 CT-model catalog entry (firmware source of truth). */
typedef struct {
    const char *id;        ///< stable id, e.g. "sct013-030"
    const char *name;      ///< display name, e.g. "SCT-013-030"
    uint8_t     rated_a;   ///< rated current (A)
    uint8_t     code;      ///< REG_CT_MODEL preset code for set_ct_model_ch()
    bool        available; ///< false = code assigned but preset unimplemented (v1.3)
} rbamp_ct_model_t;

/** @brief The SCT-013 model catalog (firmware SOT). @param count receives length. */
const rbamp_ct_model_t *rbamp_source_ct_catalog(size_t *count);

/**
 * @brief Queue a CT-model change (channel 0) for the module at @p addr.
 *
 * Verify-then-set in the poll task: reads the applied code and writes ONLY if it
 * differs (writing a preset overwrites per-unit factory gain cal on v1.3). Sets
 * sensor class SCT-013 then the per-channel preset. @p code must be an
 * `available` catalog code.
 *
 * @return ESP_OK (queued/applied); ESP_ERR_NOT_FOUND (no module at addr);
 *         ESP_ERR_INVALID_STATE (not initialized, or a change already queued).
 */
esp_err_t rbamp_source_request_ct_model(uint8_t addr, uint8_t code);

/**
 * @brief Applied CT-model code for @p addr from the poll-task cache (0 = unset).
 * @return ESP_OK (found), ESP_ERR_NOT_FOUND (addr not cached), ESP_ERR_INVALID_ARG.
 */
esp_err_t rbamp_source_get_ct_model(uint8_t addr, uint8_t *code);

/** @brief Number of modules that responded during init. */
size_t rbamp_source_alive_count(void);

/**
 * @brief Poll-cycle I2C timing (for the `timing` debug readout).
 * @param last_us  Receives the last poll cycle's I2C duration (us). May be NULL.
 * @param avg_us   Receives the EMA of cycle I2C duration (us). May be NULL.
 * @param count    Receives the number of completed poll cycles. May be NULL.
 */
void rbamp_source_get_timing(uint32_t *last_us, uint32_t *avg_us, uint32_t *count);

#ifdef __cplusplus
}
#endif

#endif /* RBAMP_SOURCE_H */
