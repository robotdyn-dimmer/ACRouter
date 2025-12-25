# 12. Home Assistant Integration

**Version:** 1.2.0
**Date:** 2025-12-24

Complete guide for integrating ACRouter with Home Assistant using MQTT Auto-Discovery.

---

## Table of Contents

- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Setup](#setup)
- [Auto-Discovered Entities](#auto-discovered-entities)
- [Dashboard Example](#dashboard-example)
- [Automations](#automations)
- [Energy Dashboard](#energy-dashboard)
- [Troubleshooting](#troubleshooting)

---

## Overview

ACRouter supports **MQTT Auto-Discovery**, which means Home Assistant automatically creates all entities when the device connects. No manual YAML configuration required!

### Features

- Automatic device and entity creation
- Real-time power monitoring sensors
- Mode selection control
- Parameter adjustment (gain, threshold)
- Emergency stop button
- Availability tracking (online/offline)
- Device grouping with manufacturer info

---

## Prerequisites

1. **Home Assistant** installed and running
2. **MQTT Broker** (Mosquitto recommended)
   - Can be the Home Assistant Mosquitto add-on
   - Or external broker
3. **MQTT Integration** configured in Home Assistant
4. **ACRouter** with MQTT enabled

---

## Setup

### Step 1: Install MQTT Broker

**Option A: Home Assistant Add-on (Recommended)**

1. Go to Settings > Add-ons > Add-on Store
2. Search for "Mosquitto broker"
3. Click Install
4. Start the add-on
5. Configure credentials in add-on configuration

**Option B: External Broker**

See [11_MQTT_GUIDE.md](11_MQTT_GUIDE.md#mosquitto-broker-setup) for Mosquitto setup.

### Step 2: Configure MQTT Integration

1. Go to Settings > Devices & Services
2. Click "Add Integration"
3. Search for "MQTT"
4. Enter broker details:
   - Broker: `localhost` (if using add-on) or broker IP
   - Port: `1883`
   - Username/Password (if configured)

### Step 3: Configure ACRouter

Via serial console:

```bash
# Set broker URL
mqtt-broker mqtt://192.168.1.10:1883

# Set credentials (if required)
mqtt-user homeassistant
mqtt-pass your_password

# Set friendly device name
mqtt-device-name "Solar Router"

# Ensure HA Discovery is enabled
mqtt-ha-discovery 1

# Enable MQTT
mqtt-enable
```

### Step 4: Verify Discovery

1. Go to Settings > Devices & Services > MQTT
2. Look for "ACRouter Solar" device
3. All entities should be automatically created

---

## Auto-Discovered Entities

### Sensors

| Entity | Description | Unit | Device Class |
|--------|-------------|------|--------------|
| `sensor.acrouter_grid_power` | Grid import/export power | W | power |
| `sensor.acrouter_solar_power` | Solar generation power | W | power |
| `sensor.acrouter_load_power` | Load consumption power | W | power |
| `sensor.acrouter_voltage` | AC voltage | V | voltage |
| `sensor.acrouter_dimmer` | Dimmer level | % | - |
| `sensor.acrouter_wifi_signal` | WiFi signal strength | dBm | signal_strength |

### Select

| Entity | Description | Options |
|--------|-------------|---------|
| `select.acrouter_router_mode` | Router operating mode | off, auto, eco, offgrid, manual, boost |

### Numbers

| Entity | Description | Range |
|--------|-------------|-------|
| `number.acrouter_control_gain` | P-controller gain | 10-1000 |
| `number.acrouter_balance_threshold` | Balance deadband | 0-1000 W |
| `number.acrouter_manual_level` | Manual dimmer level | 0-100% |

### Buttons

| Entity | Description |
|--------|-------------|
| `button.acrouter_emergency_stop` | Emergency stop |
| `button.acrouter_reboot` | Restart device |
| `button.acrouter_refresh` | Force data refresh |

### Device Info

All entities are grouped under a single device with:

- **Name:** ACRouter Solar (or custom name)
- **Manufacturer:** RobotDyn
- **Model:** ACRouter
- **SW Version:** 1.2.0
- **Configuration URL:** http://device_ip

---

## Dashboard Example

### Lovelace Card YAML

```yaml
type: entities
title: ACRouter Solar
entities:
  - entity: select.acrouter_router_mode
    name: Mode
  - entity: sensor.acrouter_dimmer
    name: Dimmer Level
  - type: divider
  - entity: sensor.acrouter_grid_power
    name: Grid Power
  - entity: sensor.acrouter_solar_power
    name: Solar Power
  - entity: sensor.acrouter_load_power
    name: Load Power
  - type: divider
  - entity: sensor.acrouter_voltage
    name: Voltage
  - entity: sensor.acrouter_wifi_signal
    name: WiFi Signal
```

### Gauge Card for Power

```yaml
type: gauge
entity: sensor.acrouter_grid_power
name: Grid Power
min: -3000
max: 3000
severity:
  green: -3000
  yellow: 0
  red: 500
```

### Custom Button Card

```yaml
type: horizontal-stack
cards:
  - type: button
    entity: button.acrouter_emergency_stop
    name: STOP
    icon: mdi:stop
    tap_action:
      action: call-service
      service: button.press
      target:
        entity_id: button.acrouter_emergency_stop
    show_state: false
  - type: button
    entity: button.acrouter_reboot
    name: Reboot
    icon: mdi:restart
    tap_action:
      action: call-service
      service: button.press
      target:
        entity_id: button.acrouter_reboot
    show_state: false
```

### Complete Dashboard View

```yaml
title: Solar Router
views:
  - title: Overview
    cards:
      - type: vertical-stack
        cards:
          - type: entities
            title: Status
            entities:
              - entity: select.acrouter_router_mode
              - entity: sensor.acrouter_dimmer
              - entity: sensor.acrouter_voltage

          - type: glance
            title: Power
            entities:
              - entity: sensor.acrouter_grid_power
                name: Grid
              - entity: sensor.acrouter_solar_power
                name: Solar
              - entity: sensor.acrouter_load_power
                name: Load

          - type: entities
            title: Settings
            entities:
              - entity: number.acrouter_control_gain
              - entity: number.acrouter_balance_threshold
              - entity: number.acrouter_manual_level

          - type: horizontal-stack
            cards:
              - type: button
                entity: button.acrouter_emergency_stop
                name: Emergency Stop
                icon: mdi:stop-circle
              - type: button
                entity: button.acrouter_refresh
                name: Refresh
                icon: mdi:refresh
```

---

## Automations

### Night Boost Mode (Cheap Tariff)

```yaml
alias: "Solar Router - Night Boost"
description: "Enable BOOST mode during cheap night tariff"
trigger:
  - platform: time
    at: "23:00:00"
action:
  - service: select.select_option
    target:
      entity_id: select.acrouter_router_mode
    data:
      option: "boost"
mode: single
```

### Morning Auto Mode

```yaml
alias: "Solar Router - Morning Auto"
description: "Switch to AUTO mode in the morning"
trigger:
  - platform: time
    at: "07:00:00"
action:
  - service: select.select_option
    target:
      entity_id: select.acrouter_router_mode
    data:
      option: "auto"
mode: single
```

### Emergency Stop on High Power

```yaml
alias: "Solar Router - Emergency High Power"
description: "Emergency stop if load exceeds 3000W"
trigger:
  - platform: numeric_state
    entity_id: sensor.acrouter_load_power
    above: 3000
    for:
      seconds: 10
action:
  - service: button.press
    target:
      entity_id: button.acrouter_emergency_stop
  - service: notify.mobile_app
    data:
      message: "ACRouter emergency stop triggered - load exceeded 3000W"
mode: single
```

### Notify on Disconnect

```yaml
alias: "Solar Router - Offline Alert"
description: "Notify when router goes offline"
trigger:
  - platform: state
    entity_id: select.acrouter_router_mode
    to: "unavailable"
    for:
      minutes: 5
action:
  - service: notify.mobile_app
    data:
      message: "ACRouter is offline!"
      title: "Solar Router Alert"
mode: single
```

### Solar Surplus Routing

```yaml
alias: "Solar Router - Auto Enable on Surplus"
description: "Enable AUTO mode when solar surplus detected"
trigger:
  - platform: numeric_state
    entity_id: sensor.acrouter_grid_power
    below: -100
    for:
      minutes: 1
condition:
  - condition: state
    entity_id: select.acrouter_router_mode
    state: "off"
action:
  - service: select.select_option
    target:
      entity_id: select.acrouter_router_mode
    data:
      option: "auto"
mode: single
```

---

## Energy Dashboard

### Configure Energy Sensors

ACRouter power sensors can be integrated with Home Assistant Energy Dashboard.

1. Go to Settings > Dashboards > Energy
2. Add Grid consumption: `sensor.acrouter_grid_power` (when positive)
3. Add Solar production: `sensor.acrouter_solar_power`

**Note:** The Energy Dashboard requires energy sensors (kWh), not power sensors (W). You may need to create template sensors or use integration helpers.

### Template Sensor for Energy (Optional)

```yaml
# configuration.yaml
template:
  - sensor:
      - name: "ACRouter Grid Import Energy"
        unit_of_measurement: "kWh"
        device_class: energy
        state_class: total_increasing
        state: >
          {% set power = states('sensor.acrouter_grid_power') | float(0) %}
          {% if power > 0 %}
            {{ (power / 1000) | round(3) }}
          {% else %}
            0
          {% endif %}
```

---

## Troubleshooting

### Device Not Discovered

1. **Check MQTT connection:**
   ```bash
   mqtt-status
   ```
   Should show `State: Connected`

2. **Check HA Discovery enabled:**
   ```bash
   mqtt-ha-discovery 1
   mqtt-publish
   ```

3. **Check MQTT integration in HA:**
   - Settings > Devices & Services > MQTT
   - Click "Configure" > Check broker settings

4. **Restart discovery:**
   ```bash
   mqtt-reconnect
   ```

### Entities Show "Unavailable"

1. **Check device is online:**
   - Look at `status/online` topic
   - Should be `online`

2. **Check WiFi connection:**
   ```bash
   wifi-status
   ```

3. **Check broker connection:**
   - Verify broker is reachable from HA
   - Test with mosquitto_sub

### Values Not Updating

1. **Check publish interval:**
   ```bash
   mqtt-interval 5000
   ```

2. **Force refresh:**
   ```bash
   mqtt-publish
   ```

3. **Check MQTT messages are being received:**
   ```bash
   mosquitto_sub -h broker_ip -t "acrouter/#" -v
   ```

### Mode Select Not Working

1. **Check command topic subscription:**
   - Device should log: `MQTT: Command: mode = auto`

2. **Verify mode is valid:**
   - Must be: `off`, `auto`, `eco`, `offgrid`, `manual`, `boost`

3. **Check mode compatibility:**
   - Some modes require specific sensors configured

---

## See Also

- [11_MQTT_GUIDE.md](11_MQTT_GUIDE.md) - MQTT setup and topic reference
- [04_ROUTER_MODES.md](04_ROUTER_MODES.md) - Router mode descriptions
- [Home Assistant MQTT Documentation](https://www.home-assistant.io/integrations/mqtt/)

---

**Last Updated:** 2025-12-24
