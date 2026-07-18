# Dimmer Management Component
# Pure C / ESP-IDF implementation

idf_component_register(
    SRCS
        "src/dimmer_manager.c"
        "src/dimmer_i2c.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        nvs_flash
        esp_common
        driver
        dimmerlink
        # Transport-agnostic output: the ESP-NOW dimmer dispatch calls esp_now_source.
        # Required unconditionally (a CONFIG-gated REQUIRES is unreliable — CONFIG_* is
        # not resolved during dependency expansion). The dispatch/include stay guarded
        # by CONFIG_ACROUTER_ESPNOW_SOURCE, so an OFF build just never calls it.
        esp_now_source
)
