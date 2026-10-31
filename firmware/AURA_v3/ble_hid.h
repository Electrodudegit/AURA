// ble_hid.h - NimBLE HID peripheral (ID1 keyboard, ID2 consumer control), Just Works.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

enum HidKind : uint8_t { HID_KIND_CONSUMER = 1, HID_KIND_KEY = 2 };

// Fixed-size hidQ item (no heap).
struct HidItem {
  uint8_t kind;
  uint8_t modifiers;  // keyboard only
  uint8_t keycode;    // keyboard only (HID usage page 0x07)
  uint8_t count;      // repeat count (coalesced detents), 1..HID_MAX_REPEAT
  uint16_t usage;     // consumer only (usage page 0x0C)
};

#define HID_CC_VOL_UP    0x00E9
#define HID_CC_VOL_DOWN  0x00EA
#define HID_CC_MUTE      0x00E2
#define HID_CC_PLAYPAUSE 0x00CD
#define HID_CC_NEXT      0x00B5
#define HID_CC_PREV      0x00B6

bool ble_task_start(void);
// PC-keys policy (v3.1):
//  - ble_set_enabled(false): BLE never starts; a running stack is fully deinit'ed.
//  - ble_kick(): (enabled only) start advertising if stopped and (re)arm the
//    2-min no-connect window. Called at boot, on Macro app entry and from Settings.
//  - No host within BLE_NOCONN_MS -> full deinit (heap released).
//  - While a host is connected the stack stays up on every screen; nothing else
//    (TLS fetches included) ever stops a connected link.
// All calls are any-task; applied by ble_task within ~200 ms.
void ble_set_enabled(bool on);
bool ble_enabled(void);
void ble_kick(void);
void ble_request(bool on);  // CLI: on = kick, off = stop now (until the next kick)
bool ble_is_running(void);
TaskHandle_t ble_task_handle(void);
// Non-blocking enqueue from any task. Returns false if the queue is full.
bool hid_send_consumer(uint16_t usage);
// One queue item that the BLE task expands into `count` press/release pairs.
bool hid_send_consumer_n(uint16_t usage, uint8_t count);
bool hid_send_key(uint8_t modifiers, uint8_t keycode);
