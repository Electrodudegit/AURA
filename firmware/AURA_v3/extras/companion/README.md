# AURA laptop companion

A small Python program that runs on your laptop and sends three things to **one** paired AURA on the same network:

- CPU percent
- RAM percent
- a stub title (a fixed local list, or whatever you pass with `--note`)

It does not read the operating-system notification center, does not open any socket except the one to that AURA, and does not upload anywhere.

The device side is documented in the firmware README (Settings > Companion). Turn Companion on, join Wi-Fi, and read the pairing code and the address on the screen. Serial snapshots are blocked while that page is showing the code.

## Run

Linux or macOS, Python 3, no extra packages:

```bash
python3 aura_companion.py --host 192.168.1.42 --token K7M2Q9XW
```

The address and the 8-character code are on the AURA companion page. The first successful run writes `~/.config/aura-companion/pair.json` (mode 0600) with that host, the token, and a random client id. Later runs reuse the file and refuse a different host until you rebind:

```bash
python3 aura_companion.py --rebind --host 192.168.1.42 --token NEWCODE1
```

`--note "Build finished"` replaces the stub title. Titles are trimmed to 47 printable ASCII characters.

Leave it running. It retries if the AURA sleeps the listener or reboots. Ctrl-C stops it. Nothing else is contacted.

## PC Status app (firmware)

AURA shows CPU, RAM, and the notification title from this link in the **PC Status** app. Settings > Companion is still where you turn the listener on and read the pairing code. The PC Status screen shows the code only while no laptop is paired. A serial snapshot is blocked while that code is on screen.

```
HELLO <8-char-token> <16-hex-client-id>   -> OK | DENY
STAT <cpu 0-100> <ram 0-100> [title]      -> ACK | DENY
NOTIF <title>                             -> ACK | DENY
```

`NOTIF` updates the title and keeps the last CPU and RAM. Pairing is one laptop per AURA. The first HELLO binds the client id. Any other id is denied until Forget on the device, which rotates the token.

## Now Playing (v3.1.6)

Same connection, after `HELLO` returns `OK`:

```
NOW <title>|<artist>                       -> ACK | DENY
ART <w> <h> <nbytes>\n<nbytes raw bytes>   -> ACK | DENY
ART 0 0 0                                  -> ACK
```

Title and artist are printable ASCII, 0..47 characters. `NOW |` clears the track and does not clear art. Art is RGB565 little-endian, row-major, at most 48x48.

Windows (`run_companion.bat`) reads CPU and RAM locally and, unless you pass `--now`, sends the Spotify window title as `NOW`. Pairing file: `%APPDATA%\aura-companion\pair.json`.

```bat
run_companion.bat --host 192.168.1.42 --token K7M2Q9XW
python aura_companion.py --now "Song title|Artist name"
python aura_companion.py --art 48x48:cover.rgb565
```
