#include "provider.h"
#include <stddef.h>
#include <string.h>

static const provider_info_t catalog[PROVIDER_COUNT] = {
    [PROVIDER_CODEX] = {"codex", "Codex", 0x00a878, {"5h", "1w"}, {40, 40}},
    [PROVIDER_CURSOR] = {"cursor", "Cursor", 0xf54e00, {"Cheap", "Good"}, {28, 31}},
    [PROVIDER_CLAUDE] = {"claude", "Claude", 0xd97757, {"5h", "1w"}, {40, 40}},
};

const provider_info_t *provider_info(provider_id_t id) {
  return (unsigned)id < PROVIDER_COUNT ? &catalog[id] : NULL;
}

bool provider_from_name(const char *name, provider_id_t *out) {
  if (!name || !out) return false;
  for (unsigned i = 0; i < PROVIDER_COUNT; ++i) {
    if (strcmp(name, catalog[i].id) == 0) { *out = (provider_id_t)i; return true; }
  }
  return false;
}
