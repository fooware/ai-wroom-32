#include "ui.h"

#include <stdio.h>

#define COLOR_TRACK lv_color_hex(0x2a333c)
#define COLOR_MUTED lv_color_hex(0xa8b4c0)
#define COLOR_CODEX lv_color_hex(0x00a878)
#define COLOR_CURSOR lv_color_hex(0xf54e00)

/* LVGL angles run clockwise from 3 o'clock, so 270 is the top. */
#define ARC_TOP_DEG 270
#define ARC_SPAN_DEG 127
#define ARC_TOP_GAP_DEG 9
#define ARC_WIDTH 24
#define CENTER_WIDTH 150
#define BAR_LABEL_X 30

static int clamp_pct(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

static void style_screen(lv_obj_t *scr) {
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clean(scr);
  /* Deleting children alone leaves the previous mode's pixels on the panel. */
  lv_obj_invalidate(scr);
}

static lv_obj_t *make_label(lv_obj_t *parent, lv_color_t color, const char *text) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, color, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  return lbl;
}

static void create_side_arc(lv_obj_t *parent, bool left_side, lv_color_t accent, ui_side_bar_t *out) {
  lv_obj_t *arc = lv_arc_create(parent);
  lv_obj_set_size(arc, 228, 228);
  lv_obj_center(arc);
  lv_arc_set_range(arc, 0, 100);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, COLOR_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);
  /* 0% at the bottom, 100% near the top; remaining drains toward empty. */
  if (left_side) {
    lv_arc_set_bg_angles(arc, ARC_TOP_DEG - ARC_TOP_GAP_DEG - ARC_SPAN_DEG, ARC_TOP_DEG - ARC_TOP_GAP_DEG);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
  } else {
    lv_arc_set_bg_angles(arc, ARC_TOP_DEG + ARC_TOP_GAP_DEG,
                         (ARC_TOP_DEG + ARC_TOP_GAP_DEG + ARC_SPAN_DEG) % 360);
    lv_arc_set_mode(arc, LV_ARC_MODE_REVERSE);
  }

  lv_obj_t *name = lv_label_create(parent);
  lv_obj_set_style_text_color(name, COLOR_MUTED, 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
  lv_obj_t *percent = lv_label_create(parent);
  lv_obj_set_style_text_color(percent, lv_color_white(), 0);
  lv_obj_set_style_text_font(percent, &lv_font_montserrat_16, 0);
  /* Both labels sit below the open end of their own arc. */
  const int32_t x_offset = left_side ? -BAR_LABEL_X : BAR_LABEL_X;
  lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(percent, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(name, LV_ALIGN_BOTTOM_MID, x_offset, -36);
  lv_obj_align(percent, LV_ALIGN_BOTTOM_MID, x_offset, -16);

  out->arc = arc;
  out->name = name;
  out->percent = percent;
}

static void set_side_bar(ui_side_bar_t *bar, const char *name, int remaining_pct) {
  remaining_pct = clamp_pct(remaining_pct);
  lv_label_set_text(bar->name, name);
  lv_arc_set_value(bar->arc, remaining_pct);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", remaining_pct);
  lv_label_set_text(bar->percent, buf);
}

static void create_center(lv_obj_t *parent, ui_face_t *face) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, CENTER_WIDTH, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(box, 2, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, -10);

  face->title = make_label(box, lv_color_white(), "");
  lv_obj_set_style_text_font(face->title, &lv_font_montserrat_34, 0);
  face->line1 = make_label(box, COLOR_MUTED, "");
  face->line2 = make_label(box, COLOR_MUTED, "");
  face->line3 = make_label(box, COLOR_MUTED, "");
  lv_obj_set_style_text_font(face->line1, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_font(face->line2, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_font(face->line3, &lv_font_montserrat_18, 0);
  lv_obj_set_width(face->title, CENTER_WIDTH);
  lv_obj_set_width(face->line1, CENTER_WIDTH);
  lv_obj_set_width(face->line2, CENTER_WIDTH);
  lv_obj_set_width(face->line3, CENTER_WIDTH);
}

static void create_face(lv_display_t *disp, lv_color_t accent, ui_face_t *out) {
  lv_display_set_default(disp);
  lv_obj_t *scr = lv_display_get_screen_active(disp);
  style_screen(scr);
  out->root = scr;
  create_side_arc(scr, true, accent, &out->left_bar);
  create_side_arc(scr, false, accent, &out->right_bar);
  create_center(scr, out);
}

void ui_codex_apply(ui_face_t *face, const ui_codex_data_t *data) {
  set_side_bar(&face->left_bar, "5h", data->primary_left_pct);
  set_side_bar(&face->right_bar, "1w", data->weekly_left_pct);
  lv_label_set_text(face->title, "Codex");
  lv_label_set_text(face->line1, data->primary_until);
  lv_label_set_text(face->line2, data->weekly_reset);
  lv_label_set_text(face->line3, data->free_resets);
}

void ui_cursor_apply(ui_face_t *face, const ui_cursor_data_t *data) {
  set_side_bar(&face->left_bar, "Auto", data->auto_left_pct);
  set_side_bar(&face->right_bar, "API", data->named_left_pct);
  lv_label_set_text(face->title, "Cursor");
  lv_label_set_text(face->line1, data->on_demand_used);
  lv_label_set_text(face->line2, data->team_on_demand_left);
  lv_label_set_text(face->line3, data->until_reset);
}

void ui_demo_create(lv_display_t *codex_disp, lv_display_t *cursor_disp, ui_demo_t *out) {
  create_face(codex_disp, COLOR_CODEX, &out->codex);
  create_face(cursor_disp, COLOR_CURSOR, &out->cursor);
}
