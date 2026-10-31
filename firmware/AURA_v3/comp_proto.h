// comp_proto.h - local laptop companion framing. No sockets here.
// The device listens; it never dials out. See extras/companion/README.md.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define COMP_TOKEN_LEN 8
#define COMP_ID_LEN 16
#ifndef COMP_NOTE_MAX
#define COMP_NOTE_MAX 47
#endif
#define COMP_PORT 42131
// v3.1.6 Now Playing. Same values as config.h (identical redefinition is legal);
// repeated here so host tests can include this header without config.h.
#ifndef COMP_NOW_FIELD_MAX
#define COMP_NOW_FIELD_MAX 47
#endif
#ifndef COMP_ART_MAX_DIM
#define COMP_ART_MAX_DIM 48
#endif
#ifndef COMP_ART_MAX
#define COMP_ART_MAX 4608
#endif

// COMP_NOW / COMP_ART appended (v3.1.6) so existing values do not move.
enum CompKind : uint8_t { COMP_NONE = 0, COMP_HELLO, COMP_STAT, COMP_NOTIF, COMP_BAD, COMP_NOW, COMP_ART };

struct CompLine {
  uint8_t kind;
  char token[COMP_TOKEN_LEN + 1];
  char id[COMP_ID_LEN + 1];
  uint8_t cpu, ram;
  char note[COMP_NOTE_MAX + 1];
  // COMP_NOW: "NOW <title>|<artist>". Both empty = clear.
  char title[COMP_NOW_FIELD_MAX + 1];
  char artist[COMP_NOW_FIELD_MAX + 1];
  // COMP_ART header: "ART <w> <h> <nbytes>". RGB565 LE, row-major; payload is
  // read raw by net_task (not here). w == h == art_n == 0 means clear.
  uint8_t art_w, art_h;
  uint16_t art_n;
};

// One line, newline already stripped. False on a reject (kind is COMP_BAD).
// For ART only the header line is parsed; the caller then reads art_n raw bytes.
bool comp_parse(const char *line, CompLine *out);
