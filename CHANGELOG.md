# ACRouter - Release Notes

Public changelog for stable releases.

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
