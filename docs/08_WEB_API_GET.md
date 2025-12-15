# 8. Web API - GET Endpoints

**Version:** 1.0.0
**Date:** 2025-01-15

Detailed documentation for REST API GET endpoints for monitoring and retrieving system information from ACRouter.

---

## Table of Contents

- [8.1 Introduction](#81-introduction)
- [8.2 Base URL and Authentication](#82-base-url-and-authentication)
- [8.3 Response Format](#83-response-format)
- [8.4 Endpoint: GET /api/status](#84-endpoint-get-apistatus)
- [8.5 Endpoint: GET /api/metrics](#85-endpoint-get-apimetrics)
- [8.6 Endpoint: GET /api/config](#86-endpoint-get-apiconfig)
- [8.7 Endpoint: GET /api/info](#87-endpoint-get-apiinfo)
- [8.8 Endpoint: GET /api/wifi/status](#88-endpoint-get-apiwifistatus)
- [8.9 Endpoint: GET /api/wifi/scan](#89-endpoint-get-apiwifiscan)
- [8.10 Endpoint: GET /api/hardware/config](#810-endpoint-get-apihardwareconfig)
- [8.11 HTTP Error Codes](#811-http-error-codes)
- [8.12 Usage Examples](#812-usage-examples)

---

## 8.1 Introduction

ACRouter REST API provides a set of GET endpoints for:

- **Monitoring** - retrieve current router status and power metrics
- **Configuration** - read system parameters
- **Diagnostics** - system information, WiFi status, hardware configuration

All endpoints return data in JSON format.

**Implementation:** `components/comm/src/WebServerManager.cpp`

**Quick Start:** See [07_COMMANDS_EN.md](07_COMMANDS_EN.md) section "REST API Reference" for basic usage examples.

---

## 8.2 Base URL and Authentication

### 8.2.1 Base URL

**Access Point Mode:**
```
http://192.168.4.1/api/
```

**Station Mode:**
```
http://<router-ip>/api/
```

Use serial command `wifi-status` or GET `/api/wifi/status` to find the IP address.

### 8.2.2 Authentication

⚠️ **Important:** REST API **does not require authentication**. All endpoints are accessible without password.

**Security Recommendations:**
- Use a separate WiFi network for IoT devices
- Configure firewall rules to restrict access
- Do not expose port 80 to external networks
- Basic authentication is planned for future versions

### 8.2.3 CORS

CORS (Cross-Origin Resource Sharing) is **enabled** for all endpoints.

Response header:
```
Access-Control-Allow-Origin: *
```

This allows API calls from web applications hosted on other domains.

---

## 8.3 Response Format

### 8.3.1 Successful Response

**HTTP Status:** `200 OK`

**Content-Type:** `application/json`

**Structure:**
```json
{
  "field1": "value1",
  "field2": 123,
  "field3": true
}
```

### 8.3.2 Error Response

**HTTP Status:** `400`, `404`, `500`, `501`

**Content-Type:** `application/json`

**Structure:**
```json
{
  "error": "Error message description"
}
```

**Example:**
```json
{
  "error": "Not Found"
}
```

### 8.3.3 Encoding

All responses use **UTF-8** encoding.

---

## 8.4 Endpoint: GET /api/status

Get complete router status including operating mode, controller state, grid power, and dimmer level.

### 8.4.1 Request

```http
GET /api/status HTTP/1.1
Host: 192.168.4.1
```

**Parameters:** None

### 8.4.2 Response

**HTTP Status:** `200 OK`

**JSON Schema:**

```json
{
  "mode": "string",              // Router operating mode
  "state": "string",             // Controller state
  "power_grid": 0.0,             // Grid power (W)
  "dimmer": 0,                   // Dimmer level (0-100%)
  "target_level": 0,             // Target level (0-100%)
  "control_gain": 0.0,           // Control gain
  "balance_threshold": 0.0,      // Balance threshold (W)
  "valid": true,                 // Data validity
  "uptime": 0,                   // Uptime (seconds)
  "free_heap": 0                 // Free memory (bytes)
}
```

### 8.4.3 Field Descriptions

| Field | Type | Description | Possible Values |
|-------|------|-------------|-----------------|
| `mode` | string | Router operating mode | `"off"`, `"auto"`, `"eco"`, `"offgrid"`, `"manual"`, `"boost"`, `"unknown"` |
| `state` | string | Controller state | `"idle"`, `"increasing"`, `"decreasing"`, `"at_max"`, `"at_min"`, `"error"` |
| `power_grid` | float | Grid power in Watts. **Positive** = import from grid (consumption), **Negative** = export to grid (surplus) | `-∞` .. `+∞` |
| `dimmer` | integer | Current dimmer level in percent | `0` .. `100` |
| `target_level` | integer | Target dimmer level (for AUTO, ECO, OFFGRID modes) | `0` .. `100` |
| `control_gain` | float | Control loop gain | `1.0` .. `1000.0` |
| `balance_threshold` | float | Balance threshold in Watts (for AUTO mode) | `0.0` .. `100.0` |
| `valid` | boolean | Measurement data validity. `false` = sensors not initialized or error | `true` / `false` |
| `uptime` | integer | Uptime since boot in seconds | `0` .. `∞` |
| `free_heap` | integer | Free RAM in bytes | `0` .. `∞` |

### 8.4.4 Example Response

```json
{
  "mode": "auto",
  "state": "increasing",
  "power_grid": 1250.3,
  "dimmer": 45,
  "target_level": 50,
  "control_gain": 150.0,
  "balance_threshold": 50.0,
  "valid": true,
  "uptime": 3625,
  "free_heap": 187456
}
```

**Interpretation:**
- Router in **AUTO** mode (Solar Router)
- State **INCREASING** (increasing power)
- Grid import: **1250.3 W** (consumption exceeds generation)
- Current dimmer: **45%**
- Target level: **50%** (will increase to 50%)
- Uptime: **3625 sec** (≈ 1 hour)
- Free memory: **187 KB**

### 8.4.5 Usage

**Real-time monitoring:**

```bash
# Poll every 2 seconds
while true; do
  curl -s http://192.168.4.1/api/status | jq '.power_grid, .dimmer'
  sleep 2
done
```

**Check operating mode:**

```bash
curl -s http://192.168.4.1/api/status | jq -r '.mode'
# Output: auto
```

**Python - balance monitoring:**

```python
import requests
import time

def monitor_balance():
    url = "http://192.168.4.1/api/status"
    while True:
        resp = requests.get(url).json()
        power = resp['power_grid']
        dimmer = resp['dimmer']

        if power > 0:
            print(f"IMPORT: {power:.1f}W | Dimmer: {dimmer}%")
        elif power < 0:
            print(f"EXPORT: {abs(power):.1f}W | Dimmer: {dimmer}%")
        else:
            print(f"BALANCE: 0W | Dimmer: {dimmer}%")

        time.sleep(2)

monitor_balance()
```

---

## 8.5 Endpoint: GET /api/metrics

Get lightweight power metrics for frequent polling. Returns only critical data for monitoring, minimizing response size.

### 8.5.1 Request

```http
GET /api/metrics HTTP/1.1
Host: 192.168.4.1
```

**Parameters:** None

### 8.5.2 Response

**HTTP Status:** `200 OK`

**JSON Schema:**

```json
{
  "metrics": {
    "power_grid": 0.0,      // Grid power (W)
    "dimmer": 0,            // Dimmer level (0-100%)
    "target_level": 0       // Target level (0-100%)
  },
  "timestamp": 0            // Time (milliseconds since boot)
}
```

### 8.5.3 Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `metrics.power_grid` | float | Grid power (W). Positive = import, negative = export |
| `metrics.dimmer` | integer | Current dimmer level (0-100%) |
| `metrics.target_level` | integer | Target dimmer level (0-100%) |
| `timestamp` | integer | Time since boot in milliseconds |

### 8.5.4 Example Response

```json
{
  "metrics": {
    "power_grid": -125.7,
    "dimmer": 80,
    "target_level": 80
  },
  "timestamp": 3625482
}
```

**Interpretation:**
- Grid export: **125.7 W** (generation exceeds consumption)
- Dimmer: **80%** (routing surplus to load)
- Time: **3625 seconds** since boot

### 8.5.5 Usage

**High-frequency monitoring:**

```bash
# Poll every second (minimal traffic)
while true; do
  curl -s http://192.168.4.1/api/metrics | jq '.metrics'
  sleep 1
done
```

**JavaScript - real-time chart:**

```javascript
const baseUrl = "http://192.168.4.1/api";
const chartData = {
  labels: [],
  power: [],
  dimmer: []
};

function updateMetrics() {
  fetch(`${baseUrl}/metrics`)
    .then(r => r.json())
    .then(data => {
      const time = new Date().toLocaleTimeString();
      chartData.labels.push(time);
      chartData.power.push(data.metrics.power_grid);
      chartData.dimmer.push(data.metrics.dimmer);

      // Update chart (Chart.js, D3.js, etc.)
      updateChart(chartData);
    });
}

// Update every 2 seconds
setInterval(updateMetrics, 2000);
```

**Comparison with /api/status:**

| Criteria | `/api/status` | `/api/metrics` |
|----------|---------------|----------------|
| Response size | ~250 bytes | ~80 bytes |
| Fields | 10 fields | 3 fields + timestamp |
| Use case | Complete information | Real-time monitoring |
| Polling frequency | 5-10 seconds | 1-2 seconds |

---

## 8.6 Endpoint: GET /api/config

Get all system configuration parameters saved in NVS.

### 8.6.1 Request

```http
GET /api/config HTTP/1.1
Host: 192.168.4.1
```

**Parameters:** None

### 8.6.2 Response

**HTTP Status:** `200 OK`

**JSON Schema:**

```json
{
  "control_gain": 0.0,          // Control gain
  "balance_threshold": 0.0,     // Balance threshold (W)
  "voltage_coef": 0.0,          // Voltage coefficient
  "current_coef": 0.0,          // Current coefficient (A/V)
  "current_threshold": 0.0,     // Current detection threshold (A)
  "power_threshold": 0.0,       // Power detection threshold (W)
  "router_mode": 0,             // Router mode (number)
  "manual_level": 0             // Manual mode level (0-100%)
}
```

### 8.6.3 Field Descriptions

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `control_gain` | float | `1.0` .. `1000.0` | `150.0` | Control loop gain |
| `balance_threshold` | float | `0.0` .. `100.0` | `50.0` | Balance threshold for AUTO mode (W) |
| `voltage_coef` | float | `0.1` .. `10.0` | `1.0` | Voltage calibration coefficient |
| `current_coef` | float | `0.1` .. `100.0` | `30.0` | Current conversion coefficient (A/V) |
| `current_threshold` | float | `0.01` .. `10.0` | `0.1` | Current detection threshold (A) |
| `power_threshold` | float | `1.0` .. `1000.0` | `10.0` | Active power detection threshold (W) |
| `router_mode` | integer | `0` .. `5` | `1` | Router mode: 0=OFF, 1=AUTO, 2=ECO, 3=OFFGRID, 4=MANUAL, 5=BOOST |
| `manual_level` | integer | `0` .. `100` | `0` | Dimmer level for manual mode (%) |

### 8.6.4 Example Response

```json
{
  "control_gain": 180.0,
  "balance_threshold": 40.0,
  "voltage_coef": 1.02,
  "current_coef": 30.0,
  "current_threshold": 0.12,
  "power_threshold": 12.0,
  "router_mode": 1,
  "manual_level": 50
}
```

**Interpretation:**
- Gain increased to **180** (faster response)
- Balance threshold reduced to **±40 W** (tighter balancing)
- Voltage corrected by **+2%** (sensor calibration)
- Mode: **1** (AUTO - Solar Router)
- Manual level: **50%** (when switching to MANUAL)

### 8.6.5 Usage

**Configuration backup:**

```bash
# Save configuration to file
curl -s http://192.168.4.1/api/config > acrouter_config_backup.json

# Restore later via POST /api/config
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d @acrouter_config_backup.json
```

**Python - compare configurations:**

```python
import requests
import json

def compare_configs(url1, url2):
    config1 = requests.get(f"{url1}/api/config").json()
    config2 = requests.get(f"{url2}/api/config").json()

    differences = {}
    for key in config1:
        if config1[key] != config2.get(key):
            differences[key] = {
                'device1': config1[key],
                'device2': config2.get(key)
            }

    return differences

# Compare two routers
diffs = compare_configs("http://192.168.4.1", "http://192.168.4.2")
print(json.dumps(diffs, indent=2))
```

---

## 8.7 Endpoint: GET /api/info

Get system information: firmware version, chip type, flash size, free memory, uptime.

### 8.7.1 Request

```http
GET /api/info HTTP/1.1
Host: 192.168.4.1
```

**Parameters:** None

### 8.7.2 Response

**HTTP Status:** `200 OK`

**JSON Schema:**

```json
{
  "version": "string",          // Firmware version
  "chip": "string",             // Chip type
  "flash_size": 0,              // Flash size (bytes)
  "free_heap": 0,               // Free memory (bytes)
  "uptime": 0,                  // [DEPRECATED] Uptime (sec)
  "uptime_sec": 0               // Uptime (seconds)
}
```

### 8.7.3 Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `version` | string | ACRouter firmware version (format: "X.Y.Z") |
| `chip` | string | Microcontroller type (typically "ESP32") |
| `flash_size` | integer | Flash memory size in bytes |
| `free_heap` | integer | Free RAM in bytes |
| `uptime` | integer | **[DEPRECATED]** Uptime in seconds (use `uptime_sec`) |
| `uptime_sec` | integer | Uptime since boot in seconds |

### 8.7.4 Example Response

```json
{
  "version": "1.0.0",
  "chip": "ESP32",
  "flash_size": 4194304,
  "free_heap": 189328,
  "uptime": 7234,
  "uptime_sec": 7234
}
```

**Interpretation:**
- Firmware version: **1.0.0**
- Chip: **ESP32**
- Flash: **4 MB** (4194304 bytes)
- Free RAM: **184 KB** (189328 bytes)
- Uptime: **7234 sec** (≈ 2 hours 34 seconds)

### 8.7.5 Usage

**Memory monitoring:**

```bash
# Check for memory leaks
while true; do
  free_heap=$(curl -s http://192.168.4.1/api/info | jq '.free_heap')
  echo "$(date +%T) Free heap: $free_heap bytes"
  sleep 60
done
```

**Python - low memory alert:**

```python
import requests
import time

def monitor_memory(url, threshold_kb=100):
    while True:
        info = requests.get(f"{url}/api/info").json()
        free_kb = info['free_heap'] / 1024

        if free_kb < threshold_kb:
            print(f"⚠️ WARNING: Low memory! {free_kb:.1f} KB free")
            # Send notification (email, Telegram, etc.)
        else:
            print(f"✓ Memory OK: {free_kb:.1f} KB free")

        time.sleep(300)  # Check every 5 minutes

monitor_memory("http://192.168.4.1", threshold_kb=150)
```

**Format uptime:**

```python
def format_uptime(seconds):
    days = seconds // 86400
    hours = (seconds % 86400) // 3600
    minutes = (seconds % 3600) // 60
    secs = seconds % 60

    return f"{days}d {hours}h {minutes}m {secs}s"

info = requests.get("http://192.168.4.1/api/info").json()
print(f"Uptime: {format_uptime(info['uptime_sec'])}")
# Output: Uptime: 0d 2h 34m 18s
```

---

## 8.8 Endpoint: GET /api/wifi/status

Get detailed WiFi connection status: AP/STA mode, IP addresses, SSID, signal strength, MAC address, saved credentials.

### 8.8.1 Request

```http
GET /api/wifi/status HTTP/1.1
Host: 192.168.4.1
```

**Parameters:** None

### 8.8.2 Response

**HTTP Status:** `200 OK`

**JSON Schema:**

```json
{
  "state": "string",              // WiFi state
  "ap_active": false,             // AP active
  "ap_ssid": "string",            // AP SSID
  "ap_ip": "string",              // AP IP address
  "ap_clients": 0,                // Connected clients
  "sta_connected": false,         // STA connected
  "sta_ssid": "string",           // STA SSID
  "sta_ip": "string",             // STA IP address
  "rssi": 0,                      // Signal strength (dBm)
  "has_saved_credentials": false, // Credentials saved in NVS
  "mac": "string",                // MAC address
  "hostname": "string"            // Hostname
}
```

### 8.8.3 Field Descriptions

| Field | Type | Description | Possible Values |
|-------|------|-------------|-----------------|
| `state` | string | WiFi state | `"IDLE"`, `"AP_ONLY"`, `"STA_CONNECTING"`, `"STA_CONNECTED"`, `"AP_STA"`, `"STA_FAILED"` |
| `ap_active` | boolean | Access point active | `true` / `false` |
| `ap_ssid` | string | AP name (format: `ACRouter_XXXXXX`) | - |
| `ap_ip` | string | AP IP address (usually `192.168.4.1`) | - |
| `ap_clients` | integer | Number of clients connected to AP | `0` .. `4` |
| `sta_connected` | boolean | Connected to WiFi network as client | `true` / `false` |
| `sta_ssid` | string | WiFi network name | - |
| `sta_ip` | string | IP address obtained from DHCP | - |
| `rssi` | integer | WiFi signal strength in dBm | `-100` .. `-30` |
| `has_saved_credentials` | boolean | Credentials saved in NVS | `true` / `false` |
| `mac` | string | Device MAC address (format: `XX:XX:XX:XX:XX:XX`) | - |
| `hostname` | string | Device hostname | - |

**Signal Strength Assessment (RSSI):**

| RSSI (dBm) | Quality | Description |
|------------|---------|-------------|
| -30 to -50 | Excellent | Very strong signal |
| -51 to -70 | Good | Stable connection |
| -71 to -85 | Fair | Occasional drops possible |
| -86 to -100 | Poor | Unstable connection |

### 8.8.4 Example Response (AP + STA active)

```json
{
  "state": "AP_STA",
  "ap_active": true,
  "ap_ssid": "ACRouter_A1B2C3",
  "ap_ip": "192.168.4.1",
  "ap_clients": 2,
  "sta_connected": true,
  "sta_ssid": "MyHomeNetwork",
  "sta_ip": "192.168.1.150",
  "rssi": -58,
  "has_saved_credentials": true,
  "mac": "24:6F:28:A1:B2:C3",
  "hostname": "acrouter"
}
```

**Interpretation:**
- Mode: **AP_STA** (simultaneous access point and client)
- AP active: `ACRouter_A1B2C3` at `192.168.4.1`
- **2 clients** connected to AP
- STA connected to `MyHomeNetwork` with IP `192.168.1.150`
- Signal strength: **-58 dBm** (good quality)
- Credentials saved in NVS

### 8.8.5 Example Response (AP only)

```json
{
  "state": "AP_ONLY",
  "ap_active": true,
  "ap_ssid": "ACRouter_A1B2C3",
  "ap_ip": "192.168.4.1",
  "ap_clients": 1,
  "sta_connected": false,
  "has_saved_credentials": false,
  "mac": "24:6F:28:A1:B2:C3",
  "hostname": "acrouter"
}
```

### 8.8.6 Usage

**Check network connection:**

```bash
sta_connected=$(curl -s http://192.168.4.1/api/wifi/status | jq -r '.sta_connected')

if [ "$sta_connected" = "true" ]; then
  echo "✓ Connected to WiFi"
else
  echo "✗ Not connected to WiFi"
fi
```

**Python - signal strength monitoring:**

```python
import requests
import time

def monitor_wifi_signal(url):
    while True:
        status = requests.get(f"{url}/api/wifi/status").json()

        if not status['sta_connected']:
            print("⚠️ Not connected to WiFi")
            time.sleep(10)
            continue

        rssi = status['rssi']
        ssid = status['sta_ssid']

        if rssi >= -50:
            quality = "Excellent"
        elif rssi >= -70:
            quality = "Good"
        elif rssi >= -85:
            quality = "Fair"
        else:
            quality = "Poor"

        print(f"{ssid}: {rssi} dBm ({quality})")
        time.sleep(30)

monitor_wifi_signal("http://192.168.4.1")
```

---

## 8.9 Endpoint: GET /api/wifi/scan

Scan for available WiFi networks in range.

### 8.9.1 Request

```http
GET /api/wifi/scan HTTP/1.1
Host: 192.168.4.1
```

**Parameters:** None

⚠️ **Warning:** Scanning takes **2-3 seconds**. STA connection may briefly disconnect during scan.

### 8.9.2 Response

**HTTP Status:** `200 OK`

**JSON Schema:**

```json
{
  "networks": [
    {
      "ssid": "string",         // Network name
      "rssi": 0,                // Signal strength (dBm)
      "encryption": "string",   // Encryption type
      "channel": 0              // Channel number
    }
  ],
  "count": 0                    // Number of networks found
}
```

### 8.9.3 Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `networks` | array | Array of found networks |
| `networks[].ssid` | string | WiFi network name |
| `networks[].rssi` | integer | Signal strength in dBm |
| `networks[].encryption` | string | Encryption type: `"open"` or `"secured"` |
| `networks[].channel` | integer | WiFi channel number (1-14) |
| `count` | integer | Total number of networks found |

### 8.9.4 Example Response

```json
{
  "networks": [
    {
      "ssid": "MyHomeNetwork",
      "rssi": -45,
      "encryption": "secured",
      "channel": 6
    },
    {
      "ssid": "GuestNetwork",
      "rssi": -62,
      "encryption": "open",
      "channel": 11
    },
    {
      "ssid": "Neighbor_WiFi",
      "rssi": -78,
      "encryption": "secured",
      "channel": 1
    }
  ],
  "count": 3
}
```

### 8.9.5 Example Response (no networks)

```json
{
  "networks": [],
  "count": 0
}
```

### 8.9.6 Usage

**Bash - list available networks:**

```bash
curl -s http://192.168.4.1/api/wifi/scan | jq -r '.networks[] | "\(.ssid): \(.rssi) dBm (\(.encryption))"'
```

**Output:**
```
MyHomeNetwork: -45 dBm (secured)
GuestNetwork: -62 dBm (open)
Neighbor_WiFi: -78 dBm (secured)
```

**Python - find best network:**

```python
import requests

def find_best_network(url, preferred_ssids):
    """Find best network from preferred list"""
    scan_result = requests.get(f"{url}/api/wifi/scan").json()

    best_network = None
    best_rssi = -100

    for network in scan_result['networks']:
        if network['ssid'] in preferred_ssids:
            if network['rssi'] > best_rssi:
                best_rssi = network['rssi']
                best_network = network

    return best_network

# Usage
preferred = ["HomeNetwork_2.4GHz", "HomeNetwork_5GHz", "GuestNetwork"]
best = find_best_network("http://192.168.4.1", preferred)

if best:
    print(f"Best network: {best['ssid']} ({best['rssi']} dBm)")
else:
    print("No preferred networks found")
```

**JavaScript - display network list:**

```javascript
fetch('http://192.168.4.1/api/wifi/scan')
  .then(r => r.json())
  .then(data => {
    const networkList = data.networks.map(net => {
      const signal = net.rssi >= -50 ? '▂▄▆█' :
                     net.rssi >= -70 ? '▂▄▆_' :
                     net.rssi >= -85 ? '▂▄__' : '▂___';

      const lock = net.encryption === 'secured' ? '🔒' : '🔓';

      return `${lock} ${net.ssid} ${signal} (Channel ${net.channel})`;
    });

    console.log(`Found ${data.count} networks:\n${networkList.join('\n')}`);
  });
```

---

## 8.10 Endpoint: GET /api/hardware/config

Get hardware component configuration: ADC channels, dimmers, zero-cross, relays, LEDs.

### 8.10.1 Request

```http
GET /api/hardware/config HTTP/1.1
Host: 192.168.4.1
```

**Parameters:** None

### 8.10.2 Response

**HTTP Status:** `200 OK`

**JSON Schema:**

```json
{
  "adc_channels": [
    {
      "gpio": 0,                // GPIO pin
      "type": 0,                // Sensor type (number)
      "type_name": "string",    // Sensor type name
      "multiplier": 0.0,        // Multiplier
      "offset": 0.0,            // Offset
      "enabled": false          // Enabled
    }
  ],
  "dimmer_ch1": {
    "gpio": 0,                  // GPIO pin
    "enabled": false            // Enabled
  },
  "dimmer_ch2": {
    "gpio": 0,
    "enabled": false
  },
  "zerocross_gpio": 0,          // Zero-cross detector GPIO
  "zerocross_enabled": false,   // Zero-cross enabled
  "relay_ch1": {
    "gpio": 0,
    "active_high": false,       // Logic: true=HIGH active
    "enabled": false
  },
  "relay_ch2": {
    "gpio": 0,
    "active_high": false,
    "enabled": false
  },
  "led_status_gpio": 0,         // Status LED GPIO
  "led_load_gpio": 0            // Load LED GPIO
}
```

### 8.10.3 Sensor Types (SensorType)

| Code | Name | Description |
|------|------|-------------|
| 0 | `NONE` | Not used |
| 1 | `ZMPT107` | ZMPT107 voltage sensor |
| 2 | `SCT013` | SCT-013 current sensor (transformer) |
| 3 | `ACS712` | ACS712 current sensor (Hall effect) |

### 8.10.4 Example Response

```json
{
  "adc_channels": [
    {
      "gpio": 34,
      "type": 1,
      "type_name": "ZMPT107",
      "multiplier": 320.0,
      "offset": 0.0,
      "enabled": true
    },
    {
      "gpio": 35,
      "type": 2,
      "type_name": "SCT013",
      "multiplier": 30.0,
      "offset": 0.0,
      "enabled": true
    },
    {
      "gpio": 32,
      "type": 3,
      "type_name": "ACS712",
      "multiplier": 30.0,
      "offset": 0.0,
      "enabled": true
    },
    {
      "gpio": 33,
      "type": 0,
      "type_name": "NONE",
      "multiplier": 1.0,
      "offset": 0.0,
      "enabled": false
    }
  ],
  "dimmer_ch1": {
    "gpio": 19,
    "enabled": true
  },
  "dimmer_ch2": {
    "gpio": 23,
    "enabled": false
  },
  "zerocross_gpio": 18,
  "zerocross_enabled": true,
  "relay_ch1": {
    "gpio": 15,
    "active_high": true,
    "enabled": false
  },
  "relay_ch2": {
    "gpio": 2,
    "active_high": true,
    "enabled": false
  },
  "led_status_gpio": 17,
  "led_load_gpio": 5
}
```

**Interpretation:**
- **ADC channel 0:** GPIO 34, ZMPT107 voltage sensor
- **ADC channel 1:** GPIO 35, SCT013 current sensor
- **ADC channel 2:** GPIO 32, ACS712 current sensor
- **ADC channel 3:** Not used
- **Dimmer 1:** GPIO 19 (active)
- **Dimmer 2:** GPIO 23 (disabled)
- **Zero-cross:** GPIO 18 (active)
- **Relays 1-2:** Disabled
- **Status LED:** GPIO 17
- **Load LED:** GPIO 5

### 8.10.5 Usage

**Hardware configuration diagnostics:**

```bash
curl -s http://192.168.4.1/api/hardware/config | jq '.'
```

**Python - check GPIO conflicts:**

```python
import requests

def check_gpio_conflicts(url):
    config = requests.get(f"{url}/api/hardware/config").json()

    gpio_usage = {}
    conflicts = []

    # Collect all used GPIOs
    for i, ch in enumerate(config['adc_channels']):
        if ch['enabled']:
            gpio = ch['gpio']
            if gpio in gpio_usage:
                conflicts.append(f"GPIO {gpio}: {gpio_usage[gpio]} and ADC channel {i}")
            else:
                gpio_usage[gpio] = f"ADC channel {i}"

    if config['dimmer_ch1']['enabled']:
        gpio = config['dimmer_ch1']['gpio']
        if gpio in gpio_usage:
            conflicts.append(f"GPIO {gpio}: {gpio_usage[gpio]} and Dimmer 1")
        else:
            gpio_usage[gpio] = "Dimmer 1"

    # ... check all other GPIOs

    if conflicts:
        print("⚠️ GPIO conflicts detected:")
        for conflict in conflicts:
            print(f"  - {conflict}")
    else:
        print("✓ No GPIO conflicts")

check_gpio_conflicts("http://192.168.4.1")
```

---

## 8.11 HTTP Error Codes

| Code | Status | Description | Example |
|------|--------|-------------|---------|
| 200 | OK | Successful request | All GET endpoints on success |
| 400 | Bad Request | Invalid request format | Invalid parameter |
| 404 | Not Found | Endpoint not found | GET /api/unknown |
| 500 | Internal Server Error | Internal server error | Error reading NVS |
| 501 | Not Implemented | Feature not implemented | GET /api/calibrate |

**Error Format:**

```json
{
  "error": "Error message description"
}
```

**Examples:**

```json
{
  "error": "Not Found"
}
```

```json
{
  "error": "Failed to read configuration from NVS"
}
```

---

## 8.12 Usage Examples

### 8.12.1 Status Monitoring (Python)

```python
import requests
import time
from datetime import datetime

class ACRouterMonitor:
    def __init__(self, base_url):
        self.base_url = base_url

    def get_status(self):
        return requests.get(f"{self.base_url}/api/status").json()

    def get_metrics(self):
        return requests.get(f"{self.base_url}/api/metrics").json()

    def get_config(self):
        return requests.get(f"{self.base_url}/api/config").json()

    def monitor_loop(self, interval=2):
        print("ACRouter Monitor - Press Ctrl+C to stop")
        print("-" * 60)

        try:
            while True:
                status = self.get_status()

                timestamp = datetime.now().strftime("%H:%M:%S")
                mode = status['mode'].upper()
                state = status['state']
                power = status['power_grid']
                dimmer = status['dimmer']

                # Format output
                power_str = f"{power:+7.1f}W"
                if power > 0:
                    direction = "IMPORT"
                elif power < 0:
                    direction = "EXPORT"
                else:
                    direction = "BALANCE"

                print(f"[{timestamp}] {mode:8} | {state:11} | {power_str} {direction:7} | Dimmer: {dimmer:3}%")

                time.sleep(interval)

        except KeyboardInterrupt:
            print("\nMonitoring stopped")

# Usage
monitor = ACRouterMonitor("http://192.168.4.1")
monitor.monitor_loop(interval=2)
```

### 8.12.2 Dashboard (JavaScript + HTML)

```html
<!DOCTYPE html>
<html>
<head>
    <title>ACRouter Dashboard</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .metric { display: inline-block; margin: 10px; padding: 15px; border: 1px solid #ccc; border-radius: 5px; min-width: 150px; }
        .metric-label { font-size: 12px; color: #666; }
        .metric-value { font-size: 24px; font-weight: bold; }
        .positive { color: #d32f2f; }
        .negative { color: #388e3c; }
        .zero { color: #1976d2; }
    </style>
</head>
<body>
    <h1>ACRouter Dashboard</h1>

    <div id="metrics">
        <div class="metric">
            <div class="metric-label">Mode</div>
            <div class="metric-value" id="mode">-</div>
        </div>
        <div class="metric">
            <div class="metric-label">Grid Power</div>
            <div class="metric-value" id="power">-</div>
        </div>
        <div class="metric">
            <div class="metric-label">Dimmer</div>
            <div class="metric-value" id="dimmer">-</div>
        </div>
        <div class="metric">
            <div class="metric-label">State</div>
            <div class="metric-value" id="state">-</div>
        </div>
    </div>

    <script>
        const API_BASE = "http://192.168.4.1/api";

        function updateDashboard() {
            fetch(`${API_BASE}/status`)
                .then(r => r.json())
                .then(data => {
                    // Mode
                    document.getElementById('mode').textContent = data.mode.toUpperCase();

                    // Power
                    const power = data.power_grid;
                    const powerEl = document.getElementById('power');
                    powerEl.textContent = `${power.toFixed(1)}W`;

                    if (power > 0) {
                        powerEl.className = 'metric-value positive';
                    } else if (power < 0) {
                        powerEl.className = 'metric-value negative';
                    } else {
                        powerEl.className = 'metric-value zero';
                    }

                    // Dimmer
                    document.getElementById('dimmer').textContent = `${data.dimmer}%`;

                    // State
                    document.getElementById('state').textContent = data.state;
                })
                .catch(err => console.error('Error fetching status:', err));
        }

        // Update every 2 seconds
        updateDashboard();
        setInterval(updateDashboard, 2000);
    </script>
</body>
</html>
```

### 8.12.3 Export to CSV (Bash)

```bash
#!/bin/bash

API_URL="http://192.168.4.1/api"
CSV_FILE="acrouter_metrics_$(date +%Y%m%d_%H%M%S).csv"

# CSV header
echo "timestamp,mode,state,power_grid,dimmer,target_level,free_heap" > "$CSV_FILE"

echo "Logging metrics to $CSV_FILE (Press Ctrl+C to stop)"

while true; do
    # Get data
    status=$(curl -s "$API_URL/status")

    # Extract fields
    timestamp=$(date +"%Y-%m-%d %H:%M:%S")
    mode=$(echo "$status" | jq -r '.mode')
    state=$(echo "$status" | jq -r '.state')
    power=$(echo "$status" | jq -r '.power_grid')
    dimmer=$(echo "$status" | jq -r '.dimmer')
    target=$(echo "$status" | jq -r '.target_level')
    heap=$(echo "$status" | jq -r '.free_heap')

    # Write to CSV
    echo "$timestamp,$mode,$state,$power,$dimmer,$target,$heap" >> "$CSV_FILE"

    # Display
    echo "[$(date +%H:%M:%S)] Logged: Power=${power}W, Dimmer=${dimmer}%"

    sleep 5
done
```

---

## Related Documentation

- [01_OVERVIEW.md](01_OVERVIEW.md) - Project overview
- [03_STRUCTURE.md](03_STRUCTURE.md) - Application architecture
- [07_COMMANDS.md](07_COMMANDS.md) - Commands Reference (RU)
- [07_COMMANDS_EN.md](07_COMMANDS_EN.md) - Commands Reference (EN)
- [09_WEB_API_POST.md](09_WEB_API_POST.md) - Web API POST endpoints (next section)

---

**Firmware Version:** 1.0.0
**Last Updated:** 2025-01-15
