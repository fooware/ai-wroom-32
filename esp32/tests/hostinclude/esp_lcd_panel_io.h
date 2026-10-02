#pragma once
#include <stddef.h>
#include "esp_lcd_panel_vendor.h"
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int cmd, const void *data, size_t len);
esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io, int cmd, const void *data, size_t len);
