#define SDL_MAIN_HANDLED
#include "lvgl.h"
#include "ui.h"

#include <stdbool.h>
#include <stdio.h>

int main(void) {
  lv_init();

  lv_display_t *left = lv_sdl_window_create(UI_HRES, UI_VRES);
  lv_display_t *right = lv_sdl_window_create(UI_HRES, UI_VRES);
  if (left == NULL || right == NULL) {
    fprintf(stderr, "Failed to create SDL windows\n");
    return 1;
  }

  lv_sdl_window_set_title(left, "ai-wroom-32 left");
  lv_sdl_window_set_title(right, "ai-wroom-32 right");
  lv_indev_t *mouse = lv_sdl_mouse_create();
  (void)mouse;

  ui_attraction_t attraction;
  ui_attraction_create(left, right, "192.168.1.42", 4827, 0x12345678, &attraction);

  while (true) {
    uint32_t wait_ms = lv_timer_handler();
    if (wait_ms > 32) {
      wait_ms = 32;
    }
    lv_delay_ms(wait_ms);
  }
}
