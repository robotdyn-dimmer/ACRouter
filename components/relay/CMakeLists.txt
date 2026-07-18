# Relay Component - Unified relay management (pure C)
# Supports GPIO, I2C, ESP-NOW relay interfaces

idf_component_register(
    SRCS
        "src/relay_manager.c"
        "src/relay_gpio.c"
        "src/relay_i2c.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        nvs_flash
        esp_timer
        dimmerlink
    PRIV_REQUIRES
        log
)

# C17 features
set_source_files_properties(
    src/relay_manager.c
    src/relay_gpio.c
    PROPERTIES COMPILE_FLAGS "-std=gnu17"
)
