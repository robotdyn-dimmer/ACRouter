/**
 * @file main.cpp
 * @brief ACRouter - AC Power Router Controller
 *
 * Entry point. All initialization logic is in system_init.cpp,
 * main loop is in system_loop.cpp.
 */

#include "Arduino.h"
#include "esp_log.h"
#include "system_init.h"
#include "system_loop.h"

extern "C" void app_main()
{
    initArduino();

    esp_err_t err = system_init();
    if (err != ESP_OK) {
        ESP_LOGE("MAIN", "System initialization failed: %s", esp_err_to_name(err));
        ESP_LOGE("MAIN", "System halted.");
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    system_main_loop();  // never returns
}
