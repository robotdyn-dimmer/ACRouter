/**
 * @file RouterController.cpp
 * @brief Solar Router Controller Implementation
 */

#include "RouterController.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// Dedicated control-loop heartbeat UART (docs/18 §11): an independent debug channel on
// UART1/GPIO10 → external USB-UART (e.g. COM25), separate from the UART0 console. The
// control task writes the heartbeat here with a DIRECT uart_write (bypassing the ESP_LOG
// allocation path) so it survives a heap-wedge — a true liveness probe. C2-only: on the
// classic ESP32 GPIO10 is SPI flash, so there we keep the console heartbeat only.
#if CONFIG_IDF_TARGET_ESP32C2
#include "driver/uart.h"
#define ROUTER_HB_UART       UART_NUM_1
#define ROUTER_HB_UART_TX    10
#define ROUTER_HB_UART_BAUD  115200
#define ROUTER_HB_UART_EN    1
#else
#define ROUTER_HB_UART_EN    0
#endif

// Isolated control task placement. On a dual-core target (ESP32) the control loop is
// pinned to APP_CPU (core 1) so WiFi/LWIP/httpd/MQTT on PRO_CPU (core 0) can never
// preempt it. On a single-core target (C2, FREERTOS_UNICORE) there is only one core —
// isolation is by PRIORITY instead (control preempts the lower-priority web/MQTT tasks).
#if !CONFIG_FREERTOS_UNICORE
#define ROUTER_CTRL_CORE     1
#else
#define ROUTER_CTRL_CORE     tskNO_AFFINITY
#endif
#define ROUTER_CTRL_PRIO     10     // above web/MQTT (~5), below WiFi (~23)
#define ROUTER_CTRL_STACK    4096
#define ROUTER_CTRL_TICK_MS  1000   // wake at least this often to pet the Task-WDT
#define ROUTER_CTRL_HB_US    5000000LL  // control-loop heartbeat every 5 s

static const char* TAG = "RouterCtrl";

// ============================================================
// Singleton Instance
// ============================================================

RouterController& RouterController::getInstance() {
    static RouterController instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================

RouterController::RouterController()
    : m_dimmer_id(0)
    , m_target_level(0.0f)
    , m_manual_level(0)
    , m_grid_current_limit_a(RouterConfig::DEFAULT_GRID_CURRENT_LIMIT_A)
    , m_active_priority_count(0)
    , m_multi_device_mode(false)
    , m_priority_mutex(nullptr)
    , m_ctrl_queue(nullptr)
    , m_ctrl_task(nullptr)
    , m_initialized(false)
{
    // Guards the priority map against a rebuild (MQTT/web task) racing the control
    // loop's iteration. Created here so it exists before begin()'s first rebuild.
    m_priority_mutex = xSemaphoreCreateMutex();
}

RouterController::~RouterController() {
    // Free allocated device arrays
    for (uint8_t i = 0; i < m_active_priority_count; i++) {
        if (m_priority_levels[i].devices) {
            delete[] m_priority_levels[i].devices;
        }
    }
}

// ============================================================
// Initialization
// ============================================================

bool RouterController::begin(uint8_t dimmer_id) {
    // Check if dimmer manager is initialized
    if (!dimmer_manager_is_initialized()) {
        ESP_LOGE(TAG, "Dimmer manager is not initialized");
        return false;
    }

    // Check if relay manager is initialized
    if (!relay_manager_is_initialized()) {
        ESP_LOGW(TAG, "Relay manager is not initialized - relays will not be available");
    }

    // Check if dimmer exists and is enabled
    if (!dimmer_is_enabled(dimmer_id)) {
        ESP_LOGW(TAG, "Dimmer %d is not enabled, RouterController will use it anyway", dimmer_id);
    }

    m_dimmer_id = dimmer_id;
    m_multi_device_mode = false;  // Legacy single-dimmer mode
    m_initialized = true;

    // Start in OFF mode
    m_status.mode = RouterMode::OFF;
    m_status.state = RouterState::IDLE;
    m_status.dimmer_percent = 0;
    m_target_level = 0.0f;
    m_status.valid = true;

    // Ensure dimmer is off
    dimmer_set_level(m_dimmer_id, 0);

    // Build priority map (for future multi-device support)
    rebuildPriorityMap();

    ESP_LOGI(TAG, "RouterController initialized, dimmer_id=%d (legacy mode)", dimmer_id);
    return true;
}

void RouterController::setPrimaryDimmer(uint8_t id) {
    if (m_dimmer_id == id) {
        return;  // already the primary — nothing to do (called every reconcile cycle)
    }
    ESP_LOGI(TAG, "RouterController primary dimmer %u -> %u (runtime auto-bound output)",
             m_dimmer_id, id);
    m_dimmer_id = id;
    // The AUTO cascade drives the priority map (built at begin() before this dimmer
    // existed); rebuild it so id is included there too.
    refreshPriorityMap();
}

// ============================================================
// Event Bus Integration
// ============================================================

esp_err_t RouterController::subscribeEvents() {
    /* Spin up the dedicated, isolated control task BEFORE registering the event
     * handler, so the handler always has a valid mailbox to enqueue into. The heavy
     * update() runs in this task (own core/priority/WDT), never in the shared event
     * loop — a busy web/MQTT handler can no longer stall the control cadence. */
    if (!m_ctrl_queue) {
        m_ctrl_queue = xQueueCreate(1, sizeof(acrouter_measurements_t));  // latest-wins mailbox
    }
    if (m_ctrl_queue && !m_ctrl_task) {
        BaseType_t ok = xTaskCreatePinnedToCore(
            &RouterController::controlTask, "router_ctrl", ROUTER_CTRL_STACK,
            this, ROUTER_CTRL_PRIO, &m_ctrl_task, ROUTER_CTRL_CORE);
        if (ok != pdPASS) {
            m_ctrl_task = nullptr;  // onPowerUpdateEvent falls back to inline update()
            ESP_LOGE(TAG, "Failed to start control task — control will run inline (degraded)");
        }
    }

    /* Subscribe to MERGED_UPDATE (from Sensor Hub) — preferred path.
     * Sensor Hub already merges all sources (ADC, I2C, ESP-NOW) with
     * priority logic before posting MERGED_UPDATE.
     * If Sensor Hub is not initialized yet, we fall back to POWER_UPDATE. */
    esp_err_t err = esp_event_handler_register(
        ACROUTER_EVENT, ACROUTER_EVENT_MERGED_UPDATE,
        &RouterController::onPowerUpdateEvent, this);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Subscribed to ACROUTER_EVENT_MERGED_UPDATE (Sensor Hub)");
    } else {
        ESP_LOGE(TAG, "Failed to subscribe to events: %s", esp_err_to_name(err));
    }
    return err;
}

void RouterController::onPowerUpdateEvent(void* handler_arg, esp_event_base_t base,
                                          int32_t id, void* event_data) {
    RouterController* self = static_cast<RouterController*>(handler_arg);
    const acrouter_measurements_t* m = static_cast<const acrouter_measurements_t*>(event_data);
    if (!self || !m) {
        return;
    }
    // Fast, non-blocking hand-off to the isolated control task (latest-wins mailbox).
    // This runs in the shared event-loop task, so it must do NO heavy work here.
    // Gate on the TASK (not just the queue): if the task failed to start, the mailbox
    // would have no consumer — fall back to an inline update() so control still runs.
    if (self->m_ctrl_task && self->m_ctrl_queue) {
        xQueueOverwrite(self->m_ctrl_queue, m);
    } else {
        self->update(*m);  // degraded fallback if the control task never started
    }
}

// Dedicated control loop — see header. Consumes the freshest merged measurement and
// runs the control math off the shared event loop, on its own core/priority, WDT-guarded.
void RouterController::controlTask(void* arg) {
    RouterController* self = static_cast<RouterController*>(arg);
    const bool wdt = (esp_task_wdt_add(NULL) == ESP_OK);
    acrouter_measurements_t m;

    ESP_LOGI(TAG, "Control task started (isolated: %s, prio=%d)",
             (ROUTER_CTRL_CORE == tskNO_AFFINITY) ? "single-core/priority" : "core 1",
             ROUTER_CTRL_PRIO);

    // Heartbeat = the isolation verification instrument. A steady rate under web/MQTT
    // load proves the control loop is NOT being starved by the comms plane.
#if ROUTER_HB_UART_EN
    {
        uart_config_t uc = {};
        uc.baud_rate  = ROUTER_HB_UART_BAUD;
        uc.data_bits  = UART_DATA_8_BITS;
        uc.parity     = UART_PARITY_DISABLE;
        uc.stop_bits  = UART_STOP_BITS_1;
        uc.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
        uc.source_clk = UART_SCLK_DEFAULT;
        // TX-only: rx buffer is the driver minimum, tx buffer 0 = blocking write.
        uart_driver_install(ROUTER_HB_UART, 256, 0, 0, NULL, 0);
        uart_param_config(ROUTER_HB_UART, &uc);
        uart_set_pin(ROUTER_HB_UART, ROUTER_HB_UART_TX, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        const char* banner = "\r\n[ACRouter control heartbeat @UART1/GPIO10]\r\n";
        uart_write_bytes(ROUTER_HB_UART, banner, strlen(banner));
    }
#endif
    int64_t hb_last = esp_timer_get_time();
    uint32_t hb_updates = 0;

    for (;;) {
        // Block for the next merged measurement, but wake at least every tick so the
        // Task-WDT stays fed even during a sensor gap. On a gap the control simply
        // holds its last state (staleness→failsafe is a planned follow-up).
        if (xQueueReceive(self->m_ctrl_queue, &m, pdMS_TO_TICKS(ROUTER_CTRL_TICK_MS)) == pdTRUE) {
            self->update(m);
            hb_updates++;
        } else {
            // C2: a full tick (>1s) with no merged measurement means ALL sources are
            // silent (single rbAmp dead / I2C bus fault / ESP-NOW grid node down) — there
            // are no POWER_UPDATE events at all, so update() would otherwise never run and
            // the load would hold its last level forever. Drive update() with an empty
            // (valid, no-data) frame so each mode's failsafe fires: AUTO/ECO/GRID_LIMIT
            // decay toward off, OFFGRID decays on no-solar, OFF/MANUAL/BOOST hold setpoint.
            // (Normal 5 Hz cadence returns from the queue well within the tick, so this
            // never triggers while any source is live.)
            acrouter_measurements_t empty = {};
            empty.valid = true;   // valid frame, all has_* = false → "no data"
            self->update(empty);
        }
        if (wdt) {
            esp_task_wdt_reset();
        }

        const int64_t now = esp_timer_get_time();
        const int64_t dt = now - hb_last;
        if (dt >= ROUTER_CTRL_HB_US) {
            const float rate  = (float)hb_updates * 1e6f / (float)dt;
            const unsigned heap = (unsigned)esp_get_free_heap_size();
            const int mode    = (int)self->m_status.mode;
            ESP_LOGI(TAG, "CTRL hb: %.1f Hz, heap=%u, mode=%d", rate, heap, mode);
#if ROUTER_HB_UART_EN
            // Direct UART write — bypasses the ESP_LOG alloc path, so the heartbeat keeps
            // ticking on COM25 even if the console/log infra is starved under web load.
            char hb[80];
            int n = snprintf(hb, sizeof(hb), "CTRL hb: %.1f Hz heap=%u mode=%d\r\n",
                             rate, heap, mode);
            if (n > 0) uart_write_bytes(ROUTER_HB_UART, hb, (size_t)n);
#endif
            hb_last = now;
            hb_updates = 0;
        }
    }
}

void RouterController::update(const acrouter_measurements_t& m) {
    if (!m_initialized || !m.valid) {
        return;
    }

    m_status.last_update_ms = millis();

    // Extract power values from unified measurements. A channel counts as present only
    // when its has_* flag is set AND the value is finite: a driver glitch can surface a
    // NaN/Inf with has_*=true, and NaN silently defeats every comparison below (clamps and
    // thresholds all read false, so AUTO would never failsafe and could drive garbage).
    // Treat non-finite as "no data" → power defaults to 0 and the flag drops → the mode's
    // failsafe (decay toward off) fires instead. (MAJOR-7/8 / BLOCKER-6/7 from code review.)
    bool  has_grid_power  = m.has_power[ACROUTER_CH_GRID]  && isfinite(m.power_active[ACROUTER_CH_GRID]);
    bool  has_solar_power = m.has_power[ACROUTER_CH_SOLAR] && isfinite(m.power_active[ACROUTER_CH_SOLAR]);
    bool  has_load_power  = m.has_power[ACROUTER_CH_LOAD]  && isfinite(m.power_active[ACROUTER_CH_LOAD]);
    float power_grid  = has_grid_power  ? m.power_active[ACROUTER_CH_GRID]  : 0.0f;
    float power_solar = has_solar_power ? m.power_active[ACROUTER_CH_SOLAR] : 0.0f;
    float power_load  = has_load_power  ? m.power_active[ACROUTER_CH_LOAD]  : 0.0f;

    // Grid current magnitude — available even from a current-only (I) module,
    // which has no power/voltage. Used by GRID_LIMIT mode. Same finite gate.
    bool  has_grid_current = m.has_current[ACROUTER_CH_GRID] && isfinite(m.current_rms[ACROUTER_CH_GRID]);
    float grid_current     = has_grid_current ? m.current_rms[ACROUTER_CH_GRID] : 0.0f;

    m_status.power_grid = power_grid;
    m_status.power_solar = power_solar;
    m_status.power_load = power_load;

    // Serialize the mode processing (which iterates m_priority_levels[] and drives
    // its devices) against rebuildPriorityMap() on the MQTT/web task — otherwise the
    // rebuild's delete[]/realloc frees the arrays under us (use-after-free, D2).
    if (m_priority_mutex) xSemaphoreTake(m_priority_mutex, portMAX_DELAY);

    // Process based on current mode
    switch (m_status.mode) {
        case RouterMode::OFF:
            if (m_status.dimmer_percent != 0) {
                applyDimmerLevel(0);
            }
            m_status.state = RouterState::IDLE;
            break;

        case RouterMode::AUTO:
            // Regulate only with a live grid-power reading; otherwise a stale/lost grid
            // sensor reads 0 W and AUTO would treat it as balanced and hold. Fail safe.
            if (has_grid_power) {
                processAutoMode(power_grid);
            } else {
                failsafeDecay();
            }
            break;

        case RouterMode::ECO:
            if (has_grid_power) {
                processEcoMode(power_grid);
            } else {
                failsafeDecay();
            }
            break;

        case RouterMode::OFFGRID:
            // OFFGRID tracks solar power. Regulate only with a live, finite solar reading;
            // if the solar source is stale/lost/NaN, fail toward off instead of tracking a
            // defaulted 0 (BLOCKER-6).
            if (!has_solar_power) {
                failsafeDecay();
            } else if (power_solar > m_status.balance_threshold) {
                float available_power = power_solar * 0.8f;
                float delta = available_power / m_status.control_gain;
                float new_level = m_target_level + delta;
                applyDimmerLevel(new_level);
                updateState(power_solar);
            } else {
                if (m_target_level > 0) {
                    applyDimmerLevel(m_target_level - 1.0f);
                }
                m_status.state = RouterState::DECREASING;
            }
            break;

        case RouterMode::MANUAL:
            if (m_status.dimmer_percent != m_manual_level) {
                applyDimmerLevel(m_manual_level);
            }
            m_status.state = RouterState::IDLE;
            break;

        case RouterMode::BOOST:
            if (m_status.dimmer_percent != 100) {
                applyDimmerLevel(100);
            }
            m_status.state = RouterState::AT_MAXIMUM;
            break;

        case RouterMode::GRID_LIMIT:
            // Current-magnitude cap. Regulate only with a live grid-current
            // source; otherwise a missing sensor reads 0 A and would ramp the
            // load to 100% (0 < limit → "headroom"). Fail toward off instead of
            // holding blind at a breaker cap (H3 — was: hold IDLE).
            if (has_grid_current) {
                processGridLimitMode(grid_current);
            } else {
                failsafeDecay();
            }
            break;
    }

    if (m_priority_mutex) xSemaphoreGive(m_priority_mutex);
}

// ============================================================
// AUTO Mode Algorithm
// ============================================================

void RouterController::processAutoMode(float power_grid) {
    // Multi-Device Solar Router Algorithm:
    // Goal: P_grid → 0 (zero export/import)
    //
    // error > 0 when EXPORTING (power_grid < 0) → need to INCREASE load
    // error < 0 when IMPORTING (power_grid > 0) → need to DECREASE load
    //
    // Priority-based cascade:
    // - Same priority → parallel/proportional distribution
    // - Different priorities → cascade activation (0 first, then 1, 2, ...)

    // Check if within balance threshold
    if (fabs(power_grid) <= m_status.balance_threshold) {
        // Within threshold - hold current levels
        updateState(power_grid);
        return;
    }

    // Calculate error and total delta
    float error = -power_grid;  // Invert: export = positive error
    float total_delta = error / m_status.control_gain;

    // Available power to distribute (positive = export, need to increase load)
    float remaining_delta = total_delta;

    // Debug logging preparation
    static uint32_t last_log = 0;
    bool should_log = (millis() - last_log >= 5000);

    if (should_log) {
        ESP_LOGI(TAG, "AUTO: P_grid=%.1fW, error=%.1f, total_delta=%.2f%%",
                 power_grid, error, total_delta);
    }

    // Iterate through priority levels (sorted 0→255)
    for (uint8_t i = 0; i < m_active_priority_count; i++) {
        PriorityLevel& level = m_priority_levels[i];

        // Skip if no devices at this level
        if (level.device_count == 0 || level.total_power_w == 0) {
            continue;
        }

        // Handle RELAY devices differently from DIMMER devices
        if (level.device_type == DeviceType::RELAY) {
            // Relay control: binary ON/OFF decision
            processRelayPriority(level, remaining_delta, should_log);
            // Note: remaining_delta is updated inside processRelayPriority
            continue;
        }

        // Calculate current total level for this priority
        float current_total_level = 0.0f;
        for (uint8_t j = 0; j < level.device_count; j++) {
            DeviceRef& dev = level.devices[j];
            current_total_level += dev.target_level;
        }

        // Calculate average level for this priority
        float avg_level = current_total_level / level.device_count;

        // Check saturation limits (use tighter thresholds to prevent premature cascade)
        bool at_maximum = (avg_level >= 99.9f);  // Allow small tolerance
        bool at_minimum = (avg_level <= 0.5f);   // Consider minimum if avg < 0.5%

        // Determine if this priority can accept the remaining delta
        if (remaining_delta > 0) {
            // Need to INCREASE load (exporting)
            if (at_maximum) {
                // This priority is saturated, continue to next
                if (should_log) {
                    ESP_LOGI(TAG, "  Priority %d: at maximum (%.1f%%), cascade to next",
                             level.priority, avg_level);
                }
                continue;
            }

            // Calculate how much this priority can accept
            float max_increase = (100.0f - avg_level);  // Room to 100%
            float delta_to_apply = (remaining_delta < max_increase) ? remaining_delta : max_increase;

            // Distribute EQUALLY across all devices at same priority
            // Each device's 100% already represents its nominal power,
            // so equal % change = proportional power change
            float device_delta = delta_to_apply / level.device_count;

            for (uint8_t j = 0; j < level.device_count; j++) {
                DeviceRef& dev = level.devices[j];

                // Update target level
                dev.target_level += device_delta;

                // Clamp to 0-100
                if (dev.target_level > 100.0f) dev.target_level = 100.0f;
                if (dev.target_level < 0.0f) dev.target_level = 0.0f;

                // Apply to dimmer hardware. Surface a failed I2C/RF write (MAJOR-5): the
                // proportional loop retries implicitly next cycle, but silence hid real faults.
                uint8_t percent = static_cast<uint8_t>(dev.target_level + 0.5f);
                esp_err_t derr = dimmer_set_level(dev.id, percent);
                if (derr != ESP_OK) {
                    ESP_LOGW(TAG, "cascade dimmer %d set_level(%u%%) failed: %s",
                             dev.id, percent, esp_err_to_name(derr));
                }

                if (should_log) {
                    ESP_LOGI(TAG, "  Dimmer %d [P%d]: delta=%.2f%%, new=%.1f%% (%d%%)",
                             dev.id, level.priority, device_delta, dev.target_level, percent);
                }
            }

            // Deduct from remaining
            remaining_delta -= delta_to_apply;

            // If fully consumed, stop
            if (remaining_delta <= 0.01f) {
                break;
            }

        } else if (remaining_delta < 0) {
            // Need to DECREASE load (importing)
            if (at_minimum) {
                // This priority is already at minimum, continue to next
                if (should_log) {
                    ESP_LOGI(TAG, "  Priority %d: at minimum (%.1f%%), cascade to next",
                             level.priority, avg_level);
                }
                continue;
            }

            // Calculate how much this priority can decrease
            float max_decrease = -avg_level;  // Room to 0%
            float delta_to_apply = (remaining_delta > max_decrease) ? remaining_delta : max_decrease;

            // Distribute EQUALLY across all devices at same priority
            // Each device's 100% already represents its nominal power,
            // so equal % change = proportional power change
            float device_delta = delta_to_apply / level.device_count;

            for (uint8_t j = 0; j < level.device_count; j++) {
                DeviceRef& dev = level.devices[j];

                // Update target level
                dev.target_level += device_delta;

                // Clamp to 0-100
                if (dev.target_level > 100.0f) dev.target_level = 100.0f;
                if (dev.target_level < 0.0f) dev.target_level = 0.0f;

                // Apply to dimmer hardware. Surface a failed I2C/RF write (MAJOR-5): the
                // proportional loop retries implicitly next cycle, but silence hid real faults.
                uint8_t percent = static_cast<uint8_t>(dev.target_level + 0.5f);
                esp_err_t derr = dimmer_set_level(dev.id, percent);
                if (derr != ESP_OK) {
                    ESP_LOGW(TAG, "cascade dimmer %d set_level(%u%%) failed: %s",
                             dev.id, percent, esp_err_to_name(derr));
                }

                if (should_log) {
                    ESP_LOGI(TAG, "  Dimmer %d [P%d]: delta=%.2f%%, new=%.1f%% (%d%%)",
                             dev.id, level.priority, device_delta, dev.target_level, percent);
                }
            }

            // Deduct from remaining
            remaining_delta -= delta_to_apply;

            // If fully consumed, stop
            if (remaining_delta >= -0.01f) {
                break;
            }
        }
    }

    // Update legacy single-dimmer status for backward compatibility
    // Use primary dimmer (m_dimmer_id) if it exists
    dimmer_status_t dimmer_status;
    if (dimmer_get_status(m_dimmer_id, &dimmer_status) == ESP_OK) {
        m_status.dimmer_percent = dimmer_status.level_percent;
        m_target_level = (float)m_status.dimmer_percent;
        m_status.target_level = m_target_level;
    }

    // Update state
    updateState(power_grid);

    if (should_log) {
        ESP_LOGI(TAG, "AUTO: Remaining delta: %.2f%%", remaining_delta);
        last_log = millis();
    }
}

// ============================================================
// Relay Priority Control
// ============================================================

void RouterController::processRelayPriority(PriorityLevel& level, float& remaining_delta, bool should_log) {
    // Simplified relay control (v1.3.0)
    // TODO: Add intelligent power-based control with safety margin in future release
    // For now: simple ON/OFF based on remaining_delta > 0

    // Iterate through all relays at this priority
    for (uint8_t j = 0; j < level.device_count; j++) {
        DeviceRef& dev = level.devices[j];

        // Get relay status
        relay_status_t relay_status;
        if (relay_get_status(dev.id, &relay_status) != ESP_OK) {
            continue;  // Skip if can't get status
        }

        bool is_on = (relay_status.state == RELAY_STATE_ON);
        dev.target_level = is_on ? 100.0f : 0.0f;  // Sync target with actual state

        if (remaining_delta > 0) {
            // INCREASE mode: Turn ON relays if available export power
            if (!is_on) {
                // Simple logic: turn ON if any remaining delta
                relay_turn_on(dev.id, false);  // force=false (respect debounce)
                dev.target_level = 100.0f;
                remaining_delta = 0.0f;  // Consume all remaining (simplified)

                if (should_log) {
                    ESP_LOGI(TAG, "  Relay %d [P%d]: turned ON (power=%dW)",
                             dev.id, level.priority, dev.power_w);
                }
            } else {
                // Already ON - no action needed
                if (should_log) {
                    ESP_LOGI(TAG, "  Relay %d [P%d]: already ON (power=%dW)",
                             dev.id, level.priority, dev.power_w);
                }
            }

        } else if (remaining_delta < 0) {
            // DECREASE mode: Turn OFF relays if needed
            if (is_on) {
                // Turn OFF to reduce load
                relay_turn_off(dev.id, false);  // force=false (respect debounce)
                dev.target_level = 0.0f;
                remaining_delta += 100.0f;  // Release 100% back

                if (should_log) {
                    ESP_LOGI(TAG, "  Relay %d [P%d]: turned OFF (power=%dW)",
                             dev.id, level.priority, dev.power_w);
                }
            } else {
                // Already OFF - no action needed
                if (should_log) {
                    ESP_LOGI(TAG, "  Relay %d [P%d]: already OFF",
                             dev.id, level.priority);
                }
            }
        }
    }
}

// ============================================================
// ECO Mode Algorithm
// ============================================================

void RouterController::processEcoMode(float power_grid) {
    // Economic Mode Algorithm:
    // Goal: P_grid <= 0 (avoid grid import, allow export)
    //
    // Only reduce load when importing from grid
    // Do not increase load when exporting (conservative)
    // Slower response than AUTO mode for stability

    // Check if importing from grid (beyond threshold)
    if (power_grid > m_status.balance_threshold) {
        // Importing from grid - reduce load
        float error = -power_grid;  // Negative error to decrease

        // Slower response: increase gain by 1.5x
        float delta = error / (m_status.control_gain * 1.5f);
        m_target_level += delta;

        // Apply new level
        applyDimmerLevel(m_target_level);

        // Update state
        updateState(power_grid);

        // Debug logging (every 5 seconds)
        static uint32_t last_log = 0;
        if (millis() - last_log >= 5000) {
            ESP_LOGI(TAG, "ECO: P_grid=%.1fW (importing), delta=%.2f, target=%.1f%%, dimmer=%d%%",
                     power_grid, delta, m_target_level, m_status.dimmer_percent);
            last_log = millis();
        }
    } else {
        // Exporting or balanced - hold current level
        // Do not increase load aggressively
        updateState(power_grid);

        // Debug logging (every 10 seconds for idle state)
        static uint32_t last_idle_log = 0;
        if (millis() - last_idle_log >= 10000) {
            ESP_LOGI(TAG, "ECO: P_grid=%.1fW (balanced/exporting), holding dimmer=%d%%",
                     power_grid, m_status.dimmer_percent);
            last_idle_log = millis();
        }
    }
}

// ============================================================
// Failsafe — required sensor unavailable (grid lost / total silence)
// ============================================================

void RouterController::failsafeDecay() {
    // The regulating input is unavailable: grid stale/lost in AUTO/ECO, grid current
    // lost in GRID_LIMIT, or ALL sources silent (C2). We must NOT hold the last level on
    // dead data — an unknown amount of load could keep running. Fail toward off: step the
    // load down each call until fresh data returns (mirrors OFFGRID's no-solar decay).
    // A brief hiccup only nudges it down; a real loss walks it to 0 (~20 s on the 5 Hz
    // stale path; slower on the 1 Hz total-silence path — bounded, never an infinite hold).
    static uint32_t last_warn = 0;
    if (millis() - last_warn >= 5000) {
        ESP_LOGW(TAG, "FAILSAFE: required sensor data unavailable — decaying load toward off (dimmer=%d%%)",
                 m_status.dimmer_percent);
        last_warn = millis();
    }
    if (m_target_level > 0.0f) {
        applyDimmerLevel(m_target_level - 1.0f);
    }
    m_status.state = RouterState::DECREASING;
}

// ============================================================
// GRID_LIMIT Mode Algorithm (current-magnitude cap, no voltage/solar)
// ============================================================

void RouterController::processGridLimitMode(float grid_current_a) {
    // Goal: keep |I_grid| <= limit while consuming available headroom.
    //   error > 0  → headroom below the limit → can INCREASE load
    //   error < 0  → over the limit → must DECREASE load
    // Valid only with no export (no PV backfeed): grid current is always import,
    // so its magnitude equals the import — no sign needed. Guarded by the caller
    // (only invoked when a grid-current source is present).
    float error = m_grid_current_limit_a - grid_current_a;

    m_status.power_grid = 0.0f;  // no voltage → no real power; report 0 W (current-only)

    if (error < -RouterConfig::GRID_LIMIT_DEADBAND_A) {
        // Over the limit — reduce load. error<0 → negative step.
        m_target_level += error / RouterConfig::GRID_LIMIT_GAIN;
        applyDimmerLevel(m_target_level);
        m_status.state = RouterState::DECREASING;
    } else if (error > RouterConfig::GRID_LIMIT_DEADBAND_A) {
        // Headroom — increase load toward the cap (applyDimmerLevel clamps 0..100).
        m_target_level += error / RouterConfig::GRID_LIMIT_GAIN;
        applyDimmerLevel(m_target_level);
        m_status.state = RouterState::INCREASING;
    } else {
        // Within the deadband around the limit — hold.
        if (m_status.dimmer_percent >= RouterConfig::MAX_DIMMER_PERCENT) {
            m_status.state = RouterState::AT_MAXIMUM;
        } else {
            m_status.state = RouterState::IDLE;
        }
    }

    static uint32_t last_log = 0;
    if (millis() - last_log >= 5000) {
        ESP_LOGI(TAG, "GRID_LIMIT: I_grid=%.2fA, limit=%.2fA, err=%.2f, dimmer=%d%%",
                 grid_current_a, m_grid_current_limit_a, error, m_status.dimmer_percent);
        last_log = millis();
    }
}

void RouterController::setGridCurrentLimit(float amps) {
    if (amps < 0.0f) amps = 0.0f;
    if (amps > RouterConfig::MAX_GRID_CURRENT_LIMIT_A) amps = RouterConfig::MAX_GRID_CURRENT_LIMIT_A;
    m_grid_current_limit_a = amps;
    ESP_LOGI(TAG, "Grid current limit set: %.2f A", amps);
}

// ============================================================
// Dimmer Control
// ============================================================

void RouterController::applyDimmerLevel(float level) {
    // NaN/Inf guard (BLOCKER-7): a non-finite level defeats the clamps below (every
    // comparison with NaN is false), leaving m_target_level = NaN and making
    // static_cast<uint8_t>(NaN + 0.5f) undefined → a garbage percent to the hardware.
    // Treat non-finite as a hard 0 (safe-off) and warn.
    if (!isfinite(level)) {
        ESP_LOGW(TAG, "applyDimmerLevel: non-finite level — forcing 0%% (safe)");
        level = 0.0f;
    }

    // Clamp to valid range
    if (level < RouterConfig::MIN_DIMMER_PERCENT) {
        level = RouterConfig::MIN_DIMMER_PERCENT;
    }
    if (level > RouterConfig::MAX_DIMMER_PERCENT) {
        level = RouterConfig::MAX_DIMMER_PERCENT;
    }

    // Store float target
    m_target_level = level;

    // Convert to integer percent
    uint8_t percent = static_cast<uint8_t>(level + 0.5f);  // Round

    // Apply to dimmer if changed. Only commit the cached level when the hardware
    // write actually succeeds — otherwise a failed I2C/RF write would be masked as
    // applied, and the "percent != dimmer_percent" guard would suppress the retry,
    // leaving the load stuck at the wrong power with healthy-looking telemetry (D4).
    if (percent != m_status.dimmer_percent) {
        esp_err_t err = dimmer_set_level(m_dimmer_id, percent);
        if (err == ESP_OK) {
            m_status.dimmer_percent = percent;
            m_status.target_level = m_target_level;
        } else {
            ESP_LOGW(TAG, "dimmer %d set_level(%u%%) failed: %s — will retry next cycle",
                     m_dimmer_id, percent, esp_err_to_name(err));
        }
    }
}

void RouterController::updateState(float power_grid) {
    // Determine state based on current conditions
    if (m_status.dimmer_percent >= RouterConfig::MAX_DIMMER_PERCENT) {
        m_status.state = RouterState::AT_MAXIMUM;
    } else if (m_status.dimmer_percent <= RouterConfig::MIN_DIMMER_PERCENT) {
        m_status.state = RouterState::AT_MINIMUM;
    } else if (power_grid < -m_status.balance_threshold) {
        m_status.state = RouterState::INCREASING;  // Exporting, need more load
    } else if (power_grid > m_status.balance_threshold) {
        m_status.state = RouterState::DECREASING;  // Importing, need less load
    } else {
        m_status.state = RouterState::IDLE;  // Balanced
    }
}

// ============================================================
// Mode Control
// ============================================================

void RouterController::setMode(RouterMode mode) {
    if (m_status.mode == mode) {
        return;  // No change
    }

    RouterMode old_mode = m_status.mode;
    m_status.mode = mode;

    ESP_LOGI(TAG, "Mode changed: %d -> %d", static_cast<int>(old_mode), static_cast<int>(mode));

    // Handle mode transitions
    switch (mode) {
        case RouterMode::OFF:
            applyDimmerLevel(0);
            m_target_level = 0;
            // H1: OFF must shed the WHOLE load, not just the primary dimmer — otherwise
            // cascade dimmers and relays keep heating after the user selects OFF.
            dimmer_set_level_all(0);
            relay_all_off(true);
            break;

        case RouterMode::AUTO:
        case RouterMode::ECO:
        case RouterMode::OFFGRID:
        case RouterMode::GRID_LIMIT:
            // Keep current level as starting point
            // Algorithm will adjust from here
            break;

        case RouterMode::MANUAL:
            applyDimmerLevel(m_manual_level);
            break;

        case RouterMode::BOOST:
            applyDimmerLevel(100);
            break;
    }
}

void RouterController::setManualLevel(uint8_t percent) {
    if (percent > 100) {
        percent = 100;
    }
    m_manual_level = percent;

    // Apply immediately if in MANUAL mode
    if (m_status.mode == RouterMode::MANUAL) {
        applyDimmerLevel(m_manual_level);
    }

    ESP_LOGI(TAG, "Manual level set: %d%%", percent);
}

// ============================================================
// Algorithm Parameters
// ============================================================

void RouterController::setControlGain(float gain) {
    if (gain < RouterConfig::MIN_CONTROL_GAIN) {
        gain = RouterConfig::MIN_CONTROL_GAIN;
    }
    if (gain > RouterConfig::MAX_CONTROL_GAIN) {
        gain = RouterConfig::MAX_CONTROL_GAIN;
    }
    m_status.control_gain = gain;
    ESP_LOGI(TAG, "Control gain set: %.1f", gain);
}

void RouterController::setBalanceThreshold(float threshold_watts) {
    if (threshold_watts < 0) {
        threshold_watts = 0;
    }
    if (threshold_watts > 100) {   // match ConfigManager's persist clamp so runtime==stored
        threshold_watts = 100;
    }
    m_status.balance_threshold = threshold_watts;
    ESP_LOGI(TAG, "Balance threshold set: %.1f W", threshold_watts);
}

// ============================================================
// Emergency
// ============================================================

void RouterController::emergencyStop() {
    ESP_LOGW(TAG, "EMERGENCY STOP!");

    m_status.mode = RouterMode::OFF;
    m_status.state = RouterState::IDLE;
    m_target_level = 0;
    m_status.dimmer_percent = 0;

    // H1: hard-stop the ENTIRE load — every dimmer (not just the primary m_dimmer_id)
    // and force-open every relay. Previously only the primary dimmer was zeroed, so
    // cascade dimmers and relays kept driving the load through an "emergency stop".
    dimmer_emergency_stop_all();
    relay_all_off(true);
}

// ============================================================
// Mode Validation
// ============================================================

bool RouterController::validateMode(RouterMode mode, bool has_grid, bool has_solar) {
    switch (mode) {
        case RouterMode::OFF:
        case RouterMode::MANUAL:
        case RouterMode::BOOST:
            // These modes don't require any sensors
            return true;

        case RouterMode::AUTO:
        case RouterMode::ECO:
            // AUTO and ECO modes REQUIRE CURRENT_GRID sensor
            return has_grid;

        case RouterMode::GRID_LIMIT:
            // GRID_LIMIT needs a grid current source (current-only I-module is enough).
            return has_grid;

        case RouterMode::OFFGRID:
            // OFFGRID mode REQUIRES CURRENT_SOLAR sensor
            return has_solar;

        default:
            return false;
    }
}

const char* RouterController::getValidationFailureReason(RouterMode mode, bool has_grid, bool has_solar) {
    switch (mode) {
        case RouterMode::AUTO:
            if (!has_grid) {
                return "AUTO mode requires CURRENT_GRID sensor (not configured)";
            }
            return "Unknown validation failure";

        case RouterMode::ECO:
            if (!has_grid) {
                return "ECO mode requires CURRENT_GRID sensor (not configured)";
            }
            return "Unknown validation failure";

        case RouterMode::GRID_LIMIT:
            if (!has_grid) {
                return "GRID_LIMIT mode requires a grid current sensor (not configured)";
            }
            return "Unknown validation failure";

        case RouterMode::OFFGRID:
            if (!has_solar) {
                return "OFFGRID mode requires CURRENT_SOLAR sensor (not configured)";
            }
            return "Unknown validation failure";

        default:
            return "Mode validation not required";
    }
}

// ============================================================
// Multi-Device Priority Management
// ============================================================

void RouterController::rebuildPriorityMap() {
    ESP_LOGI(TAG, "Rebuilding priority map...");

    // Hold the map lock across the whole free+realloc+sort so the control loop never
    // iterates a half-freed map (D2). Callable from begin() and MQTT/web tasks.
    if (m_priority_mutex) xSemaphoreTake(m_priority_mutex, portMAX_DELAY);

    // Free existing allocations
    for (uint8_t i = 0; i < m_active_priority_count; i++) {
        if (m_priority_levels[i].devices) {
            delete[] m_priority_levels[i].devices;
            m_priority_levels[i].devices = nullptr;
        }
    }
    m_active_priority_count = 0;

    // Temporary storage: count devices per priority
    struct PriorityInfo {
        uint8_t priority;
        DeviceType type;
        uint8_t count;
        uint32_t total_power;
    };
    PriorityInfo temp_priorities[MAX_PRIORITY_LEVELS];
    uint8_t temp_count = 0;

    auto findOrCreatePriority = [&](uint8_t pri, DeviceType type) -> PriorityInfo* {
        // Find existing
        for (uint8_t i = 0; i < temp_count; i++) {
            if (temp_priorities[i].priority == pri) {
                if (temp_priorities[i].type != type) {
                    ESP_LOGE(TAG, "ERROR: Mixed device types at priority %d!", pri);
                    return nullptr;
                }
                return &temp_priorities[i];
            }
        }
        // Create new
        if (temp_count >= MAX_PRIORITY_LEVELS) {
            ESP_LOGE(TAG, "ERROR: Too many priority levels (max %d)", MAX_PRIORITY_LEVELS);
            return nullptr;
        }
        temp_priorities[temp_count].priority = pri;
        temp_priorities[temp_count].type = type;
        temp_priorities[temp_count].count = 0;
        temp_priorities[temp_count].total_power = 0;
        return &temp_priorities[temp_count++];
    };

    // First pass: count devices per priority
    bool error_occurred = false;
    for (uint8_t id = 0; id < DIMMER_MAX_COUNT; id++) {
        if (!dimmer_is_enabled(id)) continue;
        uint8_t pri = dimmer_get_priority(id);
        uint16_t pwr = dimmer_get_nominal_power(id);
        PriorityInfo* info = findOrCreatePriority(pri, DeviceType::DIMMER);
        if (info) {
            info->count++;
            info->total_power += pwr;
        } else {
            error_occurred = true;
            break;
        }
    }

    if (!error_occurred) {
        for (uint8_t id = 0; id < RELAY_MAX_COUNT; id++) {
            if (!relay_is_enabled(id)) continue;
            uint8_t pri = relay_get_priority(id);
            uint16_t pwr = relay_get_nominal_power(id);
            PriorityInfo* info = findOrCreatePriority(pri, DeviceType::RELAY);
            if (info) {
                info->count++;
                info->total_power += pwr;
            } else {
                error_occurred = true;
                break;
            }
        }
    }

    // If error occurred, abort rebuild
    if (error_occurred) {
        ESP_LOGE(TAG, "Priority map rebuild FAILED due to configuration error");
        ESP_LOGE(TAG, "Please ensure same priority level contains only ONE device type (all dimmers OR all relays)");
        m_active_priority_count = 0;
        // Release the map lock on this error path too — otherwise the control task
        // blocks forever on xSemaphoreTake → Task-WDT reset → the same bad NVS config
        // fails the rebuild again in begin() → boot-loop (C1).
        if (m_priority_mutex) xSemaphoreGive(m_priority_mutex);
        return;
    }

    // Allocate arrays and populate
    for (uint8_t i = 0; i < temp_count; i++) {
        PriorityLevel* level = &m_priority_levels[i];
        level->priority = temp_priorities[i].priority;
        level->device_type = temp_priorities[i].type;
        level->device_capacity = temp_priorities[i].count;
        level->device_count = 0;
        level->total_power_w = temp_priorities[i].total_power;
        level->devices = new DeviceRef[level->device_capacity];
    }
    m_active_priority_count = temp_count;

    // Second pass: populate device arrays
    for (uint8_t id = 0; id < DIMMER_MAX_COUNT; id++) {
        if (!dimmer_is_enabled(id)) continue;
        uint8_t pri = dimmer_get_priority(id);
        uint16_t pwr = dimmer_get_nominal_power(id);

        // Find priority level
        for (uint8_t i = 0; i < m_active_priority_count; i++) {
            if (m_priority_levels[i].priority == pri) {
                PriorityLevel* level = &m_priority_levels[i];
                level->devices[level->device_count++] = DeviceRef(DeviceType::DIMMER, id, pwr);
                ESP_LOGD(TAG, "  Dimmer %d: priority=%d, power=%dW", id, pri, pwr);
                break;
            }
        }
    }

    for (uint8_t id = 0; id < RELAY_MAX_COUNT; id++) {
        if (!relay_is_enabled(id)) continue;
        uint8_t pri = relay_get_priority(id);
        uint16_t pwr = relay_get_nominal_power(id);

        // Find priority level
        for (uint8_t i = 0; i < m_active_priority_count; i++) {
            if (m_priority_levels[i].priority == pri) {
                PriorityLevel* level = &m_priority_levels[i];
                level->devices[level->device_count++] = DeviceRef(DeviceType::RELAY, id, pwr);
                ESP_LOGD(TAG, "  Relay %d: priority=%d, power=%dW", id, pri, pwr);
                break;
            }
        }
    }

    // Sort by priority (0 = highest)
    for (uint8_t i = 0; i < m_active_priority_count - 1; i++) {
        for (uint8_t j = i + 1; j < m_active_priority_count; j++) {
            if (m_priority_levels[j].priority < m_priority_levels[i].priority) {
                PriorityLevel temp = m_priority_levels[i];
                m_priority_levels[i] = m_priority_levels[j];
                m_priority_levels[j] = temp;
            }
        }
    }

    // Summary
    ESP_LOGI(TAG, "Priority map built: %d active priority levels", m_active_priority_count);
    for (uint8_t i = 0; i < m_active_priority_count; i++) {
        const char* type_str = (m_priority_levels[i].device_type == DeviceType::DIMMER) ? "Dimmers" : "Relays";
        ESP_LOGI(TAG, "  Priority %d: %d %s, total %lu W",
                 m_priority_levels[i].priority,
                 m_priority_levels[i].device_count,
                 type_str,
                 (unsigned long)m_priority_levels[i].total_power_w);
    }

    // Auto-bind the primary (single-dimmer legacy API) output to the first enabled
    // DimmerLink dimmer, transport-agnostic — so MANUAL/BOOST/OFF actuation AND the
    // status readback (m_status.dimmer_percent) target a real output even when no
    // ESP-NOW reconcile runs (e.g. the C2-HTTP profile, where the old default stuck at
    // the legacy id 0). v2.0: dimming is only via DimmerLink; there is no GPIO primary.
    for (uint8_t id = DIMMER_I2C_START; id < DIMMER_ESPNOW_END; id++) {
        if (dimmer_is_enabled(id)) {
            if (m_dimmer_id != id) {
                ESP_LOGI(TAG, "Primary dimmer auto-bound %u -> %u", m_dimmer_id, id);
                m_dimmer_id = id;
            }
            break;
        }
    }

    // H2: when we're OFF (always true at boot — boot mode is OFF), force every enabled
    // DimmerLink output to 0. A freshly-bound, externally-powered DimmerLink may still
    // hold its pre-reboot level, and applyDimmerLevel's primary-cache guard would suppress
    // a 0-write — leaving the load heating at the old level under a reported "OFF, 0%".
    // Guarded on OFF so a live reconfig in AUTO (which also calls rebuildPriorityMap)
    // never slams the regulating load to 0.
    if (m_status.mode == RouterMode::OFF) {
        for (uint8_t id = DIMMER_I2C_START; id < DIMMER_ESPNOW_END; id++) {
            if (dimmer_is_enabled(id)) dimmer_set_level(id, 0);
        }
    }

    if (m_priority_mutex) xSemaphoreGive(m_priority_mutex);
}

const PriorityLevel* RouterController::getDevicesAtPriority(uint8_t priority) const {
    // Linear search in sorted array (small array, so OK)
    for (uint8_t i = 0; i < m_active_priority_count; i++) {
        if (m_priority_levels[i].priority == priority) {
            return &m_priority_levels[i];
        }
    }
    return nullptr;
}
