// AURA_v3.ino - AURA v3.1.6 firmware entry point (full product UI).
// Board: classic ESP32-WROOM-32 DevKit V1 (no PSRAM, 4 MB), arduino-esp32 3.x,
// FQBN esp32:esp32:esp32:PartitionScheme=min_spiffs.
//
// Tasks:  ui  (core 1, prio 5)  display + touch + encoder events, sole SPI owner
//         net (core 0, prio 3)  wifi_mgr, SNTP, data jobs (one TLS at a time), setup portal
//         ble (core 0, prio 4)  NimBLE HID 'PC keys' (on demand, 2-min no-connect stop)
//         ble also polls the serial CLI, battery and health log (v3.1.3); the Arduino
//         loopTask runs setup() and then deletes itself, returning its stack to the heap
#include "config.h"
#include "version.h"
#include "aura_log.h"
#include "data_store.h"
#include "encoder.h"
#include "battery.h"
#include "ui_task.h"
#include "net_task.h"
#include "ble_hid.h"
#include "health_log.h"
#include "setup_cli.h"
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_flash.h>
#include <esp_mac.h>

// Arduino-ESP32: shrink loopTask (default 8192; p1 HWM showed ~2.2 KB used).
SET_LOOP_TASK_STACK_SIZE(LOOP_TASK_STACK);

static const char *reset_reason_str(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_EXT: return "EXT";
    default: return "OTHER";
  }
}

void setup() {
  Serial.setTxBufferSize(2048);  // must precede begin(); keeps log writes from blocking the UI
  Serial.begin(115200);
  aura_log("%s", "");
  aura_log("[boot] %s %s (built " __DATE__ " " __TIME__ ")", AURA_NAME, AURA_VERSION);
  aura_log("[boot] reset=%s idf=%s cpu=%luMHz heap=%lu", reset_reason_str(esp_reset_reason()), esp_get_idf_version(),
           (unsigned long)getCpuFrequencyMhz(), (unsigned long)ESP.getFreeHeap());

  esp_chip_info_t ci;
  esp_chip_info(&ci);
  uint32_t flash_id = 0, flash_size = 0;
  const bool fid_ok = esp_flash_read_id(nullptr, &flash_id) == ESP_OK;
  const bool fsz_ok = esp_flash_get_size(nullptr, &flash_size) == ESP_OK;
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  aura_log("[boot] chip=%s rev=v%u.%u cores=%u features=0x%lx flash_id=%s%06lX flash=%luKB mac=%02X:%02X:%02X:%02X:%02X:%02X",
           ci.model == CHIP_ESP32 ? "ESP32" : "other", (unsigned)(ci.revision / 100), (unsigned)(ci.revision % 100),
           (unsigned)ci.cores, (unsigned long)ci.features, fid_ok ? "0x" : "ERR:", (unsigned long)flash_id,
           fsz_ok ? (unsigned long)(flash_size / 1024u) : 0ul, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  aura_log("[boot] pins spi sck=%d miso=%d mosi=%d tft cs=%d dc=%d rst=%d @%luHz bl=%d pwm@%luHz touch cs=%d irq=%d @%luHz",
           PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST, (unsigned long)SPI_TFT_HZ,
           PIN_TFT_BL, (unsigned long)BL_LEDC_FREQ, PIN_TOUCH_CS, PIN_TOUCH_IRQ, (unsigned long)TOUCH_SPI_HZ);
  aura_log("[boot] pins enc clk=%d dt=%d sw=%d batt=%d (div %.2f ASSUMED) spare=16,17 i2s_reserved=14,27,35,26",
           PIN_ENC_CLK, PIN_ENC_DT, PIN_ENC_SW, PIN_BATT_ADC, (double)BATT_DIVIDER);

  bool ok = health_init();
  if (!ok) aura_log("[boot] failed-alloc hook NOT registered");
  ok = ds_init() && ok;
  ok = encoder_init() && ok;
  ok = battery_init() && ok;
  ok = ui_task_start() && ok;
  ok = ble_task_start() && ok;
  ok = net_task_start() && ok;
  aura_log("[boot] tasks %s heap=%lu largest=%lu (stacks ui=%u net=%u ble=%u loop=%u)", ok ? "started" : "START FAILED",
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)UI_TASK_STACK, (unsigned)NET_TASK_STACK, (unsigned)BLE_TASK_STACK, (unsigned)LOOP_TASK_STACK);
  cli_print_help();
  aura_log("[boot] loopTask deleted (stack %u B back to the heap; CLI/battery/health run in the ble task)",
           (unsigned)LOOP_TASK_STACK);
  vTaskDelete(nullptr);  // v3.1.3: never returns
}

void loop() { vTaskDelay(portMAX_DELAY); }  // unreachable: loopTask is deleted at the end of setup()
