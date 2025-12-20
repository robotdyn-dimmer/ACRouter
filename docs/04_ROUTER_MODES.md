# 4. RouterController Operating Modes

## 4.1 Mode Overview

RouterController supports 6 operating modes, each optimized for a specific use case scenario.

### Mode Table

| Mode | Enum | Dimmer Control | P_grid Control | Required Sensors | Application |
|------|------|----------------|----------------|------------------|-------------|
| **OFF** | 0 | 0% (fixed) | ❌ No | - | Maintenance, shutdown |
| **AUTO** | 1 | Automatic | ✅ P_grid → 0 | Grid Current | Standard Solar Router |
| **ECO** | 2 | Semi-automatic | ⚠️ Import only | Grid Current | Export prevention |
| **OFFGRID** | 3 | Automatic | ❌ No | Solar Current | Off-grid systems |
| **MANUAL** | 4 | Fixed | ❌ No | - | Night tariff, testing |
| **BOOST** | 5 | 100% (fixed) | ❌ No | - | Forced heating |

---

## 4.2 OFF Mode (0) - Disabled

### Description

Complete dimmer control shutdown. Used for maintenance, debugging, or when load control is not required.

### Algorithm

```cpp
void RouterController::processOffMode() {
    // Dimmer always 0%
    if (m_status.dimmer_percent != 0) {
        applyDimmerLevel(0);
    }
    m_status.state = RouterState::IDLE;
}
```

### Parameters

No configurable parameters.

### Required Sensors

Sensors are not required (measurements continue but are not used).

### Behavior

- **Dimmer level:** 0% (constant)
- **P_grid:** Not controlled
- **State:** IDLE
- **Measurements:** Continue (PowerMeterADC works)
- **Web interface:** Available
- **Serial commands:** Work

### When to Use

- ✅ Device maintenance
- ✅ Sensor testing without control
- ✅ System debugging
- ✅ Temporary load disconnection

### Usage Example

```cpp
// Via code
RouterController& router = RouterController::getInstance();
router.setMode(RouterMode::OFF);

// Via Serial
set-mode 0

// Via REST API
POST /api/mode
{"mode": 0}
```

---

## 4.3 AUTO Mode (1) - Automatic Solar Router

### Description

**Main Solar Router mode.** Automatically balances grid power (P_grid) to zero, redirecting excess solar energy to the load instead of exporting to the grid.

### Algorithm

```cpp
void RouterController::processAutoMode(float power_grid) {
    // Goal: P_grid → 0
    // Proportional control with configurable gain

    float error = -power_grid;  // Invert: export = positive error

    // Check if within balance threshold
    if (fabs(power_grid) <= balance_threshold) {
        // Within threshold - hold current level
        return;
    }

    // Proportional control
    float delta = error / control_gain;
    m_target_level += delta;

    // Apply new level (clamped to 0-100%)
    applyDimmerLevel(m_target_level);
}
```

### Behavior by P_grid Sign

| Situation | P_grid | Error | Delta | Action |
|-----------|--------|-------|-------|--------|
| **Export to grid** | < 0 (negative) | > 0 (positive) | > 0 | **Increase** dimmer → more load |
| **Import from grid** | > 0 (positive) | < 0 (negative) | < 0 | **Decrease** dimmer → less load |
| **Balance** | ≈ 0 (within threshold) | ≈ 0 | 0 | **Hold** current level |

### Parameters

#### control_gain (Kp)

**Description:** Proportional controller coefficient. Determines the response speed to P_grid changes.

**Formula:** `delta = error / control_gain`

**Range:** 10.0 - 1000.0

**Default:** 200.0

**Impact:**

- **Lower Kp (faster):**
  - ✅ Fast response to changes
  - ❌ Possible oscillations
  - Example: Kp = 50 → fast, but unstable

- **Higher Kp (slower):**
  - ✅ Smooth control
  - ✅ Stability
  - ❌ Slow response
  - Example: Kp = 500 → slow, but stable

**Recommendations:**

```
System Type              | Recommended Kp | Comment
-------------------------|----------------|------------------------
Stable grid              | 150 - 250      | Optimal
Unstable grid            | 300 - 500      | Increase for stability
Fast clouds              | 100 - 150      | Decrease for responsiveness
High inertia heater      | 200 - 400      | Medium values
```

#### balance_threshold

**Description:** Balance threshold in Watts. If `|P_grid| < threshold`, the system is considered balanced.

**Range:** 0.0 - 100.0 W

**Default:** 10.0 W

**Impact:**

- **Lower threshold:**
  - ✅ More precise balance (P_grid closer to 0)
  - ❌ More adjustments (TRIAC wear)

- **Higher threshold:**
  - ✅ Fewer switching cycles
  - ❌ Less accurate balance

**Recommendations:**

```
Load Power               | Recommended Threshold
-------------------------|----------------------
< 500 W                  | 5 - 10 W
500 - 1500 W             | 10 - 20 W
> 1500 W                 | 20 - 50 W
```

### Required Sensors

**Minimum:**

- ✅ **Current Grid** (required) - for P_grid determination
- ✅ **Voltage** (recommended) - for power calculation

**Optional:**

- ⚪ Current Load - for consumption monitoring
- ⚪ Current Solar - for generation monitoring

### Operation Examples

#### Example 1: Solar Excess

```
Initial state:
  P_solar = 3000 W
  P_house = 800 W
  P_grid = -2200 W (export!)
  Dimmer = 0%

After 10 seconds (Kp=200):
  error = -(-2200) = +2200
  delta = 2200 / 200 = +11% per iteration
  → Dimmer gradually increases

After 30 seconds:
  Dimmer ≈ 70% (~1400 W to heater)
  P_grid ≈ -800 W (still exporting)
  → Continues to increase

After 60 seconds (stabilization):
  Dimmer = 95% (~1900 W)
  P_grid ≈ -5 W (within 10W threshold)
  → Balance achieved ✅
```

#### Example 2: Cloud Covers Sun

```
Stable state:
  P_solar = 2500 W
  P_grid = 0 W (balance)
  Dimmer = 80% (~1600 W)

Cloud:
  P_solar = 500 W (sharp drop)
  P_grid = +1100 W (import!)

  error = -1100
  delta = -1100 / 200 = -5.5%
  → Dimmer decreases by 5.5%

After 5 seconds:
  Dimmer ≈ 50% (~1000 W)
  P_grid ≈ +100 W (close to balance)

After 10 seconds:
  Dimmer = 30% (~600 W)
  P_grid ≈ +5 W (within threshold)
  → New balance achieved ✅
```

### Configuration via Interfaces

```cpp
// Via code
router.setMode(RouterMode::AUTO);
router.setControlGain(200.0f);
router.setBalanceThreshold(10.0f);

// Via Serial
set-mode 1
set-kp 200
set-threshold 10

// Via REST API
POST /api/mode
{"mode": 1}

POST /api/config
{"control_gain": 200.0, "balance_threshold": 10.0}
```

---

## 4.4 ECO Mode (2) - Economy (Anti-Export)

### Description

**Export prevention mode.** Avoids exporting electricity to the grid, but allows import. Used when export is unprofitable or prohibited.

### Algorithm

```cpp
void RouterController::processEcoMode(float power_grid) {
    // Goal: P_grid >= 0 (avoid export, allow import)

    if (power_grid > balance_threshold) {
        // IMPORTING from grid - reduce load
        float error = -power_grid;

        // Slower response: increase gain by 1.5x
        float delta = error / (control_gain * 1.5f);
        m_target_level += delta;

        applyDimmerLevel(m_target_level);
    } else {
        // EXPORTING or BALANCED - hold current level
        // Do not increase load aggressively
    }
}
```

### Differences from AUTO

| Aspect | AUTO | ECO |
|--------|------|-----|
| **Export** | Prevented (dimmer increases) | Allowed (dimmer does not increase) |
| **Import** | Prevented (dimmer decreases) | **Allowed** (dimmer slowly decreases) |
| **Response speed** | Fast (Kp) | **Slower** (Kp × 1.5) |
| **Goal** | P_grid = 0 | P_grid ≥ 0 |

### Behavior

- **P_grid > threshold (import):**
  - Slowly decrease dimmer
  - Speed: Kp × 1.5 (slower than AUTO)

- **P_grid < 0 (export):**
  - **DO NOT increase** dimmer
  - Hold current level

- **P_grid ≈ 0:**
  - Hold current level

### Parameters

Same as AUTO:

- **control_gain** - affects decrease speed during import
- **balance_threshold** - threshold for balance determination

### Required Sensors

- ✅ **Current Grid** (required)
- ✅ **Voltage** (recommended)

### When to Use

- ✅ No grid export contract
- ✅ Export tariff is unfavorable (< import cost)
- ✅ Technical export restriction
- ✅ Export protection with unstable grid

### Operation Example

```
Initial state:
  P_solar = 2000 W
  P_house = 1000 W
  P_grid = -1000 W (export 1 kW)
  Dimmer = 0%

In ECO mode:
  → Dimmer does NOT increase
  → P_grid remains -1000 W (export continues)

  Result: Excess goes to grid (acceptable in ECO)

After 1 hour (cloud):
  P_solar = 500 W
  P_house = 1000 W
  P_grid = +500 W (import!)

  → Dimmer slowly decreases (if it was enabled)
  → After some time P_grid ≈ 0
```

---

## 4.5 OFFGRID Mode (3) - Off-Grid

### Description

**Mode for autonomous systems** with solar panels and batteries, without grid connection. Uses excess solar energy for the load.

### Algorithm

```cpp
void RouterController::processOffgridMode(const Measurements& meas) {
    float power_solar = meas.power_active[CURRENT_SOLAR];
    float power_load = meas.power_active[CURRENT_LOAD];

    if (power_solar > balance_threshold) {
        // Solar generation available
        float available_power = power_solar * 0.8f;  // 80% safety margin

        if (available_power > power_load + balance_threshold) {
            // Can increase load
            float delta = (available_power - power_load) / (control_gain * 2.0f);
            m_target_level += delta;
        } else if (available_power < power_load - balance_threshold) {
            // Need to reduce load
            m_target_level -= 5.0f;  // Gradual decrease
        }

        applyDimmerLevel(m_target_level);
    } else {
        // No solar generation - reduce load to minimum
        m_target_level -= 10.0f;  // Faster decrease
        applyDimmerLevel(m_target_level);
    }
}
```

### Features

- **Does NOT use P_grid** (grid sensor may be absent)
- **Uses P_solar** to determine available power
- **Conservative:** 80% of available power (battery protection)
- **Priority:** Main load → Batteries → Heater

### Parameters

- **control_gain** - affects load increase speed
- **balance_threshold** - minimum solar power for activation
- **Safety margin:** 0.8 (80%) - hardcoded

### Required Sensors

**Minimum:**

- ✅ **Current Solar** (required) - for generation determination
- ✅ **Voltage** (recommended)

**Optional:**

- ⚪ Current Load - for more precise control
- ❌ Current Grid - **NOT required** (may be absent)

### When to Use

- ✅ Off-grid systems
- ✅ Solar panels + batteries
- ✅ No grid connection
- ✅ Maximize solar energy usage

### Operation Example

```
Daytime (sunny):
  P_solar = 1500 W
  P_house = 800 W (from battery/solar)
  P_load (dimmer) = 0 W

  available_power = 1500 * 0.8 = 1200 W

  → Can turn on heater
  → Dimmer gradually increases to ~60% (1200 W)
  → Battery is not discharging (enough solar)

Evening (sunset):
  P_solar = 200 W (dropping)
  available_power = 200 * 0.8 = 160 W
  P_load = 800 W (heater was on)

  → Too little solar
  → Dimmer quickly decreases (10% per iteration)
  → After several iterations: Dimmer = 0%
  → Battery is saved for main load

Night:
  P_solar = 0 W
  → Dimmer = 0%
  → Heater is off
```

---

## 4.6 MANUAL Mode (4) - Manual

### Description

Fixed dimmer level set by user. No automatic control.

### Algorithm

```cpp
void RouterController::processManualMode() {
    // Fixed level set by user
    if (m_status.dimmer_percent != m_manual_level) {
        applyDimmerLevel(m_manual_level);
    }
    m_status.state = RouterState::IDLE;
}
```

### Parameters

- **manual_level** - dimmer level (0-100%)

### Required Sensors

Sensors are not required (measurements continue for monitoring).

### When to Use

- ✅ Night tariff (set 100% for night)
- ✅ Load testing
- ✅ Temperature maintenance (set 50%)
- ✅ System debugging

### Usage Example

```cpp
// Set 75%
router.setMode(RouterMode::MANUAL);
router.setManualLevel(75);

// Via Serial
set-mode 4
set-manual 75

// Via REST API
POST /api/mode
{"mode": 4}

POST /api/manual
{"level": 75}
```

### Scenario: Night Tariff

```bash
# 23:00 - night tariff starts (cheap)
set-mode 4
set-manual 100  # Full power

# 07:00 - night tariff ends
set-mode 1      # Switch to AUTO
```

---

## 4.7 BOOST Mode (5) - Forced Heating

### Description

Dimmer fixed at 100%. Maximum power to load.

### Algorithm

```cpp
void RouterController::processBoostMode() {
    // Always 100%
    if (m_status.dimmer_percent != 100) {
        applyDimmerLevel(100);
    }
    m_status.state = RouterState::AT_MAXIMUM;
}
```

### Parameters

No configurable parameters (always 100%).

### Required Sensors

Not required.

### When to Use

- ✅ Fast water heater heating
- ✅ Using cheap tariff
- ✅ Emergency mode

### Warnings

⚠️ **WARNING:**

- High grid consumption
- Possible load overheating
- Monitor temperature manually
- Do not leave unattended

### Usage Example

```cpp
// Enable BOOST
router.setMode(RouterMode::BOOST);

// Return to AUTO after 2 hours
delay(2 * 60 * 60 * 1000);
router.setMode(RouterMode::AUTO);
```

---

## 4.8 Switching Between Modes

### Safe Switching

When changing mode, RouterController:

1. Logs the mode change
2. Resets internal state
3. Applies new algorithm

```cpp
void RouterController::setMode(RouterMode mode) {
    if (m_status.mode == mode) {
        return;  // No change
    }

    RouterMode old_mode = m_status.mode;
    m_status.mode = mode;

    ESP_LOGI(TAG, "Mode changed: %d -> %d",
             static_cast<int>(old_mode),
             static_cast<int>(mode));

    // Reset state for new mode
    m_status.state = RouterState::IDLE;
}
```

### Switching Recommendations

**Safe transitions:**

```
OFF ↔ AUTO      ✅ Safe
OFF ↔ MANUAL    ✅ Safe
AUTO ↔ ECO      ✅ Safe (similar algorithms)
MANUAL ↔ BOOST  ✅ Safe
```

**Caution:**

```
AUTO → BOOST    ⚠️ Sharp power spike
OFFGRID → AUTO  ⚠️ Different sensors
```

### Command Sequences

```bash
# Example 1: Morning transition
set-mode 0        # OFF - pause
# ... system check ...
set-mode 1        # AUTO - start operation

# Example 2: Testing
set-mode 4        # MANUAL
set-manual 25     # 25% for test
# ... observation ...
set-manual 50     # 50%
# ... observation ...
set-mode 1        # Return to AUTO
```

---

## 4.9 Minimum Sensor Configurations

### For Each Mode

| Mode | Required Sensors | Optional | Note |
|------|------------------|----------|------|
| **OFF** | - | - | Sensors not used |
| **AUTO** | Voltage, Current Grid | Current Load, Current Solar | Grid needed for P_grid |
| **ECO** | Voltage, Current Grid | Current Load, Current Solar | Grid needed for P_grid |
| **OFFGRID** | Voltage, Current Solar | Current Load | Grid **NOT required** |
| **MANUAL** | - | Voltage, Current Grid | For monitoring only |
| **BOOST** | - | Voltage, Current Grid | For monitoring only |

### Minimum Configuration (Solar Router)

**For AUTO/ECO modes:**

```
ADC0: GPIO35, VOLTAGE_AC,    enabled=true   ← Required
ADC1: GPIO36, CURRENT_GRID,  enabled=true   ← Required
ADC2: -,      NONE,           enabled=false
ADC3: -,      NONE,           enabled=false

Dimmer1: GPIO19, enabled=true               ← Required
ZeroCross: GPIO18, enabled=true             ← Required
```

**Result:**

- ✅ P_grid measurement (for balance)
- ✅ Dimmer control
- ❌ No solar/load monitoring

### Full Configuration

```
ADC0: GPIO35, VOLTAGE_AC,     enabled=true
ADC1: GPIO39, CURRENT_LOAD,   enabled=true
ADC2: GPIO36, CURRENT_GRID,   enabled=true
ADC3: GPIO34, CURRENT_SOLAR,  enabled=true

Dimmer1: GPIO19, enabled=true
Dimmer2: GPIO23, enabled=true (optional)
ZeroCross: GPIO18, enabled=true
```

**Result:**

- ✅ Full system monitoring
- ✅ All modes supported
- ✅ Detailed analytics

---

## 4.10 Parameter Configuration via Interfaces

### Via Serial Commands

```bash
# Mode switching
set-mode 0          # OFF
set-mode 1          # AUTO
set-mode 2          # ECO
set-mode 3          # OFFGRID
set-mode 4          # MANUAL
set-mode 5          # BOOST

# AUTO/ECO parameters
set-kp 200          # Set Kp = 200
set-threshold 10    # Balance threshold 10 W

# MANUAL parameters
set-manual 75       # Set 75%

# Save
config-save         # Save to NVS
```

### Via REST API

```bash
# Set mode
curl -X POST http://192.168.4.1/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": 1}'

# Set parameters
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "control_gain": 200.0,
    "balance_threshold": 10.0
  }'

# Set MANUAL level
curl -X POST http://192.168.4.1/api/manual \
  -H "Content-Type: application/json" \
  -d '{"level": 75}'
```

### Via Web Interface

1. Open Dashboard: `http://192.168.4.1/`
2. Select mode (6 buttons)
3. For MANUAL: move slider
4. Click "Apply"

---

**Next Section:** [5. Module APIs and Data Structures](05_API_REFERENCE.md)
