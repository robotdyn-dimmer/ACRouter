---
author: RobotDyn
description: ACRouter development roadmap — delivered features and planned direction by phase (smart-module v2.0 architecture, connectivity & automation, advanced energy logic, ecosystem).
keywords:
- ACRouter roadmap
- ACRouter planned features
- ACRouter development phases
- RobotDyn solar router
title: ACRouter Roadmap — Delivered & Planned Features
url: acrouter-roadmap
---
[← Glossary](https://www.rbdimmer.com/acrouter-glossary) | [Contents](https://www.rbdimmer.com/acrouter-what-is) | [ChangeLog →](https://www.rbdimmer.com/acrouter-changelog)

# ACRouter Roadmap

This document outlines the planned development direction of the ACRouter project.

All items are subject to change based on community feedback and technical constraints. Legend: ✅ delivered ·
🔍 in research · 💡 open for contributions; unchecked items are planned.

---

## Phase 1 — Core Solar Router (Delivered)

- ✅ Real-time power measurement
- ✅ Phase-angle AC dimming control
- ✅ OFF / AUTO / ECO / OFFGRID / MANUAL / BOOST modes
- ✅ Web dashboard + REST API
- ✅ Hardware abstraction and configuration
- ✅ Non-volatile configuration storage
- ✅ Open-source documentation

**Status:** ✅ Delivered (superseded by the v2.0 architecture — see Phase 2.5)

---

## Phase 2 — Connectivity & Automation

- ✅ MQTT support
- ✅ Home Assistant integration
- ✅ OTA firmware updates
- 🔌 ESPHome-compatible external interface — a native-API endpoint so Home Assistant can consume ACRouter directly *(planned; MQTT + HA auto-discovery already ship as the current HA path)*
- ⏰ Scheduled operation (time-based modes) — run modes on a clock, e.g. low-tariff windows *(planned as a fast-follow after v2.0)*
- 🌡️ External temperature input support

**Status:** ✅ Core delivered (MQTT / HA / OTA); scheduling & temperature planned

---

## Phase 2.5 — Smart-Module Architecture (v2.0, Delivered)

Hardware-validated on ESP32 and ESP32-C2.

- ✅ Smart sensing over I2C (**rbAmp**) with per-channel roles — replaces on-device ADC
- ✅ **DimmerLink** I2C smart dimmers (legacy GPIO/TRIAC dimming removed)
- ✅ **ESP-NOW** remote sensing / dimmer nodes
- ✅ **ESP32-C2** target + **compile tiering** (C2-HTTP / headless C2-MQTT / ESP32)
- ✅ External web app + **config-over-MQTT** provisioning
- ✅ Unified device registry; control loop isolated in its own RTOS task
- ✅ **GRID_LIMIT** operating mode
- ✅ **Multi-load priority cascade** — multiple dimmers, surplus spilling to the next by priority
- ✅ **GPIO relay outputs** driven in the AUTO priority cascade (ESP-NOW relay nodes — future)
- ✅ **Sensor-loss failsafe** (decay-to-off) and whole-cascade Emergency Stop / OFF

**Status:** ✅ Delivered (v2.0.0)

---

## Phase 3 — Advanced Energy Logic

- 🔒 Per-load **power** limits (W) — *(per-load % level limits already ship)*
- 💰 Tariff-aware control logic
- 📈 Smarter anti-flicker algorithms
- 🎯 **Directional** export margin (asymmetric deadband) — *(a symmetric `balance_threshold` deadband already ships)*

**Status:** 🔍 Research / Community discussion

---

## Phase 4 — Ecosystem & Community

- 📐 Reference hardware designs
- 📋 Example configurations
- 🤝 Community-contributed integrations
- 📚 Improved documentation and tutorials

**Status:** 💡 Open for contributions

---

## Contributions

Community feedback, testing, and pull requests are welcome.

Design decisions prioritize stability, transparency, and electrical safety.

---

## How to Influence the Roadmap

1. **Open an Issue** — Describe your use case or feature request
2. **Join Discussions** — Participate in GitHub Discussions
3. **Submit a PR** — Implement a feature and submit for review
4. **Share Feedback** — Report bugs and suggest improvements

Your input helps shape the future of ACRouter! 🚀

---

[← Glossary](https://www.rbdimmer.com/acrouter-glossary) | [Contents](https://www.rbdimmer.com/acrouter-what-is) | [ChangeLog →](https://www.rbdimmer.com/acrouter-changelog)
