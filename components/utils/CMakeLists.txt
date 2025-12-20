# Utils Component - Utility functions and common types

idf_component_register(
    SRCS
        "src/ConfigManager.cpp"
        "src/HardwareConfigManager.cpp"
        "src/SerialCommand.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        espressif__arduino-esp32
        nvs_flash
    PRIV_REQUIRES
        acrouter_hal
        comm
)
