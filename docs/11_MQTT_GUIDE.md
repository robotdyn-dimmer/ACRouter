# 12. MQTT Integration Guide

**Version:** 1.2.0
**Date:** 2025-12-24

Complete guide for ACRouter MQTT connectivity, including topic structure and Home Assistant integration.

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Quick Start](#quick-start)
- [Topic Structure](#topic-structure)
- [Console Commands](#console-commands)
- [Configuration](#configuration)
- [Testing Topics](#testing-topics)
- [Troubleshooting](#troubleshooting)

---

## Overview

ACRouter supports MQTT protocol for:

- **Real-time monitoring** - Power metrics, voltage, currents, router status
- **Remote control** - Mode switching, dimmer control, emergency stop
- **Configuration** - Adjust parameters remotely
- **Home Assistant integration** - Auto-discovery for seamless smart home integration

The MQTT client is based on ESP-IDF's `esp-mqtt` library with automatic reconnection, Last Will & Testament (LWT), and QoS support.

---

## Features

| Feature | Description |
|---------|-------------|
| Auto-reconnect | Automatically reconnects if connection is lost |
| LWT (Last Will) | Publishes "offline" status when device disconnects |
| Retained messages | Status and config topics are retained |
| QoS levels | QoS 0 for metrics, QoS 1 for commands |
| HA Discovery | Automatic Home Assistant entity creation |
| JSON aggregation | Combined metrics in single JSON message |
| NVS persistence | Configuration saved across reboots |

---

## Quick Start

### 1. Enable MQTT via Serial Console

```bash
# Set broker URL (replace with your broker IP)
mqtt-broker mqtt://192.168.1.10:1883

# Enable MQTT
mqtt-enable

# Check status
mqtt-status
```

### 2. Verify Connection

```text
======================================================
  MQTT Status
======================================================
State:         Connected
Enabled:       Yes
Broker:        mqtt://192.168.1.10:1883
Device ID:     039C7C
Device Name:   (default)
HA Discovery:  Enabled
Pub Interval:  5000 ms
------------------------------------------------------
Uptime:        125 sec
Published:     250 messages
Received:      3 messages
======================================================
```

### 3. Subscribe to Topics

Using Mosquitto client tools as an example:

```bash
# Subscribe to all ACRouter topics
mosquitto_sub -h 192.168.1.10 -t "acrouter/#" -v
```

---

## Topic Structure

All topics follow the pattern: `acrouter/{device_id}/category/name`

The `{device_id}` is auto-generated from the last 6 characters of the MAC address (e.g., `039C7C`) or can be set manually.

### Status Topics (Retained)

| Topic | Description | Example Value |
|-------|-------------|---------------|
| `status/online` | Device availability (LWT) | `online` / `offline` |
| `status/mode` | Current router mode | `auto`, `manual`, `boost`... |
| `status/state` | Controller state | `idle`, `increasing`, `at_max`... |
| `status/dimmer` | Dimmer level | `0` - `100` (%) |
| `status/wifi_rssi` | WiFi signal strength | `-65` (dBm) |

### Metrics Topics (Not Retained)

Published every 5 seconds (configurable).

| Topic | Description | Unit |
|-------|-------------|------|
| `metrics/voltage` | AC voltage RMS | V |
| `metrics/power_grid` | Grid power (+ import, - export) | W |
| `metrics/power_solar` | Solar/generation power | W |
| `metrics/power_load` | Load power consumption | W |
| `metrics/current_grid` | Grid current | A |
| `metrics/current_solar` | Solar current | A |
| `metrics/current_load` | Load current | A |
| `metrics/direction` | Power flow direction | `consuming`, `supplying`, `balanced` |

### Config Topics (Retained)

| Topic | Description | Range |
|-------|-------------|-------|
| `config/control_gain` | P-controller gain | 10 - 1000 |
| `config/balance_threshold` | Balance deadband | 0 - 1000 W |
| `config/manual_level` | Manual dimmer level | 0 - 100 % |

### Command Topics (Write-Only)

Send commands by publishing to these topics:

| Topic | Description | Payload |
|-------|-------------|---------|
| `command/mode` | Set router mode | `off`, `auto`, `eco`, `offgrid`, `manual`, `boost` |
| `command/dimmer` | Set dimmer (manual mode) | `0` - `100` |
| `command/emergency_stop` | Emergency stop | any value |
| `command/reboot` | Restart device | any value |
| `command/refresh` | Force publish all data | any value |

### System Topics (Retained)

| Topic | Description | Example |
|-------|-------------|---------|
| `system/version` | Firmware version | `1.1.0` |
| `system/ip` | Device IP address | `192.168.1.100` |
| `system/mac` | MAC address | `AA:BB:CC:DD:EE:FF` |
| `system/uptime` | Uptime in seconds | `3600` |
| `system/free_heap` | Free heap memory | `150000` |

### JSON Aggregated Topics

For efficiency, metrics are also published as aggregated JSON:

**Topic:** `json/metrics`

```json
{
  "voltage": 230.5,
  "power_grid": -150.2,
  "power_solar": 1250.0,
  "power_load": 1100.0,
  "current_grid": 0.65,
  "current_solar": 5.43,
  "current_load": 4.78,
  "direction": "supplying"
}
```

---

## Console Commands

### Connection Management

| Command | Description |
|---------|-------------|
| `mqtt-status` | Show connection status and statistics |
| `mqtt-enable` | Enable MQTT client |
| `mqtt-disable` | Disable MQTT client |
| `mqtt-reconnect` | Force reconnection |
| `mqtt-publish` | Force publish all data immediately |

### Configuration

| Command | Description |
|---------|-------------|
| `mqtt-config` | Show all MQTT configuration |
| `mqtt-broker <url>` | Set broker URL (e.g., `mqtt://192.168.1.10:1883`) |
| `mqtt-user <username>` | Set authentication username |
| `mqtt-pass <password>` | Set authentication password |
| `mqtt-device-id <id>` | Set custom device ID for topics |
| `mqtt-device-name <name>` | Set device name (for Home Assistant) |
| `mqtt-interval <ms>` | Set publish interval (1000-60000 ms) |
| `mqtt-ha-discovery <0\|1>` | Enable/disable Home Assistant discovery |

### Examples

```bash
# Configure broker with authentication
mqtt-broker mqtt://192.168.1.10:1883
mqtt-user myuser
mqtt-pass mypassword
mqtt-enable

# Set custom device name for Home Assistant
mqtt-device-name "Solar Router Kitchen"

# Reduce publish frequency to 10 seconds
mqtt-interval 10000

# Disable Home Assistant auto-discovery
mqtt-ha-discovery 0
```

---

## Configuration

### NVS Storage Keys

All MQTT settings are persisted in NVS (Non-Volatile Storage):

| Key | Description |
|-----|-------------|
| `mqtt_enabled` | MQTT enabled flag |
| `mqtt_broker` | Broker URL |
| `mqtt_user` | Username |
| `mqtt_pass` | Password |
| `mqtt_dev_id` | Device ID |
| `mqtt_dev_name` | Device name |
| `mqtt_interval` | Publish interval |
| `mqtt_ha_disc` | HA Discovery flag |

### Default Values

| Parameter | Default |
|-----------|---------|
| Enabled | No |
| Broker | (empty) |
| Device ID | Auto-generated from MAC |
| Publish Interval | 5000 ms |
| HA Discovery | Enabled |

---

## Testing Topics

You can use any MQTT client to test topics. Examples below use Mosquitto client tools (`mosquitto_sub` / `mosquitto_pub`).

### Subscribe to All Topics

```bash
mosquitto_sub -h 192.168.1.10 -t "acrouter/#" -v
```

### Subscribe to Specific Topics

```bash
# Status only
mosquitto_sub -h 192.168.1.10 -t "acrouter/039C7C/status/#" -v

# Metrics only
mosquitto_sub -h 192.168.1.10 -t "acrouter/039C7C/metrics/#" -v

# JSON aggregated
mosquitto_sub -h 192.168.1.10 -t "acrouter/039C7C/json/#" -v
```

### Send Commands

```bash
# Change mode to AUTO
mosquitto_pub -h 192.168.1.10 -t "acrouter/039C7C/command/mode" -m "auto"

# Set dimmer to 50% (in MANUAL mode)
mosquitto_pub -h 192.168.1.10 -t "acrouter/039C7C/command/dimmer" -m "50"

# Emergency stop
mosquitto_pub -h 192.168.1.10 -t "acrouter/039C7C/command/emergency_stop" -m "1"

# Reboot device
mosquitto_pub -h 192.168.1.10 -t "acrouter/039C7C/command/reboot" -m "1"

# Force refresh all data
mosquitto_pub -h 192.168.1.10 -t "acrouter/039C7C/command/refresh" -m "1"
```

### Update Configuration via MQTT

```bash
# Set control gain
mosquitto_pub -h 192.168.1.10 -t "acrouter/039C7C/config/control_gain/set" -m "200"

# Set balance threshold
mosquitto_pub -h 192.168.1.10 -t "acrouter/039C7C/config/balance_threshold/set" -m "25"
```

---

## Troubleshooting

### Connection Issues

**Symptom:** `esp-tls: select() timeout`

**Solutions:**

1. Check broker is running and accessible
2. Check firewall allows port 1883
3. Verify broker URL format: `mqtt://IP:PORT`
4. Test broker connectivity from another client

### "Client has started" Error

**Symptom:** Repeated `mqtt_client: Client has started` errors

**Solution:** This was a bug in earlier versions. Update to latest firmware where `esp_mqtt_client_start()` is called only once.

### WiFi Not Connected

**Symptom:** MQTT doesn't connect on boot

**Explanation:** MQTT now waits for WiFi connection before attempting to connect. Check WiFi status first:

```bash
wifi-status
```

### Messages Not Received

**Symptom:** Commands sent but not processed

**Solutions:**

1. Check topic spelling (case-sensitive)
2. Verify device ID matches: `mqtt-status`
3. Check message is received: device log shows `MQTT: Command: mode = auto`

### Home Assistant Not Discovering

**Symptom:** Device doesn't appear in Home Assistant

**Solutions:**

1. Verify HA Discovery is enabled: `mqtt-ha-discovery 1`
2. Check HA MQTT integration is configured
3. Force republish: `mqtt-publish` or restart device
4. Check HA logs for discovery messages

---

## See Also

- [12_HOME_ASSISTANT.md](12_HOME_ASSISTANT.md) - Home Assistant integration details
- [07_COMMANDS_EN.md](07_COMMANDS_EN.md) - Complete command reference
- [05_API_REFERENCE_EN.md](05_API_REFERENCE_EN.md) - REST API documentation

---

**Last Updated:** 2025-12-24
