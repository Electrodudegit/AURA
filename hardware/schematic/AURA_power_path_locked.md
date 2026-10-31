# AURA v3 — Power path (locked)

Source: Power Electronics Engineer → HARD DUDE, 2026-10-30

## Wiring
- 18650 → TP4056 **B+ / B−** (observe polarity)
- USB charger → TP4056 **IN+ / IN−** only; do not also feed DevKit USB unless accepting dual-path risk
- Load: TP4056 **OUT+** → **slide switch** → **DevKit VIN**; **OUT−** → GND
- Do **not** put the switch in the battery B+ path

## Slide switch
- Between TP4056 OUT+ and DevKit VIN (cuts load only)
- Avoid switching B+ or GND

## Batt ADC (GPIO34)
- 2:1 divider from **B+**: Rhi=100k (B+→ADC), Rlo=100k (ADC→GND); ~100 nF ADC→GND
- Sense B+ (not post-switch) unless SoC-only-when-on is preferred
- Charge-while-on: ADC reads charge voltage; keep load light or charge with switch OFF

## Audience callouts
- No load-sharing IC — USB+batt+load simultaneous is unsafe for termination
- OUT is ~3.0–4.2 V, not 5 V — DevKit AMS1117 on VIN browns out as pack sags
- Dual USB (TP4056 + DevKit) can back-feed
- Cheap TP4056 modules may tie OUT to B+ — verify silkscreen
- Divider must keep GPIO34 ≤ 3.3 V at 4.2 V
