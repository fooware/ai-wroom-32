#include "ui.h"

#include <stdio.h>

#define COLOR_TRACK lv_color_hex(0x2a333c)
#define COLOR_MUTED lv_color_hex(0xa8b4c0)

/* LVGL angles run clockwise from 3 o'clock, so 270 is the top. */
#define ARC_TOP_DEG 270
#define ARC_SPAN_DEG 127
#define ARC_TOP_GAP_DEG 9
#define ARC_WIDTH 24
#define CENTER_WIDTH 150

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

static void create_side_arc(lv_obj_t *parent, int width, int height, int32_t label_x,
                            lv_color_t accent, ui_side_bar_t *out) {
  const int arc_size = ui_scale_px(width, height, 228);
  const int arc_width = ui_scale_px(width, height, ARC_WIDTH);
  lv_obj_t *arc = lv_arc_create(parent);
  lv_obj_set_size(arc, arc_size, arc_size);
  lv_obj_center(arc);
  lv_arc_set_range(arc, 0, 100);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arc, arc_width, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, arc_width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, COLOR_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);
  /* 0% at the bottom, 100% near the top; remaining drains toward empty. */
  if (label_x < 0) {
    lv_arc_set_bg_angles(arc, ARC_TOP_DEG - ARC_TOP_GAP_DEG - ARC_SPAN_DEG, ARC_TOP_DEG - ARC_TOP_GAP_DEG);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
  } else {
    lv_arc_set_bg_angles(arc, ARC_TOP_DEG + ARC_TOP_GAP_DEG,
                         (ARC_TOP_DEG + ARC_TOP_GAP_DEG + ARC_SPAN_DEG) % 360);
    lv_arc_set_mode(arc, LV_ARC_MODE_REVERSE);
  }

  lv_obj_t *name = lv_label_create(parent);
  lv_obj_set_style_text_color(name, COLOR_MUTED, 0);
  /* Keep the large face legible without adding the expensive 72px font. */
  const lv_font_t *label_font = ui_is_large_display(width, height)
                                     ? &lv_font_montserrat_24
                                     : &lv_font_montserrat_16;
  lv_obj_set_style_text_font(name, label_font, 0);
  lv_obj_t *percent = lv_label_create(parent);
  lv_obj_set_style_text_color(percent, lv_color_white(), 0);
  lv_obj_set_style_text_font(percent, label_font, 0);
  /* Both labels sit below the open end of their own arc. */
  lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(percent, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(name, LV_ALIGN_BOTTOM_MID, label_x, -ui_scale_px(width, height, 36));
  lv_obj_align(percent, LV_ALIGN_BOTTOM_MID, label_x, -ui_scale_px(width, height, 16));

  out->arc = arc;
  out->name = name;
  out->percent = percent;
}

static void set_side_bar(ui_side_bar_t *bar, const char *name, int remaining_pct) {
  bool unknown = remaining_pct < 0;
  remaining_pct = clamp_pct(remaining_pct);
  lv_label_set_text(bar->name, name);
  lv_arc_set_value(bar->arc, remaining_pct);
  char buf[8];
  if (unknown) snprintf(buf, sizeof(buf), "--");
  else snprintf(buf, sizeof(buf), "%d%%", remaining_pct);
  lv_label_set_text(bar->percent, buf);
}

static void create_center(lv_obj_t *parent, int width, int height, ui_face_t *face) {
  const int center_width = ui_scale_px(width, height, CENTER_WIDTH);
  const bool large = ui_is_large_display(width, height);
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, center_width, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(box, ui_scale_px(width, height, 2), 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, -ui_scale_px(width, height, 10));

  face->title = make_label(box, lv_color_white(), "");
  lv_obj_set_style_text_font(face->title,
                             large ? &lv_font_montserrat_48 : &lv_font_montserrat_34, 0);
  face->line1 = make_label(box, COLOR_MUTED, "");
  face->line2 = make_label(box, COLOR_MUTED, "");
  face->line3 = make_label(box, COLOR_MUTED, "");
  const lv_font_t *line_font = large ? &lv_font_montserrat_28 : &lv_font_montserrat_18;
  lv_obj_set_style_text_font(face->line1, line_font, 0);
  lv_obj_set_style_text_font(face->line2, line_font, 0);
  lv_obj_set_style_text_font(face->line3, line_font, 0);
  lv_obj_set_width(face->title, center_width);
  lv_obj_set_width(face->line1, center_width);
  lv_obj_set_width(face->line2, center_width);
  lv_obj_set_width(face->line3, center_width);
}

static void create_face(lv_display_t *disp, lv_color_t accent, const ui_bar_label_pad_t *label_pad,
                        ui_face_t *out) {
  lv_display_set_default(disp);
  const int width = lv_display_get_horizontal_resolution(disp);
  const int height = lv_display_get_vertical_resolution(disp);
  lv_obj_t *scr = lv_display_get_screen_active(disp);
  style_screen(scr);
  out->root = scr;
  create_side_arc(scr, width, height, -ui_scale_px(width, height, label_pad->left_x), accent,
                  &out->left_bar);
  create_side_arc(scr, width, height, ui_scale_px(width, height, label_pad->right_x), accent,
                  &out->right_bar);
  create_center(scr, width, height, out);
}

void ui_face_create(lv_display_t *display, provider_id_t provider, ui_face_t *out) {
  const provider_info_t *info = provider_info(provider);
  if (!display || !info || !out) return;
  const ui_bar_label_pad_t pad = {info->label_x[0], info->label_x[1]};
  create_face(display, lv_color_hex(info->accent), &pad, out);
  const provider_data_t waiting = {.remaining = {-1, -1}, .status = "Waiting for login"};
  ui_provider_apply(out, provider, &waiting);
}

void ui_provider_apply(ui_face_t *face, provider_id_t provider, const provider_data_t *data) {
  const provider_info_t *info = provider_info(provider);
  if (!face || !info || !data) return;
  lv_label_set_text(face->title, info->title);
  set_side_bar(&face->left_bar, info->bar_names[0], data->available ? data->remaining[0] : -1);
  set_side_bar(&face->right_bar, info->bar_names[1], data->available ? data->remaining[1] : -1);
  lv_label_set_text(face->line1, data->available ? data->lines[0] : data->status);
  lv_label_set_text(face->line2, data->available ? data->lines[1] : "");
  lv_label_set_text(face->line3, data->available ? data->lines[2] : "");
}
