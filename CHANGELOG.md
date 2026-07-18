# ACRouter - Release Notes

Public changelog for stable releases.

---

## [2.0.0] - 2026-07-16

**v2.0 — Smart-Module Architecture & Compile Tiering**

Firmware complete and hardware-validated on both ESP32 and ESP32-C2 targets.

### Added
- **Smart sensing over I2C (rbAmp)** — current/voltage from smart modules with per-channel roles (grid/solar/load/voltage), replacing on-device ADC sensing.
- **ESP-NOW remote nodes (ESP32-tier)** — wireless sensing and dimmer-output nodes; the control loop is source-agnostic.
- **DimmerLink I2C smart dimmers** — dimmers are now driven exclusively over DimmerLink (`role=dimmer` auto-binds the primary output; see Removed).
- **ESP32-C2 (ESP8684) target** — single-core, RAM-constrained, alongside ESP32.
- **Compile tiering** — three profiles (C2-HTTP, headless C2-MQTT, ESP32) selected by Kconfig (`ACROUTER_HTTP_SERVER`/`ACROUTER_MQTT_CLIENT`/`ACROUTER_OTA`/`ACROUTER_RBAMP_SOURCE`/`ACROUTER_MQTT_BOOTSTRAP`); `/api/info` `features` reports the build's capabilities.
- **config-over-MQTT** — provision/control a headless device over MQTT (`config/set`, retained `config/state`, `config/get`) with a build-time MQTT bootstrap; `config/state` is published retained on connect.
- **Unified device registry** — `/api/modules` with per-channel roles/names.
- **GRID_LIMIT mode** — a seventh operating mode that caps grid draw by current (`grid_current_limit`, default 16 A; API/serial only).

### Changed
- **Control loop isolated** into a dedicated high-priority RTOS task (dual-core split on ESP32; the C2 web layer can no longer wedge the control loop).
- **Web app is external** — the device redirects to an externally-hosted UI; CORS origin allowlist (was `*`).
- **Sensor-loss failsafe** — AUTO/ECO/GRID_LIMIT (and OFFGRID) now fail toward off: on a lost regulating reading or total sensor silence the dimmer decays to 0 (−1%/cycle, ~20 s) instead of holding the last level.
- **Emergency Stop / OFF** now de-energize the whole cascade — all dimmers *and* all relays (previously dimmers only).

### Removed
- On-device ADC current/voltage sensing (superseded by rbAmp/ESP-NOW).
- **Legacy GPIO / direct-TRIAC dimming** — v2.0 drives dimmers only over DimmerLink I2C; the on-device zero-cross/TRIAC path and per-GPIO dimmer routes (`/api/dimmers/{0-3}/level|config`) were removed.
- **Built-in dashboard pages** (Dashboard, Dimmers, Hardware Config, OTA) — the main UI is the external web app. Minimal on-device pages for WiFi, MQTT, and Relays remain.

### Known Limitations
- OTA is ESP32-only (the C2 flashes over serial).
- Auth is compiled off this release — write endpoints are open on the LAN; the bearer-token layer is disabled (a token can't be set).
- Per-dimmer control over MQTT / Home Assistant is not exposed yet (no HA light entity for DimmerLink; per-dimmer MQTT topics don't reach id 4+). Drive via the router mode or serial `dimmer <id>`.
- `POST /api/hardware/validate` and `POST /api/calibrate` are stubs; CT models above 50 A are not preset-backed.

---

## [1.3.1] - 2026-01-12

**Hardware Config API Refactoring v2.0**

### What's New

#### Restructured Hardware Config API

- **GET /api/hardware/config** - Completely restructured with device-type sections
  - **sensors** - ADC sensors with driver information (replaces old "adc_channels")
  - **dimmers** - All dimmer configuration fields exposed
  - **relays** - All relay configuration fields exposed
  - **system** - System-wide hardware settings (zerocross, LEDs)
- **New field: interface** - Connection type indicator ("ADC", "GPIO", "I2C", "ESP-NOW")
- **Exposed all fields** - All `dimmer_config_t` and `relay_config_t` fields now in API
- **Sensor binding** - `current_sensor_id` now visible for dimmers and relays
- **Optimized output** - Only configured devices included (skips type=NONE slots)

#### New Endpoint: Sensor Types

- **GET /api/hardware/sensor-types** - Sensor catalog with driver information
  - **voltage_sensors** - Voltage sensor types with available drivers
  - **current_sensors** - Current sensor types with available drivers
  - **Driver details** - Default multipliers, offsets, max current/voltage
  - **Supported drivers:**
    - Voltage: ZMPT107, ZMPT101B
    - Current: SCT013 series (5A-100A), ACS712 series (5A-50A)

#### API Structure Example

**New GET /api/hardware/config response:**
```json
{
  "sensors": [
    {
      "id": 0,
      "interface": "ADC",
      "gpio": 35,
      "sensor_type": 1,
      "sensor_type_name": "VOLTAGE_AC",
      "driver": "ZMPT107",
      "nominal_vdc": 0.70,
      "multiplier": 328.57,
      "offset": 0.0,
      "enabled": true
    }
  ],
  "dimmers": [
    {
      "id": 0,
      "type": "GPIO",
      "interface": "GPIO",
      "enabled": true,
      "name": "Heater 1",
      "gpio": 19,
      "nominal_power_w": 3000,
      "current_sensor_id": 1,
      "curve": 1,
      "mode": 0,
      "min_level": 0,
      "max_level": 100,
      "priority": 25
    }
  ],
  "relays": [
    {
      "id": 0,
      "type": "GPIO",
      "interface": "GPIO",
      "enabled": true,
      "name": "Heater",
      "gpio": 15,
      "active_high": true,
      "nominal_power_w": 1000,
      "current_sensor_id": -1,
      "mode": 0,
      "min_on_time_s": 60,
      "min_off_time_s": 60,
      "priority": 1
    }
  ],
  "system": {
    "zerocross_gpio": 18,
    "zerocross_enabled": true,
    "led_status_gpio": 17,
    "led_load_gpio": 5
  }
}
```

### Changes (BREAKING)

- **Hardware Config API restructured** - Old flat structure replaced with hierarchical
- **Section renamed** - "adc_channels" → "sensors"
- **Device filtering** - Only configured devices returned (type != NONE)
- **Field additions** - All configuration fields now exposed (previously hidden)

### Migration Notes

- **External React app** - Update to use new API structure with sensors/dimmers/relays sections
- **Old embedded web UI** - May require updates if still in use
- **Backward compatibility** - Not maintained (breaking change)
- **Benefits** - Cleaner structure, full field access, scalable for I2C/ESP-NOW devices

### Technical Details

- **Implementation files:**
  - `components/comm/src/WebServerManager.cpp` - API refactoring
  - `components/comm/include/WebServerManager.h` - New endpoint declarations
- **Helper functions:**
  - `getInterfaceType()` - Maps device type to interface string
  - `getCurrentSensorDescription()` - Sensor type descriptions
  - `buildSensorTypesJson()` - Driver catalog builder
- **Driver enums used:**
  - `VoltageSensorDriver` - ZMPT107_ADC, ZMPT101B_ADC
  - `CurrentSensorDriver` - SCT013/ACS712 variants with empirical calibrations

---

## [1.3.0] - 2026-01-05

**Multi-Device AUTO Mode with Priority Management**

### What's New

#### Priority-Based Load Management

- **Multi-device support:** Up to 64 dimmers + 64 relays simultaneously
- **Priority levels:** 0-255 (where 0 = highest priority)
- **Equal percentage distribution:** Devices at same priority receive equal % changes
- **Cascade activation:** Lower priority devices activate when higher priority saturates
- **NVS persistence:** Priority settings survive reboots

#### HTTP API Extensions

- **GET /api/dimmers/status** - Added `priority` field to dimmer status
- **GET /api/dimmers/{id}/config** - Priority included in dimmer configuration
- **POST /api/dimmers/{id}/config** - Set dimmer priority via JSON body
- **GET /api/relays/status** - Added `priority` field to relay status
- **POST /api/relays/{id}/config** - Set relay priority via JSON body

#### Serial Commands (2 new)

- `dimmer-priority <id> [priority]` - Get or set dimmer priority
- `relay-priority <id> [priority]` - Get or set relay priority

#### Web Interface Updates

- **Dimmers page:** Priority input field in configuration modal (0-255)
- **Relays page:** Priority input field in configuration modal (0-255)
- Helper text: "Devices with priority 0 activate first, then 1, 2, etc."
- Real-time priority configuration with immediate NVS save

#### RouterController Enhancements

- **Priority map system:** Dynamic grouping of devices by priority level
- **Memory optimized:** Max 16 active priority levels, dynamic allocation
- **Auto-refresh:** Priority map rebuilds on device configuration changes
- **Simplified relay control:** Unconditional ON/OFF for v1.3.0 (power-based logic deferred)

#### Documentation

- **13_PRIORITY_SYSTEM_EN.md** - Complete priority system guide (English)
  - HTTP API reference with priority endpoints
  - Serial command examples
  - AUTO mode behavior explanation
  - Configuration examples and best practices
  - Troubleshooting guide
- Updated CHANGELOG.md with v1.3.0 features

### Changes

- **RouterController:** Refactored from single-dimmer to multi-device architecture
- **Priority system:** Default priority = 0 for all devices (backward compatible)
- **Relay control:** Simplified to ON/OFF logic (smart power control planned for v1.4.0)

### Migration Notes

- **Existing installations:** All devices default to priority 0 (legacy behavior preserved)
- **To use priority system:** Configure different priorities via web UI, HTTP API, or serial commands
- **No breaking changes:** Existing API endpoints remain fully compatible

### Technical Details

- **Equal % algorithm:** `device_delta = total_delta / device_count`
- **Proportional power:** Automatic (each device's 100% = nominal power)
- **Cascade threshold:** 99.9% for dimmers (0.5% min_level margin)
- **Priority storage:** NVS keys `d{id}_prio` for dimmers, `r{id}_prio` for relays
- **Implementation files:**
  - `components/comm/src/WebServerManager.cpp` - HTTP API handlers
  - `components/acrouter_hal/src/RouterController.cpp` - Priority algorithm
  - `components/comm/web/pages/DimmersPage.h` - Web UI (dimmers)
  - `components/comm/web/pages/RelaysPage.h` - Web UI (relays)

---

## [1.2.0] - 2025-12-24

**MQTT Integration & Home Assistant Support**

### What's New

#### MQTT Integration

- Full MQTT client with auto-reconnect and Last Will & Testament
- Hierarchical topic structure: `acrouter/{device_id}/category/name`
- Real-time metrics publishing (configurable interval)
- Remote control via MQTT commands
- Configuration via MQTT topics

#### Home Assistant Auto-Discovery

- Automatic entity creation in Home Assistant
- All sensors: voltage, power (grid/solar/load), currents, dimmer level
- Controls: mode selector, manual dimmer, emergency stop, reboot
- Device grouping with firmware version info

#### Web UI MQTT Settings

- New MQTT configuration page at `/mqtt`
- Enable/disable toggle
- Broker URL, credentials configuration
- Device ID and name settings
- Publish interval adjustment
- Connection status indicator

#### Console Commands (13 new)

- `mqtt-status`, `mqtt-config` - View status and configuration
- `mqtt-broker`, `mqtt-user`, `mqtt-pass` - Connection settings
- `mqtt-device-id`, `mqtt-device-name` - Device identification
- `mqtt-interval`, `mqtt-ha-discovery` - Publishing options
- `mqtt-enable`, `mqtt-disable`, `mqtt-reconnect`, `mqtt-publish` - Control

#### Documentation

- Complete MQTT guide (English and Russian)
- Home Assistant integration guide with examples
- Dashboard and automation examples for HA

---

## [1.1.0] - 2025-12-23

**GitHub OTA Updates & ACS712 Calibration**

### What's New

#### OTA Updates from GitHub

- Check for updates directly from GitHub Releases
- One-click firmware update from web interface
- Console commands: `ota-check`, `ota-update-github`, `ota-rollback`
- Version comparison with changelog display

#### ACS712 Sensor Calibration

- Calibrated ACS712-10A, ACS712-30A, ACS712-50A profiles
- Automatic multiplier application at startup
- Improved accuracy (±2% in linear range)

#### Web UI Enhancements

- Firmware version in header
- Footer with uptime, heap, and links
- Dashboard visual improvements
- Better mobile/tablet responsiveness

---

## [1.0.0] - 2025-12-18

**First stable release - Phase 1 complete**

### What's New

#### Solar Router Control
- 6 operating modes: OFF, AUTO, ECO, OFFGRID, MANUAL, BOOST
- Automatic power routing from solar to loads
- Phase-angle AC dimming control (0-100%)
- Real-time power measurement and control

#### Sensor Support
- **Voltage sensors:** ZMPT101B, AC230V
- **Current sensors:**
  - SCT-013 series: 5A, 10A, 20A, 30A, 50A, 60A, 80A, 100A
  - ACS712 series: 5A, 20A, 30A
- Per-channel calibration (multiplier + offset)

#### Connectivity
- WiFi support (AP + Station modes)
- Web interface with REST API
- WebSocket real-time updates
- Serial console (20+ commands)
- OTA firmware updates
- NTP time synchronization

#### Configuration
- Persistent configuration (NVS storage)
- Hardware configuration manager
- Sensor driver profiles
- Web-based configuration interface

### Documentation
- English and Russian versions
- Hardware setup guide
- Command reference
- API documentation
- Sensor calibration guide
- Router modes explained

### System Requirements
- ESP32 (dual-core, 240MHz)
- ESP-IDF v5.x
- 4MB Flash minimum

### Known Limitations
- No MQTT/Home Assistant integration
- No historical data storage
- No temperature sensors
- Basic web UI (no charts)
- No time-based scheduling

---

## Upcoming Features (Phase 3)

Planned for future releases:

- Time-based scheduling
- Temperature sensors (DS18B20)
- Historical data and analytics
- Improved web UI with charts
- Multi-load prioritization

---

**Project:** [ACRouter on GitHub](https://github.com/yourusername/ACRouter)
**Documentation:** See `docs/` folder
**Support:** GitHub Issues
