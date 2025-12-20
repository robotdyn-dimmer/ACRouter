/**
 * @file WiFiManager.cpp
 * @brief WiFi manager implementation
 */

#include "WiFiManager.h"
#include "esp_log.h"
#include <esp_wifi.h>
#include <Preferences.h>

const char* WiFiManager::TAG = "WiFiMgr";
WiFiManager* WiFiManager::s_instance = nullptr;

// NVS namespace and keys
static const char* NVS_NAMESPACE = "wifi";
static const char* NVS_KEY_SSID = "sta_ssid";
static const char* NVS_KEY_PASSWORD = "sta_pass";

// ============================================================
// Singleton
// ============================================================

WiFiManager& WiFiManager::getInstance() {
    static WiFiManager instance;
    return instance;
}

WiFiManager::WiFiManager()
    : m_hostname("ACRouter")
    , m_initialized(false)
    , m_sta_connect_start(0)
    , m_sta_connecting(false)
{
    s_instance = this;
}

// ============================================================
// Initialization
// ============================================================

bool WiFiManager::begin() {
    WiFiConfig default_config;
    return begin(default_config);
}

bool WiFiManager::begin(const WiFiConfig& config) {
    if (m_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    m_config = config;

    // Set hostname
    WiFi.setHostname(m_hostname.c_str());

    // Register event handler
    WiFi.onEvent(onWiFiEvent);

    // Two-level priority for credentials:
    // Priority 1: Credentials passed in config (from main.cpp)
    // Priority 2: Credentials from NVS (saved by user via 'connect' command)
    // If both empty: Start in AP-only mode

    bool has_sta_credentials = strlen(m_config.sta_ssid) > 0;

    if (has_sta_credentials) {
        ESP_LOGI(TAG, "Using credentials from config: %s", m_config.sta_ssid);
    } else {
        // Try to load from NVS
        ESP_LOGI(TAG, "No credentials in config, checking NVS...");
        if (loadCredentials()) {
            has_sta_credentials = true;
            ESP_LOGI(TAG, "Loaded credentials from NVS: %s", m_config.sta_ssid);
        } else {
            ESP_LOGI(TAG, "No credentials in NVS, starting AP-only mode");
        }
    }

    if (has_sta_credentials) {
        // AP+STA mode: start AP and try to connect to STA
        ESP_LOGI(TAG, "Starting AP+STA mode");
        WiFi.mode(WIFI_AP_STA);

        // Start AP
        startAP(m_config.ap_ssid[0] ? m_config.ap_ssid : nullptr,
                m_config.ap_password[0] ? m_config.ap_password : nullptr);

        // Start STA connection
        connectSTA(m_config.sta_ssid, m_config.sta_password);

    } else {
        // AP only mode
        ESP_LOGI(TAG, "Starting AP-only mode (no STA credentials)");
        WiFi.mode(WIFI_AP);
        startAP(m_config.ap_ssid[0] ? m_config.ap_ssid : nullptr,
                m_config.ap_password[0] ? m_config.ap_password : nullptr);
        m_status.state = WiFiState::AP_ONLY;
    }

    m_initialized = true;
    return true;
}

// ============================================================
// Main Loop Handler
// ============================================================

void WiFiManager::handle() {
    if (!m_initialized) return;

    // Check STA connection timeout
    if (m_sta_connecting) {
        if (WiFi.status() == WL_CONNECTED) {
            m_sta_connecting = false;
            m_status.sta_connected = true;
            m_status.sta_ip = WiFi.localIP();
            m_status.sta_ssid = WiFi.SSID();
            m_status.state = m_status.ap_active ? WiFiState::AP_STA : WiFiState::STA_CONNECTED;

            ESP_LOGI(TAG, "STA connected to %s, IP: %s",
                     m_status.sta_ssid.c_str(),
                     m_status.sta_ip.toString().c_str());

            // Stop AP if not always-on mode
            if (!m_config.ap_always_on && m_status.ap_active) {
                ESP_LOGI(TAG, "Stopping AP (STA connected, ap_always_on=false)");
                stopAP();
            }

        } else if (millis() - m_sta_connect_start > m_config.sta_timeout_ms) {
            m_sta_connecting = false;
            m_status.state = WiFiState::STA_FAILED;
            ESP_LOGW(TAG, "STA connection timeout after %lu ms", m_config.sta_timeout_ms);

            // Make sure AP is running
            if (!m_status.ap_active) {
                startAP();
            }
        }
    }

    // Update RSSI if connected
    if (m_status.sta_connected) {
        m_status.rssi = WiFi.RSSI();

        // Check for disconnect
        if (WiFi.status() != WL_CONNECTED) {
            ESP_LOGW(TAG, "STA disconnected");
            m_status.sta_connected = false;
            m_status.sta_ip = IPAddress();
            m_status.state = m_status.ap_active ? WiFiState::AP_ONLY : WiFiState::IDLE;

            // Restart AP if needed
            if (!m_status.ap_active) {
                startAP();
            }
        }
    }

    // Update AP client count
    if (m_status.ap_active) {
        wifi_sta_list_t sta_list;
        if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
            m_status.sta_clients = sta_list.num;
        }
    }
}

void WiFiManager::stop() {
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    m_status = WiFiStatus();
    m_initialized = false;
    ESP_LOGI(TAG, "WiFi stopped");
}

// ============================================================
// STA Control
// ============================================================

bool WiFiManager::connectSTA(const char* ssid, const char* password) {
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Invalid SSID");
        return false;
    }

    // Save credentials
    strncpy(m_config.sta_ssid, ssid, sizeof(m_config.sta_ssid) - 1);
    m_config.sta_ssid[sizeof(m_config.sta_ssid) - 1] = '\0';

    if (password) {
        strncpy(m_config.sta_password, password, sizeof(m_config.sta_password) - 1);
        m_config.sta_password[sizeof(m_config.sta_password) - 1] = '\0';
    } else {
        m_config.sta_password[0] = '\0';
    }

    ESP_LOGI(TAG, "Connecting to STA: %s", ssid);

    // Save credentials to NVS immediately (before connection attempt)
    // This ensures credentials persist even if device reboots during connection
    if (saveCredentials()) {
        ESP_LOGI(TAG, "Credentials saved to NVS");
    } else {
        ESP_LOGW(TAG, "Failed to save credentials to NVS");
    }

    // Ensure we're in AP+STA mode if AP is active
    if (m_status.ap_active && WiFi.getMode() != WIFI_AP_STA) {
        WiFi.mode(WIFI_AP_STA);
    } else if (!m_status.ap_active && WiFi.getMode() != WIFI_STA) {
        WiFi.mode(WIFI_STA);
    }

    WiFi.begin(ssid, password);
    m_sta_connecting = true;
    m_sta_connect_start = millis();
    m_status.state = WiFiState::STA_CONNECTING;

    return true;
}

void WiFiManager::disconnectSTA() {
    WiFi.disconnect();
    m_sta_connecting = false;
    m_status.sta_connected = false;
    m_status.sta_ip = IPAddress();
    m_status.sta_ssid = "";
    m_status.rssi = 0;

    if (m_status.ap_active) {
        m_status.state = WiFiState::AP_ONLY;
        WiFi.mode(WIFI_AP);
    } else {
        m_status.state = WiFiState::IDLE;
    }

    ESP_LOGI(TAG, "STA disconnected");
}

// ============================================================
// AP Control
// ============================================================

bool WiFiManager::startAP(const char* ssid, const char* password) {
    String ap_ssid = ssid ? String(ssid) : generateAPSSID();

    // Validate password (WPA2 requires 8+ chars)
    bool has_password = password && strlen(password) >= 8;

    ESP_LOGI(TAG, "Starting AP: %s (%s)",
             ap_ssid.c_str(),
             has_password ? "secured" : "open");

    // Configure AP
    IPAddress local_ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.softAPConfig(local_ip, gateway, subnet);

    bool success;
    if (has_password) {
        success = WiFi.softAP(ap_ssid.c_str(), password);
    } else {
        success = WiFi.softAP(ap_ssid.c_str());
    }

    if (success) {
        m_status.ap_active = true;
        m_status.ap_ip = WiFi.softAPIP();
        m_status.ap_ssid = ap_ssid;

        // Save to config
        strncpy(m_config.ap_ssid, ap_ssid.c_str(), sizeof(m_config.ap_ssid) - 1);
        if (has_password) {
            strncpy(m_config.ap_password, password, sizeof(m_config.ap_password) - 1);
        }

        ESP_LOGI(TAG, "AP started, IP: %s", m_status.ap_ip.toString().c_str());
    } else {
        ESP_LOGE(TAG, "Failed to start AP");
    }

    return success;
}

void WiFiManager::stopAP() {
    WiFi.softAPdisconnect(true);
    m_status.ap_active = false;
    m_status.ap_ip = IPAddress();
    m_status.sta_clients = 0;

    // Switch to STA only if connected
    if (m_status.sta_connected) {
        WiFi.mode(WIFI_STA);
        m_status.state = WiFiState::STA_CONNECTED;
    } else {
        m_status.state = WiFiState::IDLE;
    }

    ESP_LOGI(TAG, "AP stopped");
}

// ============================================================
// Utility
// ============================================================

String WiFiManager::generateAPSSID() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[20];
    snprintf(ssid, sizeof(ssid), "ACRouter_%02X%02X", mac[4], mac[5]);
    return String(ssid);
}

String WiFiManager::getMACAddress() const {
    return WiFi.macAddress();
}

void WiFiManager::setHostname(const char* hostname) {
    m_hostname = hostname;
    if (m_initialized) {
        WiFi.setHostname(hostname);
    }
}

void WiFiManager::updateStatus() {
    m_status.sta_connected = (WiFi.status() == WL_CONNECTED);
    if (m_status.sta_connected) {
        m_status.sta_ip = WiFi.localIP();
        m_status.sta_ssid = WiFi.SSID();
        m_status.rssi = WiFi.RSSI();
    }
    m_status.ap_active = (WiFi.getMode() & WIFI_AP) != 0;
    if (m_status.ap_active) {
        m_status.ap_ip = WiFi.softAPIP();
    }
}

// ============================================================
// Event Handler
// ============================================================

void WiFiManager::onWiFiEvent(WiFiEvent_t event) {
    if (!s_instance) return;

    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            ESP_LOGI(TAG, "STA got IP: %s", WiFi.localIP().toString().c_str());
            s_instance->m_status.sta_connected = true;
            s_instance->m_status.sta_ip = WiFi.localIP();
            s_instance->m_sta_connecting = false;
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            ESP_LOGW(TAG, "STA disconnected");
            s_instance->m_status.sta_connected = false;
            break;

        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            ESP_LOGI(TAG, "AP: client connected");
            break;

        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "AP: client disconnected");
            break;

        default:
            break;
    }
}

// ============================================================
// NVS Persistence
// ============================================================

bool WiFiManager::saveCredentials() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        ESP_LOGE(TAG, "Failed to open NVS namespace");
        return false;
    }

    bool success = true;
    if (!prefs.putString(NVS_KEY_SSID, m_config.sta_ssid)) {
        ESP_LOGE(TAG, "Failed to save SSID to NVS");
        success = false;
    }
    if (!prefs.putString(NVS_KEY_PASSWORD, m_config.sta_password)) {
        ESP_LOGE(TAG, "Failed to save password to NVS");
        success = false;
    }

    prefs.end();
    return success;
}

bool WiFiManager::loadCredentials() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {  // Read-only mode
        ESP_LOGW(TAG, "Failed to open NVS namespace for reading");
        return false;
    }

    String ssid = prefs.getString(NVS_KEY_SSID, "");
    String password = prefs.getString(NVS_KEY_PASSWORD, "");
    prefs.end();

    if (ssid.length() == 0) {
        ESP_LOGI(TAG, "No credentials found in NVS");
        return false;
    }

    // Load into config
    strncpy(m_config.sta_ssid, ssid.c_str(), sizeof(m_config.sta_ssid) - 1);
    m_config.sta_ssid[sizeof(m_config.sta_ssid) - 1] = '\0';

    strncpy(m_config.sta_password, password.c_str(), sizeof(m_config.sta_password) - 1);
    m_config.sta_password[sizeof(m_config.sta_password) - 1] = '\0';

    return true;
}

bool WiFiManager::clearCredentials() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        ESP_LOGE(TAG, "Failed to open NVS namespace");
        return false;
    }

    bool success = true;
    if (!prefs.remove(NVS_KEY_SSID)) {
        ESP_LOGW(TAG, "Failed to remove SSID from NVS");
        success = false;
    }
    if (!prefs.remove(NVS_KEY_PASSWORD)) {
        ESP_LOGW(TAG, "Failed to remove password from NVS");
        success = false;
    }

    prefs.end();

    // Clear from config
    m_config.sta_ssid[0] = '\0';
    m_config.sta_password[0] = '\0';

    ESP_LOGI(TAG, "Credentials cleared from NVS");
    return success;
}

bool WiFiManager::hasCredentials() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        return false;
    }

    bool has_ssid = prefs.isKey(NVS_KEY_SSID);
    prefs.end();

    return has_ssid;
}
