#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *esp_lcd_panel_io_handle_t; typedef struct esp_lcd_panel_t esp_lcd_panel_t; typedef esp_lcd_panel_t *esp_lcd_panel_handle_t;
struct esp_lcd_panel_t { esp_err_t (*del)(esp_lcd_panel_t *); esp_err_t (*reset)(esp_lcd_panel_t *); esp_err_t (*init)(esp_lcd_panel_t *); esp_err_t (*draw_bitmap)(esp_lcd_panel_t *,int,int,int,int,const void *); esp_err_t (*invert_color)(esp_lcd_panel_t *,bool); esp_err_t (*mirror)(esp_lcd_panel_t *,bool,bool); esp_err_t (*swap_xy)(esp_lcd_panel_t *,bool); esp_err_t (*set_gap)(esp_lcd_panel_t *,int,int); esp_err_t (*disp_on_off)(esp_lcd_panel_t *,bool); };
typedef enum { LCD_RGB_ELEMENT_ORDER_RGB, LCD_RGB_ELEMENT_ORDER_BGR } lcd_rgb_element_order_t;
typedef struct { int reset_gpio_num; int bits_per_pixel; lcd_rgb_element_order_t rgb_ele_order; struct { unsigned reset_active_high:1; } flags; } esp_lcd_panel_dev_config_t;
