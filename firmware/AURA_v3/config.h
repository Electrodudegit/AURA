// config.h - AURA v3 P0 board configuration, pins and tunables.
// Hardware (locked): classic ESP32-WROOM-32 DevKit V1, no PSRAM, 4 MB flash.
//
// SECRETS (v3.1.2): built-in defaults carried over from AURA v2 live ONLY in
// secrets.h (owner-approved), included by wifi_mgr.cpp and net_task.cpp. NVS
// values (setup portal, serial `wifi set`) override every default.
#pragma once
#include <stdint.h>

// ------------------------------------------------------------- SPI (VSPI) ---
// ESP-IDF: SPI3_HOST == VSPI on the classic ESP32. 18/19/23/5 are the VSPI
// IO_MUX pins, so the 40 MHz display clock does not go through the GPIO matrix.
#define PIN_SPI_SCK   18
#define PIN_SPI_MISO  19
#define PIN_SPI_MOSI  23

// ILI9341 320x240 (landscape)
#define PIN_TFT_CS    5
#define PIN_TFT_DC    2
#define PIN_TFT_RST   4
#define PIN_TFT_BL    15
// TFT SPI clock. 40 MHz is fine on the VSPI IO_MUX pins with short wires.
// Documented fallback if you see corruption: 26666666 (80 MHz / 3).
#define SPI_TFT_HZ    40000000
#define TFT_ROTATION  1        // 0..3, 1 = landscape 320x240
#define TFT_INVERT    0        // set 1 if the panel shows inverted colours
#define TFT_BGR       1        // MADCTL BGR bit (most ILI9341 modules)
#define BL_LEDC_FREQ  5000
#define BL_LEDC_BITS  8
#define BL_DEFAULT    255      // 0..255, init -> first UI frame only (then prefs.bright applies)
#define BL_BRIGHT_DEF 100      // prefs.bright default, percent (was 80 through v3.1.6)
// Idle backlight, percent of prefs.bright. Was 40 / 12 with an absolute 38 % night
// cap that was a no-op on Buddy (38 ~= 40) and never reached the saver.
#define BL_IDLE_PCT   75       // 20 s no input, and Buddy idle takeover (day)
#define BL_SAVER_PCT  25       // split-flap saver (day)
#define BL_NIGHT_PCT  50       // extra factor on Buddy / saver 22:30-06:30 when Night dim is on

// XPT2046 resistive touch on the same bus
#define PIN_TOUCH_CS  21
#define PIN_TOUCH_IRQ 22       // PENIRQ, active low (routing on the module is UNVERIFIED)
#define TOUCH_SPI_HZ  (2 * 1000 * 1000)

// KY-040 rotary encoder
#define PIN_ENC_CLK   32
#define PIN_ENC_DT    33
#define PIN_ENC_SW    25
#define ENC_STEPS_PER_DETENT 4 // quadrature transitions per detent (ASSUMPTION: KY-040 typ. 4; some are 2)
#define ENC_SW_DEBOUNCE_MS   30
#define ENC_SW_LONG_MS       600

// Battery
#define PIN_BATT_ADC  34       // ADC1_CH6, input only
#define BATT_DIVIDER  2.0f     // ASSUMPTION: divider ratio unknown, 2.0 = 100k/100k. Measure and fix.
#define BATT_PERIOD_MS 2000

// Spare / reserved (not driven by P0)
// GPIO16, GPIO17 : spare (no MPU in v3)
// GPIO14, 27, 35, 26 : reserved for I2S, unused in P0

// ------------------------------------------------------------------ touch ---
#define TOUCH_TICK_MS        10    // 100 Hz while pressed / IRQ low
#define TOUCH_FALLBACK_MS    50    // ~20 Hz idle poll when PENIRQ stays high (unrouted IRQ safety net)
#define TOUCH_SAMPLES        7     // per axis; min+max dropped, mean of middle 5
#define TOUCH_Z_PRESS        400   // press when Z > this
#define TOUCH_Z_RELEASE      250   // release when Z < this (hysteresis)
#define TOUCH_DEBOUNCE_DOWN  2     // consecutive valid samples to go DOWN
#define TOUCH_DEBOUNCE_UP    3     // consecutive invalid samples to go UP
#define TOUCH_JUMP_PX        40    // reject a sample that jumps further than this while held
#define TOUCH_SPREAD_MAX     80    // raw spread (max-min of middle 5) above this = noisy sample
#define TOUCH_IIR_SHIFT      1     // alpha = 1/2
#define TOUCH_BOOT_HOLD_MS   2000  // hold touch this long at boot -> calibration screen
#define TOUCH_CAL_VERIFY_PX  10    // 4th (verify) point must land within this

// Default mapping used until `cal` is run. MEASURED on board d31c (p1 tgrid log),
// expressed in LANDSCAPE rotation-1 screen space (independent of TFT_ROTATION):
// screen X follows raw Y (decreasing), screen Y follows raw X (decreasing).
// Least-squares fit of 7 X points (max residual 3 px) and 2 rows of Y points
// (Y extrapolated beyond y=24..72: re-check at the bottom edge).
#define TOUCH_DEF_RY_AT_SX0    3900   // raw Y at screen x = 0
#define TOUCH_DEF_RY_AT_SX319  339    // raw Y at screen x = 319
#define TOUCH_DEF_RX_AT_SY0    3806   // raw X at screen y = 0
#define TOUCH_DEF_RX_AT_SY239  491    // raw X at screen y = 239

// ---------------------------------------------------------------- gesture ---
#define GEST_TAP_MAX_MS      350
#define GEST_TAP_MAX_PX      10
#define GEST_LONG_MS         600
#define GEST_SWIPE_MIN_PX    40
#define GEST_SWIPE_DOM_X10   15    // dominant axis must be >= 1.5x the other

// ---------------------------------------------------------------------- UI ---
#define UI_FPS               25
#define UI_FRAME_MS          (1000 / UI_FPS)
#define UI_FRAME_CAP_MS      80    // frame longer than this counts as an overrun
#define UI_BAND_H            20    // p2: 30 -> 20 rows, 2x12800 B instead of 2x19200 B (+12.8 KB heap)
#define UI_MIN_HIT_PX        44    // minimum touch target edge (resistive, finger)
#define UI_BOOT_SPLASH_MS    1500

// --------------------------------------------------------------------- net ---
#define NET_WIFI_CONNECT_TIMEOUT_MS 15000
#define NET_BACKOFF_MIN_MS   2000
#define NET_BACKOFF_MAX_MS   60000
// v3.1.6: ignore coex/beacon blips. 1.5 s is the stronger of the two reviews
// (RTOS used ~500 ms). A link that was up >= WIFI_STABLE_MS resets backoff and
// tries WIFI_FAST_RECONNECT_TRIES of WiFi.reconnect() before a full begin().
#define WIFI_LINK_LOSS_DEBOUNCE_MS 1500u
#define WIFI_STABLE_MS             15000u
#define WIFI_FAST_RECONNECT_TRIES  2
#define NET_TLS_TIMEOUT_MS   6000
#define NET_CRYPTO_TIMEOUT_MS 12000  // v3.1.6 p0p1: CoinGecko markets+sparkline body ~16 KB (streamed)
// TLS gate (ESTIMATES, see README): mbedTLS here has fixed 16 KB IN + 16 KB OUT
// record buffers (~16.7 KB each, separate allocations), plus ~10 KB handshake
// state, plus Wi-Fi RX buffers for the server flight. Need one >= ~17 KB block
// with slack, and enough total for the whole session.
#define NET_TLS_MIN_LARGEST  17408u                // health warning only (= GATE_DUAL_MIN); the gate is net_proto gate_decide
// v3.1.3: the 51200 free floor is gone; the floor is per host = measured handshake peak + RX
// reserve + margin (net_proto tls_floor: Groq/xAI 55040, open-meteo 63232, see README).
// v3.1.3 Wi-Fi driver buffers (wifi_preinit; Arduino 3.3.12 defaults in brackets)
#define WIFI_STATIC_RX_BUF   3    // [4] 1.6 KB each, allocated at init (min 2)
#define WIFI_DYN_RX_BUF      10   // [32] cap only (allocated on demand); bounds the RX peak during TLS
#define WIFI_DYN_TX_BUF      12   // [32] cap only
#define WIFI_CACHE_TX_BUF    2    // [4]
#define WIFI_MGMT_SBUF       10   // [32]
#define WIFI_RX_MGMT_BUF     3    // [5]
#define NET_UI_RELEASE_MS    150u                  // wait for the UI to drop its caches before a TLS gate
#define UI_CACHE_RESERVE     24576u                // a UI cache may only be built if this much stays free
#define NET_TLS_SKIP_BACKOFF_MIN_MS (60UL * 1000UL)  // heap-guard skip: 60 s, doubling
#define NET_TLS_SKIP_BACKOFF_MAX_MS (10UL * 60UL * 1000UL)  // ... up to 10 min
#define NET_HEAP_SAMPLE_US   2000u                 // esp_timer heap sampling period during a TLS session
// v3.1 data jobs (one TLS session at a time, HTTP/1.0 so bodies are never chunked)
#define WEATHER_HOST         "api.open-meteo.com"
#define AIR_HOST             "air-quality-api.open-meteo.com"
#define CRYPTO_HOST          "api.coingecko.com"
#define NEWS_HOST            "feeds.bbci.co.uk"
#define NEWS_PATH            "/news/rss.xml"
#define STOCK_HOST           "api.twelvedata.com"   // 32-char (Twelve Data) keys: batch /quote
// v3.1.6 p0p1: 16-char keys are Alpha Vantage keys (Twelve Data answers 401 to them). Free tier =
// 25 requests/day, one symbol per GLOBAL_QUOTE: one symbol per run, quick fill after boot, then slow.
#define STOCK_AV_HOST        "www.alphavantage.co"
#define NET_PERIOD_STOCK_AV_MS   (70UL * 60UL * 1000UL)  // steady: ~21 calls/day incl. a 4-call boot fill
#define NET_PERIOD_STOCK_FILL_MS (75UL * 1000UL)        // until all STOCK_N rows exist (AV burst limit 1/s)
#define CRIC_HOST            "api.cricapi.com"
// Default location until one is entered in the setup portal (ASSUMPTION: New Delhi,
// same as p0-p2). The home clock is fixed to IST (UTC+5:30).
#define LOC_DEFAULT_LAT      28.61f
#define LOC_DEFAULT_LON      77.21f
#define LOC_DEFAULT_CITY     "NEW DELHI"
#define AURA_TZ              "IST-5:30"
#define AURA_TZ_OPENMETEO    "Asia%2FKolkata"
#define NET_PERIOD_WX_MS     (10UL * 60UL * 1000UL)
#define NET_PERIOD_AIR_MS    (30UL * 60UL * 1000UL)
#define NET_PERIOD_CRYPTO_MS (5UL * 60UL * 1000UL)
#define NET_PERIOD_NEWS_MS   (15UL * 60UL * 1000UL)
#define NET_PERIOD_STOCK_MS  (15UL * 60UL * 1000UL)  // Twelve Data free-tier credits
#define NET_PERIOD_CRIC_MS   (15UL * 60UL * 1000UL)  // cricapi free tier is about 100 hits/day
#define NET_JOB_RETRIES      3      // retry delays 20 s, 60 s, 180 s, then the normal period
#define NET_NEWS_TIMEOUT_MS  12000  // RSS is streamed; we stop after NEWS_MAX titles
#define NET_BODY_MAX         4096   // bounded JSON body buffer (Open-Meteo ~2 KB)
#define NET_PATH_MAX         448u   // longest HTTPS request path; written as its own TLS record (WX 349 B)
#define PORTAL_TIMEOUT_MS    (10UL * 60UL * 1000UL)  // setup AP closes itself after 10 min
#define COMP_STALE_MS        15000u  // companion STAT older than this -> CompData.live = false
// v3.1.6 Now Playing over the same link (NOW / ART). Token-identical to comp_proto.h / data_store.h.
#define COMP_NOW_FIELD_MAX   47      // title / artist, printable ASCII
#define COMP_ART_MAX_DIM     48      // ART w, h in 1..48
#define COMP_ART_MAX         4608    // 48*48*2 RGB565 LE (one static buffer in data_store)
#define COMP_NOW_STALE_MS    120000u // NOW/ART older than this -> CompNowData.live = false
#define COMP_ART_RX_MS       5000u   // ART payload must finish within this after its header

// ------------------------------------------------------------------ health ---
#define HEALTH_PERIOD_MS     5000
// ---- Oracle (xAI v3.1.1, Groq v3.1.2) ---------------------------------------
// Keys: NVS "keys"/"oracle" (xAI) and "keys"/"groq" (Groq), entered in the setup
// portal; otherwise XAI_API_KEY_DEFAULT / GROQ_API_KEY_DEFAULT from secrets.h.
// Never logged or shown (length only). Both APIs are OpenAI-compatible.
#define ORACLE_HOST          "api.x.ai"
#define ORACLE_PATH          "/v1/chat/completions"
#define ORACLE_MODEL_DEFAULT "grok-3-mini"            // NVS "oracle"/"model" overrides (portal field)
#define GROQ_HOST            "api.groq.com"
#define GROQ_PATH            "/openai/v1/chat/completions"
#define GROQ_MODEL_DEFAULT   "llama-3.1-8b-instant"   // NVS "oracle"/"gmodel" overrides (portal field)
// v3.1.6 p0p1: Google Gemini generateContent (NOT OpenAI format). Key in the x-goog-api-key header
// (never in the URL). gemini-2.5-flash with thinkingBudget 0 answered in ~0.9 s on 2026-10-26;
// gemini-3.5-flash-lite took 20-27 s (over ORACLE_TIMEOUT_MS), gemini-2.0-flash / 2.5-flash-lite are retired.
#define GEMINI_HOST          "generativelanguage.googleapis.com"
#define GEMINI_PATH_FMT      "/v1beta/models/%s:generateContent"
#define GEMINI_MODEL_DEFAULT "gemini-2.5-flash"       // NVS "oracle"/"mmodel" overrides (portal field)
#define ORACLE_MAX_TOKENS    200
#define ORACLE_TEMPERATURE   "0.7"
#define ORACLE_TIMEOUT_MS    20000u                   // thinking timeout (also the max gap between tokens)
#define ORACLE_SSE_MARGIN    8192u                    // SSE only if the gate passes with this much to spare
#define ORACLE_PROMPT_MAX    384
#define ORACLE_BUSY_DEFAULT_S 30u                     // 429 without Retry-After
#define HEALTH_FREE_FLOOR    8192u  // WARN if free/largest ever drops below this during a TLS session

// ------------------------------------------------------------------- tasks ---
// ESP-IDF/FreeRTOS: stack sizes are in BYTES on ESP32.
// p2 sizes from p1 hardware HWM (free bytes): ui 4184/6144, net 7032/9216 (no TLS yet),
// ble 4236/6144 (incl. NimBLE init), loop 5988/8192.
#define UI_TASK_STACK        4608   // v3.1.3: hw HWM free 3900 of 6144 (2244 used) -> 4608 keeps 2.3 KB margin
#define UI_TASK_PRIO         5
#define UI_TASK_CORE         1
#define NET_TASK_STACK       9216   // TLS handshake runs here; 8 KB = ESP-IDF https example size. Re-check HWM after TLS
#define NET_TASK_PRIO        3
#define NET_TASK_CORE        0
#define BLE_TASK_STACK       5632   // v3.1.3: ble + CLI/battery/health (loopTask deleted); hw: ble used 2280, loop 3880 incl. a 1.7 KB AuraData now static
#define BLE_SVC_POLL_MS      20     // v3.1.3: ble task queue wait = CLI/battery/health poll period
#define BLE_TASK_PRIO        4
#define BLE_TASK_CORE        0
#define HIDQ_LEN             16
#define HID_MAX_REPEAT       10     // coalesced encoder detents per HID queue item
#define LAT_LOG_MIN_MS       250    // [lat] line rate limit
#define LOOP_TASK_STACK      6144   // Arduino loopTask: runs setup() only, then deletes itself (v3.1.3)
// BLE on demand: the stack is only started while HID mode is ON and fully
// de-initialised (heap returned) when HID mode goes OFF. 0 = always on (p1 behaviour).
#define BLE_ON_DEMAND        1
#define BLE_NOCONN_MS        120000u  // PC keys: stop BLE if no host connects within 2 min
#define BLE_READV_DELAY_MS   1500u    // Windows HID bond/probe settle before re-advertise
#define BLE_READV_FLAP_MS    5000u    // cool-down after flap breaker clears bonds
#define BLE_FLAP_WINDOW_MS   10000u   // count disconnects in this window
#define BLE_FLAP_LIMIT       3        // → deleteAllBonds + long cool-down
// Conn intervals for Wi-Fi modem-sleep coexistence (units of 1.25 ms).
// 7.5–15 ms (old) + updateConnParams-on-connect caused Windows HID connect/disconnect flaps.
#define BLE_CONN_INTERVAL_MIN 24      // 30 ms
#define BLE_CONN_INTERVAL_MAX 40      // 50 ms
#define BLE_CONN_LATENCY      0
#define BLE_CONN_TIMEOUT      400     // 4 s (units of 10 ms)
#define BLE_DEVICE_NAME      "AURA v3"
