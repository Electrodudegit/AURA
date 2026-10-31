# AURA v3.1.0 UI spec (320x240 landscape, RGB565)

Every number here is also in `face_specs.h` (namespace `aura::spec`). Colours use the names in `theme_v2.h`, fonts use the role aliases in `fonts/fonts_poppins.h`, and times are in ms.
Colour names here are the v2 names; `theme_v2.h` aliases them to the theme_v3 names (`C_TEXT`=`C_TXT`, `C_ACCENT`=`C_ACC`, `C_MUTED`=`C_MUT`, `C_ACCENT_BORDER`=`C_FOCUS_BORDER`, `C_H_*`=`C_HOME_*`).
Sources: `src/home_faces.py`, `home_faces_v2.py`, `home_concepts.py`, `screens.py` (aurora), `faces.py` (eyes), `menu_v2.py` and `menu_final.py`, `anim_icons.py`.
The mockups were rendered at 12.5–25 fps, so keep the **ms** values and derive frame counts from your loop rate.

## Status bar (all screens)
| Element | Geometry | Font / colour |
|---|---|---|
| Bar | 0,0,320,24 | C_BG (home: C_H_BG) |
| Rule | y=23 | v2: C_LINE · home: aur_grad(x) at 43% |
| Tick (home / no icon) | 12,7 3x10 r1 | accent |
| Header icon | 9,4 16x16 (ICONS_16) | C_ACCENT; name then starts at x=31 |
| Name | x=21, top y=7 | AF_MICRO C_TEXT, then 'EXAMPLE' AF_MICRO_S C_EXAMPLE_TAG +10 px |
| Wi-Fi | centre 251,17; arcs r 5/8/11, 225°–315°, w2 | C_MUTED; offline: C_DOWN slash |
| Battery % | right x=283, y=7 | AF_MICRO_S C_MUTED |
| Battery | 288,7 shell 18x9 r2 + nub 2x4 | outline C_MUTED, fill C_TEXT (home C_H_UP) |

## Home common
- **Page dots:** y=234, 4 faces (F1, F2, F5, F6). The active dot is a 14x4 pill; the others are 4x4. Step 10 px (18 after the pill).
- **Aurora** (3 ribbons, 2 px columns):
  - `y = base + 11·sin(0.021x + 1.15T + ph) + 5·sin(0.052x − 0.75T + ph)`, intensity `0.55 + 0.45·sin(0.09x + 1.9T + 1.7ph)`.
  - Ribbon i: base 18+15i, length 36−7i rows, peak α (150−28i)/255, phases 0 / 2.1 / 4.2.
  - Vertical profile `((len−2k)/len)²`.
  - 34 stars from a hash.
  - Boost: `BG + (A−BG)·boost`.
- **Particles:** up to 40, from a seed table (`PARTICLES[40]`). Each rises at 6–16 px/s, wobbles 3·sin(1.3t + x0) and twinkles 0.5+0.5·sin(5t + x0). Colour is α (80+150·tw)/255; when tw > 0.8 it is drawn as a 2 px cross.

| Face | Aurora y0/h, T-scale, boost | Particles y0/h/n, t-scale |
|---|---|---|
| F1/D2 | 24/126, ×2.1, 1.45 | 24/126/40, ×1 |
| F2 | 150/60, ×2.0, 0.80 | 150/60/24, ×1 |
| F6 | — (gradient 24..239) | 24/210/30, ×0.5 |

## F1 / D2 split-flap aurora (default)
- **Tiles:** 70x92 at x = 4/78/166/240, y=30, r11, face α 214. AF_FLAP digit centred with a +3 px shadow. Hinge at y 45–47, pins 5x12, shadow rects (+1,+4).
- **Flip:** 500 ms.
  - u<0.5: the top folds with k = cos(πu), height 46k, shade 170(1−k)→90(1−k).
  - u≥0.5: the bottom drops with k = −cos(πu), shade 110(1−k)→0, and a glint line α 160(1−k).
  - About 13 frames at 25 fps.
- **Colon:** dots at (157, 76±13), r4. Blinks 500 ms on, then 500 ms at 39%.
- **Date (y=139 centre):** 'TUESDAY' AF_LABEL_M tracked +1.8, then a 3 px accent dot, then '20 OCTOBER' AF_BODY tracked +1.2 in C_H_MUTED.
- **Seconds bar:** y=154, x 16–304. Track C_H_TEXT at 13%, progress aur_grad at 82%, head dot r2.
- **Gauges:**
  - Centres x 56/160/264, y=184, r21, width 4, arc 135°→405° (clockwise, screen coordinates).
  - 11 ticks every 27°; major ticks at k%5==0.
  - Count-up 1100 ms, ease-out quart. After 1200 ms a 3-dot comet runs at 120°/s.
  - Value AF_LABEL_M centred at y 184; label AF_MICRO_S at y 198.
- **Footer:** AF_MICRO_S centred at y=214.

## F2 Buddy clock
- **Eyes:** rendered at 320x240, then crop (14,44)–(306,196) is scaled to 216x112 at (52,26), so scale ≈0.74. Composite with 'lighter'. Full-size eyes are 92x92 at x=160±75, y=110, CYAN.
- **Time:** AF_CLOCK_F2 centred at (160,166) with a 2 px dark stroke and glow (3/2/1 px at 15/28/43%).
- **Info line:** AF_MICRO at y=204.
- **Animation (3000 ms loop):**
  - Blink peaks at 1100 ms, half-width 120 ms (triangular).
  - Glance to −0.85 at 1600 ms (150 ms ramp), then +0.85 at 2200 ms (150 ms ramp).
  - Returns to 0 from 2750 ms over 200 ms.

## F5 Weather scene
- **Sky:** 0,24,320,216, gradient C_H_RAIN_TOP→BOT.
- **Rain:** 26 drops; each is a line (x,y)→(x−3,y+9) at 60–120 px/s ×1.2.
- **Clouds:** C_H_RAIN_CLOUD, drifting at 10 and 5 px/s.
- **Time:** at (160,84) in AF_FLAP with C_H_HUMID glow at 0.7. Date AF_MICRO at y 128; rule at y 150.
- **Glass strip:** (8,160)–(312,222), r10, 60% BG, outline C_H_HUMID at 43%.
- **4 days** at x = 18+74i:
  - label (x+28,166)
  - glyph (x+16,192)
  - high temp AF_LABEL (x+40,186), left-middle
  - low temp AF_BODY_S (x+40,203), C_H_DIM

## F6 Analog aurora
- **Dial:** centre (160,130), R 96.
- **Ring:** 120 arcs of 3°, width 2, aur_grad, brightness (90+60·sin(2t+0.1k))/255.
- **Ticks:** major R−12..R−4, width 3. Minor R−7..R−4, width 1.
- **Numerals:** 12/3/6/9 in AF_ROW at radius 70.
- **Hands:**

| Hand | Length | Width | Tail | Colour |
|---|---|---|---|---|
| Hour | 50 | 6 | 10 | AUR0 |
| Minute | 74 | 4 | 10 | AUR1 |
| Second | 82 | 2 | 10 | AUR2 |

  - Hub: r5 C_H_TEXT plus an r2 AUR2 dot.
- **Time chip:** 56x16 r5, AF_LABEL.
- **Placement rule (once per minute):**
  - Slots: bottom (0,+50), top (0,−46), right (+37,0) with a 20° penalty, left (−37,0) with a 20° penalty.
  - Pick the slot maximising `min(angdist(slot, hour), angdist(slot, minute)) − penalty`.
  - Date AF_MICRO_S goes at chip y−20 for the bottom slot, otherwise y+13.

## Menu grid (default)
- **Tiles:** 96x88 r10 at x 8/112/216, y 30/126. Icon 48 centred at (x+48, y+33). Name AF_LABEL at y+64; value AF_MICRO_S at y+72.
- **Focus:**
  - Shadow (+1,+3); the plate is lifted 1 px and drawn in C_TILE_HI.
  - Border C_ACCENT_BORDER; underline (x+36, y+81, 25x3).
  - LAST chip at (x+60, y+5), 29x12 r5.
- **Motion:**

| Motion | Spec |
|---|---|
| Focus slide | 140 ms, ease-out cubic `1−(1−u)³` |
| Hop | 40 ms steps, scale 1.05 → 1.08 → 1.05 → 1.02 → 1.00, y −2·(s−1)/0.08 |
| Page slide | 200 ms, ease-out cubic |
| Crossfade | 200 ms |
| Split-flap page | 320 ms. Tile j starts at frame 0.5j and lasts 3 frames (40 ms each); squash \|cos πu\|; content swaps at u=0.5 |
| Page dots | y=226 |

## Menu list
- **Viewport and rows:**
  - Viewport y 24–240; rows 40, headers 22, top pad 4.
  - Focus glass x 8–305, 42 high, r10, accent bar (12, cy−10, 3x21).
  - Icon centre x 34, name x 58, value right edge x 296.
- **Rail:** x 313, y 30–234, minimum thumb 18.
- **Edge fades:** top 6 px, bottom 10 px.
- **Fonts:** idle row AF_ROW. Focused row: AF_ROW_FOCUS name at cy−7, AF_BODY_S subtitle at cy+9, value AF_ROW_FOCUS at cy−6, label AF_MICRO_S at cy+6.

## Living icons
- Timing: 80 ms steps (12.5 fps). Sequences and loop lengths are in `icons_anim_a4.h`.
- Dirty rects: grid 52x52 around the 48 px icon; list 28x28.

## Dirty regions and SPI cost
These are **estimates** from mockup frame diffs at 40 MHz SPI (full frame 30.7 ms), not measurements.

| Screen | Changing regions | Avg / max of screen | SPI ms avg / max |
|---|---|---|---|
| F1/D2 | aurora rows 24–149, seconds bar 152–156, gauge comets, flipping tile rect | 37.8 / 44.8 % | 11.6 / 13.8 |
| F2 | face rect 52,26,216,112 + aurora rows 150–209 | 41.2 / 42.6 % | 12.7 / 13.1 |
| F5 | rain/cloud sky above the strip (24–159) | 14.3 / 18.1 % | 4.4 / 5.6 |
| F6 | second hand sweep, ring shimmer, particles | 20.4 / 23.6 % | 6.3 / 7.2 |
| Grid focus move | old + new tile rects | 16.1 / 25.9 % | 4.9 / 8.0 |
| Page slide / crossfade | rows 24–235 | 78 / 81 % · 69 / 73 % | 24.0 / 25.0 · 21.3 / 22.4 |
| Split-flap page | staggered tiles | 31.8 % avg | 9.8 avg |
| List focus move (with scroll) | viewport | 44.8 / 79.4 % | 13.8 / 24.4 |
| Icon animation | 52x52 / 28x28 | 1.0 / 2.3 % | 0.3 / 0.7 |

## Values that could not be extracted cleanly
1. **Mockup vs device timing.** The mockup focus slide is 4 frames × 40 ms = 160 ms; the device target is 140 ms (spec uses 140). The hop is 4 scaling steps plus a 1.00 settle, so 200 ms in total.
2. **F5 time size.** The mockup uses 72 px; the spec uses the shared 74 px digit font (+2 px, still centred at (160,84)).
3. **Glow and blur passes.** Baked glows on the home faces (tile glow, F2 and F5 digits) are multi-pass Pillow blurs. On device, use pre-rendered halos or skip them; the alpha percentages given are approximate.
4. **F5 clouds.** Cloud shapes come from the `wx_scene` ellipse clusters; only colour, sizes and speeds are specified, so check against the capture.
5. **Dirty and SPI numbers.** All are estimates, as noted above.
6. **Font width.** Poppins is about 8–10% wider than the DejaVu used in the mockups, so long strings (footer, news) need checking in `/workspace/aura_v3/captures/`.
