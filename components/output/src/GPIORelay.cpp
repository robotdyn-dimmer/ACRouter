/**
 * @file GPIORelay.cpp
 * @brief GPIO-based relay implementation
 */

#include "GPIORelay.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <cstring>

namespace ACRouter {

// NVS namespace for relay configuration
static const char* NVS_NAMESPACE = "relay_cfg";

GPIORelay::GPIORelay(uint8_t id) :
    m_is_on(false),
    m_initialized(false),
    m_last_switch_ms(0),
    m_pending_on(false),
    m_pending_off(false)
{
    m_config.id = id;
    snprintf(m_config.name, sizeof(m_config.name), "Relay %d", id + 1);
}

GPIORelay::~GPIORelay() {
    if (m_initialized) {
        turnOffRelay(true);
    }
}

bool GPIORelay::begin() {
    if (!m_config.enabled || m_config.gpio_pin < 0) {
        m_initialized = false;
        return false;
    }

    // Configure GPIO as output
    pinMode(m_config.gpio_pin, OUTPUT);

    // Start in OFF state
    m_is_on = false;
    applyGpioState();

    m_initialized = true;
    m_last_switch_ms = millis();

    return true;
}

void GPIORelay::setName(const char* name) {
    if (name) {
        strncpy(m_config.name, name, sizeof(m_config.name) - 1);
        m_config.name[sizeof(m_config.name) - 1] = '\0';
    }
}

void GPIORelay::setEnabled(bool enabled) {
    if (!enabled && m_initialized) {
        turnOffRelay(true);
    }
    m_config.enabled = enabled;
}

OutputState GPIORelay::getState() const {
    if (!m_config.enabled) {
        return OutputState::OFF;
    }
    if (!m_initialized) {
        return OutputState::ERROR;
    }
    return m_is_on ? OutputState::ON : OutputState::OFF;
}

void GPIORelay::setMode(OutputMode mode) {
    m_config.mode = mode;

    // Apply mode-specific behavior
    switch (mode) {
        case OutputMode::MANUAL_OFF:
            turnOffRelay(true);
            break;
        case OutputMode::MANUAL_ON:
            turnOn(true);
            break;
        default:
            // AUTO and SCHEDULE are controlled externally
            break;
    }
}

bool GPIORelay::turnOn(bool force) {
    if (!m_initialized) {
        return false;
    }

    // Check mode restrictions
    if (m_config.mode == OutputMode::MANUAL_OFF && !force) {
        return false;
    }

    // Check debounce
    if (!force && !canSwitch()) {
        m_pending_on = true;
        m_pending_off = false;
        return true; // Will be applied later
    }

    if (!m_is_on) {
        m_is_on = true;
        applyGpioState();
        m_last_switch_ms = millis();
    }

    m_pending_on = false;
    m_pending_off = false;

    return true;
}

bool GPIORelay::turnOffRelay(bool force) {
    if (!m_initialized) {
        return false;
    }

    // Check mode restrictions
    if (m_config.mode == OutputMode::MANUAL_ON && !force) {
        return false;
    }

    // Check debounce
    if (!force && !canSwitch()) {
        m_pending_off = true;
        m_pending_on = false;
        return true; // Will be applied later
    }

    if (m_is_on) {
        m_is_on = false;
        applyGpioState();
        m_last_switch_ms = millis();
    }

    m_pending_on = false;
    m_pending_off = false;

    return true;
}

bool GPIORelay::toggle(bool force) {
    return m_is_on ? turnOffRelay(force) : turnOn(force);
}

bool GPIORelay::isDebounceActive() const {
    return !canSwitch();
}

uint16_t GPIORelay::getDebounceRemaining() const {
    if (!m_initialized) return 0;

    uint32_t elapsed = (millis() - m_last_switch_ms) / 1000;
    uint16_t min_time = m_is_on ? m_config.min_on_time_s : m_config.min_off_time_s;

    if (elapsed >= min_time) {
        return 0;
    }
    return min_time - elapsed;
}

uint32_t GPIORelay::getOnDuration() const {
    if (!m_is_on || !m_initialized) {
        return 0;
    }
    return (millis() - m_last_switch_ms) / 1000;
}

void GPIORelay::setActiveHigh(bool active_high) {
    m_config.active_high = active_high;
    if (m_initialized) {
        applyGpioState();
    }
}

void GPIORelay::setGpioPin(int8_t pin) {
    // Can only change pin when not initialized
    if (!m_initialized) {
        m_config.gpio_pin = pin;
    }
}

RelayStatus GPIORelay::getStatus() const {
    RelayStatus status;
    status.id = m_config.id;
    status.enabled = m_config.enabled;
    status.state = getState();
    status.mode = m_config.mode;
    status.is_on = m_is_on;
    status.nominal_power_w = m_config.nominal_power_w;
    status.current_sensor_id = m_config.current_sensor_id;
    status.min_on_time_s = m_config.min_on_time_s;
    status.min_off_time_s = m_config.min_off_time_s;
    status.last_switch_ms = m_last_switch_ms;
    status.on_duration_ms = m_is_on ? (millis() - m_last_switch_ms) : 0;
    status.debounce_active = isDebounceActive();
    strncpy(status.name, m_config.name, sizeof(status.name));
    return status;
}

void GPIORelay::update() {
    if (!m_initialized) return;

    // Process pending operations when debounce expires
    if (canSwitch()) {
        if (m_pending_on) {
            turnOn(false);
        } else if (m_pending_off) {
            turnOffRelay(false);
        }
    }
}

void GPIORelay::setConfig(const GPIORelayConfig& config) {
    bool wasEnabled = m_config.enabled;
    m_config = config;

    // If disabling, turn off
    if (wasEnabled && !config.enabled && m_initialized) {
        turnOffRelay(true);
    }
}

void GPIORelay::applyGpioState() {
    if (m_config.gpio_pin < 0) return;

    // active_high: ON = HIGH, OFF = LOW
    // active_low: ON = LOW, OFF = HIGH
    bool gpio_level = m_is_on ? m_config.active_high : !m_config.active_high;
    digitalWrite(m_config.gpio_pin, gpio_level ? HIGH : LOW);
}

bool GPIORelay::canSwitch() const {
    uint32_t elapsed = (millis() - m_last_switch_ms) / 1000;
    uint16_t min_time = m_is_on ? m_config.min_on_time_s : m_config.min_off_time_s;
    return elapsed >= min_time;
}

String GPIORelay::getNvsPrefix() const {
    char prefix[8];
    snprintf(prefix, sizeof(prefix), "rel%d_", m_config.id);
    return String(prefix);
}

bool GPIORelay::saveConfig() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }

    String prefix = getNvsPrefix();
    bool success = true;

    // Save configuration
    success &= (nvs_set_i8(handle, (prefix + "gpio").c_str(), m_config.gpio_pin) == ESP_OK);
    success &= (nvs_set_u8(handle, (prefix + "en").c_str(), m_config.enabled ? 1 : 0) == ESP_OK);
    success &= (nvs_set_u8(handle, (prefix + "ah").c_str(), m_config.active_high ? 1 : 0) == ESP_OK);
    success &= (nvs_set_str(handle, (prefix + "name").c_str(), m_config.name) == ESP_OK);
    success &= (nvs_set_u16(handle, (prefix + "pwr").c_str(), m_config.nominal_power_w) == ESP_OK);
    success &= (nvs_set_i8(handle, (prefix + "sens").c_str(), m_config.current_sensor_id) == ESP_OK);
    success &= (nvs_set_u16(handle, (prefix + "mon").c_str(), m_config.min_on_time_s) == ESP_OK);
    success &= (nvs_set_u16(handle, (prefix + "moff").c_str(), m_config.min_off_time_s) == ESP_OK);
    success &= (nvs_set_u8(handle, (prefix + "mode").c_str(), static_cast<uint8_t>(m_config.mode)) == ESP_OK);

    if (success) {
        nvs_commit(handle);
    }

    nvs_close(handle);
    return success;
}

bool GPIORelay::loadConfig() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    String prefix = getNvsPrefix();
    int8_t gpio = -1;
    uint8_t enabled = 0;
    uint8_t active_high = 1;
    char name[16] = {0};
    size_t name_len = sizeof(name);
    uint16_t power = 0;
    int8_t sensor = -1;
    uint16_t min_on = 60;
    uint16_t min_off = 60;
    uint8_t mode = 0;

    // Load configuration (ignore errors for missing keys - use defaults)
    nvs_get_i8(handle, (prefix + "gpio").c_str(), &gpio);
    nvs_get_u8(handle, (prefix + "en").c_str(), &enabled);
    nvs_get_u8(handle, (prefix + "ah").c_str(), &active_high);
    nvs_get_str(handle, (prefix + "name").c_str(), name, &name_len);
    nvs_get_u16(handle, (prefix + "pwr").c_str(), &power);
    nvs_get_i8(handle, (prefix + "sens").c_str(), &sensor);
    nvs_get_u16(handle, (prefix + "mon").c_str(), &min_on);
    nvs_get_u16(handle, (prefix + "moff").c_str(), &min_off);
    nvs_get_u8(handle, (prefix + "mode").c_str(), &mode);

    nvs_close(handle);

    // Apply loaded values
    m_config.gpio_pin = gpio;
    m_config.enabled = (enabled != 0);
    m_config.active_high = (active_high != 0);
    if (name[0] != '\0') {
        strncpy(m_config.name, name, sizeof(m_config.name));
    }
    m_config.nominal_power_w = power;
    m_config.current_sensor_id = sensor;
    m_config.min_on_time_s = min_on;
    m_config.min_off_time_s = min_off;
    m_config.mode = static_cast<OutputMode>(mode);

    return true;
}

} // namespace ACRouter
