// net_task.h - core 0 network task: wifi_mgr, SNTP, the data job scheduler
// (one TLS session at a time, heap-gated) and the Wi-Fi setup portal.
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
bool net_task_start(void);
TaskHandle_t net_task_handle(void);
// Any task: run job now (JobId, -1 = all) / legacy alias for the weather job.
void net_request_refresh(int job);
void net_request_weather_now(void);
// Any task: start/stop the captive setup portal (applied by net_task).
void net_portal(bool on);
// Local companion listener. The device never opens an outbound socket for this.
void net_comp_enable(bool on);
void net_comp_forget(void);
// API keys (NVS "keys"): presence only is ever published or printed.
// name: stocks|cricket|oracle (xAI)|groq|owm|gemini; "" clears (a real secrets.h value then applies)
bool net_key_set(const char *name, const char *value);
// NVS lengths only (0 = not stored). Order: stocks, cricket, oracle, groq, owm, gemini.
void net_key_lens(uint8_t out[6]);
// Oracle (xAI or Groq, v3.1.2): any task. Queues one question (a newer one replaces a queued
// one) and returns its sequence number; progress is published as OracleData.
uint32_t net_oracle_ask(const char *prompt);
