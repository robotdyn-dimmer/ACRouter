/**
 * @file system_init.h
 * @brief ACRouter system initialization orchestrator
 *
 * Breaks the monolithic app_main() into sequenced initialization phases.
 * Each phase initializes a logical group of components with proper
 * dependency ordering.
 */

#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include "esp_err.h"

/**
 * @brief Run all initialization phases in order
 *
 * Phases:
 *   0: Storage (NVS, ConfigManager, HardwareConfigManager)
 *   1: Buses (WiFi, I2C - future)
 *   2: Outputs (Dimmer GPIO/Manager, Relay Manager)
 *   3: Sensors (rbAmp I2C source, Sensor Hub)
 *   4: Control (RouterController, event subscriptions)
 *   5: Network (NTP, WebServer, MQTT, OTA)
 *
 * @return ESP_OK if all critical phases succeeded
 */
esp_err_t system_init(void);

/**
 * @brief Check if system is in safe mode
 */
bool system_is_safe_mode(void);

#endif // SYSTEM_INIT_H
