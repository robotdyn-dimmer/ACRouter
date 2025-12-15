# 2. Compilation Requirements

## 2.1 Platform and Tools

### Main Platform
- **ESP-IDF version:** 5.5.1 (required)
- **Microcontroller:** ESP32 (classic, not ESP32-S2/S3/C3)
- **Architecture:** Xtensa dual-core LX6
- **CPU frequency:** 240 MHz
- **Minimum Flash:** 4 MB
- **Minimum RAM:** 520 KB (ESP32-WROOM-32 or ESP32-WROVER)

### Frameworks and Components
- **Arduino Core for ESP32:** version 3.x (via ESP-IDF Component Manager)
- **CMake:** minimum version 3.16
- **Python:** 3.8+ (for idf.py scripts)

---

## 2.2 Required Libraries

### Main Dependencies (via IDF Component Manager)

Defined in file [main/idf_component.yml](../main/idf_component.yml):

```yaml
dependencies:
  # Arduino Core 3.x for ESP32
  espressif/arduino-esp32:
    version: "^3.0.0"

  # ArduinoJson v7 - JSON serialization
  bblanchon/arduinojson:
    version: "^7.0.0"

  # IDF built-in components
  idf:
    version: ">=5.0.0"
```

### Built-in ESP-IDF Components (used automatically)

- **nvs_flash** - Non-Volatile Storage for configuration
- **esp_wifi** - WiFi driver and protocols
- **esp_http_server** - Asynchronous web server
- **esp_adc** - ADC driver with DMA support
- **driver** - GPIO, LEDC, Timer drivers
- **freertos** - Real-Time Operating System

### Custom Components (local)

Located in [components/](../components/):

| Component | Path | Purpose |
|-----------|------|---------|
| **comm** | `components/comm` | WiFi, WebServer, REST API, Material UI interface |
| **hal** | `components/hal` | Hardware Abstraction Layer (DimmerHAL, PowerMeterADC) |
| **utils** | `components/utils` | ConfigManager, HardwareConfigManager, SerialCommand |
| **control** | `components/control` | RouterController, control algorithms |
| **rbdimmer** | `components/rbdimmer` | RBDimmer library (TRIAC dimmer) |

---

## 2.3 Build Configuration (sdkconfig)

### Main Parameters

```ini
# Microcontroller
CONFIG_IDF_TARGET="esp32"
CONFIG_SOC_CPU_CORES_NUM=2
CONFIG_ESP32_DEFAULT_CPU_FREQ_240=y

# Flash configuration
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y

# FreeRTOS
CONFIG_FREERTOS_HZ=1000                    # 1ms tick
CONFIG_FREERTOS_UNICORE=n                  # Dual-core mode

# Serial Monitor
CONFIG_ESPTOOLPY_MONITOR_BAUD=115200
```

### Key Project Configurations

#### ADC Configuration

```ini
CONFIG_SOC_ADC_SUPPORTED=y
CONFIG_SOC_ADC_DIG_CTRL_SUPPORTED=y        # Digital controller
CONFIG_SOC_ADC_DMA_SUPPORTED=y             # DMA for continuous reading
CONFIG_SOC_ADC_DIGI_MAX_BITWIDTH=12        # 12-bit resolution
```

#### WiFi Configuration

```ini
CONFIG_SOC_WIFI_SUPPORTED=y
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=10
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32
CONFIG_ESP_WIFI_AMPDU_RX_ENABLED=y
```

#### Flash & Partition

```ini
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"
```

---

## 2.4 Partition Table

File: [partitions.csv](../partitions.csv)

```csv
# ESP32 Partition Table for AC Power Router Controller with OTA
# Flash Size: 4MB
#
# Name,     Type,    SubType,  Offset,   Size,     Flags
nvs,        data,    nvs,      0x9000,   0x6000,
otadata,    data,    ota,      0xf000,   0x2000,
app0,       app,     ota_0,    0x20000,  0x190000,
app1,       app,     ota_1,    0x1B0000, 0x190000,
spiffs,     data,    spiffs,   0x340000, 0xC0000,
```

### Partition Descriptions

| Partition | Type | Size | Offset | Purpose |
|-----------|------|------|--------|---------|
| **nvs** | data (nvs) | 24 KB (0x6000) | 0x9000 | Device configuration (WiFi, RouterController, HardwareConfig) |
| **otadata** | data (ota) | 8 KB (0x2000) | 0xf000 | OTA metadata (which app0/app1 is active) |
| **app0** | app (ota_0) | 1.6 MB (0x190000) | 0x20000 | First firmware slot (OTA slot 0) |
| **app1** | app (ota_1) | 1.6 MB (0x190000) | 0x1B0000 | Second firmware slot (OTA slot 1) |
| **spiffs** | data (spiffs) | 768 KB (0xC0000) | 0x340000 | File system for Web UI (HTML/CSS/JS) |

### Flash Memory Map (4 MB)

```
0x000000 ┌─────────────────────────┐
         │  Bootloader             │ (Automatic)
0x008000 ├─────────────────────────┤
         │  Partition Table        │ (Automatic)
0x009000 ├─────────────────────────┤
         │  NVS (24 KB)            │ ← Configuration
0x00F000 ├─────────────────────────┤
         │  OTA Data (8 KB)        │ ← OTA Metadata
0x020000 ├─────────────────────────┤
         │                         │
         │  APP 0 (1.6 MB)         │ ← Firmware slot 0
         │                         │
0x1B0000 ├─────────────────────────┤
         │                         │
         │  APP 1 (1.6 MB)         │ ← Firmware slot 1
         │                         │
0x340000 ├─────────────────────────┤
         │                         │
         │  SPIFFS (768 KB)        │ ← Web UI files
         │                         │
0x400000 └─────────────────────────┘ (4 MB)
```

---

## 2.5 Compilation and Flashing Commands

### Environment Setup (Windows)

```powershell
# Install ESP-IDF 5.5.1
# Download from https://dl.espressif.com/dl/esp-idf/

# Activate environment
cd %USERPROFILE%\esp\esp-idf
.\export.ps1

# Navigate to project directory
cd C:\Users\<username>\Documents\ESP-PROJECTS\ACRouter\ACRouter-project
```

### Environment Setup (Linux/macOS)

```bash
# Install ESP-IDF 5.5.1
mkdir -p ~/esp
cd ~/esp
git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32

# Activate environment
. $HOME/esp/esp-idf/export.sh

# Navigate to project directory
cd ~/ACRouter-project
```

---

### Main Build Commands

#### 1. Install Dependencies (first time)

```bash
idf.py set-target esp32
idf.py reconfigure
```

This will automatically install:

- Arduino Core 3.x (espressif/arduino-esp32)
- ArduinoJson v7 (bblanchon/arduinojson)

#### 2. Project Configuration (optional)

```bash
idf.py menuconfig
```

Settings available via menuconfig:

- Serial flasher config → Flash size (4 MB)
- Serial flasher config → Flash frequency (40 MHz)
- Partition Table → Custom partition table CSV
- Component config → FreeRTOS → Tick rate (1000 Hz)

#### 3. Build Project

```bash
# Full build
idf.py build

# Quick rebuild (only changed files)
idf.py app

# Clean and full rebuild
idf.py fullclean
idf.py build
```

#### 4. Flash and Monitor

```bash
# Flash + Serial monitor
idf.py flash monitor

# Flash only
idf.py flash

# Serial monitor only
idf.py monitor

# Flash via specific port (Windows)
idf.py -p COM3 flash monitor

# Flash via specific port (Linux)
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## 2.6 Flash Command and Memory Dump Addresses

### esptool.py Command (direct flashing)

If you need to flash manually via `esptool.py` (for example, from another computer):

```bash
esptool.py --chip esp32 \
  --port COM3 \
  --baud 921600 \
  --before default_reset \
  --after hard_reset \
  write_flash -z \
  --flash_mode dio \
  --flash_freq 40m \
  --flash_size 4MB \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x20000 build/ACRouter-project.bin
```

### File Addresses in Memory Dump

| File | Address | Size | Description |
|------|---------|------|-------------|
| **bootloader.bin** | 0x1000 | ~28 KB | ESP32 bootloader (second stage) |
| **partition-table.bin** | 0x8000 | ~3 KB | Partition table |
| **ACRouter-project.bin** | 0x20000 | ~1.6 MB | Main firmware (app0) |
| **ota_data_initial.bin** | 0xf000 | 8 KB | Initial OTA data (optional) |

### Flash Backup

```bash
# Read entire Flash (4 MB)
esptool.py --chip esp32 --port COM3 \
  read_flash 0x0 0x400000 backup_flash_4MB.bin

# Read firmware only (app0)
esptool.py --chip esp32 --port COM3 \
  read_flash 0x20000 0x190000 backup_app0.bin

# Read NVS only (configuration)
esptool.py --chip esp32 --port COM3 \
  read_flash 0x9000 0x6000 backup_nvs.bin
```

### Restore from Backup

```bash
# Restore full Flash
esptool.py --chip esp32 --port COM3 --baud 921600 \
  write_flash 0x0 backup_flash_4MB.bin

# Restore NVS only (preserve configuration)
esptool.py --chip esp32 --port COM3 \
  write_flash 0x9000 backup_nvs.bin
```

---

## 2.7 Firmware Size and Memory Usage

### Typical Sizes After Build

```
Build Output:
=====================================
Bootloader:              28,512 bytes (0x6F80)
Partition Table:          3,072 bytes (0xC00)
ACRouter-project.bin:   ~1,200,000 bytes (~1.15 MB)
=====================================
```

### Flash Usage Map (after flashing)

```
Flash Usage (4 MB total):
├─ Bootloader + Partition     ~32 KB      (0.8%)
├─ NVS (configuration)         24 KB      (0.6%)
├─ OTA Data                     8 KB      (0.2%)
├─ APP 0 (firmware)          ~1.2 MB     (30%)
├─ APP 1 (OTA reserve)       ~1.2 MB     (30%)  ← Will be used during OTA update
├─ SPIFFS (Web UI)            768 KB     (19%)  ← Reserved for future
└─ Free                       ~748 KB    (18%)
```

### RAM Usage (runtime)

```
DRAM (Data RAM):
├─ Static .data + .bss       ~120 KB
├─ Heap (available)          ~180 KB
└─ FreeRTOS stacks           ~80 KB
=====================================
Total DRAM:                  ~380 KB / 520 KB

IRAM (Instruction RAM):
├─ Code (.text)              ~100 KB
├─ Cache                     ~32 KB
=====================================
Total IRAM:                  ~132 KB / 192 KB
```

---

## 2.8 Verifying Successful Build

After running `idf.py build` you should see:

```
Project build complete. To flash, run:
 idf.py flash
or
 idf.py -p (PORT) flash
or
 esptool.py --chip esp32 --port (PORT) --baud 921600 write_flash \
   --flash_mode dio --flash_size 4MB --flash_freq 40m \
   0x1000 build/bootloader/bootloader.bin \
   0x8000 build/partition_table/partition-table.bin \
   0x20000 build/ACRouter-project.bin
```

### Build Output Files (build/)

Main files in the `build/` directory:

- `ACRouter-project.bin` - main firmware
- `ACRouter-project.elf` - ELF file with symbols (for debugging)
- `ACRouter-project.map` - memory map (function addresses)
- `bootloader/bootloader.bin` - bootloader
- `partition_table/partition-table.bin` - partition table

---

## 2.9 Common Build Issues

### Issue 1: Error "arduino-esp32 not found"

**Solution:**

```bash
idf.py reconfigure
# Or manually:
cd managed_components
rm -rf espressif__arduino-esp32
idf.py reconfigure
```

### Issue 2: Not Enough Flash Space

**Symptom:**

```
Error: app partition is too small for binary
```

**Solution:** Check `partitions.csv` - app0/app1 should be >= 0x190000 (1.6 MB)

### Issue 3: ESP-IDF Version Conflict

**Symptom:**

```
CMake Error: ESP-IDF version mismatch
```

**Solution:**

```bash
# Check IDF version
idf.py --version

# Should be: ESP-IDF v5.5.1
# If different version - reinstall IDF 5.5.1
```

### Issue 4: Error "NVS namespace too long"

**Symptom:**

```
Error: NVS key exceeds 15 characters
```

**Solution:** NVS keys are limited to 15 characters. Use abbreviations (see `HardwareConfigManager.h`).

---
