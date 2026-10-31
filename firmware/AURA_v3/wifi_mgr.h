// wifi_mgr.h - STA connection manager (runs inside net_task, core 0).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "data_store.h"

#define WIFI_SSID_MAX 32
#define WIFI_PASS_MAX 64

bool wifi_mgr_init(void);
// Drive the state machine. Call every ~100 ms from net_task only.
WifiState wifi_mgr_poll(uint32_t now_ms);
WifiState wifi_mgr_state(void);
bool wifi_mgr_has_creds(void);
// Copy the STA SSID currently in RAM (NVS or built-in default). Never the password.
void wifi_ssid_copy(char *out, size_t cap);
// Setup portal (net_task only): park STA while the SoftAP runs, then reload + reconnect.
void wifi_mgr_suspend(void);
void wifi_mgr_resume(void);

// Credential storage (NVS namespace "wifi") — source of truth across reboot.
// secrets.h defaults are boot fallback only; wifi_mgr promotes them to NVS on
// first successful STA UP. Safe to call save/clear from the CLI task; net_task
// picks the change up on its next poll.
bool wifi_creds_save(const char *ssid, const char *pass);
// Write current RAM creds (including secrets.h defaults in use) into NVS.
bool wifi_creds_materialize(void);
bool wifi_creds_clear(void);
// Copies the stored SSID (never the password) and reports the password length.
// NVS only: returns false (ssid_out "") when only the built-in default exists.
bool wifi_creds_peek(char *ssid_out, size_t ssid_cap, size_t *pass_len);
// v3.1.2: lengths of the built-in default (secrets.h) and whether it is in use.
void wifi_default_lens(size_t *ssid_len, size_t *pass_len);
bool wifi_creds_from_default(void);
bool wifi_preinit(void);  // v3.1.3: esp_wifi_init with trimmed buffers before any WiFi.mode(non-OFF)
