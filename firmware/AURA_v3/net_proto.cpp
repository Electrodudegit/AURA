// net_proto.cpp - see net_proto.h. Pure C++, compiled on the device and in the host tests.
#include "net_proto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ------------------------------------------------------------------ gate --
GateRes gate_decide(uint32_t largest, uint32_t second, uint32_t free_total, uint32_t free_floor) {
  if (free_total < free_floor) return GATE_SKIP_FREE;
  if (largest >= GATE_SINGLE_MIN) return GATE_OK_SINGLE;
  if (largest >= GATE_DUAL_MIN && second >= GATE_DUAL_MIN) return GATE_OK_DUAL;
  return GATE_SKIP_BLOCKS;
}
// Measured 2026-10-21 (tlsbench/tls_probe, see README): handshake peak, ESP heap estimate.
struct TlsPeak { const char *host; uint32_t peak; };
static const TlsPeak k_tls_peak[] = {
    {"api.groq.com", 46336u},                     // 46168: ECDHE-ECDSA P-256, 3 certs 2451 B
    {"api.x.ai", 46336u},                         // 46244: ECDHE-ECDSA P-256, 3 certs 2469 B
    {"api.open-meteo.com", 54528u},               // 54372: ECDHE-RSA 2048, 3 certs 4067 B
    {"air-quality-api.open-meteo.com", 54528u},   // 54340
    {"api.coingecko.com", 53248u},                // 53164: ECDSA, 4 certs 3416 B
    {"feeds.bbci.co.uk", 50688u},                 // 50464: ECDSA, 4 certs 4502 B
};
uint32_t tls_need_peak(const char *host) {
  if (host)
    for (const TlsPeak &p : k_tls_peak)
      if (strcmp(p.host, host) == 0) return p.peak;
  return TLS_PEAK_DEFAULT;
}
const char *gate_name(GateRes r) {
  switch (r) {
    case GATE_OK_SINGLE: return "ok(single)";
    case GATE_OK_DUAL: return "ok(dual)";
    case GATE_SKIP_FREE: return "skip(free<floor)";
    default: return "skip(blocks)";
  }
}

// ---------------------------------------------------------------- header --
void http_hdr_init(HttpHdr *h) {
  memset(h, 0, sizeof(*h));
  h->content_length = -1;
  h->retry_after_s = -1;
  h->first = true;
}
static inline bool st_digit(char c) { return c >= '0' && c <= '9'; }
static inline bool st_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
static const char *st_int(const char *s, long *v) {  // %d: optional spaces, sign, >= 1 digit
  while (st_space(*s)) s++;
  bool neg = false;
  if (*s == '+' || *s == '-') neg = *s++ == '-';
  if (!st_digit(*s)) return nullptr;
  long x = 0;
  while (st_digit(*s)) { if (x < 100000000L) x = x * 10 + (*s - '0'); s++; }
  *v = neg ? -x : x;
  return s;
}
bool http_status_parse(const char *line, int *code) {
  if (!line || !code || strncmp(line, "HTTP/", 5) != 0) return false;
  long ign = 0, c = 0;
  const char *s = st_int(line + 5, &ign);
  if (!s || *s != '.') return false;
  s = st_int(s + 1, &ign);
  if (!s) return false;
  if (!st_int(s, &c)) return false;
  *code = (int)c;
  return true;
}
static bool prefix_ci(const char *s, const char *p) {
  for (; *p; s++, p++) {
    char a = *s, b = *p;
    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
    if (a != b) return false;
  }
  return true;
}
static void hdr_line(HttpHdr *h) {
  h->line[h->ll] = 0;
  if (h->first) {
    h->first = false;
    int code = 0;
    if (http_status_parse(h->line, &code)) h->status = (uint16_t)code;
    return;
  }
  const char *v = strchr(h->line, ':');
  if (!v) return;
  v++;
  while (*v == ' ') v++;
  if (prefix_ci(h->line, "transfer-encoding:")) {
    h->chunked = strstr(v, "chunked") != nullptr || strstr(v, "Chunked") != nullptr;
  } else if (prefix_ci(h->line, "content-length:")) {
    h->content_length = atol(v);
  } else if (prefix_ci(h->line, "retry-after:")) {
    if (*v >= '0' && *v <= '9') {
      const long s = atol(v);
      h->retry_after_s = s > 3600 ? 3600 : (int32_t)s;
    }  // HTTP-date form ignored -> caller default
  }
  memset(h->line, 0, sizeof(h->line));  // values never linger
}
size_t http_hdr_feed(HttpHdr *h, const uint8_t *p, size_t n) {
  size_t i = 0;
  while (i < n && !h->done) {
    const char c = (char)p[i++];
    if (c == '\r') continue;
    if (c == '\n') {
      if (h->ll == 0 && !h->first) { h->done = true; break; }
      hdr_line(h);
      h->ll = 0;
      continue;
    }
    if (h->ll < sizeof(h->line) - 1) h->line[h->ll++] = c;  // long lines: prefix only
  }
  return i;
}

// --------------------------------------------------------------- chunked --
enum { CK_SIZE = 0, CK_EXT, CK_SIZE_LF, CK_DATA, CK_DATA_CR, CK_DATA_LF, CK_TRAILER, CK_END };
void chunk_init(ChunkDec *c) { memset(c, 0, sizeof(*c)); }
size_t chunk_feed(ChunkDec *c, uint8_t *p, size_t n) {
  size_t o = 0;
  for (size_t i = 0; i < n; i++) {
    const uint8_t b = p[i];
    switch (c->st) {
      case CK_SIZE: {
        int v = -1;
        if (b >= '0' && b <= '9') v = b - '0';
        else if (b >= 'a' && b <= 'f') v = b - 'a' + 10;
        else if (b >= 'A' && b <= 'F') v = b - 'A' + 10;
        if (v >= 0) {
          c->left = (c->left << 4) | (uint32_t)v;
          if (++c->digits > 6) { c->error = true; return o; }  // > 16 MB: refuse
        } else if (b == ';' || b == ' ') c->st = CK_EXT;
        else if (b == '\r') c->st = CK_SIZE_LF;
        else if (b == '\n') goto size_done;
        else { c->error = true; return o; }
        break;
      }
      case CK_EXT:
        if (b == '\n') goto size_done;
        if (b == '\r') c->st = CK_SIZE_LF;
        break;
      case CK_SIZE_LF:
        if (b != '\n') { c->error = true; return o; }
      size_done:
        if (c->digits == 0) { c->error = true; return o; }
        if (c->left == 0) { c->st = CK_TRAILER; c->done = true; }
        else c->st = CK_DATA;
        c->digits = 0;
        break;
      case CK_DATA:
        p[o++] = b;
        if (--c->left == 0) c->st = CK_DATA_CR;
        break;
      case CK_DATA_CR:
        if (b == '\r') c->st = CK_DATA_LF;
        else if (b == '\n') c->st = CK_SIZE;
        else { c->error = true; return o; }
        break;
      case CK_DATA_LF:
        if (b != '\n') { c->error = true; return o; }
        c->st = CK_SIZE;
        break;
      default:  // trailer / end: ignore
        break;
    }
  }
  return o;
}

// ----------------------------------------------------------------- reply --
void reply_init(ReplyBuf *r) { memset(r, 0, sizeof(*r)); }
static void reply_raw(ReplyBuf *r, char c) {
  if (r->full) return;
  if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  if (c == ' ' && (r->n == 0 || r->s[r->n - 1] == ' ')) return;  // collapse whitespace
  if (r->n < ORACLE_REPLY_MAX) { r->s[r->n++] = c; r->s[r->n] = 0; return; }
  // cap: cut at the last word boundary (not earlier than 80 % of the cap)
  int cut = r->n;
  if (c != ' ') {
    while (cut > ORACLE_REPLY_MAX * 4 / 5 && r->s[cut - 1] != ' ') cut--;
    if (cut <= ORACLE_REPLY_MAX * 4 / 5) cut = r->n;
  }
  while (cut > 0 && (r->s[cut - 1] == ' ' || r->s[cut - 1] == ',' || r->s[cut - 1] == ';')) cut--;
  r->s[cut] = (char)0x81;  // ellipsis glyph
  r->s[cut + 1] = 0;
  r->n = (uint16_t)(cut + 1);
  r->full = true;
}
static char fold_cp(uint32_t cp) {
  if (cp >= 0x20 && cp < 0x7F) return (char)cp;
  switch (cp) {
    case 0x2018: case 0x2019: case 0x2032: return '\'';
    case 0x201C: case 0x201D: return '"';
    case 0x2013: case 0x2014: case 0x2212: return '-';
    case 0x2026: return (char)0x81;
    case 0x00B0: return (char)0x7F;  // degree glyph in the AURA fonts
    case 0x00A0: case 0x2009: case 0x202F: return ' ';
    case 0x2022: case 0x00B7: return (char)0x80;  // middle dot
  }
  if (cp >= 0xC0 && cp <= 0xFF) {
    static const char fold[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    return fold[cp - 0xC0];
  }
  return 0;  // dropped (emoji, CJK, ...)
}
void reply_putc(ReplyBuf *r, uint8_t c) {
  if (r->full) return;
  if (r->u8need) {
    if ((c & 0xC0) != 0x80) { r->u8need = 0; r->u8n = 0; }  // broken sequence: resync
    else {
      r->u8[r->u8n++] = c;
      if (r->u8n == r->u8need) {
        uint32_t cp = r->u8[0] & (0x7F >> r->u8need);
        for (int i = 1; i < r->u8n; i++) cp = (cp << 6) | (r->u8[i] & 0x3F);
        r->u8need = 0;
        r->u8n = 0;
        const char f = fold_cp(cp);
        if (f) reply_raw(r, f);
      }
      return;
    }
  }
  if (c < 0x80) { if (c >= 0x20 || c == '\n' || c == '\t') reply_raw(r, (char)c); return; }
  const uint8_t need = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
  if (!need) return;
  r->u8[0] = c;
  r->u8n = 1;
  r->u8need = need;
}
static void reply_cp(ReplyBuf *r, uint32_t cp) {  // from \uXXXX
  if (cp < 0x80) { reply_putc(r, (uint8_t)cp); return; }
  const char f = fold_cp(cp);
  if (f) reply_raw(r, f);
}
void reply_finish(ReplyBuf *r) {
  while (r->n && r->s[r->n - 1] == ' ') r->s[--r->n] = 0;
}

// ------------------------------------------------------------------ jstr --
enum { JS_ANCHOR = 0, JS_NEEDLE, JS_STR, JS_ESC, JS_U, JS_DONE };
void jstr_init(JStr *j, const char *anchor, const char *needle) {
  memset(j, 0, sizeof(*j));
  j->anchor = anchor;
  j->needle = needle;
  j->st = anchor && anchor[0] ? JS_ANCHOR : JS_NEEDLE;
}
static void match(const char *pat, uint8_t *idx, char c) {
  if (c == pat[*idx]) { (*idx)++; return; }
  *idx = (c == pat[0]) ? 1 : 0;
}
static inline bool jws(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
void jstr_feed(JStr *j, char c, ReplyBuf *out) {
  if (j->ws && (j->st == JS_ANCHOR || j->st == JS_NEEDLE) && jws(c)) return;  // pretty-printed JSON (Gemini)
  switch (j->st) {
    case JS_ANCHOR:
      match(j->anchor, &j->ai, c);
      if (!j->anchor[j->ai]) j->st = JS_NEEDLE;
      break;
    case JS_NEEDLE:
      match(j->needle, &j->ni, c);
      if (!j->needle[j->ni]) { j->st = JS_STR; j->found = true; }
      break;
    case JS_STR:
      if (c == '\\') j->st = JS_ESC;
      else if (c == '"') { j->st = JS_DONE; j->closed = true; }
      else reply_putc(out, (uint8_t)c);
      break;
    case JS_ESC:
      j->st = JS_STR;
      switch (c) {
        case 'n': case 'r': case 't': reply_putc(out, ' '); break;
        case 'b': case 'f': break;
        case 'u': j->st = JS_U; j->u = 0; j->uh = 0; break;
        default: reply_putc(out, (uint8_t)c); break;  // \" \\ \/
      }
      break;
    case JS_U: {
      int v = -1;
      if (c >= '0' && c <= '9') v = c - '0';
      else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
      if (v < 0) { j->st = JS_STR; break; }
      j->u = (uint16_t)((j->u << 4) | v);
      if (++j->uh == 4) {
        j->st = JS_STR;
        if (j->u < 0xD800 || j->u > 0xDFFF) reply_cp(out, j->u);  // surrogates (emoji) dropped
      }
      break;
    }
    default:
      break;
  }
}

// ------------------------------------------------------------------- sse --
enum { SL_START = 0, SL_PROBE, SL_DATA, SL_SKIP };
void sse_init(SseParser *s) {
  memset(s, 0, sizeof(*s));
  jstr_init(&s->js, "\"delta\":", "\"content\":\"");
}
void sse_feed(SseParser *s, const uint8_t *p, size_t n, ReplyBuf *out) {
  for (size_t i = 0; i < n && !s->done; i++) {
    const char c = (char)p[i];
    if (c == '\n') {  // end of line: reset the per-line state
      s->st = SL_START;
      s->pi = 0;
      continue;
    }
    if (c == '\r') continue;
    switch (s->st) {
      case SL_START:
        s->pi = 0;
        s->st = SL_PROBE;
        // fallthrough
      case SL_PROBE:
        s->probe[s->pi++] = c;
        if (s->pi == 5) {
          if (memcmp(s->probe, "data:", 5) == 0) {
            s->st = SL_DATA;
            s->events++;
            jstr_init(&s->js, "\"delta\":", "\"content\":\"");
            s->pi = 0;
          } else s->st = SL_SKIP;  // event:, id:, retry:, comments
        }
        break;
      case SL_DATA:
        // detect " [DONE]" at the start of the payload
        if (s->pi < 7) {
          static const char k_done[] = " [DONE]";
          if (c == k_done[s->pi] || (s->pi == 0 && c == '[')) {
            if (s->pi == 0 && c == '[') s->pi = 1;
            s->pi++;
            if (s->pi == 7) s->done = true;
          } else s->pi = 8;  // not DONE
        }
        jstr_feed(&s->js, c, out);
        break;
      default:
        break;
    }
  }
}

// ------------------------------------------------------------ json escape --
bool json_escape(const char *s, char *out, size_t cap) {
  size_t o = 0;
  for (; *s; s++) {
    const unsigned char c = (unsigned char)*s;
    char e[8];
    size_t l;
    if (c == '"' || c == '\\') { e[0] = '\\'; e[1] = (char)c; l = 2; }
    else if (c == '\n') { e[0] = '\\'; e[1] = 'n'; l = 2; }
    else if (c < 0x20) { l = (size_t)snprintf(e, sizeof(e), "\\u%04x", c); }
    else if (c == 0x7F) { memcpy(e, "\\u00b0", 6); l = 6; }   // AURA degree glyph -> real degree sign
    else if (c >= 0x80) { e[0] = ' '; l = 1; }                 // internal glyphs (dot, ellipsis)
    else { e[0] = (char)c; l = 1; }
    if (o + l + 1 > cap) { if (cap) out[o] = 0; return false; }
    memcpy(out + o, e, l);
    o += l;
  }
  if (cap) out[o] = 0;
  return true;
}

// ------------------------------------------------------- oracle pipeline --
void ors_init(OrStream *s, bool sse, bool gemini) {
  http_hdr_init(&s->h);
  chunk_init(&s->ck);
  sse_init(&s->sp);
  // Gemini: candidates[0].content.parts[0].text (first "text" after "candidates"; bodies are pretty-printed)
  if (gemini) jstr_init(&s->js, "\"candidates\"", "\"text\":\"");
  else jstr_init(&s->js, "\"message\":", "\"content\":\"");
  s->js.ws = gemini;
  reply_init(&s->r);
  s->sse = sse && !gemini;
  s->gemini = gemini;
  s->key_invalid = false;
  s->kim = 0;
  s->err_n = 0;
}
bool ors_feed(OrStream *s, uint8_t *p, size_t n) {
  size_t off = 0;
  if (!s->h.done) {
    off = http_hdr_feed(&s->h, p, n);
    if (!s->h.done) return false;
    if (s->h.status != 200 && !s->gemini) return true;
  }
  if (off >= n) return false;
  p += off;
  n -= off;
  if (s->h.chunked) {
    n = chunk_feed(&s->ck, p, n);
    if (s->ck.error) return true;
  }
  if (s->h.status != 200) {  // Gemini error body: only look for the key-invalid reason, keep nothing
    static const char kInv[] = "API_KEY_INVALID";
    for (size_t q = 0; q < n; q++) {
      match(kInv, &s->kim, (char)p[q]);
      if (!kInv[s->kim]) { s->key_invalid = true; return true; }
    }
    s->err_n = (uint16_t)(s->err_n + n > 65535u ? 65535u : s->err_n + n);
    return s->err_n >= ORS_ERR_SCAN || (s->h.chunked && s->ck.done);
  }
  if (s->sse) sse_feed(&s->sp, p, n, &s->r);
  else for (size_t q = 0; q < n && !s->js.closed; q++) jstr_feed(&s->js, (char)p[q], &s->r);
  return s->r.full || ors_complete(s) || (s->h.chunked && s->ck.done);
}

int or_build_body(char *out, size_t cap, const char *model_esc, const char *system_esc, const char *prompt_esc,
                  int max_tokens, const char *temperature, bool stream, bool reasoning_low) {
  return snprintf(out, cap,
                  "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"%s\"},{\"role\":\"user\","
                  "\"content\":\"%s\"}],\"max_tokens\":%d,\"temperature\":%s,\"stream\":%s%s}",
                  model_esc, system_esc, prompt_esc, max_tokens, temperature, stream ? "true" : "false",
                  reasoning_low ? ",\"reasoning_effort\":\"low\"" : "");
}

int gm_build_body(char *out, size_t cap, const char *model, const char *system_esc, const char *prompt_esc,
                  int max_tokens, const char *temperature) {
  const char *think = "";
  if (model && strncmp(model, "gemini-2.5-flash", 16) == 0) think = ",\"thinkingConfig\":{\"thinkingBudget\":0}";
  else if (model && strncmp(model, "gemini-3", 8) == 0 && strstr(model, "flash"))
    think = ",\"thinkingConfig\":{\"thinkingLevel\":\"minimal\"}";
  return snprintf(out, cap,
                  "{\"systemInstruction\":{\"parts\":[{\"text\":\"%s\"}]},\"contents\":[{\"role\":\"user\",\"parts\":"
                  "[{\"text\":\"%s\"}]}],\"generationConfig\":{\"maxOutputTokens\":%d,\"temperature\":%s%s}}",
                  system_esc, prompt_esc, max_tokens, temperature, think);
}

// --------------------------------------------------------------- stocks --
static const char *stock_obj_end(const char *s) {
  int d = 1;
  bool str = false, esc = false;
  for (const char *p = s; *p; p++) {
    if (str) {
      if (esc) esc = false;
      else if (*p == '\\') esc = true;
      else if (*p == '"') str = false;
      continue;
    }
    if (*p == '"') str = true;
    else if (*p == '{') d++;
    else if (*p == '}') { if (--d == 0) return p; }
  }
  return nullptr;
}
static const char *stock_find(const char *s, const char *e, const char *key) {
  char k[40];
  snprintf(k, sizeof(k), "\"%s\":", key);
  const size_t kl = strlen(k);
  for (const char *p = s; p + kl <= e; p++)
    if (*p == '"' && memcmp(p, k, kl) == 0) return p + kl;
  return nullptr;
}
// Copies a JSON string or a bare number (a leading quote on close/percent_change is skipped).
static bool stock_scalar(const char *s, const char *e, const char *key, char *dst, int cap) {
  const char *p = stock_find(s, e, key);
  if (!p || cap < 2) return false;
  while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  if (p >= e || *p == 'n') return false;
  bool q = false;
  if (*p == '"') { q = true; p++; }
  int n = 0;
  while (p < e && n < cap - 1) {
    if (q) {
      if (*p == '"') break;
      if (*p == '\\' && p + 1 < e) p++;
    } else if (*p == ',' || *p == '}' || *p == ' ' || *p == '\n' || *p == '\r') break;
    dst[n++] = *p++;
  }
  dst[n] = 0;
  return n > 0;
}
bool stock_parse(const char *json, StocksData *out) {
  memset(out, 0, sizeof(*out));
  if (!json) return false;
  static const char *const k_sym[STOCK_N] = {"AAPL", "MSFT", "GOOGL", "AMZN"};
  for (int i = 0; i < STOCK_N; i++) {
    char key[24];
    snprintf(key, sizeof(key), "\"%s\":", k_sym[i]);
    const char *p = json;
    const char *body = nullptr;
    const char *end = nullptr;
    const size_t kl = strlen(key);
    while ((p = strstr(p, key)) != nullptr) {
      const char *b = p + kl;
      while (*b == ' ' || *b == '\t' || *b == '\n' || *b == '\r') b++;
      if (*b == '{') {
        end = stock_obj_end(b + 1);
        if (end) body = b + 1;
        break;
      }
      p += kl;
    }
    if (!body || !end) continue;
    char st[16];
    if (stock_scalar(body, end, "status", st, sizeof(st)) && strcmp(st, "error") == 0) continue;
    char price[24], pct[24];
    if (!stock_scalar(body, end, "close", price, sizeof(price))) continue;
    StockRow &r = out->row[out->n];
    snprintf(r.sym, sizeof(r.sym), "%s", k_sym[i]);
    r.price = strtof(price, nullptr);
    r.chg = stock_scalar(body, end, "percent_change", pct, sizeof(pct)) ? strtof(pct, nullptr) : 0;
    stock_scalar(body, end, "name", r.name, (int)sizeof(r.name));
    out->n++;
  }
  out->valid = out->n > 0;
  return out->valid;
}

// ---------------------------------------------------- alpha vantage --
uint8_t av_quote_parse(const char *json, float *price, float *chg_pct) {
  if (!json) return AVQ_PARSE;
  if (strstr(json, "\"Error Message\"")) return (strstr(json, "apikey") || strstr(json, "API key")) ? AVQ_KEY : AVQ_PARSE;
  if (strstr(json, "\"Information\"") || strstr(json, "\"Note\"")) {
    // AV reports both quota and some key problems as "Information"; only an explicit invalid-key text is KEY.
    return (strstr(json, "apikey is invalid") || strstr(json, "invalid API key")) ? AVQ_KEY : AVQ_RATE;
  }
  const char *g = strstr(json, "\"Global Quote\"");
  const char *ob = g ? strchr(g, '{') : nullptr;
  const char *e = ob ? stock_obj_end(ob + 1) : nullptr;
  if (!e) return AVQ_PARSE;
  char v[24];
  if (!stock_scalar(ob + 1, e, "05. price", v, sizeof(v))) return AVQ_PARSE;
  char *end = nullptr;
  const float pr = strtof(v, &end);
  if (end == v || !(pr > 0)) return AVQ_PARSE;
  *price = pr;
  *chg_pct = stock_scalar(ob + 1, e, "10. change percent", v, sizeof(v)) ? strtof(v, nullptr) : 0.0f;
  return AVQ_OK;
}

// ------------------------------------------------- coingecko markets --
void cg_scan_init(CgScan *s, const char *const *ids, uint8_t nids) {
  memset(s, 0, sizeof(*s));
  s->ids = ids;
  s->nids = nids > CG_IDS_MAX ? CG_IDS_MAX : nids;
  s->slot = -1;
}
static void cg_coin_reset(CgScan *s) {
  s->slot = -1; s->have = 0; s->rh = 0; s->rn = 0;
  s->price = s->chg = s->vol = s->hi = s->lo = 0;
}
static void cg_coin_commit(CgScan *s) {
  if (s->slot < 0 || !(s->have & 1)) return;
  Coin &c = s->coin[s->slot];
  memset(&c, 0, sizeof(c));
  c.price = s->price; c.chg = s->chg; c.vol = s->vol; c.hi24 = s->hi; c.lo24 = s->lo;
  const uint8_t n = s->rn;
  const uint8_t start = n < CRYPTO_SPARK_N ? 0 : s->rh;
  float lo = 0, hi = 0;
  for (uint8_t i = 0; i < n; i++) {
    const float v = s->ring[(start + i) % CRYPTO_SPARK_N];
    if (i == 0 || v < lo) lo = v;
    if (i == 0 || v > hi) hi = v;
  }
  for (uint8_t i = 0; i < n; i++) {
    const float v = s->ring[(start + i) % CRYPTO_SPARK_N];
    c.spark[i] = hi > lo ? (uint8_t)((v - lo) * 255.0f / (hi - lo) + 0.5f) : 128;
  }
  c.ns = n; c.spark_lo = lo; c.spark_hi = hi;
  s->got[s->slot] = true;
}
static void cg_num_end(CgScan *s) {
  if (!s->in_num) return;
  s->in_num = false;
  s->num[s->nl] = 0;
  char *end = nullptr;
  const float v = strtof(s->num, &end);
  const bool ok = end != s->num && *end == 0;  // null/true/false -> not a number
  s->nl = 0;
  if (!ok) return;
  if (s->depth == 4 && s->in_spark) {
    s->ring[s->rh] = v;
    s->rh = (uint8_t)((s->rh + 1) % CRYPTO_SPARK_N);
    if (s->rn < CRYPTO_SPARK_N) s->rn++;
  } else if (s->depth == 2 && s->after_colon) {
    if (!strcmp(s->key, "current_price")) { s->price = v; s->have |= 1; }
    else if (!strcmp(s->key, "price_change_percentage_24h")) s->chg = v;
    else if (!strcmp(s->key, "total_volume")) s->vol = v;
    else if (!strcmp(s->key, "high_24h")) s->hi = v;
    else if (!strcmp(s->key, "low_24h")) s->lo = v;
  }
}
bool cg_scan_feed(CgScan *s, const uint8_t *p, size_t n) {
  if (s->error) return false;
  for (size_t i = 0; i < n; i++) {
    const char c = (char)p[i];
    if (s->in_str) {
      if (s->esc) { s->esc = false; continue; }
      if (c == '\\') { s->esc = true; continue; }
      if (c == '"') {
        s->in_str = false;
        if (s->str_key) { s->key[s->kl] = 0; }
        else {
          s->sv[s->svl] = 0;
          if (s->depth == 2 && !strcmp(s->key, "id"))
            for (uint8_t k = 0; k < s->nids; k++)
              if (!strcmp(s->sv, s->ids[k])) { s->slot = (int8_t)k; break; }
        }
        continue;
      }
      if (s->str_key) { if (s->kl < sizeof(s->key) - 1) s->key[s->kl++] = c; }
      else if (s->svl < sizeof(s->sv) - 1) s->sv[s->svl++] = c;
      continue;
    }
    switch (c) {
      case '"':
        cg_num_end(s);
        s->in_str = true;
        s->str_key = s->depth > 0 && s->ctx[s->depth] == '{' && !s->after_colon;
        if (s->str_key) s->kl = 0; else s->svl = 0;
        break;
      case ':': s->after_colon = true; break;
      case ',': cg_num_end(s); s->after_colon = false; break;
      case '{': case '[':
        if (s->depth + 1 >= (int)sizeof(s->ctx)) { s->error = true; return false; }
        s->depth++;
        s->ctx[s->depth] = c;
        if (c == '{' && s->depth == 3) s->in_sl = !strcmp(s->key, "sparkline_in_7d");
        if (c == '[' && s->depth == 4 && s->in_sl && !strcmp(s->key, "price")) s->in_spark = true;
        if (c == '{' && s->depth == 2) cg_coin_reset(s);
        s->after_colon = false;
        break;
      case '}': case ']':
        cg_num_end(s);
        if (!s->depth || s->ctx[s->depth] != (c == '}' ? '{' : '[')) { s->error = true; return false; }
        if (c == ']' && s->depth == 4) s->in_spark = false;
        if (c == '}' && s->depth == 3) s->in_sl = false;
        if (c == '}' && s->depth == 2) cg_coin_commit(s);
        s->depth--;
        s->after_colon = s->depth > 0 && s->ctx[s->depth] == '{';  // a value just ended inside an object
        break;
      case ' ': case '\n': case '\r': case '\t': cg_num_end(s); break;
      default:
        if (!s->in_num) { s->in_num = true; s->nl = 0; }
        if (s->nl < sizeof(s->num) - 1) s->num[s->nl++] = c;
        break;
    }
  }
  return true;
}
uint8_t cg_scan_take(const CgScan *s, Coin *out, uint8_t cap) {
  uint8_t n = 0;
  for (uint8_t k = 0; k < s->nids && n < cap; k++)
    if (s->got[k]) out[n++] = s->coin[k];
  return n;
}

// -------------------------------------------------------------- cricket --
enum { CS_SEEK = 0, CS_IN, CS_DONE };
enum { EXP_KEY = 0, EXP_COLON, EXP_VAL, EXP_COMMA };

static void cric_copy(char *d, int cap, const char *s) {
  if (cap < 1) return;
  int n = 0;
  for (; s[n] && n < cap - 1; n++) {
    const unsigned char c = (unsigned char)s[n];
    d[n] = (c < 0x20 || c > 0x7e) ? ' ' : (char)c;
  }
  d[n] = 0;
}
static void cric_commit(CricScan *s) {
  s->seen++;
  const bool live = s->cur.started && !s->cur.ended;
  if (live && !s->have_live) {
    if (s->out.n < CRIC_N) {
      if (s->out.n) memmove(&s->out.m[1], &s->out.m[0], s->out.n * sizeof(CricMatch));
      s->out.m[0] = s->cur;
      s->out.n++;
    } else {
      memmove(&s->out.m[1], &s->out.m[0], (CRIC_N - 1) * sizeof(CricMatch));
      s->out.m[0] = s->cur;
    }
    s->have_live = true;
  } else if (s->out.n < CRIC_N) {
    s->out.m[s->out.n++] = s->cur;
  }
  if ((s->out.n >= CRIC_N && s->have_live) || s->seen >= 16) s->st = CS_DONE;
}
static void cric_apply_str(CricScan *s) {
  if (s->teams_d && s->depth == s->teams_d && s->cur.nt < 2) {
    cric_copy(s->cur.team[s->cur.nt], (int)sizeof(s->cur.team[0]), s->val);
    if (s->cur.team[s->cur.nt][0]) s->cur.nt++;
    return;
  }
  if (s->score_obj && s->depth == s->score_obj && strcmp(s->key, "o") == 0) {
    cric_copy(s->so, (int)sizeof(s->so), s->val);
    s->shave = true;
    return;
  }
  if (s->depth == 2 && s->kind[2] == 1) {
    if (strcmp(s->key, "name") == 0) cric_copy(s->cur.name, (int)sizeof(s->cur.name), s->val);
    else if (strcmp(s->key, "matchType") == 0) cric_copy(s->cur.kind, (int)sizeof(s->cur.kind), s->val);
    else if (strcmp(s->key, "status") == 0) cric_copy(s->cur.detail, (int)sizeof(s->cur.detail), s->val);
  }
}
static void cric_apply_tok(CricScan *s) {
  if (s->score_obj && s->depth == s->score_obj) {
    if (strcmp(s->key, "r") == 0) { s->sr = (int16_t)strtol(s->val, nullptr, 10); s->shave = true; }
    else if (strcmp(s->key, "w") == 0) { s->sw = (int16_t)strtol(s->val, nullptr, 10); s->shave = true; }
    else if (strcmp(s->key, "o") == 0) { cric_copy(s->so, (int)sizeof(s->so), s->val); s->shave = true; }
  } else if (s->depth == 2) {
    const bool on = strcmp(s->val, "true") == 0 || strcmp(s->val, "1") == 0;
    if (strcmp(s->key, "matchStarted") == 0) s->cur.started = on;
    else if (strcmp(s->key, "matchEnded") == 0) s->cur.ended = on;
  }
  if (s->depth < 10) s->exp[s->depth] = EXP_COMMA;
}
static void cric_byte(CricScan *s, char c) {
  if (s->st == CS_DONE) return;
  if (s->st == CS_SEEK) {
    static const char pat[] = "\"data\":";
    if (s->si < 7) {
      if (c == pat[s->si]) s->si++;
      else s->si = (c == pat[0]) ? 1 : 0;
    } else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      return;
    } else if (c == '[') {
      s->st = CS_IN;
      s->depth = 1;
      s->kind[1] = 2;
      s->exp[1] = EXP_VAL;
    } else s->si = (c == '"') ? 1 : 0;
    return;
  }
  if (s->tok) {
    if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '{' || c == '[') {
      s->val[s->vl] = 0;
      cric_apply_tok(s);
      s->tok = false;
      s->vl = 0;
    } else {
      if (s->vl + 1 < (int)sizeof(s->val)) s->val[s->vl++] = c;
      return;
    }
  }
  if (s->in_str) {
    if (s->esc) {
      s->esc = false;
      if (s->capturing && s->vl + 1 < (int)sizeof(s->val)) s->val[s->vl++] = (c == 'n' || c == 't') ? ' ' : c;
      return;
    }
    if (c == '\\') { s->esc = true; return; }
    if (c == '"') {
      s->in_str = false;
      s->val[s->vl] = 0;
      if (s->depth < 10 && s->kind[s->depth] == 1 && s->exp[s->depth] == EXP_KEY) {
        cric_copy(s->key, (int)sizeof(s->key), s->val);
        s->exp[s->depth] = EXP_COLON;
      } else if (s->depth < 10 && s->exp[s->depth] == EXP_VAL) {
        cric_apply_str(s);
        s->exp[s->depth] = EXP_COMMA;
      }
      return;
    }
    if (s->capturing && s->vl + 1 < (int)sizeof(s->val)) s->val[s->vl++] = c;
    return;
  }
  if (c == ' ' || c == '\n' || c == '\r' || c == '\t') return;
  if (c == '"') {
    s->in_str = true;
    s->esc = false;
    s->vl = 0;
    s->val[0] = 0;
    s->capturing = s->depth < 10 && (s->exp[s->depth] == EXP_KEY || s->exp[s->depth] == EXP_VAL);
    return;
  }
  if (c == '{') {
    if (s->depth >= 9) { s->st = CS_DONE; return; }
    const uint8_t parent = s->depth;
    s->depth++;
    s->kind[s->depth] = 1;
    s->exp[s->depth] = EXP_KEY;
    if (parent == 1 && s->kind[1] == 2) {
      memset(&s->cur, 0, sizeof(s->cur));
      s->teams_d = s->score_d = s->score_obj = 0;
    }
    if (s->score_d && parent == s->score_d) {
      s->score_obj = s->depth;
      s->sr = s->sw = 0;
      s->so[0] = 0;
      s->shave = false;
    }
    return;
  }
  if (c == '[') {
    if (s->depth >= 9) { s->st = CS_DONE; return; }
    const uint8_t parent = s->depth;
    bool teams = false, score = false;
    if (parent < 10 && s->kind[parent] == 1 && s->exp[parent] == EXP_VAL) {
      teams = strcmp(s->key, "teams") == 0;
      score = strcmp(s->key, "score") == 0;
      s->exp[parent] = EXP_COMMA;
    }
    s->depth++;
    s->kind[s->depth] = 2;
    s->exp[s->depth] = EXP_VAL;
    if (teams) s->teams_d = s->depth;
    if (score) s->score_d = s->depth;
    return;
  }
  if (c == ':') {
    if (s->depth < 10 && s->kind[s->depth] == 1 && s->exp[s->depth] == EXP_COLON) s->exp[s->depth] = EXP_VAL;
    return;
  }
  if (c == ',') {
    if (s->depth < 10) s->exp[s->depth] = s->kind[s->depth] == 1 ? (uint8_t)EXP_KEY : (uint8_t)EXP_VAL;
    return;
  }
  if (c == '}' || c == ']') {
    if (c == '}' && s->score_obj && s->depth == s->score_obj) {
      if (s->shave && s->cur.ni < 2) {
        s->cur.inn[s->cur.ni].r = s->sr;
        s->cur.inn[s->cur.ni].w = s->sw;
        cric_copy(s->cur.inn[s->cur.ni].o, (int)sizeof(s->cur.inn[0].o), s->so);
        s->cur.ni++;
      }
      s->score_obj = 0;
    }
    if (c == '}' && s->depth == 2 && s->kind[2] == 1) cric_commit(s);
    if (c == ']' && s->depth == s->teams_d) s->teams_d = 0;
    if (c == ']' && s->depth == s->score_d) s->score_d = 0;
    if (s->depth) s->depth--;
    if (s->depth == 0) s->st = CS_DONE;
    return;
  }
  if (s->depth < 10 && s->exp[s->depth] == EXP_VAL &&
      (c == 't' || c == 'f' || c == 'n' || c == '-' || (c >= '0' && c <= '9'))) {
    s->tok = true;
    s->vl = 0;
    s->val[s->vl++] = c;
    s->val[s->vl] = 0;
  }
}
void cric_scan_init(CricScan *s) { memset(s, 0, sizeof(*s)); }
bool cric_scan_feed(CricScan *s, const uint8_t *p, size_t n) {
  if (!s || s->st == CS_DONE) return false;
  for (size_t i = 0; i < n; i++) {
    cric_byte(s, (char)p[i]);
    if (s->st == CS_DONE) return false;
  }
  return true;
}
void cric_scan_take(const CricScan *s, CricketData *out) {
  if (!s || !out) return;
  *out = s->out;
  out->valid = out->n > 0;
}
