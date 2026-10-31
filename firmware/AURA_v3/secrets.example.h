// secrets.h - public template. Every value is the placeholder, not a real secret.
// Settings → Credentials (NVS) overrides these. Do not commit real SSIDs, passwords, or API keys.
#pragma once
#include <string.h>

#define AURA_CRED_PLACEHOLDER "put your credential here"
// The placeholder is not a network name or an API key. A private rebuild may replace it.
static inline bool aura_cred_usable(const char *s) {
  return s && s[0] && strcmp(s, AURA_CRED_PLACEHOLDER) != 0;
}

#define WIFI_SSID_DEFAULT         "put your credential here"   // Wi-Fi name (SSID)
#define WIFI_PASS_DEFAULT         "put your credential here"   // Wi-Fi password
#define OWM_API_KEY_DEFAULT       "put your credential here"   // Weather API key
#define CRICKET_API_KEY_DEFAULT   "put your credential here"   // Cricket API key
#define STOCK_API_KEY_DEFAULT     "put your credential here"   // Stocks API key
#define FOOTBALL_API_KEY_DEFAULT  "put your credential here"   // Football API key (optional)
#define GROQ_API_KEY_DEFAULT      "put your credential here"   // Groq / Oracle API key
#define XAI_API_KEY_DEFAULT       "put your credential here"   // xAI / Grok API key (optional)
#define GEMINI_API_KEY_DEFAULT    "put your credential here"   // Gemini API key (optional)
