# 1. Application Overview

## 1.1 Purpose

**ACRouter** is an open-source controller for automatically redirecting excess solar energy to resistive loads (such as water heaters) instead of exporting it to the grid. It also optimizes energy consumption during peak and off-peak tariff periods with consumption limits.

**Main Goal:** Minimize energy export to the grid by dynamically controlling load power based on the current balance between production and consumption. Reduce costs of electricity consumption from the grid.

### Key Benefits:
- ✅ Automatic management of excess solar energy
- ✅ Optimization of off-peak tariffs and consumption limits
- ✅ Reduced electricity costs (self-consumption)
- ✅ Lower equipment costs. No batteries required for energy storage
- ✅ Export protection (ECO mode)
- ✅ Flexible configuration via web interface
- ✅ Multiple operating modes supported
- ✅ Control of multiple consumption devices. Dimmers and relays.
- ✅ Open source code

---

## 1.2 Features

### Measurement and Monitoring
- **Grid voltage measurement** (ZMPT107 AC voltage sensor)
- **Current measurement** at 3 points (SCT-013/ACS-712 current sensors):
  - Load current (Load)
  - Grid current (Grid) - import/export detection
  - Solar generation current (Solar)
- **Active power calculation** in real time
- **Update frequency:** 200 ms (5 times per second)
- **Measurement accuracy:** 12-bit ADC with DMA (up to 80kHz on 8 channels)

### Load Control
- **Phase-angle dimmer control** (TRIAC dimmer)
  - Smooth power adjustment 0-100%
  - Synchronization with zero-cross detector
  - Support for up to 2 dimmer channels, expandable to available GPIO
- **Relay outputs** (ROADMAP: Develop Phase 2)
  - 2 independent relays, expandable to available GPIO
  - Programmable polarity (Active HIGH/LOW)

### Control Algorithms
- **Proportional controller** (P-controller)
  - Balancing P_grid → 0
  - Adjustable gain coefficient (Kp)
- **Operating modes:** OFF, AUTO, ECO, OFFGRID, MANUAL, BOOST
- **Anti-flickering** - smoothing of dimmer changes

### Communication
- **WiFi:** AP mode (192.168.4.1) + STA mode (connection to router)
- **WebServer:** REST API + Material UI interface
- **Serial Console:** Command line for configuration (115200 baud)
- **OTA Updates:** Over-the-air firmware updates (Phase 2)

### Configuration and Storage
- **NVS (Non-Volatile Storage):**
  - ACRouter settings (mode, Kp, setpoint)
  - WiFi configuration (SSID, password)
  - Hardware configuration (GPIO pins, sensor types)
- **Web interface:**
  - Dashboard with real-time metrics
  - WiFi settings page
  - Hardware configuration page
  - Operating mode selection
- **Serial terminal (console):**
  - Real-time metrics display
  - Command line for hardware and device parameter configuration
  - Operating mode selection

---

## 1.3 Functionality

### Automatic Control
1. **Power measurement:**
   - Voltage and currents are measured every 200 ms
   - RMS value calculation (root mean square)
   - Active power calculation (P = U × I × cos φ)

2. **Balance determination:**
   - P_solar = power from solar panels
   - P_load = current house consumption
   - P_grid = import/export to grid
   - P_dimmer = controlled load power

3. **Control algorithm:**
   - Goal: P_grid → 0 (zero export/import)
   - Proportional controller: ΔDimmer = Kp × P_grid
   - Applied to dimmer with limits 0-100%

### Monitoring via Web Interface
- **Real-time metrics:**
  - Grid voltage (V)
  - Currents: Grid, Solar, Load (A)
  - Power: Grid, Solar, Load, Dimmer (W)
  - Dimmer level (%)
  - Current operating mode

- **System information:**
  - Firmware version
  - Uptime
  - Heap memory (free RAM)
  - WiFi RSSI (signal level)

### Configuration via web-interface
- Operating mode selection (6 buttons)
- Manual dimmer control (slider 0-100%)
- WiFi setup (network scanning, connection)
- GPIO pin and sensor type configuration
- Sensor calibration (multiplier, offset)
- Device reboot

---

## 1.4 Operating Modes

### OFF (0) - Disabled
**Description:** Dimmer is completely disabled (0%), system is inactive.

**Usage:**
- Maintenance
- Load disconnection
- Sensor testing without control

**Behavior:**
- Dimmer level = 0%
- Measurements continue (200 ms)
- Web interface is available
- Serial commands work

---

### AUTO (1) - Automatic (Solar Router)
**Description:** Main Solar Router mode. Automatic balancing of P_grid → 0.

**Algorithm:**
```cpp
// Every 200 ms
P_error = P_grid;  // Positive = import, negative = export
delta = Kp * P_error;  // Proportional controller
dimmer_level += delta;
dimmer_level = constrain(dimmer_level, 0, 100);
```

**Behavior:**
- **P_grid > 0** (import from grid) → Increase dimmer (more load)
- **P_grid < 0** (export to grid) → Decrease dimmer (less load)
- **P_grid ≈ 0** → Dimmer stabilized

**Settings:**
- `Kp` (gain) - gain coefficient (default: 0.05)
  - Higher Kp = faster response, but possible oscillations
  - Lower Kp = smoother, but slower
- `setpoint` - target P_grid value (usually 0 W)

**Example:**
```
Initial state:
  P_solar = 2000 W
  P_load = 500 W
  P_grid = -1500 W (export!)
  Dimmer = 0%

After 5 seconds (AUTO mode, Kp=0.05):
  P_solar = 2000 W
  P_load = 500 W
  Dimmer = 75% (~1500 W to water heater)
  P_grid = 0 W ✅ (balance achieved)
```

---

### ECO (2) - Economy (Anti-Export)
**Description:** Prevents export to the grid. Import is allowed, but export is prohibited.

**Algorithm:**
```cpp
if (P_grid < 0) {  // Export to grid
    // Decrease dimmer (reduce load)
    delta = Kp * P_grid;  // Negative delta
    dimmer_level += delta;
} else {
    // Import is allowed, do not increase dimmer
    // Keep current level
}
dimmer_level = constrain(dimmer_level, 0, 100);
```

**Behavior:**
- **P_grid < 0** (export) → Decrease dimmer (avoid export)
- **P_grid > 0** (import) → Do not change dimmer (import is allowed)
- **P_grid = 0** → Dimmer is stable

**Applications:**
- Export tariff is unfavorable or absent
- Need to use only excess energy
- Export protection when there is no grid export contract

**Example:**
```
Situation 1: Solar excess
  P_solar = 3000 W
  P_load = 1000 W
  P_grid = -500 W (export)
  → ECO mode will increase dimmer by ~500 W → P_grid = 0

Situation 2: Solar shortage
  P_solar = 500 W
  P_load = 1000 W
  P_grid = +500 W (import)
  → ECO mode will NOT change dimmer (import is allowed)
```

---

### OFFGRID (3) - Off-Grid
**Description:** Mode for autonomous systems with batteries. Uses excess solar energy for the load.

**Algorithm:**
```cpp
// Balance by solar panel current
P_available = P_solar - P_load;  // Available power
if (P_available > 0) {
    // There is solar excess → increase dimmer
    dimmer_level = map(P_available, 0, P_dimmer_max, 0, 100);
} else {
    // No excess → turn off dimmer (save battery)
    dimmer_level = 0;
}
```

**Behavior:**
- Uses only solar energy
- Does not consider P_grid (no grid)
- Priority: main load → batteries → dimmer
- Dimmer works only when there is solar excess

**Applications:**
- Systems without grid connection
- Solar panels + batteries
- Maximize solar energy usage

**Example:**
```
Daytime (sunny):
  P_solar = 1500 W
  P_load = 800 W
  P_available = 700 W
  → Dimmer = 50% (~700 W to water heater)
  → Battery is not discharging

Evening (no sun):
  P_solar = 0 W
  P_load = 800 W (from battery)
  → Dimmer = 0% (battery saving)
```

---

### MANUAL (4) - Manual Mode
**Description:** Dimmer is set to a fixed level (no automation).

**Behavior:**
- Dimmer level is set by user (0-100%)
- No automatic regulation
- Level is maintained until manually changed

**Applications:**
- Load testing
- Night tariff (set to 100%)
- Temperature control (set to 50%)
- System debugging

**Settings:**
- `manual_level` - dimmer level (0-100%)

**Example:**
```bash
# Via Serial commands
set-manual 75        # Set dimmer to 75%
set-mode 4           # Switch to MANUAL mode

# Via web interface
1. Select MANUAL mode
2. Move slider to 75%
3. Click "Apply"
```

---

### BOOST (5) - Maximum Power
**Description:** Dimmer at 100% (forced heating).

**Behavior:**
- Dimmer level = 100% (constant)
- Ignores all sensors
- Maximum power to load

**Applications:**
- Fast water heater heating
- Using cheap tariff
- Emergency mode

**Warnings:**
- ⚠️ High grid consumption
- ⚠️ Possible load overheating
- ⚠️ Monitor temperature manually

**Example:**
```
Night tariff (23:00-07:00):
  1. Switch to BOOST mode
  2. Water heater heats at maximum
  3. In the morning switch back to AUTO

Result:
  Heating from cheap tariff
  Daytime operation in AUTO mode (solar)
```

---

## 1.5 Mode Comparison Table

| Mode | Dimmer Control | P_grid Balance | Import Allowed | Export Allowed | Use Case |
|------|----------------|----------------|----------------|----------------|----------|
| **OFF** | 0% (fixed) | ❌ No | N/A | N/A | Maintenance |
| **AUTO** | Automatic | ✅ Yes (→ 0) | ✅ Yes | ✅ Yes | Standard Solar Router |
| **ECO** | Auto (anti-export) | ⚠️ Partial | ✅ Yes | ❌ No | No export contract |
| **OFFGRID** | Auto (solar only) | ❌ No | N/A | N/A | Off-grid systems |
| **MANUAL** | Fixed (user) | ❌ No | ✅ Yes | ✅ Yes | Testing / Night tariff |
| **BOOST** | 100% (fixed) | ❌ No | ✅ Yes | ❌ No | Fast heating |

---

## 1.6 Use Case Scenarios

### Scenario 1: Standard Solar Router
**Equipment:**
- 3 kW solar panels
- Sensors: Solar, Grid, Load, Voltage
- Load: 2 kW water heater

**Mode:** AUTO

**Operation:**
1. During the day there is excess solar energy
2. P_grid is negative (export to grid)
3. Controller increases dimmer → water heater heating
4. P_grid balances → 0 (zero export/import)
5. All solar energy is used locally

---

### Scenario 2: Export Protection (ECO)
**Equipment:**
- 5 kW solar panels
- No grid export contract
- Sensors: Solar, Grid, Voltage
- Load: Water heater element

**Mode:** ECO

**Operation:**
1. During the day solar excess → export to grid begins
2. ECO mode detects P_grid < 0
3. Dimmer increases → water heating
4. Export prevented (P_grid ≥ 0)
5. Evening sun sets → dimmer decreases
6. Import allowed (grid purchase when needed)

---

### Scenario 3: Autonomous System (OFFGRID)
**Equipment:**
- 2 kW solar panels
- 10 kWh batteries
- No grid connection
- Sensors: Solar, Load, Voltage
- Load: Tank heating element

**Mode:** OFFGRID

**Operation:**
1. System is autonomous (no grid)
2. During the day solar excess → water heating
3. At night heating element is off (no sun)
4. Batteries are used for main load
5. Maximize solar energy usage

---

### Scenario 4: Night Tariff + Water Heater
**Equipment:**
- Two-tariff meter (day/night)
- Sensors: Grid, Voltage
- Load: Water heater

**Mode:** MANUAL (night) / OFF (day)

**Operation:**
1. Night (23:00-07:00): MANUAL 100% (cheap tariff)
2. Day (07:00-23:00): OFF (expensive tariff)
3. Programmable via schedule (Phase 2: SCHEDULE mode)

---

## 1.7 System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      ESP32 Controller                       │
│                                                             │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────────┐  │
│  │ PowerMeter  │  │   Router     │  │  DimmerHAL         │  │
│  │   ADC       │→ │  Controller  │→ │  (Zero-cross)      │  │
│  └─────────────┘  └──────────────┘  └────────────────────┘  │
│         ↓                                      ↓            │
│    Measurements                           TRIAC Control     │
│    (200ms cycle)                          (50Hz sync)       │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              WiFi Manager                            │   │
│  │  ┌──────────────┐  ┌────────────────────────────┐    │   │
│  │  │ AP Mode      │  │ WebServer (Material UI)    │    │   │
│  │  │ 192.168.4.1  │  │ + REST API                 │    │   │
│  │  └──────────────┘  └────────────────────────────┘    │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │         ConfigManager + HardwareConfigManager        │   │
│  │                    (NVS Storage)                     │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
        ↑                              ↓
   ┌────────────┐             ┌──────────────────┐
   │  Sensors   │             │  Dimmer Output   │
   ├────────────┤             ├──────────────────┤
   │ ZMPT107    │             │  TRIAC           │
   │ (Voltage)  │             │  (0-100%)        │
   │ SCT-013    │             │                  │
   │ ACS-712    │             │  Load:           │ 
   │ (Current)  │             │  Heater/Boiler   │
   │ x3 sensors │             │  (up to 2kW)     │
   └────────────┘             └──────────────────┘
```

---

## 1.8 Target Audience

**ACRouter is designed for:**

1. **DIY enthusiasts** - build a Solar Router yourself
2. **Homeowners with solar panels** - optimize self-consumption
3. **Developers** - foundation for custom projects (open source)
4. **Educational projects** - learning IoT, energy management, ESP32
5. **Small businesses** - reduce electricity costs

---

## 1.9 Limitations and Warnings

⚠️ **IMPORTANT:**

1. **Electrical Safety**
   - Working with 230V mains voltage is dangerous
   - Electrician qualification is required
   - Galvanic isolation of sensors is mandatory
   - RCD (Residual Current Device) is mandatory

2. **Load Compatibility for dimmers**
   - Suitable only for resistive loads (heating elements, heaters)
   - NOT suitable for inductive loads (motors, transformers)
   - NOT suitable for electronics (LED drivers, power supplies)

3. **Power**
   - Maximum is limited by TRIAC module (usually 2 kW)
   - Cooling is required for high loads
   - Overheat protection is recommended

4. **Measurement Accuracy**
   - Calibration is required for accuracy
   - Error ±5-10% with basic settings
   - Temperature affects sensors

5. **Grid Requirements**
   - Stable grid frequency (50/60 Hz)
   - Quality voltage without strong fluctuations
   - Zero-cross detector is critical for TRIAC

---
