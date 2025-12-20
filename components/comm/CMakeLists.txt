# Communication Component - WiFi, WebServer, NTP, OTA
# Provides network connectivity and web interface

idf_component_register(
    SRCS
        "src/WiFiManager.cpp"
        "src/WebServerManager.cpp"
        "src/NTPManager.cpp"
        "src/OTAManager.cpp"
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
        acrouter_hal
    PRIV_REQUIRES
        utils
)
