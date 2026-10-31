// wifi_mgr.cpp - Arduino WiFi (ESP-IDF esp_wifi underneath).
// Modem sleep stays ON: required when Wi-Fi and BLE share the radio.
// RTOS persist: NVS namespace "wifi" is source of truth; secrets.h is boot fallback
// only. On first STA UP from defaults, credentials are promoted + verified in NVS
// so reboot / empty secrets.h / app-only flash (NVS @ 0x9000 kept) still reconnect.
#include "wifi_mgr.h"
#include "config.h"
#include "aura_log.h"
#include "secrets.h"  // WIFI_SSID_DEFAULT / WIFI_PASS_DEFAULT (NVS overrides)
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>

static char s_ssid[WIFI_SSID_MAX + 1];
static char s_pass[WIFI_PASS_MAX + 1];
static bool s_from_default = false;  // creds came from secrets.h (never logged, length only)
static volatile bool s_reload = true;
static WifiState s_state = WIFI_ST_NO_CREDS;
static uint32_t s_t_state = 0;
static uint32_t s_backoff = NET_BACKOFF_MIN_MS;
static uint32_t s_up_since = 0;      // millis() when WIFI_ST_UP began (0 = not up)
static uint32_t s_unlink_since = 0;  // first !linked sample while UP (0 = linked)
static uint8_t s_fast_left = 0;      // WiFi.reconnect() tries left after a stable drop
// arduino-esp32 3.3.12 has no WiFi.disconnectReason(). Capture the IDF reason
// from ARDUINO_EVENT_WIFI_STA_DISCONNECTED (wifi_sta_disconnected.reason).
static volatile int s_sta_reason = -1;
// 3.3.12: the info-bearing callback is void (*)(arduino_event_t *), not WiFi.disconnectReason().
static void on_sta_disc(arduino_event_t *ev) {
  if (!ev || ev->event_id != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
  s_sta_reason = (int)ev->event_info.wifi_sta_disconnected.reason;
}

static void wipe_v(void *p, size_t n) {
  volatile uint8_t *v = (volatile uint8_t *)p;
  while (n--) *v++ = 0;
}

// Write ssid/pass to NVS "wifi" and read them back. Does NOT set s_reload.
// Passwords are never logged — only lengths / ssid for NVS source.
static bool creds_write_nvs(const char *ssid, const char *pass) {
  if (!ssid || !pass) return false;
  const size_t ls = strlen(ssid), lp = strlen(pass);
  if (!aura_cred_usable(ssid) || ls > WIFI_SSID_MAX || lp > WIFI_PASS_MAX || (lp > 0 && lp < 8)) return false;
  if (lp && !aura_cred_usable(pass)) return false;
  Preferences p;
  if (!p.begin("wifi", false)) {
    aura_log("[wifi] nvs begin(rw) FAILED");
    return false;
  }
  bool ok = p.putString("ssid", ssid) == ls;
  if (ok) {
    if (lp == 0) {
      p.remove("pass");  // open network
    } else {
      ok = p.putString("pass", pass) == lp;
    }
  }
  p.end();
  if (!ok) {
    aura_log("[wifi] nvs put FAILED ssid_len=%u pass_len=%u", (unsigned)ls, (unsigned)lp);
    return false;
  }
  // Read-back verify (survives power loss only if flash commit succeeded)
  char rs[WIFI_SSID_MAX + 1];
  char rp[WIFI_PASS_MAX + 1];
  wipe_v(rs, sizeof(rs));
  wipe_v(rp, sizeof(rp));
  Preferences v;
  if (!v.begin("wifi", true)) {
    aura_log("[wifi] nvs verify begin FAILED");
    return false;
  }
  v.getString("ssid", rs, sizeof(rs));
  v.getString("pass", rp, sizeof(rp));
  v.end();
  ok = (strncmp(rs, ssid, sizeof(rs)) == 0) && (strncmp(rp, pass, sizeof(rp)) == 0);
  wipe_v(rs, sizeof(rs));
  wipe_v(rp, sizeof(rp));
  if (!ok) aura_log("[wifi] nvs verify MISMATCH ssid_len=%u pass_len=%u", (unsigned)ls, (unsigned)lp);
  return ok;
}

static bool load_creds(void) {
  wipe_v(s_ssid, sizeof(s_ssid));
  wipe_v(s_pass, sizeof(s_pass));
  s_from_default = false;
  Preferences p;
  if (p.begin("wifi", true)) {
    p.getString("ssid", s_ssid, sizeof(s_ssid));
    p.getString("pass", s_pass, sizeof(s_pass));
    p.end();
  } else {
    aura_log("[wifi] nvs begin(ro) FAILED — falling back to built-in default if any");
  }
  if (!aura_cred_usable(s_ssid)) {  // placeholder or empty is not a saved network
    wipe_v(s_ssid, sizeof(s_ssid));
    wipe_v(s_pass, sizeof(s_pass));
  }
  if (s_ssid[0] == '\0' && aura_cred_usable(WIFI_SSID_DEFAULT)) {  // NVS wins; placeholder is not a network
    strncpy(s_ssid, WIFI_SSID_DEFAULT, WIFI_SSID_MAX);
    s_ssid[WIFI_SSID_MAX] = '\0';
    if (aura_cred_usable(WIFI_PASS_DEFAULT)) {
      strncpy(s_pass, WIFI_PASS_DEFAULT, WIFI_PASS_MAX);
      s_pass[WIFI_PASS_MAX] = '\0';
    }
    s_from_default = true;
  }
  return s_ssid[0] != '\0';
}

// When STA comes UP on secrets.h defaults only, promote into NVS once so the
// next reboot still connects after secrets.h is emptied or after app-only flash
// (NVS @ 0x9000 preserved). Does not set s_reload (already associated).
static void promote_default_to_nvs(void) {
  if (!s_from_default || s_ssid[0] == '\0') return;
  if (!creds_write_nvs(s_ssid, s_pass)) {
    aura_log("[wifi] nvs promote FAILED ssid_len=%u pass_len=%u (will retry next UP)",
             (unsigned)strlen(s_ssid), (unsigned)strlen(s_pass));
    return;
  }
  s_from_default = false;
  aura_log("[wifi] nvs promote ok ssid_len=%u pass_len=%u", (unsigned)strlen(s_ssid), (unsigned)strlen(s_pass));
}

// Persist whatever is currently in RAM (NVS or post-default). Used by portal when
// the user leaves Wi-Fi fields blank but other settings are saved, and by promote.
bool wifi_creds_materialize(void) {
  if (s_ssid[0] == '\0') return false;
  if (!creds_write_nvs(s_ssid, s_pass)) return false;
  s_from_default = false;
  aura_log("[wifi] nvs materialize ok ssid_len=%u pass_len=%u", (unsigned)strlen(s_ssid), (unsigned)strlen(s_pass));
  return true;
}

bool wifi_creds_save(const char *ssid, const char *pass) {
  if (!creds_write_nvs(ssid, pass)) return false;
  // Mirror into RAM immediately so portal/CLI see NVS as source before reload.
  wipe_v(s_ssid, sizeof(s_ssid));
  wipe_v(s_pass, sizeof(s_pass));
  strncpy(s_ssid, ssid, WIFI_SSID_MAX);
  s_ssid[WIFI_SSID_MAX] = '\0';
  strncpy(s_pass, pass, WIFI_PASS_MAX);
  s_pass[WIFI_PASS_MAX] = '\0';
  s_from_default = false;
  s_reload = true;
  return true;
}

bool wifi_creds_clear(void) {
  Preferences p;
  if (!p.begin("wifi", false)) {
    aura_log("[wifi] nvs clear begin FAILED");
    return false;
  }
  const bool ok = p.clear();
  p.end();
  s_reload = true;
  if (!ok) aura_log("[wifi] nvs clear FAILED");
  return ok;
}

void wifi_default_lens(size_t *ssid_len, size_t *pass_len) {
  *ssid_len = aura_cred_usable(WIFI_SSID_DEFAULT) ? strlen(WIFI_SSID_DEFAULT) : 0;
  *pass_len = aura_cred_usable(WIFI_PASS_DEFAULT) ? strlen(WIFI_PASS_DEFAULT) : 0;
}
bool wifi_creds_from_default(void) { return s_from_default; }

bool wifi_creds_peek(char *ssid_out, size_t cap, size_t *pass_len) {
  char pw[WIFI_PASS_MAX + 1];
  Preferences p;
  if (!p.begin("wifi", true)) { if (cap) ssid_out[0] = 0; *pass_len = 0; return false; }
  p.getString("ssid", ssid_out, cap);
  memset(pw, 0, sizeof(pw));
  p.getString("pass", pw, sizeof(pw));
  p.end();
  *pass_len = strlen(pw);
  memset(pw, 0, sizeof(pw));
  return ssid_out[0] != '\0';
}

static bool s_driver_on = false;
#define WIFI_HEAP_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

// v3.1.3: bring the driver up with a trimmed esp_wifi_init config BEFORE Arduino's WiFi.mode().
// Arduino 3.3.12 (wifiLowLevelInit) calls esp_wifi_init() with srx4/drx32/dtx32/cache4 and
// AMPDU on; the prebuilt esp_wifi_init() returns early once the driver is initialised, so
// initialising it here first makes Arduino's call a no-op and our numbers stick.
// QEMU probe (tlsbench/wifimem): esp_wifi_init cost 28608 B (Arduino) -> 24108 B (this cfg).
bool wifi_preinit(void) {
  wifi_mode_t m;
  if (esp_wifi_get_mode(&m) != ESP_ERR_WIFI_NOT_INIT) return true;  // already up (ours or Arduino's)
  Network.begin();  // esp_netif + default event loop, exactly as Arduino would
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.static_tx_buf_num = 0;   // Arduino default: dynamic TX
  cfg.tx_buf_type = 1;
  cfg.dynamic_tx_buf_num = WIFI_DYN_TX_BUF;
  cfg.cache_tx_buf_num = WIFI_CACHE_TX_BUF;
  cfg.static_rx_buf_num = WIFI_STATIC_RX_BUF;
  cfg.dynamic_rx_buf_num = WIFI_DYN_RX_BUF;
  cfg.ampdu_rx_enable = 0;     // no block-ack reorder buffers (lwIP TCP_WND is 4 x MSS anyway)
  cfg.ampdu_tx_enable = 0;
  cfg.nvs_enable = 0;          // Arduino uses WIFI_STORAGE_RAM; credentials live in our own NVS namespace
  cfg.mgmt_sbuf_num = WIFI_MGMT_SBUF;
  cfg.rx_mgmt_buf_num = WIFI_RX_MGMT_BUF;
  const uint32_t f0 = heap_caps_get_free_size(WIFI_HEAP_CAPS);
  const esp_err_t e = esp_wifi_init(&cfg);
  aura_log("[wifi] preinit srx=%d drx=%d dtx=%d cache=%d ampdu=off nvs=off mgmt=%d rx_mgmt=%d err=0x%x cost=%ld",
           cfg.static_rx_buf_num, cfg.dynamic_rx_buf_num, cfg.dynamic_tx_buf_num, cfg.cache_tx_buf_num, cfg.mgmt_sbuf_num,
           cfg.rx_mgmt_buf_num, (unsigned)e, (long)f0 - (long)heap_caps_get_free_size(WIFI_HEAP_CAPS));
  return e == ESP_OK;
}

// p2: the Wi-Fi driver (esp_wifi_init + start, tasks, static RX buffers) is
// only brought up when credentials exist, and torn down (esp_wifi_deinit via
// WiFi.mode(WIFI_OFF)) when they are cleared. p1 started it unconditionally.
static bool driver_on(void) {
  if (s_driver_on) return true;
#ifdef AURA_QEMU_NORADIO
  return false;  // QEMU test build only: no PHY in the emulator
#endif
  const uint32_t f0 = heap_caps_get_free_size(WIFI_HEAP_CAPS);
  wifi_preinit();  // v3.1.3 trimmed buffers (falls back to Arduino's config if it fails)
  if (!WiFi.mode(WIFI_STA)) return false;
  WiFi.setSleep(true);                    // WIFI_PS_MIN_MODEM; mandatory for Wi-Fi/BT coexistence
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);     // explicit; survives Arduino mode toggles
  WiFi.setAutoReconnect(false);           // we own reconnect + backoff
  WiFi.setHostname("aura-v3");
  s_driver_on = true;
  const uint32_t f1 = heap_caps_get_free_size(WIFI_HEAP_CAPS);
  aura_log("[wifi] driver on, heap %lu -> %lu (cost %ld) largest=%lu", (unsigned long)f0, (unsigned long)f1,
           (long)f0 - (long)f1, (unsigned long)heap_caps_get_largest_free_block(WIFI_HEAP_CAPS));
  return true;
}

static void driver_off(void) {
  if (!s_driver_on) return;
  const uint32_t f0 = heap_caps_get_free_size(WIFI_HEAP_CAPS);
  WiFi.mode(WIFI_OFF);  // Arduino 3.x: esp_wifi_stop + esp_wifi_deinit
  s_driver_on = false;
  s_up_since = 0;
  s_unlink_since = 0;
  s_fast_left = 0;
  aura_log("[wifi] driver off, heap %lu -> %lu", (unsigned long)f0,
           (unsigned long)heap_caps_get_free_size(WIFI_HEAP_CAPS));
}

bool wifi_mgr_init(void) {
  WiFi.persistent(false);  // creds live in our own NVS namespace, not the IDF one
  WiFi.onEvent(on_sta_disc, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  return true;             // driver starts lazily in wifi_mgr_poll()
}

WifiState wifi_mgr_state(void) { return s_state; }
bool wifi_mgr_has_creds(void) { return s_ssid[0] != '\0'; }
void wifi_ssid_copy(char *out, size_t cap) {
  if (!out || !cap) return;
  size_t n = strlen(s_ssid);
  if (n >= cap) n = cap - 1;
  memcpy(out, s_ssid, n);
  out[n] = 0;
}

// Setup portal: STA is parked while the SoftAP runs (the portal owns the radio
// mode); resume reloads the (possibly new) credentials and reconnects.
static bool s_suspended = false;
void wifi_mgr_suspend(void) {
  if (s_suspended) return;
  s_suspended = true;
  if (s_driver_on) WiFi.disconnect(false, false);
  s_driver_on = false;  // the portal switches the mode; driver_on() re-runs on resume
  s_up_since = 0;
  s_unlink_since = 0;
  s_fast_left = 0;
  s_state = WIFI_ST_BACKOFF;
}
void wifi_mgr_resume(void) {
  if (!s_suspended) return;
  s_suspended = false;
  s_reload = true;
}

static void set_state(WifiState st, uint32_t now) {
  s_state = st;
  s_t_state = now;
}

// Fast path: WiFi.reconnect() keeps the STA config. A full begin() disconnects
// first so a stale association cannot stick. Modem sleep stays WIFI_PS_MIN_MODEM.
static void start_associate(uint32_t now) {
  if (!driver_on()) {
    aura_log("[wifi] driver start FAILED, backoff");
    set_state(WIFI_ST_BACKOFF, now);
    return;
  }
  if (s_fast_left > 0) {
    s_fast_left--;
    aura_log("[wifi] fast reconnect (%u left)", (unsigned)s_fast_left);
    WiFi.reconnect();
  } else {
    WiFi.disconnect(false, false);
    WiFi.begin(s_ssid, s_pass[0] ? s_pass : nullptr);
  }
  set_state(WIFI_ST_CONNECTING, now);
}

WifiState wifi_mgr_poll(uint32_t now) {
  if (s_suspended) return s_state;
  if (s_reload) {
    s_reload = false;
    if (s_driver_on) WiFi.disconnect(false, false);
    s_up_since = 0;
    s_unlink_since = 0;
    s_fast_left = 0;
    if (load_creds()) {
      aura_log("[wifi] creds loaded src=%s ssid_len=%u pass_len=%u", s_from_default ? "default" : "nvs",
               (unsigned)strlen(s_ssid), (unsigned)strlen(s_pass));
      s_backoff = NET_BACKOFF_MIN_MS;
      start_associate(now);
    } else {
      aura_log("[wifi] no credentials (Settings > Wi-Fi setup, or serial: wifi set <ssid> <pass>); driver stays off");
      driver_off();
      set_state(WIFI_ST_NO_CREDS, now);
    }
  }
  const bool linked = WiFi.status() == WL_CONNECTED;
  switch (s_state) {
    case WIFI_ST_NO_CREDS:
      break;
    case WIFI_ST_CONNECTING:
      if (linked) {
        const IPAddress ip = WiFi.localIP();
        aura_log("[wifi] up ip=%u.%u.%u.%u rssi=%d", ip[0], ip[1], ip[2], ip[3], (int)WiFi.RSSI());
        s_backoff = NET_BACKOFF_MIN_MS;
        s_fast_left = 0;
        s_unlink_since = 0;
        s_up_since = now ? now : 1;
        set_state(WIFI_ST_UP, now);
        promote_default_to_nvs();  // secrets.h -> NVS so reboot keeps STA
      } else if ((uint32_t)(now - s_t_state) > NET_WIFI_CONNECT_TIMEOUT_MS) {
        WiFi.disconnect(false, false);
        s_fast_left = 0;
        aura_log("[wifi] connect timeout reason=%d, backoff %lums", (int)s_sta_reason,
                 (unsigned long)s_backoff);
        set_state(WIFI_ST_BACKOFF, now);
      }
      break;
    case WIFI_ST_UP:
      if (linked) {
        s_unlink_since = 0;
        if (s_up_since && (uint32_t)(now - s_up_since) >= WIFI_STABLE_MS) s_backoff = NET_BACKOFF_MIN_MS;
      } else {
        if (s_unlink_since == 0) s_unlink_since = now ? now : 1;
        if ((uint32_t)(now - s_unlink_since) >= WIFI_LINK_LOSS_DEBOUNCE_MS) {
          const bool was_stable = s_up_since != 0 && (uint32_t)(s_unlink_since - s_up_since) >= WIFI_STABLE_MS;
          if (was_stable) {
            s_backoff = NET_BACKOFF_MIN_MS;
            s_fast_left = WIFI_FAST_RECONNECT_TRIES;
          }
          WiFi.disconnect(false, false);
          aura_log("[wifi] link lost debounce=%ums%s reason=%d, backoff %lums",
                   (unsigned)WIFI_LINK_LOSS_DEBOUNCE_MS, was_stable ? " after-stable" : "",
                   (int)s_sta_reason, (unsigned long)s_backoff);
          s_unlink_since = 0;
          s_up_since = 0;
          set_state(WIFI_ST_BACKOFF, now);
        }
      }
      break;
    case WIFI_ST_BACKOFF:
      if ((uint32_t)(now - s_t_state) >= s_backoff) {
        s_backoff = (s_backoff * 2 > NET_BACKOFF_MAX_MS) ? NET_BACKOFF_MAX_MS : s_backoff * 2;
        start_associate(now);
      }
      break;
  }
  return s_state;
}
