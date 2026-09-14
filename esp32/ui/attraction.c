#include "ui.h"

#include <stdio.h>
#include <string.h>

#define STAR_CENTER_Y 120
#define STAR_RESET_X 230
#define STAR_RESET_Y 118
#define STAR_TICK_MS 33
#define COLOR_HINT lv_color_hex(0xa8b4c0)
#define COLOR_WARNING lv_color_hex(0xffb020)
/* Each glass carries the accent of the service whose meter it becomes. */
#define COLOR_CODEX lv_color_hex(0x00a878)
#define COLOR_CURSOR lv_color_hex(0xf54e00)

/* Geometric spacing keeps each star at a different point in its travel time. */
static const uint8_t INITIAL_DISTANCE[UI_STAR_COUNT] = {3, 6, 11, 20, 37, 69, 128};

static uint32_t next_random(ui_attraction_t *ui) {
  uint32_t x = ui->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  ui->rng = x != 0 ? x : 0x6d2b79f5U;
  return ui->rng;
}

static void reset_star(ui_attraction_t *ui, ui_star_t *star, int32_t distance) {
  const int32_t slope = star->slope_pct + (int32_t)(next_random(ui) % 13) - 6;
  star->x_q8 = (star->side == 0 ? -distance : distance) << 8;
  star->y_q8 = (slope * distance * STAR_RESET_Y / (100 * STAR_RESET_X)) << 8;
}

static void position_star(ui_star_t *star) {
  const int32_t x = star->x_q8 >> 8;
  const int32_t y = star->y_q8 >> 8;
  const int32_t local_x = star->side == 0 ? UI_HRES + x : x;
  const int32_t distance = x < 0 ? -x : x;
  const int32_t size = distance > 150 ? 4 : (distance > 75 ? 3 : 2);

  lv_obj_set_size(star->dot, size, size);
  lv_obj_set_pos(star->dot, local_x - size / 2, STAR_CENTER_Y + y - size / 2);
  lv_obj_set_style_opa(star->dot, distance > 100 ? LV_OPA_COVER : LV_OPA_70, 0);
}

static void starfield_tick(lv_timer_t *timer) {
  ui_attraction_t *ui = lv_timer_get_user_data(timer);
  for (size_t i = 0; i < UI_STAR_COUNT; ++i) {
    ui_star_t *star = &ui->stars[i];
    star->x_q8 += star->x_q8 / 28;
    star->y_q8 += star->y_q8 / 28;

    const int32_t x = star->x_q8 >> 8;
    const int32_t y = star->y_q8 >> 8;
    if ((x < 0 ? -x : x) > STAR_RESET_X || (y < 0 ? -y : y) > STAR_RESET_Y) {
      reset_star(ui, star, 3);
    }
    position_star(star);
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

void ui_attraction_create(lv_display_t *left_disp, lv_display_t *right_disp,
                          const char *ip_address, uint16_t pin, uint32_t random_seed,
                          ui_attraction_t *out) {
  memset(out, 0, sizeof(*out));
  out->rng = random_seed != 0 ? random_seed : 0x6d2b79f5U;

  lv_display_set_default(left_disp);
  out->left_root = lv_display_get_screen_active(left_disp);
  style_screen(out->left_root);

  lv_display_set_default(right_disp);
  out->right_root = lv_display_get_screen_active(right_disp);
  style_screen(out->right_root);

  for (size_t i = 0; i < UI_STAR_COUNT; ++i) {
    ui_star_t *star = &out->stars[i];
    const uint8_t side = i & 1U;
    const uint8_t lane = i / 2;
    const uint8_t lane_count = side == 0 ? (UI_STAR_COUNT + 1) / 2 : UI_STAR_COUNT / 2;
    lv_obj_t *parent = side == 0 ? out->left_root : out->right_root;
    star->side = side;
    star->slope_pct = lane_count > 1 ? -90 + (180 * lane) / (lane_count - 1) : 0;
    star->dot = lv_obj_create(parent);
    lv_obj_remove_style_all(star->dot);
    lv_obj_set_style_bg_color(star->dot, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(star->dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(star->dot, LV_RADIUS_CIRCLE, 0);
    reset_star(out, star, INITIAL_DISTANCE[i]);
    position_star(star);
  }

  make_text(out->left_root, "AI-O-\nMETER", &lv_font_montserrat_48,
            COLOR_CODEX, LV_ALIGN_CENTER, -4, 200);

  out->hint = make_text(out->right_root, "", &lv_font_montserrat_16,
                        COLOR_HINT, LV_ALIGN_CENTER, -58, 168);
  out->ip = make_text(out->right_root, "", &lv_font_montserrat_28,
                      COLOR_CURSOR, LV_ALIGN_CENTER, 4, 0);
  out->pin = make_text(out->right_root, "", &lv_font_montserrat_28,
                       COLOR_CURSOR, LV_ALIGN_CENTER, 40, 0);
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
