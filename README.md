<h1 align="center">AURA — DIY ESP32 Desk Buddy</h1>

<p align="center">
  <b>An all-in-one ESP32 desk companion with a color touchscreen, a rotary knob, and battery power.</b><br>
  Designed and built by <a href="https://www.youtube.com/c/ElectroDude">ELECTRO DUDE</a>.
</p>

<p align="center">
  <img alt="Firmware" src="https://img.shields.io/badge/firmware-v3.1.6-orange">
  <img alt="Platform" src="https://img.shields.io/badge/platform-ESP32--WROOM--32-blue">
  <img alt="Display" src="https://img.shields.io/badge/display-ILI9341%20320%C3%97240-informational">
  <img alt="Arduino" src="https://img.shields.io/badge/Arduino%20IDE-2.x-00979D">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-green">
</p>

---

AURA is a small box that sits on your desk and actually does things. It shows a smart clock, live weather, air quality, crypto prices, news, a focus timer, Oracle AI chat, and an animated Buddy face. Its rotary knob also works as a **Bluetooth macropad**, so it can control your laptop (play/pause, volume, media keys). This repo has everything you need to build your own: the **firmware**, the **schematic and pin map**, and the **3D enclosure (STEP)**.

> 🎥 **Watch the full build:** [ELECTRO DUDE on YouTube](https://www.youtube.com/c/ElectroDude). It covers the breadboard prototype, the 3D enclosure, soldering, and the finished AURA.

---

## Table of Contents
- [Features](#-features)
- [Hardware (BOM)](#-hardware-bom)
- [Pin Map](#-pin-map)
- [Repository Layout](#-repository-layout)
- [Flashing the Firmware](#-flashing-the-firmware)
- [First Boot & Setup](#-first-boot--setup)
- [Roadmap](#-roadmap)
- [Sponsor](#-sponsor--rapiddirect)
- [Follow Electro Dude](#-follow-electro-dude)
- [License](#-license)

---

## ✨ Features

**Firmware v3.1.6**

| App | What it does |
|---|---|
| 🕰 **Home / Clock faces** | Split-flap, Buddy face, weather, and analog faces. Turn the knob or swipe to switch |
| 🌦 **Weather · Forecast · Sky · Air quality** | Live conditions, a multi-day forecast, sunrise/sunset/moon, and AQI |
| 💰 **Crypto** | Live price dashboard |
| 📰 **News** | Scrolling headline ticker |
| ⏱ **Focus** | Pomodoro / focus timer, plus world clock and system info |
| 🔮 **Oracle AI** | Ask anything, and type with the on-screen **rotary keyboard** |
| 🙂 **Buddy** | Animated desk-pet face |
| 🎛 **Macro** | **Bluetooth HID macropad** for play/pause, volume, and media keys on your PC |
| 📈 **Stocks · 🏏 Cricket · ⚽ Football** | You can save API keys on the device now. Live feeds arrive in the **P4 update** |
| 💻 **PC companion link** | Planned for a later update (not in v3.1.6) |

**Hardware highlights:** 2.8" color touchscreen · rotary knob with push button · 18650 battery with USB-C charging · custom 3D-printed shell.

---

## 🛠 Hardware (BOM)

| # | Component | Qty | Notes |
|---|---|---|---|
| 1 | ESP32-WROOM-32 DevKit (ESP32 Dev Module) | 1 | 4 MB flash, no PSRAM needed |
| 2 | 2.8" ILI9341 TFT, 320×240, with XPT2046 resistive touch | 1 | SPI module with on-board touch controller |
| 3 | KY-040 rotary encoder module (with push switch) | 1 | Navigation knob |
| 4 | 18650 Li-ion cell | 1 | A protected cell is recommended |
| 5 | TP4056 USB-C charger module (with DW01 protection) | 1 | Charges the cell and protects it |
| 6 | SPDT slide switch | 1 | Main power on/off |
| 7 | 100 kΩ resistor | 2 | Battery-voltage divider into GPIO34 |
| 8 | 100 nF ceramic capacitor | 1 | ADC filter |
| 9 | 3D-printed enclosure | 1 set | Front bezel, back shell, and knob ([STEP](hardware/step/AURA_enclosure.step)) |
| 10 | Hook-up wire, heat-shrink, M2/M2.5 screws | – | General assembly |

**Tools:** soldering iron, multimeter, a 3D printer (or a manufacturing service), and a USB **data** cable.

---

## 🔌 Pin Map

All logic runs at **3.3 V**. The display and touch share **one SPI bus** and use separate chip-select pins.

| Function | ESP32 GPIO | Module pin |
|---|---|---|
| SPI SCK | 18 | ILI9341 SCK + XPT2046 T_CLK |
| SPI MISO | 19 | ILI9341 SDO + XPT2046 T_DO |
| SPI MOSI | 23 | ILI9341 SDI + XPT2046 T_DIN |
| TFT CS | 5 | ILI9341 CS |
| TFT DC | 2 | ILI9341 DC |
| TFT RST | 4 | ILI9341 RESET |
| Backlight (PWM) | 15 | ILI9341 LED |
| Touch CS | 21 | XPT2046 T_CS |
| Touch IRQ | 22 | XPT2046 T_IRQ (optional, the firmware falls back to polling) |
| Encoder CLK | 32 | KY-040 CLK |
| Encoder DT | 33 | KY-040 DT |
| Encoder SW | 25 | KY-040 SW |
| Battery sense | 34 (ADC) | B+ through a 100k/100k divider + 100 nF |
| 3V3 | 3V3 | TFT VCC, KY-040 "+" |
| VIN | VIN | TP4056 OUT+ through the slide switch |
| GND | GND | Common ground |

**Power path:** 18650 → TP4056 B+/B− · USB-C → TP4056 IN · TP4056 OUT+ → slide switch → DevKit **VIN** · OUT− → GND.

> ⚠️ **Wiring notes**
> - Power the KY-040 "+" from **3V3, not 5V**.
> - **VIN is not the 5V pin.** TP4056 OUT goes to **VIN**.
> - **Don't plug in the DevKit USB and the TP4056 USB at the same time.** Power can back-feed between them.
> - GPIO2, GPIO5, and GPIO15 are strapping pins. Wire the TFT exactly as shown so the board boots reliably.
> - Keep the SPI wires short, because the display runs at 40 MHz.

The full schematic and power-path drawings are in [`hardware/schematic/`](hardware/schematic/).

---

## 📁 Repository Layout

```
AURA/
├── README.md                    ← you are here
├── LICENSE                      ← MIT
├── firmware/
│   └── AURA_v3/                 ← Arduino sketch (open AURA_v3.ino)
│       ├── AURA_v3.ino
│       ├── *.cpp / *.h          ← UI, apps, net, BLE HID, drivers
│       ├── src/assets/          ← fonts, icons, theme
│       ├── secrets.h            ← placeholders only: "put your credential here"
│       └── extras/              ← optional host tools (not needed to flash)
├── hardware/
│   ├── schematic/               ← connection schematic, power path, pin net table (PNG + SVG)
│   └── step/
│       └── AURA_enclosure.step  ← 3D enclosure for printing or CNC
├── releases/
│   └── AURA_v3_subscriber.zip   ← same sketch as a one-click download
└── docs/
    └── PROJECT.md               ← architecture + project overview
```

---

## 🔧 Flashing the Firmware

**Toolchain**
- **Arduino IDE 2.x**
- **esp32 by Espressif Systems 3.3.x** (Boards Manager)
- **NimBLE-Arduino by h2zero 2.5.x** (Library Manager), used for the Bluetooth macropad

**Steps**
1. Clone this repo (or download [`releases/AURA_v3_subscriber.zip`](releases/AURA_v3_subscriber.zip) and extract it), then open **`firmware/AURA_v3/AURA_v3.ino`** (or `AURA_v3/AURA_v3.ino` from the zip) in Arduino IDE 2.x. Keep all files in the same folder.
2. Install **esp32 by Espressif Systems (3.3.x)** from *Boards Manager*.
3. Install **NimBLE-Arduino by h2zero (2.5.x)** from *Library Manager*.
4. Go to *Tools → Board* and pick **ESP32 Dev Module**.
5. Go to *Tools → Partition Scheme* and pick **Minimal SPIFFS (1.9MB APP with OTA)**.
6. Connect the ESP32 with a USB data cable, select the port, and click **Upload**. If it stalls at "Connecting…", hold the **BOOT** button.
7. *(Optional)* Open the Serial Monitor at **115200 baud** to see the boot log.

> 🔐 **No secrets in this repo.** Every credential field in the firmware is a placeholder that says `put your credential here`. You enter your **Wi-Fi and API keys on the device** after flashing, and AURA stores them in the ESP32's NVS flash. Nothing private gets compiled into your build or committed to git.

> 💡 Updating from an older v3.1.x build? A normal app upload keeps your saved Wi-Fi, keys, touch calibration, and preferences. A full flash erase wipes them.

---

## 🚀 First Boot & Setup

1. AURA plays its boot animation and goes to **Home**.
2. Open **Settings → Wi-Fi setup**. AURA starts a temporary hotspot named **`AURA-Setup-XXXX`** and shows a QR code with a one-time password.
3. Scan the QR with your phone. The setup page opens by itself, or you can go to **http://192.168.4.1**.
4. Enter your **Wi-Fi**, **city / latitude / longitude**, and any optional **API keys** (AI, weather, stocks, cricket, football).
5. Tap **Save**. AURA connects and starts pulling live data.

Saved keys are never shown or printed back. The page only shows `set (len N)`. **Remove all saved keys** wipes them.

Defaults: the clock is set to IST (Asia/Kolkata) and the weather location is New Delhi. You can change the location on the setup page.

---

## 🗺 Roadmap

- [x] v3.1.6: clock faces, weather suite, crypto, news, focus, Oracle AI, Buddy, BLE macropad
- [ ] **P4:** live Stocks / Cricket / Football feeds
- [ ] PC companion link
- [ ] More enclosure variants

Ideas and bug reports are welcome. Please open an **Issue**.

---

## 🏭 Sponsor — RapidDirect

With RapidDirect's integrated manufacturing services, makers and engineers can turn ideas into real products. They offer custom PCBs and electronic components, CNC machining, 3D printing, sheet metal fabrication, injection molding, and more.

**Get up to 20% off your first order →** https://www.rapiddirect.com/?rdtm_from=Electro_Dude

---

## 🔗 Follow Electro Dude

| Platform | Link |
|---|---|
| ▶️ YouTube | https://www.youtube.com/c/ElectroDude |
| 📸 Instagram | https://www.instagram.com/electro__dude/ |
| 👍 Facebook | https://www.facebook.com/Electroodude |
| 🛠 Hackster | https://www.hackster.io/Electrodude/ |

If you build an AURA, tag **@electro__dude**. I'd love to see it! ⭐ Star this repo to follow updates.

---

## 📄 License

Released under the [MIT License](LICENSE).

<p align="center">Built with ❤️ in India by <b>ELECTRO DUDE</b><br><i>Bas bana do, maza aayega!</i></p>
