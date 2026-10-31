#!/usr/bin/env python3
"""Decode an AURA serial snapshot into a PNG.

Capture the serial log while running `snap` (CLI) or Settings > Diagnostics >
Snapshot, then:   python3 snap2png.py serial.log out.png
Lines used:  [snap] begin w=320 h=240 ...  /  [snap] b=<band> o=<offset> <base64>  /  [snap] end
Pixels are RGB565 big-endian (panel byte order), 12 bands of 20 rows.
"""
import base64, re, sys
from PIL import Image

def main(log, out):
    w, h, bh = 320, 240, 20
    bands = {}
    for line in open(log, errors="replace"):
        m = re.search(r"\[snap\] begin w=(\d+) h=(\d+) fmt=\S+ band_h=(\d+)", line)
        if m:
            w, h, bh = int(m[1]), int(m[2]), int(m[3]); bands = {}
            continue
        m = re.search(r"\[snap\] b=(\d+) o=(\d+) ([A-Za-z0-9+/=]+)", line)
        if m:
            b, o = int(m[1]), int(m[2])
            buf = bands.setdefault(b, bytearray(w * bh * 2))
            data = base64.b64decode(m[3])
            buf[o:o + len(data)] = data
    im = Image.new("RGB", (w, h))
    px = im.load()
    for b, buf in bands.items():
        for i in range(0, len(buf), 2):
            v = (buf[i] << 8) | buf[i + 1]
            n = i // 2
            x, y = n % w, b * bh + n // w
            if y < h:
                px[x, y] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    im.save(out)
    print(f"{out}: {len(bands)}/{h // bh} bands")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
