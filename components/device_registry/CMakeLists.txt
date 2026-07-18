# Device Registry (L2) — device-entity identify/registry layer
idf_component_register(
    SRCS
        "src/device_registry.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        i2c_bus
        rbamp_source
        dimmerlink
        dimmer
        nvs_flash
)
