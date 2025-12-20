idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        espressif__arduino-esp32
        nvs_flash
        driver
        hal
        esp_hw_support
        log
        esp_wifi
        # Our custom components
        acrouter_hal
        utils
        comm
)
