/**
 * @file system_loop.h
 * @brief ACRouter main loop
 */

#ifndef SYSTEM_LOOP_H
#define SYSTEM_LOOP_H

/**
 * @brief Main system loop (never returns)
 *
 * Handles periodic tasks:
 * - Serial command processing
 * - Relay debounce updates
 * - WiFi/NTP/WebServer/MQTT handling
 * - Statistics logging (every 60s)
 *
 * Loop period: 500ms
 */
void system_main_loop(void);

#endif // SYSTEM_LOOP_H
