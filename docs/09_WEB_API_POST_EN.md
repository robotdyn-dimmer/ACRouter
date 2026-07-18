---
author: RobotDyn
description: ACRouter v2.0 REST API — POST endpoints. Set configuration and operating mode, commission modules (rescan, roles, rbAmp address/CT model), configure hardware, control dimmers/relays, WiFi, MQTT, NTP, and OTA.
keywords:
- ACRouter REST API POST
- ACRouter set mode
- ACRouter commissioning API
- solar router control API
- ACRouter modules role
title: ACRouter Web API — POST Endpoints (v2.0)
url: acrouter-web-api-post
---
[← Web API - GET](https://www.rbdimmer.com/acrouter-web-api-get) | [Contents](https://www.rbdimmer.com/acrouter-what-is) | [Next: Sensor Calibration →](https://www.rbdimmer.com/acrouter-sensor-calibration)

# Web API — POST Endpoints

Control and write endpoints. Read the [GET reference §8.1](https://www.rbdimmer.com/acrouter-web-api-get)
first — the base URL, external-UI, CORS, auth, compile-profile, and C2-client rules apply here too.

## 9.1 Conventions

- **Content-Type:** `application/json` for endpoints with a body. The browser sends an `OPTIONS`
  preflight first; the server answers `204` with CORS headers.
- **Response envelope:** success is `{ "success": true, "message": "…" }` at `200`; errors are
  `{ "error": "…" }` (missing body → `400 {"error":"Missing request body"}`, bad JSON →
  `400 {"error":"Invalid JSON"}`). Some legacy sections show `{"status":"ok"|"error"}` — treat the
  `success`/`error` form as authoritative.
- **Persistence:** config/WiFi/hardware writes are saved to **NVS** and survive reboot.
- **Async operations return `202`** and finish in the background — see [§9.3](#93-commissioning).

---

## 9.2 Configuration & Control

### POST /api/config
Update any subset of control parameters (only the fields you send change).

```bash
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{"control_gain": 250.0, "balance_threshold": 15.0}'
```
Body fields (all optional, the complete set): `control_gain` (10–1000, default 200) · `balance_threshold`
(W, 0–100, default 10) · `grid_current_limit` (A, 0–100, default 16 — the GRID_LIMIT cap) ·
`current_threshold` (A, 0–10, default 1) · `power_threshold` (W, 0–100, default 5). →
`200 {"success":true,"message":"Configuration updated"}`.

> **`manual_level` is not accepted here** — set it with `POST /api/manual`. (`GET /api/config` *returns*
> `manual_level` for reference, but this endpoint does not write it.) The v1.x `voltage_coef` /
> `current_coef` fields are gone.

### POST /api/config/reset
Reset all configuration to factory defaults (erases custom settings, writes defaults to NVS).
`curl -X POST http://192.168.4.1/api/config/reset` → `200 {"success":true,"message":"Configuration reset to defaults"}`.

### POST /api/mode
Set the operating mode.

```bash
curl -X POST http://192.168.4.1/api/mode -H "Content-Type: application/json" -d '{"mode": "auto"}'
```
- `mode` — `off` · `auto` · `eco` · `offgrid` · `manual` · `boost` · `grid_limit`
- `grid_limit` caps grid draw at `grid_current_limit` A (current-only, no export) — set the cap with
  `POST /api/config {"grid_current_limit": <A>}`.
- Invalid value → `400 {"error":"Invalid mode (use: off, auto, eco, offgrid, manual, boost, grid_limit)"}`.

### POST /api/manual
Set the manual level **and** switch to MANUAL mode in one call.
`-d '{"value": 50}'` (`value` 0–100%) → `200 {"success":true,"message":"Manual control set"}`.

### POST /api/dimmer
Set the manual **level only** — `-d '{"value": 75}'` (0–100%). Unlike `/api/manual`, this does **not**
switch modes: the level takes effect immediately only if the router is **already** in MANUAL. To use it,
set MANUAL first (`POST /api/mode {"mode":"manual"}`).

---

## 9.3 Commissioning

Setting up modules the first time. See the Commissioning guide for the full walkthrough.

### POST /api/modules/rescan — async
On-demand I2C scan + non-destructive reconcile (matches transport+addr → keeps config; new → added;
missing → marked offline but kept; never deletes).

- Returns **`202 {"scanning":true}`** immediately (the ~2.5 s scan runs in a worker task).
- A second rescan while busy → **`409 {"error":"busy","operation":"rescan"}`**.
- Poll `GET /api/modules` — when its `"scanning"` is `false`, the list is fresh.

### POST /api/modules/role
Assign a per-channel role via the registry (the role source of truth). This is the **recommended**
way to bind a module — assigning `dimmer` auto-binds a DimmerLink to its output.

```json
{ "addr": "0x51", "channel": 0, "role": "grid" }
```
- `role` — `grid|solar|load|voltage|dimmer|relay|none`
- → `200 {"success":true,"message":"Role saved"}` · no module at addr → `404` · bad channel → `400`.

### POST /api/modules/name
Set a human-readable per-channel name (shows up in `GET /api/metrics` and the UI).

```json
{ "addr": "0x51", "channel": 0, "name": "Grid CT" }
```
- `channel` defaults to `0`. → `200 {"success":true}` · no module at addr → `404` · bad channel → `400`.

### POST /api/rbamp/modules/address — async
Change an rbAmp module's I2C address (two-phase commit; role mapping migrates and persists).

```json
{ "addr": "0x51", "new_addr": "0x52" }
```
- → **`202 {"success":true,"pending":true,"new_addr":"0x52", "message":"applies after reset; module re-appears at new_addr"}`**
- `400` new_addr outside 0x08–0x77 / equal to addr · `404` no module at addr · `409` target already a
  known rbAmp, or a change is already pending.

### POST /api/rbamp/modules/ct-model — async
Set the rbAmp SCT-013 CT model (channel 0). Verify-then-set.

```json
{ "addr": "0x51", "ct_model": "sct013-030" }
```
- `ct_model` = a catalog `id` from `GET /api/rbamp/ct-models`.
- → **`202 {"success":true,"pending":true,"ct_model":"sct013-030", …}`**.
- `400` unknown ct_model · `404` no module · `409` model not available on this firmware / change pending.

> ⚠️ Changing the CT model reloads the preset gain and **overwrites the module's per-unit factory gain
> calibration**. Re-selecting the same model is a no-op (protected).

---

## 9.4 Hardware

### POST /api/hardware/config
Set the host hardware configuration (I2C bus pins, relay GPIOs, status LEDs). Written to NVS;
**a reboot is required to apply**. The common use is the I2C bus:

```json
{ "i2c": { "bus0": { "sda": 25, "scl": 26, "enabled": true } },
  "relay_ch1": { "gpio": 15, "active_high": true, "enabled": true },
  "led_status_gpio": 17, "led_load_gpio": 5 }
```
- → `200 {"success":true,"message":"Configuration saved to NVS (reboot required)"}` · validation fail
  → `400` · NVS fail → `500`.
- Relays are the objects **`relay_ch1`** / **`relay_ch2`** (host-GPIO, ids 0–3), **not** a `relays[]`
  array (the array is the GET *response* shape). LED pin defaults (17 / 5) are **ESP32-specific** — on a
  C2 they clash with the I2C SDA / flash pins, so reassign them there.
- 🔴 **Dimmers are not configured here.** There is no GPIO/TRIAC dimmer in v2.0 — dimmers are DimmerLink
  modules, set up by role assignment ([Commissioning](https://www.rbdimmer.com/acrouter-commissioning)).
  The v1.x keys `dimmer_ch*`, `zerocross_*`, and `adc_channels[]` are gone / ignored — don't send them.
- **Partial bodies merge per-key** — a key you omit keeps its current value (this holds for `i2c.busN`
  and the `relay_chN` blocks alike).

### POST /api/hardware/validate
🚧 Stub in this release — the body is read but not applied; validation runs against a default config.
Do not rely on field-level validation yet.

---

## 9.5 Dimmers & Relays

### POST /api/dimmers/all-on · /api/dimmers/all-off
Set all enabled dimmers to 100% / 0%.
→ `200 {"success":true,"dimmers":[{"dimmer_id":4,"level":100,"success":true}]}`.

> Per-dimmer routes `/api/dimmers/{0-3}/level` and `/config` were **removed** — dimmer level is driven
> through the router mode (`/api/mode`, `/api/manual`), not per-dimmer.

### POST /api/relays/{id}/on · /api/relays/{id}/off
Turn relay `{id}` (0–3) on/off. Optional body `{"force": true}` (on only).
→ `200 {"relay_id":0,"state":"on","success":true}` · debounce → `400 {…"state":"debounce"…}`.
Also `/api/relays/all-on` · `/api/relays/all-off`.

### POST /api/relays/{id}/config
Configure a relay (all fields optional): `name`, `gpio`, `power_w`, `min_on`, `min_off`,
`active_high`, `enabled`, `priority`. → `200 {"success":true,"message":"Relay configuration saved"}`.

---

## 9.6 WiFi

### POST /api/wifi/connect
Connect and save credentials to NVS (auto-connect next boot; AP stays up).

```bash
curl -X POST http://192.168.4.1/api/wifi/connect -H "Content-Type: application/json" \
  -d '{"ssid": "MyHomeNetwork", "password": "MyPassword123"}'
```
- `password` optional for open networks. The connect is **asynchronous** — a `200` only means the attempt
  started: `{"success":true,"message":"Connecting to WiFi... Credentials will be saved on success","ssid":"…"}`
  (no `ip` field). If the attempt can't be initiated → `500`. Poll **`GET /api/wifi/status`** for the
  actual result and the assigned IP. Credentials are sent in plaintext (LAN).

### POST /api/wifi/disconnect · /api/wifi/forget
`disconnect` → AP-only, credentials kept. `forget` → clears saved credentials (no auto-connect next boot).

---

## 9.7 Integrations

### POST /api/mqtt/config
Update MQTT config (all optional): `broker`, `username`, `password`, `device_id`, `device_name`,
`publish_interval`, `ha_discovery`, `enabled`. → `200 {"success":true,"message":"MQTT configuration saved"}`.
If `enabled` and disconnected, it auto-connects.

### POST /api/mqtt/reconnect · /api/mqtt/publish
`reconnect` → `{"success":true,"message":"Reconnection initiated"}` (`400` if MQTT disabled).
`publish` → force-publish all topics now (`400` if not connected).

> Headless (C2-MQTT) and any MQTT build can be configured **over MQTT** — see the
> [MQTT Guide](https://www.rbdimmer.com/acrouter-mqtt-guide) (config-over-MQTT topics).

### POST /api/ntp/config · /api/ntp/sync
`config` (optional `ntp_server`, `timezone`, `gmt_offset_sec`, `daylight_offset_sec`) →
`200 {"success":true,"message":"NTP configuration updated and saved"}`. `sync` forces a sync now.
`gmt_offset_sec` / `daylight_offset_sec` are applied **only together with `timezone`** — a body without
`timezone` returns `400`.

### POST /api/ota/update-github
Download + flash firmware from an asset **URL**, then reboot. Despite the `-github` name, this REST
endpoint is a low-level "flash this URL" — it **requires** a body `{"url": "<asset-url>"}`.
→ `200 {"success":true,"message":"OTA update started. Device will reboot after download."}` (sent
before flashing; no second response). Gate on `features.ota` / `features.github_ota`.

> The serial `ota-update-github` command (no argument) is different — it resolves the latest release
> itself. Use `ota-check` first to find the asset URL for this REST call.

> **Raw-binary OTA:** `POST /ota/upload` takes the raw firmware image
> (`Content-Type: application/octet-stream`, **not** multipart). Large OTA over WiFi is impractical on
> the ESP32-C2 — use serial flash on the C2.

---

## 9.8 System

### POST /api/web/config
Set the external app URL (redirect target + CORS origin). `{"app_url": "http://host:port"}`; empty
clears it (open). Serial equivalent: `web-url set <url> | clear | show`.

### POST /api/system/reboot
`{"success":true,"message":"Rebooting..."}` — the device restarts a few seconds (~3) after replying.

### POST /api/auth
`{"new_token":"…"}` sets/changes the bearer token (empty string clears it; gated by the current token).
**No-op this release** — auth is compiled off (`ACROUTER_AUTH_ENFORCE=0`), so the token can't be set and
write endpoints stay open on the LAN. See [Web API — GET §8.8](https://www.rbdimmer.com/acrouter-web-api-get).

---

## 9.9 Advanced / Developer (optional)

Not for normal use:

- **POST /api/sim/inject** / **/api/sim/stop** — Tier-0 test harness that injects a synthetic power
  event (HTTP builds only; for bench/CI). `inject` body: `{"role":"grid","current":9.0,"voltage":230.0,"power":-2000.0,"latch":false}`.
- **POST /api/dimmerlink/devices**, **/api/dimmerlink/devices/address** — low-level DimmerLink slot
  registration/addressing (prefer role assignment above).
- **POST /api/espnow/nodes** — assign a role to an ESP-NOW node by MAC.
- **POST /api/rbamp/rescan** — rbAmp-only rescan (`501` when autodiscovery is off, e.g. default on C2).
- **POST /api/calibrate** — 🚧 not implemented (`501`).

---

## 9.10 Removed in v2.0

- **Per-GPIO dimmer routes** `/api/dimmers/{0-3}/level` and `/config` — dimming is via router mode now.
- **`adc_channels[]`** in `/api/hardware/config` — accepted but ignored (ADC pipeline removed).
- **Sensor-driver routes** `/api/hardware/sensor-profiles`, `/sensor-types`, `/voltage-drivers`,
  `/current-drivers` — smart modules are factory-calibrated; use `/api/modules/role` + `/api/rbamp/modules/ct-model`.

---

[← Web API - GET](https://www.rbdimmer.com/acrouter-web-api-get) | [Contents](https://www.rbdimmer.com/acrouter-what-is) | [Next: Sensor Calibration →](https://www.rbdimmer.com/acrouter-sensor-calibration)
