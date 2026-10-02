/* SPDX-License-Identifier: MIT */
#pragma once

#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create a standard 4-wire SPI GC9B72 panel (360 x 360 RGB565). */
esp_err_t esp_lcd_new_panel_gc9b72(esp_lcd_panel_io_handle_t io,
                                   const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
