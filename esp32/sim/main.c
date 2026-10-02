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

/* Export LVGL's rendered pixels for inspecting round-panel layouts headlessly. */
static bool save_snapshot(lv_display_t *display, const char *prefix, size_t index) {
  lv_obj_t *root = lv_display_get_screen_active(display);
  lv_obj_update_layout(root);
  lv_draw_buf_t *image = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB888);
  if (!image) return false;
  char filename[1024];
  int length = snprintf(filename, sizeof(filename), "%s-%zu.ppm", prefix, index + 1);
  FILE *file = length > 0 && length < (int)sizeof(filename) ? fopen(filename, "wb") : NULL;
  if (!file) { lv_draw_buf_destroy(image); return false; }
  fprintf(file, "P6\n%u %u\n255\n", (unsigned)image->header.w, (unsigned)image->header.h);
  /* LVGL's RGB888 byte layout is B,G,R; PPM expects R,G,B. */
  bool ok = true;
  for (unsigned y = 0; y < image->header.h; ++y) {
    for (unsigned x = 0; x < image->header.w; ++x) {
      const uint8_t *pixel = image->data + y * image->header.stride + x * 3;
      uint8_t rgb[] = {pixel[2], pixel[1], pixel[0]};
      if (fwrite(rgb, 1, 3, file) != 3) ok = false;
    }
  }
  if (fclose(file)) ok = false;
  lv_draw_buf_destroy(image);
  return ok;
}

int main(int argc, char **argv) {
  bool smoke = false;
  const char *snapshot_prefix = NULL;
  bool meters = SIM_START_METERS;
  provider_id_t providers[UI_MAX_SCREENS] = {PROVIDER_CODEX, PROVIDER_CURSOR, PROVIDER_CLAUDE};
  size_t count = 2;
  int resolutions[UI_MAX_SCREENS] = {240, 240, 240};
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--smoke")) smoke = true;
    else if (!strcmp(argv[i], "--snapshot") && i + 1 < argc) snapshot_prefix = argv[++i];
    else if (!strcmp(argv[i], "--meters")) meters = true;
    else if (!strcmp(argv[i], "--claude")) { providers[1] = PROVIDER_CLAUDE; meters = true; }
    else if (!strcmp(argv[i], "--count") && i + 1 < argc) {
      char *end; long n = strtol(argv[++i], &end, 10);
      if (*end || n < 1 || n > UI_MAX_SCREENS) return 2;
      count = (size_t)n;
    }
    else if (!strcmp(argv[i], "--screens") && i + 1 < argc) {
      /* Same ordered type:provider pairs as the device JSON, without an IP/PIN. */
      char *entries = argv[++i];
      char *save = NULL;
      count = 0;
      for (char *entry = strtok_r(entries, ",", &save); entry; entry = strtok_r(NULL, ",", &save)) {
        char *separator = strchr(entry, ':');
        if (!separator || count == UI_MAX_SCREENS) return 2;
        *separator++ = '\0';
        if (!strcmp(entry, "gc9a01_240")) resolutions[count] = 240;
        else if (!strcmp(entry, "gc9b72_360")) resolutions[count] = 360;
        else return 2;
        if (!provider_from_name(separator, &providers[count])) return 2;
        ++count;
      }
      if (!count) return 2;
    }
    else { fprintf(stderr, "Usage: sim [--meters] [--claude] [--smoke] [--count 1..3] [--screens type:provider,...] [--snapshot path-prefix]\n"); return 2; }
  }
  lv_init();

  lv_display_t *displays[UI_MAX_SCREENS] = {0};
  ui_face_t faces[UI_MAX_SCREENS];
  const provider_data_t sample = {
    .available = true, .remaining = {72, 41},
    .lines = {"Sample usage", "3h 12m", "4d 6h"},
  };
  for (size_t i = 0; i < count; ++i) {
    displays[i] = lv_sdl_window_create(resolutions[i], resolutions[i]);
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
      for (size_t i = 0; snapshot_prefix && i < count; ++i) {
        if (!save_snapshot(displays[i], snapshot_prefix, i)) return 1;
      }
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
