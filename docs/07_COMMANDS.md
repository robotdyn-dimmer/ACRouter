# 7. ACRouter Command Reference

**Version:** 1.0.0
**Date:** 2025-01-15

Complete guide for ACRouter serial terminal and REST API commands.

---

## Table of Contents

- [General Commands](#general-commands)
- [Router Control](#router-control)
- [Configuration Management](#configuration-management)
- [WiFi Network](#wifi-network)
- [Web Server](#web-server)
- [Time Synchronization](#time-synchronization)
- [MQTT Integration](#mqtt-integration)
- [OTA Firmware Updates](#ota-firmware-updates)
- [Hardware Configuration](#hardware-configuration)
- [Current Sensors](#current-sensors)
- [REST API Reference](#rest-api-reference)
- [Quick Start Examples](#quick-start-examples)
- [Notes](#notes)

---

## General Commands

### `help`

Display complete command reference.

```bash
help
```

### `status`

Show current router status including mode, state, dimmer level, and power consumption.

```bash
status
```

Example output:

```text
=== Router Status ===
Mode:    AUTO
State:   INCREASING
Dimmer:  45%
Power:   1250.3 W
Gain:    150.0
Thresh:  50.0 W
=====================
```

---

## Router Control

### `router-mode <mode>`

Set the router operating mode.

**Modes:**
- `off` or `0` - Router disabled
- `auto` or `1` - Solar Router mode (minimize grid import/export)
- `eco` or `2` - Economic mode (avoid grid import, allow export)
- `offgrid` or `3` - Offgrid mode (solar/battery autonomous)
- `manual` or `4` - Manual dimmer control
- `boost` or `5` - Maximum power routing

**Usage:**

```bash
router-mode auto          # Set auto mode
router-mode manual        # Set manual mode
router-mode               # Show current mode
```

⚠️ **Note:** Changes are saved to NVS immediately.

---

### `router-dimmer <ID> <value>`

Control specific dimmer output level.

**Parameters:**
- `ID` - Dimmer identifier: `1`, `2`, or `all`
- `value` - Power level (0-100%)

**Usage:**

```bash
router-dimmer 1 75        # Set dimmer 1 to 75%
router-dimmer all 50      # Set all dimmers to 50%
router-dimmer             # Show current level
```

⚠️ **Note:** Setting dimmer automatically switches to MANUAL mode.

---

### `router-status`

Show detailed router status (same as `status` command).

```bash
router-status
```

---

### `router-calibrate`

Run power meter calibration routine.

```bash
router-calibrate
```

🚧 **Status:** Feature under development.

---

### `debug-adc <period>`

Enable/disable debug logging output from the power meter (PowerMeterADC).

**Usage:**

```bash
debug-adc <period>            # Set debug output period
debug-adc                     # Show current settings
```

**Parameters:**

- `<period>` - Debug output period in seconds:
  - `0` - Disable debug output
  - `>0` - Enable with specified period (e.g., `5` = every 5 seconds)

**Examples:**

```bash
# Enable debug output every 5 seconds
debug-adc 5
# Output: debug-adc = 5 seconds (enabled)

# Disable debug output
debug-adc 0
# Output: debug-adc = DISABLED

# Show current settings
debug-adc
# Output: debug-adc = 5 seconds  (or DISABLED)
```

**Debug Information:**

When debug output is enabled, detailed ADC operation information will be logged:

**For voltage sensor:**

```text
VOLTAGE CH0: rms=230.5V, phase=POSITIVE (pos=12345, neg=-11234)
```

**For current sensors:**

```text
DEBUG CH1 [GPIO39] CURRENT_GRID: dc_avg=2048.3, rms_adc=145.23, vdc=0.512V, amps=25.60
  Phase: current=POSITIVE (pos=13456, neg=-12345), correlation: same=3850, diff=150 -> CONSUMING
```

**Fields:**

- `rms` - RMS voltage in volts
- `phase` - Signal phase (POSITIVE/NEGATIVE/BALANCED)
- `pos/neg` - Sum of positive/negative half-periods (for asymmetry diagnostics)
- `dc_avg` - Average DC value (should be around 2048 for 12-bit ADC)
- `rms_adc` - RMS value in ADC units
- `vdc` - Sensor output voltage (V)
- `amps` - Measured current (A)
- `same/diff` - Voltage-current phase correlation:
  - `same > diff` → CONSUMING (importing from grid)
  - `diff > same` → SUPPLYING (exporting to grid)

**Applications:**

- Sensor troubleshooting
- Verify correct current direction detection
- Analyze signal asymmetry (pos/neg sums)
- Debug sensor calibration
- Monitor measurement quality

⚠️ **Important:** Debug output creates significant load on the Serial port. Use only for diagnostics, disable after troubleshooting.

📝 **Note:** This setting is NOT saved to NVS. Debug output will be disabled after reboot.

---

## Configuration Management

All configuration commands save values to NVS (Non-Volatile Storage) immediately.

### `config-show`

Display all configuration parameters.

```bash
config-show
```

---

### `config-reset`

Reset all configuration to factory defaults.

```bash
config-reset
```

⚠️ **Warning:** This will erase all custom settings.

---

### `config-gain [value]`

Set control loop gain parameter (affects response speed).

**Range:** 1-1000 | **Default:** 150

```bash
config-gain 200           # Set gain
config-gain               # Show current gain
```

**Effect:** Higher values = faster response, lower values = more stable.

---

### `config-threshold [value]`

Set balance threshold for auto mode.

**Range:** 0-100 W | **Default:** 50 W

```bash
config-threshold 30       # Set threshold
config-threshold          # Show current threshold
```

**Effect:** Router tries to keep grid power within ±threshold of zero.

---

### `config-manual [value]`

Set default manual mode dimmer level.

**Range:** 0-100% | **Default:** 0%

```bash
config-manual 50          # Set manual level
config-manual             # Show current level
```

---

### `config-vcoef [value]`

Set voltage calibration coefficient.

**Range:** 0.1-10.0 | **Default:** 1.0

```bash
config-vcoef 1.05         # Set coefficient
config-vcoef              # Show current value
```

**Effect:** Multiplier for voltage measurements (sensor calibration).

---

### `config-icoef [value]`

Set current measurement coefficient.

**Range:** 0.1-100.0 A/V | **Default:** 30.0 A/V

```bash
config-icoef 33.0         # Set coefficient
config-icoef              # Show current value
```

**Effect:** Converts sensor voltage to current measurement.

---

### `config-ithresh [value]`

Set current detection threshold.

**Range:** 0.01-10.0 A | **Default:** 0.1 A

```bash
config-ithresh 0.15       # Set threshold
config-ithresh            # Show current value
```

**Effect:** Minimum current to detect active power flow.

---

### `config-pthresh [value]`

Set power detection threshold.

**Range:** 1-1000 W | **Default:** 10 W

```bash
config-pthresh 15         # Set threshold
config-pthresh            # Show current value
```

**Effect:** Minimum power to detect active consumption/generation.

---

## WiFi Network

### `wifi-status`

Display WiFi connection status, IP addresses, and saved credentials.

```bash
wifi-status
```

**Status Fields:**
- `IDLE` - Not initialized
- `AP_ONLY` - Access Point only
- `STA_CONNECTING` - Connecting to network
- `STA_CONNECTED` - Connected as client
- `AP+STA` - Both AP and STA active
- `STA_FAILED` - Connection failed

**Signal Strength Guide:**
- `-30 to -50 dBm` - Excellent
- `-51 to -70 dBm` - Good
- `-71 to -85 dBm` - Fair
- `-86 to -100 dBm` - Poor

---

### `wifi-scan`

Scan for available WiFi networks.

```bash
wifi-scan
```

---

### `wifi-connect <ssid> [password]`

Connect to WiFi network and save credentials to NVS.

**Parameters:**
- `ssid` - Network name (required)
  - For SSID with spaces, use double quotes: `"My Network"`
- `password` - Network password (optional for open networks)
  - For passwords with spaces, use double quotes: `"My Pass 123"`

**Usage:**

```bash
# Simple connection (no spaces)
wifi-connect MyHomeNetwork MyPassword123
wifi-connect GuestNetwork                    # For open networks

# SSID with spaces (quoted)
wifi-connect "My Home Network" MyPassword123

# Both SSID and password with spaces (both quoted)
wifi-connect "Coffee Shop WiFi" "welcome guest 2024"
```

**Examples:**

```bash
# Connect to secured network
wifi-connect MyNetwork SecurePass2024
# Output: Connecting to: MyNetwork
#         Password: ***

# Connect to network with spaces in name
wifi-connect "TP-LINK Home" MyPassword
# Output: Connecting to: TP-LINK Home
#         Password: ***

# Connect to open network
wifi-connect PublicWiFi
# Output: Connecting to: PublicWiFi
#         No password (open network)
```

**Behavior:**
1. Connects to specified network
2. On success, credentials are automatically saved to NVS
3. On next boot, router will auto-connect
4. AP mode remains active (AP+STA mode)

⚠️ **Security Note:** Password is transmitted in plaintext over serial.

---

### `wifi-disconnect`

Disconnect from current STA network (AP remains active).

```bash
wifi-disconnect
```

**Effect:** Router returns to AP-only mode. Saved credentials remain in NVS.

---

### `wifi-forget`

Clear saved WiFi credentials from NVS.

```bash
wifi-forget
```

**Effect:** Router will not auto-connect on next boot. Current connection remains active.

---

## Web Server

### `web-status`

Display web server status, access URLs, and API endpoints.

```bash
web-status
```

---

### `web-start`

Start the web server.

```bash
web-start
```

**Default Ports:**
- HTTP: 80
- WebSocket: 81

---

### `web-stop`

Stop the web server.

```bash
web-stop
```

---

### `web-urls`

Display all web interface access URLs.

```bash
web-urls
```

**Web Pages:**
- `/` - Main control interface (future)
- `/wifi` - WiFi configuration page
- `/ota` - Firmware update page

---

## Time Synchronization

### `time-status`

Display NTP time synchronization status.

```bash
time-status
```

**Default NTP Servers:**
- pool.ntp.org
- time.google.com

---

### `time-sync`

Force immediate NTP synchronization.

```bash
time-sync
```

---

## MQTT Integration

ACRouter supports MQTT for remote monitoring and control. See [11_MQTT_GUIDE.md](11_MQTT_GUIDE.md) for complete documentation.

### `mqtt-status`

Display MQTT connection status and statistics.

```bash
mqtt-status
```

**Example output:**

```text
======================================================
  MQTT Status
======================================================
State:         Connected
Enabled:       Yes
Broker:        mqtt://192.168.1.10:1883
Device ID:     039C7C
Device Name:   Solar Router
HA Discovery:  Enabled
Pub Interval:  5000 ms
------------------------------------------------------
Uptime:        125 sec
Published:     250 messages
Received:      3 messages
======================================================
```

---

### `mqtt-config`

Display all MQTT configuration settings.

```bash
mqtt-config
```

---

### `mqtt-broker <url>`

Set MQTT broker URL.

**Format:** `mqtt://host:port` or `mqtts://host:port`

```bash
mqtt-broker mqtt://192.168.1.10:1883
mqtt-broker                           # Show current broker
```

---

### `mqtt-user <username>`

Set MQTT authentication username.

```bash
mqtt-user myuser
mqtt-user                             # Show current username
```

---

### `mqtt-pass <password>`

Set MQTT authentication password.

```bash
mqtt-pass mysecretpassword
mqtt-pass                             # Show password status
```

---

### `mqtt-device-id <id>`

Set custom device ID for MQTT topics.

**Default:** Auto-generated from MAC address (last 6 chars)

```bash
mqtt-device-id kitchen_router
mqtt-device-id                        # Show current ID
```

**Note:** Changes the topic prefix from `acrouter/039C7C/...` to `acrouter/kitchen_router/...`

---

### `mqtt-device-name <name>`

Set human-readable device name (used in Home Assistant).

```bash
mqtt-device-name "Solar Router Kitchen"
mqtt-device-name                      # Show current name
```

---

### `mqtt-interval <ms>`

Set metrics publish interval.

**Range:** 1000-60000 ms | **Default:** 5000 ms

```bash
mqtt-interval 10000                   # Set to 10 seconds
mqtt-interval                         # Show current interval
```

---

### `mqtt-ha-discovery <0|1>`

Enable or disable Home Assistant MQTT Auto-Discovery.

```bash
mqtt-ha-discovery 1                   # Enable
mqtt-ha-discovery 0                   # Disable
mqtt-ha-discovery                     # Show current status
```

**Note:** When enabled, device and entities are automatically created in Home Assistant.

---

### `mqtt-enable`

Enable MQTT client and connect to broker.

```bash
mqtt-enable
```

**Note:** Broker URL must be configured first.

---

### `mqtt-disable`

Disable MQTT client and disconnect from broker.

```bash
mqtt-disable
```

---

### `mqtt-reconnect`

Force MQTT reconnection.

```bash
mqtt-reconnect
```

**Use case:** After changing broker settings or to recover from connection issues.

---

### `mqtt-publish`

Force immediate publication of all data.

```bash
mqtt-publish
```

**Use case:** Trigger immediate update of all topics without waiting for next interval.

---

## OTA Firmware Updates

ACRouter supports OTA (Over-The-Air) firmware updates from multiple sources:
- **GitHub Releases** - Automatic version checking and updates
- **Custom URL** - Direct URL to firmware binary
- **Web Upload** - Upload .bin file through web interface

### `ota-check`

Check for firmware updates on GitHub Releases.

```bash
ota-check
```

**Example output:**

```text
┌──────────────────────────────────────────────────────
│ 🎉 Update Available!
├──────────────────────────────────────────────────────
│ Latest Version:  v1.2.0
│ Release Name:    ACRouter v1.2.0 - Web UI Improvements
│ Published:       2025-12-22T10:00:00Z
│ File:            ACRouter-v1.2.0.bin (1048576 bytes)
├──────────────────────────────────────────────────────
│ Changelog:
│ ## What's New
│ - Enhanced sensor configuration
│ - Improved dashboard visuals
│ - Bug fixes and performance improvements
├──────────────────────────────────────────────────────
│ To update, run: ota-update-github
└──────────────────────────────────────────────────────
```

**Features:**
- Compares current version with GitHub latest release
- Shows changelog excerpt (first 500 characters)
- Caches results for 5 minutes to avoid rate limits

---

### `ota-update-github`

Download and install firmware from GitHub latest release.

```bash
ota-update-github
```

**Process:**
1. Fetches latest release information
2. Downloads .bin file from GitHub
3. Verifies firmware size
4. Writes to OTA partition
5. Reboots device

**Example output:**

```text
Starting OTA update from GitHub...
Downloading: https://github.com/owner/repo/releases/download/v1.2.0/ACRouter-v1.2.0.bin
Size: 1048576 bytes
Progress: 10% (104857 / 1048576 bytes)
Progress: 20% (209715 / 1048576 bytes)
...
Progress: 100% (1048576 / 1048576 bytes)
✓ Firmware written successfully
✓ Update successful! Rebooting in 3 seconds...
```

⚠️ **Requirements:**
- Internet connection
- Sufficient free space in OTA partition
- GitHub repository configured correctly

---

### `ota-update-url`

Download and install firmware from custom URL.

```bash
ota-update-url <url>
```

**Arguments:**
- `<url>` - Direct URL to firmware .bin file (HTTPS recommended)

**Example:**

```bash
ota-update-url https://example.com/firmware/ACRouter-v1.2.0.bin
```

**Use Cases:**
- Custom firmware builds
- Local development server
- Alternative hosting platforms

---

### `ota-rollback`

Rollback to previous firmware version (if available).

```bash
ota-rollback
```

**Example output:**

```text
Rolling back to previous firmware...
Current:  ota_0
Rollback: ota_1
✓ Rollback configured. Rebooting...
```

**Notes:**
- Requires dual OTA partition scheme (default)
- Only works if previous firmware still exists
- Useful for recovering from bad updates

---

### `ota-info`

Display OTA partition information and system details.

```bash
ota-info
```

**Example output:**

```text
┌──────────────────────────────────────────────────────
│ OTA Partition Information
├──────────────────────────────────────────────────────
│ Running Partition:
│   Label:   ota_0
│   Address: 0x00010000
│   Size:    1536 KB
│
│ Update Target:
│   Label:   ota_1
│   Address: 0x00190000
│   Size:    1536 KB
│
│ Free Heap:   142 KB
│ Flash Size:  4 MB
└──────────────────────────────────────────────────────
```

**Information Provided:**
- Current running partition
- Next update target partition
- Memory status
- Flash size

---

### `ota-status`

Display OTA web interface URL (legacy command, maintained for compatibility).

```bash
ota-status
```

**Web Update Process:**
1. Open OTA URL in web browser (e.g., `http://192.168.1.100/ota`)
2. Click "Check for Updates" to check GitHub
3. Or select local firmware .bin file
4. Upload and wait for completion
5. Device will automatically reboot

⚠️ **Important:** Do not power off or disconnect during update!

**Safety Features:**
- Critical tasks suspended during update
- Progress indicator shows upload status
- Automatic partition selection
- Rollback on verification failure

---

## Hardware Configuration

Commands for voltage and current sensor configuration, NVS version management, and system operations.

### Hardware Reset - `hardware-reset`

Reset hardware configuration to factory defaults (keeps NVS structure).

```bash
hardware-reset
```

**Process:**

1. Stops PowerMeterADC to prevent DMA conflicts
2. Resets all hardware settings to factory defaults
3. Saves to NVS

**Example output:**

```text
Resetting hardware configuration to factory defaults...
Stopping PowerMeterADC...
Hardware configuration reset successful
IMPORTANT: Reboot required for changes to take effect!
Use 'reboot' command to restart
```

⚠️ **Important:** Reboot required after this command (`reboot`).

---

### Voltage Sensor - `hardware-voltage-show`

Display current voltage sensor configuration.

```bash
hardware-voltage-show
```

**Example output:**

```text
========== Voltage Sensor ==========
Channel:  0
GPIO:     35
Type:     VOLTAGE_AC
Driver:   ZMPT107
Nominal VDC:  0.700 V
Multiplier:   321.43
Offset:   0.00
Status:   ENABLED
====================================
```

### Voltage Sensor - `hardware-voltage-config-type`

Set voltage sensor driver type.

```bash
hardware-voltage-config-type <type>
```

**Parameters:**
- `<type>` - Sensor type:
  - `ZMPT107` - ZMPT107 sensor (0.70V RMS nominal)
  - `ZMPT101B` - ZMPT101B sensor (1.0V RMS nominal)
  - `CUSTOM` - Custom sensor

**Example:**

```bash
hardware-voltage-config-type ZMPT107
```

### Voltage Sensor - `hardware-voltage-config-port`

Set GPIO pin for voltage sensor.

```bash
hardware-voltage-config-port GPIO<pin>
```

**Parameters:**
- `<pin>` - GPIO number (32-39 for ESP32 ADC1)

**Example:**

```bash
hardware-voltage-config-port GPIO35
```

⚠️ **Important:** Reboot required after GPIO changes (`reboot`).

### Voltage Sensor - `hardware-voltage-calibrate` ⭐

**Automatic voltage sensor calibration** with VDC measurement.

```bash
hardware-voltage-calibrate <measured_VAC>
```

**Parameters:**
- `<measured_VAC>` - Grid voltage measured with multimeter (VAC RMS)
- Range: 50-300V

**Calibration Process:**

1. Measure grid voltage with multimeter (AC RMS mode)
2. Enter command with measured value
3. System automatically:
   - Measures current sensor VDC output (RMS)
   - Calculates multiplier: `multiplier = V_measured / V_sensor`
   - Saves calibrated nominal_vdc and multiplier to NVS
4. Reboot device (`reboot`)

**Example:**

```bash
# Multimeter shows 230.5V
hardware-voltage-calibrate 230.5
```

**Output:**

```text
========== Voltage Calibration ==========
Measuring sensor VDC output...
Measured grid voltage:  230.50 V AC (from multimeter)
Measured sensor VDC:    0.814 V (auto-measured)
Calculated multiplier:  283.17
=========================================

Calibration saved successfully!
Updated:
  - Nominal VDC: 0.814 V
  - Multiplier:  283.17

IMPORTANT: Reboot required for changes to take effect!
Use 'reboot' command to restart
```

**Benefits of Automatic Calibration:**

✅ No need to manually adjust sensor potentiometer
✅ Works with any sensor output voltage
✅ Automatically updates nominal_vdc
✅ More accurate and faster

📝 **Note:** Old method (manually adjusting potentiometer to 0.7V) is no longer required!

### Voltage Sensor - `hardware-voltage-config-multiplier`

Directly set multiplier (for advanced users).

```bash
hardware-voltage-config-multiplier <value>
```

**Parameters:**
- `<value>` - Multiplier (0.1-1000)

⚠️ **Recommendation:** Use `hardware-voltage-calibrate` instead of manual multiplier setting.

---

## Current Sensors

### `hardware-current-list`

Display all configured current sensors with their bindings.

```bash
hardware-current-list
```

**Example output:**

```text
========== Configured Current Sensors ==========
[CH1] GRID       GPIO39  SCT013-50A    50.00 A/V  Offset: 0.00V
[CH2] SOLAR      GPIO36  SCT013-30A    30.00 A/V  Offset: 0.00V
[CH3] LOAD_1     GPIO34  ACS712-20A    15.15 A/V  Offset: 1.65V
================================================
```

**Fields:**

- `CH` - ADC channel number (0-3)
- Binding - Functional role (GRID, SOLAR, LOAD_1..LOAD_8)
- `GPIO` - GPIO pin (32-39)
- Sensor type - Current sensor model
- `A/V` - Calibration multiplier
- `Offset` - DC offset (for ACS712 sensors)

---

### `hardware-current-config <binding> <sensor_type> GPIO<pin>`

Configure current sensor: set type, GPIO pin and functional binding.

**Parameters:**

- `<binding>` - Functional binding:
  - `GRID` - Grid connection (import/export)
  - `SOLAR` - Solar panel
  - `LOAD_1`, `LOAD_2`, ... `LOAD_8` - Loads 1-8
- `<sensor_type>` - Sensor type:
  - **SCT-013 series** (AC current transformers, 0-1V output):
    - `SCT013-5A` - 0-5A, 1V @ 5A
    - `SCT013-10A` - 0-10A, 1V @ 10A
    - `SCT013-20A` - 0-20A, 1V @ 20A
    - `SCT013-30A` - 0-30A, 1V @ 30A
    - `SCT013-50A` - 0-50A, 1V @ 50A
    - `SCT013-60A` - 0-60A, 1V @ 60A
    - `SCT013-80A` - 0-80A, 1V @ 80A
    - `SCT013-100A` - 0-100A, 1V @ 100A
  - **ACS712 series** (Hall effect sensors, 2.5V center @ 5V):
    - `ACS712-5A` - ±5A
    - `ACS712-20A` - ±20A
    - `ACS712-30A` - ±30A
  - `CUSTOM` - Custom sensor
- `<GPIO>` - GPIO number (32-39, ADC1 only)

**Usage:**

```bash
# Configure grid sensor: SCT-013-50A on GPIO39
hardware-current-config GRID SCT013-50A GPIO39

# Configure solar panel: SCT-013-100A on GPIO36
hardware-current-config SOLAR SCT013-100A GPIO36

# Configure load 1: ACS712-20A on GPIO34
hardware-current-config LOAD_1 ACS712-20A GPIO34
```

**Example output:**

```text
========== Current Sensor Configured ==========
Binding:     GRID
Channel:     1
GPIO:        39
Driver:      SCT013-50A
Nominal:     50.0 A
Multiplier:  50.00
DC Offset:   0.00 V
===============================================

IMPORTANT: Reboot required for changes to take effect!
Use 'reboot' command to restart

NOTE: ACS712 sensors have DC bias (1.65V after divider)
Use 'hardware-current-calibrate-zero GRID' to calibrate zero point
```

**Channel selection algorithm:**

The system automatically selects ADC channel by priority:

1. **Priority 1:** Channel with matching GPIO (reconfigure existing sensor)
2. **Priority 2:** Channel with same binding type (update sensor type)
3. **Priority 3:** First free channel (type = NONE)
4. **Priority 4:** Any channel with current sensor (migration from old types)

⚠️ **Important:** Reboot required (`reboot`) for changes to take effect.

---

### `hardware-current-show <binding>`

Display detailed information about specific current sensor.

**Parameters:**

- `<binding>` - Sensor binding (`GRID`, `SOLAR`, `LOAD_1`..`LOAD_8`)

**Usage:**

```bash
hardware-current-show GRID
```

**Example output:**

```text
========== Current Sensor Configuration ==========
Binding:     GRID
Channel:     1
GPIO:        39
Driver:      SCT013-50A
Multiplier:  50.00
DC Offset:   0.00 V
Status:      ENABLED
===================================================
```

---

### `hardware-current-delete <binding>`

Delete current sensor configuration and free ADC channel.

**Parameters:**

- `<binding>` - Sensor binding (`GRID`, `SOLAR`, `LOAD_1`..`LOAD_8`)

**Usage:**

```bash
hardware-current-delete GRID
```

**Example output:**

```text
Successfully deleted sensor: GRID
  Channel: 1
  GPIO: 39
  Driver: SCT013-50A

Channel is now free and can be reassigned.
IMPORTANT: Reboot required for changes to take effect!
```

**Error (sensor not found):**

```text
ERROR: No sensor configured for binding: GRID
Use 'hardware-current-list' to see configured sensors
```

---

### `hardware-current-calibrate-zero <binding>`

Calibrate zero point of current sensor (DC offset compensation).

**Parameters:**

- `<binding>` - Sensor binding to calibrate

**When to use:**

- After installing a new sensor
- When readings drift (sensor shows current with no load)
- After adjusting potentiometer on ACS712 sensor

**Calibration process:**

1. **IMPORTANT:** Disconnect load from sensor (current = 0A)
2. Enter command
3. System measures current DC offset
4. Calculates and saves correcting offset to NVS
5. Reboot device

**Usage:**

```bash
# Disconnect load from GRID sensor
# Ensure current through sensor = 0A

hardware-current-calibrate-zero GRID
```

**Example output:**

```text
========== Zero-Point Calibration ==========
Ensure NO current is flowing through sensor!
Measuring DC offset...
NOTICE: Auto-calibration not yet implemented
PowerMeterADC automatically compensates DC offset
Manual adjustment not required in most cases
===========================================
```

📝 **Note:**

- **SCT-013:** Calibration usually not required (pure AC output)
- **ACS712:** Recommended for compensating center point drift (2.5V)
- PowerMeterADC automatically subtracts DC offset, this command compensates remaining drift

---

### NVS Version - `hw-version-show`

Display NVS data format version and safe mode status.

```bash
hw-version-show
```

**Example output (normal mode):**

```text
========== NVS Version Info ==========
Firmware version:  2
NVS version:       2
Status:            OK
======================================
```

**Example output (safe mode):**

```text
========== NVS Version Info ==========
Firmware version:  2
NVS version:       1
Status:            SAFE MODE
Reason:            NVS version mismatch (NVS: 1, Firmware: 2).
                   Use 'hw-erase-nvs' command to reset.
======================================

WARNING: PowerMeterADC is NOT initialized in safe mode!
System will continue with limited functionality:
  - WiFi and WebServer: WORKING
  - Manual dimmer control: WORKING
  - Power measurements: DISABLED
```

**Version History:**

- `v1` - Initial version (hardcoded ZMPT107 multiplier)
- `v2` - Added voltage_driver and nominal_vdc fields

📝 **Safe mode** is automatically activated on version mismatch to prevent system crashes.

### NVS Reset - `hw-erase-nvs`

Completely erase all NVS data and reset to factory defaults.

```bash
hw-erase-nvs
```

**Process:**

1. Command will ask for confirmation
2. Enter `YES` (in capital letters)
3. System erases all data from `hw_config` namespace
4. Loads factory defaults
5. Saves settings with current version

**Example:**

```bash
hw-erase-nvs
```

**Output:**

```text
WARNING: This will erase ALL hardware configuration!
Type 'YES' to confirm (timeout: 10s):
YES

Erasing NVS namespace 'hw_config'...
NVS namespace erased successfully
Factory defaults saved (version 2)
Hardware configuration reset complete!

IMPORTANT: Reboot required!
Use 'reboot' command to restart
```

⚠️ **WARNING:** This operation is irreversible! All custom sensor settings and GPIO configurations will be lost.

**When to use:**

- Upgrading to new firmware with incompatible NVS format
- Returning to factory hardware settings
- Troubleshooting configuration issues

### System Reboot - `reboot`

Restart ESP32.

```bash
reboot
```

**Output:**

```text
Rebooting in 1 second...
```

📝 **Note:** Required after:

- Changing GPIO pins
- Sensor calibration
- NVS reset
- Hardware configuration updates

---

## REST API Reference

⚠️ **Note:** This section provides a quick reference for REST API endpoints. For integration with web applications, use these endpoints along with the serial commands described above.

### Access URLs

**Access Point Mode:**
```
http://192.168.4.1/api/
```

**Station Mode:**
```
http://<router-ip>/api/
```

Use `wifi-status` command to find current IP address.

---

### Status & Monitoring

**GET /api/status** - Complete router status

```bash
curl http://192.168.4.1/api/status
```

**GET /api/metrics** - Power metrics (lightweight, for polling)

```bash
curl http://192.168.4.1/api/metrics
```

**GET /api/config** - All configuration parameters

```bash
curl http://192.168.4.1/api/config
```

**GET /api/info** - System information

```bash
curl http://192.168.4.1/api/info
```

---

### Configuration

**POST /api/config** - Update configuration

```bash
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{"control_gain": 200, "balance_threshold": 30}'
```

**POST /api/config/reset** - Reset to factory defaults

```bash
curl -X POST http://192.168.4.1/api/config/reset
```

---

### Router Control

**POST /api/mode** - Set operating mode

Valid modes: `off`, `auto`, `eco`, `offgrid`, `manual`, `boost`

```bash
# Set AUTO mode (Solar Router)
curl -X POST http://192.168.4.1/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": "auto"}'

# Set ECO mode (Economic, avoid import)
curl -X POST http://192.168.4.1/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": "eco"}'

# Set OFFGRID mode (Autonomous solar/battery)
curl -X POST http://192.168.4.1/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": "offgrid"}'
```

**POST /api/dimmer** - Set dimmer level (switches to MANUAL mode)

```bash
curl -X POST http://192.168.4.1/api/dimmer \
  -H "Content-Type: application/json" \
  -d '{"value": 75}'
```

**POST /api/manual** - Set manual level (alternative endpoint)

```bash
curl -X POST http://192.168.4.1/api/manual \
  -H "Content-Type: application/json" \
  -d '{"value": 50}'
```

---

### WiFi Management

**GET /api/wifi/status** - WiFi status and IP addresses

```bash
curl http://192.168.4.1/api/wifi/status
```

**GET /api/wifi/scan** - Scan for networks

```bash
curl http://192.168.4.1/api/wifi/scan
```

**POST /api/wifi/connect** - Connect to network (saves to NVS)

```bash
curl -X POST http://192.168.4.1/api/wifi/connect \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyNetwork", "password": "MyPassword"}'
```

**POST /api/wifi/disconnect** - Disconnect from STA network

```bash
curl -X POST http://192.168.4.1/api/wifi/disconnect
```

**POST /api/wifi/forget** - Clear saved credentials from NVS

```bash
curl -X POST http://192.168.4.1/api/wifi/forget
```

---

### Web API Usage Examples

**Python (requests):**

```python
import requests

base = "http://192.168.4.1/api"

# Get status
status = requests.get(f"{base}/status").json()
print(f"Mode: {status['mode']}, Dimmer: {status['dimmer']}%")

# Set ECO mode
requests.post(f"{base}/mode", json={"mode": "eco"})

# Update gain
requests.post(f"{base}/config", json={"control_gain": 250})

# Monitor metrics (polling)
import time
while True:
    metrics = requests.get(f"{base}/metrics").json()
    print(f"Grid: {metrics['metrics']['power_grid']}W")
    time.sleep(2)
```

**JavaScript (Fetch):**

```javascript
const base = "http://192.168.4.1/api";

// Get status
fetch(`${base}/status`)
  .then(r => r.json())
  .then(d => console.log(`Mode: ${d.mode}, Dimmer: ${d.dimmer}%`));

// Set OFFGRID mode
fetch(`${base}/mode`, {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({mode: 'offgrid'})
});

// Polling metrics every 2 seconds
setInterval(() => {
  fetch(`${base}/metrics`)
    .then(r => r.json())
    .then(d => console.log(`Grid: ${d.metrics.power_grid}W`));
}, 2000);
```

**Bash (curl + jq):**

```bash
BASE="http://192.168.4.1/api"

# Get status (formatted with jq)
curl -s "$BASE/status" | jq .

# Set AUTO mode
curl -X POST "$BASE/mode" \
  -H "Content-Type: application/json" \
  -d '{"mode": "auto"}'

# Monitor loop
while true; do
  curl -s "$BASE/metrics" | jq '.metrics'
  sleep 5
done
```

---

## Quick Start Examples

### Initial Setup

```bash
# Check status
status

# Scan for WiFi networks
wifi-scan

# Connect to WiFi (credentials saved to NVS)
wifi-connect MyNetwork MyPassword

# Check WiFi status
wifi-status

# Set router to auto mode
router-mode auto

# Adjust gain for faster response
config-gain 200
```

---

### Manual Control

```bash
# Switch to manual mode
router-mode manual

# Set dimmer to 50%
router-dimmer 1 50

# Set all dimmers to 100%
router-dimmer all 100

# Return to auto mode
router-mode auto
```

---

### Troubleshooting

```bash
# Check all systems
status
wifi-status
web-status
time-status

# Reset configuration if needed
config-reset

# Reconnect WiFi
wifi-connect MyNetwork MyPassword

# Force time sync
time-sync

# Restart web server
web-stop
web-start
```

---

## Notes

### Command Format

- Commands are case-insensitive
- Parameters are space-separated

### Data Persistence

- All `config-*` commands save to NVS immediately
- WiFi credentials saved automatically on successful connection
- Settings persist across reboots

### Safety Features

- Dimmer limits: 0-100%
- Parameter range validation
- Watchdog protection during OTA
- Critical task suspension during updates

### Web Interface

- Access via any browser
- Responsive design for mobile
- Real-time status updates
- No authentication (use firewall rules)

---

## Related Documentation

- [01_OVERVIEW.md](01_OVERVIEW.md) - Project overview and features
- [02_COMPILATION.md](02_COMPILATION.md) - Build instructions
- [03_STRUCTURE.md](03_STRUCTURE.md) - Application architecture
- [04_ROUTER_MODES.md](04_ROUTER_MODES.md) - Router operating modes explained
- [05_API_REFERENCE_EN.md](05_API_REFERENCE_EN.md) - Component API reference
- [06_MAIN_APPLICATION.md](06_MAIN_APPLICATION.md) - Main application entry point
- [07_COMMANDS.md](07_COMMANDS.md) - Russian version (Команды и REST API)

---

**Firmware Version:** 1.0.0
**Last Updated:** 2025-01-15
