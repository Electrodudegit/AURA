#!/usr/bin/env python3
"""Contact sheet of captures matching a glob: sheet.py <out.png> <glob> [cols]"""
import glob, sys
from PIL import Image, ImageDraw
out, pat = sys.argv[1], sys.argv[2]
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 3
fs = sorted(glob.glob(pat))
rows = (len(fs) + cols - 1) // cols
W, H = 320, 254
im = Image.new("RGB", (cols * (W + 6), rows * (H + 6)), (60, 60, 60))
d = ImageDraw.Draw(im)
for i, f in enumerate(fs):
    x, y = (i % cols) * (W + 6), (i // cols) * (H + 6)
    im.paste(Image.open(f), (x, y + 14))
    d.text((x + 2, y + 1), f.split("/")[-1], fill=(255, 255, 0))
im.save(out)
print(out, len(fs))
