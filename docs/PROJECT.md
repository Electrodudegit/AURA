# AURA — Project Overview

**AURA** is a DIY ESP32 desk buddy by ELECTRO DUDE. It is one battery-powered box with a color touchscreen and a rotary knob that combines a smart clock, info dashboards, an AI chat, and a Bluetooth macropad.

- **Firmware:** v3.1.6 (Arduino C++, arduino-esp32 core 3.3.x, NimBLE-Arduino 2.5.x)
- **Board:** ESP32-WROOM-32 (ESP32 Dev Module) · Partition: Minimal SPIFFS (1.9MB APP with OTA)
- **Secrets:** none in the repo. All credential fields say `put your credential here`, and you enter real values on the device (stored in NVS).

## Architecture

```
            ┌──────────────── ESP32-WROOM-32 ────────────────┐
 KY-040 ───►│ Input: encoder (CLK 32 / DT 33 / SW 25)        │
 XPT2046 ──►│        touch (SPI, CS 21, IRQ 22)              │
            │                                                │
            │ UI core: app grid/list menu · 17 apps          │──► ILI9341 320×240 (SPI, CS 5, DC 2, RST 4, BL 15)
            │ Services: Wi-Fi + NTP · HTTP API fetchers      │◄─► Internet (weather, AQI, crypto, news, AI)
            │ BLE HID (NimBLE): macropad / media keys        │──► Laptop / PC
            │ Storage: NVS (Wi-Fi, keys, prefs, touch cal)   │
            │ Setup: AURA-Setup-XXXX SoftAP + web page       │◄── Phone (QR → 192.168.4.1)
            │ Power: battery sense on GPIO34 (2:1 divider)   │
            └────────────────────────────────────────────────┘
 Power: 18650 → TP4056 (USB-C, DW01) → slide switch → VIN
```

**Layers**
1. **Hardware drivers:** shared SPI bus for the TFT and touch, a quadrature encoder with a push switch, and an ADC battery monitor.
2. **UI framework:** app launcher (grid/list), clock faces, the on-screen rotary keyboard, and the Buddy animations.
3. **Apps:** Home/Clock, Weather, Forecast, Sky, Air quality, Crypto, News, Focus/Pomodoro, World clock, System info, Oracle AI, Buddy, Macro, Stocks, Cricket, Football, and Settings.
4. **Connectivity:** Wi-Fi station mode for live data, a SoftAP captive setup page for credentials, and BLE HID for the macropad.
5. **Persistence:** ESP32 NVS. Keys are never echoed back to the screen or serial port.

## Status (v3.1.6)
- ✅ Clock faces, weather suite, crypto, news, focus, Oracle AI, Buddy, BLE macropad
- ⏳ Stocks / Cricket / Football: keys can be saved on the device now, and live feeds arrive in **P4**
- ⏳ PC companion link: planned for a later update

## Folder Map

| Path | Contents |
|---|---|
| `firmware/AURA_v3/` | Arduino sketch. Open `AURA_v3.ino`. `secrets.h` has placeholders only |
| `hardware/schematic/` | Connection schematic, power-path drawing, pin map |
| `hardware/step/` | `AURA_enclosure.step`, the 3D-printable / CNC enclosure |
| `releases/` | `AURA_v3_subscriber.zip`, the sketch as a one-click download |
| `docs/` | This overview and any further build notes |
| `README.md` | BOM, pin map, flashing, setup, links |
| `LICENSE` | MIT |
