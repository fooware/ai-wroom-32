/*
 * SPDX-License-Identifier: MIT
 *
 * The GC9B72 initialization table is derived from xboot/xstar's
 * fb-gc9b72.c at bd51ce0ce0e6350fdb76ccdaed3627d3b8ad84e8. See
 * GC9B72-LICENSE.txt for the full attribution and license notice.
 */

#include "gc9b72.h"

#include <stdlib.h>
#include <stddef.h>
#include <sys/cdefs.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "gc9b72";

#ifndef __containerof
#define __containerof(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

typedef struct {
  /* Every entry is one command, its exact parameter byte count, and a delay. */
  uint8_t command;
  uint8_t data[32];
  uint8_t data_bytes;
  uint16_t delay_ms;
} gc9b72_init_command_t;

typedef struct {
  esp_lcd_panel_t base;
  esp_lcd_panel_io_handle_t io;
  int reset_gpio_num;
  bool reset_level;
  int x_gap;
  int y_gap;
  uint8_t madctl;
  uint8_t colmod;
} gc9b72_panel_t;

/*
 * The controller's vendor register meanings are unpublished. Keep their
 * addresses visibly distinct from standard MIPI DCS commands instead of
 * assigning speculative names. ESP-IDF already supplies the standard TEON name.
 */
enum {
  GC9B72_PIXEL_FORMAT_RGB565 = 0x05,
  GC9B72_VENDOR_REG_EE = 0xEE,
  GC9B72_VENDOR_REG_EF = 0xEF,
  GC9B72_VENDOR_REG_FE = 0xFE,
  GC9B72_VENDOR_REG_60 = 0x60,
  GC9B72_VENDOR_REG_61 = 0x61,
  GC9B72_VENDOR_REG_62 = 0x62,
  GC9B72_VENDOR_REG_63 = 0x63,
  GC9B72_VENDOR_REG_64 = 0x64,
  GC9B72_VENDOR_REG_65 = 0x65,
  GC9B72_VENDOR_REG_66 = 0x66,
  GC9B72_VENDOR_REG_68 = 0x68,
  GC9B72_VENDOR_REG_69 = 0x69,
  GC9B72_VENDOR_REG_6A = 0x6A,
  GC9B72_VENDOR_REG_6C = 0x6C,
  GC9B72_VENDOR_REG_6E = 0x6E,
  GC9B72_VENDOR_REG_70 = 0x70,
  GC9B72_VENDOR_REG_74 = 0x74,
  GC9B72_VENDOR_REG_7C = 0x7C,
  GC9B72_VENDOR_REG_7D = 0x7D,
  GC9B72_VENDOR_REG_80 = 0x80,
  GC9B72_VENDOR_REG_81 = 0x81,
  GC9B72_VENDOR_REG_82 = 0x82,
  GC9B72_VENDOR_REG_83 = 0x83,
  GC9B72_VENDOR_REG_84 = 0x84,
  GC9B72_VENDOR_REG_85 = 0x85,
  GC9B72_VENDOR_REG_86 = 0x86,
  GC9B72_VENDOR_REG_87 = 0x87,
  GC9B72_VENDOR_REG_88 = 0x88,
  GC9B72_VENDOR_REG_89 = 0x89,
  GC9B72_VENDOR_REG_8A = 0x8A,
  GC9B72_VENDOR_REG_8B = 0x8B,
  GC9B72_VENDOR_REG_8C = 0x8C,
  GC9B72_VENDOR_REG_8E = 0x8E,
  GC9B72_VENDOR_REG_8F = 0x8F,
  GC9B72_VENDOR_REG_90 = 0x90,
  GC9B72_VENDOR_REG_93 = 0x93,
  GC9B72_VENDOR_REG_98 = 0x98,
  GC9B72_VENDOR_REG_99 = 0x99,
  GC9B72_VENDOR_REG_AA = 0xAA,
  GC9B72_VENDOR_REG_AC = 0xAC,
  GC9B72_VENDOR_REG_B4 = 0xB4,
  GC9B72_VENDOR_REG_B5 = 0xB5,
  GC9B72_VENDOR_REG_C3 = 0xC3,
  GC9B72_VENDOR_REG_C4 = 0xC4,
  GC9B72_VENDOR_REG_C9 = 0xC9,
  GC9B72_VENDOR_REG_CB = 0xCB,
  GC9B72_VENDOR_REG_EB = 0xEB,
  GC9B72_VENDOR_REG_EC = 0xEC,
  GC9B72_VENDOR_REG_F0 = 0xF0,
  GC9B72_VENDOR_REG_F1 = 0xF1,
  GC9B72_VENDOR_REG_F2 = 0xF2,
  GC9B72_VENDOR_REG_F3 = 0xF3,
  GC9B72_VENDOR_REG_F6 = 0xF6,
  GC9B72_VENDOR_REG_F9 = 0xF9,
  GC9B72_VENDOR_REG_FB = 0xFB,
};

#define CMD0(command) {command, {0}, 0, 0}
#define CMD0_DELAY(command, delay) {command, {0}, 0, delay}
#define CMD1(command, value) {command, {value}, 1, 0}
#define CMD2(command, first, second) {command, {first, second}, 2, 0}
#define CMDN(command, byte_count, ...) {command, {__VA_ARGS__}, byte_count, 0}

/*
 * Exact 360x360 startup sequence from xboot's fb-gc9b72.c. The vendor
 * payloads and lengths are preserved byte-for-byte; their meanings are not
 * publicly documented. Do not edit this table without panel validation.
 */
static const gc9b72_init_command_t default_init[] = {
    /* Initial vendor commands and one-byte setup values. */
    CMD0(GC9B72_VENDOR_REG_FE), CMD0(GC9B72_VENDOR_REG_EF),
    CMD1(GC9B72_VENDOR_REG_80, 0x19), CMD1(GC9B72_VENDOR_REG_82, 0x09), CMD1(GC9B72_VENDOR_REG_83, 0x03),
    CMD1(GC9B72_VENDOR_REG_88, 0x00), CMD1(GC9B72_VENDOR_REG_89, 0x38), CMD1(GC9B72_VENDOR_REG_8A, 0x40),
    CMD1(GC9B72_VENDOR_REG_8B, 0x0A), CMD1(GC9B72_VENDOR_REG_8C, 0x00), CMD1(GC9B72_VENDOR_REG_81, 0xFF),
    CMD1(GC9B72_VENDOR_REG_84, 0xFF), CMD1(GC9B72_VENDOR_REG_85, 0xFF), CMD1(GC9B72_VENDOR_REG_86, 0xFF),
    CMD1(GC9B72_VENDOR_REG_87, 0xFF), CMD1(GC9B72_VENDOR_REG_8E, 0xFF), CMD1(GC9B72_VENDOR_REG_8F, 0xFF),
    CMD1(GC9B72_VENDOR_REG_98, 0x3E), CMD1(GC9B72_VENDOR_REG_99, 0x3E), CMD1(GC9B72_VENDOR_REG_7D, 0x72),

    /* Vendor setup blocks. CMDN's count is the exact source payload length. */
    CMDN(GC9B72_VENDOR_REG_70, 10, 0x02,0x03,0x03,0x06,0x03,0x03,0x09,0x07,0x09,0x03),
    CMDN(GC9B72_VENDOR_REG_90, 4, 0x06,0x06,0x01,0x01), CMDN(GC9B72_VENDOR_REG_93, 3, 0x02,0xFF,0x00),
    CMD1(GC9B72_VENDOR_REG_CB, 0x02), CMD2(GC9B72_VENDOR_REG_FB, 0x00, 0x00), CMD1(GC9B72_VENDOR_REG_F6, 0xC0),
    CMDN(GC9B72_VENDOR_REG_6C, 7, 0x00,0x00,0x22,0x00,0xCC,0x04,0x58), CMD2(GC9B72_VENDOR_REG_AA, 0x0B, 0x00),
    CMD1(GC9B72_VENDOR_REG_EC, 0x07), CMD1(GC9B72_VENDOR_REG_F9, 0x40), CMD2(GC9B72_VENDOR_REG_EB, 0x01, 0x67),
    CMDN(GC9B72_VENDOR_REG_74, 6, 0x01,0x60,0x00,0x00,0x00,0x00), CMDN(GC9B72_VENDOR_REG_B5, 3, 0x14,0x14,0x14),
    CMDN(GC9B72_VENDOR_REG_6E, 32, 0x0B,0x0B,0x09,0x09,0x13,0x13,0x11,0x11,0x16,0x15,0x01,0x04,0x00,0x0D,0x1D,0x00,
         0x00,0x1D,0x0D,0x00,0x04,0x08,0x15,0x16,0x12,0x12,0x14,0x14,0x0A,0x0A,0x0C,0x0C),
    CMDN(GC9B72_VENDOR_REG_60, 4, 0x38,0x1C,0x13,0x56), CMDN(GC9B72_VENDOR_REG_61, 4, 0xF8,0x0A,0x13,0x56),
    CMDN(GC9B72_VENDOR_REG_62, 4, 0xF8,0x0B,0x13,0x56), CMDN(GC9B72_VENDOR_REG_63, 4, 0x38,0x1C,0x13,0x56),
    CMDN(GC9B72_VENDOR_REG_64, 6, 0x38,0x20,0x72,0xF8,0x13,0x56), CMDN(GC9B72_VENDOR_REG_65, 6, 0x78,0x1A,0x70,0x0B,0x56,0x13),
    CMDN(GC9B72_VENDOR_REG_66, 6, 0x38,0x24,0x72,0xFC,0x13,0x56), CMDN(GC9B72_VENDOR_REG_68, 7, 0xB3,0x08,0x0E,0x08,0x0E,0x0A,0x0A),
    CMDN(GC9B72_VENDOR_REG_69, 7, 0xB3,0x08,0x0E,0x08,0x0E,0x0A,0x0A), CMD2(GC9B72_VENDOR_REG_6A, 0x00, 0x00),

    /* Standard DCS: 0x05 is 16-bit RGB565; MADCTL is replaced from caller configuration. */
    CMD1(LCD_CMD_COLMOD, GC9B72_PIXEL_FORMAT_RGB565), CMD1(LCD_CMD_MADCTL, 0x00),

    /* More unpublished vendor setup. F0-F3 are intentionally not labelled as gamma registers. */
    CMD2(GC9B72_VENDOR_REG_7C, 0xB6, 0x29), CMD1(GC9B72_VENDOR_REG_AC, 0x40), CMD1(GC9B72_VENDOR_REG_C3, 0x1A),
    CMD1(GC9B72_VENDOR_REG_C4, 0x24), CMD1(GC9B72_VENDOR_REG_C9, 0x2F),
    CMDN(GC9B72_VENDOR_REG_F0, 6, 0x11,0x17,0x08,0x06,0x05,0x38), CMDN(GC9B72_VENDOR_REG_F1, 6, 0x4D,0x72,0x72,0x2D,0x34,0x8F),
    CMDN(GC9B72_VENDOR_REG_F2, 6, 0x11,0x17,0x08,0x06,0x05,0x38), CMDN(GC9B72_VENDOR_REG_F3, 6, 0x4D,0x72,0x72,0x2D,0x34,0x8F),
    CMD1(GC9B72_VENDOR_REG_B4, 0x0A), CMD1(LCD_CMD_TEON, 0x00),

    /* Final vendor commands, then leave sleep and enable pixels after required delays. */
    CMD0(GC9B72_VENDOR_REG_FE), CMD0(GC9B72_VENDOR_REG_EE),
    CMD0_DELAY(LCD_CMD_SLPOUT, 120), CMD0_DELAY(LCD_CMD_DISPON, 20),
};

static esp_err_t panel_del(esp_lcd_panel_t *panel);
static esp_err_t panel_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_init(esp_lcd_panel_t *panel);
static esp_err_t panel_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_invert(esp_lcd_panel_t *panel, bool invert);
static esp_err_t panel_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_swap_xy(esp_lcd_panel_t *panel, bool swap_xy);
static esp_err_t panel_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_disp_on_off(esp_lcd_panel_t *panel, bool on);

esp_err_t esp_lcd_new_panel_gc9b72(esp_lcd_panel_io_handle_t io,
                                   const esp_lcd_panel_dev_config_t *config,
                                   esp_lcd_panel_handle_t *ret_panel) {
  if (!io || !config || !ret_panel) return ESP_ERR_INVALID_ARG;
  if (config->bits_per_pixel != 16) return ESP_ERR_NOT_SUPPORTED;
  gc9b72_panel_t *gc9b72 = calloc(1, sizeof(*gc9b72));
  if (!gc9b72) return ESP_ERR_NO_MEM;
  gc9b72->io = io;
  gc9b72->reset_gpio_num = config->reset_gpio_num;
  gc9b72->reset_level = config->flags.reset_active_high;
  gc9b72->colmod = GC9B72_PIXEL_FORMAT_RGB565; /* GC9B72 uses RGB565's 0x05 encoding. */
  switch (config->rgb_ele_order) {
    case LCD_RGB_ELEMENT_ORDER_RGB: gc9b72->madctl = 0; break;
    case LCD_RGB_ELEMENT_ORDER_BGR: gc9b72->madctl = LCD_CMD_BGR_BIT; break;
    default: free(gc9b72); return ESP_ERR_NOT_SUPPORTED;
  }
  if (gc9b72->reset_gpio_num >= 0) {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gc9b72->reset_gpio_num)) {
      free(gc9b72);
      return ESP_ERR_INVALID_ARG;
    }
    gpio_config_t reset_config = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << gc9b72->reset_gpio_num};
    esp_err_t err = gpio_config(&reset_config);
    if (err != ESP_OK) { free(gc9b72); return err; }
  }
  gc9b72->base.del = panel_del;
  gc9b72->base.reset = panel_reset;
  gc9b72->base.init = panel_init;
  gc9b72->base.draw_bitmap = panel_draw_bitmap;
  gc9b72->base.invert_color = panel_invert;
  gc9b72->base.mirror = panel_mirror;
  gc9b72->base.swap_xy = panel_swap_xy;
  gc9b72->base.set_gap = panel_set_gap;
  gc9b72->base.disp_on_off = panel_disp_on_off;
  *ret_panel = &gc9b72->base;
  return ESP_OK;
}

static esp_err_t panel_del(esp_lcd_panel_t *panel) {
  gc9b72_panel_t *gc9b72 = __containerof(panel, gc9b72_panel_t, base);
  if (gc9b72->reset_gpio_num >= 0) gpio_reset_pin(gc9b72->reset_gpio_num);
  free(gc9b72);
  return ESP_OK;
}

static esp_err_t panel_reset(esp_lcd_panel_t *panel) {
  gc9b72_panel_t *gc9b72 = __containerof(panel, gc9b72_panel_t, base);
  if (gc9b72->reset_gpio_num >= 0) {
    /* Active level is supplied by esp_lcd's generic panel configuration. */
    gpio_set_level(gc9b72->reset_gpio_num, gc9b72->reset_level);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(gc9b72->reset_gpio_num, !gc9b72->reset_level);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
  }
  /* The display manager may have already pulsed a shared hardware reset. */
  ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(gc9b72->io, LCD_CMD_SWRESET, NULL, 0), TAG, "software reset");
  vTaskDelay(pdMS_TO_TICKS(20));
  return ESP_OK;
}

static esp_err_t panel_init(esp_lcd_panel_t *panel) {
  gc9b72_panel_t *gc9b72 = __containerof(panel, gc9b72_panel_t, base);
  for (size_t i = 0; i < sizeof(default_init) / sizeof(default_init[0]); ++i) {
    const gc9b72_init_command_t *entry = &default_init[i];
    uint8_t value = entry->data[0];
    const void *data = entry->data;
    /* Preserve every vendor byte, but apply the caller's color order and RGB565 format. */
    if (entry->command == LCD_CMD_MADCTL) { value = gc9b72->madctl; data = &value; }
    if (entry->command == LCD_CMD_COLMOD) { value = gc9b72->colmod; data = &value; }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(gc9b72->io, entry->command, data, entry->data_bytes), TAG, "init %02X", entry->command);
    if (entry->delay_ms) vTaskDelay(pdMS_TO_TICKS(entry->delay_ms));
  }
  return ESP_OK;
}

static esp_err_t panel_draw_bitmap(esp_lcd_panel_t *panel, int xs, int ys, int xe, int ye, const void *color_data) {
  gc9b72_panel_t *gc9b72 = __containerof(panel, gc9b72_panel_t, base);
  if (!color_data || xs < 0 || ys < 0 || xs >= xe || ys >= ye || xe > 360 || ye > 360)
    return ESP_ERR_INVALID_ARG;
  /* esp_lcd uses end-exclusive rectangles; CASET/RASET instead take inclusive ends. */
  int64_t gx_start = (int64_t)xs + gc9b72->x_gap, gx_end = (int64_t)xe + gc9b72->x_gap;
  int64_t gy_start = (int64_t)ys + gc9b72->y_gap, gy_end = (int64_t)ye + gc9b72->y_gap;
  if (gx_start < 0 || gy_start < 0 || gx_end > 360 || gy_end > 360) return ESP_ERR_INVALID_ARG;
  xs = (int)gx_start; xe = (int)gx_end; ys = (int)gy_start; ye = (int)gy_end;
  uint8_t column[] = {xs >> 8, xs, (xe - 1) >> 8, xe - 1};
  uint8_t row[] = {ys >> 8, ys, (ye - 1) >> 8, ye - 1};
  ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(gc9b72->io, LCD_CMD_CASET, column, sizeof(column)), TAG, "CASET");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(gc9b72->io, LCD_CMD_RASET, row, sizeof(row)), TAG, "RASET");
  return esp_lcd_panel_io_tx_color(gc9b72->io, LCD_CMD_RAMWR, color_data, (size_t)(xe - xs) * (ye - ys) * 2);
}

static esp_err_t panel_invert(esp_lcd_panel_t *panel, bool invert) {
  gc9b72_panel_t *gc9b72 = __containerof(panel, gc9b72_panel_t, base);
  return esp_lcd_panel_io_tx_param(gc9b72->io, invert ? LCD_CMD_INVON : LCD_CMD_INVOFF, NULL, 0);
}

static esp_err_t write_madctl(gc9b72_panel_t *gc9b72) {
  /* MADCTL carries RGB/BGR order as well as the two mirror bits and XY swap bit. */
  return esp_lcd_panel_io_tx_param(gc9b72->io, LCD_CMD_MADCTL, &gc9b72->madctl, 1);
}

static esp_err_t panel_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y) {
  gc9b72_panel_t *gc9b72 = __containerof(panel, gc9b72_panel_t, base);
  gc9b72->madctl = (gc9b72->madctl & ~(LCD_CMD_MX_BIT | LCD_CMD_MY_BIT)) |
                   (mirror_x ? LCD_CMD_MX_BIT : 0) | (mirror_y ? LCD_CMD_MY_BIT : 0);
  return write_madctl(gc9b72);
}

static esp_err_t panel_swap_xy(esp_lcd_panel_t *panel, bool swap_xy) {
  gc9b72_panel_t *gc9b72 = __containerof(panel, gc9b72_panel_t, base);
  if (swap_xy) gc9b72->madctl |= LCD_CMD_MV_BIT;
  else gc9b72->madctl &= ~LCD_CMD_MV_BIT;
  return write_madctl(gc9b72);
}

static esp_err_t panel_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap) {
  gc9b72_panel_t *gc9b72 = __containerof(panel, gc9b72_panel_t, base);
  gc9b72->x_gap = x_gap;
  gc9b72->y_gap = y_gap;
  return ESP_OK;
}

static esp_err_t panel_disp_on_off(esp_lcd_panel_t *panel, bool on) {
  gc9b72_panel_t *gc9b72 = __containerof(panel, gc9b72_panel_t, base);
  return esp_lcd_panel_io_tx_param(gc9b72->io, on ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0);
}
