---
author: RobotDyn
description: ACRouter v2.0 serial terminal command reference — router modes, rbAmp and DimmerLink modules, device registry, ESP-NOW, I2C/hardware, relays, dimmers, config, WiFi, MQTT, OTA, and diagnostics over the serial console.
keywords:
- ACRouter terminal commands
- ACRouter serial CLI
- rbAmp DimmerLink commands
- ACRouter router-mode command
- solar router serial console
title: ACRouter Terminal Commands — Serial CLI Reference (v2.0)
url: acrouter-terminal-commands
---
[← Router Modes](https://www.rbdimmer.com/acrouter-operating-modes) | [Contents](https://www.rbdimmer.com/acrouter-what-is) | [Next: Web API - GET →](https://www.rbdimmer.com/acrouter-web-api-get)

# Terminal Commands

Serial console reference for ACRouter v2.0 (115200 baud, 8N1). Type **`help`** on the device for the
live list. Most of these have a REST equivalent — see the [Web API](https://www.rbdimmer.com/acrouter-web-api-get).

## 7.1 General

| Command | Description |
|---------|-------------|
| `help` | Show the command list |
| `status` | Show router status |
| `reboot` | Restart the device |

## 7.2 Router

| Command | Description |
|---------|-------------|
| `router-mode <mode>` | Set the operating mode — `off · auto · eco · offgrid · manual · boost · grid_limit` |
| `router-grid-limit <A>` | Set the grid current cap (amps) for GRID_LIMIT mode |
| `router-status` | Show detailed router status |

```text
router-mode auto
router-grid-limit 16
```
See [Router Modes](https://www.rbdimmer.com/acrouter-operating-modes) for what each mode does.

## 7.3 rbAmp (measurement modules)

| Command | Description |
|---------|-------------|
| `rbamp-status` | Show discovered rbAmp modules + roles |
| `rbamp-rescan` | Re-scan the bus for new rbAmp modules |
| `rbamp-config <addr_hex> <role>` | Driver-direct role (`grid·solar·load·voltage·none`) — prefer `dev-role` |
| `rbamp-ct-model <addr_hex> <code>` | Set the SCT-013 CT model (preset code) on channel 0 |
| `rbamp-address <cur_hex> <new_hex>` | Re-address a module (hex or dec) |

## 7.4 DimmerLink (dimmer modules)

| Command | Description |
|---------|-------------|
| `dl-status [slot]` | Show DimmerLink device status |
| `dl-config <slot> <addr_hex> <role>` | Register a DimmerLink in a slot (advanced). Role vocabulary: `current_grid·current_solar·current_load·voltage·dimmer·relay` (DimmerLink-as-sensor roles) |
| `dl-address <cur_hex> <new_hex>` | Re-address a DimmerLink (hex or dec) |

> Prefer assigning `role=dimmer` (see Device registry / Commissioning) — the firmware auto-binds the
> DimmerLink to a dimmer output. `dl-config` is the low-level path.

## 7.5 Device Registry

The **recommended** way to assign roles — the unified registry, same as the web app's role assignment
(`POST /api/modules/role`).

| Command | Description |
|---------|-------------|
| `dev-list` | List all discovered modules (bus, address, family, channels, primary role) |
| `dev-scan [bus]` | Re-scan a bus (default 0) and reconcile the registry |
| `dev-role <addr> <channel> <role>` | Assign a per-channel role — `grid·solar·load·voltage·dimmer·relay·none` |
| `dev-identify <bus> <addr>` | Identify a device (VERSION-gate protocol) |

> Use `dev-role` for normal commissioning. The driver-direct commands (`rbamp-config`, `dl-config`,
> `espnow-config`) are low-level alternatives with their own role vocabularies — advanced/diagnostic only.

## 7.6 ESP-NOW

> ESP-NOW is an **ESP32-tier** feature — these commands apply on ESP32 builds; the ESP32-C2 uses wired
> DimmerLink over I2C.

| Command | Description |
|---------|-------------|
| `espnow-status` | Show ESP-NOW nodes + roles |
| `espnow-config <mac> <role>` | Assign a role to a node (`grid·solar·load·voltage·none`) |
| `espnow-out` | List ESP-NOW output nodes (dimmer/relay) |
| `espnow-bind <mac>` | Bind an output node to a dimmer (RouterController drives it) |
| `espnow-set <mac> <pct>` | Drive an output directly (wire-path test) |

## 7.7 I2C & Hardware

| Command | Description |
|---------|-------------|
| `i2c-scan [bus]` | Scan an I2C bus (default 0) for devices |
| `i2c-read [bus] <addr> <reg>` / `i2c-reads [bus] <addr> <reg>` | Read a register / read (multi) |
| `i2c-write [bus] <addr> <reg> <val>` | Write a register |
| `i2c-init` / `i2c-reinit <bus> <sda> <scl> [freq]` | (Re)initialize a bus at runtime |
| `hw-bus1 <sda> <scl> [khz] [en]` | Persist the optional second I2C bus (bus 1) |
| `pin-read <gpio> [samples]` | Read a GPIO level |
| `hw-rbamp-bus <0\|1>` | Select which I2C bus rbAmp uses |
| `hw-rbamp-drdy <gpio\|-1>` | Bind the optional rbAmp DRDY interrupt pin (`-1` disables) |
| `hw-version-show` | Show NVS version info & safe-mode status |
| `hw-erase-nvs` | Full NVS erase + factory reset |
| `hardware-reset` | Reset hardware config to factory defaults (keeps NVS structure) |

> Persistent I2C bus pins are set with `POST /api/hardware/config` (reboot required); `i2c-reinit` is a
> runtime-only change (see the [Hardware Guide](https://www.rbdimmer.com/acrouter-hardware-guide)).

## 7.8 Relays

| Command | Description |
|---------|-------------|
| `relay <id> <on\|off\|toggle> [force]` | Control relay `id` (`force` bypasses debounce) |
| `relay-list` | Show all relays |
| `relay-all-off` | Turn all relays off |
| `relay-priority <id> <0-255>` | Set relay priority |
| `hw-relay-status [id]` | Show relay HW config |
| `hw-relay-enable <id> <on\|off>` · `hw-relay-gpio <id> <pin>` · `hw-relay-name <id> <name>` · `hw-relay-power <id> <watts>` · `hw-relay-active <id> <high\|low>` · `hw-relay-debounce <id> <min_on> <min_off>` · `hw-relay-save [id\|all]` | Configure a relay |

> Relays are host-GPIO outputs — **functional ids 0–3** (ids 4+ are reserved and not implemented). In the
> automatic control they act **only in AUTO mode** (binary ON/OFF in a priority cascade); ECO / OFFGRID /
> MANUAL / BOOST / GRID_LIMIT do not drive relays. Power-aware prioritization is planned for a later phase
> — you can always switch a relay manually with `relay <id> on|off`. The debounce `min_on`/`min_off` are
> in **seconds** (default 60 s each).

## 7.9 Dimmers (output)

| Command | Description |
|---------|-------------|
| `dimmer <ID\|all> <0-100>` | Set a dimmer output level (I2C dimmer ids start at 4) |
| `dimmer-priority <ID> <0-255>` | Set dimmer priority |
| `hw-dimmer-status [ID\|gpio\|all]` | Show dimmer HW config |
| `hw-dimmer-enable <ID\|all> <on\|off>` · `hw-dimmer-name <ID> <name>` · `hw-dimmer-power <ID> <watts>` · `hw-dimmer-curve <linear\|rms\|log> [ID]` · `hw-dimmer-save [ID\|all]` | Configure a dimmer |

Curves: `linear` (direct angle), `rms` (for resistive loads). The `log` curve is a legacy of the
rbDimmer ecosystem for LED loads — **not for ACRouter**, which drives resistive loads only.

> v2.0 dimming is DimmerLink-only; I2C dimmer ids start at **4**. If `hw-dimmer-status` shows legacy
> GPIO dimmer slots (ids 0–3), they are retired remnants — not a usable feature.

## 7.10 Configuration

| Command | Description |
|---------|-------------|
| `config-show` | Show all config |
| `config-reset` | Reset config to defaults |
| `config-gain [value]` | Control gain (10–1000) |
| `config-threshold [value]` | Balance threshold (W, 0–100) |
| `config-manual <0-100>` | MANUAL dimmer level |
| `config-ithresh [value]` | Current threshold (A, 0–10) |
| `config-pthresh [value]` | Power threshold (W, 0–100) |

## 7.11 WiFi

| Command | Description |
|---------|-------------|
| `wifi-status` | Show WiFi status |
| `wifi-scan` | Scan for networks |
| `wifi-connect <ssid> [password]` | Connect + save credentials |
| `wifi-disconnect` | Disconnect from station |
| `wifi-forget` | Clear saved credentials |

## 7.12 Web Server

| Command | Description |
|---------|-------------|
| `web-status` | Show server status & URLs |
| `web-start` / `web-stop` | Start / stop the web server |
| `web-urls` | Show all access URLs |
| `web-url set <url> \| clear \| show` | Set/clear the external web-app URL (redirect + CORS origin) |

## 7.13 Time (NTP)

| Command | Description |
|---------|-------------|
| `time-status` | Show NTP sync status |
| `time-sync` | Force an NTP sync |

## 7.14 OTA

| Command | Description |
|---------|-------------|
| `ota-status` | Show OTA status & URLs |
| `ota-check` | Check GitHub for updates |
| `ota-update-github` | Download + install from GitHub |
| `ota-update-url <url>` | Download + install from a custom URL |
| `ota-rollback` | Roll back to the previous firmware |
| `ota-info` | Show OTA partition info |

## 7.15 MQTT

| Command | Description |
|---------|-------------|
| `mqtt-status` / `mqtt-config` | Show MQTT status / configuration |
| `mqtt-broker <url>` | Set broker (`mqtt://host:port`) |
| `mqtt-user <name>` · `mqtt-pass <pass>` | Credentials |
| `mqtt-device-id <id>` · `mqtt-device-name <n>` | Device id / name (HA) |
| `mqtt-interval <ms>` | Publish interval (1000–60000) |
| `mqtt-ha-discovery <0\|1>` | Enable/disable Home Assistant discovery |
| `mqtt-enable` / `mqtt-disable` | Enable / disable MQTT |
| `mqtt-reconnect` / `mqtt-publish` | Force reconnect / publish |

## 7.16 Diagnostics & Test

| Command | Description |
|---------|-------------|
| `sensor-hub` | Show the merged sensor-hub state |
| `timing` | I2C poll cadence / CPU-time per module |
| `sim-inject <grid\|solar\|load> <A> [V] [W]` | Inject a synthetic measurement (Tier-0 test harness). Voltage form: `sim-inject voltage <V>`. REST equivalent: `POST /api/sim/inject` |
| `auth-token set <token> \| clear \| show` | **No-op this release** — auth is compiled off, so a token can't be set and write endpoints are open on the LAN |

---

## 7.17 Removed / Deprecated

- **Removed in v2.0:** `debug-adc`, `router-dimmer` (on-chip ADC / legacy GPIO dimmer paths).
- **Deprecated (legacy ADC sensing — do not use for measurement):** `hardware-voltage-*`,
  `hardware-current-*` (incl. `hardware-current-calibrate-zero`). v2.0 sensing is the rbAmp modules
  over I2C — use `rbamp-*` and `dev-role` instead.

---

[← Router Modes](https://www.rbdimmer.com/acrouter-operating-modes) | [Contents](https://www.rbdimmer.com/acrouter-what-is) | [Next: Web API - GET →](https://www.rbdimmer.com/acrouter-web-api-get)
