// ble_hid.cpp - NimBLE-Arduino 2.x HID device. ble_task (core 0) is the only
// caller of NimBLE APIs after init; other tasks talk to it through hidQ.
#include "ble_hid.h"
#include "battery.h"
#include "health_log.h"
#include "setup_cli.h"
#include "config.h"
#include "aura_log.h"
#include "data_store.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <freertos/queue.h>
#include <esp_heap_caps.h>

static TaskHandle_t s_task = nullptr;
static StaticQueue_t s_qbuf;
static uint8_t s_qstore[HIDQ_LEN * sizeof(HidItem)];
static QueueHandle_t s_hidq = nullptr;

static NimBLEServer *s_server = nullptr;
static NimBLEHIDDevice *s_hid = nullptr;
static NimBLECharacteristic *s_kb_in = nullptr;
static NimBLECharacteristic *s_cc_in = nullptr;
static volatile uint8_t s_conn = 0;
static volatile bool s_conn_changed = false;
static uint32_t s_sent = 0, s_dropped = 0;
static volatile bool s_enabled = false;  // Settings > PC keys (set by the UI from NVS at boot)
static volatile bool s_kick = false;     // start / re-arm request
static volatile bool s_force_off = false;
static volatile bool s_running = false;  // actual state (ble_task only writes)
static uint32_t s_deadline = 0;          // millis(): stop if still no host
static uint32_t s_base_free = 0, s_base_largest = 0;  // pre-start baseline for the leak check

#define BLE_HEAP_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

static const uint8_t k_report_map[] = {
  // ---- Report ID 1: boot-style keyboard ----
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,  // modifiers
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                                                              // reserved
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,                          // LEDs out
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,                                                              // LED pad
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,  // 6 keys
  0xC0,
  // ---- Report ID 2: consumer control, one 16-bit usage ----
  0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,
  0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
  0xC0,
};

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *srv, NimBLEConnInfo &) override {
    s_conn = srv->getConnectedCount();
    s_conn_changed = true;
  }
  void onDisconnect(NimBLEServer *srv, NimBLEConnInfo &, int) override {
    s_conn = srv->getConnectedCount();
    s_conn_changed = true;
  }
  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    // Runs in the NimBLE host task: keep it tiny, no blocking.
    (void)info;
    s_conn_changed = true;
  }
};
static ServerCb s_cb;

TaskHandle_t ble_task_handle(void) { return s_task; }

bool hid_send_consumer(uint16_t usage) { return hid_send_consumer_n(usage, 1); }

bool hid_send_consumer_n(uint16_t usage, uint8_t count) {
  if (!s_hidq || count == 0) return false;
  if (count > HID_MAX_REPEAT) count = HID_MAX_REPEAT;
  HidItem it = {HID_KIND_CONSUMER, 0, 0, count, usage};
  if (xQueueSend(s_hidq, &it, 0) != pdTRUE) { __atomic_fetch_add(&s_dropped, 1u, __ATOMIC_RELAXED); return false; }
  return true;
}

bool hid_send_key(uint8_t mods, uint8_t key) {
  if (!s_hidq) return false;
  HidItem it = {HID_KIND_KEY, mods, key, 1, 0};
  if (xQueueSend(s_hidq, &it, 0) != pdTRUE) { __atomic_fetch_add(&s_dropped, 1u, __ATOMIC_RELAXED); return false; }
  return true;
}

static bool ble_init(void) {
  // v3.1.3 note: NimBLEDevice::setScanDuplicateCacheSize() is compiled out with the observer role
  // disabled, so the controller duplicate list stays at NimBLE-Arduino's hard-coded 100 (locked).
#ifdef AURA_QEMU_NORADIO
  return false;  // QEMU test build only: no PHY in the emulator
#endif
  if (!NimBLEDevice::init(BLE_DEVICE_NAME)) return false;
  NimBLEDevice::setSecurityAuth(true, false, true);              // bond, no MITM, LE Secure Connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);    // Just Works
  s_server = NimBLEDevice::createServer();
  if (!s_server) return false;
  s_server->setCallbacks(&s_cb, false);
  s_server->advertiseOnDisconnect(true);
  // Allocated per BLE session (start), freed on stop. Not in the steady-state path.
  s_hid = new NimBLEHIDDevice(s_server);
  s_hid->setManufacturer("IMDEVIL");
  s_hid->setPnp(0x02, 0x303A, 0x4155, 0x0300);  // USB-IF VID source; Espressif VID, placeholder PID
  s_hid->setHidInfo(0x00, 0x01);
  s_hid->setReportMap((uint8_t *)k_report_map, sizeof(k_report_map));
  s_kb_in = s_hid->getInputReport(1);
  (void)s_hid->getOutputReport(1);              // keyboard LED output report
  s_cc_in = s_hid->getInputReport(2);
  if (!s_kb_in || !s_cc_in) return false;
  s_hid->setBatteryLevel(100);
  if (!s_server->start()) return false;
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(s_hid->getHidService()->getUUID());
  adv->setName(BLE_DEVICE_NAME);
  return adv->start();
}

static void send_item(const HidItem &it) {
  if (s_conn == 0) { __atomic_fetch_add(&s_dropped, 1u, __ATOMIC_RELAXED); return; }
  if (it.kind == HID_KIND_CONSUMER) {
    uint8_t press[2] = {(uint8_t)(it.usage & 0xFF), (uint8_t)(it.usage >> 8)};
    const uint8_t rel[2] = {0, 0};
    const uint8_t n = (it.count == 0 || it.count > HID_MAX_REPEAT) ? 1 : it.count;
    for (uint8_t i = 0; i < n; i++) {
      s_cc_in->setValue(press, sizeof(press));
      bool ok = s_cc_in->notify();
      vTaskDelay(pdMS_TO_TICKS(8));
      s_cc_in->setValue(rel, sizeof(rel));
      ok = s_cc_in->notify() && ok;
      vTaskDelay(pdMS_TO_TICKS(8));
      if (ok) s_sent++; else __atomic_fetch_add(&s_dropped, 1u, __ATOMIC_RELAXED);
    }
  } else if (it.kind == HID_KIND_KEY) {
    uint8_t rep[8] = {it.modifiers, 0, it.keycode, 0, 0, 0, 0, 0};
    const uint8_t rel[8] = {0};
    s_kb_in->setValue(rep, sizeof(rep));
    bool ok = s_kb_in->notify();
    vTaskDelay(pdMS_TO_TICKS(8));
    s_kb_in->setValue(rel, sizeof(rel));
    ok = s_kb_in->notify() && ok;
    if (ok) s_sent++; else __atomic_fetch_add(&s_dropped, 1u, __ATOMIC_RELAXED);
  }
}

void ble_set_enabled(bool on) { s_enabled = on; }
bool ble_enabled(void) { return s_enabled; }
void ble_kick(void) { s_kick = true; }
void ble_request(bool on) {
  if (on) s_kick = true;
  else s_force_off = true;
}
bool ble_is_running(void) { return s_running; }

// Full teardown: disconnect, stop the host + controller (NimBLEDevice::deinit
// -> nimble_port_deinit -> esp_bt_controller_deinit) and delete every object,
// so their heap returns. The controller's static DRAM reservation stays (it is
// NOT mem_released), which is what allows a later restart without reboot.
static void ble_stop(void) {
  if (s_server) {
    for (uint8_t i = s_server->getConnectedCount(); i > 0; i--) {
      s_server->disconnect(s_server->getPeerInfo(i - 1).getConnHandle());
    }
    if (s_conn) vTaskDelay(pdMS_TO_TICKS(200));  // let the LL terminate before stopping the host
    NimBLEDevice::stopAdvertising();
  }
  NimBLEDevice::deinit(true);  // clearAll: deletes server, services, advertising objects
  delete s_hid;                // holds only pointers into the (now deleted) server
  s_hid = nullptr;
  s_server = nullptr;
  s_kb_in = s_cc_in = nullptr;
  s_conn = 0;
  xQueueReset(s_hidq);
  s_running = false;
}

static void publish(bool started) {
  BleData b = {};
  b.enabled = s_enabled;
  b.started = started;
  if (started && s_conn == 0) {
    const int32_t left = (int32_t)(s_deadline - millis());
    b.adv_left_ms = left > 0 ? (uint32_t)left : 0;
  }
  b.connected = s_conn;
  // Only touch NimBLE objects while running (getAdvertising() would allocate).
  b.advertising = started && s_running && NimBLEDevice::getAdvertising()->isAdvertising();
  b.hid_sent = s_sent;
  b.hid_dropped = s_dropped;
  ds_set_ble(&b);
}

static void ble_task(void *) {
  aura_log("[ble] policy: PC keys pref, advertise %lus after boot / Macro / Settings, stays up while connected",
           (unsigned long)(BLE_NOCONN_MS / 1000u));
  uint32_t last_batt = 0;
  for (;;) {
    // ---- PC-keys policy (all NimBLE calls stay in this task)
    const uint32_t tnow = millis();
    const char *stop_why = nullptr;
    if (s_running) {
      if (!s_enabled) stop_why = "pc keys off";
      else if (s_force_off) stop_why = "cli";
      else if (s_conn == 0 && (int32_t)(tnow - s_deadline) >= 0) stop_why = "no host in 2 min";
    }
    s_force_off = false;
    if (stop_why) {
      const uint32_t f0 = heap_caps_get_free_size(BLE_HEAP_CAPS), l0 = heap_caps_get_largest_free_block(BLE_HEAP_CAPS);
      ble_stop();
      vTaskDelay(pdMS_TO_TICKS(20));  // let the host task free its last buffers
      const uint32_t f1 = heap_caps_get_free_size(BLE_HEAP_CAPS), l1 = heap_caps_get_largest_free_block(BLE_HEAP_CAPS);
      aura_log("[ble] stop released=%ld net_leak=%ld largest %lu->%lu (base %lu, net %ld) free=%lu reason=%s",
               (long)f1 - (long)f0, (long)s_base_free - (long)f1, (unsigned long)l0, (unsigned long)l1,
               (unsigned long)s_base_largest, (long)s_base_largest - (long)l1, (unsigned long)f1, stop_why);
    }
    if (s_kick) {
      s_kick = false;
      if (s_enabled) {
        if (!s_running) {
          s_base_free = heap_caps_get_free_size(BLE_HEAP_CAPS);
          s_base_largest = heap_caps_get_largest_free_block(BLE_HEAP_CAPS);
          const uint32_t t0 = millis();
          const bool okk = ble_init();
          s_running = okk;
          const uint32_t f1 = heap_caps_get_free_size(BLE_HEAP_CAPS), l1 = heap_caps_get_largest_free_block(BLE_HEAP_CAPS);
          aura_log("[ble] start %s cost=%ld largest_cost=%ld (free %lu->%lu largest %lu->%lu) in %lums as '%s'",
                   okk ? "ok" : "FAILED", (long)s_base_free - (long)f1, (long)s_base_largest - (long)l1,
                   (unsigned long)s_base_free, (unsigned long)f1, (unsigned long)s_base_largest, (unsigned long)l1,
                   (unsigned long)(millis() - t0), BLE_DEVICE_NAME);
          if (!okk) {
            ble_stop();
            const uint32_t f2 = heap_caps_get_free_size(BLE_HEAP_CAPS);
            aura_log("[ble] stop released=%ld net_leak=%ld reason=init failed", (long)f2 - (long)f1, (long)s_base_free - (long)f2);
          }
        }
        s_deadline = millis() + BLE_NOCONN_MS;
      }
    }
    const bool ok = s_running;

    HidItem it;
    if (xQueueReceive(s_hidq, &it, pdMS_TO_TICKS(BLE_SVC_POLL_MS)) == pdTRUE) {
      if (ok) send_item(it);
      else __atomic_fetch_add(&s_dropped, 1u, __ATOMIC_RELAXED);
    }
    if (s_conn_changed) {
      s_conn_changed = false;
      if (s_conn == 0) s_deadline = millis() + BLE_NOCONN_MS;  // host left: advertise for another window
      aura_log("[ble] connected=%u", (unsigned)s_conn);
    }
    const uint32_t now = millis();
    if (ok && (uint32_t)(now - last_batt) > 60000u) {
      last_batt = now;
      const int8_t pct = battery_last_pct();  // v3.1.3: no 1.7 KB AuraData copy
      if (pct >= 0) s_hid->setBatteryLevel((uint8_t)pct, s_conn > 0);
    }
    publish(ok);
    // v3.1.3: the Arduino loopTask is deleted after setup(); its housekeeping runs here.
    cli_poll();
    battery_poll(now);
    health_poll(now);
  }
}

bool ble_task_start(void) {
  s_hidq = xQueueCreateStatic(HIDQ_LEN, sizeof(HidItem), s_qstore, &s_qbuf);
  if (!s_hidq) return false;
  // Stack + TCB come from the heap ONCE at boot (not steady state). Static
  // stacks would overflow dram0_0_seg (the static-DRAM region, ~124 KB with BT).
  if (xTaskCreatePinnedToCore(ble_task, "ble", BLE_TASK_STACK, nullptr, BLE_TASK_PRIO, &s_task, BLE_TASK_CORE) != pdPASS) s_task = nullptr;
  return s_task != nullptr;
}
