# Firmware: AURA v3.1.6

[`AURA_v3/`](AURA_v3/) is the full Arduino sketch folder. Open **`AURA_v3/AURA_v3.ino`** in Arduino IDE 2.x and keep every file in the same folder.
You can also download the same sketch as a zip from [`releases/AURA_v3_subscriber.zip`](../releases/AURA_v3_subscriber.zip).

- **Board:** ESP32 Dev Module · **Partition:** Minimal SPIFFS (1.9MB APP with OTA)
- **Core:** esp32 by Espressif Systems 3.3.x · **Library:** NimBLE-Arduino by h2zero 2.5.x
- `secrets.h` / `secrets.example.h` only hold the placeholder `put your credential here`. After flashing, enter your Wi-Fi and API keys on the device under **Settings → Credentials**. They are saved in NVS.
- `extras/` has optional host tools (a PC companion that's planned for a later update, plus a host test bench). They aren't needed to flash.

See the main [README](../README.md#-flashing-the-firmware) for the full steps.
