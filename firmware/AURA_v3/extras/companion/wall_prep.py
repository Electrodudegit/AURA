#!/usr/bin/env python3
"""Convert a picture or GIF into an AURA WALL blob (ADR-024) for `aura_companion.py --wall`.

  python3 wall_prep.py photo.jpg  my.wall     # fmt0 STILL_IDX8: 160x120, 256-colour palette (19712 B)
  python3 wall_prep.py anim.gif   my.wall     # fmt1 LOOP_IDX4: 80x60, 16 colours, <= 8 frames
  python3 wall_prep.py --still anim.gif my.wall   # force a still from the first frame

Needs Pillow (pip install pillow) on the laptop only. The AURA never decodes PNG/JPEG/GIF: it only
receives palette-indexed pixels. Local files only; nothing is uploaded.
"""
import argparse
import struct
import sys

try:
    from PIL import Image, ImageSequence
except ImportError:  # pragma: no cover
    sys.exit("wall_prep.py needs Pillow: pip install pillow")


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def fit(img, w, h):
    """Centre-crop to w:h, then resize (no letterbox)."""
    img = img.convert("RGB")
    sw, sh = img.size
    if sw * h > sh * w:
        nw = sh * w // h
        img = img.crop(((sw - nw) // 2, 0, (sw - nw) // 2 + nw, sh))
    else:
        nh = sw * h // w
        img = img.crop((0, (sh - nh) // 2, sw, (sh - nh) // 2 + nh))
    return img.resize((w, h), Image.LANCZOS)


def palette_bytes(pal_img, n):
    pal = (pal_img.getpalette() or [])[: n * 3]
    pal += [0] * (n * 3 - len(pal))
    return b"".join(struct.pack("<H", rgb565(*pal[i * 3:i * 3 + 3])) for i in range(n))


def still(img):
    q = fit(img, 160, 120).quantize(colors=256, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    out = palette_bytes(q, 256) + q.tobytes()
    assert len(out) == 19712
    return out


def loop(img, max_frames=8):
    frames, delays = [], []
    for fr in ImageSequence.Iterator(img):
        frames.append(fit(fr.copy(), 80, 60))
        delays.append(int(fr.info.get("duration", 100) or 100))
    if len(frames) > max_frames:  # evenly sample, keep total loop time
        step = len(frames) / max_frames
        total = sum(delays)
        frames = [frames[int(i * step)] for i in range(max_frames)]
        delays = [max(100, total // max_frames)] * max_frames
    f = len(frames)
    # One shared 16-colour palette for every frame: quantise a vertical strip of all frames.
    strip = Image.new("RGB", (80, 60 * f))
    for i, fr in enumerate(frames):
        strip.paste(fr, (0, 60 * i))
    q = strip.quantize(colors=16, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    idx = q.tobytes()
    packed = bytearray()
    for i in range(0, len(idx), 2):
        packed.append(((idx[i] & 15) << 4) | (idx[i + 1] & 15))  # high nibble = left pixel
    out = palette_bytes(q, 16) + bytes(packed) + b"".join(struct.pack("<H", max(100, min(65535, d))) for d in delays)
    assert len(out) == 32 + f * 2400 + f * 2
    return out, f


def main():
    ap = argparse.ArgumentParser(description="Make an AURA WALL blob (fmt0 still / fmt1 loop).")
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--still", action="store_true", help="always make a fmt0 still (first frame)")
    a = ap.parse_args()
    img = Image.open(a.src)
    animated = getattr(img, "n_frames", 1) > 1 and not a.still
    if animated:
        blob, f = loop(img)
        desc = "fmt1 LOOP_IDX4 80x60 f=%d" % f
    else:
        blob = still(img)
        desc = "fmt0 STILL_IDX8 160x120"
    with open(a.dst, "wb") as fh:
        fh.write(blob)
    print("%s -> %s (%s, %d B)" % (a.src, a.dst, desc, len(blob)))


if __name__ == "__main__":
    main()
