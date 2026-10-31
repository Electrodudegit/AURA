#!/usr/bin/env python3
"""capdiff.py <dirA> <dirB> : per-capture pixel diff of two PPM sets (max channel delta, % pixels > 8/255)."""
import sys, os
def rd(p):
    b = open(p, 'rb').read()
    parts = b.split(b'\n', 3)
    w, h = map(int, parts[1].split()); return w, h, parts[3]
A, B = sys.argv[1], sys.argv[2]
rows = []
for f in sorted(os.listdir(A)):
    if not f.endswith('.ppm') or not os.path.exists(os.path.join(B, f)): continue
    wa, ha, da = rd(os.path.join(A, f)); wb, hb, db = rd(os.path.join(B, f))
    if (wa, ha) != (wb, hb): rows.append((f, -1, -1)); continue
    mx = 0; big = 0
    for i in range(0, len(da), 3):
        d = max(abs(da[i] - db[i]), abs(da[i+1] - db[i+1]), abs(da[i+2] - db[i+2]))
        if d > mx: mx = d
        if d > 8: big += 1
    rows.append((f, mx, 100.0 * big / (wa * ha)))
for f, mx, pc in rows:
    if len(sys.argv) > 3 or mx > 8: print(f"{f:40s} max={mx:3d} >8: {pc:6.2f}%")
print(f"{len(rows)} compared")
