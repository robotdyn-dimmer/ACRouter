# 3. Application Structure

## 3.1 Module Overview

ACRouter is built on a modular architecture with a clear component hierarchy.

### Component Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        main.cpp                             │
│                    (Application Entry)                      │
└────────────┬────────────────────────────────────────────────┘
             │
             ├─────────────────────────────────────────────────┐
             │                                                 │
     ┌───────▼──────┐                                ┌─────────▼─────────┐
     │   acrouter   │                                │      comm         │
     │     _hal     │◄───────────────────────────────│  (Communication)  │
     │  (Hardware)  │                                └─────────┬─────────┘
     └───────┬──────┘                                          │
             │                                                 │
             │  ┌──────────────────┐                           │
             ├──►  RouterController│                           │
             │  └──────────────────┘                           │
             │                                                 │
             │  ┌──────────────────┐                           │
             ├──►   DimmerHAL      │                           │
             │  └──────────────────┘                           │
             │                                                 │
             │  ┌──────────────────┐                           │
             └──►  PowerMeterADC   │                           │
                └──────────────────┘                           │
                                                               │
     ┌──────────────────┐                             ┌────────▼────────┐
     │      utils       │◄────────────────────────────│  WiFiManager    │
     │  (Utilities)     │                             ├─────────────────┤
     └───────┬──────────┘                             │ WebServerManager│
             │                                        ├─────────────────┤
             ├──► ConfigManager                       │   NTPManager    │
             │                                        ├─────────────────┤
             ├──► HardwareConfigManager               │   OTAManager    │
             │                                        └─────────────────┘
             └──► SerialCommand

     ┌──────────────────┐
     │    rbdimmer      │
     │  (External lib)  │
     └──────────────────┘

     ┌──────────────────┐
     │     sensors      │
     │  (Sensor Types)  │
     └──────────────────┘
```

---

## 3.2 acrouter_hal Module (Hardware Abstraction Layer)

**Path:** `components/acrouter_hal/`

**Purpose:** Hardware abstraction layer for working with sensors, dimmer, and control algorithms.

### Components

#### 3.2.1 RouterController

**Files:**

- [include/RouterController.h](../components/acrouter_hal/include/RouterController.h)
- [src/RouterController.cpp](../components/acrouter_hal/src/RouterController.cpp)

**Description:**

Main Solar Router controller. Implements control algorithms for all 6 operating modes (OFF, AUTO, ECO, OFFGRID, MANUAL, BOOST).

**Functions:**

- Proportional controller for balancing P_grid → 0
- Dimmer control depending on mode
- Callback-driven architecture (called every 200 ms from PowerMeterADC)
- Dimmer level limiting (0-100%)
- State detection (IDLE, INCREASING, DECREASING, AT_MAXIMUM, AT_MINIMUM)

**Main Parameters:**

```cpp
namespace RouterConfig {
    constexpr float DEFAULT_CONTROL_GAIN = 200.0f;      // Kp coefficient
    constexpr float DEFAULT_BALANCE_THRESHOLD = 10.0f;  // Balance threshold (W)
    constexpr uint8_t MIN_DIMMER_PERCENT = 0;
    constexpr uint8_t MAX_DIMMER_PERCENT = 100;
    constexpr uint32_t UPDATE_INTERVAL_MS = 200;        // Update frequency
}
```

**Operating Modes:**

```cpp
enum class RouterMode : uint8_t {
    OFF = 0,        // Dimmer disabled
    AUTO,           // Solar Router (P_grid → 0)
    ECO,            // Export prevention
    OFFGRID,        // Off-grid mode
    MANUAL,         // Fixed level
    BOOST           // Forced 100%
};
```

**Public Methods:**

```cpp
// Initialization
bool begin(DimmerHAL* dimmer);

// Mode control
void setMode(RouterMode mode);
RouterMode getMode() const;

// Control parameters
void setControlGain(float gain);              // Set Kp
void setBalanceThreshold(float threshold);     // Set balance threshold
void setManualLevel(uint8_t percent);          // For MANUAL mode

// Main loop (called from PowerMeterADC callback)
void updateControl(const PowerData& data);

// Emergency stop
void emergencyStop();

// Status information
RouterStatus getStatus() const;
```

---

#### 3.2.2 PowerMeterADC

**Files:**

- [include/PowerMeterADC.h](../components/acrouter_hal/include/PowerMeterADC.h)
- [src/PowerMeterADC.cpp](../components/acrouter_hal/src/PowerMeterADC.cpp)

**Description:**

High-performance power meter using DMA ADC in continuous mode.

**Architecture:**

```
ADC DMA (80 kHz) → ISR callback (10 ms) → Processing Task → RMS calc (200 ms) → User Callback
```

**Specifications:**

- **Sampling frequency:** 10 kHz per channel (80 kHz total for 8 channels)
- **RMS period:** 200 ms (10 AC periods at 50 Hz)
- **Resolution:** 12-bit ADC (0-4095)
- **Range:** 0-3.3V (ADC_ATTEN_DB_12)
- **Channels:** Up to 4 channels (Voltage + 3× Current)

**DMA Configuration:**

```cpp
namespace PowerMeterConfig {
    constexpr uint32_t SAMPLING_FREQ_HZ = 20000;       // 20 kHz on ADC1, 5 kHz per channel for 4 channels
    constexpr uint8_t MAX_CHANNELS = 4;
    constexpr uint32_t FRAME_TIME_MS = 10;             // DMA callback every 10 ms
    constexpr uint32_t SAMPLES_PER_FRAME = 200;        // 200 samples/channel/frame
    constexpr uint16_t RMS_FRAMES_COUNT = 20;          // 20 frames = 200 ms
    constexpr uint32_t RMS_UPDATE_INTERVAL_MS = 200;   // RMS update
}
```

**Supported Sensors:**

```cpp
enum class SensorType : uint8_t {
    NONE = 0,           // Channel not used
    VOLTAGE_AC,         // ZMPT107 (voltage)
    CURRENT_LOAD,       // ACS-712 (load current, dimmer current sensor)
    CURRENT_GRID,       // SCT-013 (grid current)
    CURRENT_SOLAR,      // SCT-013 (solar panel current)
    CURRENT_AUX1,       // Additional channel 1
    CURRENT_AUX2        // Additional channel 2
};
```

**Public Methods:**

```cpp
// Initialization
bool begin();
void setCallback(std::function<void(const PowerData&)> callback);

// Control
bool start();
void stop();
bool isRunning() const;

// Calibration
void setVoltageCalibration(float multiplier, float offset = 0.0f);
void setCurrentCalibration(uint8_t channel, float multiplier, float offset = 0.0f);

// Data retrieval
PowerData getPowerData() const;
float getVoltageRMS() const;
float getCurrentRMS(CurrentChannel channel) const;
float getPower(CurrentChannel channel) const;
```

**Callback Data Structure:**

```cpp
struct PowerData {
    float voltage_rms;              // RMS voltage (V)
    float current_rms[3];           // RMS currents [LOAD, GRID, SOLAR] (A)
    float power[3];                 // Power [LOAD, GRID, SOLAR] (W)
    float power_dimmer;             // Dimmer power (W)
    uint32_t timestamp_ms;          // Timestamp
    bool valid;                     // Data is valid
};
```

---

#### 3.2.3 DimmerHAL

**Files:**

- [include/DimmerHAL.h](../components/acrouter_hal/include/DimmerHAL.h)
- [src/DimmerHAL.cpp](../components/acrouter_hal/src/DimmerHAL.cpp)

**Description:**

HAL for controlling AC dimmer with TRIAC and zero-crossing detector.

**Features:**

- Dual-channel dimmer (2 independent loads)
- Zero-crossing detection for AC waveform synchronization
- Automatic mains frequency detection (50/60 Hz)
- RMS compensated power curve
- Smooth transitions

**Hardware Configuration:**

```cpp
namespace DimmerConfig {
    constexpr uint8_t PHASE_NUM = 0;                    // Single phase
    constexpr uint16_t MAINS_FREQUENCY = 0;             // 0 = auto-detect
    constexpr uint8_t MAX_CHANNELS = 2;                 // 2 channels
    constexpr uint32_t DEFAULT_TRANSITION_MS = 500;     // Transition time
}
```

**GPIO Pins (default):**

- **Zero-Cross:** GPIO 18
- **Dimmer Ch1:** GPIO 19
- **Dimmer Ch2:** GPIO 23

**Public Methods:**

```cpp
// Initialization
bool begin();

// Channel control
void setPower(DimmerChannel channel, uint8_t percent);
void setPowerSmooth(DimmerChannel channel, uint8_t percent, uint32_t transition_ms);
uint8_t getPower(DimmerChannel channel) const;

// Quick turn off
void turnOff(DimmerChannel channel);
void turnOffAll();

// Power curve
void setCurve(DimmerChannel channel, DimmerCurve curve);

// Information
DimmerStatus getStatus(DimmerChannel channel) const;
bool isInitialized() const;
```

**Curve Types:**

```cpp
enum class DimmerCurve : uint8_t {
    LINEAR,         // Linear
    RMS,            // RMS-compensated (recommended for heating elements)
    LOGARITHMIC     // Logarithmic (for LED)
};
```

---

#### 3.2.4 PinDefinitions.h

**File:** [include/PinDefinitions.h](../components/acrouter_hal/include/PinDefinitions.h)

**Description:**

Centralized GPIO pin definitions for the entire project.

**Default Pins:**

```cpp
// ADC pins (ADC1 only!)
constexpr uint8_t ADC_VOLTAGE_PIN = 35;      // ZMPT107 voltage sensor
constexpr uint8_t ADC_CURRENT_LOAD_PIN = 39; // SCT-013 load current
constexpr uint8_t ADC_CURRENT_GRID_PIN = 36; // SCT-013 grid current
constexpr uint8_t ADC_CURRENT_SOLAR_PIN = 34;// SCT-013 solar current

// Dimmer pins
constexpr uint8_t DIMMER_ZEROCROSS_PIN = 18; // Zero-cross detector
constexpr uint8_t DIMMER_CH1_PIN = 19;       // Dimmer channel 1
constexpr uint8_t DIMMER_CH2_PIN = 23;       // Dimmer channel 2 (Phase 2)

// LED indicators
constexpr uint8_t LED_STATUS_PIN = 17;       // Status LED
constexpr uint8_t LED_LOAD_PIN = 5;          // Load indicator LED

// Relays (Phase 2)
constexpr uint8_t RELAY_CH1_PIN = 15;        // Relay 1
constexpr uint8_t RELAY_CH2_PIN = 2;         // Relay 2
```

**Note:** Pins can be reassigned via HardwareConfigManager and web interface.

---

## 3.3 utils Module (Utilities)

**Path:** `components/utils/`

**Purpose:** Helper classes for configuration, commands, and data types.

### Components

#### 3.3.1 ConfigManager

**Files:**

- [include/ConfigManager.h](../components/utils/include/ConfigManager.h)
- [src/ConfigManager.cpp](../components/utils/src/ConfigManager.cpp)

**Description:**

System configuration manager with automatic saving to NVS.

**NVS namespace:** `"acrouter"`

**Stored Parameters:**

```cpp
struct SystemConfig {
    // Router parameters
    uint8_t router_mode;        // RouterMode (0-5)
    float control_gain;         // Kp coefficient (10-1000)
    float balance_threshold;    // Balance threshold (W)
    uint8_t manual_level;       // Level for MANUAL mode (0-100%)

    // Sensor calibration
    float voltage_coef;         // Voltage coefficient
    float current_coef;         // Current coefficient (A/V)
    float current_threshold;    // Minimum current (A)
    float power_threshold;      // Minimum power (W)
};
```

**NVS Keys:**

```cpp
namespace ConfigKeys {
    constexpr const char* ROUTER_MODE       = "router_mode";
    constexpr const char* CONTROL_GAIN      = "ctrl_gain";
    constexpr const char* BALANCE_THRESHOLD = "bal_thresh";
    constexpr const char* MANUAL_LEVEL      = "manual_lvl";
    constexpr const char* VOLTAGE_COEF      = "volt_coef";
    constexpr const char* CURRENT_COEF      = "curr_coef";
    // ... etc.
}
```

**Public Methods:**

```cpp
// Initialization
bool begin();

// Get configuration
const SystemConfig& getConfig() const;

// Set parameters (automatically saved to NVS)
bool setRouterMode(uint8_t mode);
bool setControlGain(float gain);
bool setBalanceThreshold(float threshold);
bool setManualLevel(uint8_t level);
bool setVoltageCoefficient(float coef);
bool setCurrentCoefficient(float coef);

// Bulk operations
bool saveAll();
bool loadAll();
bool resetToDefaults();
```

**Default Values:**

```cpp
namespace ConfigDefaults {
    constexpr uint8_t ROUTER_MODE = 1;          // AUTO
    constexpr float CONTROL_GAIN = 200.0f;
    constexpr float BALANCE_THRESHOLD = 10.0f;
    constexpr uint8_t MANUAL_LEVEL = 0;
    constexpr float VOLTAGE_COEF = 230.0f;
    constexpr float CURRENT_COEF = 50.0f;       // SCT-013 50A/1V
}
```

---

#### 3.3.2 HardwareConfigManager

**Files:**

- [include/HardwareConfigManager.h](../components/utils/include/HardwareConfigManager.h)
- [src/HardwareConfigManager.cpp](../components/utils/src/HardwareConfigManager.cpp)

**Description:**

Hardware configuration manager for GPIO pins and sensor types. Allows complete device configuration without recompilation.

**NVS namespace:** `"hw_config"`

**Configurable Components:**

```cpp
struct HardwareConfig {
    ADCChannelConfig adc_channels[4];       // 4 ADC channels
    DimmerChannelConfig dimmer_ch1;         // Dimmer 1
    DimmerChannelConfig dimmer_ch2;         // Dimmer 2
    uint8_t zerocross_gpio;                 // Zero-cross pin
    bool zerocross_enabled;
    RelayChannelConfig relay_ch1;           // Relay 1
    RelayChannelConfig relay_ch2;           // Relay 2
    uint8_t led_status_gpio;                // Status LED
    uint8_t led_load_gpio;                  // Load LED
};
```

**ADC Channel:**

```cpp
struct ADCChannelConfig {
    uint8_t gpio;               // GPIO pin (ADC1 only: 32,33,34,35,36,39)
    SensorType type;            // Sensor type
    float multiplier;           // Calibration multiplier
    float offset;               // Calibration offset
    bool enabled;               // Channel enabled
};
```

**Public Methods:**

```cpp
// Initialization
bool begin();

// ADC channel configuration
bool setADCChannel(uint8_t channel, const ADCChannelConfig& config);
ADCChannelConfig getADCChannel(uint8_t channel) const;

// Dimmer configuration
bool setDimmerChannel(uint8_t channel, const DimmerChannelConfig& config);

// Relay configuration
bool setRelayChannel(uint8_t channel, const RelayChannelConfig& config);

// Zero-cross
bool setZeroCross(uint8_t gpio, bool enabled = true);

// LED
bool setStatusLED(uint8_t gpio);
bool setLoadLED(uint8_t gpio);

// Validation
bool validate(String* error_msg = nullptr) const;

// Bulk operations
bool saveAll();
bool loadAll();
bool resetToDefaults();
void printConfig() const;
```

**Validation:**

- ✅ GPIO conflict check (one pin = one function)
- ✅ ADC pin validity check (ADC1 only)
- ✅ Input-only pin check (34, 35, 36, 39 cannot be outputs)
- ✅ Sensor type validation

**Factory Settings:**

```cpp
// ADC channels
ADC0: GPIO35, VOLTAGE_AC,    mult=230.0, offset=0.0, ENABLED
ADC1: GPIO39, CURRENT_LOAD,  mult=30.0,  offset=0.0, ENABLED
ADC2: GPIO36, CURRENT_GRID,  mult=30.0,  offset=0.0, ENABLED
ADC3: GPIO34, CURRENT_SOLAR, mult=30.0,  offset=0.0, ENABLED

// Dimmer
Ch1: GPIO19, ENABLED
Ch2: GPIO23, DISABLED

// Zero-Cross
GPIO18, ENABLED
```

---

#### 3.3.3 SerialCommand

**Files:**

- [include/SerialCommand.h](../components/utils/include/SerialCommand.h)
- [src/SerialCommand.cpp](../components/utils/src/SerialCommand.cpp)

**Description:**

Serial command handler for configuration and monitoring via terminal.

**Main Commands:**

```bash
# Information
help                    # Show all commands
status                  # Current state
metrics                 # Power metrics
config-show             # Show configuration
hw-config-show          # Show hardware configuration

# Mode control
set-mode <0-5>          # Set mode (0=OFF, 1=AUTO, ...)
set-manual <0-100>      # Set level for MANUAL

# Parameters
set-kp <float>          # Set Kp coefficient
set-threshold <float>   # Set balance threshold

# Calibration
calibrate-voltage <mult> [offset]
calibrate-current <ch> <mult> [offset]

# Configuration
config-save             # Save configuration
config-reset            # Reset to factory defaults
hw-config-reset         # Reset hardware configuration

# System
reboot                  # Reboot
factory-reset           # Full reset (all settings)
```

**Public Methods:**

```cpp
// Initialization
void begin();

// Main loop (call from loop())
void update();

// Command registration
void registerCommand(const char* cmd, CommandHandler handler);
```

---

#### 3.3.4 DataTypes.h

**File:** [include/DataTypes.h](../components/utils/include/DataTypes.h)

**Description:**

Common data types used throughout the project.

**Main Structures:**

```cpp
// Power data
struct PowerData {
    float voltage_rms;
    float current_rms[3];       // [LOAD, GRID, SOLAR]
    float power[3];             // [LOAD, GRID, SOLAR]
    float power_dimmer;
    uint32_t timestamp_ms;
    bool valid;
};

// System metrics
struct SystemMetrics {
    PowerData power;
    uint8_t dimmer_percent;
    RouterMode mode;
    RouterState state;
    uint32_t uptime_ms;
    uint32_t free_heap;
};
```

---

## 3.4 comm Module (Communication)

**Path:** `components/comm/`

**Purpose:** Network communication, web server, REST API, WiFi management.

### Components

#### 3.4.1 WiFiManager

**Files:**

- [include/WiFiManager.h](../components/comm/include/WiFiManager.h)
- [src/WiFiManager.cpp](../components/comm/src/WiFiManager.cpp)

**Description:**

WiFi management with simultaneous AP+STA support (ESP32 dual mode).

**Operating Modes:**

```cpp
enum class WiFiState : uint8_t {
    IDLE,               // Not initialized
    AP_ONLY,            // AP only (no STA credentials)
    STA_CONNECTING,     // Connecting to STA
    STA_CONNECTED,      // Connected to STA
    AP_STA,             // Both modes active
    STA_FAILED          // STA failed, AP active
};
```

**Default Configuration:**

```cpp
AP SSID: "ACRouter_XXXXXX"  // XXXXXX = last 6 digits of MAC
AP Password: "12345678"
AP IP: 192.168.4.1
STA Timeout: 15 seconds
```

**Public Methods:**

```cpp
// Initialization
bool begin();

// AP mode
bool startAP(const char* ssid = nullptr, const char* password = nullptr);
void stopAP();
bool isAPActive() const;

// STA mode
bool connectSTA(const char* ssid, const char* password);
void disconnectSTA();
bool isSTAConnected() const;

// WiFi scan
std::vector<WiFiNetwork> scanNetworks();

// Status
WiFiStatus getStatus() const;
```

---

#### 3.4.2 WebServerManager

**Files:**

- [include/WebServerManager.h](../components/comm/include/WebServerManager.h)
- [src/WebServerManager.cpp](../components/comm/src/WebServerManager.cpp)

**Description:**

HTTP web server with REST API and Material UI interface.

**Endpoints:**

**Web Pages:**

- `GET /` - Dashboard (main page)
- `GET /wifi` - WiFi configuration page
- `GET /settings/hardware` - Hardware config page

**REST API - Status and Metrics:**

- `GET /api/status` - System status
- `GET /api/metrics` - Power metrics
- `GET /api/config` - Router configuration

**REST API - Control:**

- `POST /api/mode` - Set mode
- `POST /api/manual` - Set manual level
- `POST /api/calibrate` - Sensor calibration

**REST API - WiFi:**

- `GET /api/wifi/status` - WiFi status
- `GET /api/wifi/scan` - Network scanning
- `POST /api/wifi/connect` - Connect to network
- `POST /api/wifi/disconnect` - Disconnect
- `POST /api/wifi/forget` - Forget network

**REST API - Hardware Configuration:**

- `GET /api/hardware/config` - Get configuration
- `POST /api/hardware/config` - Save configuration
- `POST /api/hardware/validate` - Configuration validation

**REST API - System:**

- `POST /api/system/reboot` - Reboot

**Public Methods:**

```cpp
// Initialization
bool begin(uint16_t http_port = 80);

// Control
void start();
void stop();
bool isRunning() const;

// Processing (call from loop())
void handleClient();
```

**Material UI Interface:**

- Separate CSS styles ([web/styles/MaterialStyles.h](../components/comm/web/styles/MaterialStyles.h))
- Reusable Layout ([web/components/Layout.h](../components/comm/web/components/Layout.h))
- Individual pages ([web/pages/](../components/comm/web/pages/))

---

#### 3.4.3 NTPManager

**Files:**

- [include/NTPManager.h](../components/comm/include/NTPManager.h)
- [src/NTPManager.cpp](../components/comm/src/NTPManager.cpp)

**Description:**

Time synchronization manager via NTP (for future features: SCHEDULE mode, logging).

**Features:**

- Automatic synchronization with NTP servers
- Time zone support
- Periodic time updates

**Status:** Phase 2 (not critical for current version)

---

#### 3.4.4 OTAManager

**Files:**

- [include/OTAManager.h](../components/comm/include/OTAManager.h)
- [src/OTAManager.cpp](../components/comm/src/OTAManager.cpp)

**Description:**

OTA (Over-The-Air) firmware update manager.

**Features:**

- Update via WiFi
- Integrity verification (MD5)
- Safe rollback on errors
- Uses app0/app1 partitions

**Status:** Phase 2 (infrastructure ready in partition table)

---

## 3.5 rbdimmer Module (External Library)

**Path:** `components/rbdimmer/`

**Description:**

External library for controlling AC TRIAC dimmer with zero-crossing detector.

**Main Features:**

- Phase-angle TRIAC control
- Zero-crossing synchronization
- Multiple power curve support (LINEAR, RMS, LOGARITHMIC)
- Automatic mains frequency detection (50/60 Hz)

**Note:** DimmerHAL is a wrapper over rbdimmer for ease of use.

---

## 3.6 sensors Module (Sensor Types)

**Path:** `components/sensors/`

**File:** [include/SensorTypes.h](../components/sensors/include/SensorTypes.h)

**Description:**

Sensor type definitions and ADC channel configurations.

**Sensor Types:**

```cpp
enum class SensorType : uint8_t {
    NONE = 0,           // Channel not used
    VOLTAGE_AC,         // ZMPT107 (AC voltage)
    CURRENT_LOAD,       // SCT-013 (load current)
    CURRENT_GRID,       // SCT-013 (grid current, import/export)
    CURRENT_SOLAR,      // SCT-013 (solar panel current)
    CURRENT_AUX1,       // Additional sensor 1
    CURRENT_AUX2        // Additional sensor 2
};
```

**Channel Configuration:**

```cpp
struct ADCChannelConfig {
    uint8_t gpio;               // GPIO pin
    SensorType type;            // Sensor type
    float multiplier;           // Calibration multiplier
    float offset;               // Offset
    bool enabled;               // Channel active
};
```

---

## 3.7 Module Dependencies

### Dependency Graph

```
main.cpp
  │
  ├─► acrouter_hal
  │     ├─► RouterController
  │     │     └─► DimmerHAL → rbdimmer
  │     ├─► DimmerHAL → rbdimmer
  │     └─► PowerMeterADC → sensors
  │
  ├─► comm
  │     ├─► WiFiManager
  │     ├─► WebServerManager
  │     ├─► NTPManager
  │     └─► OTAManager
  │
  └─► utils
        ├─► ConfigManager
        ├─► HardwareConfigManager → sensors
        ├─► SerialCommand
        └─► DataTypes
```

### Dependency Table

| Module | Depends On | Used In |
|--------|------------|---------|
| **RouterController** | DimmerHAL, PowerMeterADC | main.cpp |
| **DimmerHAL** | rbdimmer, PinDefinitions | RouterController |
| **PowerMeterADC** | SensorTypes, DataTypes | RouterController, WebServerManager |
| **ConfigManager** | NVS | main.cpp, WebServerManager |
| **HardwareConfigManager** | SensorTypes, NVS | main.cpp, WebServerManager |
| **WiFiManager** | ESP32 WiFi | main.cpp, WebServerManager |
| **WebServerManager** | WiFiManager, all | main.cpp |
| **SerialCommand** | ConfigManager, RouterController | main.cpp |

---

## 3.8 Module Settings

### ConfigManager (NVS namespace: "acrouter")

| Parameter | NVS Key | Type | Default | Range |
|-----------|---------|------|---------|-------|
| Router mode | `router_mode` | uint8_t | 1 (AUTO) | 0-5 |
| Kp coefficient | `ctrl_gain` | float | 200.0 | 10.0-1000.0 |
| Balance threshold | `bal_thresh` | float | 10.0 | 0.0-100.0 |
| Manual level | `manual_lvl` | uint8_t | 0 | 0-100 |
| Voltage coef. | `volt_coef` | float | 230.0 | - |
| Current coef. | `curr_coef` | float | 50.0 | - |

### HardwareConfigManager (NVS namespace: "hw_config")

| Component | NVS Keys | Example |
|-----------|----------|---------|
| ADC0 | `adc0_gpio`, `adc0_type`, `adc0_mult`, `adc0_offset`, `adc0_en` | GPIO35, VOLTAGE_AC, 230.0, 0.0, true |
| ADC1 | `adc1_gpio`, `adc1_type`, `adc1_mult`, `adc1_offset`, `adc1_en` | GPIO39, CURRENT_LOAD, 30.0, 0.0, true |
| Dimmer1 | `dim1_gpio`, `dim1_en` | GPIO19, true |
| Zero-Cross | `zc_gpio`, `zc_en` | GPIO18, true |
| Relay1 | `rel1_gpio`, `rel1_pol`, `rel1_en` | GPIO15, true, false |
| LED Status | `led_st_gpio` | GPIO17 |

---

**Next Section:** [4. RouterController Operating Modes](04_ROUTER_MODES.md)
