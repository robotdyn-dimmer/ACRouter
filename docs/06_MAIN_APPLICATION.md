# 6. Main Application File (main.cpp)

## 6.1 Overview

The `main/main.cpp` file is the entry point of the AC Power Router application. It implements the `app_main()` function (standard ESP-IDF entry point) and performs the following tasks:

- **Arduino Core initialization** - for compatibility with Arduino libraries
- **Sequential initialization of all system components** in strict order
- **Callback mechanism setup** for the main power processing loop
- **Main application loop** - handling commands, WiFi, web server

### Operating Architecture

The system operates on a **callback-driven** architecture:

```
PowerMeterADC (DMA ADC)
       |
       | Every 200 ms (10 AC cycles)
       v
RMS Callback ────────> RouterController.update()
       |                       |
       |                       v
       |               Mode processing (AUTO/ECO/OFFGRID)
       |                       |
       |                       v
       |               DimmerHAL (dimmer control)
       v
Main Loop (100 ms)
  ├─> SerialCommand.process()
  ├─> WiFiManager.handle()
  ├─> WebServerManager.handle()
  ├─> NTPManager.handle()
  └─> Statistics (every 10 seconds)
```

**Important**: The main power control logic is executed **inside the RMS callback**, which is called every 200 ms independently of the main loop. The main loop only handles user interface (Serial, WiFi, Web).

---

## 6.2 Component Initialization Order

Components are initialized in a **strictly defined order** considering dependencies:

### Dependency Diagram

```
1. Arduino Core (initArduino)
   └─> 2. Serial (Serial.begin)
       └─> 3. ConfigManager (NVS)
           ├─> 4. WiFiManager (network)
           │   └─> 5. WebServerManager (REST API)
           │       └─> 6. NTPManager (time, after STA connection)
           │
           └─> 7. DimmerHAL (hardware)
               └─> 8. RouterController (control)
                   └─> 9. PowerMeterADC (measurements + callback)
                       └─> 10. SerialCommand (user interface)
```

### Component Criticality Table

| Component | Criticality | Action on Error | Dependencies |
|-----------|-------------|-----------------|--------------|
| Arduino Core | **CRITICAL** | System won't start | - |
| ConfigManager | Medium | Defaults used | Arduino |
| WiFiManager | Low | AP-only mode operation | Config |
| WebServerManager | Low | No web interface | WiFi |
| DimmerHAL | **CRITICAL** | System halted (while(1)) | Arduino |
| RouterController | **CRITICAL** | System halted (while(1)) | DimmerHAL, Config |
| PowerMeterADC | **CRITICAL** | System halted (while(1)) | RouterController |
| SerialCommand | Low | No Serial interface | Config, Router |
| NTPManager | Low | No time synchronization | WiFi STA |

---

## 6.3 Basic main.cpp Version

Below is a **simplified basic version** of main.cpp with comments:

```cpp
/**
 * @file main.cpp
 * @brief AC Power Router Controller - Main Entry Point
 */

#include "Arduino.h"
#include "esp_log.h"
#include "PowerMeterADC.h"
#include "DimmerHAL.h"
#include "RouterController.h"
#include "ConfigManager.h"
#include "SerialCommand.h"
#include "WiFiManager.h"
#include "WebServerManager.h"
#include "NTPManager.h"
#include "PinDefinitions.h"
#include "SensorTypes.h"

static const char* TAG = "MAIN";

// Mode names for logging
const char* ROUTER_MODE_NAMES[] = {"OFF", "AUTO", "MANUAL", "BOOST"};
const char* ROUTER_STATE_NAMES[] = {"IDLE", "INCREASING", "DECREASING",
                                     "AT_MAX", "AT_MIN", "ERROR"};

// Buzzer pin (disable at startup)
#define PIN_BUZZER 4

extern "C" void app_main()
{
    // ================================================================
    // STEP 1: Initialize Arduino Core
    // ================================================================
    initArduino();

    // Disable buzzer (hardware-specific)
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, HIGH);

    // ================================================================
    // STEP 2: Setup Serial for debugging
    // ================================================================
    Serial.begin(115200);
    delay(100);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "AC Power Router Controller");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "========================================");

    // ================================================================
    // STEP 3: Initialize ConfigManager (NVS)
    // ================================================================
    ESP_LOGI(TAG, "Initializing ConfigManager...");
    ConfigManager& config = ConfigManager::getInstance();

    if (!config.begin()) {
        ESP_LOGE(TAG, "Failed to initialize ConfigManager!");
        ESP_LOGW(TAG, "Using default values");
        // NOT critical - continue with defaults
    }

    // ================================================================
    // STEP 4: Initialize WiFiManager
    // ================================================================
    ESP_LOGI(TAG, "Initializing WiFiManager...");
    WiFiManager& wifi = WiFiManager::getInstance();
    wifi.setHostname("ACRouter");

    // Load credentials from NVS (if available) or start AP-only
    if (!wifi.begin()) {
        ESP_LOGE(TAG, "Failed to initialize WiFiManager!");
        // NOT critical - can operate without network
    } else {
        const WiFiStatus& ws = wifi.getStatus();
        if (ws.ap_active) {
            ESP_LOGI(TAG, "WiFi AP started: %s, IP: %s",
                     ws.ap_ssid.c_str(),
                     wifi.getAPIP().toString().c_str());
        }
        if (ws.sta_connected) {
            ESP_LOGI(TAG, "WiFi STA connected: %s, IP: %s",
                     ws.sta_ssid.c_str(),
                     wifi.getSTAIP().toString().c_str());
        }
    }

    // ================================================================
    // STEP 5: Initialize WebServerManager
    // ================================================================
    ESP_LOGI(TAG, "Initializing WebServerManager...");
    WebServerManager& webserver = WebServerManager::getInstance();

    if (!webserver.begin(80, 81)) {
        ESP_LOGE(TAG, "Failed to initialize WebServerManager!");
        // NOT critical - can operate without web interface
    } else {
        ESP_LOGI(TAG, "WebServer started - HTTP:%d, WS:%d",
                 webserver.getHttpPort(), webserver.getWsPort());
    }

    // ================================================================
    // STEP 6: Configure ADC channels for measurements
    // ================================================================
    ADCChannelConfig adc_channels[4] = {
        // Channel 0: Voltage sensor on GPIO35 (ADC1_CH7)
        ADCChannelConfig(
            PIN_VOLTAGE_SENSOR,              // GPIO35
            SensorType::VOLTAGE_AC,          // ZMPT107
            SensorCalibration::ZMPT107_MULTIPLIER,  // ~0.185
            SensorCalibration::ZMPT107_OFFSET,      // ~0.5
            true                             // Enabled
        ),
        // Channel 1: Load current sensor on GPIO39 (ADC1_CH3)
        ADCChannelConfig(
            PIN_CURRENT_SENSOR_1,
            SensorType::CURRENT_LOAD,
            SensorCalibration::SCT013_030_MULTIPLIER,
            SensorCalibration::SCT013_030_OFFSET,
            true
        ),
        // Channel 2: Grid current sensor on GPIO36 (ADC1_CH0)
        ADCChannelConfig(
            PIN_CURRENT_SENSOR_2,
            SensorType::CURRENT_GRID,
            SensorCalibration::SCT013_030_MULTIPLIER,
            SensorCalibration::SCT013_030_OFFSET,
            true
        ),
        // Channel 3: Solar panel current sensor on GPIO34 (ADC1_CH6)
        ADCChannelConfig(
            PIN_CURRENT_SENSOR_3,
            SensorType::CURRENT_SOLAR,
            SensorCalibration::SCT013_030_MULTIPLIER,
            SensorCalibration::SCT013_030_OFFSET,
            true
        )
    };

    // ================================================================
    // STEP 7: Initialize DimmerHAL (CRITICAL!)
    // ================================================================
    ESP_LOGI(TAG, "Initializing DimmerHAL...");
    DimmerHAL& dimmer = DimmerHAL::getInstance();

    if (!dimmer.begin(DimmerCurve::RMS)) {
        ESP_LOGE(TAG, "Failed to initialize DimmerHAL!");
        ESP_LOGE(TAG, "System halted.");
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    ESP_LOGI(TAG, "DimmerHAL initialized, frequency=%d Hz",
             dimmer.getMainsFrequency());

    // ================================================================
    // STEP 8: Initialize RouterController (CRITICAL!)
    // ================================================================
    ESP_LOGI(TAG, "Initializing RouterController...");
    RouterController& router = RouterController::getInstance();

    if (!router.begin(&dimmer, DimmerChannel::CHANNEL_1)) {
        ESP_LOGE(TAG, "Failed to initialize RouterController!");
        ESP_LOGE(TAG, "System halted.");
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Apply configuration from NVS (or defaults)
    const SystemConfig& cfg = config.getConfig();
    router.setControlGain(cfg.control_gain);
    router.setBalanceThreshold(cfg.balance_threshold);
    router.setMode(static_cast<RouterMode>(cfg.router_mode));
    if (cfg.router_mode == 2) {  // MANUAL mode
        router.setManualLevel(cfg.manual_level);
    }
    ESP_LOGI(TAG, "RouterController initialized: mode=%s, gain=%.1f, threshold=%.1f W",
             ROUTER_MODE_NAMES[cfg.router_mode],
             cfg.control_gain,
             cfg.balance_threshold);

    // ================================================================
    // STEP 9: Initialize PowerMeterADC (CRITICAL!)
    // ================================================================
    ESP_LOGI(TAG, "Initializing PowerMeterADC...");
    PowerMeterADC& powerMeter = PowerMeterADC::getInstance();

    if (!powerMeter.begin(adc_channels, 4)) {
        ESP_LOGE(TAG, "Failed to initialize PowerMeterADC!");
        ESP_LOGE(TAG, "System halted.");
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // ================================================================
    // STEP 10: Register RMS Callback - MAIN SYSTEM DRIVER
    // ================================================================
    powerMeter.setResultsCallback([](const PowerMeterADC::Measurements& m,
                                      void* user_data) {
        // This callback is called every 200 ms with new RMS data
        // THIS IS THE MAIN DRIVER for all system processing!

        // === UPDATE ROUTERCONTROLLER ===
        // Pass measurements for AUTO/ECO/OFFGRID mode processing
        RouterController& router = RouterController::getInstance();
        router.update(m);

        // Additional logic can be added here:
        // - Data logging
        // - WebSocket transmission
        // - SD card writing
        // - etc.

    }, nullptr);

    // Start DMA ADC
    if (!powerMeter.start()) {
        ESP_LOGE(TAG, "Failed to start PowerMeterADC!");
        ESP_LOGE(TAG, "System halted.");
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "PowerMeterADC started successfully");

    // ================================================================
    // STEP 11: Initialize SerialCommand processor
    // ================================================================
    SerialCommand& serialCmd = SerialCommand::getInstance();
    serialCmd.begin(&config, &router);

    ESP_LOGI(TAG, "System initialization complete");
    ESP_LOGI(TAG, "Power measurement running (callback-driven)");

    // NTP Manager - initialized when WiFi STA connects
    NTPManager& ntp = NTPManager::getInstance();
    bool ntp_initialized = false;

    // ================================================================
    // MAIN LOOP - system is now callback-driven!
    // ================================================================
    while(1) {
        // Process serial commands
        serialCmd.process();

        // Process WiFi events
        wifi.handle();

        // Initialize NTP when STA connects and gets IP
        const WiFiStatus& ws = wifi.getStatus();
        if (!ntp_initialized && ws.sta_connected &&
            ws.sta_ip != IPAddress(0, 0, 0, 0)) {
            ESP_LOGI(TAG, "WiFi STA connected, initializing NTPManager...");
            // UTC+3 for Moscow, change for your timezone
            if (ntp.begin("pool.ntp.org",
                         "EET-2EEST,M3.5.0/3,M10.5.0/4",
                         3 * 3600, 3600)) {
                ESP_LOGI(TAG, "NTP started - Server: pool.ntp.org");
                ntp_initialized = true;
            } else {
                ESP_LOGE(TAG, "Failed to initialize NTPManager!");
            }
        }

        // Process WebServer requests
        webserver.handle();

        // Process NTP synchronization (if initialized)
        if (ntp_initialized) {
            ntp.handle();
        }

        // Statistics every 10 seconds
        static uint32_t last_stats = 0;
        uint32_t now = millis();
        if (now - last_stats >= 10000) {
            ESP_LOGI(TAG, "Statistics: Frames=%lu, Dropped=%lu, RMS=%lu, Freq=%dHz",
                     powerMeter.getFramesProcessed(),
                     powerMeter.getFramesDropped(),
                     powerMeter.getRMSUpdateCount(),
                     dimmer.getMainsFrequency());

            // WiFi status
            const WiFiStatus& ws = wifi.getStatus();
            if (ws.sta_connected) {
                ESP_LOGI(TAG, "WiFi STA: %s, IP=%s, RSSI=%d",
                         ws.sta_ssid.c_str(),
                         ws.sta_ip.toString().c_str(),
                         ws.rssi);
            }
            if (ws.ap_active) {
                ESP_LOGI(TAG, "WiFi AP: %s, IP=%s, clients=%d",
                         ws.ap_ssid.c_str(),
                         ws.ap_ip.toString().c_str(),
                         ws.sta_clients);
            }

            last_stats = now;
        }

        // 100 ms delay for responsive serial input
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

---

## 6.4 app_main() Function - Detailed Analysis

### 6.4.1 Arduino Core Initialization

```cpp
extern "C" void app_main()
{
    initArduino();
```

**Purpose**: Initializes the Arduino-compatible layer on top of ESP-IDF.

**What it does**:
- Creates Arduino loop task
- Initializes GPIO, I2C, SPI HAL
- Starts FreeRTOS scheduler for Arduino tasks
- Configures Serial UART0

**Important**: Without this call, `Serial`, `pinMode()`, `digitalWrite()`, `millis()` and other Arduino functions won't work.

---

### 6.4.2 Serial Debug Setup

```cpp
Serial.begin(115200);
delay(100);

ESP_LOGI(TAG, "========================================");
ESP_LOGI(TAG, "AC Power Router Controller");
ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
ESP_LOGI(TAG, "========================================");
```

**Parameters**:
- Speed: **115200 baud** (standard for ESP32)
- 100 ms delay for UART stabilization

**Startup output**:
```
========================================
AC Power Router Controller
ESP-IDF Version: v5.5.1
========================================
```

---

### 6.4.3 ConfigManager Initialization

```cpp
ConfigManager& config = ConfigManager::getInstance();

if (!config.begin()) {
    ESP_LOGE(TAG, "Failed to initialize ConfigManager!");
    ESP_LOGW(TAG, "Using default values");
}
```

**What it does**:
- Opens NVS namespace `"acrouter"`
- Loads saved parameters: `router_mode`, `control_gain`, `balance_threshold`, `manual_level`
- If NVS is empty or error - uses defaults

**Criticality**: **Low** - system continues with default values.

**Defaults** (from ConfigManager.cpp):
```cpp
router_mode = 0;           // OFF
control_gain = 800.0f;     // Proportional controller coefficient
balance_threshold = 50.0f; // Balance threshold ±50 W
manual_level = 0;          // Dimmer 0% in MANUAL mode
```

---

### 6.4.4 WiFiManager Initialization

```cpp
WiFiManager& wifi = WiFiManager::getInstance();
wifi.setHostname("ACRouter");

if (!wifi.begin()) {
    ESP_LOGE(TAG, "Failed to initialize WiFiManager!");
}
```

**What it does**:
- Loads WiFi credentials from NVS namespace `"wifi"` (if available)
- If credentials exist - connects to STA + starts AP
- If no credentials - starts only AP: `ACRouter-XXXXXX`

**Two configuration options**:

#### Option 1: Hardcoded credentials (for testing)

```cpp
WiFiConfig wifiConfig;
strncpy(wifiConfig.sta_ssid, "MyNetwork", sizeof(wifiConfig.sta_ssid) - 1);
strncpy(wifiConfig.sta_password, "MyPassword123", sizeof(wifiConfig.sta_password) - 1);
if (!wifi.begin(wifiConfig)) {
    ESP_LOGE(TAG, "Failed to initialize WiFiManager!");
}
```

#### Option 2: From NVS via Serial command (recommended)

```bash
# Via Serial command:
wifi-connect MyNetwork MyPassword123
```

Credentials are saved to NVS and loaded automatically at each startup.

**Criticality**: **Low** - system operates without network in AP-only mode.

---

### 6.4.5 WebServerManager Initialization

```cpp
WebServerManager& webserver = WebServerManager::getInstance();

if (!webserver.begin(80, 81)) {
    ESP_LOGE(TAG, "Failed to initialize WebServerManager!");
} else {
    ESP_LOGI(TAG, "WebServer started - HTTP:%d, WS:%d",
             webserver.getHttpPort(), webserver.getWsPort());
}
```

**Parameters**:
- **HTTP port**: 80 (REST API)
- **WebSocket port**: 81 (real-time data)

**Dependencies**:
- Requires initialized WiFiManager
- Works on both AP IP (192.168.4.1) and STA IP

**Criticality**: **Low** - can control via Serial.

---

### 6.4.6 ADC Channel Configuration

```cpp
ADCChannelConfig adc_channels[4] = {
    ADCChannelConfig(
        PIN_VOLTAGE_SENSOR,              // GPIO35
        SensorType::VOLTAGE_AC,          // ZMPT107
        SensorCalibration::ZMPT107_MULTIPLIER,  // ~0.185
        SensorCalibration::ZMPT107_OFFSET,      // ~0.5
        true                             // enabled
    ),
    // ... remaining 3 channels
};
```

**ADCChannelConfig structure**:
```cpp
struct ADCChannelConfig {
    gpio_num_t gpio;          // GPIO pin (ADC1: 32-39)
    SensorType type;          // Sensor type
    float multiplier;         // Calibration multiplier
    float offset;             // ADC offset (usually 0.5)
    bool enabled;             // Channel enabled/disabled
};
```

**Sensor types**:
- `VOLTAGE_AC`: ZMPT107 (220V AC voltage)
- `CURRENT_LOAD`: ACS-712 or SCT-013 (dimmer current)
- `CURRENT_GRID`: SCT-013 (grid current)
- `CURRENT_SOLAR`: SCT-013 (solar panel current)

**GPIO pins** (from PinDefinitions.h):
```cpp
#define PIN_VOLTAGE_SENSOR    35  // ADC1_CH7
#define PIN_CURRENT_SENSOR_1  39  // ADC1_CH3 (Load)
#define PIN_CURRENT_SENSOR_2  36  // ADC1_CH0 (Grid)
#define PIN_CURRENT_SENSOR_3  34  // ADC1_CH6 (Solar)
```

---

### 6.4.7 DimmerHAL Initialization (CRITICAL!)

```cpp
DimmerHAL& dimmer = DimmerHAL::getInstance();

if (!dimmer.begin(DimmerCurve::RMS)) {
    ESP_LOGE(TAG, "Failed to initialize DimmerHAL!");
    ESP_LOGE(TAG, "System halted.");
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**What it does**:
- Initializes zero-crossing detector on GPIO26
- Configures TRIAC control pins (GPIO22, GPIO23)
- Starts FreeRTOS task for mains synchronization
- Detects mains frequency (50 or 60 Hz)

**DimmerCurve parameter**:
- `DimmerCurve::RMS`: Linear curve for heating elements (resistors)
- `DimmerCurve::LINEAR`: For incandescent lamps (not used)

**Criticality**: **MAXIMUM** - without DimmerHAL, power control is impossible. On error, system halts (`while(1)`).

**Possible errors**:
- No zero-crossing signal (mains not connected)
- Zero-crossing detector malfunction (H11AA1)
- GPIO conflict

---

### 6.4.8 RouterController Initialization (CRITICAL!)

```cpp
RouterController& router = RouterController::getInstance();

if (!router.begin(&dimmer, DimmerChannel::CHANNEL_1)) {
    ESP_LOGE(TAG, "Failed to initialize RouterController!");
    ESP_LOGE(TAG, "System halted.");
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Apply configuration from NVS
const SystemConfig& cfg = config.getConfig();
router.setControlGain(cfg.control_gain);
router.setBalanceThreshold(cfg.balance_threshold);
router.setMode(static_cast<RouterMode>(cfg.router_mode));
if (cfg.router_mode == 2) {  // MANUAL mode
    router.setManualLevel(cfg.manual_level);
}
```

**Parameters**:
- **dimmer**: Pointer to DimmerHAL
- **channel**: `DimmerChannel::CHANNEL_1` (first of two dimmer channels)

**Settings from NVS**:
- `control_gain`: P-controller coefficient (default 800.0)
- `balance_threshold`: Balance threshold (default 50.0 W)
- `router_mode`: Operating mode (default OFF)
- `manual_level`: Dimmer level for MANUAL mode (default 0%)

**Criticality**: **MAXIMUM** - without RouterController, there's no control logic.

---

### 6.4.9 PowerMeterADC Initialization (CRITICAL!)

```cpp
PowerMeterADC& powerMeter = PowerMeterADC::getInstance();

if (!powerMeter.begin(adc_channels, 4)) {
    ESP_LOGE(TAG, "Failed to initialize PowerMeterADC!");
    ESP_LOGE(TAG, "System halted.");
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**What it does**:
- Configures ADC1 in continuous (DMA) mode
- Sampling frequency: **10 kHz per channel** (80 kHz total for 8 channels)
- Creates DMA buffers
- Starts callback processing every 10 ms (DMA frame)
- Calculates RMS every 200 ms (20 frames)

**Criticality**: **MAXIMUM** - without PowerMeterADC, there are no power measurements.

---

### 6.4.10 RMS Callback Registration - Main System Driver

```cpp
powerMeter.setResultsCallback([](const PowerMeterADC::Measurements& m,
                                  void* user_data) {
    // Called every 200 ms with new RMS data

    RouterController& router = RouterController::getInstance();
    router.update(m);  // MAIN CONTROL LOGIC!

}, nullptr);
```

**Call frequency**: **Every 200 ms** (5 times per second)

**What happens inside the callback**:
1. **RouterController::update(m)** receives measurements:
   - `m.voltage_rms`: Grid voltage (V)
   - `m.current_rms[]`: Currents on 4 channels (A)
   - `m.power_active[]`: Active power (W)
   - `m.direction[]`: Current direction (consumption/generation)

2. **RouterController analyzes mode**:
   - **AUTO**: P_grid → 0 (proportional controller)
   - **ECO**: P_grid ≤ 0 (anti-export)
   - **OFFGRID**: P_load ≤ 0.8 × P_solar
   - **MANUAL**: Fixed level
   - **BOOST**: 100% power
   - **OFF**: 0% power

3. **DimmerHAL sets power level** on TRIAC

**IMPORTANT**: This is the **only place** in the code where power control happens! The main loop only handles user interface.

---

### 6.4.11 Starting PowerMeterADC

```cpp
if (!powerMeter.start()) {
    ESP_LOGE(TAG, "Failed to start PowerMeterADC!");
    ESP_LOGE(TAG, "System halted.");
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

ESP_LOGI(TAG, "PowerMeterADC started successfully");
```

**What it does**:
- Starts DMA ADC pipeline
- Begins calling DMA callbacks every 10 ms
- Starts RMS callback every 200 ms

From this point, the system is completely **callback-driven** - power control works independently of the main loop.

---

### 6.4.12 SerialCommand Initialization

```cpp
SerialCommand& serialCmd = SerialCommand::getInstance();
serialCmd.begin(&config, &router);

ESP_LOGI(TAG, "System initialization complete");
ESP_LOGI(TAG, "Power measurement running (callback-driven)");
```

**What it does**:
- Registers Serial interface commands
- Links commands to ConfigManager and RouterController
- Enables system control via UART

**Command examples**:
```bash
status                    # Show system status
set-mode auto            # Switch to AUTO mode
set-gain 800             # Set gain coefficient
calibrate-adc 0 1.0 0.5  # Calibrate ADC channel 0
```

The complete command list will be in the next documentation section (07_COMMANDS.md).

---

## 6.5 Main Loop

After all components are initialized, the **infinite main loop** starts:

```cpp
while(1) {
    // 1. Process serial commands
    serialCmd.process();

    // 2. Process WiFi events
    wifi.handle();

    // 3. Initialize NTP when STA connects
    const WiFiStatus& ws = wifi.getStatus();
    if (!ntp_initialized && ws.sta_connected &&
        ws.sta_ip != IPAddress(0, 0, 0, 0)) {
        // NTPManager initialization...
    }

    // 4. Process WebServer requests
    webserver.handle();

    // 5. Process NTP synchronization
    if (ntp_initialized) {
        ntp.handle();
    }

    // 6. Statistics every 10 seconds
    static uint32_t last_stats = 0;
    uint32_t now = millis();
    if (now - last_stats >= 10000) {
        // Output statistics...
        last_stats = now;
    }

    // 7. 100 ms delay
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

### Main Loop Tasks

| Task | Frequency | Purpose |
|------|-----------|---------|
| `serialCmd.process()` | Every 100 ms | Process commands from Serial UART |
| `wifi.handle()` | Every 100 ms | Reconnect, AP keepalive, events |
| NTP initialization | Once on STA connect | Start time synchronization |
| `webserver.handle()` | Every 100 ms | Process HTTP/WebSocket requests |
| `ntp.handle()` | Every 100 ms | Periodic synchronization (hourly) |
| Statistics | Every 10 seconds | Serial logging |

**Important**: The main loop **does NOT handle power control**! This is done by the RMS callback every 200 ms independently.

---

### 6.5.1 NTP Manager - Deferred Initialization

```cpp
NTPManager& ntp = NTPManager::getInstance();
bool ntp_initialized = false;

// Inside main loop:
const WiFiStatus& ws = wifi.getStatus();
if (!ntp_initialized && ws.sta_connected &&
    ws.sta_ip != IPAddress(0, 0, 0, 0)) {
    ESP_LOGI(TAG, "WiFi STA connected, initializing NTPManager...");
    if (ntp.begin("pool.ntp.org",
                 "EET-2EEST,M3.5.0/3,M10.5.0/4",  // Timezone
                 3 * 3600,                         // GMT offset (UTC+3)
                 3600)) {                          // DST offset (1 hour)
        ESP_LOGI(TAG, "NTP started - Server: pool.ntp.org");
        ntp_initialized = true;
    } else {
        ESP_LOGE(TAG, "Failed to initialize NTPManager!");
    }
}
```

**Why deferred initialization?**
- NTP requires internet connection (STA mode)
- In AP-only mode, NTP is not needed
- When STA connects - starts automatically

**NTP parameters**:
- **Server**: `pool.ntp.org` (public NTP server pool)
- **Timezone**: `EET-2EEST,M3.5.0/3,M10.5.0/4` (UTC+3 with daylight saving time transition)
- **GMT offset**: 3 * 3600 = 10800 seconds (UTC+3)
- **DST offset**: 3600 seconds (1 hour)

**Change for other timezones**:
```cpp
// UTC+0 (London)
ntp.begin("pool.ntp.org", "GMT0BST,M3.5.0/1,M10.5.0", 0, 3600);

// UTC-5 (New York)
ntp.begin("pool.ntp.org", "EST5EDT,M3.2.0,M11.1.0", -5 * 3600, 3600);

// UTC+8 (Beijing)
ntp.begin("pool.ntp.org", "CST-8", 8 * 3600, 0);
```

---

### 6.5.2 Statistics Every 10 Seconds

```cpp
static uint32_t last_stats = 0;
uint32_t now = millis();
if (now - last_stats >= 10000) {
    ESP_LOGI(TAG, "Statistics: Frames=%lu, Dropped=%lu, RMS=%lu, Freq=%dHz",
             powerMeter.getFramesProcessed(),
             powerMeter.getFramesDropped(),
             powerMeter.getRMSUpdateCount(),
             dimmer.getMainsFrequency());

    // WiFi status
    const WiFiStatus& ws = wifi.getStatus();
    if (ws.sta_connected) {
        ESP_LOGI(TAG, "WiFi STA: %s, IP=%s, RSSI=%d",
                 ws.sta_ssid.c_str(),
                 ws.sta_ip.toString().c_str(),
                 ws.rssi);
    }
    if (ws.ap_active) {
        ESP_LOGI(TAG, "WiFi AP: %s, IP=%s, clients=%d",
                 ws.ap_ssid.c_str(),
                 ws.ap_ip.toString().c_str(),
                 ws.sta_clients);
    }

    last_stats = now;
}
```

**Serial output**:
```
I (10000) MAIN: Statistics: Frames=1000, Dropped=0, RMS=50, Freq=50Hz
I (10000) MAIN: WiFi STA: MyNetwork, IP=192.168.1.100, RSSI=-45
I (10000) MAIN: WiFi AP: ACRouter-ABCD, IP=192.168.4.1, clients=1
```

**Statistics parameters**:
- **Frames**: Number of DMA frames (every 10 ms)
- **Dropped**: Dropped frames (should be 0!)
- **RMS**: Number of RMS calculations (every 200 ms)
- **Freq**: Detected mains frequency (50 or 60 Hz)

---

## 6.6 Minimal Version (without WiFi and WebServer)

For systems that don't need network interface:

```cpp
extern "C" void app_main()
{
    initArduino();
    Serial.begin(115200);
    delay(100);

    ESP_LOGI(TAG, "AC Power Router - Minimal Version");

    // ConfigManager
    ConfigManager& config = ConfigManager::getInstance();
    config.begin();

    // ADC channels configuration
    ADCChannelConfig adc_channels[4] = {
        // ... (same as above)
    };

    // DimmerHAL
    DimmerHAL& dimmer = DimmerHAL::getInstance();
    if (!dimmer.begin(DimmerCurve::RMS)) {
        ESP_LOGE(TAG, "DimmerHAL init failed!");
        while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    // RouterController
    RouterController& router = RouterController::getInstance();
    if (!router.begin(&dimmer, DimmerChannel::CHANNEL_1)) {
        ESP_LOGE(TAG, "RouterController init failed!");
        while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    const SystemConfig& cfg = config.getConfig();
    router.setControlGain(cfg.control_gain);
    router.setBalanceThreshold(cfg.balance_threshold);
    router.setMode(static_cast<RouterMode>(cfg.router_mode));

    // PowerMeterADC
    PowerMeterADC& powerMeter = PowerMeterADC::getInstance();
    if (!powerMeter.begin(adc_channels, 4)) {
        ESP_LOGE(TAG, "PowerMeterADC init failed!");
        while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    // RMS Callback
    powerMeter.setResultsCallback([](const PowerMeterADC::Measurements& m,
                                      void* user_data) {
        RouterController& router = RouterController::getInstance();
        router.update(m);
    }, nullptr);

    if (!powerMeter.start()) {
        ESP_LOGE(TAG, "PowerMeterADC start failed!");
        while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    // SerialCommand
    SerialCommand& serialCmd = SerialCommand::getInstance();
    serialCmd.begin(&config, &router);

    ESP_LOGI(TAG, "System ready (minimal mode)");

    // Main loop
    while(1) {
        serialCmd.process();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

**Minimal version advantages**:
- Less memory usage (no WiFi/WebServer)
- Faster startup
- Fewer dependencies
- Control only via Serial

**Disadvantages**:
- No remote access
- No web interface
- No time synchronization

---

## 6.7 Arduino-Format Application

For those who prefer the standard Arduino format with `setup()` and `loop()` functions, below shows how to organize the code in this style.

**Important**: In ESP-IDF with Arduino Core, `setup()` and `loop()` functions are called automatically from `app_main()`. If you create a `main.cpp` file with `app_main()`, you **cannot** use `setup()` and `loop()` - they conflict. But if you work in Arduino IDE with Arduino framework, use the format below.

### 6.7.1 Global Variables

```cpp
/**
 * @file ACRouter.ino
 * @brief AC Power Router Controller - Arduino Format
 */

#include "Arduino.h"
#include "esp_log.h"
#include "PowerMeterADC.h"
#include "DimmerHAL.h"
#include "RouterController.h"
#include "ConfigManager.h"
#include "SerialCommand.h"
#include "WiFiManager.h"
#include "WebServerManager.h"
#include "NTPManager.h"
#include "PinDefinitions.h"
#include "SensorTypes.h"

static const char* TAG = "ACROUTER";

// Global component references (for use in loop)
ConfigManager* g_config = nullptr;
WiFiManager* g_wifi = nullptr;
WebServerManager* g_webserver = nullptr;
NTPManager* g_ntp = nullptr;
SerialCommand* g_serialCmd = nullptr;
PowerMeterADC* g_powerMeter = nullptr;
DimmerHAL* g_dimmer = nullptr;
RouterController* g_router = nullptr;

// State flags
bool g_ntp_initialized = false;
uint32_t g_last_stats = 0;

// Buzzer pin
#define PIN_BUZZER 4
```

---

### 6.7.2 setup() Function

```cpp
void setup()
{
    // ================================================================
    // STEP 1: Disable buzzer
    // ================================================================
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, HIGH);

    // ================================================================
    // STEP 2: Setup Serial for debugging
    // ================================================================
    Serial.begin(115200);
    delay(100);

    Serial.println("========================================");
    Serial.println("AC Power Router Controller");
    Serial.print("ESP-IDF Version: ");
    Serial.println(esp_get_idf_version());
    Serial.println("========================================");

    // ================================================================
    // STEP 3: Initialize ConfigManager (NVS)
    // ================================================================
    Serial.println("Initializing ConfigManager...");
    g_config = &ConfigManager::getInstance();

    if (!g_config->begin()) {
        Serial.println("ERROR: Failed to initialize ConfigManager!");
        Serial.println("WARNING: Using default values");
    }

    // ================================================================
    // STEP 4: Initialize WiFiManager
    // ================================================================
    Serial.println("Initializing WiFiManager...");
    g_wifi = &WiFiManager::getInstance();
    g_wifi->setHostname("ACRouter");

    if (!g_wifi->begin()) {
        Serial.println("ERROR: Failed to initialize WiFiManager!");
    } else {
        const WiFiStatus& ws = g_wifi->getStatus();
        if (ws.ap_active) {
            Serial.print("WiFi AP started: ");
            Serial.print(ws.ap_ssid);
            Serial.print(", IP: ");
            Serial.println(g_wifi->getAPIP().toString());
        }
        if (ws.sta_connected) {
            Serial.print("WiFi STA connected: ");
            Serial.print(ws.sta_ssid);
            Serial.print(", IP: ");
            Serial.println(g_wifi->getSTAIP().toString());
        }
    }

    // ================================================================
    // STEP 5: Initialize WebServerManager
    // ================================================================
    Serial.println("Initializing WebServerManager...");
    g_webserver = &WebServerManager::getInstance();

    if (!g_webserver->begin(80, 81)) {
        Serial.println("ERROR: Failed to initialize WebServerManager!");
    } else {
        Serial.print("WebServer started - HTTP:");
        Serial.print(g_webserver->getHttpPort());
        Serial.print(", WS:");
        Serial.println(g_webserver->getWsPort());
    }

    // ================================================================
    // STEP 6: Configure ADC channels for measurements
    // ================================================================
    static ADCChannelConfig adc_channels[4] = {
        // Channel 0: Voltage sensor on GPIO35 (ADC1_CH7)
        ADCChannelConfig(
            PIN_VOLTAGE_SENSOR,
            SensorType::VOLTAGE_AC,
            SensorCalibration::ZMPT107_MULTIPLIER,
            SensorCalibration::ZMPT107_OFFSET,
            true
        ),
        // Channel 1: Load current sensor on GPIO39 (ADC1_CH3)
        ADCChannelConfig(
            PIN_CURRENT_SENSOR_1,
            SensorType::CURRENT_LOAD,
            SensorCalibration::SCT013_030_MULTIPLIER,
            SensorCalibration::SCT013_030_OFFSET,
            true
        ),
        // Channel 2: Grid current sensor on GPIO36 (ADC1_CH0)
        ADCChannelConfig(
            PIN_CURRENT_SENSOR_2,
            SensorType::CURRENT_GRID,
            SensorCalibration::SCT013_030_MULTIPLIER,
            SensorCalibration::SCT013_030_OFFSET,
            true
        ),
        // Channel 3: Solar panel current sensor on GPIO34 (ADC1_CH6)
        ADCChannelConfig(
            PIN_CURRENT_SENSOR_3,
            SensorType::CURRENT_SOLAR,
            SensorCalibration::SCT013_030_MULTIPLIER,
            SensorCalibration::SCT013_030_OFFSET,
            true
        )
    };

    // ================================================================
    // STEP 7: Initialize DimmerHAL (CRITICAL!)
    // ================================================================
    Serial.println("Initializing DimmerHAL...");
    g_dimmer = &DimmerHAL::getInstance();

    if (!g_dimmer->begin(DimmerCurve::RMS)) {
        Serial.println("ERROR: Failed to initialize DimmerHAL!");
        Serial.println("System halted.");
        while(1) {
            delay(1000);
        }
    }
    Serial.print("DimmerHAL initialized, frequency=");
    Serial.print(g_dimmer->getMainsFrequency());
    Serial.println(" Hz");

    // ================================================================
    // STEP 8: Initialize RouterController (CRITICAL!)
    // ================================================================
    Serial.println("Initializing RouterController...");
    g_router = &RouterController::getInstance();

    if (!g_router->begin(g_dimmer, DimmerChannel::CHANNEL_1)) {
        Serial.println("ERROR: Failed to initialize RouterController!");
        Serial.println("System halted.");
        while(1) {
            delay(1000);
        }
    }

    // Apply configuration from NVS (or defaults)
    const SystemConfig& cfg = g_config->getConfig();
    g_router->setControlGain(cfg.control_gain);
    g_router->setBalanceThreshold(cfg.balance_threshold);
    g_router->setMode(static_cast<RouterMode>(cfg.router_mode));
    if (cfg.router_mode == 2) {  // MANUAL mode
        g_router->setManualLevel(cfg.manual_level);
    }
    Serial.print("RouterController initialized: mode=");
    Serial.print(cfg.router_mode);
    Serial.print(", gain=");
    Serial.print(cfg.control_gain);
    Serial.print(", threshold=");
    Serial.print(cfg.balance_threshold);
    Serial.println(" W");

    // ================================================================
    // STEP 9: Initialize PowerMeterADC (CRITICAL!)
    // ================================================================
    Serial.println("Initializing PowerMeterADC...");
    g_powerMeter = &PowerMeterADC::getInstance();

    if (!g_powerMeter->begin(adc_channels, 4)) {
        Serial.println("ERROR: Failed to initialize PowerMeterADC!");
        Serial.println("System halted.");
        while(1) {
            delay(1000);
        }
    }

    // ================================================================
    // STEP 10: Register RMS Callback - MAIN SYSTEM DRIVER
    // ================================================================
    g_powerMeter->setResultsCallback([](const PowerMeterADC::Measurements& m,
                                         void* user_data) {
        // Called every 200 ms with new RMS data

        // Update RouterController
        g_router->update(m);

        // Additional logic can be added here:
        // - WebSocket transmission
        // - SD card writing
        // - etc.

    }, nullptr);

    // Start DMA ADC
    if (!g_powerMeter->start()) {
        Serial.println("ERROR: Failed to start PowerMeterADC!");
        Serial.println("System halted.");
        while(1) {
            delay(1000);
        }
    }

    Serial.println("PowerMeterADC started successfully");

    // ================================================================
    // STEP 11: Initialize SerialCommand processor
    // ================================================================
    g_serialCmd = &SerialCommand::getInstance();
    g_serialCmd->begin(g_config, g_router);

    // ================================================================
    // STEP 12: Initialize NTPManager (will be later in loop)
    // ================================================================
    g_ntp = &NTPManager::getInstance();

    Serial.println("System initialization complete");
    Serial.println("Power measurement running (callback-driven)");
}
```

---

### 6.7.3 loop() Function

```cpp
void loop()
{
    // ================================================================
    // 1. Process serial commands
    // ================================================================
    g_serialCmd->process();

    // ================================================================
    // 2. Process WiFi events
    // ================================================================
    g_wifi->handle();

    // ================================================================
    // 3. Initialize NTP when STA connects and gets IP
    // ================================================================
    const WiFiStatus& ws = g_wifi->getStatus();
    if (!g_ntp_initialized && ws.sta_connected &&
        ws.sta_ip != IPAddress(0, 0, 0, 0)) {
        Serial.println("WiFi STA connected, initializing NTPManager...");
        // UTC+3 for Moscow, change for your timezone
        if (g_ntp->begin("pool.ntp.org",
                         "EET-2EEST,M3.5.0/3,M10.5.0/4",
                         3 * 3600, 3600)) {
            Serial.println("NTP started - Server: pool.ntp.org");
            g_ntp_initialized = true;
        } else {
            Serial.println("ERROR: Failed to initialize NTPManager!");
        }
    }

    // ================================================================
    // 4. Process WebServer requests
    // ================================================================
    g_webserver->handle();

    // ================================================================
    // 5. Process NTP synchronization (if initialized)
    // ================================================================
    if (g_ntp_initialized) {
        g_ntp->handle();
    }

    // ================================================================
    // 6. Statistics every 10 seconds
    // ================================================================
    uint32_t now = millis();
    if (now - g_last_stats >= 10000) {
        Serial.print("Statistics: Frames=");
        Serial.print(g_powerMeter->getFramesProcessed());
        Serial.print(", Dropped=");
        Serial.print(g_powerMeter->getFramesDropped());
        Serial.print(", RMS=");
        Serial.print(g_powerMeter->getRMSUpdateCount());
        Serial.print(", Freq=");
        Serial.print(g_dimmer->getMainsFrequency());
        Serial.println("Hz");

        // WiFi status
        if (ws.sta_connected) {
            Serial.print("WiFi STA: ");
            Serial.print(ws.sta_ssid);
            Serial.print(", IP=");
            Serial.print(ws.sta_ip.toString());
            Serial.print(", RSSI=");
            Serial.println(ws.rssi);
        }
        if (ws.ap_active) {
            Serial.print("WiFi AP: ");
            Serial.print(ws.ap_ssid);
            Serial.print(", IP=");
            Serial.print(ws.ap_ip.toString());
            Serial.print(", clients=");
            Serial.println(ws.sta_clients);
        }

        g_last_stats = now;
    }

    // ================================================================
    // 7. 100 ms delay for responsive serial input
    // ================================================================
    delay(100);
}
```

---

### 6.7.4 Comparison: app_main() vs setup()/loop()

| Aspect | app_main() | setup() + loop() |
|--------|-----------|------------------|
| **Entry point** | `extern "C" void app_main()` | `void setup()` + `void loop()` |
| **Global variables** | Not needed (local in app_main) | Needed (for access from loop) |
| **Infinite loop** | Explicit `while(1)` | Automatic loop() call |
| **Delay** | `vTaskDelay(pdMS_TO_TICKS(100))` | `delay(100)` |
| **FreeRTOS** | Direct access | Through Arduino wrapper |
| **Compatibility** | ESP-IDF only | Multiplatform (ESP32, AVR, etc) |

---

### 6.7.5 Minimal Arduino Version

Simplified version without WiFi/WebServer for Arduino IDE:

```cpp
#include "Arduino.h"
#include "esp_log.h"
#include "PowerMeterADC.h"
#include "DimmerHAL.h"
#include "RouterController.h"
#include "ConfigManager.h"
#include "SerialCommand.h"
#include "PinDefinitions.h"
#include "SensorTypes.h"

static const char* TAG = "ACROUTER";

ConfigManager* g_config = nullptr;
SerialCommand* g_serialCmd = nullptr;
PowerMeterADC* g_powerMeter = nullptr;
DimmerHAL* g_dimmer = nullptr;
RouterController* g_router = nullptr;

void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.println("AC Power Router - Minimal Arduino Version");

    // ConfigManager
    g_config = &ConfigManager::getInstance();
    g_config->begin();

    // ADC channels
    static ADCChannelConfig adc_channels[4] = {
        ADCChannelConfig(PIN_VOLTAGE_SENSOR, SensorType::VOLTAGE_AC,
                         SensorCalibration::ZMPT107_MULTIPLIER,
                         SensorCalibration::ZMPT107_OFFSET, true),
        ADCChannelConfig(PIN_CURRENT_SENSOR_1, SensorType::CURRENT_LOAD,
                         SensorCalibration::SCT013_030_MULTIPLIER,
                         SensorCalibration::SCT013_030_OFFSET, true),
        ADCChannelConfig(PIN_CURRENT_SENSOR_2, SensorType::CURRENT_GRID,
                         SensorCalibration::SCT013_030_MULTIPLIER,
                         SensorCalibration::SCT013_030_OFFSET, true),
        ADCChannelConfig(PIN_CURRENT_SENSOR_3, SensorType::CURRENT_SOLAR,
                         SensorCalibration::SCT013_030_MULTIPLIER,
                         SensorCalibration::SCT013_030_OFFSET, true)
    };

    // DimmerHAL
    g_dimmer = &DimmerHAL::getInstance();
    if (!g_dimmer->begin(DimmerCurve::RMS)) {
        Serial.println("ERROR: DimmerHAL init failed!");
        while(1) { delay(1000); }
    }

    // RouterController
    g_router = &RouterController::getInstance();
    if (!g_router->begin(g_dimmer, DimmerChannel::CHANNEL_1)) {
        Serial.println("ERROR: RouterController init failed!");
        while(1) { delay(1000); }
    }

    const SystemConfig& cfg = g_config->getConfig();
    g_router->setControlGain(cfg.control_gain);
    g_router->setBalanceThreshold(cfg.balance_threshold);
    g_router->setMode(static_cast<RouterMode>(cfg.router_mode));

    // PowerMeterADC
    g_powerMeter = &PowerMeterADC::getInstance();
    if (!g_powerMeter->begin(adc_channels, 4)) {
        Serial.println("ERROR: PowerMeterADC init failed!");
        while(1) { delay(1000); }
    }

    // RMS Callback
    g_powerMeter->setResultsCallback([](const PowerMeterADC::Measurements& m,
                                         void* user_data) {
        g_router->update(m);
    }, nullptr);

    if (!g_powerMeter->start()) {
        Serial.println("ERROR: PowerMeterADC start failed!");
        while(1) { delay(1000); }
    }

    // SerialCommand
    g_serialCmd = &SerialCommand::getInstance();
    g_serialCmd->begin(g_config, g_router);

    Serial.println("System ready");
}

void loop()
{
    g_serialCmd->process();
    delay(100);
}
```

---

### 6.7.6 Important Arduino Format Differences

**1. Global pointers:**
```cpp
// In app_main() we use local references:
ConfigManager& config = ConfigManager::getInstance();

// In setup()/loop() we need global pointers:
ConfigManager* g_config = nullptr;
g_config = &ConfigManager::getInstance();
```

**2. Delays:**
```cpp
// app_main():
vTaskDelay(pdMS_TO_TICKS(100));

// loop():
delay(100);
```

**3. Infinite loop:**
```cpp
// app_main() - explicit loop:
while(1) {
    // ...
}

// loop() - called automatically:
void loop() {
    // ... code executes infinitely
}
```

**4. Static variables in setup():**
```cpp
// For use in callback, static is needed:
static ADCChannelConfig adc_channels[4] = { ... };

// Otherwise the array will be deleted after exiting setup()!
```

---

### 6.7.7 When to Use Each Format?

**Use app_main() if:**
- ✅ Working in ESP-IDF framework
- ✅ Need full control over FreeRTOS
- ✅ Project is ESP32 only
- ✅ Using advanced ESP-IDF features

**Use setup()/loop() if:**
- ✅ Working in Arduino IDE
- ✅ Using PlatformIO: check compatible of PlatformIO with ESP32 Arduino core 3.x
- ✅ Need compatibility with Arduino libraries
- ✅ More comfortable with Arduino code style
- ✅ Planning to port to other platforms

---

## 6.8 FreeRTOS Tasks

Although main.cpp doesn't explicitly create FreeRTOS tasks, they are created inside the components:

### System Tasks Table

| Task | Component | Priority | Stack | Purpose |
|------|-----------|----------|-------|---------|
| `arduino_loop` | Arduino Core | 1 | 8192 | Arduino loop() emulation |
| `dimmer_task` | DimmerHAL | **10** | 4096 | Zero-crossing synchronization |
| `adc_dma_task` | PowerMeterADC | 8 | 4096 | DMA buffer processing |
| `wifi_task` | WiFiManager | 5 | 4096 | WiFi events, reconnect |
| `httpd_task` | WebServerManager | 5 | 8192 | HTTP/WebSocket server |
| `ntp_task` | NTPManager | 3 | 2048 | NTP synchronization |

**Priorities** (0 - lowest, 24 - highest):
- **Dimmer task (10)**: Highest - zero-crossing timing is critical
- **ADC DMA task (8)**: High - cannot miss DMA events
- **WiFi/HTTP (5)**: Medium - delays are not critical
- **NTP (3)**: Low - synchronization once per hour
- **Arduino loop (1)**: Minimum - not used in this project

---

## 6.9 Error Handling

### Critical Components (system halt)

On initialization error of **critical components**, the system halts:

```cpp
if (!dimmer.begin(DimmerCurve::RMS)) {
    ESP_LOGE(TAG, "Failed to initialize DimmerHAL!");
    ESP_LOGE(TAG, "System halted.");
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**Critical components**:
- DimmerHAL
- RouterController
- PowerMeterADC

**Halt reasons**:
- No zero-crossing signal (220V mains not connected)
- ADC conflict (another component uses ADC1)
- GPIO conflict

### Non-Critical Components (continue operation)

On error of **non-critical components**, the system continues:

```cpp
if (!config.begin()) {
    ESP_LOGE(TAG, "Failed to initialize ConfigManager!");
    ESP_LOGW(TAG, "Using default values");
    // Continue with defaults
}
```

**Non-critical components**:
- ConfigManager (defaults)
- WiFiManager (AP-only mode)
- WebServerManager (Serial control)
- NTPManager (no time)

---

## 6.10 Customization Examples

### Example 1: Logging Measurements to Serial

Add data output to RMS callback:

```cpp
powerMeter.setResultsCallback([](const PowerMeterADC::Measurements& m,
                                  void* user_data) {
    static uint32_t callback_count = 0;
    callback_count++;

    // Update RouterController
    RouterController& router = RouterController::getInstance();
    router.update(m);

    // Log every 5 callbacks (1 second)
    if (callback_count % 5 == 0) {
        ESP_LOGI(TAG, "Voltage: %.1f V", m.voltage_rms);
        ESP_LOGI(TAG, "Power Grid: %.0f W",
                 m.power_active[PowerMeterADC::CURRENT_GRID]);
        ESP_LOGI(TAG, "Power Solar: %.0f W",
                 m.power_active[PowerMeterADC::CURRENT_SOLAR]);

        const RouterStatus& status = router.getStatus();
        ESP_LOGI(TAG, "Dimmer: %d%%", status.dimmer_percent);
    }
}, nullptr);
```

---

### Example 2: Sending Data via WebSocket

Add real-time data transmission:

```cpp
powerMeter.setResultsCallback([](const PowerMeterADC::Measurements& m,
                                  void* user_data) {
    RouterController& router = RouterController::getInstance();
    router.update(m);

    // Every callback (200 ms) send to WebSocket
    WebServerManager& ws = WebServerManager::getInstance();

    StaticJsonDocument<256> doc;
    doc["voltage"] = m.voltage_rms;
    doc["power_grid"] = m.power_active[PowerMeterADC::CURRENT_GRID];
    doc["power_solar"] = m.power_active[PowerMeterADC::CURRENT_SOLAR];
    doc["dimmer"] = router.getStatus().dimmer_percent;

    String json;
    serializeJson(doc, json);
    ws.broadcastWebSocket(json);

}, nullptr);
```

---

### Example 3: Writing to SD Card

Data logging to SD card for analysis:

```cpp
#include "SD.h"
#include "SPI.h"

// In setup (after initialization):
if (!SD.begin(5)) {  // CS pin = GPIO5
    ESP_LOGE(TAG, "SD card mount failed!");
} else {
    ESP_LOGI(TAG, "SD card mounted");
}

// In RMS callback:
powerMeter.setResultsCallback([](const PowerMeterADC::Measurements& m,
                                  void* user_data) {
    static uint32_t count = 0;
    count++;

    RouterController& router = RouterController::getInstance();
    router.update(m);

    // Write every 5 seconds (25 callbacks)
    if (count % 25 == 0) {
        File dataFile = SD.open("/datalog.csv", FILE_APPEND);
        if (dataFile) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%lu,%.1f,%.0f,%.0f,%d\n",
                     millis(),
                     m.voltage_rms,
                     m.power_active[PowerMeterADC::CURRENT_GRID],
                     m.power_active[PowerMeterADC::CURRENT_SOLAR],
                     router.getStatus().dimmer_percent);
            dataFile.print(buf);
            dataFile.close();
        }
    }
}, nullptr);
```

---

### Example 4: Custom Serial Command

Add your own command to SerialCommand:

```cpp
// After serialCmd.begin():
serialCmd.registerCommand("test", [](const char* args) {
    ESP_LOGI(TAG, "Test command executed with args: %s", args);
    Serial.println("OK");
});

// Now you can call:
// test hello world
```

---

## 6.11 Debugging Checklist

When experiencing startup problems, check:

- [ ] **Serial output**: Is USB-UART connected, correct speed (115200)?
- [ ] **220V power**: Is power connected for zero-crossing detector?
- [ ] **GPIO conflicts**: Are pins used by other components?
- [ ] **NVS partition**: Is there an NVS partition in the partition table?
- [ ] **Flash size**: Is 4MB flash sufficient for the application?
- [ ] **ADC channels**: Are sensors connected correctly to GPIO 32-39?
- [ ] **WiFi credentials**: Are they saved via Serial command?
- [ ] **Mains frequency**: Is 50/60 Hz detected correctly?
- [ ] **Dropped frames**: Is `Dropped=0` in statistics?
- [ ] **Memory**: Is there enough free heap memory?

---

## 6.12 Modification Recommendations

### ✅ Safe Modifications:

1. **Adding logging** to RMS callback
2. **Changing statistics frequency** (from 10 seconds to another)
3. **Adding Serial commands** via `serialCmd.registerCommand()`
4. **Changing WiFi credentials** in code or NVS
5. **Changing NTP server and timezone**

### ⚠️ Caution:

1. **Changing ADC sampling frequency** - may disrupt RMS calculations
2. **Changing initialization order** - consider dependencies
3. **Adding heavy operations to RMS callback** - callback should be fast (< 50 ms)
4. **Changing FreeRTOS task priorities** - may disrupt zero-crossing synchronization

### ❌ Dangerous (may break the system):

1. **Removing critical components** (DimmerHAL, RouterController, PowerMeterADC)
2. **Changing zero-crossing algorithm** in DimmerHAL
3. **Using ADC1 for other purposes** (conflicts with PowerMeterADC)
4. **Blocking operations** in RMS callback (delay, long loops)

---

## 6.13 Summary

The main file `main.cpp` implements:

1. **Sequential initialization** of all components considering dependencies
2. **Callback-driven architecture** for power control (every 200 ms)
3. **Non-blocking main loop** for interface handling (Serial, WiFi, Web)
4. **Graceful degradation** - system operates even with non-critical component errors
5. **Deferred initialization** of NTP when connecting to the network

**Key feature**: Power control happens **inside the RMS callback**, independently of the main loop. The main loop only handles user interface.

---

**Next Section**: [07_COMMANDS.md](07_COMMANDS.md) - complete list of Serial commands for system control.
