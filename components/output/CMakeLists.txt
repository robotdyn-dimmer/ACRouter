# Output Component - Relays Management
# Provides abstraction layer for relay output devices

idf_component_register(
    SRCS
        "src/GPIORelay.cpp"
        "src/RelayManager.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        espressif__arduino-esp32
        driver
        nvs_flash
    PRIV_REQUIRES
        log
)

# C++17 features
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

# Disable exceptions for smaller binary
target_compile_options(${COMPONENT_LIB} PRIVATE -fno-exceptions)
