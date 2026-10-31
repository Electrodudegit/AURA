// fonts_poppins.h - AURA v3.1.0 font index (Poppins, SIL OFL 1.1, see OFL.txt). Depends only on <stdint.h>.
// Sizes are em pixels (same as the Pillow mockups' font size). 1 bpp GFXfont; draw with your band renderer:
//   for each glyph: x0 = cursorX + xOffset, y0 = baselineY + yOffset; walk width*height bits MSB-first from bitmap[bitmapOffset].
#pragma once
#include "gfxfont_types.h"
#include "poppins_semibold8.h"
#include "poppins_semibold9.h"
#include "poppins_semibold12.h"
#include "poppins_semibold13.h"
#include "poppins_semibold14.h"
#include "poppins_semibold16.h"
#include "poppins_semibold18.h"
#include "poppins_regular10.h"
#include "poppins_regular12.h"
#include "poppins_bold22.h"
#include "poppins_bold26.h"
#include "poppins_bold34_num.h"
#include "poppins_bold46_digits.h"
#include "poppins_bold58_digits.h"
#include "poppins_bold74_digits.h"

// ---------------------------------------------------------------------------------------------
// Fonts (15 files, total 27201 B Flash incl. 8-B glyph records)
//  poppins_semibold8        SemiBold    8 px  0x20-0x82  99 glyphs  yAdv  12    1129 B
//  poppins_semibold9        SemiBold    9 px  0x20-0x82  99 glyphs  yAdv  14    1153 B
//  poppins_semibold12       SemiBold   12 px  0x20-0x82  99 glyphs  yAdv  18    1442 B
//  poppins_semibold13       SemiBold   13 px  0x20-0x82  99 glyphs  yAdv  20    1494 B
//  poppins_semibold14       SemiBold   14 px  0x20-0x82  99 glyphs  yAdv  21    1618 B
//  poppins_semibold16       SemiBold   16 px  0x20-0x82  99 glyphs  yAdv  24    1843 B
//  poppins_semibold18       SemiBold   18 px  0x20-0x82  99 glyphs  yAdv  27    2125 B
//  poppins_regular10        Regular    10 px  0x20-0x82  99 glyphs  yAdv  15    1233 B
//  poppins_regular12        Regular    12 px  0x20-0x82  99 glyphs  yAdv  18    1385 B
//  poppins_bold22           Bold       22 px  0x20-0x82  99 glyphs  yAdv  33    2953 B
//  poppins_bold26           Bold       26 px  0x20-0x82  99 glyphs  yAdv  39    3787 B
//  poppins_bold34_num       Bold       34 px  0x20-0x3A  27 glyphs  yAdv  51    1421 B
//  poppins_bold46_digits    Bold       46 px  0x30-0x3A  11 glyphs  yAdv  69    1128 B
//  poppins_bold58_digits    Bold       58 px  0x30-0x3A  11 glyphs  yAdv  87    1727 B
//  poppins_bold74_digits    Bold       74 px  0x30-0x3A  11 glyphs  yAdv 111    2763 B
//  TOTAL 27201 B (~26.6 KB)
//
// Extended glyphs (text fonts only, codes after '~'): firmware converts UTF-8 before drawing:
//   0x7F = U+00B0 degree '°' (C2 B0)   0x80 = U+00B7 middle dot '·' (C2 B7)
//   0x81 = U+2026 ellipsis '…' (E2 80 A6)   0x82 = U+203A '›' (E2 80 BA)
#define AF_DEG   "\x7F"
#define AF_DOT   "\x80"
#define AF_ELL   "\x81"
#define AF_CHEV  "\x82"
// *_num  = 0x20-0x3A (space ! " # $ % & ' ( ) * + , - . / 0-9 :)   *_digits = 0x30-0x3A (0-9 and ':')
//
// DejaVu (mockups) -> Poppins mapping: DejaVuSans-Bold <=18 px -> SemiBold, >=22 px -> Bold;
// DejaVuSans -> Regular; DejaVuSansMono-Bold micro caps (9/8/7 px, 7/6/5 px advance) -> SemiBold 9/8
// drawn UPPERCASE with +1 px tracking (Poppins is proportional; old fixed advance no longer applies).
// Consolidation: 11->12, 15/17->16, 20->18, 28/30->26, 32/38->34_num, 72->74_digits (F5).
// Poppins is ~8-10 % wider than DejaVu at equal em size: re-check long strings in captures.

// ---------------------------------------------------------------------------------------------
// Role aliases (use these in UI code so sizes can be retuned in one place)
#define AF_MICRO        poppins_semibold9     // status name, micro-caps labels
#define AF_MICRO_S      poppins_semibold8     // EXAMPLE tag, chips, gauge labels, page/day labels, LAST
#define AF_BODY_S       poppins_regular10     // subtitles, lo temps
#define AF_BODY         poppins_regular12     // paragraphs (oracle reply, news summary), F1 date '20 OCTOBER'
#define AF_LABEL        poppins_semibold12    // grid tile name, small values, F6 time chip, chips
#define AF_LABEL_M      poppins_semibold13    // F1 'TUESDAY' + gauge values, system rows
#define AF_ROW          poppins_semibold14    // list idle name, F6 numerals
#define AF_ROW_FOCUS    poppins_semibold16    // list focused name + value, oracle/news headlines
#define AF_TITLE        poppins_semibold18    // card big values (PM, humidity)
#define AF_NUM_M        poppins_bold22        // world clock times
#define AF_NUM_L        poppins_bold26        // crypto/stocks price
#define AF_NUM_XL       poppins_bold34_num    // AQI, score, focus timer
#define AF_WX_TEMP      poppins_bold46_digits // weather app temperature
#define AF_CLOCK_F2     poppins_bold58_digits // F2 buddy clock time
#define AF_FLAP         poppins_bold74_digits // F1/D2 split-flap digits, F5 time

// ---------------------------------------------------------------------------------------------
// Per-screen font table (element : font : anchor). Anchors: L=left, C=centre, R=right; M=middle of cap height, B=baseline.
// STATUS BAR (all screens)  name: AF_MICRO L/M y=11 | EXAMPLE: AF_MICRO_S L/M | '88%': AF_MICRO R/M | updated pill: AF_MICRO_S
// HOME F1/D2   digits: AF_FLAP C/M in 70x92 tiles | 'TUESDAY': AF_LABEL_M L/M y=139 track 1.8 | '20 OCTOBER': AF_BODY L/M track 1.2
//              gauge value: AF_LABEL_M C/M | gauge label: AF_MICRO_S C | footer line: AF_MICRO_S C y=214
// HOME F2      time: AF_CLOCK_F2 C/M (160,166) | info line: AF_MICRO C y=204
// HOME F5      time: AF_FLAP C/M (160,84) | date line: AF_MICRO C y=128 | day: AF_MICRO_S C | hi: AF_LABEL L/M | lo: AF_BODY_S L/M
// HOME F6      numerals 12/3/6/9: AF_ROW C/M | time chip: AF_LABEL C/M | 'TUE 20': AF_MICRO_S C
// MENU GRID    tile name: AF_LABEL C/M y+64 | value: AF_MICRO_S C y+72 | LAST: AF_MICRO_S | status 'P1/3 . 06/15': AF_MICRO_S R
// MENU LIST    focused name: AF_ROW_FOCUS L/M | subtitle: AF_BODY_S L/M | focused value: AF_ROW_FOCUS R/M | label: AF_MICRO_S R
//              idle name: AF_ROW L/M | idle value: AF_MICRO R | section header: AF_MICRO_S L
// SETTINGS     focused row: AF_ROW_FOCUS(15->16) + AF_BODY_S | rows: AF_LABEL_M | segmented: AF_LABEL | values: AF_MICRO R
// WEATHER      temp: AF_WX_TEMP | condition: AF_ROW | cards value: AF_TITLE | labels/units: AF_MICRO
// FORECAST     all text AF_MICRO / AF_MICRO_S
// SKY          AF_MICRO
// AIR          AQI: AF_NUM_XL C/M | PM values: AF_TITLE | labels: AF_MICRO
// CRYPTO       price: AF_NUM_L L/M | row prices: AF_LABEL R/M | symbols: AF_MICRO / AF_MICRO_S
// STOCKS       index: AF_NUM_L | rows: AF_LABEL R/M | labels: AF_MICRO
// NEWS         headline: AF_ROW_FOCUS(17->16) | summary: AF_BODY (11->12) | wire/footer: AF_MICRO / AF_MICRO_S
// CRICKET      score: AF_NUM_XL C/M | stats: AF_ROW_FOCUS(15->16) | labels/badges: AF_MICRO
// FOOTBALL     score: AF_NUM_XL C/M (38->34) | events: AF_LABEL (11->12) | minute pill: AF_MICRO
// FOCUS        timer: AF_NUM_XL C/M (32->34) | break: AF_ROW_FOCUS | chips/labels: AF_MICRO / AF_MICRO_S
// WORLD        times: AF_NUM_M R/M | city/offset: AF_MICRO / AF_MICRO_S
// SYSTEM       card values: AF_ROW_FOCUS(15->16) | rows: AF_LABEL_M | values: AF_MICRO R
// ORACLE v2    'Ask me anything': AF_ROW_FOCUS(15->16) | chips/rows: AF_LABEL | reply: AF_BODY | sub: AF_BODY_S / regular9->AF_BODY_S
// BUDDY        hint: AF_MICRO_S C

// ---------------------------------------------------------------------------------------------
// Developer short names F_P_<R|SB|B><px>[N|D] -> the same GFXfont objects (no extra Flash).
//   R = Regular, SB = SemiBold, B = Bold; N = *_num (0x20-0x3A), D = *_digits (0-9 and ':').
//   Use like the AF_ names: &F_P_SB12 is a const GFXfont*.
#define F_P_SB8    poppins_semibold8
#define F_P_SB9    poppins_semibold9
#define F_P_SB12   poppins_semibold12
#define F_P_SB13   poppins_semibold13
#define F_P_SB14   poppins_semibold14
#define F_P_SB16   poppins_semibold16
#define F_P_SB18   poppins_semibold18
#define F_P_R10    poppins_regular10
#define F_P_R12    poppins_regular12
#define F_P_B22    poppins_bold22
#define F_P_B26    poppins_bold26
#define F_P_B34N   poppins_bold34_num
#define F_P_B46D   poppins_bold46_digits
#define F_P_B58D   poppins_bold58_digits
#define F_P_B74D   poppins_bold74_digits
// Degree sign is code 0x7F in every text font (as requested); the digit-only fonts have no degree sign.
#define F_P_DEG_CHAR  0x7F     // char code   e.g. buf[n++] = (char)F_P_DEG_CHAR;
#define F_P_DEG_STR   "\x7F"   // string form e.g. "27" F_P_DEG_STR "C"  (same as AF_DEG)
// Optional 4-bit anti-aliased hero digits: see fonts_poppins_aa.h (separate include, the 1-bit fonts stay).
