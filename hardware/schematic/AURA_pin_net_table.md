# AURA — I/O Pin / Net Table (Rev A.1, 2026-10-30 (VIN≠5V wording fix))

> **AURA — prototype / module wiring for presentation (not production PCB)**
> AURA BY ELECTRO DUDE · Dude Tronix · Board: **ESP32-WROOM-32 DevKit V1 (30-pin)**
> Pins verified against `/workspace/fw-review/wx-iot-slim/AURA_v3/config.h` (2026-10-30).
> Drawing: `AURA_connection_schematic.svg` / `.png`. Power path: see locked sheet `aura_power_path.svg` (not redrawn here).

## Short pin table (HARD DUDE handoff)

| FW macro | GPIO (DevKit pin) | Module pin |
|---|---|---|
| PIN_SPI_SCK | 18 (D18) | ILI9341 SCK + XPT2046 T_CLK |
| PIN_SPI_MISO | 19 (D19) | ILI9341 SDO(MISO) + T_DO |
| PIN_SPI_MOSI | 23 (D23) | ILI9341 SDI(MOSI) + T_DIN |
| PIN_TFT_CS | 5 (D5) | ILI9341 CS |
| PIN_TFT_DC | 2 (D2) | ILI9341 DC |
| PIN_TFT_RST | 4 (D4) | ILI9341 RESET |
| PIN_TFT_BL | 15 (D15) | ILI9341 LED (PWM backlight) |
| PIN_TOUCH_CS | 21 (D21) | XPT2046 T_CS |
| PIN_TOUCH_IRQ | 22 (D22) | XPT2046 T_IRQ (active-low; module routing UNVERIFIED) |
| PIN_ENC_CLK | 32 (D32) | KY-040 CLK |
| PIN_ENC_DT | 33 (D33) | KY-040 DT |
| PIN_ENC_SW | 25 (D25) | KY-040 SW (active-low) |
| PIN_BATT_ADC | 34 (D34, ADC1_CH6, input-only) | B+ via 100k/100k divider + 100 nF |
| 3V3 | DevKit 3V3 | TFT VCC, KY-040 "+" |
| VSYS | DevKit VIN | TP4056 OUT+ via slide switch (see power sheet) |
| GND | DevKit GND (common) | TFT GND, KY-040 GND, TP4056 OUT− |

## Power (reference only — locked on `aura_power_path`)
18650 → TP4056 B+/B− · USB charger → TP4056 IN only · OUT+ → slide switch → DevKit **VIN** · OUT− → GND · do not switch B+ or GND · GPIO34 senses B+ (pre-switch) via 2:1 divider.

## Light protection notes
- **KY-040:** module usually has 10k pull-ups on CLK/DT; SW often has none → `INPUT_PULLUP` or 10k to 3V3. Bare encoder: 10k to 3V3 on CLK/DT/SW. Power "+" from **3V3, not 5V**.
- **Common GND** across DevKit, TFT/touch, encoder, TP4056 OUT−.
- **Logic modules on 3V3**; DevKit load from TP4056 goes to **VIN** (external into AMS1117). DevKit **5V** pin is the USB 5V rail — different net. Do not equate VIN with 5V. Avoid plugging DevKit USB and TP4056 USB at once (back-feed risk).
- **TP4056 OUT/switch:** OUT ≈ 3.0–4.2 V (pack voltage, not 5 V) → AMS1117 3V3 browns out as pack sags; no load-sharing IC; verify whether your module ties OUT+ to B+.
- **Battery divider:** `BATT_DIVIDER 2.0f` is an **ASSUMPTION** (100k/100k) — measure VBAT vs ADC and fix.
- **Touch IRQ:** T_IRQ → GPIO22 unverified on module; FW falls back to ~20 Hz polling.
- **SPI:** shared VSPI bus (TFT 40 MHz, touch 2 MHz), separate CS 5 / 21; keep wires short.
- **Strapping:** GPIO2 / GPIO15 (TFT_DC / TFT_BL) and GPIO5 (TFT_CS) are ESP32 boot-strap pins — keep TFT wiring as in FW; avoid forcing wrong levels at reset.
