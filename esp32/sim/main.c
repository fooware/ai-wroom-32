#define SDL_MAIN_HANDLED
#include "lvgl.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * Boot layout for local UI work. Flip to 1 here, or configure with
 *   cmake -S esp32/sim -B esp32/sim/build -DSIM_START_METERS=ON
 */
#ifndef SIM_START_METERS
#define SIM_START_METERS 0
#endif

int main(int argc, char **argv) {
  bool smoke = false;
  bool meters = SIM_START_METERS;
  provider_id_t providers[UI_MAX_SCREENS] = {PROVIDER_CODEX, PROVIDER_CURSOR, PROVIDER_CLAUDE};
  size_t count = 2;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--smoke")) smoke = true;
    else if (!strcmp(argv[i], "--meters")) meters = true;
    else if (!strcmp(argv[i], "--claude")) { providers[1] = PROVIDER_CLAUDE; meters = true; }
    else if (!strcmp(argv[i], "--count") && i + 1 < argc) {
      char *end; long n = strtol(argv[++i], &end, 10);
      if (*end || n < 1 || n > UI_MAX_SCREENS) return 2;
      count = (size_t)n;
    }
    else { fprintf(stderr, "Usage: sim [--meters] [--claude] [--smoke] [--count 1..3]\n"); return 2; }
  }
  lv_init();

  lv_display_t *displays[UI_MAX_SCREENS] = {0};
  ui_face_t faces[UI_MAX_SCREENS];
  const provider_data_t sample = {
    .available = true, .remaining = {72, 41},
    .lines = {"Sample usage", "3h 12m", "4d 6h"},
  };
  for (size_t i = 0; i < count; ++i) {
    displays[i] = lv_sdl_window_create(UI_HRES, UI_VRES);
    if (!displays[i]) return 1;
    lv_sdl_window_set_title(displays[i], provider_info(providers[i])->title);
    if (meters) {
      ui_face_create(displays[i], providers[i], &faces[i]);
      ui_provider_apply(&faces[i], providers[i], &sample);
    }
  }
  lv_indev_t *mouse = lv_sdl_mouse_create();
  (void)mouse;
  ui_attraction_t attraction = {0};
  if (!meters) ui_attraction_create(displays, providers, count, "192.168.1.42", 4827, 0x12345678, &attraction);
  unsigned iterations = 0;

  while (true) {
    if (smoke && ++iterations > 5) {
      ui_attraction_destroy(&attraction);
      for (size_t i = 0; i < count; ++i) lv_display_delete(displays[i]);
      return 0;
    }
    uint32_t wait_ms = lv_timer_handler();
    if (wait_ms > 32) {
      wait_ms = 32;
    }
    lv_delay_ms(wait_ms);
  }
}
