/**
 * @file system_loop.cpp
 * @brief ACRouter main loop
 */

#include "system_loop.h"
#include "system_init.h"

#include "Arduino.h"
#include "esp_log.h"
#include "SerialCommand.h"
#include "WiFiManager.h"
#include "NTPManager.h"
#include "WebServerManager.h"
#include "MQTTManager.h"

extern "C" {
#include "relay_manager.h"
#include "dimmer_manager.h"
}
#include "RouterController.h"
#include "sdkconfig.h"
#if CONFIG_ACROUTER_ESPNOW_SOURCE
#include "esp_now_source.h"
#include "espnow_proto.h"

// Bind any newly-discovered ESP-NOW DIMMER output node to a dimmer slot so the
// RouterController drives it — no manual espnow-bind, and it re-binds after a reboot
// (repeatable full-stack E2E). dimmer_bind_espnow is idempotent (reuses a slot per MAC).
static void espnow_output_reconcile(void) {
    esp_now_source_output_node_info_t nodes[ESP_NOW_SOURCE_OUT_NODES_MAX];
    size_t n = 0;
    esp_now_source_get_output_nodes(nodes, ESP_NOW_SOURCE_OUT_NODES_MAX, &n);
    for (size_t i = 0; i < n; i++) {
        if (nodes[i].family == RBN_FAMILY_DIMMER && nodes[i].online) {
            int bound_id = dimmer_bind_espnow(nodes[i].mac);
            // Retarget the RouterController's primary dimmer to the auto-bound slot so
            // every mode (BOOST/MANUAL/AUTO/…) actuates THIS output node, not the
            // legacy begin(0) id. Idempotent — only acts on the first bind / a change.
            if (bound_id >= 0) {
                RouterController::getInstance().setPrimaryDimmer((uint8_t)bound_id);
            }
        }
    }
}
#endif

static const char* TAG = "SysLoop";

void system_main_loop(void) {
    SerialCommand& serialCmd = SerialCommand::getInstance();
    WiFiManager& wifiMgr = WiFiManager::getInstance();
    NTPManager& ntpMgr = NTPManager::getInstance();
#if CONFIG_ACROUTER_HTTP_SERVER
    WebServerManager& webServer = WebServerManager::getInstance();
#endif
#if CONFIG_ACROUTER_MQTT_CLIENT
    MQTTManager& mqttMgr = MQTTManager::getInstance();
#endif

    uint32_t last_stats = 0;
    uint32_t last_espnow_reconcile = 0;

    while (1) {
        serialCmd.process();
        relay_update_all();

        wifiMgr.handle();
        ntpMgr.handle();
#if CONFIG_ACROUTER_HTTP_SERVER
        webServer.handle();
#endif
#if CONFIG_ACROUTER_MQTT_CLIENT
        mqttMgr.loop();
#endif

        uint32_t now = millis();

#if CONFIG_ACROUTER_ESPNOW_SOURCE
        // Auto-bind discovered ESP-NOW dimmer nodes (every 3s; cheap + idempotent).
        if (now - last_espnow_reconcile >= 3000) {
            last_espnow_reconcile = now;
            espnow_output_reconcile();
        }
#endif

        // Statistics every 60 seconds
        if (now - last_stats >= 60000) {
            ESP_LOGI(TAG, "Stats: uptime=%lus", (unsigned long)(now / 1000));
            last_stats = now;
        }

        // 10ms, not 500ms: the (single-connection) HTTP server is serviced by
        // webServer.handle() above, so a 500ms loop capped it at ~2 req/s — a burst of
        // requests (E2E) overran the accept backlog → ECONNRESET / apparent hang. The
        // periodic work above is millis()-gated, so a faster loop doesn't change it.
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
