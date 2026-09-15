#define SDL_MAIN_HANDLED
#include "lvgl.h"
#include "ui.h"

#include <stdio.h>

/*
 * Boot layout for local UI work. Flip to 1 here, or configure with
 *   cmake -S esp32/sim -B esp32/sim/build -DSIM_START_METERS=ON
 */
#ifndef SIM_START_METERS
#define SIM_START_METERS 0
#endif

#if SIM_START_METERS
static void show_meter_preview(lv_display_t *left, lv_display_t *right) {
  ui_demo_t demo;
  ui_demo_create(left, right, &demo);

  const ui_codex_data_t codex = {
      .primary_left_pct = 72,
      .weekly_left_pct = 41,
      .primary_until = "3h 12m",
      .weekly_reset = "4d 6h",
      .free_resets = "2 free resets",
  };
  const ui_cursor_data_t cursor = {
      .auto_left_pct = 79,
      .named_left_pct = 12,
      .on_demand_used = "$37.15 used",
      .team_on_demand_left = "$806 team left",
      .until_reset = "24d 9h",
  };
  ui_codex_apply(&demo.codex, &codex);
  ui_cursor_apply(&demo.cursor, &cursor);
}
#endif

int main(void) {
  lv_init();

  lv_display_t *left = lv_sdl_window_create(UI_HRES, UI_VRES);
  lv_display_t *right = lv_sdl_window_create(UI_HRES, UI_VRES);
  if (left == NULL || right == NULL) {
    fprintf(stderr, "Failed to create SDL windows\n");
    return 1;
  }

  lv_sdl_window_set_title(left, "ai-wroom-32 left (Codex)");
  lv_sdl_window_set_title(right, "ai-wroom-32 right (Cursor)");
  lv_indev_t *mouse = lv_sdl_mouse_create();
  (void)mouse;

#if SIM_START_METERS
  show_meter_preview(left, right);
#else
  ui_attraction_t attraction;
  ui_attraction_create(left, right, "192.168.1.42", 4827, 0x12345678, &attraction);
#endif

  while (true) {
    uint32_t wait_ms = lv_timer_handler();
    if (wait_ms > 32) {
      wait_ms = 32;
    }
    lv_delay_ms(wait_ms);
  }
}
