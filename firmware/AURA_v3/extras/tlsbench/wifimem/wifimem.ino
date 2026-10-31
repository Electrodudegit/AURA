// wifimem.ino - QEMU probe: heap cost of esp_wifi_init() with the Arduino 3.3.12 config vs the
// AURA v3.1.3 trimmed config (no radio needed for init; start/connect is not emulated).
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#define CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
static void hp(const char *t) {
  Serial.printf("[wm] %-28s free=%u largest=%u\n", t, (unsigned)heap_caps_get_free_size(CAPS),
                (unsigned)heap_caps_get_largest_free_block(CAPS));
}
static uint32_t try_init(const char *name, const wifi_init_config_t &cfg) {
  const uint32_t f0 = heap_caps_get_free_size(CAPS);
  esp_err_t e = esp_wifi_init(&cfg);
  const uint32_t f1 = heap_caps_get_free_size(CAPS);
  Serial.printf("[wm] init %-10s err=0x%x cost=%u\n", name, (unsigned)e, (unsigned)(f0 - f1));
  esp_err_t m = esp_wifi_set_mode(WIFI_MODE_STA);
  const uint32_t f2 = heap_caps_get_free_size(CAPS);
  Serial.printf("[wm] mode %-10s err=0x%x cost=%u\n", name, (unsigned)m, (unsigned)(f1 - f2));
  esp_wifi_set_mode(WIFI_MODE_NULL);
  esp_wifi_deinit();
  hp("after deinit");
  return f0 - f2;
}
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[bench] start");
  hp("boot");
  Network.begin();
  hp("Network.begin");
  wifi_init_config_t a = WIFI_INIT_CONFIG_DEFAULT();
  a.static_tx_buf_num = 0; a.dynamic_tx_buf_num = 32; a.tx_buf_type = 1; a.cache_tx_buf_num = 4;
  a.static_rx_buf_num = 4; a.dynamic_rx_buf_num = 32;
  Serial.printf("[wm] arduino cfg srx=%d drx=%d dtx=%d cache=%d ampdu_rx=%d ampdu_tx=%d amsdu=%d nvs=%d mgmt_sbuf=%d rx_mgmt=%d ba_win=%d\n",
                a.static_rx_buf_num, a.dynamic_rx_buf_num, a.dynamic_tx_buf_num, a.cache_tx_buf_num, a.ampdu_rx_enable,
                a.ampdu_tx_enable, a.amsdu_tx_enable, a.nvs_enable, a.mgmt_sbuf_num, a.rx_mgmt_buf_num, a.rx_ba_win);
  try_init("arduino", a);
  wifi_init_config_t b = a;
  b.static_rx_buf_num = 3; b.dynamic_rx_buf_num = 8; b.dynamic_tx_buf_num = 12; b.cache_tx_buf_num = 2;
  b.ampdu_rx_enable = 0; b.ampdu_tx_enable = 0; b.nvs_enable = 0; b.mgmt_sbuf_num = 10; b.rx_mgmt_buf_num = 3;
  try_init("aura313", b);
  wifi_init_config_t c = b; c.static_rx_buf_num = 2;
  try_init("srx2", c);
  wifi_init_config_t d = a; d.ampdu_rx_enable = 0; d.ampdu_tx_enable = 0;
  try_init("noampdu", d);
  wifi_init_config_t e = a; e.nvs_enable = 0;
  try_init("nonvs", e);
  wifi_init_config_t f = a; f.mgmt_sbuf_num = 10;
  try_init("sbuf10", f);
  Serial.println("[bench] done");
}
void loop() { delay(1000); }
