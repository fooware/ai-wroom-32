#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Stable IDs shared by firmware, UI and simulator; no account secrets here. */
typedef enum { PROVIDER_CODEX, PROVIDER_CURSOR, PROVIDER_CLAUDE, PROVIDER_COUNT } provider_id_t;

typedef struct {
  const char *id;
  const char *title;
  uint32_t accent;
  const char *bar_names[2];
  int16_t label_x[2];
} provider_info_t;

typedef struct {
  bool available;
  int remaining[2]; /* 0..100, -1 means unknown */
  char lines[3][48];
  char status[40];
} provider_data_t;

const provider_info_t *provider_info(provider_id_t id);
bool provider_from_name(const char *name, provider_id_t *out);
