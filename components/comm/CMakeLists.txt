# Communication Component - WiFi, WebServer, NTP, OTA, MQTT
# Provides network connectivity, web interface, and MQTT client

idf_component_register(
    SRCS
        "src/WiFiManager.cpp"
        "src/WebServerManager.cpp"
        "src/NTPManager.cpp"
        "src/OTAManager.cpp"
        "src/GitHubOTAChecker.cpp"
        "src/MQTTManager.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        espressif__arduino-esp32
        espressif__esp_diagnostics
        esp_wifi
        log
        bblanchon__arduinojson
        lwip
        app_update
        mbedtls
        mqtt
        acrouter_hal
    PRIV_REQUIRES
        utils
)
