# ACRouter HAL Component - Hardware Abstraction Layer
# Provides low-level hardware drivers for AC Power Router
# Component name is 'acrouter_hal' (from directory name)

idf_component_register(
    SRCS
        "src/RouterController.cpp"
        # Future HAL modules:
        # "src/IndicatorLED.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        espressif__arduino-esp32
        driver
        hal
        esp_adc
        esp_hw_support
        esp_event
        sensors    # For SensorTypes.h
        dimmer     # Dimmer manager (pure C)
        relay      # Relay manager (pure C)
        event_bus  # ACRouter event system
    PRIV_REQUIRES
        utils  # For DataTypes.h and common utilities
)

# Add compile options for C++ code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++17
    -fno-exceptions
)
