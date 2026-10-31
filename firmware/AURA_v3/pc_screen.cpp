// pc_screen.cpp - PC Status. The sanitized v3.1.6 pack referenced this app but
// did not include its source. This screen shows the companion link and CPU/RAM
// from the data the rest of the firmware already publishes.
#include "apps.h"
#include <stdio.h>
#include <string.h>

static uint8_t s_page = 0;

void pc_debug_page(uint8_t page, uint8_t) { s_page = page ? 1 : 0; }
void pc_tick(uint32_t) {}
bool pc_token_visible(void) {
  return g_ui.scr == SCR_APP && g_ui.app == APP_PC && g_ui.prefs.comp_on && !g_ui.d.comp.paired && g_ui.d.comp.token[0];
}
void pc_enter(uint32_t) { s_page = 0; }
void pc_leave(void) {}
void pc_input(const UiIn &in, uint32_t) {
  if (in.type == IN_ROT || in.type == IN_SWL || in.type == IN_SWR) s_page ^= 1;
  else if (in.type == IN_CLICK && !g_ui.prefs.comp_on) {
    g_ui.prefs.comp_on = 1;
    hal_comp_enable(true);
    ui_prefs_changed();
    ui_toast("Companion on - local only", 1500);
  }
  if (in.type == IN_ROT || in.type == IN_CLICK || in.type == IN_SWL || in.type == IN_SWR || in.type == IN_TAP) ui_consume();
}
void pc_draw(void) {
  gfx_fill(C_BG);
  const CompData &c = g_ui.d.comp;
  const PcStatusData &p = g_ui.d.pc;
  w_status(s_page ? "ALERTS" : "PC STATUS", "");
  if (!g_ui.prefs.comp_on && !p.linked) {
    w_card(10, 40, 300, 120, 10);
    gfx_text_m(&AF_TITLE, 24, 78, "Not linked", C_TXT);
    gfx_text_m(&AF_BODY_S, 24, 104, "Press to listen for one laptop", C_MUT);
    gfx_text_m(&AF_BODY_S, 24, 124, "on this Wi-Fi. Nothing is uploaded.", C_MUT);
    w_footer("PRESS: TURN ON   HOLD: BACK");
    return;
  }
  if (g_ui.prefs.comp_on && !c.paired && c.token[0]) {
    w_card(10, 40, 300, 120, 10);
    gfx_micro(24, 52, "PAIRING CODE", C_DIM);
    gfx_text_m(&AF_TITLE, 24, 78, c.token, C_ACC);
    gfx_text_m(&AF_BODY_S, 24, 108, "Type this code in the laptop app.", C_MUT);
    w_footer("HOLD: BACK");
    return;
  }
  const bool live = c.live || c.paired;
  const unsigned cpu = live ? c.cpu : p.cpu_pct;
  const unsigned ram = live ? c.ram : (p.ram_total_mb ? (unsigned)(p.ram_used_mb * 100u / p.ram_total_mb) : 0);
  w_card(10, 36, 300, 150, 10);
  gfx_text_m(&AF_ROW, 24, 58, p.host[0] ? p.host : (c.paired ? "Paired laptop" : "This Wi-Fi"), C_TXT);
  char b[40];
  snprintf(b, sizeof(b), "CPU %u%%", cpu);
  gfx_text_m(&AF_LABEL, 24, 86, b, C_TXT);
  gfx_rrect(110, 80, 170, 8, 3, C_TILE_HI);
  if (cpu) gfx_rrect(110, 80, (int)(170 * cpu / 100), 8, 3, cpu > 85 ? C_WARN : C_ACC);
  snprintf(b, sizeof(b), "RAM %u%%", ram);
  gfx_text_m(&AF_LABEL, 24, 114, b, C_TXT);
  gfx_rrect(110, 108, 170, 8, 3, C_TILE_HI);
  if (ram) gfx_rrect(110, 108, (int)(170 * ram / 100), 8, 3, C_ACC);
  if (s_page && p.notif_n) {
    gfx_micro(24, 136, p.notif[0].title, C_MUT);
  } else if (c.note[0]) {
    w_text_ell(&AF_BODY_S, 24, 146, c.note, 270, C_TXT);
  } else {
    gfx_micro(24, 140, live ? "LINKED" : "WAITING", C_DIM);
  }
  w_footer(p.notif_n ? "TURN: ALERTS   HOLD: BACK" : "HOLD: BACK");
}
