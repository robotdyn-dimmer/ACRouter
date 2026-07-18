/**
 * @file WiFiManager.cpp
 * @brief WiFi manager implementation using ESP-IDF native API
 *
 * Uses pure ESP-IDF WiFi API instead of Arduino WiFi for better
 * control over task priorities and to avoid conflicts with
 * timing-critical components like AC dimmer zero-cross detection.
 */

#include "WiFiManager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/ip4_addr.h"
#include <cstring>

const char* WiFiManager::TAG = "WiFiMgr";
WiFiManager* WiFiManager::s_instance = nullptr;

// NVS namespace and keys
static const char* NVS_NAMESPACE = "wifi";
static const char* NVS_KEY_SSID = "sta_ssid";
static const char* NVS_KEY_PASSWORD = "sta_pass";

// Static netif handles
static esp_netif_t* s_sta_netif = nullptr;
static esp_netif_t* s_ap_netif = nullptr;

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
// ESP-IDF Event Handler
// ============================================================

static const char* WIFI_TAG = "WiFiMgr";

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    WiFiManager* mgr = (WiFiManager*)arg;
    if (!mgr) return;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(WIFI_TAG, "STA started");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(WIFI_TAG, "STA connected to AP");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(WIFI_TAG, "STA disconnected");
                mgr->m_status.sta_connected = false;
                mgr->m_sta_connecting = false;
                break;
            case WIFI_EVENT_AP_START:
                ESP_LOGI(WIFI_TAG, "AP started");
                break;
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
                ESP_LOGI(WIFI_TAG, "AP: client connected (MAC: %02x:%02x:%02x:%02x:%02x:%02x)",
                         event->mac[0], event->mac[1], event->mac[2],
                         event->mac[3], event->mac[4], event->mac[5]);
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(WIFI_TAG, "AP: client disconnected");
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
            ESP_LOGI(WIFI_TAG, "STA got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            mgr->m_status.sta_connected = true;
            mgr->m_status.sta_ip = IPAddress(event->ip_info.ip.addr);
            mgr->m_sta_connecting = false;
        }
    }
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

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop if not exists
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %d", err);
        return false;
    }

    // Create default WiFi netifs
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    // Reduce WiFi task priority to avoid conflicts with timing-critical tasks
    // Default is 23, we lower it to 18 (below dimmer timer task)
    // Note: This doesn't work reliably, but we try anyway

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        this,
                                                        nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        this,
                                                        nullptr));

    // Set hostname
    esp_netif_set_hostname(s_sta_netif, m_hostname.c_str());

    // Check for credentials
    bool has_sta_credentials = strlen(m_config.sta_ssid) > 0;

    if (!has_sta_credentials) {
        ESP_LOGI(TAG, "No credentials in config, checking NVS...");
        if (loadCredentials()) {
            has_sta_credentials = true;
            ESP_LOGI(TAG, "Loaded credentials from NVS: %s", m_config.sta_ssid);
        } else {
            ESP_LOGI(TAG, "No credentials in NVS, starting AP-only mode");
        }
    }

    if (has_sta_credentials) {
        // AP+STA mode
        ESP_LOGI(TAG, "Starting AP+STA mode (auto-connecting to: %s)", m_config.sta_ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

        // Configure and start AP
        startAP(m_config.ap_ssid[0] ? m_config.ap_ssid : nullptr,
                m_config.ap_password[0] ? m_config.ap_password : nullptr);

        // Start WiFi BEFORE connecting (required for esp_wifi_connect to work)
        ESP_ERROR_CHECK(esp_wifi_start());

        // Configure and start STA (auto-connect from saved credentials)
        ESP_LOGI(TAG, "Auto-connecting to saved network: %s", m_config.sta_ssid);
        connectSTA(m_config.sta_ssid, m_config.sta_password);
    } else {
        // AP only mode
        ESP_LOGI(TAG, "Starting AP-only mode");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        startAP(m_config.ap_ssid[0] ? m_config.ap_ssid : nullptr,
                m_config.ap_password[0] ? m_config.ap_password : nullptr);

        // Start WiFi
        ESP_ERROR_CHECK(esp_wifi_start());

        m_status.state = WiFiState::AP_ONLY;
    }

    // Set WiFi power save mode to NONE for more stable timing
    // MUST be called AFTER esp_wifi_start()
    // This reduces latency but increases power consumption
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "WiFi power save disabled (WIFI_PS_NONE)");

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
        if (m_status.sta_connected) {
            m_sta_connecting = false;
            m_status.state = m_status.ap_active ? WiFiState::AP_STA : WiFiState::STA_CONNECTED;
            ESP_LOGI(TAG, "STA connected, IP: %s", m_status.sta_ip.toString().c_str());

            if (!m_config.ap_always_on && m_status.ap_active) {
                ESP_LOGI(TAG, "Stopping AP (STA connected, ap_always_on=false)");
                stopAP();
            }
        } else if (millis() - m_sta_connect_start > m_config.sta_timeout_ms) {
            m_sta_connecting = false;
            m_status.state = WiFiState::STA_FAILED;
            ESP_LOGW(TAG, "STA connection timeout");

            if (!m_status.ap_active) {
                startAP();
            }
        }
    }

    // Update RSSI
    if (m_status.sta_connected) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            m_status.rssi = ap_info.rssi;
            m_status.sta_ssid = (char*)ap_info.ssid;
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
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();

    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = nullptr;
    }
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = nullptr;
    }

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

    // Save to NVS
    saveCredentials();

    // Configure STA
    wifi_config_t sta_config = {};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    if (password) {
        strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
    }
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    // Ensure STA is enabled in the current mode before configuring it. From AP_ONLY
    // (or NULL), esp_wifi_set_config(WIFI_IF_STA) returns ESP_ERR_WIFI_MODE — and the
    // old ESP_ERROR_CHECK ABORTED the whole device on a plain `wifi-connect`. Promote
    // to APSTA (keep the AP up for the config UI) and handle errors gracefully.
    wifi_mode_t cur = WIFI_MODE_NULL;
    esp_wifi_get_mode(&cur);
    if (cur == WIFI_MODE_AP || cur == WIFI_MODE_NULL) {
        esp_err_t merr = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (merr != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable STA mode: %s", esp_err_to_name(merr));
            return false;
        }
    }

    esp_err_t cerr = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (cerr != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(STA) failed: %s", esp_err_to_name(cerr));
        return false;
    }

    // Connect
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(err));
        return false;
    }

    m_sta_connecting = true;
    m_sta_connect_start = millis();
    m_status.state = WiFiState::STA_CONNECTING;

    return true;
}

void WiFiManager::disconnectSTA() {
    esp_wifi_disconnect();
    m_sta_connecting = false;
    m_status.sta_connected = false;
    m_status.sta_ip = IPAddress();
    m_status.sta_ssid = "";
    m_status.rssi = 0;

    if (m_status.ap_active) {
        m_status.state = WiFiState::AP_ONLY;
        esp_wifi_set_mode(WIFI_MODE_AP);
    } else {
        m_status.state = WiFiState::IDLE;
    }

    ESP_LOGI(TAG, "STA disconnected");
}

// ============================================================
// AP Control
// ============================================================

bool WiFiManager::startAP(const char* ssid, const char* password) {
    // Generate SSID if not provided
    char ap_ssid[33];
    if (ssid && strlen(ssid) > 0) {
        strncpy(ap_ssid, ssid, sizeof(ap_ssid) - 1);
        ap_ssid[sizeof(ap_ssid) - 1] = '\0';
    } else {
        // Generate from MAC
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_AP, mac);
        snprintf(ap_ssid, sizeof(ap_ssid), "ACRouter_%02X%02X", mac[4], mac[5]);
    }

    bool has_password = password && strlen(password) >= 8;

    ESP_LOGI(TAG, "Starting AP: %s (%s)", ap_ssid, has_password ? "secured" : "open");

    // Configure AP
    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(ap_ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;

    if (has_password) {
        strncpy((char*)ap_config.ap.password, password, sizeof(ap_config.ap.password) - 1);
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // Set AP IP
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip_info(s_ap_netif, &ip_info);
    esp_netif_dhcps_start(s_ap_netif);

    m_status.ap_active = true;
    m_status.ap_ip = IPAddress(192, 168, 4, 1);
    m_status.ap_ssid = ap_ssid;

    strncpy(m_config.ap_ssid, ap_ssid, sizeof(m_config.ap_ssid) - 1);
    if (has_password) {
        strncpy(m_config.ap_password, password, sizeof(m_config.ap_password) - 1);
    }

    ESP_LOGI(TAG, "AP configured, IP: %s", m_status.ap_ip.toString().c_str());
    return true;
}

void WiFiManager::stopAP() {
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);

    if (mode == WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_STA);
    } else if (mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_NULL);
    }

    m_status.ap_active = false;
    m_status.ap_ip = IPAddress();
    m_status.sta_clients = 0;

    if (m_status.sta_connected) {
        m_status.state = WiFiState::STA_CONNECTED;
    } else {
        m_status.state = WiFiState::IDLE;
    }

    ESP_LOGI(TAG, "AP stopped");
}

// ============================================================
// Utility
// ============================================================

String WiFiManager::getMACAddress() const {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(mac_str);
}

void WiFiManager::setHostname(const char* hostname) {
    m_hostname = hostname;
    if (m_initialized && s_sta_netif) {
        esp_netif_set_hostname(s_sta_netif, hostname);
    }
}

void WiFiManager::updateStatus() {
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);

    m_status.ap_active = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);

    if (m_status.sta_connected) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) {
            m_status.sta_ip = IPAddress(ip_info.ip.addr);
        }
    }

    if (m_status.ap_active && s_ap_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
            m_status.ap_ip = IPAddress(ip_info.ip.addr);
        }
    }
}

// ============================================================
// NVS Persistence
// ============================================================

bool WiFiManager::saveCredentials() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return false;
    }

    bool success = true;

    err = nvs_set_str(nvs, NVS_KEY_SSID, m_config.sta_ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID: %d", err);
        success = false;
    }

    err = nvs_set_str(nvs, NVS_KEY_PASSWORD, m_config.sta_password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %d", err);
        success = false;
    }

    if (success) {
        err = nvs_commit(nvs);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
            success = false;
        } else {
            ESP_LOGI(TAG, "WiFi credentials saved to NVS: %s", m_config.sta_ssid);
        }
    }

    nvs_close(nvs);
    return success;
}

bool WiFiManager::loadCredentials() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return false;
    }

    size_t len = sizeof(m_config.sta_ssid);
    err = nvs_get_str(nvs, NVS_KEY_SSID, m_config.sta_ssid, &len);
    if (err != ESP_OK || len == 0) {
        nvs_close(nvs);
        return false;
    }

    len = sizeof(m_config.sta_password);
    err = nvs_get_str(nvs, NVS_KEY_PASSWORD, m_config.sta_password, &len);
    if (err != ESP_OK) {
        m_config.sta_password[0] = '\0';
    }

    nvs_close(nvs);
    return strlen(m_config.sta_ssid) > 0;
}

bool WiFiManager::clearCredentials() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return false;
    }

    nvs_erase_key(nvs, NVS_KEY_SSID);
    nvs_erase_key(nvs, NVS_KEY_PASSWORD);
    nvs_commit(nvs);
    nvs_close(nvs);

    m_config.sta_ssid[0] = '\0';
    m_config.sta_password[0] = '\0';

    ESP_LOGI(TAG, "Credentials cleared");
    return true;
}

bool WiFiManager::hasCredentials() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return false;
    }

    size_t len = 0;
    err = nvs_get_str(nvs, NVS_KEY_SSID, nullptr, &len);
    nvs_close(nvs);

    return (err == ESP_OK && len > 1);
}
