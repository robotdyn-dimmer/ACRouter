# ACRouter - Development Changelog

Internal development log tracking all changes, experiments, and iterations.
For public release notes, see [CHANGELOG_PUBLIC.md](CHANGELOG_PUBLIC.md).

---

## [Unreleased] - 1.3.0-dev

### Planned (Future)

- **Web UI Improvements** - Phase 1 enhancements
  - Sensor profile selector with dropdown (auto-fill multiplier/offset)
  - Documentation: `docs/WEBUI_IMPLEMENTATION_SPEC.md`

---

## [1.2.0] - 2025-12-24

### Added

- **MQTT Integration - COMPLETE**
  - Full MQTT client based on ESP-IDF `esp-mqtt` library
  - Auto-reconnect, Last Will & Testament (LWT), QoS support
  - Files: `components/comm/include/MQTTManager.h`, `src/MQTTManager.cpp`

  - **Topic Structure**
    - `status/*` - Device status (online, mode, state, dimmer, wifi_rssi) - Retained
    - `metrics/*` - Power metrics (voltage, power_grid, power_solar, power_load, currents) - Not retained
    - `config/*` - Configuration (control_gain, balance_threshold, manual_level) - Retained
    - `command/*` - Commands (mode, dimmer, emergency_stop, reboot, refresh) - Write-only
    - `system/*` - System info (version, ip, mac, uptime, free_heap) - Retained
    - `json/metrics` - Aggregated JSON metrics

  - **Home Assistant Auto-Discovery**
    - Automatic entity creation for all sensors and controls
    - Device grouping with manufacturer, model, sw_version
    - Entity types: sensor, select, number, button, binary_sensor
    - File: `docs/13_HOME_ASSISTANT.md`

  - **Console Commands** (13 new commands)
    - `mqtt-status` - Show connection status and statistics
    - `mqtt-config` - Show MQTT configuration
    - `mqtt-broker <url>` - Set broker URL
    - `mqtt-user <name>` - Set username
    - `mqtt-pass <pass>` - Set password
    - `mqtt-device-id <id>` - Set device ID for topics
    - `mqtt-device-name <name>` - Set device name for HA
    - `mqtt-interval <ms>` - Set publish interval (1000-60000)
    - `mqtt-ha-discovery <0|1>` - Enable/disable HA discovery
    - `mqtt-enable` - Enable MQTT client
    - `mqtt-disable` - Disable MQTT client
    - `mqtt-reconnect` - Force reconnection
    - `mqtt-publish` - Force publish all data
    - File: `components/utils/src/SerialCommand.cpp`

  - **Web UI MQTT Settings Page**
    - Enable/Disable toggle
    - Broker URL, username, password configuration
    - Device ID and name settings
    - Publish interval slider
    - Home Assistant Discovery toggle
    - Real-time connection status indicator
    - Test/Reconnect buttons
    - File: `components/comm/web/pages/MQTTPage.h`

  - **Web API Endpoints**
    - `GET /api/mqtt/status` - Connection status
    - `GET /api/mqtt/config` - Current configuration
    - `POST /api/mqtt/config` - Update configuration
    - `POST /api/mqtt/reconnect` - Force reconnect
    - `POST /api/mqtt/publish` - Force publish
    - File: `components/comm/src/WebServerManager.cpp`

  - **Documentation**
    - `docs/12_MQTT_GUIDE.md` - Complete MQTT integration guide (EN)
    - `docs/12_MQTT_GUIDE_RU.md` - Complete MQTT integration guide (RU)
    - `docs/13_HOME_ASSISTANT.md` - Home Assistant integration guide
    - Updated `docs/07_COMMANDS_EN.md` - MQTT commands section

  - **NVS Configuration**
    - All MQTT settings persisted across reboots
    - Keys: mqtt_enabled, mqtt_broker, mqtt_user, mqtt_pass, mqtt_dev_id, mqtt_dev_name, mqtt_interval, mqtt_ha_disc

---

## [1.1.0] - 2025-12-23

### Added (2025-12-22/23)

- **GitHub OTA Update System** - COMPLETE
  - **GitHub Releases integration** for automatic update checks
    - `GitHubOTAChecker` class with GitHub API integration
    - Version comparison algorithm (semver with dev support)
    - 5-minute caching to avoid GitHub rate limits
    - Files: `components/comm/include/GitHubOTAChecker.h`, `src/GitHubOTAChecker.cpp`

  - **Console commands** (5 new commands)
    - `ota-check` - Check GitHub for latest release
    - `ota-update-github` - Download and install from GitHub
    - `ota-update-url <url>` - Download from custom URL
    - `ota-rollback` - Rollback to previous partition
    - `ota-info` - Display partition information
    - File: `components/utils/src/SerialCommand.cpp`

  - **OTAManager enhancements**
    - New method: `updateFromURL(const char* url)`
    - HTTP streaming download with progress reporting
    - Critical task suspension before OTA
    - File: `components/comm/src/OTAManager.cpp`

  - **Web UI integration**
    - "Check for Updates" button with real-time status
    - Changelog display before update
    - Two update sources: GitHub release + local file upload
    - Update available notification with release info
    - Up-to-date confirmation message
    - Progress indicator during download
    - Files: `components/comm/web/pages/OTAPage.h`

  - **Web API endpoints**
    - `GET /api/ota/check-github` - Check for updates
    - `POST /api/ota/update-github` - Start GitHub update
    - File: `components/comm/src/WebServerManager.cpp`

  - **Documentation**
    - Complete specification: `docs/OTA_IMPLEMENTATION_SPEC.md`
    - Updated commands reference: `docs/07_COMMANDS_EN.md`

  - **Web UI Enhancements** (by user)
    - Enhanced header with firmware version display
    - Footer component with uptime, heap, and links
    - Dashboard visual enhancements (progress bars, larger values)
    - Responsive design fixes for tablets and mobile
    - Touch-friendly form controls (44px minimum)

### Added (2025-01-15)
- **ACS712 Sensor Support - Full Calibration**
  - Added ACS712-10A profile (compatible sensor, not original Allegro)
    - Enum value: `ACS712_10A = 11`
    - Calibrated multiplier: 22.0 A/V
    - Linear range: <10A (saturation warning above 10A)
    - Accuracy: ±2% in linear range
    - Calibration data: 4.2A→0.194V, 8.35A→0.375V
    - File: `CurrentSensorDrivers.h`

  - Added ACS712-50A profile (compatible sensor)
    - Enum value: `ACS712_50A = 14`
    - Calibrated multiplier: 111.0 A/V (refined from 116.0)
    - Test range: 4-29A
    - Calibration data: 4.2A→0.040V, 8.35A→0.076V, 12.1A→0.110V, 15.9A→0.141V, 19.3A→0.171V, 22.6A→0.200V, 26.3A→0.229V, 29.23A→0.257V
    - File: `CurrentSensorDrivers.h`

  - Calibrated ACS712-30A (existing profile)
    - Updated multiplier: 65.9 A/V (from theoretical 22.96 A/V)
    - Accuracy: ±2% over 4-23A range
    - Calibration data: 4.2A→0.067V, 8.35A→0.127V, 12.1A→0.183V, 15.9A→0.235V, 19.3A→0.288V, 22.6A→0.342V
    - File: `CurrentSensorDrivers.h`

  - Auto-apply driver multipliers at startup
    - Automatically loads multiplier from sensor profile to NVS config
    - Ensures calibrated values are used without manual configuration
    - Applied before PowerMeterADC initialization
    - File: `main/main.cpp`

  - Updated help text for ACS712 variants
    - Added ACS712-10A and ACS712-50A to command help
    - Files: `SerialCommand.cpp` lines 763, 1444

### Technical Details
- **Voltage divider circuit:** OUT → 10K → GPIO → (15K + 1K trim) → GND, 1nF filter
- **Divider ratio:** ~0.66 (trimmed for DC=1.65V at 0A)
- **Calibration method:** Linear regression from V_rms vs actual current measurements
- **Sensor characteristics:**
  - ACS712-30A: 66 mV/A @ 5V (datasheet) → 65.9 A/V measured
  - ACS712-10A: ~140 mV/A @ 5V (estimated) → 22.0 A/V measured, saturation >10A
  - ACS712-50A: ~40 mV/A @ 5V (estimated) → 111.0 A/V measured

### Notes
- ACS712-5A and ACS712-20A still have theoretical multipliers (NOT calibrated)
- All calibrations performed 2025-01-15 with compatible sensors (non-Allegro)

---

## [1.0.0] - 2025-12-18

### Fixed (Critical)
- **PowerMeterADC indexing bug** - CRITICAL BUG
  - **Symptom:** RouterController received 0.0A for current readings
  - **Root cause:** Sequential indexing (0,1,2,3) instead of sensor-type-based indexing
  - **Impact:** Router control algorithm was completely broken
  - **Fixed in 3 locations:**
    1. RMS calculation loop (`PowerMeterADC.cpp:357`)
    2. Direction detection (`PowerMeterADC.cpp:373`)
    3. Debug logging (`PowerMeterADC.cpp:435`)
  - **Solution:** Changed from `for(int i=0; i<4; i++)` to proper sensor type iteration
  - Files: `components/power/src/PowerMeterADC.cpp`

### Added
- **debug-adc command**
  - Console-controlled ADC debug logging
  - Usage: `debug-adc <period>` (0=disable, >0=seconds)
  - Shows RMS values, phase direction, and sensor states
  - Integrated with PowerMeterADC logging system
  - Files: `SerialCommand.cpp`, `PowerMeterADC.cpp`

### Changed
- **TODO.md rewrite**
  - Complete project status overview
  - Phase-based roadmap (Phase 1 complete, Phase 2 planned)
  - Detailed feature tracking with legend
  - Statistics: 11 components, 13 sensor types, 6 modes
  - File: `TODO.md`

### Core Features (Initial Release)

- **Hardware Abstraction Layer**
  - DimmerHAL: Phase-angle AC dimming (rbdimmerESP32)
  - PowerMeterADC: DMA-based ADC sampling (20kHz, 200ms window)
  - IndicatorLED: Status patterns

- **Router Control**
  - RouterController: 6 operating modes (OFF/AUTO/ECO/OFFGRID/MANUAL/BOOST)
  - Proportional controller (P-регулятор)
  - Mode validation against sensor configuration
  - Emergency stop functionality

- **Sensor Support**
  - Voltage sensors: ZMPT101B, AC230V
  - Current sensors: SCT-013 series (8 variants: 5A-100A)
  - Current sensors: ACS712 series (3 variants: 5A, 20A, 30A - theoretical values)
  - Calibration framework: multiplier + offset per channel

- **Configuration**
  - ConfigManager: NVS-based persistent storage
  - HardwareConfigManager: Sensor driver profiles
  - WiFi configuration (AP + STA modes)
  - OTA firmware updates

- **Communication**
  - WiFiManager: Auto-reconnect, AP fallback
  - NTPManager: Time synchronization with multiple servers
  - WebServerManager: REST API + WebSocket
  - SerialCommand: 20+ console commands

- **Documentation**
  - 11+ documents (EN + RU versions)
  - Command reference, API reference, hardware guide
  - Sensor calibration guide, router modes explained

### Known Limitations
- ACS712 sensors: theoretical multipliers only (5A, 20A, 30A NOT calibrated)
- No MQTT/Home Assistant integration
- No historical data storage
- No temperature sensors support
- Basic Web UI (no charts)
- No time-based scheduling

---

## Version Numbering Scheme

- **X.Y.Z** - Release version (stable)
  - X = Major (breaking changes)
  - Y = Minor (new features)
  - Z = Patch (bugfixes)

- **X.Y.Z-dev** - Development version (unreleased)
  - Incremental changes tracked here
  - Merged to public changelog before release

---

**Changelog started:** 2025-12-18
**Format:** Based on [Keep a Changelog](https://keepachangelog.com/)
**Versioning:** [Semantic Versioning](https://semver.org/)
