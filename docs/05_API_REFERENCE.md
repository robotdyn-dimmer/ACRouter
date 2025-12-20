# 5. Module APIs and Data Structures

## 5.1 RouterController API

**Files:**
- [components/acrouter_hal/include/RouterController.h](../components/acrouter_hal/include/RouterController.h)
- [components/acrouter_hal/src/RouterController.cpp](../components/acrouter_hal/src/RouterController.cpp)

**Design pattern:** Singleton

### Get Instance

```cpp
RouterController& router = RouterController::getInstance();
```

### Initialization

#### begin()

```cpp
bool begin(DimmerHAL* dimmer, DimmerChannel channel = DimmerChannel::CHANNEL_1);
```

**Description:** Initialize controller with dimmer binding.

**Parameters:**
- `dimmer` - pointer to initialized DimmerHAL
- `channel` - dimmer channel (CHANNEL_1 or CHANNEL_2)

**Returns:** `true` if successful, `false` on error

**Example:**

```cpp
DimmerHAL& dimmer = DimmerHAL::getInstance();
RouterController& router = RouterController::getInstance();

dimmer.begin();
if (router.begin(&dimmer, DimmerChannel::CHANNEL_1)) {
    Serial.println("RouterController initialized");
} else {
    Serial.println("RouterController init failed");
}
```

---

### Mode Management

#### setMode()

```cpp
void setMode(RouterMode mode);
```

**Description:** Set controller operating mode.

**Parameters:**
- `mode` - one of 6 modes (OFF=0, AUTO=1, ECO=2, OFFGRID=3, MANUAL=4, BOOST=5)

**Example:**

```cpp
router.setMode(RouterMode::AUTO);    // Automatic mode
router.setMode(RouterMode::MANUAL);  // Manual mode
```

#### getMode()

```cpp
RouterMode getMode() const;
```

**Description:** Get current operating mode.

**Returns:** Current RouterMode

**Example:**

```cpp
RouterMode current = router.getMode();
if (current == RouterMode::AUTO) {
    Serial.println("Running in AUTO mode");
}
```

---

### Control Parameters

#### setControlGain()

```cpp
void setControlGain(float gain);
```

**Description:** Set proportional controller coefficient (Kp).

**Parameters:**
- `gain` - gain coefficient (10.0 - 1000.0)

**Effect:**
- Lower Kp � faster response, possible oscillations
- Higher Kp � slower response, more stable

**Example:**

```cpp
router.setControlGain(200.0f);  // Standard value
router.setControlGain(100.0f);  // Faster response
router.setControlGain(400.0f);  // Slower, more stable
```

#### getControlGain()

```cpp
float getControlGain() const;
```

**Returns:** Current Kp value

#### setBalanceThreshold()

```cpp
void setBalanceThreshold(float threshold);
```

**Description:** Set balance threshold in Watts.

**Parameters:**
- `threshold` - threshold in W (typically 5.0 - 50.0)

**Effect:**
- If `|P_grid| < threshold`, system is considered balanced
- Lower threshold � more accurate balance, more switching
- Higher threshold � less switching, less accurate balance

**Example:**

```cpp
router.setBalanceThreshold(10.0f);  // �10 W considered balanced
router.setBalanceThreshold(20.0f);  // �20 W for larger loads
```

#### getBalanceThreshold()

```cpp
float getBalanceThreshold() const;
```

**Returns:** Current balance threshold (W)

---

### Manual Control (MANUAL mode)

#### setManualLevel()

```cpp
void setManualLevel(uint8_t percent);
```

**Description:** Set fixed dimmer level for MANUAL mode.

**Parameters:**
- `percent` - dimmer level 0-100%

**Example:**

```cpp
router.setMode(RouterMode::MANUAL);
router.setManualLevel(75);  // Set to 75%
```

#### getManualLevel()

```cpp
uint8_t getManualLevel() const;
```

**Returns:** Set level for MANUAL mode

---

### Main Update Loop

#### update()

```cpp
void update(const PowerMeterADC::Measurements& measurements);
```

**Description:** Main control function. Called automatically from PowerMeterADC callback every 200 ms.

**Parameters:**
- `measurements` - structure with power measurements

**Note:** Usually called automatically, manual calling not required.

**Internal logic:**

```cpp
void update(const Measurements& meas) {
    switch (mode) {
        case OFF:     processOffMode();           break;
        case AUTO:    processAutoMode(P_grid);    break;
        case ECO:     processEcoMode(P_grid);     break;
        case OFFGRID: processOffgridMode(meas);   break;
        case MANUAL:  processManualMode();        break;
        case BOOST:   processBoostMode();         break;
    }
}
```

---

### Emergency Stop

#### emergencyStop()

```cpp
void emergencyStop();
```

**Description:** Immediate dimmer shutdown (0%) and transition to OFF mode.

**When to use:**
- System overheating
- Sensor error
- Critical situation

**Example:**

```cpp
if (temperature > 80.0f) {
    router.emergencyStop();
    Serial.println("EMERGENCY STOP: Overheating!");
}
```

---

### Get Status

#### getStatus()

```cpp
RouterStatus getStatus() const;
```

**Description:** Get complete controller state information.

**Returns:** RouterStatus structure

**Example:**

```cpp
RouterStatus status = router.getStatus();

Serial.printf("Mode: %d\n", static_cast<int>(status.mode));
Serial.printf("State: %d\n", static_cast<int>(status.state));
Serial.printf("Dimmer: %d%%\n", status.dimmer_percent);
Serial.printf("P_grid: %.1f W\n", status.power_grid);
Serial.printf("Kp: %.1f\n", status.control_gain);
```

---

### Data Structures

#### RouterStatus

```cpp
struct RouterStatus {
    RouterMode mode;                    // Current mode
    RouterState state;                  // State (IDLE, INCREASING, etc.)
    uint8_t dimmer_percent;             // Dimmer level (0-100%)
    float target_level;                 // Target level (float for smoothness)
    float power_grid;                   // Current grid power (W)
    float control_gain;                 // Kp coefficient
    float balance_threshold;            // Balance threshold (W)
    uint32_t last_update_ms;            // Last update timestamp
    bool valid;                         // Status is valid
};
```

#### RouterMode

```cpp
enum class RouterMode : uint8_t {
    OFF = 0,        // Disabled
    AUTO,           // Automatic Solar Router
    ECO,            // Economic (anti-export)
    OFFGRID,        // Autonomous
    MANUAL,         // Manual
    BOOST           // Forced 100%
};
```

#### RouterState

```cpp
enum class RouterState : uint8_t {
    IDLE,           // No action
    INCREASING,     // Increasing dimmer (exporting)
    DECREASING,     // Decreasing dimmer (importing)
    AT_MAXIMUM,     // At maximum (100%)
    AT_MINIMUM,     // At minimum (0%)
    ERROR           // Error state
};
```

---

## 5.2 PowerMeterADC API

**Files:**
- [components/acrouter_hal/include/PowerMeterADC.h](../components/acrouter_hal/include/PowerMeterADC.h)
- [components/acrouter_hal/src/PowerMeterADC.cpp](../components/acrouter_hal/src/PowerMeterADC.cpp)

**Design pattern:** Singleton

### Get Instance

```cpp
PowerMeterADC& powerMeter = PowerMeterADC::getInstance();
```

### Initialization

#### begin()

```cpp
bool begin();
```

**Description:** Initialize DMA ADC and create FreeRTOS processing task.

**Returns:** `true` if successful

**Example:**

```cpp
PowerMeterADC& powerMeter = PowerMeterADC::getInstance();

if (powerMeter.begin()) {
    Serial.println("PowerMeterADC initialized");
} else {
    Serial.println("PowerMeterADC init failed");
}
```

---

### Measurement Control

#### start()

```cpp
bool start();
```

**Description:** Start continuous DMA ADC measurements.

**Returns:** `true` if successfully started

**Example:**

```cpp
if (powerMeter.start()) {
    Serial.println("Measurements started");
}
```

#### stop()

```cpp
void stop();
```

**Description:** Stop measurements.

**Example:**

```cpp
powerMeter.stop();
Serial.println("Measurements stopped");
```

#### isRunning()

```cpp
bool isRunning() const;
```

**Description:** Check if measurements are running.

**Returns:** `true` if measurements are active

---

### Results Callback

#### setCallback()

```cpp
void setCallback(std::function<void(const Measurements&)> callback);
```

**Description:** Set callback function to be called every 200 ms with RMS results.

**Parameters:**
- `callback` - function of type `void callback(const Measurements& meas)`

**Example:**

```cpp
// Option 1: Lambda
powerMeter.setCallback([](const PowerMeterADC::Measurements& meas) {
    Serial.printf("V: %.1f V, I_grid: %.2f A, P_grid: %.1f W\n",
                  meas.voltage_rms,
                  meas.current_rms[PowerMeterADC::CURRENT_GRID],
                  meas.power_active[PowerMeterADC::CURRENT_GRID]);
});

// Option 2: Global function
void onMeasurements(const PowerMeterADC::Measurements& meas) {
    // Process measurements
}
powerMeter.setCallback(onMeasurements);

// Option 3: Class method
powerMeter.setCallback([this](const auto& meas) {
    this->handleMeasurements(meas);
});
```

---

### Sensor Calibration

#### setVoltageCalibration()

```cpp
void setVoltageCalibration(float multiplier, float offset = 0.0f);
```

**Description:** Set calibration coefficients for voltage sensor.

**Parameters:**
- `multiplier` - multiplier (V/V_adc)
- `offset` - offset (V)

**Formula:** `V_real = V_adc * multiplier + offset`

**Example:**

```cpp
// ZMPT107: 230V � 1V (scale 1:230)
powerMeter.setVoltageCalibration(230.0f, 0.0f);

// With offset correction
powerMeter.setVoltageCalibration(235.0f, -2.5f);
```

#### setCurrentCalibration()

```cpp
void setCurrentCalibration(CurrentChannel channel, float multiplier, float offset = 0.0f);
```

**Description:** Set calibration coefficients for current sensor.

**Parameters:**
- `channel` - channel (CURRENT_LOAD, CURRENT_GRID, CURRENT_SOLAR)
- `multiplier` - multiplier (A/V_adc)
- `offset` - offset (A)

**Formula:** `I_real = I_adc * multiplier + offset`

**Example:**

```cpp
// SCT-013-030: 30A � 1V
powerMeter.setCurrentCalibration(PowerMeterADC::CURRENT_GRID, 30.0f, 0.0f);

// ACS-712-20A: 0A=2.5V, sensitivity 100mV/A
powerMeter.setCurrentCalibration(PowerMeterADC::CURRENT_LOAD, 10.0f, -25.0f);
```

---

### Get Data

#### getPowerData()

```cpp
Measurements getPowerData() const;
```

**Description:** Get latest measurements (thread-safe).

**Returns:** Measurements structure

**Example:**

```cpp
PowerMeterADC::Measurements data = powerMeter.getPowerData();

if (data.valid) {
    Serial.printf("Voltage: %.1f V\n", data.voltage_rms);
    Serial.printf("Current Grid: %.2f A\n", data.current_rms[PowerMeterADC::CURRENT_GRID]);
    Serial.printf("Power Grid: %.1f W\n", data.power_active[PowerMeterADC::CURRENT_GRID]);
}
```

#### getVoltageRMS()

```cpp
float getVoltageRMS() const;
```

**Description:** Get voltage RMS only.

**Returns:** Voltage in Volts

#### getCurrentRMS()

```cpp
float getCurrentRMS(CurrentChannel channel) const;
```

**Description:** Get RMS current for specified channel.

**Parameters:**
- `channel` - CURRENT_LOAD, CURRENT_GRID or CURRENT_SOLAR

**Returns:** Current in Amperes

**Example:**

```cpp
float i_grid = powerMeter.getCurrentRMS(PowerMeterADC::CURRENT_GRID);
float i_solar = powerMeter.getCurrentRMS(PowerMeterADC::CURRENT_SOLAR);

Serial.printf("Grid: %.2f A, Solar: %.2f A\n", i_grid, i_solar);
```

#### getPower()

```cpp
float getPower(CurrentChannel channel) const;
```

**Description:** Get active power for specified channel.

**Parameters:**
- `channel` - CURRENT_LOAD, CURRENT_GRID or CURRENT_SOLAR

**Returns:** Power in Watts

**Example:**

```cpp
float p_grid = powerMeter.getPower(PowerMeterADC::CURRENT_GRID);

if (p_grid < 0) {
    Serial.println("Exporting to grid");
} else {
    Serial.println("Importing from grid");
}
```

---

### Data Structures

#### Measurements

```cpp
struct Measurements {
    float voltage_rms;                  // RMS voltage (V)
    float current_rms[3];               // RMS currents [LOAD, GRID, SOLAR] (A)
    float power_active[3];              // Active power [LOAD, GRID, SOLAR] (W)
    float power_reactive[3];            // Reactive power (VAR) - Phase 2
    float power_apparent[3];            // Apparent power (VA) - Phase 2
    float power_factor[3];              // Power factor - Phase 2
    uint32_t timestamp_ms;              // Timestamp
    bool valid;                         // Data is valid
};
```

#### CurrentChannel

```cpp
enum CurrentChannel : uint8_t {
    CURRENT_LOAD = 0,    // Load current (dimmer)
    CURRENT_GRID = 1,    // Grid current (import/export)
    CURRENT_SOLAR = 2,   // Solar panel current
    CURRENT_COUNT = 3
};
```

---

## 5.3 DimmerHAL API

**Files:**
- [components/acrouter_hal/include/DimmerHAL.h](../components/acrouter_hal/include/DimmerHAL.h)
- [components/acrouter_hal/src/DimmerHAL.cpp](../components/acrouter_hal/src/DimmerHAL.cpp)

**Design pattern:** Singleton

### Get Instance

```cpp
DimmerHAL& dimmer = DimmerHAL::getInstance();
```

### Initialization

#### begin()

```cpp
bool begin();
```

**Description:** Initialize dimmer with zero-crossing detector.

**Returns:** `true` if successful

**Example:**

```cpp
DimmerHAL& dimmer = DimmerHAL::getInstance();

if (dimmer.begin()) {
    Serial.println("DimmerHAL initialized");
} else {
    Serial.println("DimmerHAL init failed");
}
```

---

### Power Control

#### setPower()

```cpp
void setPower(DimmerChannel channel, uint8_t percent);
```

**Description:** Set dimmer power instantly.

**Parameters:**
- `channel` - CHANNEL_1 or CHANNEL_2
- `percent` - power 0-100%

**Example:**

```cpp
dimmer.setPower(DimmerChannel::CHANNEL_1, 75);  // 75%
dimmer.setPower(DimmerChannel::CHANNEL_2, 50);  // 50% on second channel
```

#### setPowerSmooth()

```cpp
void setPowerSmooth(DimmerChannel channel, uint8_t percent, uint32_t transition_ms);
```

**Description:** Set power with smooth transition.

**Parameters:**
- `channel` - CHANNEL_1 or CHANNEL_2
- `percent` - target power 0-100%
- `transition_ms` - transition time in milliseconds

**Example:**

```cpp
// Smoothly transition to 80% over 2 seconds
dimmer.setPowerSmooth(DimmerChannel::CHANNEL_1, 80, 2000);

// Smooth shutdown over 1 second
dimmer.setPowerSmooth(DimmerChannel::CHANNEL_1, 0, 1000);
```

#### getPower()

```cpp
uint8_t getPower(DimmerChannel channel) const;
```

**Description:** Get current channel power.

**Returns:** Power 0-100%

**Example:**

```cpp
uint8_t current_power = dimmer.getPower(DimmerChannel::CHANNEL_1);
Serial.printf("Dimmer 1: %d%%\n", current_power);
```

---

### Quick Control

#### turnOff()

```cpp
void turnOff(DimmerChannel channel);
```

**Description:** Quickly turn off channel (0%).

**Example:**

```cpp
dimmer.turnOff(DimmerChannel::CHANNEL_1);
```

#### turnOffAll()

```cpp
void turnOffAll();
```

**Description:** Turn off all channels.

**Example:**

```cpp
dimmer.turnOffAll();  // Emergency shutdown
```

---

### Power Curve

#### setCurve()

```cpp
void setCurve(DimmerChannel channel, DimmerCurve curve);
```

**Description:** Set power curve type.

**Parameters:**
- `channel` - CHANNEL_1 or CHANNEL_2
- `curve` - LINEAR, RMS or LOGARITHMIC

**Recommendations:**
- **RMS** - for resistive loads (heaters, water heaters) - recommended
- **LINEAR** - linear dependency
- **LOGARITHMIC** - for LED lamps

**Example:**

```cpp
// RMS curve for heater (recommended)
dimmer.setCurve(DimmerChannel::CHANNEL_1, DimmerCurve::RMS);

// Linear curve
dimmer.setCurve(DimmerChannel::CHANNEL_1, DimmerCurve::LINEAR);
```

---

### Status and Information

#### getStatus()

```cpp
DimmerStatus getStatus(DimmerChannel channel) const;
```

**Description:** Get detailed channel status.

**Returns:** DimmerStatus structure

**Example:**

```cpp
DimmerStatus status = dimmer.getStatus(DimmerChannel::CHANNEL_1);

Serial.printf("Initialized: %s\n", status.initialized ? "Yes" : "No");
Serial.printf("Active: %s\n", status.active ? "Yes" : "No");
Serial.printf("Power: %d%%\n", status.power_percent);
Serial.printf("Target: %d%%\n", status.target_percent);
```

#### isInitialized()

```cpp
bool isInitialized() const;
```

**Description:** Check if dimmer is initialized.

**Returns:** `true` if initialized

---

### Data Structures

#### DimmerChannel

```cpp
enum class DimmerChannel : uint8_t {
    CHANNEL_1 = 0,      // First channel (GPIO 19)
    CHANNEL_2 = 1       // Second channel (GPIO 23)
};
```

#### DimmerCurve

```cpp
enum class DimmerCurve : uint8_t {
    LINEAR,         // Linear
    RMS,            // RMS-compensated (recommended)
    LOGARITHMIC     // Logarithmic
};
```

#### DimmerStatus

```cpp
struct DimmerStatus {
    bool initialized;           // Initialized
    bool active;                // Active
    uint8_t power_percent;      // Current power (0-100%)
    uint8_t target_percent;     // Target power (during transition)
    DimmerCurve curve;          // Curve type
    uint32_t last_update_ms;    // Last update time
};
```

---

## 5.4 ConfigManager API

**Files:**
- [components/utils/include/ConfigManager.h](../components/utils/include/ConfigManager.h)
- [components/utils/src/ConfigManager.cpp](../components/utils/src/ConfigManager.cpp)

**Design pattern:** Singleton

**NVS namespace:** `"acrouter"`

### Get Instance

```cpp
ConfigManager& config = ConfigManager::getInstance();
```

### Initialization

#### begin()

```cpp
bool begin();
```

**Description:** Initialize NVS and load configuration.

**Returns:** `true` if successful

**Example:**

```cpp
ConfigManager& config = ConfigManager::getInstance();

if (config.begin()) {
    Serial.println("ConfigManager initialized");
    Serial.printf("Router mode: %d\n", config.getConfig().router_mode);
}
```

---

### Get Configuration

#### getConfig()

```cpp
const SystemConfig& getConfig() const;
```

**Description:** Get reference to current configuration.

**Returns:** Const reference to SystemConfig

**Example:**

```cpp
const SystemConfig& cfg = config.getConfig();

Serial.printf("Mode: %d\n", cfg.router_mode);
Serial.printf("Kp: %.1f\n", cfg.control_gain);
Serial.printf("Threshold: %.1f W\n", cfg.balance_threshold);
```

---

### Set Parameters

#### setRouterMode()

```cpp
bool setRouterMode(uint8_t mode);
```

**Description:** Set router mode (automatically saved to NVS).

**Parameters:**
- `mode` - mode 0-5

**Returns:** `true` if successfully saved

**Example:**

```cpp
config.setRouterMode(1);  // AUTO mode
```

#### setControlGain()

```cpp
bool setControlGain(float gain);
```

**Description:** Set Kp coefficient.

**Parameters:**
- `gain` - coefficient (10.0 - 1000.0)

**Example:**

```cpp
config.setControlGain(200.0f);
```

#### setBalanceThreshold()

```cpp
bool setBalanceThreshold(float threshold);
```

**Description:** Set balance threshold.

**Parameters:**
- `threshold` - threshold in Watts (0.0 - 100.0)

**Example:**

```cpp
config.setBalanceThreshold(10.0f);
```

#### setManualLevel()

```cpp
bool setManualLevel(uint8_t level);
```

**Description:** Set level for MANUAL mode.

**Parameters:**
- `level` - level 0-100%

**Example:**

```cpp
config.setManualLevel(75);
```

#### setVoltageCoefficient()

```cpp
bool setVoltageCoefficient(float coef);
```

**Description:** Set voltage calibration coefficient.

**Example:**

```cpp
config.setVoltageCoefficient(230.0f);
```

#### setCurrentCoefficient()

```cpp
bool setCurrentCoefficient(float coef);
```

**Description:** Set current calibration coefficient.

**Example:**

```cpp
config.setCurrentCoefficient(30.0f);  // SCT-013-030
```

---

### Batch Operations

#### saveAll()

```cpp
bool saveAll();
```

**Description:** Save all parameters to NVS.

**Note:** Usually not required as each `set*()` method automatically saves.

#### loadAll()

```cpp
bool loadAll();
```

**Description:** Reload configuration from NVS.

#### resetToDefaults()

```cpp
bool resetToDefaults();
```

**Description:** Reset all settings to factory defaults and save to NVS.

**Example:**

```cpp
if (config.resetToDefaults()) {
    Serial.println("Config reset to defaults");
}
```

---

### Data Structures

#### SystemConfig

```cpp
struct SystemConfig {
    // Router parameters
    uint8_t router_mode;        // RouterMode (0-5)
    float control_gain;         // Kp (10.0 - 1000.0)
    float balance_threshold;    // Balance threshold (W)
    uint8_t manual_level;       // MANUAL level (0-100%)

    // Sensor calibration
    float voltage_coef;         // Voltage coefficient
    float current_coef;         // Current coefficient (A/V)
    float current_threshold;    // Minimum current (A)
    float power_threshold;      // Minimum power (W)
};
```

---

## 5.5 HardwareConfigManager API

**Files:**
- [components/utils/include/HardwareConfigManager.h](../components/utils/include/HardwareConfigManager.h)
- [components/utils/src/HardwareConfigManager.cpp](../components/utils/src/HardwareConfigManager.cpp)

**Design pattern:** Singleton

**NVS namespace:** `"hw_config"`

### Get Instance

```cpp
HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();
```

### Initialization

#### begin()

```cpp
bool begin();
```

**Description:** Initialize and load hardware configuration from NVS.

**Example:**

```cpp
HardwareConfigManager& hwConfig = HardwareConfigManager::getInstance();

if (hwConfig.begin()) {
    Serial.println("HardwareConfigManager initialized");
}
```

---

### ADC Channel Configuration

#### setADCChannel()

```cpp
bool setADCChannel(uint8_t channel, const ADCChannelConfig& config);
```

**Description:** Configure ADC channel.

**Parameters:**
- `channel` - channel number (0-3)
- `config` - channel configuration

**Returns:** `true` if successful

**Example:**

```cpp
ADCChannelConfig voltage_cfg;
voltage_cfg.gpio = 35;
voltage_cfg.type = SensorType::VOLTAGE_AC;
voltage_cfg.multiplier = 230.0f;
voltage_cfg.offset = 0.0f;
voltage_cfg.enabled = true;

hwConfig.setADCChannel(0, voltage_cfg);
```

#### getADCChannel()

```cpp
ADCChannelConfig getADCChannel(uint8_t channel) const;
```

**Description:** Get ADC channel configuration.

**Example:**

```cpp
ADCChannelConfig cfg = hwConfig.getADCChannel(0);
Serial.printf("ADC0: GPIO%d, Type=%d, Mult=%.1f\n",
              cfg.gpio, static_cast<int>(cfg.type), cfg.multiplier);
```

---

### Dimmer Configuration

#### setDimmerChannel()

```cpp
bool setDimmerChannel(uint8_t channel, const DimmerChannelConfig& config);
```

**Description:** Configure dimmer channel.

**Parameters:**
- `channel` - 0 (Channel 1) or 1 (Channel 2)
- `config` - configuration

**Example:**

```cpp
DimmerChannelConfig dim_cfg;
dim_cfg.gpio = 19;
dim_cfg.enabled = true;

hwConfig.setDimmerChannel(0, dim_cfg);
```

---

### Zero-Cross Configuration

#### setZeroCross()

```cpp
bool setZeroCross(uint8_t gpio, bool enabled = true);
```

**Description:** Configure zero-cross detector pin.

**Example:**

```cpp
hwConfig.setZeroCross(18, true);  // GPIO18, enabled
```

---

### Relay Configuration

#### setRelayChannel()

```cpp
bool setRelayChannel(uint8_t channel, const RelayChannelConfig& config);
```

**Description:** Configure relay channel.

**Parameters:**
- `channel` - 0 or 1
- `config` - relay configuration

**Example:**

```cpp
RelayChannelConfig relay_cfg;
relay_cfg.gpio = 15;
relay_cfg.active_high = true;  // Active HIGH
relay_cfg.enabled = true;

hwConfig.setRelayChannel(0, relay_cfg);
```

---

### LED Configuration

#### setStatusLED()

```cpp
bool setStatusLED(uint8_t gpio);
```

**Description:** Configure GPIO for status LED.

**Example:**

```cpp
hwConfig.setStatusLED(17);
```

#### setLoadLED()

```cpp
bool setLoadLED(uint8_t gpio);
```

**Description:** Configure GPIO for load indicator LED.

---

### Validation

#### validate()

```cpp
bool validate(String* error_msg = nullptr) const;
```

**Description:** Validate configuration for errors.

**Parameters:**
- `error_msg` - pointer to String for error message (optional)

**Returns:** `true` if configuration is valid

**Checks:**
-  GPIO conflicts (one pin = one function)
-  Pin validity for ADC (ADC1 only: 32-39)
-  Input-only pins (34,35,36,39 cannot be outputs)

**Example:**

```cpp
String error;
if (!hwConfig.validate(&error)) {
    Serial.printf("Validation failed: %s\n", error.c_str());
} else {
    Serial.println("Configuration is valid");
}
```

---

### Batch Operations

#### saveAll()

```cpp
bool saveAll();
```

**Description:** Save entire configuration to NVS.

#### loadAll()

```cpp
bool loadAll();
```

**Description:** Load configuration from NVS.

#### resetToDefaults()

```cpp
bool resetToDefaults();
```

**Description:** Reset to factory defaults.

#### printConfig()

```cpp
void printConfig() const;
```

**Description:** Print configuration to Serial.

**Example:**

```cpp
hwConfig.printConfig();
// Output:
// === Hardware Configuration ===
// ADC0: GPIO35, VOLTAGE_AC, mult=230.0, enabled
// ADC1: GPIO39, CURRENT_LOAD, mult=30.0, enabled
// ...
```

---

### Data Structures

#### ADCChannelConfig

```cpp
struct ADCChannelConfig {
    uint8_t gpio;               // GPIO pin (32-39 for ADC1)
    SensorType type;            // Sensor type
    float multiplier;           // Calibration multiplier
    float offset;               // Offset
    bool enabled;               // Channel enabled
};
```

#### DimmerChannelConfig

```cpp
struct DimmerChannelConfig {
    uint8_t gpio;               // GPIO pin
    bool enabled;               // Channel enabled
};
```

#### RelayChannelConfig

```cpp
struct RelayChannelConfig {
    uint8_t gpio;               // GPIO pin
    bool active_high;           // true=Active HIGH, false=Active LOW
    bool enabled;               // Channel enabled
};
```

#### SensorType

```cpp
enum class SensorType : uint8_t {
    NONE = 0,           // Not used
    VOLTAGE_AC,         // ZMPT107 (AC voltage)
    CURRENT_LOAD,       // ACS-712 (dimmer load current)
    CURRENT_GRID,       // SCT-013 (grid current)
    CURRENT_SOLAR,      // SCT-013 (solar current)
    CURRENT_AUX1,       // Auxiliary channel 1
    CURRENT_AUX2        // Auxiliary channel 2
};
```

---

## 5.6 WiFiManager API

**Files:**
- [components/comm/include/WiFiManager.h](../components/comm/include/WiFiManager.h)
- [components/comm/src/WiFiManager.cpp](../components/comm/src/WiFiManager.cpp)

**Design pattern:** Singleton

### Get Instance

```cpp
WiFiManager& wifi = WiFiManager::getInstance();
```

### Initialization

#### begin()

```cpp
bool begin();
```

**Description:** Initialize WiFi. Automatically starts AP if no STA credentials.

**Returns:** `true` if successful

**Example:**

```cpp
WiFiManager& wifi = WiFiManager::getInstance();

if (wifi.begin()) {
    Serial.println("WiFiManager initialized");

    WiFiStatus status = wifi.getStatus();
    Serial.printf("AP IP: %s\n", status.ap_ip.toString().c_str());
}
```

---

### AP Mode

#### startAP()

```cpp
bool startAP(const char* ssid = nullptr, const char* password = nullptr);
```

**Description:** Start Access Point.

**Parameters:**
- `ssid` - network SSID (nullptr = auto: "ACRouter_XXXXXX")
- `password` - password (nullptr = "12345678")

**Example:**

```cpp
// With auto-generated SSID
wifi.startAP();

// With custom SSID
wifi.startAP("MyACRouter", "mypassword123");
```

#### stopAP()

```cpp
void stopAP();
```

**Description:** Stop Access Point.

#### isAPActive()

```cpp
bool isAPActive() const;
```

**Returns:** `true` if AP is active

---

### STA Mode

#### connectSTA()

```cpp
bool connectSTA(const char* ssid, const char* password);
```

**Description:** Connect to WiFi network.

**Parameters:**
- `ssid` - network SSID
- `password` - password

**Returns:** `true` if connected

**Example:**

```cpp
if (wifi.connectSTA("HomeNetwork", "password123")) {
    Serial.println("Connected to WiFi");

    WiFiStatus status = wifi.getStatus();
    Serial.printf("STA IP: %s\n", status.sta_ip.toString().c_str());
}
```

#### disconnectSTA()

```cpp
void disconnectSTA();
```

**Description:** Disconnect from STA network.

#### isSTAConnected()

```cpp
bool isSTAConnected() const;
```

**Returns:** `true` if connected to STA

---

### Network Scanning

#### scanNetworks()

```cpp
std::vector<WiFiNetwork> scanNetworks();
```

**Description:** Scan available WiFi networks.

**Returns:** Vector of WiFiNetwork structures

**Example:**

```cpp
std::vector<WiFiNetwork> networks = wifi.scanNetworks();

Serial.printf("Found %d networks:\n", networks.size());
for (const auto& net : networks) {
    Serial.printf("  %s (RSSI: %d, Encrypted: %s)\n",
                  net.ssid.c_str(),
                  net.rssi,
                  net.encrypted ? "Yes" : "No");
}
```

---

### Status

#### getStatus()

```cpp
WiFiStatus getStatus() const;
```

**Description:** Get complete WiFi status.

**Example:**

```cpp
WiFiStatus status = wifi.getStatus();

Serial.printf("State: %d\n", static_cast<int>(status.state));
Serial.printf("STA connected: %s\n", status.sta_connected ? "Yes" : "No");
Serial.printf("AP active: %s\n", status.ap_active ? "Yes" : "No");

if (status.sta_connected) {
    Serial.printf("STA IP: %s\n", status.sta_ip.toString().c_str());
    Serial.printf("RSSI: %d dBm\n", status.rssi);
}

if (status.ap_active) {
    Serial.printf("AP IP: %s\n", status.ap_ip.toString().c_str());
    Serial.printf("Clients: %d\n", status.sta_clients);
}
```

---

### Data Structures

#### WiFiState

```cpp
enum class WiFiState : uint8_t {
    IDLE,               // Not initialized
    AP_ONLY,            // AP only
    STA_CONNECTING,     // Connecting to STA
    STA_CONNECTED,      // Connected to STA
    AP_STA,             // Both modes
    STA_FAILED          // STA failed
};
```

#### WiFiStatus

```cpp
struct WiFiStatus {
    WiFiState state;
    bool sta_connected;
    bool ap_active;
    IPAddress sta_ip;
    IPAddress ap_ip;
    String sta_ssid;
    String ap_ssid;
    int8_t rssi;
    uint8_t sta_clients;        // Clients on AP
};
```

#### WiFiNetwork

```cpp
struct WiFiNetwork {
    String ssid;
    int8_t rssi;
    bool encrypted;
};
```

---

## 5.7 WebServerManager API

**Files:**
- [components/comm/include/WebServerManager.h](../components/comm/include/WebServerManager.h)
- [components/comm/src/WebServerManager.cpp](../components/comm/src/WebServerManager.cpp)

**Design pattern:** Singleton

### Get Instance

```cpp
WebServerManager& webServer = WebServerManager::getInstance();
```

### Initialization

#### begin()

```cpp
bool begin(uint16_t http_port = 80);
```

**Description:** Initialize HTTP server.

**Parameters:**
- `http_port` - HTTP port (default 80)

**Example:**

```cpp
WebServerManager& webServer = WebServerManager::getInstance();

if (webServer.begin(80)) {
    Serial.println("WebServer started on port 80");
}
```

---

### Control

#### start()

```cpp
void start();
```

**Description:** Start web server.

#### stop()

```cpp
void stop();
```

**Description:** Stop web server.

#### isRunning()

```cpp
bool isRunning() const;
```

**Returns:** `true` if server is running

---

### Request Handling

#### handleClient()

```cpp
void handleClient();
```

**Description:** Handle incoming HTTP requests. Call from `loop()`.

**Example:**

```cpp
void loop() {
    webServer.handleClient();
    // ... rest of code
}
```

---

### REST API Endpoints

For complete endpoint list see [Section 8: GET endpoints](08_WEB_API_GET_EN.md) and [Section 9: POST endpoints](09_WEB_API_POST_EN.md).

**Main groups:**

- **Web pages:** `/`, `/wifi`, `/settings/hardware`
- **Status:** `/api/status`, `/api/metrics`, `/api/config`
- **Control:** `/api/mode`, `/api/manual`, `/api/calibrate`
- **WiFi:** `/api/wifi/*`
- **Hardware:** `/api/hardware/*`
- **System:** `/api/system/reboot`

---

**Next section:** [6. Main Application File (main.cpp)](06_MAIN_APPLICATION_EN.md)
