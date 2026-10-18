/**
 * @file haus_touch_test.cpp
 * @brief Guided GT911 calibration: five rounds of four cross points.
 *
 * The test compares each raw GT911 point with the native point that the
 * current rotation implies, then reports mean error, spread and the offset
 * constants that would cancel it. Results are shown on the panel and logged
 * to the debug ring.
 */

#include "haus_touch_test.h"

#include <lvgl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tpl_debug_log.h"
#include "tpl_ws7.h"

#define TT_ROUNDS 5
#define TT_POINTS 4
#define TT_TOTAL (TT_ROUNDS * TT_POINTS)
#define TT_BG 0x0e0c0b
#define TT_INK 0xf3ece4
#define TT_DIM 0x9a9086
#define TT_ACCENT 0xe39b3a
#define TT_GREEN 0x5caf6e

static const int16_t kTargetX[TT_POINTS] = {70, 410, 70, 410};
static const int16_t kTargetY[TT_POINTS] = {90, 90, 690, 690};
static const char* const kTargetName[TT_POINTS] = {"TL", "TR", "BL", "BR"};

static bool s_active = false;
static volatile bool s_start_requested = false;
static lv_obj_t* s_overlay = NULL;
static lv_obj_t* s_hint = NULL;
static lv_obj_t* s_progress = NULL;
static lv_obj_t* s_stats = NULL;
static lv_obj_t* s_cross_h = NULL;
static lv_obj_t* s_cross_v = NULL;
static lv_obj_t* s_cross_dot = NULL;

static int s_index = 0;
static bool s_done = false;
static bool s_was_down = false;
static uint32_t s_release_ms = 0;

static int16_t s_raw_x[TT_TOTAL];
static int16_t s_raw_y[TT_TOTAL];
static int16_t s_tgt_x[TT_TOTAL];
static int16_t s_tgt_y[TT_TOTAL];
static int s_samples = 0;

static char s_stats_json[320] = "";

static lv_obj_t* tt_box(lv_obj_t* parent, int x, int y, int w, int h,
                        uint32_t color) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

static lv_obj_t* tt_label(lv_obj_t* parent, const char* text, int x, int y,
                          const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

static void tt_update_target(void) {
  const int p = s_index % TT_POINTS;
  const int x = kTargetX[p];
  const int y = kTargetY[p];

  lv_obj_set_pos(s_cross_h, x - 30, y - 1);
  lv_obj_set_pos(s_cross_v, x - 1, y - 30);
  lv_obj_set_pos(s_cross_dot, x - 5, y - 5);

  char buf[64];
  snprintf(buf, sizeof(buf), "Point %d / %d   %s", s_index + 1, TT_TOTAL,
           kTargetName[p]);
  lv_label_set_text(s_progress, buf);
}

static void tt_close(void) {
  if (s_overlay) {
    lv_obj_delete(s_overlay);
    s_overlay = NULL;
  }
  s_hint = s_progress = s_stats = NULL;
  s_cross_h = s_cross_v = s_cross_dot = NULL;
  s_active = false;
  s_done = false;
  s_samples = 0;
  s_index = 0;
  s_was_down = false;
}

static void tt_start(void) {
  s_index = 0;
  s_samples = 0;
  s_done = false;
  s_was_down = false;
  s_release_ms = lv_tick_get();
  s_stats_json[0] = '\0';
  s_active = true;

  s_overlay = lv_obj_create(lv_layer_top());
  /* Logical resolution, not the native RGB panel size: with rotation the
   * display is 480x800. Using 800x480 clipped the lower two crosses. */
  lv_obj_set_size(s_overlay, lv_display_get_horizontal_resolution(NULL),
                  lv_display_get_vertical_resolution(NULL));
  lv_obj_set_pos(s_overlay, 0, 0);
  lv_obj_set_style_bg_color(s_overlay, lv_color_hex(TT_BG), 0);
  lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_overlay, 0, 0);
  lv_obj_set_style_radius(s_overlay, 0, 0);
  lv_obj_set_style_pad_all(s_overlay, 0, 0);
  lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_foreground(s_overlay);

  tt_label(s_overlay, "TOUCH TEST", 20, 24, &lv_font_montserrat_32, TT_INK);
  s_hint =
      tt_label(s_overlay, "Tap the cross. 5 rounds x 4 corners = 20 points.",
               20, 66, &lv_font_montserrat_16, TT_DIM);
  s_progress =
      tt_label(s_overlay, "", 20, 730, &lv_font_montserrat_16, TT_ACCENT);
  s_stats = tt_label(s_overlay, "", 20, 520, &lv_font_montserrat_16, TT_DIM);
  lv_obj_set_width(s_stats, 440);
  lv_label_set_long_mode(s_stats, LV_LABEL_LONG_WRAP);

  s_cross_h = tt_box(s_overlay, 0, 0, 60, 3, TT_ACCENT);
  s_cross_v = tt_box(s_overlay, 0, 0, 3, 60, TT_ACCENT);
  s_cross_dot = tt_box(s_overlay, 0, 0, 10, 10, TT_INK);
  lv_obj_set_style_radius(s_cross_dot, LV_RADIUS_CIRCLE, 0);

  tt_update_target();
  TPL_LOGI("touch", "calibration started: %d points", TT_TOTAL);
}

static void tt_compute(void) {
  if (s_samples <= 0) return;

  double sx = 0, sy = 0, sxx = 0, syy = 0;
  int16_t minx = INT16_MAX, maxx = INT16_MIN;
  int16_t miny = INT16_MAX, maxy = INT16_MIN;

  for (int i = 0; i < s_samples; i++) {
    /* Rotation index 0: native x = logical y; native y = H-1-logical x. */
    const int16_t ex = s_tgt_y[i];
    const int16_t ey = (int16_t)(TPL_WS7_SCREEN_HEIGHT - 1 - s_tgt_x[i]);
    const double dx = (double)s_raw_x[i] - ex;
    const double dy = (double)s_raw_y[i] - ey;
    sx += dx;
    sy += dy;
    sxx += dx * dx;
    syy += dy * dy;
    if (dx < minx) minx = (int16_t)dx;
    if (dx > maxx) maxx = (int16_t)dx;
    if (dy < miny) miny = (int16_t)dy;
    if (dy > maxy) maxy = (int16_t)dy;
  }

  const double n = (double)s_samples;
  const double mx = sx / n;
  const double my = sy / n;
  const double sdx = sqrt(fabs(sxx / n - mx * mx));
  const double sdy = sqrt(fabs(syy / n - my * my));
  const int offx = (int)lround(-mx);
  const int offy = (int)lround(-my);

  char text[256];
  snprintf(text, sizeof(text),
           "n=%d\n"
           "X err mean %+.1f sd %.1f  [%+d..%+d]\n"
           "Y err mean %+.1f sd %.1f  [%+d..%+d]\n"
           "suggest X_OFF %+d  Y_OFF %+d\n"
           "tap to close",
           s_samples, mx, sdx, minx, maxx, my, sdy, miny, maxy, offx, offy);
  lv_label_set_text(s_stats, text);

  snprintf(s_stats_json, sizeof(s_stats_json),
           "{\"n\":%d,\"x_mean\":%.2f,\"x_sd\":%.2f,\"x_min\":%d,"
           "\"x_max\":%d,\"y_mean\":%.2f,\"y_sd\":%.2f,\"y_min\":%d,"
           "\"y_max\":%d,\"x_offset\":%d,\"y_offset\":%d}",
           s_samples, mx, sdx, minx, maxx, my, sdy, miny, maxy, offx, offy);
  TPL_LOGI("touch",
           "calibration done: n=%d X %+.1f+/-%.1f -> X_OFFSET=%+d, "
           "Y %+.1f+/-%.1f -> Y_OFFSET=%+d",
           s_samples, mx, sdx, offx, my, sdy, offy);
  TPL_LOGI("touch", "calibration json: %s", s_stats_json);
}

void haus_touch_test_request(void) { s_start_requested = true; }

bool haus_touch_test_active(void) { return s_active; }

const char* haus_touch_test_last_stats(void) { return s_stats_json; }

void haus_touch_test_poll(void) {
  if (s_start_requested) {
    s_start_requested = false;
    if (!s_active) tt_start();
  }
  if (!s_active) return;

  int16_t rx = 0, ry = 0;
  const bool down = tpl_ws7_touch_last_raw(&rx, &ry);
  const uint32_t now = lv_tick_get();

  if (!down) {
    if (s_was_down) s_release_ms = now;
    s_was_down = false;
    return;
  }
  if (s_was_down) return; /* hold: record one point per press */
  s_was_down = true;

  if (s_done) {
    tt_close();
    return;
  }
  if ((uint32_t)(now - s_release_ms) < 180u) return; /* settle/debounce */

  s_raw_x[s_samples] = rx;
  s_raw_y[s_samples] = ry;
  s_tgt_x[s_samples] = kTargetX[s_index % TT_POINTS];
  s_tgt_y[s_samples] = kTargetY[s_index % TT_POINTS];

  TPL_LOGI("touch", "cal %d/%d %s target (%d,%d) raw (%d,%d)", s_index + 1,
           TT_TOTAL, kTargetName[s_index % TT_POINTS], s_tgt_x[s_samples],
           s_tgt_y[s_samples], rx, ry);

  s_samples++;
  s_index++;

  if (s_index >= TT_TOTAL) {
    s_done = true;
    tt_compute();
    lv_obj_add_flag(s_cross_h, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_cross_v, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_cross_dot, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_hint, "Done. Tap once more to close.");
  } else {
    tt_update_target();
  }
}
