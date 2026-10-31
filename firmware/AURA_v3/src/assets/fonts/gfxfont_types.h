// gfxfont_types.h - Adafruit-GFX-compatible font structs, standalone (only <stdint.h>).
// Layout is byte-identical to Adafruit_GFX gfxfont.h, so these headers also work with
// Adafruit_GFX / TFT_eSPI setFreeFont / LovyanGFX if those are ever used.
#pragma once
#include <stdint.h>
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef _GFXFONT_H_
#define _GFXFONT_H_
typedef struct {
  uint16_t bitmapOffset;  // offset into the font's Bitmaps[]
  uint8_t width;          // bitmap width in px
  uint8_t height;         // bitmap height in px
  uint8_t xAdvance;       // cursor advance in px
  int8_t xOffset;         // x from cursor to bitmap left edge
  int8_t yOffset;         // y from BASELINE to bitmap top edge (negative = above baseline)
} GFXglyph;
typedef struct {
  uint8_t *bitmap;        // glyph bitmaps, 1 bpp, MSB first, bits packed continuously per glyph (row stride = width bits, NOT byte aligned), each glyph starts on a byte
  GFXglyph *glyph;        // glyph table, index = code - first
  uint16_t first;         // first code
  uint16_t last;          // last code
  uint8_t yAdvance;       // line height in px
} GFXfont;
#endif
