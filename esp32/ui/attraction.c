#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STAR_TICK_MS 33
#define COLOR_HINT lv_color_hex(0xa8b4c0)
#define COLOR_WARNING lv_color_hex(0xffb020)
/* Each glass carries the accent of the service whose meter it becomes. */

/* Geometric spacing keeps each star at a different point in its travel time. */
static const uint8_t INITIAL_DISTANCE[UI_STAR_COUNT] = {3, 6, 11, 20, 37, 69, 128};

static uint32_t next_random(ui_attraction_screen_t *screen) {
  uint32_t x = screen->rng;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  screen->rng = x ? x : 0x6d2b79f5U;
  return screen->rng;
}

static void reset_star(ui_attraction_screen_t *screen, ui_star_t *star, int32_t distance) {
  int slope = star->slope_pct + (int)(next_random(screen) % 13) - 6;
  star->x_q8 = (star->side ? distance : -distance) * 256;
  star->y_q8 = slope * distance * 256 / 100;
}

static void position_star(ui_attraction_screen_t *screen, ui_star_t *star) {
  int x = star->x_q8 / 256;
  int y = star->y_q8 / 256;
  int distance = x < 0 ? -x : x;
  int size = ui_scale_px(screen->width, screen->height,
                         distance > screen->width / 3 ? 3 : 2);
  lv_obj_set_size(star->dot, size, size);
  lv_obj_set_pos(star->dot, screen->width / 2 + x - size / 2,
                screen->height / 2 + y - size / 2);
}

static void starfield_tick(lv_timer_t *timer) {
  ui_attraction_t *ui = lv_timer_get_user_data(timer);
  for (size_t n = 0; n < ui->count; ++n) {
    ui_attraction_screen_t *screen = &ui->screens[n];
    for (size_t i = 0; i < UI_STAR_COUNT; ++i) {
      ui_star_t *star = &screen->stars[i];
      star->x_q8 += star->x_q8 / 28;
      star->y_q8 += star->y_q8 / 28;
      if (abs(star->x_q8 / 256) > screen->width / 2 ||
          abs(star->y_q8 / 256) > screen->height / 2) {
        reset_star(screen, star, ui_scale_px(screen->width, screen->height, 3));
      }
      position_star(screen, star);
    }
  }
}

static void style_screen(lv_obj_t *screen) {
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clean(screen);
  /* Deleting children alone leaves the previous mode's pixels on the panel. */
  lv_obj_invalidate(screen);
}

static lv_obj_t *make_text(lv_obj_t *parent, const char *text, const lv_font_t *font,
                           lv_color_t color, lv_align_t align, int32_t y, int32_t width) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  if (width > 0) {
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  }
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, align, 0, y);
  return label;
}

void ui_attraction_set_connection(ui_attraction_t *ui, const char *ip_address, uint16_t pin) {
  if (ui == NULL || ui->hint == NULL || ui->ip == NULL || ui->pin == NULL) {
    return;
  }
  char pin_text[16];
  const bool has_text = ip_address != NULL && ip_address[0] != '\0';
  const bool softap = has_text && strncmp(ip_address, "AIOM-", 5) == 0;
  const bool got_ip = has_text && ip_address[0] >= '0' && ip_address[0] <= '9' &&
                      strchr(ip_address, '.') != NULL;

  snprintf(pin_text, sizeof(pin_text), "PIN %04u", (unsigned)(pin % 10000));
  lv_obj_set_style_text_color(ui->hint, COLOR_HINT, 0);
  if (softap || !has_text) {
    lv_label_set_text(ui->hint, "Use ESP SoftAP Provisioning app and connect to:");
    lv_label_set_text(ui->ip, has_text ? ip_address : "Wi-Fi setup");
  } else if (got_ip) {
    lv_label_set_text(ui->hint, "CONNECTED");
    lv_label_set_text(ui->ip, ip_address);
  } else {
    lv_label_set_text(ui->hint, "CONNECT");
    lv_label_set_text(ui->ip, ip_address);
  }
  lv_label_set_text(ui->pin, pin_text);
}

void ui_attraction_set_status(ui_attraction_t *ui, const char *status) {
  if (ui == NULL || ui->hint == NULL || status == NULL) {
    return;
  }
  lv_label_set_text(ui->hint, status);
  lv_obj_set_style_text_color(ui->hint, COLOR_WARNING, 0);
}

void ui_attraction_create(lv_display_t *const *displays, const provider_id_t *providers,
                          size_t count, const char *ip_address, uint16_t pin,
                          uint32_t random_seed, ui_attraction_t *out) {
  memset(out, 0, sizeof(*out));
  if (!displays || !providers || count < 1 || count > UI_MAX_SCREENS) return;
  out->count = count;
  for (size_t n = 0; n < count; ++n) {
    ui_attraction_screen_t *screen = &out->screens[n];
    screen->rng = (random_seed + (uint32_t)n * 2654435761U) | 1U;
    screen->width = lv_display_get_horizontal_resolution(displays[n]);
    screen->height = lv_display_get_vertical_resolution(displays[n]);
    screen->root = lv_display_get_screen_active(displays[n]);
    style_screen(screen->root);
    for (size_t i = 0; i < UI_STAR_COUNT; ++i) {
      ui_star_t *star = &screen->stars[i];
      star->side = i & 1U;
      star->slope_pct = -90 + (int)i * 180 / (UI_STAR_COUNT - 1);
      star->dot = lv_obj_create(screen->root);
      lv_obj_remove_style_all(star->dot);
      lv_obj_set_style_bg_color(star->dot, lv_color_white(), 0);
      lv_obj_set_style_bg_opa(star->dot, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(star->dot, LV_RADIUS_CIRCLE, 0);
      /* Seed distances in that panel's pixels so mixed resolutions have equal depth. */
      reset_star(screen, star,
                 ui_scale_px(screen->width, screen->height, INITIAL_DISTANCE[i] / 2 + 1));
      position_star(screen, star);
    }
    const provider_info_t *info = provider_info(providers[n]);
    lv_color_t accent = lv_color_hex(info ? info->accent : 0x00a878);
    const bool large = ui_is_large_display(screen->width, screen->height);
    if (n + 1 == count) {
      out->hint = make_text(screen->root, "",
                            large ? &lv_font_montserrat_24 : &lv_font_montserrat_16,
                            COLOR_HINT, LV_ALIGN_CENTER,
                            -ui_scale_px(screen->width, screen->height, 58),
                            ui_scale_px(screen->width, screen->height, 168));
      out->ip = make_text(screen->root, "",
                          large ? &lv_font_montserrat_34 : &lv_font_montserrat_28, accent,
                          LV_ALIGN_CENTER, ui_scale_px(screen->width, screen->height, 4), 0);
      out->pin = make_text(screen->root, "",
                           large ? &lv_font_montserrat_48 : &lv_font_montserrat_28, accent,
                           LV_ALIGN_CENTER, ui_scale_px(screen->width, screen->height, 40), 0);
    } else {
      make_text(screen->root, "AI-O-\nMETER", &lv_font_montserrat_48, accent,
                 LV_ALIGN_CENTER, -ui_scale_px(screen->width, screen->height, 4),
                 ui_scale_px(screen->width, screen->height, 200));
    }
  }
  ui_attraction_set_connection(out, ip_address, pin);
  out->timer = lv_timer_create(starfield_tick, STAR_TICK_MS, out);
}

void ui_attraction_destroy(ui_attraction_t *ui) {
  if (ui == NULL || ui->timer == NULL) {
    return;
  }
  lv_timer_delete(ui->timer);
  ui->timer = NULL;
}
