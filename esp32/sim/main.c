#define SDL_MAIN_HANDLED
#include "lvgl.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

/*
 * Boot layout for local UI work. Flip to 1 here, or configure with
 *   cmake -S esp32/sim -B esp32/sim/build -DSIM_START_METERS=ON
 */
#ifndef SIM_START_METERS
#define SIM_START_METERS 0
#endif

static void show_meter_preview(lv_display_t *left, lv_display_t *right, provider_id_t second) {
  ui_face_t faces[2];
  ui_face_create(left, PROVIDER_CODEX, &faces[0]);
  ui_face_create(right, second, &faces[1]);
  const provider_data_t sample = {
    .available = true, .remaining = {72, 41},
    .lines = {"Sample usage", "3h 12m", "4d 6h"},
  };
  ui_provider_apply(&faces[0], PROVIDER_CODEX, &sample);
  ui_provider_apply(&faces[1], second, &sample);
}

int main(int argc, char **argv) {
  bool smoke = false;
  bool meters = SIM_START_METERS;
  provider_id_t second = PROVIDER_CURSOR;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--smoke")) smoke = true;
    else if (!strcmp(argv[i], "--meters")) meters = true;
    else if (!strcmp(argv[i], "--claude")) { second = PROVIDER_CLAUDE; meters = true; }
    else { fprintf(stderr, "Usage: sim [--meters] [--claude] [--smoke]\n"); return 2; }
  }
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

  ui_attraction_t attraction = {0};
  if (meters) show_meter_preview(left, right, second);
  else ui_attraction_create(left, right, "192.168.1.42", 4827, 0x12345678, &attraction);
  unsigned iterations = 0;

  while (true) {
    if (smoke && ++iterations > 5) {
      ui_attraction_destroy(&attraction);
      lv_display_delete(left);
      lv_display_delete(right);
      return 0;
    }
    uint32_t wait_ms = lv_timer_handler();
    if (wait_ms > 32) {
      wait_ms = 32;
    }
    lv_delay_ms(wait_ms);
  }
}
