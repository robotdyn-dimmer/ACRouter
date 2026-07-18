/**
 * @file RelayManager.h
 * @brief Manager for multiple relay outputs
 *
 * Manages up to 4 relay channels.
 * Provides unified interface for all relay types (GPIO, ESP-NOW, BLE future).
 */

#ifndef RELAY_MANAGER_H
#define RELAY_MANAGER_H

#include "IRelayInterface.h"
#include "GPIORelay.h"
#include <Arduino.h>
#include <array>

namespace ACRouter {

// Maximum number of relays
constexpr uint8_t MAX_RELAYS = 4;

// Default GPIO pins for relays
constexpr int8_t DEFAULT_RELAY_PINS[MAX_RELAYS] = {15, 2, -1, -1};

/**
 * @brief Manager for all relay outputs
 *
 * Singleton class that:
 * - Creates and manages up to 4 GPIORelay instances
 * - Provides unified access to all relays
 * - Handles debounce timing updates
 */
class RelayManager {
public:
    /**
     * @brief Get singleton instance
     */
    static RelayManager& getInstance();

    /**
     * @brief Initialize relay system
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Check if manager is initialized
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Update all relays (call periodically for debounce handling)
     */
    void update();

    /**
     * @brief Get relay by ID
     * @param id Relay ID (0-3)
     * @return Pointer to relay, or nullptr if invalid
     */
    GPIORelay* getRelay(uint8_t id);

    /**
     * @brief Get relay by ID (const version)
     */
    const GPIORelay* getRelay(uint8_t id) const;

    /**
     * @brief Get number of enabled relays
     */
    uint8_t getEnabledCount() const;

    /**
     * @brief Get number of ON relays
     */
    uint8_t getOnCount() const;

    /**
     * @brief Get total power of ON relays
     */
    uint32_t getTotalOnPower() const;

    /**
     * @brief Configure a relay
     * @param id Relay ID
     * @param config Relay configuration
     * @return true if successful
     */
    bool configureRelay(uint8_t id, const GPIORelayConfig& config);

    /**
     * @brief Enable/disable a relay
     */
    bool setRelayEnabled(uint8_t id, bool enabled);

    /**
     * @brief Turn relay ON
     * @param id Relay ID
     * @param force Ignore debounce
     */
    bool turnOn(uint8_t id, bool force = false);

    /**
     * @brief Turn relay OFF
     * @param id Relay ID
     * @param force Ignore debounce
     */
    bool turnOff(uint8_t id, bool force = false);

    /**
     * @brief Toggle relay
     * @param id Relay ID
     * @param force Ignore debounce
     */
    bool toggle(uint8_t id, bool force = false);

    /**
     * @brief Check if relay is ON
     */
    bool isOn(uint8_t id) const;

    /**
     * @brief Turn off all relays immediately
     */
    void allOff();

    /**
     * @brief Emergency stop - turn off all relays
     */
    void emergencyStop();

    /**
     * @brief Load all relay configurations from NVS
     */
    bool loadConfig();

    /**
     * @brief Save all relay configurations to NVS
     */
    bool saveConfig();

    /**
     * @brief Get summary status of all relays
     */
    struct ManagerStatus {
        bool initialized;
        uint8_t enabled_count;
        uint8_t on_count;
        uint32_t total_on_power_w;
        RelayStatus relays[MAX_RELAYS];
    };
    ManagerStatus getStatus() const;

private:
    RelayManager();
    ~RelayManager();

    // Prevent copying
    RelayManager(const RelayManager&) = delete;
    RelayManager& operator=(const RelayManager&) = delete;

    // Relay instances
    std::array<GPIORelay*, MAX_RELAYS> m_relays;

    // State
    bool m_initialized;
};

} // namespace ACRouter

#endif // RELAY_MANAGER_H
