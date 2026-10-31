#!/usr/bin/env python3
"""Convert the shim's PPM frames to PNG (and delete the PPMs)."""
import glob, os, sys
from PIL import Image
d = sys.argv[1] if len(sys.argv) > 1 else "../captures"
n = 0
for p in sorted(glob.glob(os.path.join(d, "*.ppm"))):
    Image.open(p).save(p[:-4] + ".png")
    os.remove(p)
    n += 1
print(f"{n} PNG captures in {d}")
