// net_proto.h - pure, host-testable protocol helpers used by net_task:
// TLS heap-gate decision, bounded HTTP header scan, chunked transfer decoder,
// incremental JSON string extractor, SSE (text/event-stream) parser and the
// capped Oracle reply buffer. No Arduino / IDF dependencies, no heap.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "data_store.h"

// ---- TLS gate (v3.1.1) ------------------------------------------------------
// mbedTLS allocates the IN and OUT record buffers (~16.7 KB each) separately,
// so two distinct >= 17 KB blocks work as well as one >= 36 KB block.
#define GATE_SINGLE_MIN   36864u
#define GATE_DUAL_MIN     17408u
enum GateRes : uint8_t { GATE_OK_SINGLE = 0, GATE_OK_DUAL, GATE_SKIP_FREE, GATE_SKIP_BLOCKS };
GateRes gate_decide(uint32_t largest, uint32_t second, uint32_t free_total, uint32_t free_floor);
const char *gate_name(GateRes r);  // "ok(single)" / "ok(dual)" / "skip(free<floor)" / "skip(blocks)"
// ---- TLS need per host (v3.1.3, measured) -----------------------------------
// Handshake heap peak of the arduino-esp32 3.3.12 TLS client (prebuilt mbedTLS 3.6.6 config:
// IN = OUT = 16384 content (16717 B buffers), TLS 1.2, keep peer cert, CA bundle), measured
// on the box with that exact config (-m32 build, ESP TLSF + light-poisoning block overhead
// added per allocation). See README "TLS heap". Floor = peak + Wi-Fi RX + margin.
#define TLS_RX_RESERVE    6656u   // TCP_WND 5760 = 4 segments in ~1.66 KB Wi-Fi dynamic RX buffers
#define TLS_MARGIN        2048u
#define TLS_PEAK_DEFAULT  54528u  // unknown host: the largest measured peak
uint32_t tls_need_peak(const char *host);  // measured peak (bytes, rounded up to 256)
static inline uint32_t tls_floor(const char *host) { return tls_need_peak(host) + TLS_RX_RESERVE + TLS_MARGIN; }
static inline bool gate_ok(GateRes r) { return r == GATE_OK_SINGLE || r == GATE_OK_DUAL; }
// Keep the two largest values seen (for the heap walker).
static inline void top2_add(uint32_t v, uint32_t *a, uint32_t *b) {
  if (v > *a) { *b = *a; *a = v; } else if (v > *b) *b = v;
}

// ---- HTTP response header scanner -------------------------------------------
// Feed raw bytes; it consumes header bytes and reports where the body starts.
// Only the status code, Content-Length, chunked flag and Retry-After are kept;
// every other header (and every value) is discarded, never stored or logged.
struct HttpHdr {
  uint16_t status;
  bool chunked;
  int32_t content_length;   // -1 unknown
  int32_t retry_after_s;    // -1 none
  bool done;                // blank line seen
  // private
  char line[96];
  uint8_t ll;
  bool first;
};
void http_hdr_init(HttpHdr *h);
// "HTTP/<d>.<d> <code>" -> code. Same acceptance as sscanf("HTTP/%*d.%*d %d") for real
// status lines, so libc sscanf is not linked.
bool http_status_parse(const char *line, int *code);
// Returns the number of bytes consumed (<= n). When h->done the rest is body.
size_t http_hdr_feed(HttpHdr *h, const uint8_t *p, size_t n);

// ---- chunked transfer decoder ------------------------------------------------
struct ChunkDec {
  uint8_t st;        // private
  uint32_t left;
  uint8_t digits;
  bool done;         // final 0-size chunk seen
  bool error;        // malformed (size > 1 MB or bad hex)
};
void chunk_init(ChunkDec *c);
// Decodes in place: payload bytes are compacted to the start of p. Returns the
// payload length produced from these n input bytes.
size_t chunk_feed(ChunkDec *c, uint8_t *p, size_t n);

// ---- capped reply buffer (UTF-8 folded to the ASCII font range) ---------------
#define ORACLE_REPLY_MAX 600
struct ReplyBuf {
  char s[ORACLE_REPLY_MAX + 4];
  uint16_t n;
  bool full;         // cap reached: truncated at a word boundary + ellipsis (0x81)
  uint8_t u8[4];     // pending UTF-8 sequence
  uint8_t u8n, u8need;
};
void reply_init(ReplyBuf *r);
void reply_putc(ReplyBuf *r, uint8_t c);   // raw (possibly UTF-8) byte of decoded text
void reply_finish(ReplyBuf *r);            // trims trailing spaces

// ---- incremental JSON string extractor ---------------------------------------
// After `anchor` is seen, finds `needle` (which must end in the opening quote of
// a string value, e.g. "\"content\":\"") and streams the decoded string into
// the reply. JSON escapes (\n \" \\ \/ \t \uXXXX) are handled across chunk splits.
struct JStr {
  const char *anchor, *needle;
  uint8_t ai, ni, st, uh;
  uint16_t u;
  bool found;        // string value started
  bool closed;       // closing quote seen
  bool ws;           // v3.1.6 p0p1: skip JSON whitespace while matching anchor/needle (pretty-printed bodies)
};
void jstr_init(JStr *j, const char *anchor, const char *needle);
void jstr_feed(JStr *j, char c, ReplyBuf *out);

// ---- SSE parser (OpenAI-compatible chat.completion.chunk stream) -------------
// Lines starting with "data:" are scanned for choices[0].delta.content; the
// scan state is reset per line (no line buffer). "data: [DONE]" ends the stream.
struct SseParser {
  JStr js;
  uint8_t st;        // private: line position state
  uint8_t pi;
  char probe[8];
  bool done;
  uint32_t events;
};
void sse_init(SseParser *s);
void sse_feed(SseParser *s, const uint8_t *p, size_t n, ReplyBuf *out);

// ---- JSON string escaping for request bodies ---------------------------------
// Writes s as JSON string content (no quotes) into out (cap incl. NUL). Returns false if it did not fit.
bool json_escape(const char *s, char *out, size_t cap);
// v3.1.2: Oracle device API resolution. sel: 0 auto, 1 xAI, 2 Groq, 3 Gemini (v3.1.6 p0p1; manual wins);
// auto = Gemini if a Gemini key is set, else xAI if an xAI key is set (NVS or built-in default), else Groq.
// Returns 0 = xAI, 1 = Groq, 2 = Gemini (ORA_XAI / ORA_GROQ / ORA_GEMINI in data_store.h).
static inline uint8_t ora_resolve(uint8_t sel, bool have_xai_key, bool have_gemini_key = false) {
  return sel == 1 ? 0 : sel == 2 ? 1 : sel == 3 ? 2 : have_gemini_key ? 2 : (have_xai_key ? 0 : 1);
}
// v3.1.2: OpenAI-compatible chat.completions body (xAI and Groq). All strings are
// already JSON-escaped; temperature is a JSON number literal. reasoning_low adds
// "reasoning_effort":"low" (xAI grok-3-mini only; never for Groq). Returns the
// snprintf length (>= cap means it did not fit).
int or_build_body(char *out, size_t cap, const char *model_esc, const char *system_esc, const char *prompt_esc,
                  int max_tokens, const char *temperature, bool stream, bool reasoning_low);

// v3.1.6 p0p1: Google Gemini generateContent body (systemInstruction + one user turn). Strings are
// JSON-escaped; model is only inspected for the thinking switch (2.5 flash: thinkingBudget 0; 3.x flash:
// thinkingLevel minimal) so max_tokens is spent on the answer. Returns the snprintf length.
int gm_build_body(char *out, size_t cap, const char *model, const char *system_esc, const char *prompt_esc,
                  int max_tokens, const char *temperature);

// ---- Oracle response pipeline (shared by net_task and the host tests) ---------
// header scan -> optional chunked decode -> SSE parser or one-shot JSON scan -> reply.
// The body of a non-200 response is never parsed or kept.
struct OrStream {
  HttpHdr h;
  ChunkDec ck;
  SseParser sp;
  JStr js;
  ReplyBuf r;
  bool sse;
  // v3.1.6 p0p1 Gemini: one-shot only; reply = candidates[0].content.parts[0].text. On a non-200 the
  // first ORS_ERR_SCAN body bytes are matched against "API_KEY_INVALID" (Gemini answers 400 to a bad
  // key); nothing of the error body is stored.
  bool gemini;
  bool key_invalid;
  uint8_t kim;
  uint16_t err_n;
};
#define ORS_ERR_SCAN 1536
void ors_init(OrStream *s, bool sse, bool gemini = false);
// Feeds raw socket bytes (decoded in place). Returns true when reading can stop:
// header done with status != 200, stream finished, reply full, or a framing error.
bool ors_feed(OrStream *s, uint8_t *p, size_t n);
static inline bool ors_complete(const OrStream *s) { return s->sse ? s->sp.done : s->js.closed; }

// ---- Twelve Data quote (multi-symbol map). close and percent_change are strings. --
// Returns false when nothing parsed (top-level {"status":"error"} or an empty body).
// Per-symbol {"status":"error"} objects are skipped. Does not log or keep the key.
bool stock_parse(const char *json, StocksData *out);

// ---- Alpha Vantage GLOBAL_QUOTE (v3.1.6 p0p1; one symbol). Fills price + change percent.
// Returns AVQ_OK, AVQ_RATE ("Information"/"Note": daily or burst limit), AVQ_KEY ("Error Message"
// mentioning the apikey) or AVQ_PARSE (empty "Global Quote", unknown symbol). Keeps no body text.
enum : uint8_t { AVQ_OK = 0, AVQ_RATE, AVQ_KEY, AVQ_PARSE };
uint8_t av_quote_parse(const char *json, float *price, float *chg_pct);

// ---- CoinGecko /coins/markets?sparkline=true, streaming (v3.1.6 p0p1) ----------------------------
// ~16 KB body for 4 coins (168 hourly sparkline points each) is never stored: a bounded tokenizer
// keeps current_price, price_change_percentage_24h, total_volume, high_24h, low_24h and the LAST
// CRYPTO_SPARK_N sparkline points per coin (ring), keyed by "id" against the caller's id table.
#define CG_IDS_MAX 4
struct CgScan {
  // tokenizer
  uint8_t depth;
  char ctx[6];                 // '{' or '[' per depth (1-based)
  bool in_str, esc, str_key, after_colon, in_num;
  char key[28]; uint8_t kl;
  char sv[16]; uint8_t svl;    // string value (only "id" is used)
  char num[28]; uint8_t nl;
  bool in_sl, in_spark;        // inside sparkline_in_7d{} / its price[]
  // current coin
  int8_t slot;
  float price, chg, vol, hi, lo;
  uint8_t have;                // bit0 price
  float ring[CRYPTO_SPARK_N];
  uint8_t rh, rn;
  // config / output
  const char *const *ids;
  uint8_t nids;
  bool error;
  Coin coin[CG_IDS_MAX];
  bool got[CG_IDS_MAX];
};
void cg_scan_init(CgScan *s, const char *const *ids, uint8_t nids);
bool cg_scan_feed(CgScan *s, const uint8_t *p, size_t n);  // false = stop (framing error)
// Copies coins in id-table order (missing ids skipped); sym/name are left for the caller. Returns count.
uint8_t cg_scan_take(const CgScan *s, Coin *out, uint8_t cap);

// ---- cricapi currentMatches, streaming. Stops after a live match is featured
// and CRIC_N are stored, or after 16 matches, or at the end of "data":[ .
// The apikey field (echoed by the API) is never copied into CricketData.
struct CricScan {
  uint8_t st, si, depth;
  uint8_t kind[10];
  uint8_t exp[10];
  bool in_str, esc, tok;
  char key[20];
  char val[48];
  uint8_t vl;
  bool capturing;
  uint8_t teams_d, score_d, score_obj;
  CricMatch cur;
  int16_t sr, sw;
  char so[8];
  bool shave;
  uint8_t seen;
  bool have_live;
  CricketData out;
};
void cric_scan_init(CricScan *s);
// Returns false once the caller should close the socket.
bool cric_scan_feed(CricScan *s, const uint8_t *p, size_t n);
void cric_scan_take(const CricScan *s, CricketData *out);
