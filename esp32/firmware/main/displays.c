#include "displays.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_gc9a01.h"
#include "gc9b72.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "displays";

#define LCD_HOST SPI3_HOST
#define LVGL_BUF_LINES 20
#define LCD_SPI_HZ (20 * 1000 * 1000)
#define LCD_HOST_DEVICE_LIMIT 3

typedef struct {
  esp_lcd_panel_io_handle_t io;
  esp_lcd_panel_handle_t panel;
  lv_display_t *display;
} display_instance_t;

/* All CS pins must be inactive before a shared reset is asserted. */
static void reset_panels(const screen_config_t *config) {
  for (size_t i = 0; i < config->count; ++i) {
    const screen_pins_t *pins = screen_pins(i);
    gpio_set_level(pins->cs, 1);
    gpio_set_direction(pins->cs, GPIO_MODE_OUTPUT);
    if (pins->reset >= 0) {
      gpio_set_level(pins->reset, 1);
      gpio_set_direction(pins->reset, GPIO_MODE_OUTPUT);
    }
  }
  vTaskDelay(pdMS_TO_TICKS(1));
  for (size_t i = 0; i < config->count; ++i) {
    const screen_pins_t *pins = screen_pins(i);
    if (pins->reset >= 0) gpio_set_level(pins->reset, 0);
  }
  vTaskDelay(pdMS_TO_TICKS(10));
  for (size_t i = 0; i < config->count; ++i) {
    const screen_pins_t *pins = screen_pins(i);
    if (pins->reset >= 0) gpio_set_level(pins->reset, 1);
  }
  vTaskDelay(pdMS_TO_TICKS(10));
}

static void cleanup(display_instance_t *instances, size_t count, bool lvgl_initialized,
                    bool bus_initialized) {
  for (size_t i = 0; i < count; ++i) {
    if (instances[i].display) lvgl_port_remove_disp(instances[i].display);
    if (instances[i].panel) esp_lcd_panel_del(instances[i].panel);
    if (instances[i].io) esp_lcd_panel_io_del(instances[i].io);
  }
  if (lvgl_initialized) lvgl_port_deinit();
  if (bus_initialized) spi_bus_free(LCD_HOST);
}

static esp_err_t add_panel(const screen_type_t *type, const screen_pins_t *pins,
                            display_instance_t *instance) {
  esp_lcd_panel_io_spi_config_t io_config = GC9A01_PANEL_IO_SPI_CONFIG(pins->cs, screen_dc(), NULL, NULL);
  io_config.pclk_hz = LCD_SPI_HZ;
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &instance->io),
                      TAG, "new SPI IO");
  const bool gc9b72 = strcmp(type->id, "gc9b72_360") == 0;
  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = -1, /* reset_panels() handles individual and shared reset lines once. */
      .rgb_ele_order = gc9b72 ? LCD_RGB_ELEMENT_ORDER_RGB : LCD_RGB_ELEMENT_ORDER_BGR,
      .bits_per_pixel = 16,
  };
  /* Profiles preserve the existing GC9A01 orientation while matching the
   * GC9B72 reference sequence's RGB order and unmirrored coordinates. */
  esp_err_t err = gc9b72 ? esp_lcd_new_panel_gc9b72(instance->io, &panel_config, &instance->panel)
                        : esp_lcd_new_panel_gc9a01(instance->io, &panel_config, &instance->panel);
  ESP_RETURN_ON_ERROR(err, TAG, "new panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(instance->panel), TAG, "software reset");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(instance->panel), TAG, "panel init");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(instance->panel, !gc9b72), TAG, "panel invert");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(instance->panel, true), TAG, "panel on");

  const uint32_t partial_pixels = type->width * LVGL_BUF_LINES;
  /* LVGL draws into PSRAM when available; the port copies each partial strip
   * into its internal DMA transfer buffer before sending it on the shared bus. */
  const bool psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) != 0;
  const lvgl_port_display_cfg_t display_config = {
      .io_handle = instance->io,
      .panel_handle = instance->panel,
      .buffer_size = partial_pixels,
      .trans_size = partial_pixels,
      .double_buffer = false,
      .hres = type->width,
      .vres = type->height,
      .monochrome = false,
      .color_format = LV_COLOR_FORMAT_RGB565,
      .rotation = {.swap_xy = false, .mirror_x = !gc9b72, .mirror_y = false},
      .flags = {.buff_dma = !psram, .buff_spiram = psram, .swap_bytes = true},
  };
  instance->display = lvgl_port_add_disp(&display_config);
  return instance->display ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t displays_init(const screen_config_t *config, lv_display_t **out) {
  ESP_RETURN_ON_ERROR(screen_config_validate(config), TAG, "invalid screen configuration");
  if (!out) return ESP_ERR_INVALID_ARG;
  if (config->count > LCD_HOST_DEVICE_LIMIT) {
    ESP_LOGE(TAG, "ESP32 SPI host supports at most %u panel IO devices", LCD_HOST_DEVICE_LIMIT);
    return ESP_ERR_NOT_SUPPORTED;
  }
  memset(out, 0, config->count * sizeof(*out));

  size_t max_width = 0;
  for (size_t i = 0; i < config->count; ++i) {
    const screen_type_t *type = screen_type(config->screens[i].type);
    if (!type) {
      ESP_LOGE(TAG, "screen %u uses an unsupported controller", (unsigned)(i + 1));
      return ESP_ERR_NOT_SUPPORTED;
    }
    if (type->width > max_width) max_width = type->width;
  }

  const spi_bus_config_t bus_config = GC9A01_PANEL_BUS_SPI_CONFIG(
      screen_sclk(), screen_mosi(), max_width * LVGL_BUF_LINES * sizeof(uint16_t));
  esp_err_t err = spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) return err;
  display_instance_t instances[SCREEN_MAX_COUNT] = {0};
  reset_panels(config);

  const lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
  bool lvgl_initialized = false;
  err = lvgl_port_init(&lvgl_config);
  if (err != ESP_OK) goto fail;
  lvgl_initialized = true;
  for (size_t i = 0; i < config->count; ++i) {
    err = add_panel(screen_type(config->screens[i].type), screen_pins(i), &instances[i]);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "screen %u initialization failed (%s)", (unsigned)(i + 1), esp_err_to_name(err));
      goto fail;
    }
    out[i] = instances[i].display;
  }
  ESP_LOGI(TAG, "initialized %u display(s) at %u Hz", (unsigned)config->count, LCD_SPI_HZ);
  return ESP_OK;

fail:
  cleanup(instances, config->count, lvgl_initialized, true);
  memset(out, 0, config->count * sizeof(*out));
  return err;
}
