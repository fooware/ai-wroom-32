#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_HRES 240
#define UI_VRES 240
#define UI_STAR_COUNT 7

/*
 * Horizontal offset of each arc's name/percent labels from screen center.
 * Positive moves the label toward the outer edge of its arc.
 */
#ifndef UI_CODEX_LEFT_BAR_LABEL_X
#define UI_CODEX_LEFT_BAR_LABEL_X 40
#endif
#ifndef UI_CODEX_RIGHT_BAR_LABEL_X
#define UI_CODEX_RIGHT_BAR_LABEL_X 40
#endif
#ifndef UI_CURSOR_LEFT_BAR_LABEL_X
#define UI_CURSOR_LEFT_BAR_LABEL_X 28
#endif
#ifndef UI_CURSOR_RIGHT_BAR_LABEL_X
#define UI_CURSOR_RIGHT_BAR_LABEL_X 31
#endif

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

typedef struct {
  ui_face_t codex;
  ui_face_t cursor;
} ui_demo_t;

typedef struct {
  int primary_left_pct; /* remaining, 0..100 */
  int weekly_left_pct;
  const char *primary_until; /* hours, minutes */
  const char *weekly_reset;  /* date */
  const char *free_resets;
} ui_codex_data_t;

typedef struct {
  int auto_left_pct;
  int named_left_pct;
  const char *on_demand_used;
  const char *team_on_demand_left;
  const char *until_reset; /* days, hours */
} ui_cursor_data_t;

void ui_demo_create(lv_display_t *codex_disp, lv_display_t *cursor_disp, ui_demo_t *out);
void ui_codex_apply(ui_face_t *face, const ui_codex_data_t *data);
void ui_cursor_apply(ui_face_t *face, const ui_cursor_data_t *data);

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
