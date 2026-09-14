#pragma once

#include "esp_err.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*usage_meters_cb_t)(const ui_codex_data_t *codex, const ui_cursor_data_t *cursor);
typedef void (*usage_attraction_cb_t)(void);
/* Short, screen-sized reason a usage fetch did not produce meters. */
typedef void (*usage_status_cb_t)(const char *status);

esp_err_t usage_poll_start(usage_meters_cb_t on_meters, usage_attraction_cb_t on_attraction,
                           usage_status_cb_t on_status);

#ifdef __cplusplus
}
#endif
