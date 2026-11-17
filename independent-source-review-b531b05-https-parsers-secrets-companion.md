# Independent source-only security review — AURA firmware (HEAD b531b05)

**Scope:** `firmware/AURA_v3/` attack surfaces listed in the review brief (HTTPS client, parsers, secrets/zip, `aura_companion.py`, other injection).  
**Method:** Trace attacker-controlled input from source to sink in source only.  
**Verdict:** **NO_MEDIUM_PLUS**

No validated MEDIUM / HIGH / CRITICAL finding with a complete end-to-end attack chain.

---

## 1. HTTPS client (`net_task.cpp` / `net_proto.cpp`)

**Controls verified**

| Check | Evidence |
| --- | --- |
| Hosts are compile-time constants | `config.h` `WEATHER_HOST`/`AIR_HOST`/`CRYPTO_HOST`/`NEWS_HOST`/`STOCK_*`/`CRIC_HOST`/`ORACLE_HOST`/`GROQ_HOST`/`GEMINI_HOST`; callers pass those macros into `https_get` (`net_task.cpp` ~493–1105). Oracle uses `k_or_api[].host` (`net_task.cpp` 768–770, 912). |
| Port fixed to 443 | `s_tls.connect(host, 443, …)` at 308 and 912. |
| TLS verify on (builtin CA bundle) | `useBuiltinCACertBundle()` at 304 and 908. No `setInsecure` anywhere under `firmware/AURA_v3/`. |
| No redirect following | `https_get` parses status only; `code != 200` stops without reading `Location` (291–390, esp. 373–374). HTTP/1.0 GET (333). |
| Paths not attacker URLs | Fixed templates or bounded `snprintf` with lat/lon/timezone/API key into path; host never taken from response/body. |
| Response → RCE/overflow | Body via `body_sink` capped at `NET_BODY_MAX` (392–398) or stream parsers; Oracle reply capped at `ORACLE_REPLY_MAX` via `reply_raw` (`net_proto.cpp` 173–188). No code execution sink. |
| Secret leak via logs | Oracle logs `key_len` / `key_src` only (903–905); keys wiped after header write (932–935). |

**Not exploitable for SSRF / verify-off / HTTP cleartext client / user-controlled URL host.** Lat/lon only affect query string on `api.open-meteo.com` / air-quality host.

---

## 2. Parsers (`net_proto.cpp`, `extras/host/nettest/proto_test.cpp`)

**Controls verified**

- **HTTP/SSE/JStr/chunked:** Fixed-size line/probe buffers; long header lines truncated (`http_hdr_feed` 112); chunk size hex digits capped (`chunk_feed` 132); reply buffer hard-capped.
- **CoinGecko / sparkline (`cg_scan_feed`):** Fixed `ring[CRYPTO_SPARK_N]` (24); depth capped (`ctx[6]`, error on overflow 618); key/num/sv lengths capped; no heap alloc; no size_t multiply for allocation.
- **Cricket (`cric_scan_feed` / `cric_byte`):** Depth ≤ 9; `val`/`key` bounded copies via `cric_copy`; stops after enough matches.
- **RSS (`rss_sink`):** Tag name ≤ 16; raw title ≤ 192; `NEWS_MAX` stop; entity decode writes with `cap` check.
- **Stocks / AV:** Scalar copy into caller-sized `dst` with `cap`; no unbounded `strcpy`.
- **ART companion path:** `comp_parse` requires `n == w*h*2` with `w,h ≤ 48` and `n ≤ COMP_ART_MAX` (4608); `ds_comp_art_write` bounds `off+n` to `sizeof(s_art)`.

Host `proto_test.cpp` exercises overflow detection / framing; no separate production sink.

---

## 3. Secrets handling

**Controls verified**

- `secrets.h` / `secrets.example.h` / zip copies: all `*_DEFAULT` values are the placeholder string only (verified by equality to `AURA_CRED_PLACEHOLDER`; lengths 24). Variables: `WIFI_*_DEFAULT`, `OWM_API_KEY_DEFAULT`, `CRICKET_API_KEY_DEFAULT`, `STOCK_API_KEY_DEFAULT`, `FOOTBALL_API_KEY_DEFAULT`, `GROQ_API_KEY_DEFAULT`, `XAI_API_KEY_DEFAULT`, `GEMINI_API_KEY_DEFAULT` in `firmware/AURA_v3/secrets.h` (lines 12–20).
- `releases/AURA_v3_subscriber.zip` includes `AURA_v3/secrets.h` but only placeholders (same check).
- `make_subscriber_zip.py` packs sketch sources; does not embed runtime NVS. At this HEAD the packaged `secrets.h` is not live credentials.
- NVS namespace `"keys"`; presence logged, values not (`keys_publish` 157–178). Wi-Fi/portal logs lengths only (e.g. 1330, 1408).
- URL keys gated by `key_url_safe` (1009–1016) before path build; path wiped after GET.

**No committed live credentials and no API-key value logging found at this HEAD.**

---

## 4. Python companion (`extras/companion/aura_companion.py`)

**Controls verified**

- Outbound TCP only to CLI/pair `host:port` (`create_connection`); no listen/bind server; no HTTP client / SSRF helper in this file.
- `subprocess.check_output` with fixed argv lists (no `shell=True`); no `pickle` / `yaml` / `eval` / `exec`.
- Pair file written `0o600` (`save_pair` 41–49).
- `--art` reads a local path supplied by the operator; validates dims and exact `w*h*2` size before send.

Related opt-in `news_body.py` (not the primary companion entrypoint) uses HTTPS with default SSL context, fixed feed URL, and BBC host/path allowlist including redirect re-check — not a validated MEDIUM+ SSRF chain.

---

## 5. Other injection

- Gemini model in request path: portal `model_ok` restricts to `[A-Za-z0-9._-]` ≤ 31 (`net_task.cpp` 1358–1364) before NVS save; blocks CRLF header injection on the SoftAP write path.
- API keys for Authorization / `x-goog-api-key`: printable ASCII, no space/CR/LF (878–879).
- Companion protocol fields: printable ASCII / fixed lengths (`comp_proto.cpp`).
- No unsafe deserialization, template engines, or path-based file fetch on device HTTPS jobs.

---

## Summary

Attacker-controlled network responses are confined to bounded parsers and display text; TLS destinations are fixed HTTPS hosts with CA-bundle verification and no redirect following; secrets at HEAD are placeholders and are not logged as values; the laptop companion does not expose command injection, pickle/YAML, or listening SSRF surfaces in the reviewed code.
