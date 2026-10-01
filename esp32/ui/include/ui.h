#pragma once

#include "lvgl.h"
#include "provider.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_HRES 240
#define UI_VRES 240
#define UI_STAR_COUNT 7

typedef struct {
  int16_t left_x;
  int16_t right_x;
} ui_bar_label_pad_t;

typedef struct {
  lv_obj_t *dot;
  int32_t x_q8;
  int32_t y_q8;
  int16_t slope_pct;
  uint8_t side;
} ui_star_t;

typedef struct {
  lv_obj_t *left_root;
  lv_obj_t *right_root;
  lv_obj_t *hint;
  lv_obj_t *ip;
  lv_obj_t *pin;
  lv_timer_t *timer;
  ui_star_t stars[UI_STAR_COUNT];
  uint32_t rng;
} ui_attraction_t;

typedef struct {
  lv_obj_t *name;
  lv_obj_t *percent;
  lv_obj_t *arc;
} ui_side_bar_t;

typedef struct {
  lv_obj_t *root;
  ui_side_bar_t left_bar;
  ui_side_bar_t right_bar;
  lv_obj_t *title;
  lv_obj_t *line1;
  lv_obj_t *line2;
  lv_obj_t *line3;
} ui_face_t;

void ui_face_create(lv_display_t *display, provider_id_t provider, ui_face_t *out);
void ui_provider_apply(ui_face_t *face, provider_id_t provider, const provider_data_t *data);

/* Show one animated 480x240 starfield split across the two displays. */
void ui_attraction_create(lv_display_t *left_disp, lv_display_t *right_disp,
                          const char *ip_address, uint16_t pin, uint32_t random_seed,
                          ui_attraction_t *out);
void ui_attraction_set_connection(ui_attraction_t *ui, const char *ip_address, uint16_t pin);
/* Replace the connection line with a fetch failure reason, in place of CONNECTED. */
void ui_attraction_set_status(ui_attraction_t *ui, const char *status);
void ui_attraction_destroy(ui_attraction_t *ui);

#ifdef __cplusplus
}
#endif
