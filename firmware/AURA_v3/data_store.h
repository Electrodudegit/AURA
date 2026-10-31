// data_store.h - shared state between tasks: fixed structs behind one mutex.
// No String / heap. Every accessor has a bounded wait and returns false on timeout.
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum WifiState : uint8_t { WIFI_ST_NO_CREDS = 0, WIFI_ST_CONNECTING, WIFI_ST_UP, WIFI_ST_BACKOFF };

struct NetData {
  uint8_t wifi_state;
  int8_t rssi;
  uint32_t ip;            // network byte order as returned by IPAddress
  bool temp_valid;
  int16_t temp_x10;       // deg C * 10
  uint32_t temp_ms;       // millis() of last good fetch
  uint16_t http_status;
  uint32_t fetch_ok;
  uint32_t fetch_fail;
  int32_t last_err;       // 0 ok, <0 local error code (see net_task.cpp)
  uint32_t tls_largest_min;  // smallest "largest free block" seen while a TLS session was open
  uint32_t tls_heap_min;
  uint32_t tls_skipped;      // TLS jobs skipped by the heap guard
  char ssid[33];             // STA name currently in use (screen only, never logged)
};

struct BleData {
  bool enabled;           // Settings > PC keys (Bluetooth)
  uint32_t adv_left_ms;   // advertising window left before the no-connect stop (0 = n/a)
  bool started;
  bool advertising;
  uint8_t connected;
  bool hid_mode;
  uint32_t hid_sent;
  uint32_t hid_dropped;
};

struct BattData {
  uint16_t adc_mv;        // at the pin
  uint16_t vbat_mv;       // after BATT_DIVIDER (assumption)
  int8_t pct;             // -1 = unknown / implausible
};

struct UiData {
  uint16_t fps_x10;
  uint32_t frames;
  uint32_t overruns;
  uint32_t spi_timeouts;
  uint32_t band_fail;       // band skipped: DMA wait timeout or queue failure
  uint32_t touch_bus_skip;  // touch tick skipped: display DMA did not drain
  uint32_t touch_spi_err;   // touch transaction failed
  uint32_t lat_logged;
  uint32_t lat_suppressed;  // [lat] samples not logged (rate limit)
  uint16_t lat_max_ms;      // max input->flush latency since boot
};

// ---- v3.1 app data (written by net_task, read by the UI each frame) --------
enum JobId : uint8_t { JOB_WX = 0, JOB_AIR, JOB_CRYPTO, JOB_NEWS, JOB_STOCKS, JOB_CRICKET, JOB_COUNT };
enum JobSt : uint8_t {
  JS_NEVER = 0,   // not attempted yet (no Wi-Fi / boot)
  JS_LOADING,     // TLS job in flight
  JS_OK,          // last attempt succeeded
  JS_RETRY,       // last attempt failed, retry scheduled (attempt counts up)
  JS_FAIL,        // retries exhausted, next try at the normal period
  JS_HEAP,        // skipped by the TLS heap gate
  JS_PAUSED,      // skipped: setup portal active (BLE never pauses a job)
};
struct JobInfo {
  uint8_t st;
  uint8_t attempt;     // 1..NET_JOB_RETRIES while retrying
  int16_t err;         // NetErr of the last failure
  uint16_t http;       // last HTTP status
  uint32_t ok_ms;      // millis() of the last success (0 = never)
  uint32_t next_ms;    // millis() of the next scheduled attempt
};
struct WxDay { uint8_t code; int16_t hi_x10, lo_x10; uint8_t pop; };
struct WxData {
  bool valid;
  int16_t temp_x10, feels_x10;
  uint8_t hum;          // %
  uint16_t wind_x10;    // km/h * 10
  uint8_t uv_x10;       // UV index * 10
  uint8_t code;         // WMO weather code
  bool is_day;
  uint8_t ndays;
  WxDay day[5];
  int16_t sunrise_min, sunset_min;  // local minutes after midnight (today), -1 unknown
  uint8_t nh;           // hourly points (next 12 h)
  uint8_t hour0;        // local hour of hourly[0]
  int16_t hour_x10[12];
};
struct AirData { bool valid; uint16_t aqi; uint16_t pm25_x10, pm10_x10; };
// v3.1.6 IoT p0p1: CoinGecko /coins/markets?sparkline=true (one TLS, streamed). spark[] = the last
// CRYPTO_SPARK_N hourly prices (oldest first), quantised 0..255 between spark_lo and spark_hi (USD).
// ns = valid points (0 = no history: HMI keeps its on-device samples). hi24/lo24 = 24 h range (USD, 0 unknown).
#define CRYPTO_SPARK_N 24
struct Coin {
  char sym[6]; char name[10]; float price; float chg; float vol;  // vol = 24 h volume, USD
  float hi24, lo24;
  float spark_lo, spark_hi;
  uint8_t ns;
  uint8_t spark[CRYPTO_SPARK_N];
};
struct CryptoData { bool valid; uint8_t n; Coin c[4]; };
#define NEWS_MAX 6
#define NEWS_TITLE_MAX 100
struct NewsData { bool valid; uint8_t n; char src[24]; char title[NEWS_MAX][NEWS_TITLE_MAX]; };
// Twelve Data quote (v3). close / percent_change arrive as strings; the parser converts them.
#define STOCK_N 4
struct StockRow { char sym[8]; char name[16]; float price; float chg; };
struct StocksData { bool valid; uint8_t n; StockRow row[STOCK_N]; };
// cricapi currentMatches, trimmed. The raw body echoes the apikey and can exceed
// NET_BODY_MAX, so the net task streams a few matches and never stores the key.
#define CRIC_N 3
struct CricInn { int16_t r, w; char o[8]; };
struct CricMatch {
  char name[40];
  char team[2][14];
  char detail[40];
  char kind[8];
  CricInn inn[2];
  uint8_t nt, ni;
  bool started, ended;
};
struct CricketData { bool valid; uint8_t n; CricMatch m[CRIC_N]; };
// Presence only, never the key. oracle = xAI key, groq = Groq key (v3.1.2). A key is
// present when it is in NVS or has a built-in default (secrets.h); dflt has bit KEYB_x
// set when the built-in default is the one in use (no NVS value).
// P016: football key removed; bits follow net_task KI_* order (runtime only, never persisted).
enum : uint8_t { KEYB_STOCKS = 1, KEYB_CRICKET = 2, KEYB_XAI = 4, KEYB_GROQ = 8, KEYB_OWM = 16, KEYB_GEMINI = 32 };
// v3.1.6 p0p1: gemini appended (aggregate initialisers without it stay valid: false).
struct KeysData { bool stocks, cricket, oracle, groq, owm; uint8_t dflt; bool gemini; };
struct LocData { float lat, lon; char city[24]; };
// ap_pass: random per-session WPA2 password of the setup AP, shown on the
// device screen only (never logged). Not a user secret.
struct PortalData { bool on; bool saved; uint8_t clients; char ap_ssid[20]; char ap_pass[12]; };

// Laptop companion (local only). cpu/ram are 0-100. note is a short title from
// the paired laptop. token is shown on the companion page and is never logged.
// ms == 0 means no sample yet. ip is the STA address the listener is bound with
// (0 when it is not listening).
#ifndef COMP_NOTE_MAX
#define COMP_NOTE_MAX 47
#endif
struct CompData {
  bool on;
  bool listening;
  bool paired;
  bool session;  // TCP client authenticated
  bool live;     // session and a STAT/NOTIF newer than COMP_STALE_MS
  uint8_t cpu;
  uint8_t ram;
  uint32_t ms;
  uint32_t ip;
  char note[COMP_NOTE_MAX + 1];  // notification title (ASCII). Never a secret.
  char token[12];                // pairing code while companion is on; never log
  char devid[16];                // stable "AURA-XXXX-XX" for the Link PC card
  uint32_t tok_ms;               // millis() the current pair code was minted
};

// v3.1.6 Now Playing from the paired laptop (NOW / ART lines). Written by net_task,
// read by the HMI. Cleared when the session drops, on Forget, and when the
// listener stops. Art pixels are NOT in here (AuraData is copied every UI
// frame); fetch them with ds_get_comp_art() when art_seq changes.
#ifndef COMP_NOW_FIELD_MAX
#define COMP_NOW_FIELD_MAX 47
#endif
#ifndef COMP_ART_MAX_DIM
#define COMP_ART_MAX_DIM 48
#endif
#ifndef COMP_ART_MAX
#define COMP_ART_MAX 4608
#endif
struct CompNowData {
  bool playing;                          // title or artist non-empty
  bool live;                             // playing + session + ms fresher than COMP_NOW_STALE_MS
  char title[COMP_NOW_FIELD_MAX + 1];    // printable ASCII, may be empty
  char artist[COMP_NOW_FIELD_MAX + 1];
  uint8_t art_w, art_h;                  // 0 = no art (also 0 while a new ART is being received)
  uint16_t art_seq;                      // bumps on every art commit / clear
  uint32_t ms;                           // millis() of the last NOW or ART update (0 = none)
};

// Oracle (v3.1.1): written by net_task, read by the UI. Never holds the key.
enum OracleSt : uint8_t {
  OR_IDLE = 0, OR_THINKING,  // request sent / waiting for the first token (20 s timeout)
  OR_STREAM,                 // tokens arriving (SSE)
  OR_DONE,                   // reply complete (or capped at ORACLE_TEXT_MAX)
  OR_BUSY,                   // HTTP 429: retry_until_ms countdown
  OR_ERROR,                  // reason[] holds a short message
  OR_KEYBAD,                 // HTTP 401/403: key rejected
};
#define ORACLE_TEXT_MAX 604
struct OracleData {
  uint8_t st;
  uint8_t provider;          // 0 = device (xAI or Groq, see api), 1 = PC app
  bool sse;                  // streaming used for the current reply
  uint16_t http;
  uint32_t seq;              // request number this state belongs to
  uint32_t retry_until_ms;   // OR_BUSY
  char model[32];
  char reason[32];
  uint16_t n;
  char text[ORACLE_TEXT_MAX];
  // v3.1.2 (appended): device API. api = resolved ORA_XAI / ORA_GROQ / ORA_GEMINI; api_sel = portal
  // setting (ORA_SEL_AUTO: Gemini if its key is set, else xAI, else Groq); key_ok = the resolved
  // API has a key (NVS or built-in default).
  uint8_t api;
  uint8_t api_sel;
  bool key_ok;
};
// v3.1.6 p0p1: Gemini (Google generateContent) appended. Auto = Gemini if its key is set, else xAI, else Groq.
enum : uint8_t { ORA_XAI = 0, ORA_GROQ = 1, ORA_GEMINI = 2 };
enum : uint8_t { ORA_SEL_AUTO = 0, ORA_SEL_XAI = 1, ORA_SEL_GROQ = 2, ORA_SEL_GEMINI = 3 };
static inline const char *ora_api_name(uint8_t api) { return api == ORA_GROQ ? "GROQ" : api == ORA_GEMINI ? "GEMINI" : "XAI"; }
static inline const char *ora_api_label(uint8_t api) { return api == ORA_GROQ ? "Groq" : api == ORA_GEMINI ? "Gemini" : "Grok"; }
static inline const char *ora_api_from(uint8_t api) {
  return api == ORA_GROQ ? "FROM API.GROQ.COM" : api == ORA_GEMINI ? "FROM GENERATIVELANGUAGE.GOOGLEAPIS" : "FROM API.X.AI";
}

// PC Status sheet data. The live companion (CompData) wins when it has a sample.
// Host captures fill this when the companion snapshot is empty.
#define PC_NOTIF_MAX 4
struct PcNotif { char app[12]; char title[40]; uint32_t age_s; };
struct PcStatusData {
  bool linked;
  uint32_t ok_ms;
  char host[16];
  uint8_t cpu_pct;
  uint16_t ram_used_mb, ram_total_mb;
  uint8_t notif_count;
  uint8_t notif_n;
  PcNotif notif[PC_NOTIF_MAX];
};

struct AuraData {
  NetData net;
  BleData ble;
  BattData batt;
  UiData ui;
  JobInfo job[JOB_COUNT];
  WxData wx;
  AirData air;
  CryptoData crypto;
  NewsData news;
  StocksData stocks;
  CricketData cricket;
  KeysData keys;
  LocData loc;
  PortalData portal;
  OracleData oracle;
  CompData comp;
  CompNowData comp_now;  // v3.1.6
  bool time_valid;
  PcStatusData pc;
};

bool ds_init(void);
bool ds_get(AuraData *out, uint32_t timeout_ms = 10);
bool ds_set_net(const NetData *n, uint32_t timeout_ms = 10);
bool ds_get_net(NetData *n, uint32_t timeout_ms = 10);
bool ds_set_ble(const BleData *b, uint32_t timeout_ms = 10);
bool ds_set_hid_mode(bool on, uint32_t timeout_ms = 10);
bool ds_set_batt(const BattData *b, uint32_t timeout_ms = 10);
bool ds_set_ui(const UiData *u, uint32_t timeout_ms = 10);
// v3.1 app data
bool ds_set_job(uint8_t job, const JobInfo *j, uint32_t timeout_ms = 10);
bool ds_set_wx(const WxData *w, uint32_t timeout_ms = 10);
bool ds_set_air(const AirData *a, uint32_t timeout_ms = 10);
bool ds_set_crypto(const CryptoData *c, uint32_t timeout_ms = 10);
bool ds_set_news(const NewsData *n, uint32_t timeout_ms = 10);
bool ds_set_stocks(const StocksData *s, uint32_t timeout_ms = 10);
bool ds_set_cricket(const CricketData *c, uint32_t timeout_ms = 10);
bool ds_set_keys(const KeysData *k, uint32_t timeout_ms = 10);
bool ds_set_loc(const LocData *l, uint32_t timeout_ms = 10);
bool ds_set_portal(const PortalData *p, uint32_t timeout_ms = 10);
bool ds_set_time_valid(bool v, uint32_t timeout_ms = 10);
bool ds_set_oracle(const OracleData *o, uint32_t timeout_ms = 10);
bool ds_set_comp(const CompData *c, uint32_t timeout_ms = 10);
bool ds_set_pc(const PcStatusData *p, uint32_t timeout_ms = 10);
// v3.1.6 Now Playing. ds_set_comp_now keeps art_w/art_h/art_seq (owned by the art calls).
bool ds_set_comp_now(const CompNowData *c, uint32_t timeout_ms = 10);
bool ds_comp_art_begin(uint32_t timeout_ms = 10);
bool ds_comp_art_write(uint16_t off, const uint8_t *src, uint16_t n, uint32_t timeout_ms = 10);
bool ds_comp_art_commit(uint8_t w, uint8_t h, uint32_t timeout_ms = 10);
bool ds_comp_art_clear(uint32_t timeout_ms = 10);
bool ds_get_comp_art(uint8_t *dst, uint32_t dst_sz, uint8_t *w, uint8_t *h, uint16_t *seq, uint32_t timeout_ms = 10);
