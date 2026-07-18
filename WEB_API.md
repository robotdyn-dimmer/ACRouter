# ACRouter Web API Reference

Complete REST API documentation for ACRouter HTTP server.

---

## ⚡ v2.0 Contract Changes (2026-07) — READ FIRST

The web layer was migrated to **esp_http_server** and the hosting model changed.
These are the authoritative deltas; some per-endpoint sections below may still
describe the old behavior.

### Hosting: the UI is EXTERNAL — the device redirects
- The SPA is **NOT** hosted on the device. `GET /` returns **302** to the
  configured external web-app URL (`Location: <app_url>?device=<device-ip>`), or a
  minimal HTML stub if unset. The device is a headless JSON/MQTT API + redirect.
- **`GET /api/web/config`** → `{"app_url":"http://host:port","open":true|false}`.
- **`POST /api/web/config`** `{"app_url":"http://host:port"}` → sets the external
  app URL (redirect target **and** CORS origin). Empty `app_url` clears (open).
  Serial: `web-url set <url> | clear | show`.

### CORS: origin allowlist (was `*`)
- `Access-Control-Allow-Origin` = the configured `app_url` when set, else `*`
  (open). Applied per-response and on the OPTIONS preflight (204).

### Auth: DISABLED this release (open on LAN)
- Bearer-token auth is compiled out (`ACROUTER_AUTH_ENFORCE=0`); all writes are
  open on LAN. `GET /api/auth/check` → `200 {"authenticated":true,"enforced":false}`;
  `POST /api/auth` is a no-op. (Auth returns as a future / ESP32-tier feature.)

### Long operations are ASYNC (never block the single web task)
- **`POST /api/modules/rescan`** → **`202 {"scanning":true}`** immediately (the
  ~2.5 s I2C scan runs in a worker task). A 2nd rescan while busy → **`409
  {"error":"busy","operation":"rescan"}`**. Poll **`GET /api/modules`** — it now
  carries **`"scanning": true|false`**; when `false` the module list is fresh.
- **`GET /api/wifi/scan`** is non-blocking: kicks a background scan and returns the
  cached results immediately with **`"scanning": true|false`**. Poll until `false`.
  Response: `{"scanning":bool,"networks":[{ssid,rssi,encryption,channel}],"count":N}`.
- I2C rescan is fast now (~2.5 s, was ~20 s).

### OTA: raw binary (not multipart)
- **`POST /ota/upload`** body = **raw firmware** (`Content-Type:
  application/octet-stream`), NOT multipart form-data. Bad/empty image → 500 (no
  brick). Large OTA-over-WiFi is impractical on the ESP32-C2 (RAM/LWIP limit — the
  transfer stalls after ~one TCP window); use **serial flash** on the C2.

### ESP32-C2 client constraints
- **One request in flight** (concurrent requests wedge the C2). Serialize client
  requests (queue = 1), poll at ~1-2 s. Abandoned sockets (F5 / tab close) are
  reaped in ~10 s (TCP keepalive).

### Compile Profiles — which interfaces a build ships (v2.0 tiering)
Firmware ships in three compile-time profiles (docs/18). A build exposes **exactly one**
remote interface on the C2; the ESP32 exposes both. Discover the profile at runtime via
`GET /api/info` → `features` (`http`, `mqtt`, `ota`).

| Profile | HTTP/REST + WS | MQTT client | OTA | Notes |
|---|---|---|---|---|
| **ESP32** | ✅ | ✅ | ✅ | dual-core; both interfaces + ESP-NOW |
| **C2-HTTP** | ✅ | ❌ | ❌ | this REST API; `features.mqtt=false` |
| **C2-MQTT** | ❌ | ✅ | ❌ | **headless — NO REST API at all**; provisioned + controlled over MQTT (see MQTT Endpoints → Config-over-MQTT) |

- On **C2-MQTT there is no HTTP server** — none of the `/api/*` routes below exist. A
  client reaches the device only through the broker (telemetry topics + config-over-MQTT).
- On **C2-HTTP** the `/api/mqtt/*` routes still exist (they configure a client that is
  compiled out) but MQTT does not run; treat `features.mqtt=false` as authoritative.
- Gate UI on `features`, never on the chip string.

---

## Base URL

Access the API using the router's IP address:

**Access Point Mode:**
```
http://192.168.4.1/api/
```

**Station Mode:**
```
http://<router-ip-address>/api/
```

Use `wifi-status` terminal command or `/api/wifi/status` endpoint to find the current IP address.

---

## CORS Support

The API supports Cross-Origin Resource Sharing (CORS) for web applications.

`Access-Control-Allow-Origin` is the configured external web-app URL
(`/api/web/config`) when set, else `*` (open) — see the v2.0 changes above.

**CORS Headers:**
```
Access-Control-Allow-Origin: <app_url> | *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
Access-Control-Max-Age: 86400
```

**Preflight Requests:**
- All POST endpoints support OPTIONS preflight requests
- Browser will automatically send OPTIONS before POST/PUT/DELETE
- Server responds with 204 No Content and appropriate CORS headers

**Example from React/Fetch:**
```javascript
// Browser automatically sends OPTIONS preflight, then POST
fetch('http://192.168.1.7/api/ntp/config', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ ntp_server: 'pool.ntp.org' })
})
```

---

## Status & Monitoring Endpoints

### GET /api/status

Get complete router status including mode, state, dimmer level, and control parameters.

**Request:**
```bash
curl http://192.168.4.1/api/status
```

**Response (200 OK):**
```json
{
  "mode": "auto",
  "state": "idle",
  "power_grid": 15.3,
  "dimmer": 45,
  "dimmer_count": 1,
  "target_level": 45.2,
  "control_gain": 200.0,
  "balance_threshold": 10.0,
  "valid": true,
  "uptime": 3600,
  "free_heap": 245000,
  "i2c_active": true,
  "adc_active": true,
  "dimmerlink_count": 2
}
```

**Response Fields:**
- `mode` (string): Current operating mode - `off`, `auto`, `eco`, `offgrid`, `manual`, `boost`, `grid_limit`
- `state` (string): Control state - `idle`, `increasing`, `decreasing`, `at_max`, `at_min`, `error`
- `power_grid` (float): Grid power in watts (+ importing, - exporting)
- `dimmer` (int): Current dimmer level (0-100%)
- `target_level` (int): Internal control target level
- `control_gain` (float): Current proportional gain setting
- `balance_threshold` (float): Balance threshold in watts
- `valid` (bool): Status validity flag
- `uptime` (int): System uptime in seconds
- `free_heap` (int): Free heap memory in bytes
- `i2c_active` (bool): I2C bus / DimmerLink data path active
- `adc_active` (bool): Local ADC sensing active
- `dimmerlink_count` (int): Number of active DimmerLink devices (present only when the DimmerLink manager is initialized)

---

### GET /api/metrics

Get current power metrics (lightweight, suitable for frequent polling).

**Request:**
```bash
curl http://192.168.4.1/api/metrics
```

**Response (200 OK):**
```json
{
  "metrics": {
    "power_grid": 15.3,
    "power_solar": 480.0,
    "power_load": 1200.0,
    "voltage": 230.1,
    "current_grid": 0.07,
    "current_solar": 2.1,
    "current_load": 5.2,
    "frequency": 50.0,
    "power_factor": 0.99,
    "direction": "consuming"
  },
  "dimmers": [
    { "id": 4, "type": "i2c", "name": "Heater 1", "level": 45, "target": 45, "enabled": true, "online": true, "transitioning": false }
  ],
  "relays": [
    { "id": 0, "name": "Relay 1", "is_on": false, "enabled": true, "power_w": 1000 }
  ],
  "mode": "auto",
  "timestamp": 123456789
}
```

**Response Fields:**
- `metrics.power_grid` (float): Grid power in watts (+ importing, − exporting)
- `metrics.power_solar` (float): Solar power in watts
- `metrics.power_load` (float): Load power in watts
- `metrics.voltage` (float|null): Mains RMS voltage (V); `null` when no voltage source is providing data. **(v2.0)**
- `metrics.current_grid` / `current_solar` / `current_load` (float|null): Per-role RMS current (A) from the Sensor Hub slots; `null` when that slot has no data. **(v2.0)**
- `metrics.frequency` (float|null): Mains frequency (Hz) from any online smart module; `null` if unavailable. **(v2.0)**
- `metrics.power_factor` (float|null): Grid-role module power factor (−1..+1); `null` if unavailable. **(v2.0)**
- `metrics.direction` (string|null): Grid power direction — `"consuming"` (importing, +), `"supplying"` (exporting, −), `"balanced"` (≈0); `null` when the grid slot has no data. **(v2.0)**
- `dimmers[]` (array): Per-DimmerLink live snapshot (dashboard cards) — `id` (int, 4+), `type` ("i2c"|"espnow"), `name` (string), `level` (int 0-100), `target` (int), `enabled` (bool), `online` (bool), `transitioning` (bool). v2.0: DimmerLink outputs only (GPIO 0-3 removed).
- `relays[]` (array): Per-relay snapshot — `id` (int), `name` (string), `is_on` (bool), `enabled` (bool), `power_w` (int)
- `mode` (string): Current router mode (**lowercase**: `off`/`auto`/`eco`/`offgrid`/`manual`/`boost`/`grid_limit`)
- `timestamp` (int): Measurement timestamp (millis)

> **v2.0 single-source dashboard:** this endpoint now carries voltage/current/frequency/power-factor/direction (previously power-only), so a dashboard can poll just `/api/metrics` instead of also calling `/api/sensors`. All numeric additions are `null` when unavailable (matches the module-source null convention). Additive — old clients ignore the new fields.

---

### GET /api/config

Get all configuration parameters.

**Request:**
```bash
curl http://192.168.4.1/api/config
```

**Response (200 OK):**
```json
{
  "control_gain": 200.0,
  "balance_threshold": 10.0,
  "grid_current_limit": 16.0,
  "current_threshold": 0.1,
  "power_threshold": 5.0,
  "router_mode": 1,
  "manual_level": 0
}
```

> **v2.0:** `voltage_coef`/`current_coef` were removed — rbAmp modules are factory-calibrated and there is no on-device ADC to scale (the only sensor tuning is the CT model, `POST /api/rbamp/modules/ct-model`). They are no longer emitted here nor accepted by `POST /api/config`.

**Response Fields:**
- `control_gain` (float): Proportional control gain (10-1000)
- `balance_threshold` (float): Balance threshold in watts (0-100)
- `grid_current_limit` (float): GRID_LIMIT current cap in amps (0-100); applied live
- `current_threshold` (float): Minimum current threshold in amps (0.01-10.0)
- `power_threshold` (float): Minimum power threshold in watts (1-1000)
- `router_mode` (int): Router mode enum value (0=OFF, 1=AUTO, 2=ECO, 3=OFFGRID, 4=MANUAL, 5=BOOST, 6=GRID_LIMIT); read-only here
- `manual_level` (int): Manual mode dimmer level (0-100%); read-only here (not settable via POST /api/config)

---

### GET /api/info

Get system information.

**Request:**
```bash
curl http://192.168.4.1/api/info
```

**Response (200 OK):**
```json
{
  "version": "2.0.0",
  "chip": "ESP32-C2",
  "flash_size": 4194304,
  "free_heap": 245000,
  "uptime": 3600,
  "uptime_sec": 3600,
  "features": { "github_ota": false, "tls": false, "mqtt": false, "http": true, "ota": false }
}
```
`chip` is the real SoC model (`ESP32` | `ESP32-C2` | `ESP32-C3` | `ESP32-S3`). `features` are capability flags for semantic UI gating — **gate on the flag, not the chip string**:
- `github_ota` / `tls` — `false` on the ESP32-C2 (TLS/mbedTLS trimmed). Gate the GitHub-OTA section on `github_ota`.
- `ota` (v2.0) — OTA subsystem compiled in (`CONFIG_ACROUTER_OTA`). `false` on the C2 profiles (manual UART flash). When `false`, hide the OTA page and expect the `/api/ota/*` routes to be absent.
- `mqtt` (v2.0) — MQTT client compiled in (`CONFIG_ACROUTER_MQTT_CLIENT`). `false` on the C2-HTTP profile.
- `http` (v2.0) — HTTP/REST server compiled in (`CONFIG_ACROUTER_HTTP_SERVER`). Always `true` when you can read this endpoint (a headless C2-MQTT build has no REST at all — see **Compile Profiles**).

`features` tells a client which of the two remote interfaces (HTTP vs MQTT) and which optional subsystems this specific build ships.

**Response Fields:**
- `version` (string): Firmware version
- `chip` (string): Chip model
- `flash_size` (int): Flash memory size in bytes
- `free_heap` (int): Free heap memory in bytes
- `uptime` (int): ⚠️ Deprecated alias of `uptime_sec` (kept for backward compatibility)
- `uptime_sec` (int): System uptime in seconds

---

## Configuration Endpoints

### POST /api/config

Update configuration parameters. Only provided fields will be updated.

**Request:**
```bash
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "control_gain": 250.0,
    "balance_threshold": 15.0
  }'
```

**Request Body (JSON):**
Any combination of:
- `control_gain` (float): Proportional gain (10-1000)
- `balance_threshold` (float): Balance threshold watts (0-100)
- `voltage_coef` (float): Voltage coefficient (0.1-10.0)
- `current_coef` (float): Current coefficient A/V (0.1-100.0)
- `current_threshold` (float): Current threshold amps (0.01-10.0)
- `power_threshold` (float): Power threshold watts (1-1000)

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "Configuration updated"
}
```

**Response (400 Bad Request):**
```json
{
  "status": "error",
  "message": "Invalid JSON"
}
```

⚠️ **Note:** All changes are saved to NVS immediately and persist across reboots.

---

### POST /api/config/reset

Reset all configuration parameters to factory defaults.

**Request:**
```bash
curl -X POST http://192.168.4.1/api/config/reset
```

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "Configuration reset to defaults"
}
```

⚠️ **Warning:** This will erase all custom settings and save defaults to NVS.

---

## Router Control Endpoints

### POST /api/mode

Set router operating mode.

**Request:**
```bash
curl -X POST http://192.168.4.1/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": "auto"}'
```

**Request Body (JSON):**
```json
{
  "mode": "auto"
}
```

**Valid Modes:**
- `"off"` - Router disabled, dimmer at 0%
- `"auto"` - Solar Router mode (minimize grid import/export)
- `"eco"` - Economic mode (avoid grid import, allow export)
- `"offgrid"` - Offgrid mode (solar/battery autonomous operation)
- `"manual"` - Manual dimmer control (use `/api/dimmer` to set level)
- `"boost"` - Maximum power routing (100%)
- `"grid_limit"` - Cap grid draw at `grid_current_limit` (A). **Current-only** — works with a single current (I) module on grid, no voltage/solar. **No-export only** (no PV backfeed): uses grid current magnitude, so it must never see export. Set the cap via `POST /api/config {"grid_current_limit": <A>}` or serial `router-grid-limit <A>`.

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "Mode set to AUTO"
}
```

**Response (400 Bad Request):**
```json
{
  "status": "error",
  "message": "Invalid mode (use: off, auto, eco, offgrid, manual, boost, grid_limit)"
}
```

**Mode Details:**

**OFF Mode:**
- Dimmer: 0%
- Use case: System disabled

**AUTO Mode (Solar Router):**
- Algorithm: Minimize P_grid to zero using proportional control
- Behavior: Increases load when exporting, decreases when importing
- Sensors required: CURRENT_GRID
- Best for: Grid-connected solar systems

**ECO Mode (Economic):**
- Algorithm: Avoid grid import, allow export (conservative)
- Behavior: Only reduces load when importing, never increases on export
- Response: Slower than AUTO (gain × 1.5)
- Sensors required: CURRENT_GRID
- Best for: Systems where grid export is profitable or desired

**OFFGRID Mode (Autonomous):**
- Algorithm: Use 80% of available solar power
- Behavior: Autonomous operation without grid sensor
- Sensors required: CURRENT_SOLAR, CURRENT_LOAD
- Best for: Off-grid systems with solar/battery

**MANUAL Mode:**
- Dimmer: User-defined fixed level (set via `/api/dimmer`)
- Best for: Testing, manual override

**BOOST Mode:**
- Dimmer: 100% (maximum power)
- Best for: Maximum heating/load

---

### POST /api/dimmer

Set dimmer level and switch to MANUAL mode.

**Request:**
```bash
curl -X POST http://192.168.4.1/api/dimmer \
  -H "Content-Type: application/json" \
  -d '{"value": 75}'
```

**Request Body (JSON):**
```json
{
  "value": 75
}
```

**Parameters:**
- `value` (int): Dimmer level (0-100%)

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "Dimmer value set"
}
```

**Response (400 Bad Request):**
```json
{
  "status": "error",
  "message": "Value must be 0-100"
}
```

⚠️ **Note:** `POST /api/dimmer` sets the **manual level only** — it does **not** change the router mode. For the level to take effect, switch to `MANUAL` first via `POST /api/mode {"mode":"manual"}`.

---

### POST /api/manual

Set manual level and switch to MANUAL mode (alternative endpoint).

**Request:**
```bash
curl -X POST http://192.168.4.1/api/manual \
  -H "Content-Type: application/json" \
  -d '{"value": 50}'
```

**Request Body (JSON):**
```json
{
  "value": 50
}
```

**Parameters:**
- `value` (int): Manual level (0-100%)

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "Manual control set"
}
```

---

### POST /api/calibrate

Run power meter calibration routine.

**Request:**
```bash
curl -X POST http://192.168.4.1/api/calibrate
```

**Response (501 Not Implemented):**
```json
{
  "error": "Calibration not implemented yet"
}
```

🚧 **Status:** Stub — handler always returns 501. Feature under development.

---

## WiFi Management Endpoints

### GET /api/wifi/status

Get WiFi connection status, IP addresses, and saved credentials.

**Request:**
```bash
curl http://192.168.4.1/api/wifi/status
```

**Response (200 OK):** a **flat** object (not nested):
```json
{
  "state": "connected",
  "ap_active": true,
  "ap_ssid": "ACRouter_XXXX",
  "ap_ip": "192.168.4.1",
  "ap_clients": 1,
  "sta_connected": true,
  "sta_ssid": "MyHomeNetwork",
  "sta_ip": "192.168.1.100",
  "rssi": -65,
  "has_saved_credentials": true,
  "mac": "AA:BB:CC:DD:EE:FF",
  "hostname": "acrouter"
}
```

**Response Fields:**
- `state` (string): overall WiFi state
- `ap_active` (bool): Access Point active
- `ap_ssid` (string): AP network name (`ACRouter_XXXX` — last 2 bytes of the MAC)
- `ap_ip` (string): AP IP address
- `ap_clients` (int): number of connected AP clients
- `sta_connected` (bool): connected to a WiFi network
- `sta_ssid` (string): connected network name
- `sta_ip` (string): station IP address
- `rssi` (int): signal strength in dBm (−30 excellent, −100 poor)
- `has_saved_credentials` (bool): credentials stored in NVS
- `mac` (string): device MAC address
- `hostname` (string): device hostname

**Signal Strength Guide:**
- `-30 to -50 dBm` - Excellent
- `-51 to -70 dBm` - Good
- `-71 to -85 dBm` - Fair
- `-86 to -100 dBm` - Poor

---

### GET /api/wifi/scan

Scan for available WiFi networks.

**Request:**
```bash
curl http://192.168.4.1/api/wifi/scan
```

**Response (200 OK):**
```json
{
  "networks": [
    {
      "ssid": "MyHomeNetwork",
      "rssi": -45,
      "encryption": "WPA2",
      "channel": 6
    },
    {
      "ssid": "GuestNetwork",
      "rssi": -78,
      "encryption": "Open",
      "channel": 11
    }
  ],
  "count": 2
}
```

**Response Fields:**
- `networks[]` (array): List of discovered networks
  - `ssid` (string): Network name
  - `rssi` (int): Signal strength in dBm
  - `encryption` (string): Security type (Open, WEP, WPA, WPA2, WPA3)
  - `channel` (int): WiFi channel (1-13)
- `count` (int): Number of networks found

---

### POST /api/wifi/connect

Connect to WiFi network and save credentials to NVS.

**Request:**
```bash
curl -X POST http://192.168.4.1/api/wifi/connect \
  -H "Content-Type: application/json" \
  -d '{
    "ssid": "MyHomeNetwork",
    "password": "MyPassword123"
  }'
```

**Request Body (JSON):**
```json
{
  "ssid": "MyHomeNetwork",
  "password": "MyPassword123"
}
```

**Parameters:**
- `ssid` (string): Network name (required)
- `password` (string): Network password (optional for open networks)

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "Connected to MyHomeNetwork",
  "ip": "192.168.1.100"
}
```

**Response (400 Bad Request):**
```json
{
  "status": "error",
  "message": "Connection failed"
}
```

**Behavior:**
1. Connects to specified network
2. On success, credentials are automatically saved to NVS
3. On next boot, router will auto-connect
4. AP mode remains active (AP+STA mode)

⚠️ **Security Note:** Password is transmitted in plaintext. Use HTTPS in production or configure via serial terminal in secure environment.

---

### POST /api/wifi/disconnect

Disconnect from current station network (AP remains active).

**Request:**
```bash
curl -X POST http://192.168.4.1/api/wifi/disconnect
```

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "Disconnected from WiFi"
}
```

**Effect:** Router returns to AP-only mode. Saved credentials remain in NVS.

---

### POST /api/wifi/forget

Clear saved WiFi credentials from NVS.

**Request:**
```bash
curl -X POST http://192.168.4.1/api/wifi/forget
```

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "WiFi credentials forgotten"
}
```

**Effect:** Router will not auto-connect on next boot. Current connection remains active until disconnect or reboot.

---

## Common Response Helpers

Most write endpoints use shared helpers (`components/comm/src/WebServerManager.cpp`):

- **Success:** `{ "success": true, "message": "<text>" }` at `200 OK`.
- **Error:** `{ "error": "<text>" }` at the relevant code (`400`/`500`/`501`/`503`).
- **Request bodies** are JSON read from the raw body; a missing body returns `400 {"error":"Missing request body"}`, malformed JSON returns `400 {"error":"Invalid JSON"}`.
- All responses include the CORS headers described above.

> ⚠️ **Response-envelope inconsistency (v1 vs v2):** v1 endpoints (`/api/mode`, `/api/dimmer`, `/api/manual`, `/api/config`) and the v2 groups below use `{success, message}` / `{error}`. A few older sections of this doc historically showed `{status:"ok"}` — the firmware emits `{success:true}`. Treat `success` (bool) as authoritative.

---

## Hardware Configuration Endpoints

Hardware topology (sensors, dimmers, relays, zero-cross, LEDs). See also `HARDWARE_CONFIG.md`.

### GET /api/hardware/config

Current hardware configuration. Only configured devices are included (slots with `type=NONE` are skipped).

**Response (200 OK):**
```json
{
  "dimmers": [
    { "id": 0, "type": "GPIO", "interface": "GPIO", "enabled": true, "name": "Heater 1", "gpio": 19,
      "nominal_power_w": 2000, "current_sensor_id": 80, "current_sensor_a": 1.21, "curve": 0,
      "min_level": 0, "max_level": 100, "default_level": 0, "ramp_time_ms": 1000, "priority": 0 }
  ],
  "relays": [
    { "id": 0, "type": "GPIO", "interface": "GPIO", "enabled": false, "name": "Relay 1", "gpio": 15,
      "active_high": true, "nominal_power_w": 1000, "current_sensor_id": -1, "current_sensor_a": null,
      "min_on_time_s": 0, "min_off_time_s": 0, "priority": 0 }
  ],
  "system": { "zerocross_gpio": 18, "zerocross_enabled": true, "led_status_gpio": 17, "led_load_gpio": 5 }
}
```

**Notes (v2.0):**
- 🔻 **`sensors[]` is fully removed** (was the internal-ADC list). Sensing is commissioned per module via `/api/rbamp/modules`, `/api/espnow/nodes`, and listed at `/api/sensors`.
- `current_sensor_id` (int8): v2.0 = **rbAmp module I2C address** (e.g. `80`=0x50), or `-1` = none. (Was an ADC channel index in v1.)
- `current_sensor_a` (float|null): the resolved **live RMS current** (A) of that module, or `null` if unassigned / module not currently discovered.

### POST /api/hardware/config

Replace hardware configuration. All fields optional.
> 🔻 **`adc_channels[]` is ignored (v2.0)** — silently accepted for backward compat but not applied (ADC pipeline removed). Configure sensing via `POST /api/rbamp/modules` / `POST /api/espnow/nodes`.

**Request body:**
```json
{
  "dimmer_ch1": { "gpio": 19, "enabled": true },
  "dimmer_ch2": { "gpio": 23, "enabled": false },
  "zerocross_gpio": 18, "zerocross_enabled": true,
  "relay_ch1": { "gpio": 15, "active_high": true, "enabled": false },
  "relay_ch2": { "gpio": 2,  "active_high": true, "enabled": false },
  "led_status_gpio": 17, "led_load_gpio": 5
}
```

**Responses:** `200 {success:true, message:"Configuration saved to NVS (reboot required)"}` · `400 {success:false, error:"<validation msg>"}` on validation failure · `500 {error:"Failed to save configuration to NVS"}`.
**Side effect:** writes to NVS; **reboot required to apply**.

### POST /api/hardware/validate

Validate a hardware config. **200** `{ "valid": true }` or `{ "valid": false, "error": "<msg>" }`.
> 🚧 **Stub:** the request body is currently read but **not applied** — validation runs against a default config. Do not rely on field-level validation yet.

> 🔻 **Removed in v2.0** (ADC clean cut): `GET /api/hardware/sensor-profiles`, `/sensor-types`, `/voltage-drivers`, `/current-drivers`. Smart modules are self-calibrated, so driver/type catalogs are gone. Use `/api/sensors` (below) for the live source list and `/api/rbamp/modules` + `/api/espnow/nodes` for commissioning.

### GET /api/sensors

Unified read-only list of all active measurement sources (rbAmp I2C modules + ESP-NOW nodes), so a client can render one list instead of merging two shapes. Commissioning stays per-source (`POST /api/rbamp/modules` with `{addr,role}`, `POST /api/espnow/nodes` with `{mac,role}`).

**200:**
```json
{
  "sources": [
    { "source": "i2c",    "id": "0x50",              "role": "grid",  "online": true,
      "voltage": 230.1, "current": 1.21, "power": 278.0, "power_factor": 0.99, "frequency": 50.0 },
    { "source": "espnow", "id": "E8:DB:84:3D:42:E8", "role": "solar", "online": true,
      "voltage": null, "current": 1.21, "power": null, "power_factor": 0.0, "frequency": 0.0 }
  ]
}
```
- `source` (string): discriminator `i2c` | `espnow`.
- `id` (string): I2C address (`"0x50"`) for `i2c`, MAC (`"AA:BB:..."`) for `espnow`.
- `channel` (int): per-channel index within the module (0 for single-channel).
- `role` ∈ `grid|solar|load|voltage|none`; `online` (bool); plus the last primary-channel snapshot (`voltage` V, `current` A, `power` W signed, `power_factor`, `frequency` Hz). Unavailable numeric fields (e.g. voltage on a current-only module) may be `null`.
- Empty `sources` when both `CONFIG_ACROUTER_RBAMP_SOURCE` and `CONFIG_ACROUTER_ESPNOW_SOURCE` are off / no modules present.

---

## Dimmer Control Endpoints (DimmerLink)

**v2.0 (DimmerLink-only):** dimmers are **DimmerLink** smart modules over I2C (or ESP-NOW),
with output **id ∈ 4..11 (I2C)** / **12+ (ESP-NOW)** — `DIMMER_I2C_START = 4`; the legacy
on-chip GPIO dimmer ids 0–3 are **removed** (reserved-empty). A DimmerLink becomes a dimmer
output when its module role is set to `dimmer` (`POST /api/modules/role`) — the firmware
auto-binds it to the next free I2C output id (the first is **id 4**). The dimmer's **level is
driven by the router mode** (BOOST / MANUAL / AUTO / …), not by a per-dimmer HTTP route.

### GET /api/dimmers/status

Full per-dimmer config + state (feeds the Settings page). **200:** `{ "initialized": bool, "enabled_count": int, "mains_frequency": int, "dimmers": [ { "id": int, "type": "i2c"|"espnow", "addr": "0x50" (i2c only), "bus": int (i2c only), "mac": "aa:bb:.." (espnow only), "name": string, "enabled": bool, "online": bool, "power_w": int, "curve": uint8, "level": int, "target": int, "initialized": bool, "transitioning": bool, "priority": int } ] }` — DimmerLink outputs only (I2C/ESP-NOW); GPIO 0-3 no longer serialized.

> The lightweight live shape for the dashboard is **`GET /api/metrics` → `.dimmers[]`**:
> `{ id, type, name, level, target, enabled, online, transitioning }` (no config fields).
> `GET /api/status` carries `dimmer` (real primary-output %) + `dimmer_count`.

### GET|POST /api/dimmers/all-on · /api/dimmers/all-off

Set all enabled dimmers to 100% / 0%. **200:** `{ "success": true, "dimmers": [ { "dimmer_id": int, "level": 100|0, "success": bool } ] }`.

> 🔻 **Removed in v2.0** (legacy per-GPIO-dimmer routes): `GET|POST /api/dimmers/{0-3}/level`
> and `GET|POST /api/dimmers/{0-3}/config`. Dimmer output is now driven via router modes
> (`POST /api/mode`, `POST /api/manual`); per-dimmer name/power/priority are set at commissioning
> (serial `hw-dimmer-*` / auto-bound on role assignment). Read live level from `/api/dimmers/status`
> or `/api/metrics.dimmers[]`.

---

## Relay Control Endpoints

Up to `RELAY_MAX_COUNT` relays; **`id` ∈ 0..3**.

### GET /api/relays/status

**200:** `{ "relays": [ { "id": int, "name": string, "enabled": bool, "type": string, "gpio": int, "active_high": bool, "power_w": int, "min_on": int, "min_off": int, "is_on": bool, "initialized": bool, "state": string, "priority": int } ], "initialized": true, "enabled_count": int, "on_count": int, "total_power_w": int }`.

### GET|POST /api/relays/all-on · /api/relays/all-off

All-on respects debounce; all-off forces off. **200:** `{ "success": true, "relays": [ { "relay_id": int, "state": "on"|"off"|"debounce", "success": bool } ] }`.

### GET|POST /api/relays/{id}/on · /api/relays/{id}/off

**Optional body:** `{ "force": bool }` (default false; `on` only).
**200:** `{ "relay_id": int, "state": "on"|"off", "success": true }`.
**400:** "Invalid relay ID" or debounce `{ "relay_id": int, "state": "debounce", "success": false, "error": "Debounce active" }`. **500** on failure.

### POST /api/relays/{id}/config

**Body** (all optional): `{ "name": string, "gpio": int8, "power_w": uint16, "min_on": uint16, "min_off": uint16, "active_high": bool, "enabled": bool, "priority": uint8 }`.
**200:** `{ "success": true, "message": "Relay configuration saved" }`. **Side effect:** NVS save.

---

## MQTT Endpoints

See `docs/11_MQTT_GUIDE.md`. The MQTT **password is never returned** by GET.

### GET /api/mqtt/status

**200:** `{ "enabled": bool, "connected": bool, "state": int, "broker": string, "device_id": string, "uptime": uint, "messages_published": uint, "messages_received": uint, "last_error": string }`.

### GET /api/mqtt/config

**200:** `{ "enabled": bool, "broker": string, "username": string, "device_id": string, "device_name": string, "publish_interval": uint, "ha_discovery": bool }`.

### POST /api/mqtt/config

**Body** (all optional): `{ "broker": string, "username": string, "password": string, "device_id": string, "device_name": string, "publish_interval": uint32, "ha_discovery": bool, "enabled": bool }`.
**200:** `{ "success": true, "message": "MQTT configuration saved" | "No changes" }`. **Side effects:** NVS save; if `enabled` and not connected → auto-connect.

### POST /api/mqtt/reconnect

**200:** `{ "success": true, "message": "Reconnection initiated" }`. **400** "MQTT is disabled".

### POST /api/mqtt/publish

Force-publish all topics now. **200:** `{ "success": true, "message": "Data published" }`. **400** "MQTT not connected".

---

## Config over MQTT (v2.0, docs/18 §7.1)

The **provisioning + control contract for the headless C2-MQTT profile**, which has no
HTTP API. Also available on any build with MQTT (ESP32). All topics hang off the device
base `acrouter/<device_id>` (same base as the telemetry topics). This is what a browser
Remote-UI drives over a broker WebSocket listener (see below).

### `<base>/config/set` — inbound (client → device)
Apply a whole-config JSON blob. The device applies it live **and persists to NVS**
(survives reboot, and survives a re-flash to another profile), then republishes
`config/state`. Payload:
```json
{
  "control": { "control_gain": 333, "balance_threshold": 15, "grid_current_limit_a": 16 },
  "modules": [ { "addr": "0x51", "channel": 0, "role": "grid", "name": "Grid CT" } ],
  "dimmers": [ { "id": 12, "priority": 2, "nominal_power_w": 1500, "name": "Boiler" } ]
}
```
All three sections are optional; send only what changes. `control` persists via
ConfigManager; `modules[]` role/name persist via the device registry; `dimmers[]`
priority/power/name update the dimmer map. Keep the blob within one MQTT buffer (v1 has
no fragment reassembly).

### `<base>/config/state` — outbound, **retained** (device → client)
The current whole-config, published **retained** so a client that subscribes reads it
immediately without a write. Published: **on MQTT connect** (so a fresh browser sees the
config at once), after every `config/set`, and on `config/get`.
```json
{ "control": { "control_gain": 333, "balance_threshold": 15, "grid_current_limit_a": 16 } }
```
> **v1 scope:** `config/state` currently carries only `control{}`. Reflecting current
> `modules[]` / `dimmers[]` / `ct_model` state here is v1.1 — read those via the usual
> telemetry/registry path for now.

### `<base>/config/get` — inbound (client → device)
Empty payload. Triggers an immediate `config/state` republish.

> **Latency note (C2-MQTT):** on the single-core ESP32-C2 the `config/state` republish can
> lag `config/set` by seconds to a minute under load (inbound servicing + outbox drain),
> even though the device applies the change synchronously. The value is applied and
> persisted regardless; snappier republish is a tracked v1.1 refinement.

### Broker WebSocket (browser Remote-UI)
The device speaks MQTT over **TCP 1883**. A browser client (mqtt.js) needs a broker
**WebSocket** listener — add to mosquitto: `listener 9001` + `protocol websockets`, then
connect `ws://<broker>:9001`. Use `wss://` (broker TLS) if the UI is served over HTTPS.

---

## NTP / Time Endpoints

### GET /api/ntp/status

**200:** `{ "running": bool, "synced": bool, "current_time": string, "last_sync_ago_sec": uint, "sync_interval_sec": 3600 }` (`current_time`/`last_sync_ago_sec` present only when synced).

### GET /api/ntp/config

**200:** `{ "ntp_server": string, "timezone": string, "gmt_offset_sec": int, "daylight_offset_sec": int }`.

### POST /api/ntp/config

**Body** (optional): `{ "ntp_server": string, "timezone": string, "gmt_offset_sec": int, "daylight_offset_sec": int }`. (`gmt_offset_sec`/`daylight_offset_sec` are applied together with `timezone`.)
**200:** `{ "success": true, "message": "NTP configuration updated and saved" }`. **400** "No valid configuration fields provided". **Side effect:** NVS save.

### POST /api/ntp/sync

**200:** `{ "success": true, "message": "NTP sync requested" }`. **Side effect:** forces a sync.

---

## OTA (GitHub) Endpoints

See `OTA` page. Update flashes firmware and reboots.

### GET /api/ota/check-github

Blocking check against GitHub Releases. **Always 200.**
- Update available: `{ "success": true, "update_available": true, "current_version": string, "latest_version": string, "release_name": string, "published_at": string, "changelog": string, "asset_name": string, "asset_url": string, "asset_size": int, "is_prerelease": bool }`.
- Up to date: `{ "success": true, "update_available": false, "current_version": string, "latest_version": string, "message": string }`.
- Failure: `{ "success": false, "error": "Failed to check for updates..." }`.

### POST /api/ota/update-github

**Body:** `{ "url": string (required) }` (firmware asset URL). Missing body/url → `400`.
**200 (sent before flashing):** `{ "success": true, "message": "OTA update started. Device will reboot after download." }`.
**Side effects:** downloads + flashes, then reboots (no further HTTP response). No second response on failure.

---

## Test Harness Endpoints (v2.0, HTTP builds)

Tier-0 synthetic-measurement injection — exercise the control logic (AUTO/ECO/GRID_LIMIT
surplus routing) with **no AC and no real sensors**. Present only on builds with the HTTP
server; intended for bench/CI, not production UI.

### POST /api/sim/inject

Post one synthetic `ACROUTER_EVENT_POWER_UPDATE`. Body:
```json
{ "role": "grid", "current": 9.0, "voltage": 230.0, "power": -2000.0, "latch": false }
```
- `role` (required): `grid` | `solar` | `load` | `voltage`. For `grid|solar|load`, `current` (A) is required; `voltage`/`power` optional (`power` signed: + import / − export). For `voltage`, `voltage` (V) carries the shared reference.
- `latch` (optional, default `false`): when `true`, the measurement is re-posted internally at **5 Hz** from a timer, sustaining the stream independent of HTTP request rate (so a load test measures control starvation, not injection throttling). Latch one slot per role.
- **200:** `{ "success": true, "message": "sim measurement injected" }` (or "…latched…").

### POST /api/sim/stop

Clear all latched sim measurements and stop the 5 Hz re-post timer. **200:** `{ "success": true, "message": "sim latches cleared" }`.

---

## System Endpoints

### GET|POST /api/system/reboot

**200:** `{ "success": true, "message": "Rebooting..." }`. **Side effect:** device restarts ~800 ms after the response.

---

## I2C Bus Endpoints

### GET /api/i2c/status

**200:** `{ "bus": 0, "initialized": bool, "speed_hz": int, "dimmerlink_enabled": int, "dimmerlink_active": int }` (`dimmerlink_*` present only when the DimmerLink manager is initialized).

### GET /api/i2c/scan

Scan bus 0 (max 16 results). **200:** `{ "bus": 0, "devices": [ { "addr": "0x40", "addr_dec": 64 } ], "count": int }`. **503** "I2C bus not initialized".

---

## DimmerLink Endpoints

DimmerLink smart dimmer modules over I2C. Up to `DL_MAX_DEVICES` (8) slots; **`slot` ∈ 0..7**. ACRouter is a **consumer** of the DimmerLink protocol — protocol semantics are owned upstream by the DimmerLink project.

### GET /api/dimmerlink/devices

**200:** `{ "devices": [ { "slot": int, "enabled": bool, "addr": "0x10", "bus": int, "role": string, "name": string, "online": bool, "last_poll_ms": uint, "error_count": int } ] }` (address/role/etc. present only for enabled slots).
**Roles:** `current_grid` · `current_solar` · `current_load` · `voltage` · `dimmer` · `relay` · `temperature` · `none`.

### POST /api/dimmerlink/devices

Register / update a slot.
**Body:** `{ "slot": uint8 (required, 0-7), "addr": uint8 (def DL_DEFAULT_ADDR), "bus": uint8 (def 0), "enabled": bool (def true), "role": string (def "none"), "name": string (def "") }`.
**200:** `{ "success": true, "message": "Device registered" }`. **400** "Invalid JSON" / "slot out of range (0-7)". **500** `{ "error": "<esp_err name>" }`. **Side effect:** registers device + NVS save.

### GET /api/dimmerlink/{slot}/status

Live measurement for one slot (registered per slot 0..7).
**200:** `{ "slot": int, "name": string, "role": string, "online": bool, "current": { "rms_ma": int, "rms_a": float, "peak_ma": int, "direction": int, "crest_factor": float, "period_idx": int, "duration_ms": int }, "voltage_rms_v": float, "thermal": { "temp_c": float, "state": int, "max_level": int } }`. Optional blocks (`current`/`voltage_rms_v`/`thermal`) appear only when that data is valid/available. **404** "Slot not configured".

---

## rbAmp Sensor Endpoints (v2.0)

rbAmp sensor-module discovery + role commissioning (the v2.0 sensing path). A module's role maps its primary channel to a Sensor Hub slot; live merged values are read from `/api/sensors/hub`. Gated by firmware `CONFIG_ACROUTER_RBAMP_SOURCE` (returns empty when the feature is off / no modules).

### GET /api/rbamp/modules

Discovered modules (from the fleet) + persisted role assignments.
**200:**
```json
{
  "alive": 1,
  "modules": [
    { "addr": "0x50", "role": "grid", "ct_model": "SCT-013-030", "channels": 1,
      "has_voltage": false, "online": true, "voltage": null, "current": 0.0,
      "power": null, "power_factor": null, "frequency": 50.0 }
  ],
  "roles": [
    { "addr": "0x50", "role": "grid" }
  ]
}
```
- `alive` (int): modules discovered on the bus at init.
- `modules[]`: live fleet identity + last snapshot (primary channel):
  - `addr` (hex string), `role` (`grid|solar|load|voltage|none`), `ct_model` (string, configured CT model, e.g. `SCT-013-030`), `channels` (1..3), `has_voltage` (bool, voltage HW detected at begin; false without AC), `online` (bool, read OK last cycle).
  - `voltage` (V), `current` (A), `power` (W, signed: + import / − export), `power_factor` (−1..+1), `frequency` (Hz). **`null` = unavailable** (e.g. voltage/power/PF are `null` on current-only modules or without AC).
- `roles[]`: persisted address→role commissioning (may include addresses not currently online).

> This is the source of per-module **frequency** and **power_factor** for the dashboard (system frequency = any online module's `frequency`). `/api/sensors/hub` remains the merged routing view (value + power per slot).

### POST /api/rbamp/modules

Assign (commission) a role to a module address; persisted to NVS immediately.
**Request body:** `{ "addr": "0x50", "role": "grid" }` — `addr` accepts a hex string (`"0x50"`) or integer; `role` ∈ `grid|solar|load|voltage|none` (`none` removes the mapping).
**200:** `{ "success": true, "message": "Role saved" }`. **400** invalid JSON / addr out of range (0x08-0x77) / invalid role. **500** on set failure (table full).

### POST /api/rbamp/rescan

Re-scan the I2C bus for rbAmp modules added after boot (autoscan otherwise runs only at init). Executed by the poll task between cycles (race-free).
**200:** `{ "success": true, "message": "Rescan requested" }`. **500** if the source isn't initialized.
**501** `"Bus rescan not supported on this build"` when `CONFIG_ACROUTER_I2C_AUTODISCOVERY` is off (default on the ESP32-C2 — the pause+reprobe stalls the single-core web server; the module set is fixed from the boot scan). Handle `{success:false}` / 501 by hiding or disabling the rescan control.

### POST /api/rbamp/modules/address

Change an rbAmp module's I2C address (two-phase commit). **Async** — the reassign runs in the poll task (~1-2 s: prepare + commit + reset + on-bus verify); the module re-appears at the new address in `GET /api/rbamp/modules`. Any role mapping migrates with it and is persisted.
**Request body:** `{ "addr": "0x50", "new_addr": "0x52" }` — both accept hex string or integer.
**202 Accepted:** `{ "success": true, "pending": true, "new_addr": "0x52", "message": "applies after reset; module re-appears at new_addr" }`.
**400** new_addr out of 0x08-0x77 / == addr / bad JSON. **404** no module at `addr`. **409** target address already a known rbAmp, or a change already pending.
> Note: a cross-type collision (new_addr occupied by a non-rbAmp, e.g. a DimmerLink) is caught asynchronously by the library — the request returns 202 but the module stays put (observe via discovery).

### POST /api/dimmerlink/devices/address

Change a DimmerLink device's I2C address. Stages `DL_REG_I2C_ADDRESS` (0x30) + issues `DL_CMD_RESET`; the legacy firmware latches on reset and the device re-enumerates at the new address. Registered device config migrates + persists.
**Request body:** `{ "addr": "0x50", "new_addr": "0x51" }` — hex string or integer.
**202 Accepted:** `{ "success": true, "pending": true, "new_addr": "0x51", "message": "applies after reset; device re-appears at new_addr" }`.
**400** new_addr out of range / == addr / bad JSON. **409** target address already in use.

> Address convention: DimmerLink default **0x50**, rbAmp fresh-device default **0x51**.

### GET /api/rbamp/ct-models

SCT-013 CT-model catalog (firmware source of truth). Used to populate the per-module CT picker.
**200:** array of `{ "id": "sct013-030", "name": "SCT-013-030", "rated_a": 30, "code": 3, "available": true }`.
> `available:false` (60 A / 100 A) = the code exists but its preset is unimplemented in v1.3 firmware — applying it is rejected. Disable/hide these in the picker. Selectable: 5/10/20/30/50 A.

### POST /api/rbamp/modules/ct-model

Set an rbAmp module's SCT-013 CT model on channel 0. **Verify-then-set** (writes only if the applied code differs). Async — the change runs in the poll task; the module's `ct_model` updates in `GET /api/rbamp/modules`.
**Request body:** `{ "addr": "0x51", "ct_model": "sct013-030" }` — `addr` hex string or integer; `ct_model` = a catalog `id`.
**202 Accepted:** `{ "success": true, "pending": true, "ct_model": "sct013-030", "message": "applies shortly; re-writing the same model is skipped to protect gain cal" }`.
**400** unknown ct_model / bad JSON. **404** no module at `addr`. **409** ct_model not available on this firmware, or a change already pending.
> ⚠ Changing the CT model reloads the preset gain and **overwrites the module's per-unit factory gain calibration** (v1.3). Re-selecting the same model is a no-op. `GET /api/rbamp/modules` now includes `"ct_model": "<id>" | null` (null = not configured).

> Equivalent serial console: `rbamp-config <addr_hex> <role>`, `rbamp-status`, `rbamp-rescan`, `rbamp-address <cur> <new>`, `dl-address <cur> <new>`, `rbamp-ct-model <addr> <code>`.

---

## Unified Modules Endpoints (v2.0 device-registry)

Transport-agnostic device inventory (I2C now, ESP-NOW later). Backs the single Bus/Modules view (see MODULE_ARCHITECTURE.md). ADDITIVE-EVOLVING — dimmer (multi-ch) + ESP-NOW fields are added additively.

### GET /api/modules
Unified inventory of all discovered modules.
**200:** `{ "modules": [ { "transport":"i2c", "bus":0, "addr":"0x51", "family":"rbAmp", "channels":1, "online":true, "uid":"<24hex>", "roles":["grid"], "valid_roles":["none","grid","solar","load","voltage"] }, … ] }`.
- `family` ∈ `rbAmp|rbDimmer|DimmerLink(legacy)|legacy-sensor|unknown`. `uid` = 96-bit chip serial (v1.3+ only). `roles[]` = per-channel assigned role. `online:false` = seen before but missing from the last scan (config kept).
- 🔴 `valid_roles[]` = the roles allowed for THIS family — the UI must offer only these (roles are family-specific, NOT one flat list): sensors → `none|grid|solar|load|voltage`; dimmers (rbDimmer/DimmerLink) → `dimmer` (output); relay → `relay`.

### POST /api/modules/rescan
On-demand quiescent scan (pauses polling) + **non-destructive reconcile**: match transport+addr → keep config; new → add; missing → mark offline + keep (never deletes/overwrites).
**200:** `{ "success": true, "count": N }`.

### POST /api/modules/role
Assign a per-channel role via the registry (the role source of truth; bridges to the sensing pipeline). Supersedes `POST /api/rbamp/modules` for the unified view.
**Request body:** `{ "addr":"0x51", "channel":0, "role":"grid" }` — role ∈ `grid|solar|load|voltage|dimmer|relay|none`.
**200:** `{ "success": true, "message": "Role saved" }`. **404** no module at addr. **400** invalid channel.

> Re-address in the unified view uses the per-family endpoints (`POST /api/rbamp/modules/address`, `POST /api/dimmerlink/devices/address`), selected by `family`.
> Equivalent serial console: `dev-scan [bus]`, `dev-list`, `dev-identify <bus> <addr>`, `dev-role <addr> <ch> <role>`.

---

## ESP-NOW Node Endpoints (v2.0)

Wireless rbAmp nodes over ESP-NOW → Sensor Hub (source=ESPNOW). Nodes appear automatically when they transmit; assign a role to feed the routing loop. Gated by firmware `CONFIG_ACROUTER_ESPNOW_SOURCE`.

### GET /api/espnow/nodes

**200:**
```json
{
  "seen": 1,
  "nodes": [
    { "mac": "AA:BB:CC:DD:EE:FF", "role": "solar", "online": true,
      "voltage": 0.0, "current": 0.0, "power": 0.0, "power_factor": 0.0, "frequency": 50.0 }
  ]
}
```
- `seen` (int): nodes heard since start.
- `nodes[]`: `mac` (string), `role` (`grid|solar|load|voltage|none`), `online` (bool, REALTIME within ~2 s), plus the last primary-channel snapshot (`voltage` V, `current` A, `power` W signed, `power_factor`, `frequency` Hz).

### POST /api/espnow/nodes

Assign a role to a node by MAC; persisted to NVS.
**Body:** `{ "mac": "AA:BB:CC:DD:EE:FF", "role": "solar" }` — `role` ∈ `grid|solar|load|voltage|none`.
**200:** `{ "success": true, "message": "Node role saved" }`. **400** invalid JSON / mac / role.

> Equivalent serial console: `espnow-config <mac> <role>` and `espnow-status`.

---

## Sensor Hub Endpoint

Merged multi-source measurements (ADC + I2C/DimmerLink + future ESP-NOW/MQTT). `SENSOR_HUB_SLOTS` = 4.

### GET /api/sensors/hub

**200:** `{ "merge_count": int, "i2c_active": bool, "adc_active": bool, "slots": { "voltage": <slot>, "grid": <slot>, "solar": <slot>, "load": <slot> } }` where `<slot>` = `{ "valid": bool, "value": float, "source": "none"|"adc"|"i2c"|"espnow"|"mqtt"|"unknown", "priority": int, "power_w": float }` (`power_w` only when the slot carries power).

---

## Error Responses

All endpoints may return error responses with standard format:

**400 Bad Request:**
```json
{
  "status": "error",
  "message": "Invalid JSON"
}
```

**404 Not Found:**
```json
{
  "status": "error",
  "message": "Not Found"
}
```

**500 Internal Server Error:**
```json
{
  "status": "error",
  "message": "Internal server error"
}
```

**501 Not Implemented:**
```json
{
  "status": "error",
  "message": "Feature not implemented"
}
```

---

## Usage Examples

### Python Example (requests library)

```python
import requests

# Base URL
base_url = "http://192.168.4.1/api"

# Get status
response = requests.get(f"{base_url}/status")
status = response.json()
print(f"Mode: {status['mode']}, Dimmer: {status['dimmer']}%")

# Set AUTO mode
requests.post(f"{base_url}/mode", json={"mode": "auto"})

# Update configuration
requests.post(f"{base_url}/config", json={
    "control_gain": 250.0,
    "balance_threshold": 15.0
})

# Set manual dimmer level
requests.post(f"{base_url}/dimmer", json={"value": 75})

# WiFi scan
response = requests.get(f"{base_url}/wifi/scan")
networks = response.json()["networks"]
for net in networks:
    print(f"{net['ssid']}: {net['rssi']} dBm")

# Connect to WiFi
requests.post(f"{base_url}/wifi/connect", json={
    "ssid": "MyNetwork",
    "password": "MyPassword"
})
```

### JavaScript Example (Fetch API)

```javascript
const baseUrl = "http://192.168.4.1/api";

// Get status
fetch(`${baseUrl}/status`)
  .then(response => response.json())
  .then(data => {
    console.log(`Mode: ${data.mode}, Dimmer: ${data.dimmer}%`);
  });

// Set ECO mode
fetch(`${baseUrl}/mode`, {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({mode: 'eco'})
})
  .then(response => response.json())
  .then(data => console.log(data.message));

// Update configuration
fetch(`${baseUrl}/config`, {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({
    control_gain: 250.0,
    balance_threshold: 15.0
  })
})
  .then(response => response.json())
  .then(data => console.log(data.message));

// Get metrics (polling every 2 seconds)
setInterval(() => {
  fetch(`${baseUrl}/metrics`)
    .then(response => response.json())
    .then(data => {
      console.log(`Grid: ${data.metrics.power_grid}W, Dimmer: ${data.metrics.dimmer}%`);
    });
}, 2000);
```

### Bash Example (curl)

```bash
#!/bin/bash
BASE_URL="http://192.168.4.1/api"

# Get status
curl -s "$BASE_URL/status" | jq .

# Set OFFGRID mode
curl -X POST "$BASE_URL/mode" \
  -H "Content-Type: application/json" \
  -d '{"mode": "offgrid"}'

# Update control gain
curl -X POST "$BASE_URL/config" \
  -H "Content-Type: application/json" \
  -d '{"control_gain": 250.0}'

# Set the manual dimmer level to 50% (does NOT switch mode — use /api/mode for MANUAL)
curl -X POST "$BASE_URL/dimmer" \
  -H "Content-Type: application/json" \
  -d '{"value": 50}'

# WiFi status
curl -s "$BASE_URL/wifi/status" | jq .

# Connect to WiFi
curl -X POST "$BASE_URL/wifi/connect" \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyNetwork", "password": "MyPassword"}'

# Monitoring loop (every 5 seconds)
while true; do
  curl -s "$BASE_URL/metrics" | jq '.metrics'
  sleep 5
done
```

---

## CORS Support

All API endpoints support Cross-Origin Resource Sharing (CORS), allowing requests from web applications hosted on different domains.

---

## Rate Limiting

No rate limiting is currently implemented. Use reasonable polling intervals:
- Status monitoring: 1-5 seconds
- Metrics polling: 1-2 seconds
- Configuration changes: On-demand only

---

## WebSocket Support (Planned)

Real-time telemetry updates via WebSocket will be available on port 81:

```
ws://192.168.4.1:81/
```

🚧 **Status:** WebSocket implementation is in development.

---

## Best Practices

### Monitoring
- Use `/api/metrics` for frequent polling (lightweight)
- Use `/api/status` for complete status snapshots
- Implement exponential backoff on connection errors
- Cache `/api/config` and only refresh when needed

### Control
- Always check response status before assuming success
- Validate mode changes with GET `/api/status`
- Use `/api/mode` for mode switching
- Use `/api/dimmer` for manual control
- Avoid rapid mode switching (allow settling time)

### Configuration
- Save configuration changes one at a time for easier troubleshooting
- Verify changes with GET `/api/config`
- Document custom settings for backup/restore

### WiFi
- Use `/api/wifi/status` to verify connectivity before control operations
- Scan networks before connecting to verify SSID availability
- Always save credentials for auto-reconnect on reboot

---

## Security Considerations

⚠️ **Important Security Notes:**

1. **No Authentication**: API endpoints have no authentication. Anyone with network access can control the router.

2. **Password Transmission**: WiFi passwords are transmitted in plaintext over HTTP.

3. **Network Isolation**: Deploy on isolated networks or use firewall rules to restrict access.

4. **Recommendations**:
   - Use firewall rules to limit API access to trusted devices
   - Configure WiFi credentials via serial terminal in secure environment
   - Consider VPN or SSH tunnel for remote access
   - Monitor access logs for unauthorized access

---

## Troubleshooting

### Cannot Connect to API

**Check network connection:**
```bash
ping 192.168.4.1
```

**Verify web server is running:**
Use terminal command: `web-status`

**Check IP address:**
Use terminal command: `wifi-status` or GET `/api/wifi/status`

### API Returns 404

**Verify endpoint URL:**
- All API endpoints start with `/api/`
- Check HTTP method (GET vs POST)
- Use `web-urls` terminal command to list all endpoints

### Mode Changes Don't Work

**Check current mode:**
```bash
curl http://192.168.4.1/api/status | jq .mode
```

**Verify sensor configuration:**
- AUTO/ECO modes require CURRENT_GRID sensor
- OFFGRID mode requires CURRENT_SOLAR sensor

**Check error messages in serial log**

### Configuration Changes Not Saved

**Verify NVS is working:**
Use terminal command: `config-show`

**Check free NVS space:**
Use terminal command: `status`

---

**Firmware Version:** 1.0.0
**Last Updated:** 2025-01-15
