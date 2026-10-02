#include "driver/gpio.h"
#include "esp_lcd_panel_commands.h"
#include "gc9b72.h"
#include <assert.h>
#include <string.h>

/* Fake panel IO records command bytes and color lengths for bus assertions. */
typedef struct {
  int command;
  unsigned char data[32];
  size_t length;
} call_t;
static call_t calls[160];
static int call_count;
static size_t color_length;
static int fail_command = -1;
static unsigned delays[8];
static int delay_count;
int fake_gpio_valid(int gpio) { return gpio >= 0 && gpio < 40; }
esp_err_t gpio_config(const gpio_config_t *config) {
  (void)config;
  return ESP_OK;
}
esp_err_t gpio_set_level(int gpio, int level) {
  (void)gpio;
  (void)level;
  return ESP_OK;
}
esp_err_t gpio_reset_pin(int gpio) {
  (void)gpio;
  return ESP_OK;
}
void vTaskDelay(unsigned ticks) {
  if (delay_count < 8)
    delays[delay_count++] = ticks;
}
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int cmd,
                                    const void *data, size_t len) {
  (void)io;
  if (cmd == fail_command)
    return ESP_FAIL;
  calls[call_count].command = cmd;
  calls[call_count].length = len;
  if (data)
    memcpy(calls[call_count].data, data, len);
  ++call_count;
  return ESP_OK;
}
esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io, int cmd,
                                    const void *data, size_t len) {
  (void)io;
  (void)data;
  if (cmd == fail_command)
    return ESP_FAIL;
  calls[call_count++] = (call_t){.command = cmd, .length = len};
  color_length = len;
  return ESP_OK;
}
static esp_lcd_panel_handle_t new_panel(lcd_rgb_element_order_t order) {
  esp_lcd_panel_handle_t p = NULL;
  esp_lcd_panel_dev_config_t c = {
      .reset_gpio_num = -1, .bits_per_pixel = 16, .rgb_ele_order = order};
  assert(esp_lcd_new_panel_gc9b72((void *)1, &c, &p) == ESP_OK);
  return p;
}
static void clear(void) {
  call_count = 0;
  color_length = 0;
  fail_command = -1;
  delay_count = 0;
}
static void test_draw_and_bounds(void) {
  esp_lcd_panel_handle_t p = new_panel(LCD_RGB_ELEMENT_ORDER_RGB);
  unsigned short pixels[6] = {0};
  clear();
  assert(p->draw_bitmap(p, 1, 2, 4, 4, pixels) == ESP_OK);
  assert(call_count == 3 && calls[0].command == LCD_CMD_CASET &&
         calls[1].command == LCD_CMD_RASET &&
         calls[2].command == LCD_CMD_RAMWR);
  unsigned char col[] = {0, 1, 0, 3}, row[] = {0, 2, 0, 3};
  assert(!memcmp(calls[0].data, col, 4) && !memcmp(calls[1].data, row, 4) &&
         color_length == 12);
  assert(p->draw_bitmap(p, -1, 0, 1, 1, pixels) == ESP_ERR_INVALID_ARG);
  assert(p->draw_bitmap(p, 0, 0, 361, 1, pixels) == ESP_ERR_INVALID_ARG);
  assert(p->del(p) == ESP_OK);
}
static void test_reset_init_and_orientation(void) {
  esp_lcd_panel_handle_t p = new_panel(LCD_RGB_ELEMENT_ORDER_BGR);
  clear();
  assert(p->reset(p) == ESP_OK && call_count == 1 &&
         calls[0].command == LCD_CMD_SWRESET && delay_count == 1 &&
         delays[0] == 20);
  clear();
  assert(p->init(p) == ESP_OK);
  int colmod = -1, madctl = -1, slp = -1, disp = -1;
  for (int i = 0; i < call_count; i++) {
    if (calls[i].command == LCD_CMD_COLMOD)
      colmod = i;
    if (calls[i].command == LCD_CMD_MADCTL)
      madctl = i;
    if (calls[i].command == LCD_CMD_SLPOUT)
      slp = i;
    if (calls[i].command == LCD_CMD_DISPON)
      disp = i;
  }
  assert(colmod >= 0 && calls[colmod].data[0] == 5 && madctl >= 0 &&
         calls[madctl].data[0] == LCD_CMD_BGR_BIT && slp >= 0 && disp > slp &&
         delay_count == 2 && delays[0] == 120 && delays[1] == 20);
  clear();
  assert(p->mirror(p, true, false) == ESP_OK &&
         calls[0].data[0] == (LCD_CMD_BGR_BIT | LCD_CMD_MX_BIT));
  assert(p->swap_xy(p, true) == ESP_OK &&
         calls[1].data[0] ==
             (LCD_CMD_BGR_BIT | LCD_CMD_MX_BIT | LCD_CMD_MV_BIT));
  p->del(p);
}
static void test_constructor_and_errors(void) {
  esp_lcd_panel_dev_config_t c = {.bits_per_pixel = 18,
                                  .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB};
  esp_lcd_panel_handle_t p;
  assert(esp_lcd_new_panel_gc9b72((void *)1, &c, &p) == ESP_ERR_NOT_SUPPORTED);
  assert(esp_lcd_new_panel_gc9b72(NULL, &c, &p) == ESP_ERR_INVALID_ARG);
  c.bits_per_pixel = 16;
  c.reset_gpio_num = 40;
  assert(esp_lcd_new_panel_gc9b72((void *)1, &c, &p) == ESP_ERR_INVALID_ARG);
  p = new_panel(LCD_RGB_ELEMENT_ORDER_RGB);
  clear();
  fail_command = LCD_CMD_RASET;
  unsigned short px = 0;
  assert(p->draw_bitmap(p, 0, 0, 1, 1, &px) == ESP_FAIL && call_count == 1);
  p->del(p);
}
int main(void) {
  test_draw_and_bounds();
  test_reset_init_and_orientation();
  test_constructor_and_errors();
  return 0;
}
