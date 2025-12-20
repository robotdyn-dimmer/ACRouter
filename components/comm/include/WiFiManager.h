/**
 * @file WiFiManager.h
 * @brief WiFi connection manager with AP+STA concurrent mode
 *
 * Manages WiFi connectivity supporting:
 * - Station (STA) mode: connects to configured network
 * - Access Point (AP) mode: creates hotspot for configuration
 * - AP+STA mode: both simultaneously (ESP32 feature)
 *
 * Auto-starts AP if STA connection fails or no credentials configured.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>

/**
 * @brief WiFi connection state
 */
enum class WiFiState : uint8_t {
    IDLE = 0,           ///< Not initialized
    AP_ONLY,            ///< AP mode only (no STA credentials)
    STA_CONNECTING,     ///< Connecting to STA network
    STA_CONNECTED,      ///< Connected to STA network
    AP_STA,             ///< Both AP and STA active
    STA_FAILED          ///< STA connection failed, AP active
};

/**
 * @brief WiFi configuration
 */
struct WiFiConfig {
    char sta_ssid[33];          ///< STA network SSID (max 32 chars)
    char sta_password[65];      ///< STA network password (max 64 chars)
    char ap_ssid[33];           ///< AP SSID (auto-generated if empty)
    char ap_password[65];       ///< AP password (min 8 chars, empty = open)
    bool ap_always_on;          ///< Keep AP active even when STA connected
    uint32_t sta_timeout_ms;    ///< STA connection timeout

    WiFiConfig() :
        ap_always_on(true),
        sta_timeout_ms(15000)
    {
        sta_ssid[0] = '\0';
        sta_password[0] = '\0';
        ap_ssid[0] = '\0';
        ap_password[0] = '\0';
    }
};

/**
 * @brief WiFi status information
 */
struct WiFiStatus {
    WiFiState state;
    bool sta_connected;
    bool ap_active;
    IPAddress sta_ip;
    IPAddress ap_ip;
    String sta_ssid;
    String ap_ssid;
    int8_t rssi;
    uint8_t sta_clients;        ///< Number of clients connected to AP

    WiFiStatus() :
        state(WiFiState::IDLE),
        sta_connected(false),
        ap_active(false),
        rssi(0),
        sta_clients(0)
    {}
};

/**
 * @brief WiFi manager with AP+STA support
 *
 * Singleton class for managing WiFi connections.
 */
class WiFiManager {
public:
    /**
     * @brief Get singleton instance
     */
    static WiFiManager& getInstance();

    // Prevent copying
    WiFiManager(const WiFiManager&) = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;

    /**
     * @brief Initialize WiFi with configuration
     * @param config WiFi configuration
     * @return true if initialization successful
     */
    bool begin(const WiFiConfig& config);

    /**
     * @brief Initialize with default AP mode
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Process WiFi events (call from loop)
     */
    void handle();

    /**
     * @brief Stop all WiFi connections
     */
    void stop();

    // ============================================================
    // STA Control
    // ============================================================

    /**
     * @brief Set STA credentials and attempt connection
     * @param ssid Network SSID
     * @param password Network password
     * @return true if connection initiated
     */
    bool connectSTA(const char* ssid, const char* password);

    /**
     * @brief Disconnect from STA network
     */
    void disconnectSTA();

    /**
     * @brief Check if STA is connected
     */
    bool isSTAConnected() const { return m_status.sta_connected; }

    // ============================================================
    // AP Control
    // ============================================================

    /**
     * @brief Start Access Point
     * @param ssid AP SSID (nullptr = auto-generate)
     * @param password AP password (nullptr or empty = open)
     * @return true if AP started
     */
    bool startAP(const char* ssid = nullptr, const char* password = nullptr);

    /**
     * @brief Stop Access Point
     */
    void stopAP();

    /**
     * @brief Check if AP is active
     */
    bool isAPActive() const { return m_status.ap_active; }

    // ============================================================
    // Status
    // ============================================================

    /**
     * @brief Get current WiFi state
     */
    WiFiState getState() const { return m_status.state; }

    /**
     * @brief Get full status information
     */
    const WiFiStatus& getStatus() const { return m_status; }

    /**
     * @brief Get STA IP address
     */
    IPAddress getSTAIP() const { return m_status.sta_ip; }

    /**
     * @brief Get AP IP address
     */
    IPAddress getAPIP() const { return m_status.ap_ip; }

    /**
     * @brief Get RSSI (signal strength)
     */
    int8_t getRSSI() const { return m_status.rssi; }

    /**
     * @brief Get MAC address string
     */
    String getMACAddress() const;

    /**
     * @brief Get device hostname
     */
    const String& getHostname() const { return m_hostname; }

    // ============================================================
    // Configuration
    // ============================================================

    /**
     * @brief Set device hostname
     */
    void setHostname(const char* hostname);

    /**
     * @brief Set AP always-on mode
     */
    void setAPAlwaysOn(bool enabled) { m_config.ap_always_on = enabled; }

    /**
     * @brief Get current configuration
     */
    const WiFiConfig& getConfig() const { return m_config; }

    // ============================================================
    // NVS Persistence
    // ============================================================

    /**
     * @brief Save STA credentials to NVS
     * @return true if saved successfully
     */
    bool saveCredentials();

    /**
     * @brief Load STA credentials from NVS
     * @return true if credentials found and loaded
     */
    bool loadCredentials();

    /**
     * @brief Clear saved credentials from NVS
     * @return true if cleared successfully
     */
    bool clearCredentials();

    /**
     * @brief Check if credentials are saved in NVS
     * @return true if credentials exist in NVS
     */
    bool hasCredentials();

private:
    WiFiManager();
    ~WiFiManager() = default;

    /**
     * @brief Generate default AP SSID from MAC
     */
    String generateAPSSID();

    /**
     * @brief Update status structure
     */
    void updateStatus();

    /**
     * @brief Handle WiFi events
     */
    static void onWiFiEvent(WiFiEvent_t event);

    // State
    WiFiConfig m_config;
    WiFiStatus m_status;
    String m_hostname;
    bool m_initialized;
    uint32_t m_sta_connect_start;
    bool m_sta_connecting;

    // Singleton instance pointer for event callback
    static WiFiManager* s_instance;

    static const char* TAG;
};

#endif // WIFI_MANAGER_H
