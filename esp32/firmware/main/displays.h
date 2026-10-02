#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include "screen_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `out` must contain at least config->count entries. */
esp_err_t displays_init(const screen_config_t *config, lv_display_t **out);

#ifdef __cplusplus
}
#endif
