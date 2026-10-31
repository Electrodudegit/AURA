// proto_test.cpp - host tests for net_proto (gate, header scan, chunked, JSON string
// extractor, SSE parser, reply cap). Synthetic samples only; no network, no keys.
// Every stream is fed in many random splits (1..N bytes) to exercise state carried across chunks.
#include "net_proto.h"
#include "comp_proto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::string chunked(const std::string &body, unsigned seed) {
  std::string o;
  srand(seed);
  size_t i = 0;
  while (i < body.size()) {
    size_t k = 1 + rand() % 40;
    if (i + k > body.size()) k = body.size() - i;
    char h[32];
    snprintf(h, sizeof(h), (rand() & 1) ? "%zx\r\n" : "%zX;ext=1\r\n", k);
    o += h;
    o += body.substr(i, k);
    o += "\r\n";
    i += k;
  }
  o += "0\r\n\r\n";
  return o;
}
struct Result { uint16_t status; bool chunked; int32_t ra; std::string text; bool done; bool err; bool full; };
// Full pipeline exactly as net_task uses it (ors_feed), fed in random splits.
static Result run(const std::string &wire, bool sse, unsigned seed) {
  static OrStream os;
  ors_init(&os, sse);
  srand(seed);
  size_t i = 0;
  uint8_t buf[512];
  while (i < wire.size()) {
    size_t k = 1 + rand() % 97;
    if (i + k > wire.size()) k = wire.size() - i;
    memcpy(buf, wire.data() + i, k);
    i += k;
    if (ors_feed(&os, buf, k)) break;
  }
  reply_finish(&os.r);
  return {os.h.status, os.h.chunked, os.h.retry_after_s, std::string(os.r.s, os.r.n), ors_complete(&os), os.ck.error, os.r.full};
}
static std::string sse_body(const std::vector<std::string> &toks, bool reasoning) {
  std::string b = ": comment line\n\n";
  b += "data: {\"id\":\"x\",\"object\":\"chat.completion.chunk\",\"model\":\"grok-3-mini\",\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\",\"content\":\"\"}}]}\n\n";
  if (reasoning)
    b += "data: {\"choices\":[{\"index\":0,\"delta\":{\"reasoning_content\":\"thinking \\\"hard\\\"\"}}]}\n\n";
  for (auto &t : toks) {
    b += "data: {\"id\":\"x\",\"object\":\"chat.completion.chunk\",\"created\":1,\"choices\":[{\"index\":0,\"delta\":{\"content\":\"" + t +
         "\"},\"finish_reason\":null}],\"system_fingerprint\":\"fp\"}\r\n\r\n";
  }
  b += "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":20,\"completion_tokens\":30}}\n\n";
  b += "data: [DONE]\n\n";
  b += "data: {\"choices\":[{\"delta\":{\"content\":\"AFTER-DONE\"}}]}\n\n";
  return b;
}
int main() {
  // ---- gate
  CHECK(gate_decide(40000, 0, 60000, 51200) == GATE_OK_SINGLE, "single");
  CHECK(gate_decide(36864, 0, 60000, 51200) == GATE_OK_SINGLE, "single edge");
  CHECK(gate_decide(36863, 17408, 60000, 51200) == GATE_OK_DUAL, "dual edge");
  CHECK(gate_decide(30000, 17407, 60000, 51200) == GATE_SKIP_BLOCKS, "dual short");
  CHECK(gate_decide(17407, 17407, 60000, 51200) == GATE_SKIP_BLOCKS, "both short");
  CHECK(gate_decide(90000, 90000, 51199, 51200) == GATE_SKIP_FREE, "floor");
  // v3.1.3 per-host floors (measured handshake peak + RX reserve + margin)
  CHECK(tls_floor("api.groq.com") == 46336u + TLS_RX_RESERVE + TLS_MARGIN, "groq floor");
  CHECK(tls_floor("api.groq.com") == 55040u, "groq floor value");
  CHECK(tls_floor("api.x.ai") == 55040u, "xai floor value");
  CHECK(tls_floor("api.open-meteo.com") == 63232u, "open-meteo floor");
  CHECK(tls_floor("air-quality-api.open-meteo.com") == 63232u, "air floor");
  CHECK(tls_floor("api.coingecko.com") == 61952u, "coingecko floor");
  CHECK(tls_floor("feeds.bbci.co.uk") == 59392u, "bbc floor");
  CHECK(tls_floor("unknown.example") == TLS_PEAK_DEFAULT + TLS_RX_RESERVE + TLS_MARGIN, "default floor");
  CHECK(tls_floor(nullptr) == TLS_PEAK_DEFAULT + TLS_RX_RESERVE + TLS_MARGIN, "null host floor");
  CHECK(gate_decide(40000, 0, 55040, tls_floor("api.groq.com")) == GATE_OK_SINGLE, "groq at floor");
  CHECK(gate_decide(40000, 0, 55039, tls_floor("api.groq.com")) == GATE_SKIP_FREE, "groq under floor");
  { uint32_t a = 0, b = 0; uint32_t v[] = {100, 20000, 5, 40000, 18000, 40000};
    for (uint32_t x : v) top2_add(x, &a, &b);
    CHECK(a == 40000 && b == 40000, "top2 %u %u", a, b); }
  // ---- SSE, chunked HTTP/1.1, many splits
  const std::vector<std::string> toks = {"Hello", " there", "! It\\u2019s", " 27\\u00b0C", " \\u2014 wear", " linen", " \\ud83d\\ude00", " caf\xc3\xa9", ".\\nBye", " \\\"now\\\""};
  const std::string expect = "Hello there! It's 27\x7f" "C - wear linen cafe. Bye \"now\"";
  const std::string sse = sse_body(toks, true);
  const std::string hdr11 = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTRANSFER-ENCODING: chunked\r\nX-Long: " + std::string(300, 'z') + "\r\n\r\n";
  for (unsigned seed = 1; seed <= 400; seed++) {
    Result r = run(hdr11 + chunked(sse, seed), true, seed * 7);
    CHECK(r.status == 200 && r.chunked && r.done && !r.err, "sse flags seed %u", seed);
    CHECK(r.text == expect, "sse text seed %u: [%s]", seed, r.text.c_str());
    if (r.text != expect) break;
  }
  // SSE without chunked (HTTP/1.0 style)
  { Result r = run("HTTP/1.0 200 OK\r\nContent-Type: text/event-stream\r\n\r\n" + sse, true, 3);
    CHECK(!r.chunked && r.done && r.text == expect, "sse plain [%s]", r.text.c_str()); }
  // ---- one-shot JSON (HTTP/1.0, Content-Length), reasoning before content, nested quotes
  const std::string js = "{\"id\":\"a\",\"object\":\"chat.completion\",\"model\":\"grok-3-mini\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\","
                         "\"reasoning_content\":\"The \\\"content\\\": trap\",\"content\":\"Take an umbrella \\u2013 rain at 5\\u00a0pm.\",\"refusal\":null},"
                         "\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":9}}";
  for (unsigned seed = 1; seed <= 300; seed++) {
    char cl[64]; snprintf(cl, sizeof(cl), "Content-Length: %zu\r\n", js.size());
    Result r = run(std::string("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n") + cl + "\r\n" + js, false, seed);
    CHECK(r.done && r.text == "Take an umbrella - rain at 5 pm.", "json seed %u [%s]", seed, r.text.c_str());
    // the same body chunked
    Result r2 = run("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" + chunked(js, seed), false, seed + 1);
    CHECK(r2.done && r2.text == r.text, "json chunked seed %u", seed);
  }
  // ---- 429 / 401 headers
  { Result r = run("HTTP/1.1 429 Too Many Requests\r\nretry-after: 17\r\nContent-Length: 2\r\n\r\n{}", false, 1);
    CHECK(r.status == 429 && r.ra == 17, "429 %u %d", r.status, r.ra); }
  { Result r = run("HTTP/1.1 429 Too Many Requests\r\nRetry-After: Wed, 21 Oct 2026 07:28:00 GMT\r\n\r\n", false, 1);
    CHECK(r.status == 429 && r.ra == -1, "429 date form"); }
  { Result r = run("HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Incorrect API key provided: xa***\"}", false, 1);
    CHECK(r.status == 401 && r.text.empty(), "401 body must not become reply text"); }
  // ---- reply cap at a word boundary
  {
    std::string longt;
    for (int i = 0; i < 160; i++) longt += "word" + std::to_string(i % 10) + " ";
    std::vector<std::string> t2 = {longt};
    for (unsigned seed = 1; seed <= 50; seed++) {
      Result r = run(hdr11 + chunked(sse_body(t2, false), seed), true, seed);
      CHECK(r.full, "cap flag");
      CHECK(r.text.size() <= ORACLE_REPLY_MAX + 1 && r.text.size() > 480, "cap size %zu", r.text.size());
      CHECK((unsigned char)r.text.back() == 0x81 && r.text[r.text.size() - 2] != ' ', "ellipsis");
      const std::string body = r.text.substr(0, r.text.size() - 1);
      CHECK(body.substr(body.size() - 5, 4) == "word", "word boundary [%s]", body.substr(body.size() - 8).c_str());
    }
  }
  // ---- malformed chunked
  { ChunkDec c; chunk_init(&c); uint8_t b[] = "zz\r\nabc"; chunk_feed(&c, b, 7); CHECK(c.error, "bad hex"); }
  { ChunkDec c; chunk_init(&c); uint8_t b[] = "1000000\r\n"; chunk_feed(&c, b, 9); CHECK(c.error, "too big"); }
  // ---- json_escape
  { char o[64]; CHECK(json_escape("a\"b\\c\nd\x7f", o, sizeof(o)) && !strcmp(o, "a\\\"b\\\\c\\nd\\u00b0"), "esc [%s]", o);
    char s[8]; CHECK(!json_escape("0123456789", s, sizeof(s)), "esc overflow"); }
  // ---- v3.1.2: Groq (api.groq.com/openai/v1, OpenAI-compatible) response shapes, synthetic
  {
    auto gchunk = [](const std::string &delta, const char *fin, bool xg) {
      return std::string("data: {\"id\":\"chatcmpl-0a1b\",\"object\":\"chat.completion.chunk\",\"created\":1760000000,"
                         "\"model\":\"llama-3.1-8b-instant\",\"system_fingerprint\":\"fp_0000\",\"choices\":[{\"index\":0,\"delta\":") +
             delta + ",\"logprobs\":null,\"finish_reason\":" + fin + "}]" +
             (xg ? ",\"x_groq\":{\"id\":\"req_01\",\"usage\":{\"queue_time\":0.02,\"prompt_tokens\":40,\"completion_tokens\":12,"
                   "\"total_time\":0.05}}" : "") + "}\n\n";
    };
    std::string gs = gchunk("{\"role\":\"assistant\",\"content\":\"\"}", "null", true);
    const std::vector<std::string> gt = {"Wear", " a light", " cotton shirt", " \\u2013 31\\u00b0C", ", rain", " after 5\\u00a0pm."};
    for (auto &x : gt) gs += gchunk("{\"content\":\"" + x + "\"}", "null", false);
    gs += gchunk("{}", "\"stop\"", true);
    gs += "data: [DONE]\n\n";
    const std::string gexp = "Wear a light cotton shirt - 31\x7f" "C, rain after 5 pm.";
    const std::string gh = "HTTP/1.1 200 OK\r\nDate: Tue, 20 Oct 2026 18:00:00 GMT\r\nContent-Type: text/event-stream\r\n"
                           "Transfer-Encoding: chunked\r\nCache-Control: no-cache\r\nx-groq-region: bom\r\n"
                           "x-ratelimit-remaining-requests: 14399\r\nCF-RAY: 0000-BOM\r\n\r\n";
    for (unsigned seed = 1; seed <= 300; seed++) {
      Result r = run(gh + chunked(gs, seed), true, seed * 5 + 1);
      CHECK(r.status == 200 && r.chunked && r.done && !r.err, "groq sse flags seed %u", seed);
      CHECK(r.text == gexp, "groq sse text seed %u: [%s]", seed, r.text.c_str());
      if (r.text != gexp) break;
    }
    // one-shot (HTTP/1.0, Content-Length) with x_groq and usage after the message
    const std::string gj = "{\"id\":\"chatcmpl-0a1b\",\"object\":\"chat.completion\",\"created\":1760000000,\"model\":\"llama-3.1-8b-instant\","
                           "\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"Carry an umbrella \\u2013 60% rain "
                           "after 5\\u00a0pm.\"},\"logprobs\":null,\"finish_reason\":\"stop\"}],\"usage\":{\"queue_time\":0.02,"
                           "\"prompt_tokens\":40,\"prompt_time\":0.003,\"completion_tokens\":12,\"completion_time\":0.01,\"total_tokens\":52,"
                           "\"total_time\":0.013},\"usage_breakdown\":null,\"system_fingerprint\":\"fp_0000\",\"x_groq\":{\"id\":\"req_01\"},"
                           "\"service_tier\":\"on_demand\"}";
    for (unsigned seed = 1; seed <= 300; seed++) {
      char cl[64]; snprintf(cl, sizeof(cl), "Content-Length: %zu\r\n", gj.size());
      Result r = run(std::string("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n") + cl + "\r\n" + gj, false, seed);
      CHECK(r.done && r.text == "Carry an umbrella - 60% rain after 5 pm.", "groq json seed %u [%s]", seed, r.text.c_str());
      Result r2 = run("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" + chunked(gj, seed), false, seed + 3);
      CHECK(r2.done && r2.text == r.text, "groq json chunked seed %u", seed);
    }
    // errors: the body must never become reply text; 429 retry-after is honoured
    { Result r = run("HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\n\r\n{\"error\":{\"message\":\"Invalid API Key\","
                     "\"type\":\"invalid_request_error\",\"code\":\"invalid_api_key\"}}", false, 2);
      CHECK(r.status == 401 && r.text.empty(), "groq 401"); }
    { Result r = run("HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\n\r\n{\"error\":{\"message\":\"Forbidden\"}}", true, 2);
      CHECK(r.status == 403 && r.text.empty(), "groq 403"); }
    { Result r = run("HTTP/1.1 429 Too Many Requests\r\nretry-after: 7\r\nx-ratelimit-reset-requests: 2m59.56s\r\n"
                     "Content-Type: application/json\r\n\r\n{\"error\":{\"message\":\"Rate limit reached for model `llama-3.1-8b-instant`\","
                     "\"type\":\"tokens\",\"code\":\"rate_limit_exceeded\"}}", true, 4);
      CHECK(r.status == 429 && r.ra == 7 && r.text.empty(), "groq 429 %u %d", r.status, r.ra); }
    { Result r = run("HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"error\":{\"message\":\"The model `x` does not exist\","
                     "\"code\":\"model_not_found\"}}", false, 5);
      CHECK(r.status == 404 && r.text.empty(), "groq 404"); }
    // the 600-char cap applies to Groq streams too
    std::string lg;
    for (int i = 0; i < 160; i++) lg += "word" + std::to_string(i % 10) + " ";
    std::string gl = gchunk("{\"role\":\"assistant\",\"content\":\"\"}", "null", false) + gchunk("{\"content\":\"" + lg + "\"}", "null", false) +
                     gchunk("{}", "\"stop\"", true) + "data: [DONE]\n\n";
    for (unsigned seed = 1; seed <= 30; seed++) {
      Result r = run(gh + chunked(gl, seed), true, seed);
      CHECK(r.full && r.text.size() <= ORACLE_REPLY_MAX + 1 && (unsigned char)r.text.back() == 0x81, "groq cap seed %u size %zu", seed, r.text.size());
    }
  }
  // ---- v3.1.2: request body (shared by xAI and Groq); Groq never gets reasoning_effort
  {
    char b[1280];
    int n = or_build_body(b, sizeof(b), "llama-3.1-8b-instant", "Be brief.", "Hi \\\"there\\\"", 200, "0.7", true, false);
    CHECK(n > 0 && n < (int)sizeof(b), "body len");
    CHECK(!strcmp(b, "{\"model\":\"llama-3.1-8b-instant\",\"messages\":[{\"role\":\"system\",\"content\":\"Be brief.\"},{\"role\":\"user\","
                     "\"content\":\"Hi \\\"there\\\"\"}],\"max_tokens\":200,\"temperature\":0.7,\"stream\":true}"), "groq body [%s]", b);
    CHECK(!strstr(b, "reasoning_effort"), "groq: no reasoning_effort");
    n = or_build_body(b, sizeof(b), "grok-3-mini", "S", "P", 200, "0.7", false, true);
    CHECK(strstr(b, ",\"stream\":false,\"reasoning_effort\":\"low\"}") != nullptr, "xai body [%s]", b);
    char s[16];
    CHECK(or_build_body(s, sizeof(s), "m", "s", "p", 1, "0", false, false) >= (int)sizeof(s), "body overflow detected");
  }
  // ---- v3.1.2: provider resolution (manual wins; auto = xAI if its key is set, else Groq)
  CHECK(ora_resolve(0, true) == 0 && ora_resolve(0, false) == 1, "auto");
  CHECK(ora_resolve(1, false) == 0 && ora_resolve(1, true) == 0, "manual xai");
  CHECK(ora_resolve(2, true) == 1 && ora_resolve(2, false) == 1, "manual groq");
  // ---- v3.1.6 p0p1: Gemini (manual 3 wins; auto prefers Gemini when its key is set)
  CHECK(ora_resolve(3, false, false) == 2 && ora_resolve(3, true, false) == 2, "manual gemini");
  CHECK(ora_resolve(0, true, true) == 2 && ora_resolve(0, false, true) == 2, "auto prefers gemini");
  CHECK(ora_resolve(0, true, false) == 0 && ora_resolve(0, false, false) == 1, "auto without gemini unchanged");
  CHECK(ora_resolve(1, true, true) == 0 && ora_resolve(2, true, true) == 1, "manual xai/groq beat a gemini key");
  CHECK(!strcmp(ora_api_name(ORA_GEMINI), "GEMINI") && !strcmp(ora_api_label(ORA_GEMINI), "Gemini"), "gemini names");
  {
    char b[640];
    int n = gm_build_body(b, sizeof(b), "gemini-2.5-flash", "Be brief.", "Hi \\\"there\\\"", 200, "0.7");
    CHECK(n > 0 && n < (int)sizeof(b), "gm body len");
    CHECK(!strcmp(b, "{\"systemInstruction\":{\"parts\":[{\"text\":\"Be brief.\"}]},\"contents\":[{\"role\":\"user\",\"parts\":"
                     "[{\"text\":\"Hi \\\"there\\\"\"}]}],\"generationConfig\":{\"maxOutputTokens\":200,\"temperature\":0.7,"
                     "\"thinkingConfig\":{\"thinkingBudget\":0}}}"), "gm body [%s]", b);
    gm_build_body(b, sizeof(b), "gemini-3.5-flash-lite", "S", "P", 200, "0.7");
    CHECK(strstr(b, "\"thinkingConfig\":{\"thinkingLevel\":\"minimal\"}}}") != nullptr, "gm 3.x thinkingLevel [%s]", b);
    gm_build_body(b, sizeof(b), "gemini-2.5-pro", "S", "P", 200, "0.7");
    CHECK(!strstr(b, "thinking") && !strstr(b, "x-goog") && !strstr(b, "key"), "gm pro: no thinking config, no key in body");
    char sm[24];
    CHECK(gm_build_body(sm, sizeof(sm), "gemini-2.5-flash", "s", "p", 1, "0") >= (int)sizeof(sm), "gm overflow detected");
  }
  {
    // Shape of a real generateContent reply (pretty-printed, HTTP/1.0 one-shot, thoughtSignature after text).
    const std::string body =
        "{\n  \"candidates\": [\n    {\n      \"content\": {\n        \"parts\": [\n          {\n            \"text\": "
        "\"Paris is the capital of France.\\nIt sits on the \\\"Seine\\\".\",\n            \"thoughtSignature\": \"Q2xhdWRl\"\n"
        "          },\n          {\n            \"text\": \"SECOND-PART\"\n          }\n        ],\n        \"role\": \"model\"\n"
        "      },\n      \"finishReason\": \"STOP\",\n      \"index\": 0\n    }\n  ],\n  \"usageMetadata\": {\n"
        "    \"promptTokenCount\": 12,\n    \"candidatesTokenCount\": 9,\n    \"totalTokenCount\": 21\n  },\n"
        "  \"modelVersion\": \"gemini-2.5-flash\",\n  \"responseId\": \"abc\"\n}\n";
    const std::string wire = "HTTP/1.0 200 OK\r\nContent-Type: application/json; charset=UTF-8\r\nVary: Origin\r\n\r\n" + body;
    for (unsigned seed = 1; seed <= 60; seed++) {
      static OrStream os;
      ors_init(&os, true /* ignored for gemini */, true);
      srand(seed);
      size_t i = 0;
      uint8_t buf[128];
      while (i < wire.size()) {
        size_t k = 1 + rand() % 97;
        if (i + k > wire.size()) k = wire.size() - i;
        memcpy(buf, wire.data() + i, k);
        i += k;
        if (ors_feed(&os, buf, k)) break;
      }
      reply_finish(&os.r);
      CHECK(os.h.status == 200 && !os.sse && ors_complete(&os) && !os.key_invalid, "gemini ok seed %u", seed);
      CHECK(std::string(os.r.s, os.r.n) == "Paris is the capital of France. It sits on the \"Seine\".", "gemini text seed %u [%s]", seed, os.r.s);
    }
    // OpenAI-style stream must not be affected by the ws flag (exact matching stays exact)
    static OrStream o2;
    ors_init(&o2, false);
    CHECK(!o2.js.ws && !o2.gemini, "openai path keeps exact jstr");
  }
  {
    // Bad key: Gemini answers 400 (not 401) with reason API_KEY_INVALID; must map to key-invalid without storing text.
    std::string err = "{\n  \"error\": {\n    \"code\": 400,\n    \"message\": \"API key not valid. Please pass a valid API key.\",\n"
                      "    \"status\": \"INVALID_ARGUMENT\",\n    \"details\": [\n      {\n        \"@type\": \"type.googleapis.com/google.rpc.ErrorInfo\",\n"
                      "        \"reason\": \"API_KEY_INVALID\",\n        \"domain\": \"googleapis.com\"\n      }\n    ]\n  }\n}\n";
    for (unsigned seed = 1; seed <= 30; seed++) {
      for (int ck = 0; ck < 2; ck++) {
        const std::string wire = ck ? "HTTP/1.1 400 Bad Request\r\nTransfer-Encoding: chunked\r\n\r\n" + chunked(err, seed)
                                    : "HTTP/1.0 400 Bad Request\r\nContent-Type: application/json\r\n\r\n" + err;
        static OrStream os;
        ors_init(&os, false, true);
        srand(seed);
        size_t i = 0;
        uint8_t buf[128];
        bool stop = false;
        while (i < wire.size() && !stop) {
          size_t k = 1 + rand() % 50;
          if (i + k > wire.size()) k = wire.size() - i;
          memcpy(buf, wire.data() + i, k);
          i += k;
          stop = ors_feed(&os, buf, k);
        }
        CHECK(os.h.status == 400 && os.key_invalid && os.r.n == 0, "gemini 400 key invalid seed %u ck %d", seed, ck);
      }
    }
    static OrStream os;
    ors_init(&os, false, true);
    std::string w = "HTTP/1.0 429 Too Many Requests\r\n\r\n{\"error\":{\"code\":429,\"status\":\"RESOURCE_EXHAUSTED\"}}";
    ors_feed(&os, (uint8_t *)&w[0], w.size());
    CHECK(os.h.status == 429 && !os.key_invalid && os.r.n == 0, "gemini 429 not key-invalid");
  }
  // ---- v3.1.6 p0p1: Alpha Vantage GLOBAL_QUOTE (shape of a live 200; AV signals limits with 200 + Information)
  {
    float pr = 0, ch = 0;
    const char *ok = "{\n    \"Global Quote\": {\n        \"01. symbol\": \"AAPL\",\n        \"02. open\": \"262.0000\",\n"
                     "        \"05. price\": \"263.4500\",\n        \"06. volume\": \"38101234\",\n        \"07. latest trading day\": \"2026-10-23\",\n"
                     "        \"08. previous close\": \"262.2700\",\n        \"09. change\": \"1.1800\",\n        \"10. change percent\": \"0.4499%\"\n    }\n}";
    CHECK(av_quote_parse(ok, &pr, &ch) == AVQ_OK && pr > 263.44f && pr < 263.46f && ch > 0.449f && ch < 0.4501f, "av ok %f %f", pr, ch);
    CHECK(av_quote_parse("{\n    \"Information\": \"We have detected your API key as ... 25 requests per day.\"\n}", &pr, &ch) == AVQ_RATE, "av quota");
    CHECK(av_quote_parse("{\"Note\": \"Thank you for using Alpha Vantage! ... 1 request per second.\"}", &pr, &ch) == AVQ_RATE, "av burst");
    CHECK(av_quote_parse("{\"Error Message\": \"the parameter apikey is invalid or missing.\"}", &pr, &ch) == AVQ_KEY, "av key");
    CHECK(av_quote_parse("{\"Error Message\": \"Invalid API call. Please retry or visit the documentation.\"}", &pr, &ch) == AVQ_PARSE, "av bad call");
    CHECK(av_quote_parse("{\n    \"Global Quote\": {}\n}", &pr, &ch) == AVQ_PARSE, "av empty quote");
    CHECK(av_quote_parse("", &pr, &ch) == AVQ_PARSE && av_quote_parse(nullptr, &pr, &ch) == AVQ_PARSE, "av empty");
  }
  // ---- v3.1.6 p0p1: cricapi currentMatches live body (2026-10-26, 20.7 KB, apikey replaced by <REDACTED>)
  {
    std::string j;
    if (FILE *f = fopen("cric_live.json", "rb")) { char t[4096]; size_t k; while ((k = fread(t, 1, sizeof(t), f)) > 0) j.append(t, k); fclose(f); }
    CHECK(j.size() > 20000 && j.find("<REDACTED>") != std::string::npos, "cric fixture present + redacted");
    for (unsigned seed = 1; seed <= 20 && !j.empty(); seed++) {
      static CricScan sc;
      cric_scan_init(&sc);
      srand(seed);
      size_t i = 0;
      bool stop = false;
      while (i < j.size() && !stop) {
        size_t k = 1 + rand() % 512;
        if (i + k > j.size()) k = j.size() - i;
        stop = !cric_scan_feed(&sc, (const uint8_t *)j.data() + i, k);
        i += k;
      }
      CricketData c;
      cric_scan_take(&sc, &c);
      CHECK(stop && i < j.size() && c.valid && c.n == CRIC_N, "cric live: stops early with %u matches seed %u", c.n, seed);
      CHECK(!strcmp(c.m[0].kind, "t20") && c.m[0].nt == 2 && !strcmp(c.m[0].team[0], "Mexico Women") && c.m[0].ended && c.m[0].started,
            "cric live m0 fields");
      CHECK(c.m[0].ni == 2 && c.m[0].inn[0].r == 186 && c.m[0].inn[0].w == 7 && !strcmp(c.m[0].inn[0].o, "20") &&
                c.m[0].inn[1].r == 51 && c.m[0].inn[1].w == 10 && !strcmp(c.m[0].inn[1].o, "12.5"), "cric live m0 innings");
      CHECK(!strcmp(c.m[0].detail, "Argentina Women won by 135 runs"), "cric live detail [%s]", c.m[0].detail);
    }
  }
  // ---- v3.1.6 p0p1: CoinGecko markets + sparkline, streamed (live fixture cg_markets.json, 168 pts/coin)
  {
    static const char *const ids[4] = {"bitcoin", "ethereum", "solana", "ripple"};
    std::string j;
    if (FILE *f = fopen("cg_markets.json", "rb")) { char t[4096]; size_t k; while ((k = fread(t, 1, sizeof(t), f)) > 0) j.append(t, k); fclose(f); }
    CHECK(j.size() > 15000, "cg fixture present (%zu)", j.size());
    for (unsigned seed = 1; seed <= 40 && !j.empty(); seed++) {
      static CgScan sc;
      cg_scan_init(&sc, ids, 4);
      srand(seed);
      size_t i = 0;
      bool ok = true;
      while (i < j.size() && ok) {
        size_t k = 1 + rand() % 700;
        if (i + k > j.size()) k = j.size() - i;
        ok = cg_scan_feed(&sc, (const uint8_t *)j.data() + i, k);
        i += k;
      }
      Coin c[4];
      const uint8_t n = cg_scan_take(&sc, c, 4);
      CHECK(ok && !sc.error && n == 4 && sc.depth == 0, "cg scan seed %u n=%u", seed, n);
      if (n != 4) continue;
      // id-table order (BTC, ETH, SOL, XRP) although the API orders by market cap (XRP before SOL)
      CHECK(c[0].price == 79910.0f && c[1].price > 2314.35f && c[1].price < 2314.37f && c[2].price > 101.25f && c[2].price < 101.27f &&
                c[3].price > 1.289f && c[3].price < 1.291f, "cg prices/order seed %u", seed);
      CHECK(c[0].chg < -0.405f && c[0].chg > -0.407f && c[2].chg > 0.414f && c[2].chg < 0.4142f, "cg chg");
      CHECK(c[0].vol > 2.2e10f && c[0].hi24 == 80825.0f && c[0].lo24 == 79332.0f, "cg vol/hi/lo");
      bool sp = true;
      for (int q = 0; q < 4; q++) {
        if (c[q].ns != CRYPTO_SPARK_N || !(c[q].spark_hi > c[q].spark_lo)) sp = false;
        uint8_t mn = 255, mx = 0;
        for (int t = 0; t < CRYPTO_SPARK_N; t++) { if (c[q].spark[t] < mn) mn = c[q].spark[t]; if (c[q].spark[t] > mx) mx = c[q].spark[t]; }
        if (mn != 0 || mx != 255) sp = false;
      }
      CHECK(sp, "cg sparks 24 pts normalised");
      CHECK(c[0].spark_lo > 79489.8f && c[0].spark_lo < 79489.9f && c[0].spark_hi > 80718.9f && c[0].spark_hi < 80719.0f, "cg btc spark range = last 24 pts");
      // last point 80312.41 -> (80312.41-79489.87)/(80718.95-79489.87)*255 = 170.6
      CHECK(c[0].spark[CRYPTO_SPARK_N - 1] == 171, "cg btc newest point %u", c[0].spark[CRYPTO_SPARK_N - 1]);
    }
    // partial / short sparkline, unknown id, nulls, escaped strings
    static CgScan sc;
    cg_scan_init(&sc, ids, 4);
    const char *mini = "[{\"id\":\"dogecoin\",\"current_price\":0.1},{\"id\":\"ethereum\",\"name\":\"E\\\"th}\",\"current_price\":2000.5,"
                       "\"price_change_percentage_24h\":null,\"roi\":{\"times\":1.5,\"price\":[9]},\"sparkline_in_7d\":{\"price\":[1,2,3]}}]";
    CHECK(cg_scan_feed(&sc, (const uint8_t *)mini, strlen(mini)) && !sc.error, "cg mini feed");
    Coin c[4];
    const uint8_t n = cg_scan_take(&sc, c, 4);
    CHECK(n == 1 && c[0].price == 2000.5f && c[0].chg == 0 && c[0].ns == 3 && c[0].spark[0] == 0 && c[0].spark[2] == 255 &&
              c[0].spark_lo == 1 && c[0].spark_hi == 3, "cg mini n=%u", n);
    cg_scan_init(&sc, ids, 4);
    CHECK(!cg_scan_feed(&sc, (const uint8_t *)"[{]", 3) && sc.error, "cg framing error");
    cg_scan_init(&sc, ids, 4);
    CHECK(!cg_scan_feed(&sc, (const uint8_t *)"[[[[[[[1]]]]]]]", 15) && sc.error, "cg depth cap");
  }
  // ---- Twelve Data quotes: quoted numbers, a per-symbol error, a top-level error
  {
    const char *j =
        "{\"AAPL\":{\"symbol\":\"AAPL\",\"name\":\"Apple Inc\",\"close\":\"189.30\",\"percent_change\":\"1.25\","
        "\"previous_close\":\"186.90\"},\"MSFT\":{\"symbol\":\"MSFT\",\"name\":\"Microsoft Corporation\","
        "\"close\":\"420.10\",\"percent_change\":\"-0.50\"},\"GOOGL\":{\"code\":404,\"message\":\"not found\","
        "\"status\":\"error\"},\"AMZN\":{\"symbol\":\"AMZN\",\"name\":\"Amazon\",\"close\": \"178.00\","
        "\"percent_change\":\"0.10\"}}";
    StocksData s;
    CHECK(stock_parse(j, &s) && s.n == 3, "stocks n %u", s.n);
    CHECK(strcmp(s.row[0].sym, "AAPL") == 0 && s.row[0].price > 189.2f && s.row[0].price < 189.4f, "aapl price");
    CHECK(s.row[0].chg > 1.2f && s.row[0].chg < 1.3f, "aapl chg");
    CHECK(strcmp(s.row[1].sym, "MSFT") == 0 && s.row[1].chg < -0.4f && s.row[1].chg > -0.6f, "msft chg");
    CHECK(strcmp(s.row[2].sym, "AMZN") == 0 && s.row[2].price > 177.9f && s.row[2].price < 178.1f, "amzn");
    CHECK(strstr(s.row[0].name, "Apple") != nullptr, "aapl name");
    StocksData bad;
    CHECK(!stock_parse("{\"code\":401,\"message\":\"Invalid API key\",\"status\":\"error\"}", &bad) && bad.n == 0, "stocks err");
  }
  // ---- cricapi stream: apikey is echoed and must not land in any field; live match is featured
  {
    const char *j =
        "{\"apikey\":\"SUPERSECRETKEY99\",\"data\":["
        "{\"id\":\"a\",\"name\":\"Old finished\",\"matchType\":\"odi\",\"status\":\"Alpha won\","
        "\"teams\":[\"Alpha\",\"Beta\"],\"score\":[{\"r\":200,\"w\":10,\"o\":50,\"inning\":\"Alpha\"}],"
        "\"matchStarted\":true,\"matchEnded\":true},"
        "{\"id\":\"b\",\"name\":\"Live game\",\"matchType\":\"t20\",\"status\":\"Delta need 12\","
        "\"teams\":[\"Gamma\",\"Delta\"],\"score\":[{\"r\":160,\"w\":4,\"o\":18.2,\"inning\":\"G\"}],"
        "\"matchStarted\":true,\"matchEnded\":false},"
        "{\"id\":\"c\",\"name\":\"Upcoming\",\"matchType\":\"test\",\"status\":\"Starts tomorrow\","
        "\"teams\":[\"Echo\",\"Foxtrot\"],\"score\":[],\"matchStarted\":false,\"matchEnded\":false}"
        "],\"status\":\"success\",\"info\":{\"hitsToday\":1}}";
    CHECK(strstr(j, "SUPERSECRETKEY99") != nullptr, "fixture echoes apikey");
    auto leaked = [](const CricketData &c) {
      for (int i = 0; i < c.n; i++) {
        const CricMatch &m = c.m[i];
        if (strstr(m.name, "SUPERSECRETKEY99") || strstr(m.detail, "SUPERSECRETKEY99") || strstr(m.kind, "SUPERSECRETKEY99") ||
            strstr(m.team[0], "SUPERSECRETKEY99") || strstr(m.team[1], "SUPERSECRETKEY99") || strstr(m.inn[0].o, "SUPERSECRETKEY99") ||
            strstr(m.inn[1].o, "SUPERSECRETKEY99"))
          return true;
      }
      return false;
    };
    for (unsigned seed = 1; seed <= 20; seed++) {
      CricScan sc;
      cric_scan_init(&sc);
      srand(seed);
      size_t i = 0;
      bool stop = false;
      while (i < strlen(j) && !stop) {
        size_t k = 1 + rand() % 17;
        if (i + k > strlen(j)) k = strlen(j) - i;
        if (!cric_scan_feed(&sc, (const uint8_t *)j + i, k)) stop = true;
        i += k;
      }
      CricketData c;
      cric_scan_take(&sc, &c);
      CHECK(c.n == 3, "cric n seed %u = %u stop_at %zu", seed, c.n, i);
      CHECK(strcmp(c.m[0].name, "Live game") == 0 && c.m[0].started && !c.m[0].ended, "cric featured seed %u [%s]", seed, c.m[0].name);
      CHECK(c.m[0].ni == 1 && c.m[0].inn[0].r == 160 && c.m[0].inn[0].w == 4, "cric score seed %u", seed);
      CHECK(strcmp(c.m[0].inn[0].o, "18.2") == 0, "cric overs [%s]", c.m[0].inn[0].o);
      CHECK(strcmp(c.m[0].team[0], "Gamma") == 0 && strcmp(c.m[0].team[1], "Delta") == 0, "cric teams");
      CHECK(strcmp(c.m[0].kind, "t20") == 0, "cric kind");
      CHECK(strcmp(c.m[1].name, "Old finished") == 0 && c.m[1].ended, "cric second");
      CHECK(strcmp(c.m[2].name, "Upcoming") == 0 && !c.m[2].started && c.m[2].ni == 0, "cric upcoming");
      CHECK(!leaked(c), "cric leaked apikey seed %u", seed);
    }
  }
  {
    CompLine m;
    CHECK(comp_parse("HELLO K7M2Q9XW 0123456789abcdef", &m) && m.kind == COMP_HELLO, "hello");
    CHECK(strcmp(m.token, "K7M2Q9XW") == 0 && strcmp(m.id, "0123456789abcdef") == 0, "hello fields");
    CHECK(!comp_parse("HELLO K7M2Q9XI 0123456789abcdef", &m), "hello bad token char");
    CHECK(!comp_parse("HELLO K7M2Q9XW 0123456789abcde", &m), "hello short id");
    CHECK(!comp_parse("HELLO K7M2Q9XW 0123456789abcdef ", &m), "hello trailing");
    CHECK(comp_parse("STAT 0 100", &m) && m.kind == COMP_STAT && m.cpu == 0 && m.ram == 100 && m.note[0] == 0, "stat bare");
    CHECK(comp_parse("STAT 12 34 Build finished", &m) && m.cpu == 12 && m.ram == 34 && strcmp(m.note, "Build finished") == 0, "stat note");
    CHECK(!comp_parse("STAT 101 0", &m), "stat cpu");
    CHECK(!comp_parse("STAT 10 10\tnope", &m), "stat tab");
    char longn[80];
    snprintf(longn, sizeof(longn), "STAT 1 2 %s", "01234567890123456789012345678901234567890123456789");
    CHECK(!comp_parse(longn, &m), "stat note length");
    CHECK(!comp_parse("hello K7M2Q9XW 0123456789abcdef", &m), "hello case");
    CHECK(comp_parse("NOTIF Build finished", &m) && m.kind == COMP_NOTIF && strcmp(m.note, "Build finished") == 0, "notif");
    CHECK(!comp_parse("NOTIF ", &m), "notif empty");
    CHECK(!comp_parse("NOTIF \tbad", &m), "notif tab");
    CHECK(comp_parse("NOW Song A|Artist B", &m) && m.kind == COMP_NOW && strcmp(m.title, "Song A") == 0 && strcmp(m.artist, "Artist B") == 0, "now");
    CHECK(m.cpu == 0 && m.ram == 0 && m.note[0] == 0, "now leaves stat fields empty");
    CHECK(comp_parse("NOW |", &m) && m.kind == COMP_NOW && m.title[0] == 0 && m.artist[0] == 0, "now clear");
    CHECK(comp_parse("ART 48 48 4608", &m) && m.kind == COMP_ART && m.art_w == 48 && m.art_h == 48 && m.art_n == 4608, "art max");
    CHECK(comp_parse("ART 0 0 0", &m) && m.kind == COMP_ART && m.art_n == 0, "art clear");
    CHECK(!comp_parse("ART 49 1 98", &m), "art w over");
    CHECK(!comp_parse("NOW a|b|c", &m) && m.kind == COMP_BAD, "now extra bar");
    CHECK(COMP_HELLO == 1 && COMP_STAT == 2 && COMP_NOTIF == 3 && COMP_BAD == 4, "comp kind values stable");
  }
  printf("%s (%d checks, %d failures)\n", g_fail ? "PROTO TESTS FAILED" : "PROTO TESTS PASSED", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
