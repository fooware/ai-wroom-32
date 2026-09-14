#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t displays_init(lv_display_t **left, lv_display_t **right);

#ifdef __cplusplus
}
#endif
