---
author: RobotDyn
description: Plain-language glossary for ACRouter — the hardware, electrical, and software terms used across the docs (CT clamp, I2C, pull-up, NVS, contactor, RCD/GFCI, power factor, QoS, serial console, and more).
keywords:
- ACRouter glossary
- CT clamp meaning
- I2C pull-up
- contactor RCD GFCI
- ACRouter terms
title: ACRouter Glossary — Terms Explained
url: acrouter-glossary
---
[← Home Assistant Integration](https://www.rbdimmer.com/acrouter-home-assistant-integration) | [Contents](https://www.rbdimmer.com/acrouter-what-is)

# Glossary

Plain-language definitions of the terms used across the ACRouter documentation.

## Hardware & electrical

- **CT (current transformer) / clamp** — a sensor that clips **around a single wire** to measure the AC
  current flowing through it, without cutting the wire. Clip it around **one** conductor only (both L and
  N together cancel to ≈0). It has an arrow showing current direction — orientation sets the sign.
- **rbAmp** — the smart I2C **measurement** module (current via CT + line voltage).
- **DimmerLink** — the smart I2C **dimmer** module that phase-cuts power to the load.
- **Phase-angle dimming** — controlling AC power by switching the load on partway through each mains
  half-cycle (0–100%). Suitable for **resistive** loads (heaters), not motors or electronics.
- **Zero-cross** — the instant the AC waveform crosses 0 V; the dimmer syncs to it. In v2.0 this is
  handled inside the DimmerLink module.
- **Pull-up resistor** — a resistor (here **4.7 kΩ to 3V3**) that holds an I2C line high when idle. The
  I2C bus needs one pair (SDA + SCL) to work.
- **Contactor** — an electrically-controlled heavy-duty switch/relay for mains circuits. Recommended as
  an **external emergency disconnect** for the load, independent of the firmware.
- **RCD / GFCI** — a residual-current / ground-fault protective device that trips on leakage current.
  **Mandatory** for a mains installation.
- **Galvanic isolation** — an electrical barrier (e.g. a transformer) so the low-voltage logic side isn't
  electrically connected to mains. rbAmp is isolated; treat DimmerLink as non-isolated (see
  [Hardware Guide → Safety](https://www.rbdimmer.com/acrouter-hardware-guide)).

## Connectivity & software

- **I2C** — a two-wire (SDA/SCL) bus that connects the host to the rbAmp/DimmerLink modules. Each module
  has its own address.
- **Hex address** — a device address written in hexadecimal, e.g. `0x51` (= 81 decimal). Modules are
  re-addressable so two of the same kind can share a bus.
- **NVS (Non-Volatile Storage)** — flash storage that keeps settings (config, WiFi, roles) across reboots.
- **AP / STA (WiFi)** — **AP** = the device's own access point (`ACRouter_XXXX` at `192.168.4.1`) for
  first setup; **STA** = the device joined to your home WiFi.
- **REST API** — the device's HTTP/JSON control surface (`GET`/`POST /api/…`).
- **MQTT** — a lightweight publish/subscribe messaging protocol via a broker. Optional — needed only for
  broker-based dashboards, Home Assistant, or headless (C2-MQTT) operation.
- **QoS (MQTT)** — delivery guarantee: **QoS 0** = at most once (fast, may drop); **QoS 1** = at least once.
- **Retained (MQTT)** — the broker keeps the last message on a topic so new subscribers get it immediately.
- **Serial console** — a text terminal over the USB cable at **115200 baud, 8N1** (use PuTTY, the Arduino
  Serial Monitor, or `idf.py monitor`). Lets you configure the device without WiFi.
- **OTA** — Over-The-Air firmware update (ESP32 tier; flash the C2 over serial).

## Measurement & control

- **`power_grid` / import / export** — grid power in watts, **signed**: **+** you're buying from the grid
  (import), **−** you're sending to it (export).
- **Power factor** — the ratio of real to apparent power (0–1); 1.0 for a purely resistive load.
- **`control_gain` / `balance_threshold`** — tuning for the AUTO/ECO control loop (how hard it corrects,
  and the dead-zone around balance). Defaults suit most installs.
- **Role (grid / solar / load / voltage / dimmer)** — what a module measures or drives. Assigned during
  [Commissioning](https://www.rbdimmer.com/acrouter-commissioning); a **grid** role is mandatory for AUTO/ECO.

---

[← Home Assistant Integration](https://www.rbdimmer.com/acrouter-home-assistant-integration) | [Contents](https://www.rbdimmer.com/acrouter-what-is)
