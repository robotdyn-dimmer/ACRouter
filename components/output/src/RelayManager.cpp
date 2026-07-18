/**
 * @file RelayManager.cpp
 * @brief Manager for multiple relay outputs
 */

#include "RelayManager.h"
#include <esp_log.h>

static const char* TAG = "RelayMgr";

namespace ACRouter {

RelayManager::RelayManager() :
    m_initialized(false)
{
    // Initialize relay instances
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        m_relays[i] = new GPIORelay(i);
    }
}

RelayManager::~RelayManager() {
    allOff();

    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        delete m_relays[i];
        m_relays[i] = nullptr;
    }
}

RelayManager& RelayManager::getInstance() {
    static RelayManager instance;
    return instance;
}

bool RelayManager::begin() {
    if (m_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    // Load configuration from NVS
    loadConfig();

    // Initialize enabled relays
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        const GPIORelayConfig& cfg = m_relays[i]->getConfig();
        if (cfg.enabled && cfg.gpio_pin >= 0) {
            if (m_relays[i]->begin()) {
                ESP_LOGI(TAG, "Relay %d enabled on GPIO %d", i, cfg.gpio_pin);
            } else {
                ESP_LOGW(TAG, "Failed to initialize relay %d", i);
            }
        }
    }

    m_initialized = true;
    ESP_LOGI(TAG, "RelayManager initialized with %d relays", getEnabledCount());

    return true;
}

void RelayManager::update() {
    if (!m_initialized) return;

    // Update all relays (for debounce handling)
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        if (m_relays[i]->isInitialized()) {
            m_relays[i]->update();
        }
    }
}

GPIORelay* RelayManager::getRelay(uint8_t id) {
    if (id >= MAX_RELAYS) {
        return nullptr;
    }
    return m_relays[id];
}

const GPIORelay* RelayManager::getRelay(uint8_t id) const {
    if (id >= MAX_RELAYS) {
        return nullptr;
    }
    return m_relays[id];
}

uint8_t RelayManager::getEnabledCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        if (m_relays[i]->isEnabled()) {
            count++;
        }
    }
    return count;
}

uint8_t RelayManager::getOnCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        if (m_relays[i]->isInitialized() && m_relays[i]->isOn()) {
            count++;
        }
    }
    return count;
}

uint32_t RelayManager::getTotalOnPower() const {
    uint32_t total = 0;
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        if (m_relays[i]->isInitialized() && m_relays[i]->isOn()) {
            total += m_relays[i]->getNominalPower();
        }
    }
    return total;
}

bool RelayManager::configureRelay(uint8_t id, const GPIORelayConfig& config) {
    if (id >= MAX_RELAYS) {
        return false;
    }

    GPIORelay* relay = m_relays[id];
    const GPIORelayConfig& oldConfig = relay->getConfig();

    bool gpioChanged = (oldConfig.gpio_pin != config.gpio_pin);
    bool wasEnabled = oldConfig.enabled && relay->isInitialized();
    bool willBeEnabled = config.enabled && config.gpio_pin >= 0;

    // Apply new configuration
    relay->setConfig(config);

    // Reinitialize if needed
    if (willBeEnabled && (gpioChanged || !wasEnabled)) {
        relay->begin();
    }

    return true;
}

bool RelayManager::setRelayEnabled(uint8_t id, bool enabled) {
    if (id >= MAX_RELAYS) {
        return false;
    }

    GPIORelay* relay = m_relays[id];
    const GPIORelayConfig& cfg = relay->getConfig();

    if (enabled) {
        if (cfg.gpio_pin >= 0 && !relay->isInitialized()) {
            relay->setEnabled(true);
            relay->begin();
        }
    } else {
        relay->setEnabled(false);
    }

    return true;
}

bool RelayManager::turnOn(uint8_t id, bool force) {
    GPIORelay* relay = getRelay(id);
    if (!relay || !relay->isInitialized()) {
        return false;
    }
    return relay->turnOn(force);
}

bool RelayManager::turnOff(uint8_t id, bool force) {
    GPIORelay* relay = getRelay(id);
    if (!relay || !relay->isInitialized()) {
        return false;
    }
    return relay->turnOffRelay(force);
}

bool RelayManager::toggle(uint8_t id, bool force) {
    GPIORelay* relay = getRelay(id);
    if (!relay || !relay->isInitialized()) {
        return false;
    }
    return relay->toggle(force);
}

bool RelayManager::isOn(uint8_t id) const {
    const GPIORelay* relay = getRelay(id);
    if (!relay) {
        return false;
    }
    return relay->isOn();
}

void RelayManager::allOff() {
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        if (m_relays[i]->isInitialized()) {
            m_relays[i]->turnOffRelay(true);
        }
    }
}

void RelayManager::emergencyStop() {
    ESP_LOGW(TAG, "EMERGENCY STOP - All relays OFF");
    allOff();
}

bool RelayManager::loadConfig() {
    bool success = true;
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        if (!m_relays[i]->loadConfig()) {
            // If no config exists, set defaults for relays 0 and 1
            if (i < 2) {
                GPIORelayConfig cfg;
                cfg.id = i;
                cfg.gpio_pin = DEFAULT_RELAY_PINS[i];
                cfg.enabled = false;  // Disabled by default
                cfg.active_high = true;
                snprintf(cfg.name, sizeof(cfg.name), "Relay %d", i + 1);
                cfg.min_on_time_s = 60;
                cfg.min_off_time_s = 60;
                m_relays[i]->setConfig(cfg);
            }
            success = false;
        }
    }
    return success;
}

bool RelayManager::saveConfig() {
    bool success = true;
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        if (!m_relays[i]->saveConfig()) {
            success = false;
        }
    }
    return success;
}

RelayManager::ManagerStatus RelayManager::getStatus() const {
    ManagerStatus status;
    status.initialized = m_initialized;
    status.enabled_count = getEnabledCount();
    status.on_count = getOnCount();
    status.total_on_power_w = getTotalOnPower();

    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        status.relays[i] = m_relays[i]->getStatus();
    }

    return status;
}

} // namespace ACRouter
