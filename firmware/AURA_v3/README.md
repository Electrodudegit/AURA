<p align="center">
  <img src="docs/brand/electro-dude-logo.png" alt="ELECTRO DUDE" width="240">
</p>

# ELECTRO DUDE · AURA v3.1.6

Official AURA firmware from **ELECTRO DUDE**. AURA is a desk companion: an ESP32-WROOM-32, a 320×240 screen, touch, and a knob. The boot screen reads **AURA · BY ELECTRO DUDE**. This release is **v3.1.6**.

Wi-Fi and API keys are typed on the device. This repository only contains the placeholder `put your credential here`. It does not contain a real network name, password, or API key.

## Download ZIP → Extract → Open AURA_v3.ino

**[Download AURA_v3_subscriber.zip](releases/AURA_v3_subscriber.zip)**

1. Download that zip. You do not need git.
2. Extract it. You should see one folder named `AURA_v3`.
3. Install the [Arduino IDE](https://www.arduino.cc/en/software) (2.x).
4. Boards Manager: install **esp32 by Espressif Systems** (3.3.x).
5. Library Manager: install **NimBLE-Arduino** by h2zero (2.5.x).
6. In the `AURA_v3` folder, open `AURA_v3.ino`.
7. Tools → Board → **ESP32 Dev Module**.
8. Tools → Partition Scheme → **Huge APP (3MB No OTA/1MB SPIFFS)**.
9. Tools → Port → the port that appears when the board is plugged in.
10. Click **Upload**. The first upload of v3.1.6 must be a full Arduino upload so the new partition table is written. Do not flash an old app-only `.bin`.
11. When AURA finishes its intro, the setup page is already open. Tap **Wi-Fi name**, type it, tap **Done**, then **Wi-Fi password** if the network has one. Tap **Save**. The other rows are optional.

This project does not publish `.bin` firmware images. A binary can hide a password if someone replaced the placeholders in `secrets.h` before compiling. If a `.bin` is ever shared, change the Wi-Fi password and regenerate the API keys.

### Download from the Origin page

You can also download a zip from this repository’s Origin page. That snapshot may extract to a folder that is not named `AURA_v3`. Rename the folder to `AURA_v3` before opening the sketch, or use the release zip above. The Origin namespace cannot be renamed from this repo. The name to use everywhere else is **ELECTRO DUDE**.

## Social

- YouTube: https://www.youtube.com/c/ElectroDude
- Instagram: https://www.instagram.com/electro__dude/
- Hackster: https://www.hackster.io/Electrodude/
- Facebook: https://www.facebook.com/Electroodude

A short profile you can paste is in [docs/PROFILE.md](docs/PROFILE.md).

## What you type on AURA

The first time AURA starts with no Wi-Fi name saved, **Set up** opens before Home. Later it is **Settings → Credentials**.

| On the screen | What to type |
|---|---|
| Wi-Fi name | The name of your network. Required. |
| Wi-Fi password | Hidden as dots. Leave it empty if the network has no password. Otherwise use 8 or more characters. |
| Weather API key | From openweathermap.org. Optional. |
| Cricket | Optional. |
| Stocks | Optional. |
| Groq / Oracle | Lets AURA answer questions. Optional. |
| xAI | Optional. |
| Gemini | Optional. |

**Save** stores them on the device only. **Later** skips to Home until the next restart. **X** clears the row you are editing. A saved password or key is not shown again. **Use your phone** opens the same setup in a browser if a long key is hard to type on the screen. Anything saved on the device overrides the placeholder text in `secrets.h`.

## Do not put passwords in this repo

`secrets.h`, `secrets.example.h`, and `secrets.h.example` all use `put your credential here`. Replace that text only on your own computer if you build a private copy. Do not commit real Wi-Fi names, passwords, or API keys.

## Hardware

| Folder | What is there |
|---|---|
| `hardware/schematic/` | Wiring, power path, and the pin table |
| `hardware/step/` | Enclosure STEP, when ELECTRO DUDE adds it |
| `hardware/3d/` | STL, glTF, or pictures, when ELECTRO DUDE adds them |
