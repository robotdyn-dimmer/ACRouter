/**
 * @file RouterController.cpp
 * @brief Solar Router Controller Implementation
 */

#include "RouterController.h"
#include "esp_log.h"
#include <cmath>

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
    : m_dimmer(nullptr)
    , m_channel(DimmerChannel::CHANNEL_1)
    , m_initialized(false)
    , m_manual_level(0)
    , m_target_level(0.0f)
{
    // Status initialized by default constructor
}

// ============================================================
// Initialization
// ============================================================

bool RouterController::begin(DimmerHAL* dimmer, DimmerChannel channel) {
    if (!dimmer) {
        ESP_LOGE(TAG, "DimmerHAL pointer is null");
        return false;
    }

    if (!dimmer->isInitialized()) {
        ESP_LOGE(TAG, "DimmerHAL is not initialized");
        return false;
    }

    m_dimmer = dimmer;
    m_channel = channel;
    m_initialized = true;

    // Start in OFF mode
    m_status.mode = RouterMode::OFF;
    m_status.state = RouterState::IDLE;
    m_status.dimmer_percent = 0;
    m_target_level = 0.0f;
    m_status.valid = true;

    // Ensure dimmer is off
    m_dimmer->setPower(m_channel, 0);

    ESP_LOGI(TAG, "RouterController initialized, channel=%d", static_cast<int>(channel));
    return true;
}

// ============================================================
// Main Update Function
// ============================================================

void RouterController::update(const PowerMeterADC::Measurements& measurements) {
    if (!m_initialized) {
        return;
    }

    m_status.last_update_ms = millis();

    // Get power values from all channels
    // Positive = importing/consuming, Negative = exporting/supplying
    float power_grid = measurements.power_active[PowerMeterADC::CURRENT_GRID];
    float power_solar = measurements.power_active[PowerMeterADC::CURRENT_SOLAR];
    float power_load = measurements.power_active[PowerMeterADC::CURRENT_LOAD];

    // Store all power values in status for web interface
    m_status.power_grid = power_grid;
    m_status.power_solar = power_solar;
    m_status.power_load = power_load;

    // Process based on current mode
    switch (m_status.mode) {
        case RouterMode::OFF:
            // Dimmer always 0%
            if (m_status.dimmer_percent != 0) {
                applyDimmerLevel(0);
            }
            m_status.state = RouterState::IDLE;
            break;

        case RouterMode::AUTO:
            processAutoMode(power_grid);
            break;

        case RouterMode::ECO:
            processEcoMode(power_grid);
            break;

        case RouterMode::OFFGRID:
            processOffgridMode(measurements);
            break;

        case RouterMode::MANUAL:
            // Fixed level set by user
            if (m_status.dimmer_percent != m_manual_level) {
                applyDimmerLevel(m_manual_level);
            }
            m_status.state = RouterState::IDLE;
            break;

        case RouterMode::BOOST:
            // Always 100%
            if (m_status.dimmer_percent != 100) {
                applyDimmerLevel(100);
            }
            m_status.state = RouterState::AT_MAXIMUM;
            break;
    }
}

// ============================================================
// AUTO Mode Algorithm
// ============================================================

void RouterController::processAutoMode(float power_grid) {
    // Solar Router Algorithm:
    // Goal: P_grid → 0
    //
    // error > 0 when EXPORTING (power_grid < 0) → need to INCREASE load
    // error < 0 when IMPORTING (power_grid > 0) → need to DECREASE load
    //
    // Proportional control: target_level += error / control_gain

    float error = -power_grid;  // Invert: export = positive error

    // Check if within balance threshold
    if (fabs(power_grid) <= m_status.balance_threshold) {
        // Within threshold - hold current level
        updateState(power_grid);
        return;
    }

    // Proportional control
    float delta = error / m_status.control_gain;
    m_target_level += delta;

    // Apply new level
    applyDimmerLevel(m_target_level);

    // Update state
    updateState(power_grid);

    // Debug logging (every 5 seconds)
    static uint32_t last_log = 0;
    if (millis() - last_log >= 5000) {
        ESP_LOGI(TAG, "AUTO: P_grid=%.1fW, error=%.1f, delta=%.2f, target=%.1f%%, dimmer=%d%%",
                 power_grid, error, delta, m_target_level, m_status.dimmer_percent);
        last_log = millis();
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
// OFFGRID Mode Algorithm
// ============================================================

void RouterController::processOffgridMode(const PowerMeterADC::Measurements& measurements) {
    // Offgrid Mode Algorithm:
    // Goal: Use available solar power (P_solar - P_house)
    //
    // Does NOT use grid sensor (may not be installed)
    // Conservative: uses 80% of available power as safety margin

    // Get solar power (from CURRENT_SOLAR channel)
    float power_solar = measurements.power_active[PowerMeterADC::CURRENT_SOLAR];

    // Get load power (from CURRENT_LOAD channel, if available)
    float power_load = measurements.power_active[PowerMeterADC::CURRENT_LOAD];

    // Estimate house consumption (if we have voltage measurement)
    // P_house = P_solar - P_load (assuming load is separate from house)
    // For now, simple algorithm based on solar power

    if (power_solar > m_status.balance_threshold) {
        // Solar generation available
        // Calculate target power (80% of available)
        // Assuming load is the router-controlled heater
        // Available power ≈ P_solar (simplified, user should configure sensors properly)

        float available_power = power_solar * 0.8f;  // 80% safety margin

        // TODO: Convert power to dimmer percentage
        // This requires knowing the load power rating (e.g., 2000W heater)
        // For now, use proportional increase

        if (available_power > power_load + m_status.balance_threshold) {
            // Can increase load
            float delta = (available_power - power_load) / (m_status.control_gain * 2.0f);
            m_target_level += delta;
        } else if (available_power < power_load - m_status.balance_threshold) {
            // Need to reduce load
            m_target_level -= 5.0f;  // Gradual decrease
        }
        // else: balanced, hold level

        // Apply new level
        applyDimmerLevel(m_target_level);

        // Debug logging (every 5 seconds)
        static uint32_t last_log = 0;
        if (millis() - last_log >= 5000) {
            ESP_LOGI(TAG, "OFFGRID: P_solar=%.1fW, P_load=%.1fW, target=%.1f%%, dimmer=%d%%",
                     power_solar, power_load, m_target_level, m_status.dimmer_percent);
            last_log = millis();
        }
    } else {
        // No solar generation - reduce load to minimum
        if (m_target_level > 0) {
            m_target_level -= 10.0f;  // Faster decrease when no solar
            applyDimmerLevel(m_target_level);
        }

        // Debug logging (every 10 seconds)
        static uint32_t last_no_solar_log = 0;
        if (millis() - last_no_solar_log >= 10000) {
            ESP_LOGI(TAG, "OFFGRID: No solar (%.1fW), reducing dimmer=%d%%",
                     power_solar, m_status.dimmer_percent);
            last_no_solar_log = millis();
        }
    }

    // Update state based on current level
    if (m_status.dimmer_percent >= RouterConfig::MAX_DIMMER_PERCENT) {
        m_status.state = RouterState::AT_MAXIMUM;
    } else if (m_status.dimmer_percent <= RouterConfig::MIN_DIMMER_PERCENT) {
        m_status.state = RouterState::AT_MINIMUM;
    } else if (power_solar > m_status.balance_threshold) {
        m_status.state = RouterState::INCREASING;
    } else {
        m_status.state = RouterState::DECREASING;
    }
}

// ============================================================
// Dimmer Control
// ============================================================

void RouterController::applyDimmerLevel(float level) {
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

    // Apply to dimmer if changed
    if (percent != m_status.dimmer_percent) {
        m_dimmer->setPower(m_channel, percent);
        m_status.dimmer_percent = percent;
        m_status.target_level = m_target_level;
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
            break;

        case RouterMode::AUTO:
        case RouterMode::ECO:
        case RouterMode::OFFGRID:
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

    if (m_dimmer) {
        m_dimmer->setPower(m_channel, 0);
    }
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

        case RouterMode::OFFGRID:
            if (!has_solar) {
                return "OFFGRID mode requires CURRENT_SOLAR sensor (not configured)";
            }
            return "Unknown validation failure";

        default:
            return "Mode validation not required";
    }
}
