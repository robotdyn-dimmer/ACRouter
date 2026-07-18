# Communication Component - WiFi, WebServer, NTP, OTA, MQTT
# Provides network connectivity, web interface, and MQTT client

# esp_diagnostics is not published for esp32c2/esp32p4 (see dependencies.lock:
# "if: target not in [esp32c2, esp32p4]"). comm only uses it for the optional
# DIAG_USE_EXTERNAL_LOG_WRAP log feature — no direct API calls — so require it
# only on targets where it exists.
set(COMM_REQUIRES
    espressif__arduino-esp32
    esp_wifi
    log
    bblanchon__arduinojson
    lwip
    esp_http_server
    app_update
    mbedtls
    mqtt
    acrouter_hal
    event_bus
)
if(NOT IDF_TARGET STREQUAL "esp32c2" AND NOT IDF_TARGET STREQUAL "esp32p4")
    list(APPEND COMM_REQUIRES espressif__esp_diagnostics)
endif()

idf_component_register(
    SRCS
        "src/WiFiManager.cpp"
        "src/HttpdServer.cpp"
        "src/WebServerManager.cpp"
        "src/NTPManager.cpp"
        "src/OTAManager.cpp"
        "src/GitHubOTAChecker.cpp"
        "src/MQTTManager.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        ${COMM_REQUIRES}
    PRIV_REQUIRES
        utils
        output
        dimmer
        relay
        i2c_bus
        device_registry
        dimmerlink
        sensor_hub
        rbamp_source
        esp_now_source
)
