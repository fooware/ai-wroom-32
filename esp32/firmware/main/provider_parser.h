#pragma once

#include <stdbool.h>

#include "provider.h"

typedef bool (*provider_parse_fn_t)(const char *body, provider_data_t *out);

/* Parses only response data; networking and credentials remain outside this module. */
bool provider_parse_codex(const char *body, provider_data_t *out);
bool provider_parse_cursor(const char *body, provider_data_t *out);
bool provider_parse_claude(const char *body, provider_data_t *out);
