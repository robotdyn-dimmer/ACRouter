# Flashing ACRouter Firmware

ACRouter runs on **ESP32** and **ESP32-C2 (ESP8684)** with **ESP-IDF 5.5**. You can either build from source and flash in one step, or flash pre-built release binaries with `esptool`.

## Option A — Build & flash from source (recommended)

Requires [ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/) installed and exported.

```bash
# Clone with the rbdimmer submodule
git clone --recursive https://github.com/robotdyn-dimmer/ACRouter.git
cd ACRouter

# ESP32
idf.py set-target esp32
idf.py build
idf.py -p <PORT> flash monitor

# ESP32-C2
idf.py set-target esp32c2
idf.py build
idf.py -p <PORT> flash monitor
```

Build profiles (HTTP vs MQTT-only for the C2) are selected through the `sdkconfig.defaults*` layers — see the project documentation.

## Option B — Flash pre-built release binaries with esptool

Each release ships a 4-file binary set (bootloader, partition table, OTA-data, application). Flash them at the per-target offsets below.

**ESP32**
```bash
esptool --chip esp32 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
  0x1000  bootloader.bin \
  0x8000  partition-table.bin \
  0xf000  ota_data_initial.bin \
  0x20000 ACRouter.bin
```

**ESP32-C2**
```bash
esptool --chip esp32c2 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 60m \
  0x0     bootloader.bin \
  0x8000  partition-table.bin \
  0xf000  ota_data_initial.bin \
  0x20000 ACRouter.bin
```

> **Note the bootloader offset differs per target:** `0x1000` on ESP32, `0x0` on ESP32-C2.

A **4 MB** flash is required (dual-OTA partition layout). Both profiles fit 4 MB.

## First boot

On first boot the device starts a Wi-Fi access point `ACRouter_XXXX` (last 2 bytes of the MAC). Connect to it and configure Wi-Fi + the broker/web-app via the on-device Wi-Fi page or the serial console. See the project documentation for commissioning (module discovery, roles, CT model) and the REST/MQTT API (`WEB_API.md`).

## Over-the-air updates

Builds with OTA enabled (ESP32 / C2-HTTP) accept a firmware image at `POST /ota/upload` (raw `application/octet-stream`) and via the `/api/ota/*` endpoints. The C2-MQTT profile updates over UART with the commands above.
