# ACRouter — Open Source Solar Router Controller

<p align="center">
  <img src="docs/images/acrouter-logo.png" alt="ACRouter Logo" width="200">
</p>

<p align="center">
  <strong>Intelligent AC Power Router for Solar Energy Management</strong>
</p>

<p align="center">
  <a href="#features">Features</a> •
  <a href="#hardware">Hardware</a> •
  <a href="#quick-start">Quick Start</a> •
  <a href="#operating-modes">Modes</a> •
  <a href="#documentation">Docs</a> •
  <a href="#contributing">Contributing</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Platform-ESP32-blue?style=flat-square" alt="Platform">
  <img src="https://img.shields.io/badge/Framework-ESP--IDF%205.5-green?style=flat-square" alt="Framework">
  <img src="https://img.shields.io/badge/License-Open%20Source-orange?style=flat-square" alt="License">
  <img src="https://img.shields.io/badge/Status-Active%20Development-brightgreen?style=flat-square" alt="Status">
</p>

---

## What is ACRouter?

**ACRouter** is an open-source controller that automatically redirects excess solar energy to resistive loads (like water heaters) instead of exporting it to the grid. It helps you maximize self-consumption of your solar power and reduce electricity costs.

<img width="1900" height="1900"  alt="ACrouter_map" src="https://github.com/user-attachments/assets/c6383e47-8361-40e4-8c87-38d9490ab20c" />

### Why ACRouter?

| Problem | Solution |
|---------|----------|
| Excess solar energy exported to grid at low rates | Route it to heat water or other loads |
| Expensive battery storage systems | Use thermal storage (water heater) instead |
| Complex commercial solutions | Simple, open-source, DIY-friendly |
| Fixed on/off control wastes energy | Smooth phase-angle dimming for precise control |

---
Typical daily behavior of a grid-connected PV system with a water heater load.

| Parameter | Without ACRouter | With ACRouter |
|---------|------------------|---------------|
| Daytime solar export | High | Low |
| Self-consumption ratio | ~40–50% | ~70–85% |
| Water heating source | Grid (evening/night) | Solar surplus (daytime) |
| Evening grid import | High | Reduced |
| Battery required | Yes (for storage) | No |
| Energy wasted/exported | Significant | Minimal |
| Grid interaction | Uncontrolled export | Controlled, grid-friendly |
| Overall energy cost | Higher | Lower - reduce 30% |

## Features

### ⚡ Real-Time Power Monitoring
- AC voltage measurement (ZMPT107 sensor)
- Multi-channel current monitoring (up to 3× SCT-013/ACS-712 sensors)
- 12-bit ADC with DMA for high accuracy
- 200ms update cycle (5 updates/second)

### 🎛️ Intelligent Load Control
- Phase-angle AC dimming (0-100% smooth control)
- TRIAC-based, zero-cross synchronized
- Multiple operating modes for different scenarios
- Anti-flicker algorithms

### 📱 Easy Configuration
- Built-in WiFi Access Point for setup
- Modern web dashboard (Material UI)
- REST API for integration
- Serial console for advanced users
- All settings stored in non-volatile memory

### 🔌 Flexible Hardware
- Configurable GPIO pins via web interface
- Support for various sensor types
- Multiple dimmer channels
- Relay outputs for on/off loads

---

## Web Dashboard

The web interface provides:
- **Real-time metrics** — Voltage, current, power for all channels
- **Mode selection** — One-click switching between modes
- **Manual control** — Slider for direct dimmer control
- **WiFi settings** — Network scanning and connection
- **Hardware config** — GPIO pin assignment without reflashing

<p align="center">
<img width="1088" height="860" alt="image" src="https://github.com/user-attachments/assets/c02a0b8c-1656-4c2d-80b2-3915045386ea" />
</p>

---

## Serial console for advanced users

  - Real-time metrics display
  - Command line for hardware and device parameter configuration
  - Operating mode selection

<p align="center">
<img width="1582" height="722" alt="image" src="https://github.com/user-attachments/assets/5b7454cb-5034-4c70-b23f-9d14c9c04934" />
</p>  

---

## Hardware

### Controller Board

ACRouter runs on ESP32-based development boards designed for AC power control.

<img width="1920" height="787" alt="image" src="https://github.com/user-attachments/assets/46ff5f1d-c459-40ed-bd4c-51fe9982f69b" />

| Component | Specification |
|-----------|--------------|
| **MCU** | ESP32-WROOM-32 / ESP32-WROVER |
| **CPU** | Dual-core 240 MHz |
| **Flash** | 4 MB minimum |
| **WiFi** | 802.11 b/g/n |

### Sensors

| Sensor | Purpose | Link |
|--------|---------|------|
| **ZMPT107+ Zero-Cross Detector** | AC Voltage Measurement & AC waveform synchronization | [🛒 Shop](#) |
| **SCT-013** | Current Transformer (current variation) | [🛒 Shop](#) |
| **ACS-712** | Current Transformer (current variation) | [🛒 Shop](#) |


### Dimmers & Control

| Module | Purpose | Link |
|--------|---------|------|
| **AC Dimmer Module** | Phase-angle power control | [🛒 Shop](#) |
| **Relay Module** | On/off load switching | [🛒 Shop](#) |

### Complete Kits

| Kit | Contents | Link |
|-----|----------|------|
| **ACRouter Starter Kit** | Controller + 1 dimmer + voltage sensor + 1 CT | [🛒 Shop](#) |
| **ACRouter Pro Kit** | Controller + 2 dimmers + all sensors + enclosure | [🛒 Shop](#) |

> 💡 **Note:** ACRouter is designed to work with hardware from [rbdimmer.com](https://rbdimmer.com)

---

## Operating Modes

ACRouter supports 6 operating modes to cover different use cases:

| Mode | Description | Best For |
|------|-------------|----------|
| **OFF** | System disabled, dimmer at 0% | Maintenance |
| **AUTO** | Automatic grid balance (P_grid → 0) | ⭐ Standard solar routing |
| **ECO** | Prevent export, allow import | No feed-in tariff |
| **OFFGRID** | Use only solar excess | Off-grid systems |
| **MANUAL** | Fixed dimmer level | Testing, night tariff |
| **BOOST** | Maximum power (100%) | Fast heating |

* AUTO and ECO modes are the primary solar routing modes used in grid-connected systems.

### AUTO Mode — The Heart of Solar Routing

```
☀️ Solar: 3000W    🏠 House: 800W    ⚡ Grid: -2200W (export!)
                              ↓
                    ACRouter detects export
                              ↓
                    Increases dimmer → heats water
                              ↓
☀️ Solar: 3000W    🏠 House: 800W    🔥 Heater: 2200W    ⚡ Grid: 0W ✓
```

---

## Quick Start

### 1. Flash the Firmware

```bash
# Clone the repository
git clone https://github.com/[your-repo]/ACRouter.git
cd ACRouter

# Set up ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# Build and flash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### 2. Connect to ACRouter

After flashing, the device creates a WiFi network:

| Setting | Value |
|---------|-------|
| **SSID** | `ACRouter_XXXXXX` |
| **Password** | `12345678` |
| **IP Address** | `192.168.4.1` |

### 3. Configure via Web Interface

1. Connect to the ACRouter WiFi network
2. Open browser: `http://192.168.4.1`
3. Configure hardware GPIO pins
4. Connect to your home WiFi
5. Select AUTO mode and start saving energy!

---

## Documentation

| Document | Description |
|----------|-------------|
| [📖 User Guide](docs/USER_GUIDE.md) | Complete documentation index |
| [🔧 Hardware Reference](docs/AC_POWER_ROUTER_HARDWARE.md) | Pin assignments and specifications |
| [⚙️ Compilation Guide](docs/02_COMPILATION.md) | Build instructions and troubleshooting |
| [📐 Architecture](docs/03_STRUCTURE.md) | Software modules and API |
| [🎮 Operating Modes](docs/04_ROUTER_MODES.md) | Detailed mode descriptions |
| [🗺️ Roadmap](ROADMAP.md) | Development plans |

---

## REST API

ACRouter exposes a REST API for integration with home automation systems:

```bash
# Get current status
curl http://192.168.4.1/api/status

# Set mode to AUTO
curl -X POST http://192.168.4.1/api/mode -d '{"mode": 1}'

# Get power metrics
curl http://192.168.4.1/api/metrics
```

See [API Documentation](docs/WEB_API.md) for full endpoint list.

---

## What ACRouter Is Not

- Not a battery inverter
- Not a smart plug or relay controller
- Not a certified grid protection device
- Not suitable for inductive or electronic loads

## Safety Notice

⚠️ **WARNING: This project involves mains voltage (110V/230V AC)**

- Installation must be performed by a qualified electrician
- Always use proper galvanic isolation
- Install appropriate circuit breakers and RCD/GFCI protection
- Only use with resistive loads (heating elements)
- Not suitable for motors, LEDs, or electronic devices

---

## Contributing

We welcome contributions! Here's how you can help:

- 🐛 **Report bugs** — Open an issue with details
- 💡 **Suggest features** — Share your ideas in discussions
- 📝 **Improve docs** — Fix typos, add examples
- 🔧 **Submit code** — Fork, develop, and create a pull request

Please read our [Contributing Guidelines](CONTRIBUTING.md) before submitting.

---

## Community

- **GitHub Issues** — Bug reports and feature requests
- **Discussions** — Questions and community support
- **Wiki** — Community-contributed configurations

---

## License

ACRouter is open-source software. See [LICENSE](LICENSE) for details.

---

## Acknowledgments

- **RBDimmer** — Hardware platform and dimmer library
- **ESP-IDF** — Espressif IoT Development Framework
- **ArduinoJson** — JSON library for embedded systems
- **Community** — Contributors and testers

---

<p align="center">
  <strong>Made with ⚡ for the solar energy community</strong>
</p>

<p align="center">
  <a href="https://github.com/[your-repo]/ACRouter">⭐ Star this project</a> •
  <a href="https://github.com/[your-repo]/ACRouter/issues">🐛 Report Issue</a> •
  <a href="https://github.com/[your-repo]/ACRouter/discussions">💬 Discuss</a>
</p>
