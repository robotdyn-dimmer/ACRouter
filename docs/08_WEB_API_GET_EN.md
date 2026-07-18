[← Terminal Commands](https://www.rbdimmer.com/acrouter-terminal-commands) | [Contents](https://www.rbdimmer.com/acrouter-what-is) | [Next: Web API - POST →](https://www.rbdimmer.com/acrouter-web-api-post)

# Web API — GET Endpoints

Read-only endpoints for status, metrics, configuration, and device inventory. For control/write
endpoints see [Web API - POST](https://www.rbdimmer.com/acrouter-web-api-post).

## 8.1 Before You Start (v2.0)

These rules apply to the whole API.

- **Base URL:** `http://192.168.4.1/api/` in AP mode, or `http://<device-ip>/api/` in station mode
  (find the IP with `/api/wifi/status` or the `wifi-status` serial command).
- **The UI is external.** The device does **not** host the web app. `GET /` returns **302** to the
  configured external app (`/api/web/config`). The device is a headless JSON/MQTT API + redirect.
- **CORS:** `Access-Control-Allow-Origin` is the configured app URL when set, else `*`.
- **Auth:** **compiled off this release** — the bearer-token layer is disabled, so all write endpoints
  are open on the LAN and a token **cannot** be set (`auth-token` / `POST /api/auth` are no-ops).
  `GET /api/auth/check` always returns `{"authenticated":true,"enforced":false}`. (Auth returns in a later release.)
- **Compile profiles gate the API.** A build advertises what it supports in `GET /api/info` →
  `features`. On **ESP32** everything is present; **C2-HTTP** has no MQTT/OTA; **C2-MQTT is headless
  and exposes no `/api/*` routes at all**. Gate your UI on `features`, never on the chip name.
- **ESP32-C2 clients:** keep **one request in flight** and poll at ~1–2 s.
- **Field naming varies by endpoint — treat each endpoint's documented shape as authoritative.** The
  same concept can appear under different names or types across endpoints; don't assume they match. Known
  cases:
  - identifier — `id` in metrics/status; `dimmer_id` / `relay_id` in some action responses
  - dimmer target — `target` (int) in live views vs `target_level` (float) in `/api/status`
  - frequency — `frequency` (float) vs `mains_frequency` (int) in dimmer status
  - power — `power_w` (live) vs `nominal_power_w` (config)
  - `type` case — lowercase transport (`i2c` / `espnow`) in live views vs uppercase config type
    (`NONE` / `I2C` / `ESPNOW` / `UNKNOWN`)
  - mode — a string (`mode`, e.g. `auto`) in `/api/status`, an int enum (`router_mode`, 0=OFF…6=GRID_LIMIT)
    in `/api/config` (see [Router Modes](https://www.rbdimmer.com/acrouter-operating-modes))
  - `uptime` is a **deprecated alias** of `uptime_sec` wherever it appears
- **Missing/null fields:** any field may be absent when not applicable, and any numeric field may be `null`
  when unavailable.

---

## 8.2 Status & Monitoring

### GET /api/status
Complete router status — mode, control state, dimmer level, and control parameters.

```bash
curl http://192.168.4.1/api/status
```
```json
{
  "mode": "auto", "state": "idle", "power_grid": 15.3,
  "dimmer": 45, "dimmer_count": 1, "target_level": 45.2,
  "control_gain": 200.0, "balance_threshold": 10.0, "valid": true,
  "uptime": 3600, "free_heap": 245000,
  "i2c_active": true, "dimmerlink_count": 2
}
```
- `mode` — `off` · `auto` · `eco` · `offgrid` · `manual` · `boost` · `grid_limit`
- `state` — `idle` · `increasing` · `decreasing` · `at_max` · `at_min` · `error`
- `power_grid` (W, **+** import / **−** export) · `dimmer` (0–100%) · `valid` (bool)
- `uptime` (s) · `free_heap` (bytes) · `i2c_active` (I2C/DimmerLink path)
- `dimmer_count` — enabled dimmer **outputs** (`enabled && initialized`)
- `dimmerlink_count` — DimmerLink **modules** that are `enabled && online` (present only when the
  DimmerLink manager is initialized)
- An `adc_active` field may appear for backward compatibility; it is **vestigial and always `false`** in
  v2.0 (on-device ADC was removed).

### GET /api/metrics
Live power metrics — lightweight, suitable for frequent polling. In v2.0 this is the single source a
dashboard needs (it carries voltage/current/frequency/PF and the dimmer/relay snapshots).

```bash
curl http://192.168.4.1/api/metrics
```
```json
{
  "metrics": {
    "power_grid": 15.3, "power_solar": 480.0, "power_load": 1200.0,
    "voltage": 230.1, "current_grid": 0.07, "current_solar": 2.1, "current_load": 5.2,
    "frequency": 50.0, "power_factor": 0.99, "direction": "consuming"
  },
  "dimmers": [ { "id": 4, "type": "i2c", "name": "Heater 1", "level": 45, "target": 45, "enabled": true, "online": true, "transitioning": false } ],
  "relays": [ { "id": 0, "name": "Relay 1", "is_on": false, "enabled": true, "power_w": 1000 } ],
  "mode": "auto", "timestamp": 123456789
}
```
- `metrics.direction` — `consuming` (import) · `supplying` (export) · `balanced` (≈0)
- Any numeric metric may be **`null`** when unavailable.
- `dimmers[]` are DimmerLink outputs only (id **4+**); legacy GPIO ids 0–3 are gone.

### GET /api/config
All control/configuration parameters.

```json
{
  "control_gain": 200.0, "balance_threshold": 10.0, "grid_current_limit": 16.0,
  "current_threshold": 1.0, "power_threshold": 5.0, "router_mode": 1, "manual_level": 0
}
```
- `grid_current_limit` (A) — the GRID_LIMIT cap · `router_mode` (int enum: 0=OFF…6=GRID_LIMIT)
- `manual_level` is **read-only here** — write it via `POST /api/manual`, not `POST /api/config`.

### GET /api/info
Device identity and **feature flags** (drive UI gating from these).

```json
{
  "version": "2.0.0", "chip": "ESP32-C2",
  "flash_size": 4194304, "free_heap": 245000, "uptime_sec": 3600,
  "features": { "http": true, "mqtt": false, "ota": false, "github_ota": false, "tls": false }
}
```
- `features.http` — REST server present (true whenever you can read this)
- `features.mqtt` — MQTT client compiled in (false on C2-HTTP)
- `features.ota` / `features.github_ota` — OTA subsystem / GitHub OTA (false on C2)
- `features.tls` — TLS available (false on C2)
- `uptime` is a deprecated alias of `uptime_sec`.

---

## 8.3 Sensing & Modules

### GET /api/sensors
Unified read-only list of all active measurement sources (rbAmp over I2C + ESP-NOW nodes).

```json
{ "sources": [
  { "source": "i2c", "id": "0x51", "role": "grid", "online": true,
    "voltage": 230.1, "current": 1.21, "power": 278.0, "power_factor": 0.99, "frequency": 50.0 }
] }
```
- `source` — `i2c` or `espnow`
- `id` — the I2C address (e.g. `0x51`) for I2C, or the MAC for ESP-NOW
- `role` — `grid | solar | load | voltage | none`
- `power`/`power_solar`/`power_load` are **signed** (same convention as grid — the sign follows CT
  orientation). Unavailable numeric fields may be `null`. Empty `sources` when no modules are configured.

### GET /api/modules
Transport-agnostic device registry — every discovered module and its assigned roles. This is the
source of truth after discovery.

```json
{ "modules": [
  { "transport": "i2c", "bus": 0, "addr": "0x51", "family": "rbAmp", "channels": 1,
    "online": true, "roles": ["grid"], "valid_roles": ["none","grid","solar","load","voltage"] }
], "scanning": false }
```
- `family` — one of `rbAmp` · `rbDimmer` · `DimmerLink(legacy)` · `legacy-sensor` · `unknown`. **A
  current DimmerLink module reports `DimmerLink(legacy)`** (the `rbDimmer` string is reserved for a
  future smart-dimmer and is not yet shipped).
- Also present: `channels`, `names[]`, `uid` (chip serial).
- `online: false` — seen before but missing from the last scan (config is kept)
- `valid_roles[]` — the only roles allowed for this family; a UI must offer just these
- `has_voltage` is **not** in this response — it is only in `GET /api/rbamp/modules`.
- `scanning` — poll this after `POST /api/modules/rescan`; when `false` the list is fresh

### GET /api/rbamp/ct-models
The SCT-013 CT-model catalog (firmware source of truth) for the per-module CT picker. Returns a **bare
JSON array**:

```json
[ { "id": "sct013-030", "name": "SCT-013-030", "rated_a": 30, "code": 3, "available": true } ]
```
- Use the `id` (lowercase, e.g. `sct013-030`) as the selector key — not the display `name`. Entries with
  `available: false` (e.g. 60 A / 100 A) are not yet implemented — hide or disable them. Selectable:
  5 / 10 / 20 / 30 / 50 A.

### GET /api/rbamp/modules
Discovered rbAmp modules with live readings and persisted roles (gated by the rbAmp source feature).

```json
{ "alive": 1,
  "modules": [ { "addr": "0x51", "role": "grid", "ct_model": "sct013-030", "channels": 1,
    "has_voltage": true, "online": true, "voltage": 230.1, "current": 1.21,
    "power": 278.0, "power_factor": 0.99, "frequency": 50.0 } ],
  "roles": [ { "addr": "0x51", "role": "grid" } ] }
```
- Per-module source of `frequency` and `power_factor`. `null` = unavailable.
- A **`grid`** module must be **voltage-capable** (`has_voltage: true`), so `voltage`/`power` are real
  (not `null`). A current-only module (e.g. a `solar`/`load` channel) has `has_voltage: false` with
  `null` `voltage`/`power` — but **`frequency` is still reported** (rbAmp reads mains frequency
  independently of the voltage channel).

---

## 8.4 Dimmers & Relays

### GET /api/dimmers/status
Full per-dimmer config and state (DimmerLink outputs only).

```json
{ "initialized": true, "enabled_count": 1, "mains_frequency": 0,
  "dimmers": [ { "id": 4, "type": "i2c", "addr": "0x50", "bus": 0, "name": "Heater 1",
    "enabled": true, "online": true, "power_w": 2000, "curve": 0,
    "level": 45, "target": 45, "initialized": true, "transitioning": false, "priority": 0 } ] }
```
- Output ids: I2C **4–11** (`DIMMER_I2C_START=4`), ESP-NOW **12+** (ESP-NOW is ESP32-tier). Level is
  driven by the router mode.
- `mains_frequency` is **vestigial (always 0)** — the on-device zero-cross was removed. Read the mains
  frequency from `/api/metrics` or `/api/sensors` (per module).

### GET /api/relays/status
Per-relay config and state (functional ids **0–3**; higher ids reserved, not implemented).

```json
{ "relays": [ { "id": 0, "name": "Relay 1", "enabled": true, "type": "GPIO", "gpio": 15,
    "active_high": true, "power_w": 1000, "min_on": 0, "min_off": 0,
    "is_on": false, "initialized": true, "state": "off", "priority": 0 } ],
  "initialized": true, "enabled_count": 1, "on_count": 0, "total_power_w": 0 }
```

---

## 8.5 Hardware

### GET /api/hardware/config
Current hardware configuration (only configured devices; `NONE` slots are skipped).

```json
{
  "dimmers": [],
  "relays": [ { "id": 0, "type": "GPIO", "enabled": true, "name": "Relay 1", "gpio": 15,
    "active_high": true, "nominal_power_w": 1000, "current_sensor_id": -1, "current_sensor_a": null } ],
  "system": { "led_status_gpio": 17, "led_load_gpio": 5 }
}
```
- Only **configured** devices appear (a `NONE`/unset slot is skipped) — so the example relay is a
  configured GPIO relay. Functional relays are ids **0–3** (ids 4+ are reserved and not implemented).
- Dimmer `type` is one of `NONE` / `I2C` / `ESPNOW` / `UNKNOWN` — the v1.x `GPIO` dimmer type was
  removed. DimmerLink dimmers are configured by **role assignment** ([Commissioning](https://www.rbdimmer.com/acrouter-commissioning)),
  not through this endpoint, so `dimmers` is typically empty here. Relays remain host-GPIO.
- `led_status_gpio` / `led_load_gpio` defaults (17 / 5) are **ESP32-specific**. On the ESP32-C2 those
  pins clash with the I2C SDA (GPIO5) and in-package flash — reassign the LED pins on a C2, or leave
  them unused.
- `current_sensor_id` — the rbAmp module I2C address (e.g. `81` = 0x51), or `-1` for none.
- `current_sensor_a` — resolved live RMS current, or `null` if unassigned.
- The v1.x internal-ADC `sensors[]` block has been **removed**.

---

## 8.6 WiFi

### GET /api/wifi/status
Connection status, IPs, and whether credentials are saved. The response is **flat** (not nested):

```json
{
  "state": "connected",
  "ap_active": true, "ap_ssid": "ACRouter_A1B2", "ap_ip": "192.168.4.1", "ap_clients": 1,
  "sta_connected": true, "sta_ssid": "MyHomeNetwork", "sta_ip": "192.168.1.100",
  "rssi": -65, "has_saved_credentials": true, "mac": "AA:BB:CC:DD:EE:FF", "hostname": "acrouter"
}
```
- `rssi` (dBm): −30…−50 excellent · −51…−70 good · −71…−85 fair · −86…−100 poor. There is one `mac`
  field (the device MAC), no per-interface `channel` or `saved.ssid`.

### GET /api/wifi/scan
Scan for WiFi networks. **Non-blocking** — kicks a background scan and returns the cached results
immediately with a `scanning` flag; poll until it is `false`. `encryption` is `secured` or `open`.

```json
{ "scanning": false,
  "networks": [ { "ssid": "MyHomeNetwork", "rssi": -45, "encryption": "secured", "channel": 6 } ],
  "count": 1 }
```

---

## 8.7 Integrations

### GET /api/mqtt/status
```json
{ "enabled": true, "connected": true, "state": 0, "broker": "mqtt://192.168.1.10",
  "device_id": "acrouter_xxxx", "uptime": 3600, "messages_published": 120, "messages_received": 8, "last_error": "" }
```
On C2-HTTP the route exists but MQTT is compiled out — treat `features.mqtt=false` as authoritative.

### GET /api/mqtt/config
```json
{ "enabled": true, "broker": "mqtt://192.168.1.10", "username": "user",
  "device_id": "acrouter_xxxx", "device_name": "ACRouter", "publish_interval": 5000, "ha_discovery": true }
```
The password is never returned. `publish_interval` is in **milliseconds** (1000–60000).

### GET /api/ntp/status
```json
{ "running": true, "synced": true, "current_time": "2026-07-16 12:00:00", "last_sync_ago_sec": 120, "sync_interval_sec": 3600 }
```
`current_time` / `last_sync_ago_sec` appear only when synced.

### GET /api/ntp/config
```json
{ "ntp_server": "pool.ntp.org", "timezone": "UTC", "gmt_offset_sec": 0, "daylight_offset_sec": 0 }
```

### GET /api/ota/check-github
Blocking check against GitHub Releases. Returns **200** when the build has OTA
(`features.ota = true`) — branch on the body (`success:false` on a failed check). On builds **without
OTA (the C2)** the whole `/api/ota/*` path is **404** (the route isn't registered). Gate on
`features.ota` / `features.github_ota`.

```json
{ "success": true, "update_available": true, "current_version": "2.0.0", "latest_version": "2.1.0",
  "release_name": "v2.1.0", "published_at": "…", "changelog": "…",
  "asset_name": "acrouter.bin", "asset_url": "https://…", "asset_size": 1048576, "is_prerelease": false }
```

---

## 8.8 System

### GET /api/web/config
External web-app redirect/CORS target. `{ "app_url": "http://host:port", "open": true|false }`.

### GET /api/auth/check
Always `{ "authenticated": true, "enforced": false }` — auth is **compiled off this release**, so write
endpoints are open on the LAN and a token can't be set. (Auth returns in a later release.)

---

## 8.9 Advanced / Diagnostic (optional)

These are diagnostic or developer endpoints — not needed for normal use:

- **GET /api/i2c/status** — bus state, speed, DimmerLink counts.
- **GET /api/i2c/scan** — raw I2C address scan of bus 0 (**503** if the bus is not initialized).
- **GET /api/sensors/hub** — Sensor-Hub merge slots (voltage/grid/solar/load) with source & priority.
- **GET /api/dimmerlink/devices**, **GET /api/dimmerlink/{slot}/status** — low-level DimmerLink registry
  (by slot 0–7) and per-device current/voltage/thermal telemetry.
- **GET /api/espnow/nodes** — ESP-NOW measurement nodes (by MAC).
- **GET /api/espnow/outputs** — ESP-NOW output nodes (dimmer/relay, by MAC). ESP-NOW is ESP32-tier.

---

## 8.10 Removed in v2.0

The following v1.x reads no longer exist (the ADC pipeline was removed and sensing moved to smart
modules): `/api/hardware/sensor-profiles`, `/api/hardware/sensor-types`, `/api/hardware/voltage-drivers`,
`/api/hardware/current-drivers`, and the internal-ADC `sensors[]` block in `/api/hardware/config`.
Use `/api/sensors`, `/api/modules`, and `/api/rbamp/modules` instead.

> Response envelope: reads return their JSON at `200`. Errors use `{"error":"<text>"}` (some legacy
> sections show `{"status":"error","message":"…"}`) — treat the presence of `error` as authoritative.

> ⚠️ **A few control actions also answer on GET** (legacy convenience) — e.g. `GET /api/system/reboot`
> and the relay/dimmer on/off routes. Because auth is compiled off this release, treat these as live: keep
> the device off untrusted networks, and don't let a browser link-prefetcher or crawler hit those URLs.
> **POST is the canonical, documented method** for every mutating call.

---

[← Terminal Commands](https://www.rbdimmer.com/acrouter-terminal-commands) | [Contents](https://www.rbdimmer.com/acrouter-what-is) | [Next: Web API - POST →](https://www.rbdimmer.com/acrouter-web-api-post)
