[← Web API - POST](https://www.rbdimmer.com/acrouter-web-api-post) | [Contents](https://www.rbdimmer.com/acrouter-what-is) | [Next: MQTT Guide →](https://www.rbdimmer.com/acrouter-mqtt-guide)

# Sensor Calibration

> **v2.0.** Measurement is done by **rbAmp** smart modules over I2C. Each rbAmp arrives
> **factory-calibrated per unit** — ACRouter does not expose gain/offset/zero tuning. The one setting
> you make is the **CT model**. (The v1.x on-chip ADC calibration — ZMPT offset, gain trimming — is gone.)

## 10.1 The Only Calibration: CT Model

An rbAmp measures current with a clamp-on CT. Selecting the **CT model that matches your physical clamp**
loads the correct factory scaling preset — this is the whole of user-side calibration. Do it once per
channel during [Commissioning](https://www.rbdimmer.com/acrouter-commissioning) (which has the full
role + CT-model flow); this page is the reference.

Set it with the serial command or the REST API (or the web app's **Sensors** tab):

```text
rbamp-ct-model <addr_hex> <code>
```
```bash
curl -X POST http://192.168.4.1/api/rbamp/modules/ct-model \
  -d '{"addr":"0x51","ct_model":"sct013-030"}'
```

### Supported CT models

| Serial code | REST id | Sensor | Rated |
|:-----------:|---------|--------|-------|
| 1 | `sct013-005` | SCT-013-005 | 5 A |
| 2 | `sct013-010` | SCT-013-010 | 10 A |
| 6 | `sct013-020` | SCT-013-020 | 20 A |
| 3 | `sct013-030` | SCT-013-030 | 30 A |
| 4 | `sct013-050` | SCT-013-050 | 50 A |

> The serial codes are **historical catalog IDs, not ordered by rating** — note **code 6 = 20 A** (it
> falls between 10 A and 30 A). Use the exact code for your clamp; don't infer it from the number.

> 60 A / 100 A codes exist in the catalog but are **not yet preset-backed** — don't rely on them.
> The live catalog is `GET /api/rbamp/ct-models` (entries with `available:false` are the unsupported ones).

## 10.2 Set It Once — the Overwrite Caveat

> 🔴 **Choose the CT model once, for your actual clamp — don't toggle it.**

Applying a CT model loads that model's **factory gain preset**, which **overwrites** the module's
per-unit factory gain calibration. To protect against needless overwrites, ACRouter uses
**verify-then-set**: it reads the module's current model first and **writes only if it differs** — so
re-applying the *same* model is a safe no-op (the command reports it as queued, verify-then-set). Only a
genuine change rewrites the gain, so pick the right model at commissioning and leave it.

## 10.3 Verifying Accuracy

There is no separate reference-measurement command. To check a module reads correctly:

1. **`GET /api/rbamp/modules`** (or the web app's **Sensors** tab) — shows live per-module
   voltage / current / power / PF / frequency, and the applied `ct_model` (so you can confirm the model
   took). Compare against a known load or a handheld clamp meter. *(The serial `rbamp-status` lists only
   the discovered modules and their roles — no live readings; the nearest serial view is `sensor-hub`,
   which shows the merged per-role values.)*
2. **Signed grid under a known load** — confirm `power_grid` reads **+** on import and **−** on export
   (see [Commissioning → Verify Sensing](https://www.rbdimmer.com/acrouter-commissioning)). A wrong sign
   means the grid channel isn't voltage-capable, or the **clamp polarity** is reversed (flip the CT).

## 10.4 Related

- **Assigning roles** (grid/solar/load) is commissioning, not calibration — see
  [Commissioning](https://www.rbdimmer.com/acrouter-commissioning) (`dev-role`).
- **Wiring, addresses, CT clamps** — [Hardware Guide](https://www.rbdimmer.com/acrouter-hardware-guide).
- **Commands** — [Terminal Commands → rbAmp](https://www.rbdimmer.com/acrouter-terminal-commands).

---

[← Web API - POST](https://www.rbdimmer.com/acrouter-web-api-post) | [Contents](https://www.rbdimmer.com/acrouter-what-is) | [Next: MQTT Guide →](https://www.rbdimmer.com/acrouter-mqtt-guide)
