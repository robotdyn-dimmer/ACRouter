# 10. Sensor Calibration

## Table of Contents
- [10.1 Overview](#111-overview)
- [10.2 ZMPT107 Voltage Sensor Calibration](#112-zmpt107-voltage-sensor-calibration)
- [10.3 SCT-013 Current Sensor Calibration](#113-sct-013-current-sensor-calibration)
- [10.4 Saving Calibration](#114-saving-calibration)
- [10.5 Reset to Factory Defaults](#115-reset-to-factory-defaults)

---

## 10.1 Overview

ACRouter supports sensor calibration to ensure accurate voltage and current measurements. Calibration is performed by calculating a **multiplier coefficient** based on measurements using reference instruments.

### Why Calibration is Needed?

- **Manufacturing tolerances**: Each sensor has small deviations from nominal values
- **Grid voltage differences**: Different countries use different standards (230V, 110V, 220V, 240V, etc.)
- **Measurement accuracy**: Calibration ensures ±2% accuracy or better

### Factory Default Values

On first boot or after reset, the following values are used:

| Sensor | Model | Multiplier | Note |
|--------|-------|------------|------|
| Voltage | ZMPT107 | 328.57 | Calibrated for 230V (0.70V RMS = 230V AC) |
| Current | SCT-013-030 | 30.0 | Maximum 30A |

---

## 10.2 ZMPT107 Voltage Sensor Calibration

### ⭐ Automatic Calibration (Recommended)

**Starting with version 2.0**, ACRouter supports **automatic voltage sensor calibration**. The system automatically measures the sensor output (VDC RMS) and calculates the calibration multiplier.

#### Automatic Calibration Procedure

**Step 1: Measure Grid Voltage**

Use a multimeter to measure the actual grid voltage:

```
Example:
Multimeter shows: 232.5V AC
```

**Step 2: Run Auto-Calibration**

Enter the command with the measured voltage:

```bash
hardware-voltage-calibrate 232.5
```

**Step 3: System Automatically:**

1. Measures current sensor output (VDC RMS) - e.g., 0.72V
2. Calculates multiplier: `232.5 / 0.72 = 322.92`
3. Saves calibration data to NVS

**Example Output:**

```
Auto-calibrating voltage sensor...
Measured grid voltage (from multimeter): 232.5 V
Raw sensor VDC output: 0.72 V
Calculated multiplier: 322.92
Sensor type: ZMPT107

Configuration updated:
  nominal_vdc = 0.72 V (measured)
  multiplier = 322.92

Calibration saved to NVS. Please reboot to apply: reboot
```

**Step 4: Reboot**

```bash
reboot
```

**Benefits of Automatic Calibration:**

✅ **No need to adjust sensor potentiometer** manually
✅ **Works with any sensor output voltage** (0.5V, 0.7V, 1.0V, etc.)
✅ **Automatically updates nominal_vdc** to actual measured value
✅ **More accurate and faster** calibration

**Via Web API:**

```bash
curl -X POST http://192.168.4.1/api/hardware/voltage/calibrate \
  -H "Content-Type: application/json" \
  -d '{"measured_vac": 232.5}'
```

**Via Web Interface:**

1. Open `http://<ip>/settings/hardware`
2. Find "Voltage Sensor Auto-Calibration" section
3. Enter measured grid voltage (VAC)
4. Click "Calibrate" button
5. Reboot device

---

### Manual Calibration (Legacy Method)

For compatibility with older versions or special cases, manual calculation can be used.

#### Calibration Principle

The ZMPT107 sensor is factory-calibrated so that:
- **Nominal grid voltage** ≈ **0.70V RMS** output (may vary ±0.1V)
- Formula: `multiplier = V_measured / V_sensor_vdc`

#### Manual Calibration Procedure

**Step 1: Preparation**

1. Connect ACRouter to mains power
2. Connect to serial port (115200 baud)
3. Ensure ZMPT107 sensor is connected to configured GPIO

**Step 2: Measure Grid Voltage**

Use a multimeter to measure **actual grid voltage**:

```
Example:
Multimeter shows: 232.5V AC
```

**Step 3: Check Sensor Output**

Execute command to view raw VDC:

```bash
hardware-voltage-show
```

Example output:
```
Voltage Sensor Configuration:
  GPIO: 35
  Type: ZMPT107
  Nominal VDC: 0.70 V (factory default)
  Multiplier: 328.57
  Current reading: 232.5 V (from 0.72 V raw)
                           ^^^^^^ ← use this value
```

**Step 4: Calculate Multiplier**

Use the formula with **actual** sensor VDC:

```
multiplier = V_measured / V_sensor_vdc
```

For the example above (232.5V / 0.72V):
```
multiplier = 232.5 / 0.72 = 322.92
```

**Step 5: Apply Calibration**

**Via Console:**

```bash
hardware-voltage-config-multiplier 322.92
```

**Via Web API:**

```bash
curl -X POST http://192.168.4.1/api/hardware/config \
  -H "Content-Type: application/json" \
  -d '{
    "adc_channels": [
      {
        "gpio": 35,
        "type": 1,
        "multiplier": 322.92,
        "offset": 0.0,
        "enabled": true
      }
    ]
  }'
```

**Step 6: Reboot**

```bash
reboot
```

### Important Notes

⚠️ **WARNING**: The ZMPT107 sensor uses a voltage divider circuit:
- DC bias center: **1.65V DC**
- Maximum peak voltage: **3.06V** (for 230V AC)
- This is **very close** to ESP32 ADC maximum input (3.3V)!

**Recommendations:**
- Do not use with grid voltage above 250V AC
- For voltages above 240V, consider factory recalibration of ZMPT107 (reduce output to 0.60V = 240V)

---

## 10.3 SCT-013 Current Sensor Calibration

### Calibration Principle

SCT-013 sensors are available in different variants:
- **SCT-013-030**: 0-30A, output 0-1V
- **SCT-013-050**: 0-50A, output 0-1V
- **SCT-013-100**: 0-100A, output 0-1V

Formula: `multiplier = I_measured / V_adc`

### Calibration Procedure

#### Step 1: Preparation

1. Connect a known load (e.g., 2000W electric heater)
2. Measure current using clamp meter
3. Ensure SCT-013 sensor is properly installed on wire

#### Step 2: Measure Current

```
Example:
Load: 2000W heater
Grid voltage: 230V
Expected current: 2000W / 230V = 8.7A
Clamp meter shows: 8.65A
```

#### Step 3: Check Current ADC Reading

Execute command:

```bash
status
```

Find currents:
```
Power Measurements:
  Voltage:     230.0 V
  Current (Load):   7.2 A    ← Uncalibrated current
  Current (Grid):   0.0 A
  ...
```

#### Step 4: Calculate Multiplier

Use the formula:

```
multiplier_new = multiplier_old × (I_measured / I_current)
```

For the example above:
```
multiplier_old = 30.0  (factory default SCT-013-030)
I_measured = 8.65A (clamp meter reading)
I_current = 7.2A (ACRouter reading)

multiplier_new = 30.0 × (8.65 / 7.2) = 36.04
```

#### Step 5: Apply Calibration

**Via Web API:**

```json
{
  "adc_channels": [
    {
      "gpio": 39,
      "type": 2,
      "multiplier": 36.04,
      "offset": 0.0,
      "enabled": true
    }
  ]
}
```

#### Step 6: Verification

Reboot and verify:

```bash
reboot
```

```bash
status
```

Now current should show accurately:
```
Power Measurements:
  Current (Load):   8.65 A    ← Accurate value!
```

### Important Notes

- For accurate calibration use **stable resistive load** (heater, incandescent lamps)
- **Do NOT use** for calibration: switching power supplies or motors (high inrush current)
- Minimum current for calibration: **5A** (below this threshold ADC noise affects readings)

---

## 10.4 Saving Calibration

All configuration changes are automatically saved to **NVS (Non-Volatile Storage)** and persist after reboot.

### Verify Saving

After reboot, execute:

```bash
hardware-config
```

You should see updated `multiplier` values.

---

## 10.5 Reset to Factory Defaults

### Via Console

```bash
hardware-reset
```

Then reboot:

```bash
reboot
```

### Via Web API

Send POST request to `http://<ip>/api/hardware/config/reset`:

```bash
curl -X POST http://192.168.4.1/api/hardware/config/reset
```

---

## Reference Table: Typical Multiplier Values

### Voltage Sensors

| Model | Grid | Calibration | Multiplier |
|-------|------|-------------|------------|
| ZMPT107 | 230V AC | 0.70V = 230V | 328.57 |
| ZMPT107 | 220V AC | 0.70V = 220V | 314.29 |
| ZMPT107 | 240V AC | 0.70V = 240V | 342.86 |
| ZMPT107 | 110V AC | 0.70V = 110V | 157.14 |

### Current Sensors

| Model | Range | Output | Multiplier |
|-------|-------|--------|------------|
| SCT-013-030 | 0-30A | 1V at 30A | 30.0 |
| SCT-013-050 | 0-50A | 1V at 50A | 50.0 |
| SCT-013-100 | 0-100A | 1V at 100A | 100.0 |

---

## Useful Commands

```bash
# Show current measurements
status

# Show sensor configuration
hardware-config

# Reset to factory defaults
hardware-reset

# Reboot
reboot
```

---

**Previous Section:** [09. Web API POST](09_WEB_API_POST_EN.md)
