# 7. ACRouter Command Reference

Complete guide for ACRouter serial terminal and REST API commands.

---

## Table of Contents

- [General Commands](#general-commands)
- [Router Control](#router-control)
- [Configuration Management](#configuration-management)
- [WiFi Network](#wifi-network)
- [Web Server](#web-server)
- [Time Synchronization](#time-synchronization)
- [OTA Firmware Updates](#ota-firmware-updates)
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
- `password` - Network password (optional for open networks)

```bash
wifi-connect MyHomeNetwork MyPassword123
wifi-connect GuestNetwork                    # For open networks
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

## OTA Firmware Updates

### `ota-status`

Display OTA update status and access URLs.

```bash
ota-status
```

**Update Process:**
1. Open OTA URL in web browser
2. Select firmware binary file (.bin)
3. Upload and wait for completion
4. Device will automatically reboot

⚠️ **Important:** Do not power off or disconnect during update!

**Safety Features:**
- Critical tasks suspended during update
- Watchdog protection
- Rollback on failure

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
