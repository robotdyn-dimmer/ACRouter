/**
 * @file MQTTManager.h
 * @brief MQTT client manager for ACRouter
 *
 * Provides MQTT connectivity with:
 * - Automatic reconnection
 * - Last Will & Testament (LWT)
 * - Publishing: status, metrics, config, system info
 * - Subscribing: commands, config changes
 * - Home Assistant Auto-Discovery
 * - JSON aggregated messages
 *
 * Topic Structure:
 *   acrouter/{device_id}/status/    - Device status (retained)
 *   acrouter/{device_id}/metrics/   - Measurements (not retained)
 *   acrouter/{device_id}/config/    - Configuration (retained)
 *   acrouter/{device_id}/command/   - Commands (write-only)
 *   acrouter/{device_id}/system/    - System info (retained)
 *   acrouter/{device_id}/json/      - Aggregated JSON
 */

#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <mqtt_client.h>
#include <esp_log.h>

// Forward declarations
class RouterController;
class PowerMeterADC;
class ConfigManager;

// ============================================================================
// MQTT Configuration
// ============================================================================

/**
 * @brief MQTT configuration structure
 */
struct MQTTConfig {
    char broker[64];            ///< Broker URL (mqtt://host:port or mqtts://...)
    char username[32];          ///< Username (optional)
    char password[32];          ///< Password (optional)
    char client_id[32];         ///< Client ID (auto-generated if empty)
    char device_id[24];         ///< Device ID for topics (default: MAC last 6 chars)
    char device_name[32];       ///< Human-readable device name
    uint32_t publish_interval;  ///< Metrics publish interval in ms (default: 5000)
    uint32_t status_interval;   ///< Status publish interval in ms (default: 30000)
    bool ha_discovery;          ///< Enable Home Assistant auto-discovery
    bool enabled;               ///< MQTT enabled/disabled

    // Defaults
    MQTTConfig() :
        broker{0},
        username{0},
        password{0},
        client_id{0},
        device_id{0},
        device_name{0},
        publish_interval(5000),
        status_interval(30000),
        ha_discovery(true),
        enabled(false) {}
};

// ============================================================================
// MQTT Manager Class
// ============================================================================

/**
 * @brief MQTT client manager singleton
 */
class MQTTManager {
public:
    /**
     * @brief Get singleton instance
     */
    static MQTTManager& getInstance();

    // Delete copy constructor and assignment
    MQTTManager(const MQTTManager&) = delete;
    MQTTManager& operator=(const MQTTManager&) = delete;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Initialize MQTT manager
     * @param router Pointer to RouterController for status/control
     * @param powerMeter Pointer to PowerMeterADC for measurements
     * @param config Pointer to ConfigManager for configuration
     */
    void begin(RouterController* router, PowerMeterADC* powerMeter, ConfigManager* config);

    /**
     * @brief Process MQTT events (call in main loop)
     */
    void loop();

    /**
     * @brief Shutdown MQTT connection
     */
    void end();

    // -------------------------------------------------------------------------
    // Connection Management
    // -------------------------------------------------------------------------

    /**
     * @brief Connect to MQTT broker
     * @return true if connection initiated successfully
     */
    bool connect();

    /**
     * @brief Disconnect from broker
     */
    void disconnect();

    /**
     * @brief Force reconnection
     */
    void reconnect();

    /**
     * @brief Check if connected to broker
     */
    bool isConnected() const { return _connected; }

    /**
     * @brief Check if MQTT is enabled
     */
    bool isEnabled() const { return _config.enabled; }

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /**
     * @brief Get current configuration
     */
    const MQTTConfig& getConfig() const { return _config; }

    /**
     * @brief Set broker URL
     * @param broker URL (mqtt://host:port)
     */
    void setBroker(const char* broker);

    /**
     * @brief Set authentication credentials
     */
    void setCredentials(const char* username, const char* password);

    /**
     * @brief Set custom device ID for topics
     */
    void setDeviceId(const char* deviceId);

    /**
     * @brief Set device name (for HA discovery)
     */
    void setDeviceName(const char* name);

    /**
     * @brief Set metrics publish interval
     * @param intervalMs Interval in milliseconds (1000-60000)
     */
    void setPublishInterval(uint32_t intervalMs);

    /**
     * @brief Enable/disable Home Assistant discovery
     */
    void setHADiscovery(bool enabled);

    /**
     * @brief Enable/disable MQTT
     */
    void setEnabled(bool enabled);

    /**
     * @brief Load configuration from NVS
     */
    bool loadConfig();

    /**
     * @brief Save configuration to NVS
     */
    bool saveConfig();

    // -------------------------------------------------------------------------
    // Publishing
    // -------------------------------------------------------------------------

    /**
     * @brief Publish all status topics
     */
    void publishStatus();

    /**
     * @brief Publish all metrics topics
     */
    void publishMetrics();

    /**
     * @brief Publish configuration topics
     */
    void publishConfig();

    /**
     * @brief Publish system information
     */
    void publishSystemInfo();

    /**
     * @brief Publish Home Assistant discovery messages
     */
    void publishHADiscovery();

    /**
     * @brief Force publish all data immediately
     */
    void publishAll();

    // -------------------------------------------------------------------------
    // Status & Statistics
    // -------------------------------------------------------------------------

    /**
     * @brief Get connection state string
     */
    const char* getConnectionState() const;

    /**
     * @brief Get number of messages published
     */
    uint32_t getMessagesPublished() const { return _messagesPublished; }

    /**
     * @brief Get number of messages received
     */
    uint32_t getMessagesReceived() const { return _messagesReceived; }

    /**
     * @brief Get last error message
     */
    const char* getLastError() const { return _lastError; }

    /**
     * @brief Get uptime since last connection
     */
    uint32_t getConnectionUptime() const;

private:
    // Private constructor for singleton
    MQTTManager();
    ~MQTTManager();

    // -------------------------------------------------------------------------
    // Internal Methods
    // -------------------------------------------------------------------------

    /**
     * @brief Generate default device ID from MAC
     */
    void generateDeviceId();

    /**
     * @brief Build topic string
     * @param category Topic category (status, metrics, etc.)
     * @param name Topic name
     * @return Full topic string
     */
    String buildTopic(const char* category, const char* name);

    /**
     * @brief Publish a message
     * @param topic Full topic string
     * @param payload Message payload
     * @param retain Retain flag
     * @param qos QoS level (0, 1, or 2)
     * @return true if publish successful
     */
    bool publish(const char* topic, const char* payload, bool retain = false, int qos = 0);

    /**
     * @brief Subscribe to a topic
     * @param topic Topic pattern (can include wildcards)
     * @param qos QoS level
     */
    bool subscribe(const char* topic, int qos = 1);

    /**
     * @brief Setup all subscriptions
     */
    void setupSubscriptions();

    /**
     * @brief Publish LWT online message
     */
    void publishOnline();

    // -------------------------------------------------------------------------
    // Message Handlers
    // -------------------------------------------------------------------------

    /**
     * @brief Handle incoming message
     */
    void handleMessage(const char* topic, const char* payload, int len);

    /**
     * @brief Handle command message
     */
    void handleCommand(const char* command, const char* payload);

    /**
     * @brief Handle config set message
     */
    void handleConfigSet(const char* param, const char* value);

    // -------------------------------------------------------------------------
    // Home Assistant Discovery
    // -------------------------------------------------------------------------

    /**
     * @brief Publish HA discovery for a sensor
     */
    void publishHASensor(const char* name, const char* uniqueId, const char* stateTopic,
                         const char* unit, const char* deviceClass, const char* stateClass);

    /**
     * @brief Publish HA discovery for a select entity
     */
    void publishHASelect(const char* name, const char* uniqueId, const char* stateTopic,
                         const char* commandTopic, const char* options[], int optionCount);

    /**
     * @brief Publish HA discovery for a number entity
     */
    void publishHANumber(const char* name, const char* uniqueId, const char* stateTopic,
                         const char* commandTopic, float min, float max, float step,
                         const char* unit = nullptr);

    /**
     * @brief Publish HA discovery for a button entity
     */
    void publishHAButton(const char* name, const char* uniqueId, const char* commandTopic);

    /**
     * @brief Get device info JSON for HA discovery
     */
    String getHADeviceInfo();

    // -------------------------------------------------------------------------
    // ESP-MQTT Event Handler
    // -------------------------------------------------------------------------

    /**
     * @brief Static event handler for esp-mqtt
     */
    static void mqttEventHandler(void* handlerArgs, esp_event_base_t base,
                                  int32_t eventId, void* eventData);

    /**
     * @brief Instance event handler
     */
    void onMqttEvent(esp_mqtt_event_handle_t event);

    // -------------------------------------------------------------------------
    // Members
    // -------------------------------------------------------------------------

    esp_mqtt_client_handle_t _client;   ///< ESP-MQTT client handle
    MQTTConfig _config;                 ///< Current configuration
    bool _connected;                    ///< Connection state
    bool _initialized;                  ///< Initialization state
    bool _clientStarted;                ///< Client start() called (prevents double-start)

    // Component references
    RouterController* _router;          ///< Router controller reference
    PowerMeterADC* _powerMeter;         ///< Power meter reference
    ConfigManager* _configMgr;          ///< Config manager reference

    // Timing
    uint32_t _lastMetricsPublish;       ///< Last metrics publish time
    uint32_t _lastStatusPublish;        ///< Last status publish time
    uint32_t _lastSystemPublish;        ///< Last system info publish time
    uint32_t _connectionStartTime;      ///< Time when connected
    uint32_t _lastReconnectAttempt;     ///< Last reconnect attempt time

    // Statistics
    uint32_t _messagesPublished;        ///< Total messages published
    uint32_t _messagesReceived;         ///< Total messages received
    uint32_t _reconnectCount;           ///< Number of reconnections

    // State tracking for change detection
    uint8_t _lastMode;                  ///< Last published mode
    uint8_t _lastState;                 ///< Last published state
    uint8_t _lastDimmer;                ///< Last published dimmer level

    // Error handling
    char _lastError[64];                ///< Last error message

    // Topic prefix cache
    String _topicPrefix;                ///< Cached topic prefix
};

#endif // MQTT_MANAGER_H
