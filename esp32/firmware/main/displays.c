#include "displays.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "ui.h"

static const char *TAG = "displays";

/* Pinout from rxh-wroom-32 dual-display/src/displays.hpp */
#define PIN_SCLK 5
#define PIN_MOSI 18
#define PIN_DC 19
#define PIN_CS_A 22
#define PIN_RST_A 23
#define PIN_CS_B 15
#define PIN_RST_B 4

#define LCD_HOST SPI3_HOST
#define LVGL_BUF_LINES 40

static esp_err_t add_panel(gpio_num_t cs, gpio_num_t rst, const char *name, lv_display_t **out) {
  esp_lcd_panel_io_handle_t io = NULL;
  const esp_lcd_panel_io_spi_config_t io_config = GC9A01_PANEL_IO_SPI_CONFIG(cs, PIN_DC, NULL, NULL);
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io), TAG, "io %s",
                      name);

  esp_lcd_panel_handle_t panel = NULL;
  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = rst,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
      .bits_per_pixel = 16,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(io, &panel_config, &panel), TAG, "panel %s", name);
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset %s", name);
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "init %s", name);
  ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, true), TAG, "invert %s", name);
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "on %s", name);

  const lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = io,
      .panel_handle = panel,
      .buffer_size = UI_HRES * LVGL_BUF_LINES,
      .double_buffer = false,
      .hres = UI_HRES,
      .vres = UI_VRES,
      .monochrome = false,
      .color_format = LV_COLOR_FORMAT_RGB565,
      .rotation =
          {
              .swap_xy = false,
              .mirror_x = true,
              .mirror_y = false,
          },
      .flags =
          {
              .buff_dma = true,
              .buff_spiram = false,
              .swap_bytes = true,
          },
  };
  *out = lvgl_port_add_disp(&disp_cfg);
  return *out != NULL ? ESP_OK : ESP_FAIL;
}

esp_err_t displays_init(lv_display_t **left, lv_display_t **right) {
  const spi_bus_config_t bus_config = GC9A01_PANEL_BUS_SPI_CONFIG(PIN_SCLK, PIN_MOSI, UI_HRES * LVGL_BUF_LINES * 2);
  ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG, "spi bus");

  const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl port");

  ESP_RETURN_ON_ERROR(add_panel(PIN_CS_A, PIN_RST_A, "A", left), TAG, "display A");
  ESP_RETURN_ON_ERROR(add_panel(PIN_CS_B, PIN_RST_B, "B", right), TAG, "display B");
  return ESP_OK;
}
