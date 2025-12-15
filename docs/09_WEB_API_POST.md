# 9. Web API - POST Endpoints

**Version:** 1.0.0
**Date:** 2025-01-15

Detailed documentation for REST API POST endpoints for controlling and configuring the ACRouter system.

---

## Table of Contents

- [9.1 Introduction](#91-introduction)
- [9.2 Request Format](#92-request-format)
- [9.3 Endpoint: POST /api/config](#93-endpoint-post-apiconfig)
- [9.4 Endpoint: POST /api/config/reset](#94-endpoint-post-apiconfigreset)
- [9.5 Endpoint: POST /api/mode](#95-endpoint-post-apimode)
- [9.6 Endpoint: POST /api/dimmer](#96-endpoint-post-apidimmer)
- [9.7 Endpoint: POST /api/manual](#97-endpoint-post-apimanual)
- [9.8 Endpoint: POST /api/calibrate](#98-endpoint-post-apicalibrate)
- [9.9 Endpoint: POST /api/wifi/connect](#99-endpoint-post-apiwificonnect)
- [9.10 Endpoint: POST /api/wifi/disconnect](#910-endpoint-post-apiwifidisconnect)
- [9.11 Endpoint: POST /api/wifi/forget](#911-endpoint-post-apiwififorget)
- [9.12 Endpoint: POST /api/hardware/config](#912-endpoint-post-apihardwareconfig)
- [9.13 Endpoint: POST /api/hardware/validate](#913-endpoint-post-apihardwarevalidate)
- [9.14 Endpoint: POST /api/system/reboot](#914-endpoint-post-apisystemreboot)
- [9.15 Error Handling](#915-error-handling)
- [9.16 Usage Examples](#916-usage-examples)

---

## 9.1 Introduction

ACRouter REST API POST endpoints are designed for:

- **Router Control** - change operating mode, dimmer level
- **Configuration** - update system parameters
- **WiFi Management** - connect/disconnect from networks
- **Hardware Configuration** - configure GPIOs, sensors, dimmers
- **System Control** - reboot device

All changes are applied **immediately** and saved to **NVS** (Non-Volatile Storage).

**Implementation:** `components/comm/src/WebServerManager.cpp`

---

## 9.2 Request Format

### 9.2.1 Content-Type

All POST requests must use:

```http
Content-Type: application/json
```

### 9.2.2 Request Structure

```http
POST /api/endpoint HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "parameter1": "value1",
  "parameter2": 123
}
```

### 9.2.3 Success Response Format

**HTTP Status:** `200 OK`

```json
{
  "success": true,
  "message": "Operation completed successfully"
}
```

### 9.2.4 Error Response Format

**HTTP Status:** `400`, `500`, `501`

```json
{
  "error": "Error message description"
}
```

---

## 9.3 Endpoint: POST /api/config

Update system configuration parameters. Can update one or multiple parameters in a single request.

### 9.3.1 Request

```http
POST /api/config HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "voltage_coef": 1.02,
  "current_coef": 30.0,
  "current_threshold": 0.12,
  "power_threshold": 12.0,
  "control_gain": 180.0,
  "balance_threshold": 40.0
}
```

### 9.3.2 Parameters (optional)

All parameters are optional. Only specified fields are updated.

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `voltage_coef` | float | `0.1` .. `10.0` | Voltage calibration coefficient |
| `current_coef` | float | `0.1` .. `100.0` | Current conversion coefficient (A/V) |
| `current_threshold` | float | `0.01` .. `10.0` | Current detection threshold (A) |
| `power_threshold` | float | `1.0` .. `1000.0` | Power detection threshold (W) |
| `control_gain` | float | `1.0` .. `1000.0` | Control loop gain |
| `balance_threshold` | float | `0.0` .. `100.0` | Balance threshold for AUTO mode (W) |

### 9.3.3 Response (success)

**HTTP Status:** `200 OK`

```json
{
  "success": true,
  "message": "Configuration updated"
}
```

If no parameters were changed:

```json
{
  "success": true,
  "message": "No changes"
}
```

### 9.3.4 Response (error)

**HTTP Status:** `400 Bad Request`

```json
{
  "error": "Missing request body"
}
```

```json
{
  "error": "Invalid JSON"
}
```

### 9.3.5 Usage Examples

**Update single parameter:**

```bash
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{"control_gain": 200.0}'
```

**Update multiple parameters:**

```bash
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "control_gain": 180.0,
    "balance_threshold": 40.0,
    "voltage_coef": 1.02
  }'
```

**Python:**

```python
import requests

config = {
    "control_gain": 200.0,
    "balance_threshold": 35.0
}

response = requests.post(
    "http://192.168.4.1/api/config",
    json=config
)

print(response.json())
# {'success': True, 'message': 'Configuration updated'}
```

**JavaScript:**

```javascript
const config = {
  control_gain: 200.0,
  balance_threshold: 35.0
};

fetch('http://192.168.4.1/api/config', {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify(config)
})
  .then(r => r.json())
  .then(data => console.log(data));
```

---

## 9.4 Endpoint: POST /api/config/reset

Reset all configuration parameters to factory defaults.

### 9.4.1 Request

```http
POST /api/config/reset HTTP/1.1
Host: 192.168.4.1
```

**Request Body:** Not required

### 9.4.2 Factory Defaults

```cpp
control_gain        = 150.0
balance_threshold   = 50.0 W
voltage_coef        = 1.0
current_coef        = 30.0 A/V
current_threshold   = 0.1 A
power_threshold     = 10.0 W
```

### 9.4.3 Response (success)

**HTTP Status:** `200 OK`

```json
{
  "success": true,
  "message": "Configuration reset to defaults"
}
```

### 9.4.4 Usage Examples

**Bash:**

```bash
curl -X POST http://192.168.4.1/api/config/reset
```

**Python:**

```python
import requests

response = requests.post("http://192.168.4.1/api/config/reset")
print(response.json())
# {'success': True, 'message': 'Configuration reset to defaults'}
```

⚠️ **Warning:** This operation is **irreversible**. All custom settings will be lost.

---

## 9.5 Endpoint: POST /api/mode

Set router operating mode.

### 9.5.1 Request

```http
POST /api/mode HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "mode": "auto"
}
```

### 9.5.2 Parameters

| Parameter | Type | Required | Valid Values |
|-----------|------|----------|--------------|
| `mode` | string | Yes | `"off"`, `"auto"`, `"eco"`, `"offgrid"`, `"manual"`, `"boost"` |

**Mode Descriptions:**

| Mode | Description |
|------|-------------|
| `"off"` | Router disabled, dimmer 0% |
| `"auto"` | **Solar Router** - minimize grid import/export |
| `"eco"` | **Economic** - avoid import, allow export |
| `"offgrid"` | **Offgrid** - solar/battery, minimal grid usage |
| `"manual"` | **Manual** - fixed dimmer level |
| `"boost"` | **Maximum power** - dimmer 100% |

### 9.5.3 Response (success)

**HTTP Status:** `200 OK`

```json
{
  "success": true,
  "message": "Mode set to AUTO"
}
```

### 9.5.4 Response (error)

**HTTP Status:** `400 Bad Request`

```json
{
  "error": "Missing 'mode' field"
}
```

```json
{
  "error": "Invalid mode (use: off, auto, eco, offgrid, manual, boost)"
}
```

### 9.5.5 Usage Examples

**Set AUTO mode (Solar Router):**

```bash
curl -X POST http://192.168.4.1/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": "auto"}'
```

**Set ECO mode:**

```bash
curl -X POST http://192.168.4.1/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": "eco"}'
```

**Python - switch between modes:**

```python
import requests
import time

def set_mode(mode):
    response = requests.post(
        "http://192.168.4.1/api/mode",
        json={"mode": mode}
    )
    return response.json()

# Set AUTO mode
print(set_mode("auto"))
time.sleep(60)

# Switch to MANUAL for testing
print(set_mode("manual"))
time.sleep(30)

# Return to AUTO
print(set_mode("auto"))
```

---

## 9.6 Endpoint: POST /api/dimmer

Set dimmer level. **Automatically switches router to MANUAL mode**.

### 9.6.1 Request

```http
POST /api/dimmer HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "value": 75
}
```

### 9.6.2 Parameters

| Parameter | Type | Required | Range | Description |
|-----------|------|----------|-------|-------------|
| `value` | integer | Yes | `0` .. `100` | Dimmer level in percent |

### 9.6.3 Response (success)

**HTTP Status:** `200 OK`

```json
{
  "success": true,
  "message": "Dimmer value set"
}
```

⚠️ **Important:** Router automatically switches to **MANUAL** mode when dimmer level is set.

### 9.6.4 Response (error)

**HTTP Status:** `400 Bad Request`

```json
{
  "error": "Missing 'value' field"
}
```

```json
{
  "error": "Value must be 0-100"
}
```

### 9.6.5 Usage Examples

**Set dimmer to 50%:**

```bash
curl -X POST http://192.168.4.1/api/dimmer \
  -H "Content-Type: application/json" \
  -d '{"value": 50}'
```

**Python - smooth power ramp:**

```python
import requests
import time

def set_dimmer(value):
    response = requests.post(
        "http://192.168.4.1/api/dimmer",
        json={"value": value}
    )
    return response.json()

# Gradually increase from 0% to 100%
for level in range(0, 101, 10):
    print(f"Setting dimmer to {level}%")
    set_dimmer(level)
    time.sleep(5)

# Gradually decrease back
for level in range(100, -1, -10):
    print(f"Setting dimmer to {level}%")
    set_dimmer(level)
    time.sleep(5)
```

**JavaScript - slider control:**

```javascript
// HTML: <input type="range" id="dimmerSlider" min="0" max="100" value="0">

const slider = document.getElementById('dimmerSlider');

slider.addEventListener('change', (e) => {
  const value = parseInt(e.target.value);

  fetch('http://192.168.4.1/api/dimmer', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({value: value})
  })
    .then(r => r.json())
    .then(data => console.log(`Dimmer set to ${value}%`));
});
```

---

## 9.7 Endpoint: POST /api/manual

Alternative endpoint for setting manual mode with specified dimmer level. Equivalent to `POST /api/mode` (mode=manual) + `POST /api/dimmer`.

### 9.7.1 Request

```http
POST /api/manual HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "value": 60
}
```

### 9.7.2 Parameters

| Parameter | Type | Required | Range | Description |
|-----------|------|----------|-------|-------------|
| `value` | integer | Yes | `0` .. `100` | Dimmer level in percent |

### 9.7.3 Behavior

1. Switches router to **MANUAL** mode
2. Sets dimmer level to specified value

### 9.7.4 Response (success)

**HTTP Status:** `200 OK`

```json
{
  "success": true,
  "message": "Manual control set"
}
```

### 9.7.5 Usage Examples

**Bash:**

```bash
curl -X POST http://192.168.4.1/api/manual \
  -H "Content-Type: application/json" \
  -d '{"value": 60}'
```

**Difference between /api/dimmer and /api/manual:**

```python
import requests

# Option 1: Use /api/dimmer
# Automatically switches to MANUAL
requests.post("http://192.168.4.1/api/dimmer", json={"value": 60})

# Option 2: Use /api/manual
# Explicitly switches to MANUAL and sets level
requests.post("http://192.168.4.1/api/manual", json={"value": 60})

# Result is identical
```

---

## 9.8 Endpoint: POST /api/calibrate

Run power meter calibration routine.

### 9.8.1 Request

```http
POST /api/calibrate HTTP/1.1
Host: 192.168.4.1
```

**Request Body:** Not required

### 9.8.2 Response

**HTTP Status:** `501 Not Implemented`

```json
{
  "error": "Calibration not implemented yet"
}
```

🚧 **Status:** Feature under development. Future versions will implement automatic sensor calibration.

**Planned Features:**
- Automatic ZMPT107 calibration (voltage sensor)
- Automatic SCT-013 / ACS-712 calibration (current sensors)
- Measurement accuracy verification

---

## 9.9 Endpoint: POST /api/wifi/connect

Connect to WiFi network. Credentials are automatically saved to NVS on successful connection.

### 9.9.1 Request

```http
POST /api/wifi/connect HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "ssid": "MyHomeNetwork",
  "password": "MySecurePassword123"
}
```

### 9.9.2 Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `ssid` | string | Yes | WiFi network name (max 32 characters) |
| `password` | string | No | Network password (empty for open networks) |

### 9.9.3 Behavior

1. Disconnects from current STA network (if connected)
2. Attempts to connect to new network
3. On success:
   - Credentials are **automatically saved to NVS**
   - Obtains IP address via DHCP
   - Initializes NTP (time synchronization)
   - AP mode remains active (AP+STA mode)
4. On next boot, router will auto-connect to this network

### 9.9.4 Response (success)

**HTTP Status:** `200 OK`

```json
{
  "success": true,
  "message": "Connecting to WiFi... Credentials will be saved on success",
  "ssid": "MyHomeNetwork"
}
```

⚠️ **Important:** Response is sent immediately after connection is initiated. Actual connection takes 5-10 seconds.

### 9.9.5 Response (error)

**HTTP Status:** `400 Bad Request`

```json
{
  "error": "Missing 'ssid' field"
}
```

**HTTP Status:** `500 Internal Server Error`

```json
{
  "error": "Failed to initiate WiFi connection"
}
```

### 9.9.6 Verify Connection

After 10-15 seconds, check status:

```bash
curl -s http://192.168.4.1/api/wifi/status | jq '.sta_connected, .sta_ip'
```

### 9.9.7 Usage Examples

**Connect to secured network:**

```bash
curl -X POST http://192.168.4.1/api/wifi/connect \
  -H "Content-Type: application/json" \
  -d '{
    "ssid": "MyHomeNetwork",
    "password": "SecurePass2024"
  }'
```

**Connect to open network:**

```bash
curl -X POST http://192.168.4.1/api/wifi/connect \
  -H "Content-Type: application/json" \
  -d '{"ssid": "PublicWiFi"}'
```

**Python - connect with verification:**

```python
import requests
import time

def connect_wifi(ssid, password=None):
    """Connect to WiFi and wait for result"""

    # Send connection request
    payload = {"ssid": ssid}
    if password:
        payload["password"] = password

    response = requests.post(
        "http://192.168.4.1/api/wifi/connect",
        json=payload
    )

    if response.status_code != 200:
        print(f"Failed to initiate connection: {response.json()}")
        return False

    print(f"Connecting to {ssid}...")

    # Wait 10 seconds
    time.sleep(10)

    # Check result
    status = requests.get("http://192.168.4.1/api/wifi/status").json()

    if status['sta_connected'] and status['sta_ssid'] == ssid:
        print(f"✓ Connected to {ssid}")
        print(f"  IP: {status['sta_ip']}")
        print(f"  RSSI: {status['rssi']} dBm")
        return True
    else:
        print(f"✗ Failed to connect to {ssid}")
        return False

# Usage
connect_wifi("MyHomeNetwork", "MyPassword123")
```

⚠️ **Security:** Password is transmitted in plaintext. Use only via direct AP connection.

---

## 9.10 Endpoint: POST /api/wifi/disconnect

Disconnect from WiFi network (STA mode). Access Point (AP) remains active.

### 9.10.1 Request

```http
POST /api/wifi/disconnect HTTP/1.1
Host: 192.168.4.1
```

**Request Body:** Not required

### 9.10.2 Behavior

- Disconnects from STA network
- Switches to **AP_ONLY** mode
- **Saved credentials remain in NVS** (for auto-connect on reboot)

### 9.10.3 Response (success)

**HTTP Status:** `200 OK`

```json
{
  "success": true,
  "message": "Disconnected from WiFi network"
}
```

### 9.10.4 Usage Examples

**Bash:**

```bash
curl -X POST http://192.168.4.1/api/wifi/disconnect
```

**Python:**

```python
import requests

response = requests.post("http://192.168.4.1/api/wifi/disconnect")
print(response.json())
# {'success': True, 'message': 'Disconnected from WiFi network'}

# Check status
status = requests.get("http://192.168.4.1/api/wifi/status").json()
print(f"State: {status['state']}")  # AP_ONLY
print(f"Saved credentials: {status['has_saved_credentials']}")  # True (remain in NVS)
```

---

## 9.11 Endpoint: POST /api/wifi/forget

Clear saved WiFi credentials from NVS.

### 9.11.1 Request

```http
POST /api/wifi/forget HTTP/1.1
Host: 192.168.4.1
```

**Request Body:** Not required

### 9.11.2 Behavior

- Clears WiFi credentials from NVS
- **Current connection remains active** (if connected)
- On next boot, router will **NOT auto-connect**
- Will remain in AP_ONLY mode

### 9.11.3 Response (success)

**HTTP Status:** `200 OK`

```json
{
  "success": true,
  "message": "WiFi credentials cleared from NVS"
}
```

### 9.11.4 Response (error)

**HTTP Status:** `500 Internal Server Error`

```json
{
  "error": "Failed to clear WiFi credentials"
}
```

### 9.11.5 Usage Examples

**Bash:**

```bash
curl -X POST http://192.168.4.1/api/wifi/forget
```

**Python - complete WiFi reset:**

```python
import requests

def reset_wifi():
    """Complete WiFi reset"""

    # 1. Disconnect from network
    print("Disconnecting from WiFi...")
    requests.post("http://192.168.4.1/api/wifi/disconnect")

    # 2. Clear saved credentials
    print("Clearing saved credentials...")
    response = requests.post("http://192.168.4.1/api/wifi/forget")

    if response.json()['success']:
        print("✓ WiFi reset complete")
        print("  Router will be in AP-only mode after reboot")
    else:
        print("✗ Failed to clear credentials")

reset_wifi()
```

**Common Usage Scenario:**

```python
# Change WiFi network

# 1. Clear old credentials
requests.post("http://192.168.4.1/api/wifi/forget")

# 2. Connect to new network
requests.post("http://192.168.4.1/api/wifi/connect", json={
    "ssid": "NewNetwork",
    "password": "NewPassword"
})
```

---

## 9.12 Endpoint: POST /api/hardware/config

Update hardware configuration: ADC channels, dimmers, zero-cross, relays, LEDs.

⚠️ **Important:** Changes are applied **only after device reboot**.

### 9.12.1 Request

```http
POST /api/hardware/config HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "adc_channels": [
    {
      "gpio": 34,
      "type": 1,
      "multiplier": 320.0,
      "offset": 0.0,
      "enabled": true
    },
    ...
  ],
  "dimmer_ch1": {
    "gpio": 19,
    "enabled": true
  },
  "zerocross_gpio": 18,
  "zerocross_enabled": true,
  ...
}
```

### 9.12.2 Parameters

**ADC Channels (array of 4 elements):**

| Parameter | Type | Description |
|-----------|------|-------------|
| `adc_channels[].gpio` | integer | GPIO pin (0-39) |
| `adc_channels[].type` | integer | Sensor type: 0=NONE, 1=ZMPT107, 2=SCT013, 3=ACS712 |
| `adc_channels[].multiplier` | float | Calibration multiplier |
| `adc_channels[].offset` | float | Offset |
| `adc_channels[].enabled` | boolean | Enabled |

**Dimmers:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `dimmer_ch1.gpio` | integer | GPIO pin for dimmer 1 |
| `dimmer_ch1.enabled` | boolean | Dimmer 1 enabled |
| `dimmer_ch2.gpio` | integer | GPIO pin for dimmer 2 |
| `dimmer_ch2.enabled` | boolean | Dimmer 2 enabled |

**Zero-cross Detector:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `zerocross_gpio` | integer | GPIO pin |
| `zerocross_enabled` | boolean | Enabled |

**Relays:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `relay_ch1.gpio` | integer | GPIO pin for relay 1 |
| `relay_ch1.active_high` | boolean | Logic: true=HIGH active |
| `relay_ch1.enabled` | boolean | Relay 1 enabled |
| `relay_ch2.*` | ... | Same for relay 2 |

**LED Indicators:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `led_status_gpio` | integer | GPIO for status LED |
| `led_load_gpio` | integer | GPIO for load LED |

### 9.12.3 Validation

Configuration is automatically validated before saving:

- Check GPIO conflicts (one GPIO cannot be used twice)
- Check valid GPIO pins (0-39)
- Check sensor types

### 9.12.4 Response (success)

**HTTP Status:** `200 OK`

```json
{
  "success": true,
  "message": "Configuration saved to NVS (reboot required)"
}
```

⚠️ **Important:** Reboot is required to apply changes!

### 9.12.5 Response (validation error)

**HTTP Status:** `400 Bad Request`

```json
{
  "success": false,
  "error": "GPIO 19 conflict: used by Dimmer 1 and ADC channel 0"
}
```

### 9.12.6 Usage Examples

**Update ADC channels:**

```bash
curl -X POST http://192.168.4.1/api/hardware/config \
  -H "Content-Type: application/json" \
  -d '{
    "adc_channels": [
      {
        "gpio": 34,
        "type": 1,
        "multiplier": 320.0,
        "offset": 0.0,
        "enabled": true
      },
      {
        "gpio": 35,
        "type": 2,
        "multiplier": 30.0,
        "offset": 0.0,
        "enabled": true
      }
    ]
  }'
```

**Python - change dimmer GPIO:**

```python
import requests

config = {
    "dimmer_ch1": {
        "gpio": 21,  # Change from 19 to 21
        "enabled": True
    }
}

response = requests.post(
    "http://192.168.4.1/api/hardware/config",
    json=config
)

if response.json()['success']:
    print("Configuration saved. Rebooting device...")
    # Reboot device
    requests.post("http://192.168.4.1/api/system/reboot")
```

---

## 9.13 Endpoint: POST /api/hardware/validate

Validate hardware configuration without saving. Useful for checking before applying.

### 9.13.1 Request

```http
POST /api/hardware/validate HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "dimmer_ch1": {
    "gpio": 34,
    "enabled": true
  }
}
```

### 9.13.2 Response (configuration valid)

**HTTP Status:** `200 OK`

```json
{
  "valid": true
}
```

### 9.13.3 Response (configuration invalid)

**HTTP Status:** `200 OK`

```json
{
  "valid": false,
  "error": "GPIO 34 conflict: already used by ADC channel 0"
}
```

### 9.13.4 Usage Examples

**Python - validate before applying:**

```python
import requests

new_config = {
    "dimmer_ch1": {"gpio": 21, "enabled": True},
    "zerocross_gpio": 18
}

# 1. Validate first
validate_response = requests.post(
    "http://192.168.4.1/api/hardware/validate",
    json=new_config
)

if validate_response.json()['valid']:
    print("✓ Configuration valid, applying...")

    # 2. Apply configuration
    apply_response = requests.post(
        "http://192.168.4.1/api/hardware/config",
        json=new_config
    )

    if apply_response.json()['success']:
        print("✓ Configuration saved, rebooting...")
        requests.post("http://192.168.4.1/api/system/reboot")
else:
    print(f"✗ Invalid configuration: {validate_response.json()['error']}")
```

---

## 9.14 Endpoint: POST /api/system/reboot

Reboot device.

### 9.14.1 Request

```http
POST /api/system/reboot HTTP/1.1
Host: 192.168.4.1
```

**Request Body:** Not required

### 9.14.2 Behavior

1. Sends response to client
2. Waits 500 ms (to transmit response)
3. Waits additional 3 seconds
4. Executes reboot via `ESP.restart()`

⚠️ **Important:** Critical tasks should be stopped before reboot (planned for future versions).

### 9.14.3 Response

**HTTP Status:** `200 OK`

```json
{
  "success": true,
  "message": "Rebooting in 3 seconds..."
}
```

### 9.14.4 Usage Examples

**Bash:**

```bash
curl -X POST http://192.168.4.1/api/system/reboot
```

**Python - reboot and wait:**

```python
import requests
import time

def reboot_and_wait(url="http://192.168.4.1"):
    """Reboot device and wait for it to come back online"""

    print("Rebooting device...")
    try:
        requests.post(f"{url}/api/system/reboot", timeout=5)
    except requests.exceptions.RequestException:
        pass  # Connection may be dropped

    print("Waiting for device to reboot...")
    time.sleep(10)  # Wait 10 seconds

    # Wait until device comes back online
    for attempt in range(30):
        try:
            response = requests.get(f"{url}/api/info", timeout=2)
            if response.status_code == 200:
                info = response.json()
                print(f"✓ Device online (uptime: {info['uptime_sec']}s)")
                return True
        except requests.exceptions.RequestException:
            pass

        print(f"  Attempt {attempt + 1}/30...")
        time.sleep(2)

    print("✗ Device did not come back online")
    return False

# Usage
reboot_and_wait()
```

---

## 9.15 Error Handling

### 9.15.1 HTTP Error Codes

| Code | Status | Description |
|------|--------|-------------|
| 400 | Bad Request | Invalid request format, missing fields, invalid values |
| 500 | Internal Server Error | Internal server error (NVS error, etc.) |
| 501 | Not Implemented | Feature not implemented |

### 9.15.2 Error Examples

**Missing request body:**

```json
{
  "error": "Missing request body"
}
```

**Invalid JSON:**

```json
{
  "error": "Invalid JSON"
}
```

**Missing required field:**

```json
{
  "error": "Missing 'mode' field"
}
```

**Invalid value:**

```json
{
  "error": "Value must be 0-100"
}
```

**Validation error:**

```json
{
  "error": "GPIO 19 conflict: used by Dimmer 1 and ADC channel 0"
}
```

### 9.15.3 Error Handling in Code

**Python:**

```python
import requests

def safe_post(url, json_data):
    """Safe POST request with error handling"""
    try:
        response = requests.post(url, json=json_data, timeout=10)

        if response.status_code == 200:
            return response.json()
        else:
            error_msg = response.json().get('error', 'Unknown error')
            print(f"Error {response.status_code}: {error_msg}")
            return None

    except requests.exceptions.Timeout:
        print("Request timeout")
        return None

    except requests.exceptions.ConnectionError:
        print("Connection error")
        return None

    except Exception as e:
        print(f"Unexpected error: {e}")
        return None

# Usage
result = safe_post(
    "http://192.168.4.1/api/mode",
    {"mode": "auto"}
)

if result and result.get('success'):
    print("Success!")
else:
    print("Failed to set mode")
```

---

## 9.16 Usage Examples

### 9.16.1 Complete Router Configuration (Python)

```python
import requests
import time

class ACRouterConfig:
    def __init__(self, base_url="http://192.168.4.1"):
        self.base_url = base_url

    def set_mode(self, mode):
        """Set operating mode"""
        return requests.post(
            f"{self.base_url}/api/mode",
            json={"mode": mode}
        ).json()

    def set_dimmer(self, value):
        """Set dimmer level"""
        return requests.post(
            f"{self.base_url}/api/dimmer",
            json={"value": value}
        ).json()

    def update_config(self, **params):
        """Update configuration parameters"""
        return requests.post(
            f"{self.base_url}/api/config",
            json=params
        ).json()

    def connect_wifi(self, ssid, password=None):
        """Connect to WiFi"""
        payload = {"ssid": ssid}
        if password:
            payload["password"] = password

        response = requests.post(
            f"{self.base_url}/api/wifi/connect",
            json=payload
        ).json()

        if response['success']:
            time.sleep(10)  # Wait for connection
            status = requests.get(f"{self.base_url}/api/wifi/status").json()
            return status['sta_connected']

        return False

    def setup_solar_router(self):
        """Complete Solar Router setup"""

        # 1. Configure control parameters
        print("Configuring control parameters...")
        self.update_config(
            control_gain=180.0,
            balance_threshold=40.0
        )

        # 2. Set AUTO mode
        print("Setting AUTO mode...")
        self.set_mode("auto")

        # 3. Check status
        status = requests.get(f"{self.base_url}/api/status").json()
        print(f"Mode: {status['mode']}")
        print(f"Dimmer: {status['dimmer']}%")
        print(f"Power: {status['power_grid']}W")

# Usage
router = ACRouterConfig()
router.setup_solar_router()
```

### 9.16.2 Monitoring and Auto-Correction (Python)

```python
import requests
import time

def monitor_and_adjust():
    """Monitor power and automatically adjust mode"""

    base_url = "http://192.168.4.1"

    while True:
        # Get current status
        status = requests.get(f"{base_url}/api/status").json()

        power = status['power_grid']
        mode = status['mode']

        print(f"[{time.strftime('%H:%M:%S')}] Power: {power:+7.1f}W | Mode: {mode}")

        # If high import from grid in AUTO mode
        if mode == 'auto' and power > 500:
            print("  High import detected, increasing gain...")
            requests.post(f"{base_url}/api/config", json={"control_gain": 250.0})

        # If high export to grid
        elif mode == 'auto' and power < -500:
            print("  High export detected, switching to BOOST mode...")
            requests.post(f"{base_url}/api/mode", json={"mode": "boost"})

        time.sleep(10)

monitor_and_adjust()
```

### 9.16.3 Web Control Panel (HTML + JavaScript)

```html
<!DOCTYPE html>
<html>
<head>
    <title>ACRouter Control Panel</title>
    <style>
        .control-panel { max-width: 600px; margin: 20px auto; font-family: Arial; }
        .mode-button { padding: 10px 20px; margin: 5px; cursor: pointer; }
        .dimmer-control { width: 100%; margin: 20px 0; }
    </style>
</head>
<body>
    <div class="control-panel">
        <h2>ACRouter Control Panel</h2>

        <h3>Mode Control</h3>
        <button class="mode-button" onclick="setMode('off')">OFF</button>
        <button class="mode-button" onclick="setMode('auto')">AUTO</button>
        <button class="mode-button" onclick="setMode('eco')">ECO</button>
        <button class="mode-button" onclick="setMode('manual')">MANUAL</button>

        <h3>Dimmer Control</h3>
        <input type="range" class="dimmer-control" id="dimmer" min="0" max="100" value="0">
        <span id="dimmer-value">0%</span>

        <h3>Quick Actions</h3>
        <button onclick="resetConfig()">Reset Config</button>
        <button onclick="rebootDevice()">Reboot Device</button>
    </div>

    <script>
        const API_BASE = 'http://192.168.4.1/api';

        function setMode(mode) {
            fetch(`${API_BASE}/mode`, {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({mode: mode})
            })
            .then(r => r.json())
            .then(data => alert(data.message || data.error));
        }

        const dimmerSlider = document.getElementById('dimmer');
        const dimmerValue = document.getElementById('dimmer-value');

        dimmerSlider.addEventListener('input', (e) => {
            dimmerValue.textContent = `${e.target.value}%`;
        });

        dimmerSlider.addEventListener('change', (e) => {
            const value = parseInt(e.target.value);

            fetch(`${API_BASE}/dimmer`, {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({value: value})
            })
            .then(r => r.json())
            .then(data => alert(data.message || data.error));
        });

        function resetConfig() {
            if (confirm('Reset all configuration to defaults?')) {
                fetch(`${API_BASE}/config/reset`, {method: 'POST'})
                    .then(r => r.json())
                    .then(data => alert(data.message || data.error));
            }
        }

        function rebootDevice() {
            if (confirm('Reboot device?')) {
                fetch(`${API_BASE}/system/reboot`, {method: 'POST'})
                    .then(r => r.json())
                    .then(data => alert(data.message || data.error));
            }
        }
    </script>
</body>
</html>
```

---

## Related Documentation

- [07_COMMANDS.md](07_COMMANDS.md) - Commands Reference (RU)
- [07_COMMANDS_EN.md](07_COMMANDS_EN.md) - Commands Reference (EN)
- [08_WEB_API_GET.md](08_WEB_API_GET.md) - Web API GET endpoints (RU)
- [08_WEB_API_GET_EN.md](08_WEB_API_GET_EN.md) - Web API GET endpoints (EN)
- [09_WEB_API_POST.md](09_WEB_API_POST.md) - Web API POST endpoints (RU)
- [10_WEB_INTERFACE.md](10_WEB_INTERFACE.md) - Web interface navigation (next section)

---

**Firmware Version:** 1.0.0
**Last Updated:** 2025-01-15
