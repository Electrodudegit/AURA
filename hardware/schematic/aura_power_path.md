# AURA — Battery power path (LOCKED)

Diagram: `aura_power_path.svg` (vector) · `aura_power_path.png` (2600×1960 raster) · generator: `gen.py`

## Wiring (exact)
| From | To | Notes |
|---|---|---|
| 18650 + | TP4056 **B+** | observe polarity |
| 18650 − | TP4056 **B−** | |
| USB 5V (VBUS / GND) | TP4056 **IN+ / IN−** | charge input **only** |
| TP4056 **OUT+** | SW1 slide switch → ESP32 DevKit **VIN** | switch cuts load only |
| TP4056 **OUT−** | DevKit **GND** | common ground |
| B+ (tap) | R1 100k → GPIO34 node | sense **B+**, not OUT+/VIN |
| GPIO34 node | R2 100k → GND; C1 100 nF → GND | 2:1 divider + ADC reservoir cap |

**Do not switch B+** (or GND). SW1 sits only between OUT+ and VIN.

## GPIO34 battery sense
- GPIO34 = ADC1_CH6, input-only (safe with Wi-Fi active).
- 4.2 V cell → 2.1 V at pin; 3.0 V → 1.5 V (well under 3.3 V). Use 11 dB attenuation.
- 100 nF gives the ADC a low-impedance sample source behind the 50 kΩ divider.
- Divider drains ≈ 21 µA continuously from the cell, even with SW1 OFF.
- Charging with SW1 ON: ADC reads charge voltage (~4.2 V), not true state of charge.
- Firmware already matches: `PIN_BATT_ADC 34`, `BATT_DIVIDER 2.0f` (config.h).

## Callouts (shown on diagram)
1. **OUT ≈ 3.0–4.2 V (follows the cell), not 5 V** — VIN dropout risk: the DevKit's AMS1117-3.3 needs ~1 V headroom, so the 3V3 rail sags and Wi-Fi current peaks can brown out as the cell discharges.
2. **No load-sharing** — the DevKit draws from OUT/B+ while charging; the TP4056 sees that as charge current and may never terminate. Prefer charging with SW1 OFF or a light load.
3. **Avoid dual USB** — don't plug the DevKit's own USB with SW1 ON: DevKit 5V can back-feed VIN → OUT+ (and into the cell on some modules). Flash with SW1 OFF.
4. **Some modules tie OUT+ to B+** — bare/unprotected TP4056 boards have OUT = B (no over-discharge cutoff). On protected (DW01) boards OUT− ≠ B−. Check the silkscreen and verify with a meter.
