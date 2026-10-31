// disp_ili9341.cpp - ILI9341 over ESP-IDF spi_master (ESP-IDF specific).
// Why not TFT_eSPI/LovyanGFX: the XPT2046 shares this bus at a different clock
// and must be read between DMA bands. Owning spi_master directly gives us two
// devices (40 MHz TFT, 2 MHz touch) with per-device clocks/CS handled by the
// driver, explicit DMA completion tracking, and no hidden bus locking.
#include "disp_ili9341.h"
#include <esp_heap_caps.h>
#include <Arduino.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "soc/gpio_reg.h"
#include "esp_attr.h"

#define TFT_HOST SPI3_HOST  // VSPI
#define TRANS_PER_BAND 6

static spi_device_handle_t s_tft = nullptr;
// .bss (zero-init, not in the flash image), word aligned. All ESP32 internal DRAM is
// DMA-capable, so WORD_ALIGNED_ATTR is sufficient; DMA_ATTR would also add DRAM_ATTR,
// which put these 38 KB into .data (and the app image) in p0.
// v3.1.3: both band buffers are ONE 25.6 KB heap block (DMA-capable internal RAM, allocated at
// boot while the heap is still contiguous) instead of .bss. For a TLS session the UI lends the
// whole block: a 6.4 KB half-band buffer is allocated first (elsewhere), then the 25.6 KB block
// is freed, so the gate always sees one contiguous >= 25.6 KB block on top of the old largest
// (dual-block rule >= 17408 met). Meanwhile the UI renders 10-row half bands single-buffered.
static uint16_t *s_band[2] = {nullptr, nullptr};
static uint16_t *s_pair = nullptr;   // the 25.6 KB block (both slots), nullptr while lent
static uint16_t *s_half = nullptr;   // the 6.4 KB half-band buffer while lent
#define DISP_BAND_CAPS (MALLOC_CAP_DMA | MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)
#define DISP_PAIR_BYTES (DISP_BAND_PIXELS * 2u * 2u)
#define DISP_HALF_BYTES (DISP_BAND_PIXELS * 2u / 2u)
static spi_transaction_t s_trans[2][TRANS_PER_BAND];
static bool s_busy[2] = {false, false};
static int s_pending = 0;
static uint32_t s_timeouts = 0;
static uint32_t s_errors = 0;
static WORD_ALIGNED_ATTR uint8_t s_scratch[16];

// ISR context (SPI driver): set DC from t->user before each transaction.
static void IRAM_ATTR tft_pre_cb(spi_transaction_t *t) {
  if ((uint32_t)t->user & 1u) REG_WRITE(GPIO_OUT_W1TS_REG, BIT(PIN_TFT_DC));
  else                        REG_WRITE(GPIO_OUT_W1TC_REG, BIT(PIN_TFT_DC));
}

static bool tft_poll(uint8_t dc, const uint8_t *data, size_t len) {
  if (len == 0) return true;
  if (len > sizeof(s_scratch)) return false;
  memcpy(s_scratch, data, len);
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = len * 8;
  t.tx_buffer = s_scratch;
  t.user = (void *)(uint32_t)dc;
  esp_err_t e = spi_device_polling_transmit(s_tft, &t);
  if (e != ESP_OK) { s_errors++; return false; }
  return true;
}

static bool tft_cmd(uint8_t cmd, const uint8_t *data, size_t len) {
  if (!tft_poll(0, &cmd, 1)) return false;
  return tft_poll(1, data, len);
}

static uint8_t madctl_for_rotation(int r) {
  uint8_t m;
  switch (r & 3) {
    case 0:  m = 0x40; break;  // MX
    case 1:  m = 0x20; break;  // MV
    case 2:  m = 0x80; break;  // MY
    default: m = 0xE0; break;  // MX|MY|MV
  }
  if (TFT_BGR) m |= 0x08;
  return m;
}

bool disp_init(void) {
  if (!s_pair && !s_half) {
    s_pair = (uint16_t *)heap_caps_malloc(DISP_PAIR_BYTES, DISP_BAND_CAPS);
    if (!s_pair) return false;
    s_band[0] = s_pair;
    s_band[1] = s_pair + DISP_BAND_PIXELS;
  }
  // Backlight off during init (Arduino core 3.x LEDC API).
  if (!ledcAttach(PIN_TFT_BL, BL_LEDC_FREQ, BL_LEDC_BITS)) return false;
  ledcWrite(PIN_TFT_BL, 0);

  gpio_config_t io = {};
  io.pin_bit_mask = BIT64(PIN_TFT_DC) | BIT64(PIN_TFT_RST);
  io.mode = GPIO_MODE_OUTPUT;
  if (gpio_config(&io) != ESP_OK) return false;
  gpio_set_level((gpio_num_t)PIN_TFT_RST, 1);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_SPI_MOSI;
  bus.miso_io_num = PIN_SPI_MISO;
  bus.sclk_io_num = PIN_SPI_SCK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = DISP_BAND_PIXELS * 2 + 8;
  if (spi_bus_initialize(TFT_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = SPI_TFT_HZ;
  dev.mode = 0;
  dev.spics_io_num = PIN_TFT_CS;
  dev.queue_size = 2 * TRANS_PER_BAND + 2;
  dev.pre_cb = tft_pre_cb;
  dev.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;  // write-only
  if (spi_bus_add_device(TFT_HOST, &dev, &s_tft) != ESP_OK) return false;

  // Hardware reset. Init path only (runs once in ui_task before the frame loop).
  gpio_set_level((gpio_num_t)PIN_TFT_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level((gpio_num_t)PIN_TFT_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(120));

  struct Op { uint8_t cmd; uint8_t len; uint8_t d[15]; };
  static const Op ops[] = {
    {0xEF, 3, {0x03, 0x80, 0x02}},
    {0xCF, 3, {0x00, 0xC1, 0x30}},
    {0xED, 4, {0x64, 0x03, 0x12, 0x81}},
    {0xE8, 3, {0x85, 0x00, 0x78}},
    {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02}},
    {0xF7, 1, {0x20}},
    {0xEA, 2, {0x00, 0x00}},
    {0xC0, 1, {0x23}},          // PWCTR1
    {0xC1, 1, {0x10}},          // PWCTR2
    {0xC5, 2, {0x3E, 0x28}},    // VMCTR1
    {0xC7, 1, {0x86}},          // VMCTR2
    {0x37, 1, {0x00}},          // VSCRSADD
    {0x3A, 1, {0x55}},          // 16-bit RGB565
    {0xB1, 2, {0x00, 0x18}},    // frame rate
    {0xB6, 3, {0x08, 0x82, 0x27}},
    {0xF2, 1, {0x00}},
    {0x26, 1, {0x01}},
    {0xE0, 15, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}},
    {0xE1, 15, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}},
  };
  if (!tft_cmd(0x01, nullptr, 0)) return false;  // SWRESET
  vTaskDelay(pdMS_TO_TICKS(150));
  for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
    if (!tft_cmd(ops[i].cmd, ops[i].d, ops[i].len)) return false;
  }
  uint8_t mad = madctl_for_rotation(TFT_ROTATION);
  if (!tft_cmd(0x36, &mad, 1)) return false;
  if (!tft_cmd(TFT_INVERT ? 0x21 : 0x20, nullptr, 0)) return false;
  if (!tft_cmd(0x11, nullptr, 0)) return false;  // SLPOUT
  vTaskDelay(pdMS_TO_TICKS(120));
  if (!tft_cmd(0x29, nullptr, 0)) return false;  // DISPON

  // Pre-build the constant parts of the band transactions.
  memset(s_trans, 0, sizeof(s_trans));
  for (int s = 0; s < 2; s++) {
    for (int i = 0; i < TRANS_PER_BAND; i++) {
      spi_transaction_t *t = &s_trans[s][i];
      bool is_cmd = (i % 2) == 0;
      t->user = (void *)(uint32_t)(is_cmd ? 0 : 1);
      if (i < 5) {
        t->flags = SPI_TRANS_USE_TXDATA;
        t->length = is_cmd ? 8 : 32;
      }
    }
    s_trans[s][0].tx_data[0] = 0x2A;  // CASET
    s_trans[s][2].tx_data[0] = 0x2B;  // RASET
    s_trans[s][4].tx_data[0] = 0x2C;  // RAMWR
  }
  disp_backlight(BL_DEFAULT);
  return true;
}

uint16_t *disp_band_buf(int slot) { return s_band[slot & 1]; }

uint32_t disp_bands_lend(void) {
  if (!s_pair) return 0;
  if (!disp_wait_idle(50)) return 0;  // never free a buffer the DMA may still read
  uint16_t *h = (uint16_t *)heap_caps_malloc(DISP_HALF_BYTES, DISP_BAND_CAPS);  // before the free: lands elsewhere
  if (!h) return 0;
  heap_caps_free(s_pair);
  s_pair = nullptr;
  s_half = h;
  s_band[0] = s_half;
  s_band[1] = nullptr;
  return DISP_PAIR_BYTES - DISP_HALF_BYTES;
}
bool disp_bands_restore(void) {
  if (s_pair) return true;
  if (!disp_wait_idle(50)) return false;
  uint16_t *p = (uint16_t *)heap_caps_malloc(DISP_PAIR_BYTES, DISP_BAND_CAPS);
  if (!p) return false;
  heap_caps_free(s_half);
  s_half = nullptr;
  s_pair = p;
  s_band[0] = s_pair;
  s_band[1] = s_pair + DISP_BAND_PIXELS;
  return true;
}
int disp_band_rows(void) { return s_pair ? UI_BAND_H : UI_BAND_H / 2; }
uint32_t disp_bands_lendable(void) { return s_pair ? DISP_PAIR_BYTES - DISP_HALF_BYTES : 0u; }

// Drain one completed transaction; marks its slot free when the slot's last
// transaction completes (spi_master returns results in queue order).
static bool drain_one(uint32_t timeout_ms) {
  spi_transaction_t *rt = nullptr;
  esp_err_t e = spi_device_get_trans_result(s_tft, &rt, pdMS_TO_TICKS(timeout_ms));
  if (e != ESP_OK) { s_timeouts++; return false; }
  s_pending--;
  for (int s = 0; s < 2; s++) {
    if (rt == &s_trans[s][TRANS_PER_BAND - 1]) s_busy[s] = false;
  }
  return true;
}

bool disp_band_wait(int slot, uint32_t timeout_ms) {
  slot &= 1;
  while (s_busy[slot]) {
    if (!drain_one(timeout_ms)) return false;
  }
  return true;
}

bool disp_band_send(int slot, int y0, int h) { return disp_win_send(slot, 0, y0, DISP_W, h); }

// v3.1.3: CASET/PASET window (x0..x0+w-1, y0..y0+h-1); the slot buffer holds w*h pixels.
bool disp_win_send(int slot, int x0, int y0, int w, int h) {
  slot &= 1;
  if (!s_band[slot] || s_busy[slot] || h <= 0 || w <= 0 || y0 < 0 || y0 + h > DISP_H || x0 < 0 || x0 + w > DISP_W) return false;
  if ((uint32_t)w * (uint32_t)h > (uint32_t)(s_pair ? DISP_BAND_PIXELS : DISP_BAND_PIXELS / 2)) return false;
  const int x1 = x0 + w - 1, y1 = y0 + h - 1;
  spi_transaction_t *t = s_trans[slot];
  t[1].tx_data[0] = (uint8_t)(x0 >> 8); t[1].tx_data[1] = (uint8_t)(x0 & 0xFF);
  t[1].tx_data[2] = (uint8_t)(x1 >> 8); t[1].tx_data[3] = (uint8_t)(x1 & 0xFF);
  t[3].tx_data[0] = (uint8_t)(y0 >> 8); t[3].tx_data[1] = (uint8_t)(y0 & 0xFF);
  t[3].tx_data[2] = (uint8_t)(y1 >> 8); t[3].tx_data[3] = (uint8_t)(y1 & 0xFF);
  t[5].tx_buffer = s_band[slot];
  t[5].length = (size_t)w * (size_t)h * 16u;
  s_busy[slot] = true;
  for (int i = 0; i < TRANS_PER_BAND; i++) {
    // Queue has room for both slots, so a short timeout is only a safety net.
    if (spi_device_queue_trans(s_tft, &t[i], pdMS_TO_TICKS(20)) != ESP_OK) {
      s_errors++;
      // The slot's last transaction was never queued, so drain everything
      // that is in flight and then release the slot explicitly.
      while (s_pending > 0 && drain_one(50)) {}
      s_busy[0] = s_busy[1] = false;
      return false;
    }
    s_pending++;
  }
  return true;
}

// v3.1.3: non-blocking - collect finished transactions; true if nothing is in flight.
bool disp_poll_idle(void) {
  while (s_pending > 0) {
    spi_transaction_t *rt = nullptr;
    if (spi_device_get_trans_result(s_tft, &rt, 0) != ESP_OK) return false;
    s_pending--;
    for (int s = 0; s < 2; s++)
      if (rt == &s_trans[s][TRANS_PER_BAND - 1]) s_busy[s] = false;
  }
  return true;
}

bool disp_wait_idle(uint32_t timeout_ms) {
  while (s_pending > 0) {
    if (!drain_one(timeout_ms)) return false;
  }
  return true;
}

void disp_backlight(uint8_t level) { ledcWrite(PIN_TFT_BL, level); }
uint32_t disp_spi_timeouts(void) { return s_timeouts; }
uint32_t disp_spi_errors(void) { return s_errors; }
