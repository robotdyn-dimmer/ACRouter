[← Overview](https://www.rbdimmer.com/acrouter-overview) | [Contents](https://www.rbdimmer.com/acrouter-what-is) | [Next: Compilation →](https://www.rbdimmer.com/acrouter-application-compilation)

# Hardware Wiring Guide

> **ACRouter v2.0.** Sensing and dimming moved to smart I2C modules. The on-chip ADC
> voltage/current measurement and direct GPIO/TRIAC dimming of v1.x have been **removed**.
> A v2.0 build is an ESP32-family host plus one or more **rbAmp** (measurement) and
> **DimmerLink** (dimmer) modules sharing a single I2C bus. If you are migrating from a
> v1.x ADC build, see [§1.8 Migrating from v1.x](#18-migrating-from-v1x).

> ## ⚠️ DANGER — Mains Voltage
> **AC mains (110 V / 230 V) can cause serious injury or death.** ACRouter modules sit on the
> mains side. **De-energize the circuit before wiring**, keep the low-voltage I2C wiring
> (SDA/SCL/3V3/GND) physically isolated from the mains section, and have mains work done by
> **qualified personnel**. Full safety requirements: [§1.7 Safety](#17-safety) — read it before you build.

## 1.1 Architecture at a Glance

A functional v2.0 Solar Router consists of:

| Part | Role | Interface |
|------|------|-----------|
| **ESP32 or ESP32-C2 host** | Runs the firmware, control loop, and connectivity (WiFi/MQTT, REST server — the web UI is external) | — |
| **rbAmp** | Measures grid / solar / load current and line voltage | I2C (shared bus) |
| **DimmerLink** | Phase-cut dimmer driving the resistive load | I2C (shared bus) |

- **All modules share one I2C bus (`bus0`).** Each module has its own I2C address.
- **Minimum viable build:** one ESP32-family host + one rbAmp (with a **grid** channel) +
  one DimmerLink. Grid measurement is mandatory — see [§1.4](#14-rbamp-measurement-module).
- Firmware auto-discovers modules by an I2C scan and identifies each family from a device
  registry; you then assign sensing roles and reboot once (see the Commissioning guide).

---

## 1.2 The I2C Bus

All ACRouter modules communicate over a single I2C bus running at **100 kHz** (Standard Mode).
Because the bus carries every module, correct pins, pull-ups, and power are the foundation of
a working build.

### 1.2.1 Bus Pins by Target

The default SDA/SCL pins depend on which chip you flash. Both are firmware defaults from the
Hardware Config Manager and can be reconfigured (see [§1.6](#16-configuring-the-bus-pins)).

| Target | SDA | SCL | Notes |
|--------|-----|-----|-------|
| **ESP32** (WROOM / WROVER) | **GPIO21** | **GPIO22** | Standard ESP32 I2C pins. Configurable — e.g. the reference bench used GPIO25 / GPIO26. |
| **ESP32-C2 / ESP8684** | **GPIO5** | **GPIO6** | The C2 has no GPIO21/22. GPIO12–17 are flash, GPIO8/9 are strapping, GPIO19/20 are UART0 — so 5/6 is the default. |

### 1.2.2 Pull-Up Resistors — Required

> ⚠️ **External pull-up resistors on SDA and SCL are mandatory.**

- Use **4.7 kΩ** pull-ups from each of SDA and SCL to **3V3** (one pair per bus, not per module).
- The ESP32's weak internal pull-ups are **not sufficient** for I2C modules on a mains-side
  bench — always fit external resistors.

### 1.2.3 Power

- Power the rbAmp and DimmerLink modules from **3V3**.
- Share a common ground between the host and all modules.

### 1.2.4 Bus Topology

```
                     3V3
                      │
                 4.7k ┴ 4.7k        (one pull-up pair for the whole bus)
                   │      │
  ESP32 / C2 ──────┼──────┼──────────────┬───────────────┐
   host       SDA  │  SCL │              │               │
                   │      │           ┌──┴───┐        ┌───┴────┐
                   └──────┴───────────┤ rbAmp├────────┤DimmerLink│
                                      │ 0x51 │        │  0x50   │
                                      └──┬───┘        └───┬────┘
                                    CT clamps         phase-cut
                                  (grid/solar/load)   output → load
```

*(Addresses shown are the reference-bench values; see §1.4 / §1.5 for how they are assigned.)*

---

## 1.3 Modules on the Bus

Two module families are recognised by the firmware device registry:

| Family | Function | Example address | Role assignment |
|--------|----------|-----------------|-----------------|
| **rbAmp** | Current & voltage sensing | `0x51` | Per-channel, assigned by the user |
| **DimmerLink** | Phase-cut dimmer | `0x50` | Implied by family (`dimmer`) — not assigned manually |

> The addresses above are **example / reference values** used on the validation bench — **not**
> guaranteed factory defaults (each module's shipping address is set by the module vendor). Every
> module is **re-addressable**, so if two modules of the same family share the bus you assign each a
> unique address (see the re-addressing commands in §1.4 / §1.5). Run `i2c-scan` to see what is
> actually present on your bus.

---

## 1.4 rbAmp (Measurement Module)

The rbAmp module measures AC current with a **clamp-on current transformer (CT)** — the CT clips
around a conductor, it is **not** wired in-line — and (on a voltage-capable module) line voltage, and
reports over I2C.

> 🔴 **One rbAmp per measured feed.** In practice each rbAmp provides **one** measurement — you use a
> **separate module per current** you want to sense: one for **grid** (CT around the mains-supply
> conductor), one for **solar** (CT around the solar/inverter line), and one for **load** (CT around the
> diverted-load line), each at its **own I2C address**. Assign one role per module. A minimum solar
> router needs just the **grid** module.

- **I2C address:** `0x51` on the reference bench (example, re-addressable — not a guaranteed factory
  default). Each module needs a unique address. Re-address with the `rbamp-address` serial command or
  `POST /api/rbamp/modules/address` (verify-then-set; the new address applies after a module reset).
- **Roles:** assign each module one of `grid`, `solar`, `load`, or `voltage`.
  - 🔴 **A `grid` module is mandatory**, and it must be **voltage-capable** — real-time power *sign*
    (import vs. export) needs a voltage reference. Without it the router cannot decide when to divert.
  - Assign roles with `dev-role <addr> <ch> <role>` (serial) or `POST /api/modules/role`.

### 1.4.1 CT Model

The current-transformer model must match your physical CT so the firmware scales readings correctly.
The catalog is the firmware source of truth — fetch it with `GET /api/rbamp/ct-models`.

| CT model id | Sensor | Range |
|-------------|--------|-------|
| `sct013-005` | SCT-013-005 | 5 A |
| `sct013-010` | SCT-013-010 | 10 A *(reference bench)* |
| `sct013-020` | SCT-013-020 | 20 A |
| … | *(see `GET /api/rbamp/ct-models` for the full list)* | |

Set the model with `POST /api/rbamp/modules/ct-model {addr, ct_model:"sct013-010"}` or the serial
command `rbamp-ct-model`.

> ⚠️ The selector key is the **id** (e.g. `sct013-010`), not the display name.

### 1.4.2 Advanced (optional)

Not needed for a standard single-bus build:

- **DRDY (data-ready) signal.** The rbAmp exposes an optional DRDY line for interrupt-driven reads;
  bind it to a GPIO with `hw-rbamp-drdy`. By default the firmware **polls without DRDY** (the bench
  ran with DRDY disabled), so you can leave it unconnected.
- **Bus selection.** `hw-rbamp-bus` chooses which I2C bus (`bus0` / `bus1`) an rbAmp lives on. With a
  single shared bus you never need it; when several rbAmp modules of the same family are present,
  give each a **unique address** (see §1.3).

---

## 1.5 DimmerLink (Dimmer Module)

The DimmerLink module performs phase-cut dimming on its own dedicated controller and takes commands over I2C.

- **I2C address:** **`0x50` is the DimmerLink factory default** (re-addressable with the `dl-address`
  serial command). *(rbAmp, by contrast, ships with no default role/address — you assign it.)*
- **Role:** the only valid role is `dimmer`. Assign it through the registry (`role = dimmer`, §1.5.1) —
  the firmware then auto-binds the output. (You don't pick an output id; the family fixes the role.)
- **Thermal protection is on the module.** The DimmerLink's own firmware handles over-temperature
  protection (derate and shutdown at its thresholds). The ACRouter host **reads and reports** the
  module's temperature/state as telemetry but does **not** perform any overheat shutdown itself — the
  safety loop lives on the DimmerLink.

### 1.5.1 How a DimmerLink becomes a dimmer output (recommended path)

In practice a DimmerLink usually **binds itself automatically at discovery** — no manual role step is
needed. If you do need to set it, use the API or serial:

1. Discover the module (`i2c-scan` / rescan) — the device registry identifies it as DimmerLink and
   auto-seeds the `dimmer` role, binding the output.
2. To set it manually, use `POST /api/modules/role {"addr":…,"role":"dimmer"}` or serial
   `dev-role <addr> 0 dimmer`. *(The web app has no UI to assign the `dimmer` role — that's an
   API/serial action; role assignment in the app is on the Sensors tab, which covers sensor roles only.)*
3. The firmware binds it to the **first free I2C dimmer output** — **id 4** for the first DimmerLink,
   **id 5** for the second, and so on (`bridge_role → dimmer_bind_i2c`).
4. Drive that output with `dimmer <id> <0-100>` (e.g. `dimmer 4 60`), or via the router mode.

> **Why id 4?** Dimmer output ids **0–3 are reserved empty** — they were the legacy on-chip GPIO
> dimmer channels, removed in v2.0. I2C dimmer outputs therefore start at **id 4**
> (`DIMMER_I2C_START = 4`); ESP-NOW dimmer nodes (ESP32-tier) use ids **12+**.

### 1.5.2 Advanced: `dl-config` and slots

`dl-config <slot> <addr> <role>` is a low-level developer command that registers a DimmerLink in the
DL-manager by **slot** (0–7, `DL_MAX_DEVICES = 8`). **Most users never need it** — use the role
assignment above instead.

> 🔴 **Slot ≠ dimmer id.** The **slot** is the module's registration index inside the DL-manager;
> the **dimmer id** (e.g. id 4) is the actuation index inside the dimmer manager. They are two
> separate numbering schemes — don't conflate them.

---

## 1.6 Configuring the Bus Pins

If your wiring differs from the target default (§1.2.1), reconfigure the bus.

**Persistent (survives reboot):**
```
POST /api/hardware/config
{"i2c":{"bus0":{"sda":25,"scl":26,"enabled":true}}}
```
The configuration is stored in NVS. **A reboot is required** — bus pins are read from the hardware
config only during boot-time initialization.

**Runtime (not persisted):**
```
i2c-reinit <bus> <sda> <scl> [freq]
```
Re-initializes the bus immediately for testing, but the change is lost on reboot.

> 🔁 **Order of operations:** wire the modules → set bus pins (if non-default) → reboot → discover &
> assign roles. Assigning `role=dimmer` drives the output **live** (no reboot to actuate); a one-time
> reboot may be needed for the dimmer's status/telemetry to populate. The full first-time flow is in the
> Commissioning guide.

---

## 1.7 Safety

> ### ⚠️ DANGER: Mains Voltage
> **AC mains voltage (110 V / 230 V) can cause serious injury or death.**

The rbAmp and DimmerLink modules operate on the **mains side** of your installation. Treat the whole
build as a live-mains project.

**Before working with this project:**

1. **Qualifications** — mains electrical work should be performed by qualified personnel.
2. **De-energize first** — always disconnect power before making or changing connections.
3. **Verify** — confirm the circuit is dead with a multimeter before touching conductors.
4. **Insulation** — use properly rated wire, connectors, and CT clamps.
5. **Protection** — install appropriate fusing plus RCD/GFCI protection for your loads.
6. **Enclosure** — house all mains connections in a suitable enclosure.

**Isolate low-voltage from mains.** Keep the I2C wiring (SDA / SCL / 3V3 / GND between the host and
the modules) physically separated from the mains-carrying section. Do not run signal wiring loose
alongside live conductors.

### Module isolation & USB safety

- **rbAmp** modules are **fully galvanically isolated** (isolation withstand up to **3000 V**) between the
  mains measurement side and the 3V3/I2C logic side — so a **USB connection to the host while the modules
  are mains-powered is safe**. See **[rbamp.com](https://rbamp.com)** for isolation details.
- 🔴 **DimmerLink** — treat as **non-isolated** unless its own datasheet states otherwise. **Do not
  connect USB/UART to the host while a DimmerLink is under mains.** The DimmerLink's power stage can be
  live-referenced; power the whole build down before plugging in a PC.
- ⚠️ The general trap is *"isolated sensor, non-isolated supply"*: never power a module from a
  **non-isolated** mains PSU while a PC is attached to the host. With rbAmp's isolated front-end this is
  covered; with DimmerLink, keep the conservative rule above.

**Grounding.** Ensure proper protective-earth grounding of enclosures and metal frames — it is
essential for safety, independent of the shared signal ground the I2C bus needs (§1.2.3).

> A more detailed electrical-safety reference may be published as a separate page; this callout is
> the minimum you must observe when wiring an ACRouter.

---

## 1.8 Migrating from v1.x

If you built a v1.x ACRouter, the following on-board hardware is **no longer used** and its
firmware support has been removed:

| v1.x hardware | Status in v2.0 | Replacement |
|---------------|----------------|-------------|
| On-chip ADC current/voltage sensing | **Removed** | rbAmp over I2C |
| ZMPT voltage sensing | **Pipeline removed** (driver/config remnants remain but inert) | rbAmp `voltage` channel |
| Zero-cross detector | **Removed** | Handled inside DimmerLink |
| GPIO/TRIAC direct dimming | **Removed** | DimmerLink over I2C |
| `hardware-voltage-*` / `hardware-current-*` serial commands | **Deprecated** | rbAmp commands (`rbamp-*`, `dev-role`) |
| `hw-dimmer-gpio` serial command | **Removed** | `dl-config` / `dimmer` |

A v2.0 build reuses your ESP32 host and mains wiring, but the sensing and dimming front-ends
are now the external rbAmp and DimmerLink modules on the I2C bus.

---

[← Overview](https://www.rbdimmer.com/acrouter-overview) | [Contents](https://www.rbdimmer.com/acrouter-what-is) | [Next: Compilation →](https://www.rbdimmer.com/acrouter-application-compilation)
