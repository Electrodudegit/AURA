// touch_xpt2046.cpp - ESP-IDF spi_master device on SPI3_HOST (shared with ILI9341).
//
// One 40-byte full-duplex transaction per tick, 16 clocks per conversion
// (command byte overlaps the previous result). All commands use PD1:PD0 = 00
// so the ADC powers down between conversions with PENIRQ enabled.
//   Z1(dummy) Z1 Z2  X(dummy) X*7  Y(dummy) Y*7  = 19 conversions
#include "touch_xpt2046.h"
#include "config.h"
#include "aura_log.h"
#include <Arduino.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_attr.h"

#define CMD_X  0xD0
#define CMD_Y  0x90
#define CMD_Z1 0xB0
#define CMD_Z2 0xC0
#define N_CONV (3 + 1 + TOUCH_SAMPLES + 1 + TOUCH_SAMPLES)
#define XFER_LEN 40
static_assert(2 * N_CONV + 1 <= XFER_LEN, "touch transfer buffer too small");
static_assert(TOUCH_SAMPLES >= 3 && TOUCH_SAMPLES <= 15, "TOUCH_SAMPLES out of range");

static spi_device_handle_t s_dev = nullptr;
static WORD_ALIGNED_ATTR uint8_t s_tx[XFER_LEN];  // .bss, DMA-capable internal DRAM
static WORD_ALIGNED_ATTR uint8_t s_rx[XFER_LEN];
static volatile bool s_irq_flag = false;
static volatile uint32_t s_irq_edges = 0;

static TouchCal s_cal;
static bool s_cal_nvs = false;
static bool s_jump_reject = true;
static TouchStats s_st;

// Debounce / filter state
static bool s_down = false;
static uint8_t s_cnt_valid = 0, s_cnt_invalid = 0;
static int32_t s_fx4 = 0, s_fy4 = 0;  // IIR state, Q4 screen px
static TouchSample s_out;
static uint32_t s_last_fallback = 0;
static TouchIrqStatus s_irq_status = TOUCH_IRQ_UNKNOWN;

static void IRAM_ATTR touch_isr(void) {
  s_irq_flag = true;  // hint only; no SPI, no logging here
  s_irq_edges = s_irq_edges + 1;
}

bool touch_init(void) {
  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = TOUCH_SPI_HZ;
  dev.mode = 0;
  dev.spics_io_num = PIN_TOUCH_CS;
  dev.queue_size = 1;
  if (spi_bus_add_device(SPI3_HOST, &dev, &s_dev) != ESP_OK) return false;

  // Build the fixed command sequence once.
  memset(s_tx, 0, sizeof(s_tx));
  int i = 0;
  s_tx[2 * i++] = CMD_Z1;
  s_tx[2 * i++] = CMD_Z1;
  s_tx[2 * i++] = CMD_Z2;
  s_tx[2 * i++] = CMD_X;
  for (int k = 0; k < TOUCH_SAMPLES; k++) s_tx[2 * i++] = CMD_X;
  s_tx[2 * i++] = CMD_Y;
  for (int k = 0; k < TOUCH_SAMPLES; k++) s_tx[2 * i++] = CMD_Y;

  pinMode(PIN_TOUCH_IRQ, INPUT_PULLUP);  // PENIRQ is open-drain style; module may also pull up
  attachInterrupt(digitalPinToInterrupt(PIN_TOUCH_IRQ), touch_isr, FALLING);

  memset(&s_st, 0, sizeof(s_st));
  memset(&s_out, 0, sizeof(s_out));
  s_cal_nvs = touch_cal_load(&s_cal);
  return true;
}

bool touch_irq_low(void) { return gpio_get_level((gpio_num_t)PIN_TOUCH_IRQ) == 0; }

void touch_set_cal(const TouchCal *c, bool from_nvs) { s_cal = *c; s_cal_nvs = from_nvs; }
const TouchCal *touch_get_cal(void) { return &s_cal; }
bool touch_cal_from_nvs(void) { return s_cal_nvs; }
void touch_set_jump_reject(bool on) { s_jump_reject = on; }
void touch_get_stats(TouchStats *s) { *s = s_st; s->irq_edges = s_irq_edges; }
TouchIrqStatus touch_irq_status(void) { return s_irq_status; }

static inline uint16_t conv(int idx) {
  return (uint16_t)((((uint16_t)s_rx[2 * idx + 1] << 8) | s_rx[2 * idx + 2]) >> 3) & 0x0FFF;
}

// Sort small array, return mean of the middle (n-2), spread of the middle.
static uint16_t trimmed_mean(uint16_t *v, int n, uint16_t *spread) {
  for (int i = 1; i < n; i++) {
    uint16_t key = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
    v[j + 1] = key;
  }
  uint32_t sum = 0;
  for (int i = 1; i < n - 1; i++) sum += v[i];
  *spread = (uint16_t)(v[n - 2] - v[1]);
  return (uint16_t)((sum + (uint32_t)(n - 2) / 2) / (uint32_t)(n - 2));
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool touch_tick(uint32_t now, TouchSample *out) {
  s_out.fresh = false;
  const bool irq_low = touch_irq_low();
  const bool edge = s_irq_flag;
  s_irq_flag = false;
  bool fallback = false;
  if (!s_down && s_cnt_valid == 0 && !irq_low && !edge) {
    // Idle and no pen hint. Poll anyway at ~20 Hz in case PENIRQ is not wired.
    if ((uint32_t)(now - s_last_fallback) < TOUCH_FALLBACK_MS) {
      s_st.skipped++;
      *out = s_out;
      return true;
    }
    fallback = true;
  }
  if (fallback || (!s_down && s_cnt_valid == 0)) s_last_fallback = now;
  if (fallback) s_st.fallback_reads++;

  // PENIRQ toggles during conversions; mask the hint while we read.
  gpio_intr_disable((gpio_num_t)PIN_TOUCH_IRQ);
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = XFER_LEN * 8;
  t.rxlength = XFER_LEN * 8;
  t.tx_buffer = s_tx;
  t.rx_buffer = s_rx;
  const esp_err_t e = spi_device_polling_transmit(s_dev, &t);
  s_irq_flag = false;
  gpio_intr_enable((gpio_num_t)PIN_TOUCH_IRQ);
  s_st.reads++;

  if (e != ESP_OK) {
    s_st.spi_err++;
    *out = s_out;
    return false;  // bus error: no state change, caller counts it
  }
  bool valid = true;

  uint16_t z = 0, rx = 0, ry = 0, spx = 0, spy = 0;
  int32_t sx = 0, sy = 0;
  if (valid) {
    const int32_t z1 = conv(1), z2 = conv(2);
    int32_t zz = z1 + 4095 - z2;
    z = (uint16_t)clampi(zz, 0, 0xFFFF);
    uint16_t xs[TOUCH_SAMPLES], ys[TOUCH_SAMPLES];
    for (int k = 0; k < TOUCH_SAMPLES; k++) {
      xs[k] = conv(4 + k);
      ys[k] = conv(4 + TOUCH_SAMPLES + 1 + k);
    }
    rx = trimmed_mean(xs, TOUCH_SAMPLES, &spx);
    ry = trimmed_mean(ys, TOUCH_SAMPLES, &spy);
    s_out.z = z;

    const uint16_t zthr = s_down ? TOUCH_Z_RELEASE : TOUCH_Z_PRESS;
    if (z1 == 0 || z <= zthr) { valid = false; s_st.rej_z++; }
    else if (rx == 0 || ry == 0 || rx >= 4095 || ry >= 4095) { valid = false; s_st.rej_range++; }
    else if (spx > TOUCH_SPREAD_MAX || spy > TOUCH_SPREAD_MAX) { valid = false; s_st.rej_spread++; }
  }
  if (valid) {
    int32_t nx, ny;
    touch_cal_apply(&s_cal, rx, ry, &nx, &ny);
    touch_rot_to_screen(TFT_ROTATION, nx, ny, &sx, &sy);
    const int32_t w = (TFT_ROTATION & 1) ? TOUCH_NATIVE_H : TOUCH_NATIVE_W;
    const int32_t h = (TFT_ROTATION & 1) ? TOUCH_NATIVE_W : TOUCH_NATIVE_H;
    sx = clampi(sx, 0, w - 1);
    sy = clampi(sy, 0, h - 1);
    if (s_down && s_jump_reject) {
      const int32_t dx = sx - (s_fx4 >> 4), dy = sy - (s_fy4 >> 4);
      if (dx > TOUCH_JUMP_PX || dx < -TOUCH_JUMP_PX || dy > TOUCH_JUMP_PX || dy < -TOUCH_JUMP_PX) {
        valid = false;
        s_st.rej_jump++;
      }
    }
  }

  if (!s_down) {
    if (valid) {
      // First valid press decides whether PENIRQ is actually routed: a real
      // press pulls it low, so a press found only by the fallback poll with no
      // IRQ edge ever seen means GPIO22 is not connected.
      if (s_irq_status == TOUCH_IRQ_UNKNOWN) {
        if (irq_low || edge || s_irq_edges > 0) {
          s_irq_status = TOUCH_IRQ_SEEN;
          aura_log("[touch] irq seen");
        } else if (fallback) {
          s_irq_status = TOUCH_IRQ_UNROUTED;
          aura_log("[touch] irq not seen, polling fallback");
        }
      }
      s_cnt_valid++;
      // IIR reset on a new press: start from the latest sample.
      s_fx4 = sx << 4;
      s_fy4 = sy << 4;
      if (s_cnt_valid >= TOUCH_DEBOUNCE_DOWN) {
        s_down = true;
        s_cnt_invalid = 0;
      }
    } else {
      s_cnt_valid = 0;
    }
  } else {
    if (valid) {
      s_cnt_invalid = 0;
      const int32_t tx4 = sx << 4, ty4 = sy << 4;
      s_fx4 += (tx4 - s_fx4) / (1 << TOUCH_IIR_SHIFT);
      s_fy4 += (ty4 - s_fy4) / (1 << TOUCH_IIR_SHIFT);
    } else if (++s_cnt_invalid >= TOUCH_DEBOUNCE_UP) {
      s_down = false;
      s_cnt_valid = 0;
      s_cnt_invalid = 0;
    }
  }

  if (valid) {
    s_out.rx = rx;
    s_out.ry = ry;
    s_out.t_ms = now;
    s_out.fresh = true;
  }
  s_out.down = s_down;
  s_out.x = (int16_t)((s_fx4 + 8) >> 4);
  s_out.y = (int16_t)((s_fy4 + 8) >> 4);
  *out = s_out;
  return true;
}
