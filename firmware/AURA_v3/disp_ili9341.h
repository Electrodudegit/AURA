// disp_ili9341.h - minimal ILI9341 driver on ESP-IDF spi_master with DMA bands.
// Owner: ui_task only (core 1). Not thread-safe by design.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#define DISP_W 320
#define DISP_H 240
#define DISP_BAND_PIXELS (DISP_W * UI_BAND_H)

// Initialise the shared VSPI bus (SPI3_HOST), add the TFT device, reset and
// init the panel, start the backlight PWM. Returns false on any error.
bool disp_init(void);

// Band buffers (static, DMA-capable, RGB565 byte-swapped for the panel).
uint16_t *disp_band_buf(int slot);   // v3.1.3: slot 1 is nullptr while the bands are lent to TLS
// v3.1.3 (UI task only): lend the 25.6 KB band pair to a TLS session (a 6.4 KB half-band buffer
// replaces it; returns the net bytes freed, 0 if not possible) and take it back afterwards.
uint32_t disp_bands_lend(void);
bool disp_bands_restore(void);
int disp_band_rows(void);             // rows per buffer: UI_BAND_H, or UI_BAND_H/2 while lent
uint32_t disp_bands_lendable(void);   // bytes disp_bands_lend() would free now

// Block until the DMA for this buffer slot has completed (timeout ms). Returns false on timeout.
bool disp_band_wait(int slot, uint32_t timeout_ms);

// Queue a band (rows y0..y0+h-1, full width) from slot buffer. Non-blocking.
bool disp_band_send(int slot, int y0, int h);

// v3.1.3: queue a window (CASET x0..x0+w-1 / PASET y0..y0+h-1) of w*h pixels from the slot buffer.
bool disp_win_send(int slot, int x0, int y0, int w, int h);

// v3.1.3: non-blocking idle check (collects finished DMA results, never waits).
bool disp_poll_idle(void);

// Block until every queued display transaction has completed. Required before
// another device (touch) uses the bus.
bool disp_wait_idle(uint32_t timeout_ms);

void disp_backlight(uint8_t level);

uint32_t disp_spi_timeouts(void);
uint32_t disp_spi_errors(void);

// Swap an RGB565 colour into panel byte order.
static inline uint16_t disp_c(uint16_t rgb565) { return (uint16_t)((rgb565 >> 8) | (rgb565 << 8)); }
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
