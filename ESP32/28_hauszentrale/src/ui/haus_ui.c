/**
 * @file haus_ui.c
 * @brief Productive LVGL screens for the 28_hauszentrale panel (800x480).
 *
 * Shared by the ESP32 firmware and the macOS host harness. Builds every screen
 * from the pure-data interface in haus_gui.h; no device or host specifics.
 *
 * Layout follows docs/ui/mockup.html (landscape):
 *   0..712   content, right-hand navigation rail at 712..800
 *   0..32    status strip on Home
 *   Home: clock left, heater cabinet right, living/power bands left
 *   Detail screens: header, big value, range/tabs, chart left, side panel right
 */

#define _POSIX_C_SOURCE 200809L

#include "haus_ui.h"

#include "haus_chart.h"
#include "haus_config.h"
#include "haus_gui.h"
#include "haus_history.h"
#include "haus_mem.h"

#ifndef HAUS_HOST
#include <Arduino.h>
#endif

#include "can_monitor.h"

#if __has_include("version.h")
#include "version.h"
#endif
#ifndef VERSION_STRING
#define VERSION_STRING "dev"
#endif
#ifndef GIT_HASH
#define GIT_HASH VERSION_STRING
#endif

LV_FONT_DECLARE(haus_mono_21);
LV_FONT_DECLARE(haus_body_21);
LV_FONT_DECLARE(haus_value_32);
LV_FONT_DECLARE(haus_large_52);
LV_FONT_DECLARE(haus_value_110);

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if LVGL_VERSION_MAJOR >= 10
#define UI_LABEL_LONG_WRAP LV_LABEL_LONG_MODE_WRAP
#define ui_label_set_recolor(l, en) lv_label_set_recolor((l), (en))
#else
#define UI_LABEL_LONG_WRAP LV_LABEL_LONG_WRAP
static inline void ui_label_set_recolor(lv_obj_t* l, bool en) {
  (void)l;
  (void)en;
}
#endif

/* ─── Geometry ──────────────────────────────────────────────────────────────
 */

#define SCREEN_W 800
#define SCREEN_H 480

#define NAV_W 88
#define NAV_X (SCREEN_W - NAV_W)
#define NAV_Y 0
#define NAV_H SCREEN_H
#define NAV_TABS 6
#define CONTENT_W NAV_X
#define CONTENT_H SCREEN_H

/* ─── Palette (from docs/ui/mockup.html) ────────────────────────────────────
 */

#define C_BG 0x0e0c0b
#define C_INK 0xf3ece4
#define C_DIM 0x9a9086
#define C_FAINT 0x6a625c
#define C_LINE 0x3a342e
#define C_COLD 0x8aa4b8
#define C_AMBER 0xd4924a
#define C_HOT 0xd4553a
#define C_ACCENT 0xe39b3a
#define C_CYAN 0x6fbfb8
#define C_VIOLET 0x8d7ea3
#define C_OK 0x5caf6e
#define C_OFF 0x444444
#define C_PANEL 0x1a1412

/* ─── Bottom navigation ─────────────────────────────────────────────────────
 */

/* Six 80 px tabs. The tab index and the screen id are deliberately separate:
 * Water In/Water Out collapse into one WATER tab. */
enum ui_screen_id {
  UI_SCREEN_HOME = 0,
  UI_SCREEN_CHAMBER,
  UI_SCREEN_WATER_IN,
  UI_SCREEN_WATER_OUT,
  UI_SCREEN_POWER,
  UI_SCREEN_LIVING,
  UI_SCREEN_AC,
  UI_SCREEN_SETTINGS,
  UI_SCREEN_STORAGE,
  UI_SCREEN_CAN,
  UI_SCREEN_LOG,
  UI_SCREEN_ROLES,
};

static const int nav_screens[NAV_TABS] = {
    UI_SCREEN_HOME,  UI_SCREEN_CHAMBER, UI_SCREEN_WATER_IN,
    UI_SCREEN_POWER, UI_SCREEN_LIVING,  UI_SCREEN_SETTINGS,
};

static const char* nav_icons[NAV_TABS] = {
    LV_SYMBOL_HOME,     /* home */
    NULL,               /* chamber (drawn as a thermometer) */
    NULL,               /* water (drawn as waves) */
    LV_SYMBOL_CHARGE,   /* power: electricity bolt, not a power button */
    NULL,               /* living room (drawn as a house) */
    LV_SYMBOL_SETTINGS, /* gear */
};

static int nav_index_for(int screen) {
  if (screen == UI_SCREEN_WATER_OUT) screen = UI_SCREEN_WATER_IN;
  if (screen == UI_SCREEN_STORAGE) screen = UI_SCREEN_SETTINGS;
  if (screen == UI_SCREEN_ROLES) screen = UI_SCREEN_SETTINGS;
  if (screen == UI_SCREEN_AC) screen = UI_SCREEN_SETTINGS;
  for (int i = 0; i < NAV_TABS; i++)
    if (nav_screens[i] == screen) return i;
  return -1;
}

static int g_current_screen = UI_SCREEN_HOME;
static int g_pending_screen = -1;
static int s_clock_fmt = 0; /* 0=24h/C, 1=12h/C, 2=12h/F, 3=24h/F */

#define UI_STACK_MAX 8
static int s_stack[UI_STACK_MAX] = {UI_SCREEN_HOME};
static int s_depth = 1;

static void ui_show_screen(int screen);
static void detail_update(void);
static void log_screen_update(void);
static void can_screen_update(void);
static void settings_can_refresh(void);
static void can_overlay_close(void);
static void build_status_hotspot(void);
static void pwm_control_update(void);

/* Home-screen widgets (NULL when the home screen is not built). */
static lv_obj_t* s_status_date;
static lv_obj_t* s_clock;
static lv_obj_t* s_can_dot;
static lv_obj_t* s_fault_banner;
static lv_obj_t* s_fault_banner_lbl;
static int s_fault_banner_shown = -1;
static lv_obj_t* s_sd_dot;
static lv_obj_t* s_wifi_icon;
static lv_obj_t* s_sd_low_lbl;
static lv_obj_t* s_cabinet;
static lv_obj_t* s_fire;
static lv_obj_t* s_flame[3];
static lv_obj_t* s_flame_glow;
static lv_obj_t* s_flame_core;
static lv_obj_t* s_chamber_temp;
static lv_obj_t* s_chamber_unit;
static lv_obj_t* s_cabinet_target;
static lv_obj_t* s_water_in;
static lv_obj_t* s_water_in_lbl;
static lv_obj_t* s_pump;
static lv_obj_t* s_pump_lbl;
static lv_obj_t* s_water_out;
static lv_obj_t* s_water_out_lbl;
static lv_obj_t* s_living_temp;
static lv_obj_t* s_living_unit;
static lv_obj_t* s_living_temp_tol;
static lv_obj_t* s_living_rh;
static lv_obj_t* s_living_rh_tol;
static lv_obj_t* s_power_num;
static lv_obj_t* s_power_bolt;
static lv_obj_t* s_power_src;

static lv_obj_t* s_pipe_l;
static lv_obj_t* s_pipe_r;
static lv_obj_t* s_pipe_lu;
static lv_obj_t* s_pipe_ru;
static lv_obj_t* s_zone[5];       /* 5 invisible navigation zones */
static lv_obj_t* s_duty_src;      /* duty provenance marker (status strip) */
static void align_power_labels(void);

/* Last values pushed into the widget tree. Every setter below is guarded by
 * these caches: lv_label_set_text() and lv_obj_set_style_*() invalidate the
 * object unconditionally, even for an identical value. On the RGB panel a
 * redundant invalidation is a visible redraw, so repeatedly refreshed screens
 * must only touch what actually changed. */
static uint32_t s_last_can_color = 0xFFFFFFFFu;
static uint32_t s_last_duty_color = 0xFFFFFFFFu;
static uint32_t s_last_sd_color = 0xFFFFFFFFu;
static uint32_t s_last_wifi_color = 0xFFFFFFFFu;
static uint32_t s_last_fire_grad = 0xFFFFFFFFu;
static uint8_t s_last_flame_opa[3] = {0xFF, 0xFF, 0xFF};
static uint16_t s_last_pipe_l_color = 0xFFFFu;
static uint16_t s_last_pipe_r_color = 0xFFFFu;

/* ─── Small helpers ─────────────────────────────────────────────────────────
 */

/* Millisecond clock for the scope. The device and the host tick from the same
 * monotonic source used by the monitor rows (millis() / lv_tick_get()). */
static uint32_t can_ui_now_ms(void) {
#ifdef HAUS_HOST
  return (uint32_t)lv_tick_get();
#else
  return (uint32_t)millis();
#endif
}

static bool txt(lv_obj_t* label, const char* text) {
  if (!label || !text) return false;
  const char* cur = lv_label_get_text(label);
  if (cur && strcmp(cur, text) == 0) return false;
  lv_label_set_text(label, text);
  return true;
}

/* LVGL 10 replaced the generic flag API with per-flag setters. The firmware
 * still builds against LVGL 9.2, so keep both paths working. */
#if LVGL_VERSION_MAJOR >= 10
static void obj_set_clickable(lv_obj_t* o, bool en) {
  lv_obj_set_clickable(o, en);
}
static void obj_set_scrollable(lv_obj_t* o, bool en) {
  lv_obj_set_scrollable(o, en);
}
#else
static void obj_set_clickable(lv_obj_t* o, bool en) {
  if (en) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  }
}
static void obj_set_scrollable(lv_obj_t* o, bool en) {
  if (en) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  } else {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  }
}
#endif

static void obj_set_hidden(lv_obj_t* o, bool en) {
#if LVGL_VERSION_MAJOR >= 10
  lv_obj_set_hidden(o, en);
#else
  if (en)
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
#endif
}

static void screen_lock_scroll(void) {
  lv_obj_t* scr = lv_screen_active();
  obj_set_scrollable(scr, false);
  lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  lv_obj_scroll_to(scr, 0, 0, LV_ANIM_OFF);
}

static lv_obj_t* label_make(lv_obj_t* parent, const char* text, int x, int y,
                            const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

static void temp_text(int16_t c100, char* buf, size_t n, bool with_unit) {
  if (s_clock_fmt >= 2) {
    if (with_unit)
      snprintf(buf, n, "%.1f °F", c100 * 9 / 5 / 100.0 + 32.0);
    else
      snprintf(buf, n, "%.1f", c100 * 9 / 5 / 100.0 + 32.0);
  } else {
    if (with_unit)
      snprintf(buf, n, "%.1f °C", c100 / 100.0);
    else
      snprintf(buf, n, "%.1f", c100 / 100.0);
  }
}

/* A 1-pixel separator line. */
static void hline(lv_obj_t* parent, int y, uint32_t color) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_size(o, SCREEN_W, 1);
  lv_obj_set_pos(o, 0, y);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  obj_set_clickable(o, false);
  obj_set_scrollable(o, false);
}

static lv_obj_t* hline_w(lv_obj_t* parent, int x, int y, int w,
                         uint32_t color) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_size(o, w, 1);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  obj_set_clickable(o, false);
  obj_set_scrollable(o, false);
  return o;
}

/* Three stacked waves standing in for the water series (in and out). LVGL's
 * built-in Montserrat subset has no wave glyph, so draw it. The points array is
 * static because lv_line stores the pointer instead of copying it. */
static void nav_waves(lv_obj_t* nav, int cx, int y, uint32_t color) {
  static const lv_point_precise_t wave_pts[] = {
      {0, 2},  {4, 0},  {8, 2},  {12, 0}, {16, 2},
      {20, 0}, {24, 2}, {28, 0}, {32, 2},
  };
  for (int r = 0; r < 3; r++) {
    lv_obj_t* w = lv_line_create(nav);
    lv_line_set_points(w, wave_pts, 9);
    lv_obj_set_pos(w, cx - 16, y + r * 7);
    lv_obj_set_style_line_width(w, 2, 0);
    lv_obj_set_style_line_color(w, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(w, true, 0);
    obj_set_clickable(w, false);
  }
}

static void nav_thermo(lv_obj_t* nav, int cx, int y, uint32_t color) {
  lv_obj_t* stem = lv_obj_create(nav);
  lv_obj_set_size(stem, 6, 18);
  lv_obj_set_pos(stem, cx - 3, y);
  lv_obj_set_style_bg_color(stem, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(stem, 0, 0);
  lv_obj_set_style_radius(stem, 3, 0);
  obj_set_clickable(stem, false);
  lv_obj_t* bulb = lv_obj_create(nav);
  lv_obj_set_size(bulb, 13, 13);
  lv_obj_set_pos(bulb, cx - 6, y + 15);
  lv_obj_set_style_bg_color(bulb, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(bulb, 0, 0);
  lv_obj_set_style_radius(bulb, 7, 0);
  obj_set_clickable(bulb, false);
}

static void nav_house(lv_obj_t* nav, int cx, int y, uint32_t color) {
  static const lv_point_precise_t roof[] = {{0, 10}, {12, 0}, {24, 10}};
  static const lv_point_precise_t left[] = {{0, 10}, {0, 26}};
  static const lv_point_precise_t right[] = {{24, 10}, {24, 26}};
  lv_obj_t* r = lv_line_create(nav);
  lv_line_set_points(r, roof, 3);
  lv_obj_set_pos(r, cx - 12, y);
  lv_obj_set_style_line_width(r, 2, 0);
  lv_obj_set_style_line_color(r, lv_color_hex(color), 0);
  lv_obj_set_style_line_rounded(r, true, 0);
  obj_set_clickable(r, false);
  lv_obj_t* l = lv_line_create(nav);
  lv_line_set_points(l, left, 2);
  lv_obj_set_pos(l, cx - 12, y);
  lv_obj_set_style_line_width(l, 2, 0);
  lv_obj_set_style_line_color(l, lv_color_hex(color), 0);
  obj_set_clickable(l, false);
  lv_obj_t* rr = lv_line_create(nav);
  lv_line_set_points(rr, right, 2);
  lv_obj_set_pos(rr, cx - 12, y);
  lv_obj_set_style_line_width(rr, 2, 0);
  lv_obj_set_style_line_color(rr, lv_color_hex(color), 0);
  obj_set_clickable(rr, false);
}

static void ui_nav_job(void* arg) {
  g_pending_screen = -1;
  ui_show_screen((int)(intptr_t)arg);
}

/* Defer the screen swap until the current event has returned; deleting the
 * event's target from inside its own callback is not safe in LVGL. */
static void ui_nav_go(int screen) {
  if (screen == g_current_screen || screen == g_pending_screen) return;
  g_pending_screen = screen;
  lv_async_call(ui_nav_job, (void*)(intptr_t)screen);
}

static void ui_jump(int screen);
static void ui_push(int screen);
static void ui_pop(void);

static void nav_click_cb(lv_event_t* e) {
  int screen = (int)(intptr_t)lv_event_get_user_data(e);
  ui_jump(screen);
}

static void ui_jump(int screen) {
  if (screen < UI_SCREEN_HOME || screen > UI_SCREEN_ROLES) return;
  s_depth = 1;
  s_stack[0] = screen;
  ui_nav_go(screen);
}

static void ui_push(int screen) {
  if (screen < UI_SCREEN_HOME || screen > UI_SCREEN_ROLES) return;
  if (s_depth < UI_STACK_MAX) s_stack[s_depth++] = screen;
  ui_nav_go(screen);
}

static void ui_pop(void) {
  if (s_depth > 1) {
    s_depth--;
    ui_nav_go(s_stack[s_depth - 1]);
  } else {
    ui_jump(UI_SCREEN_HOME);
  }
}

static void nav_build(int active_screen) {
  const int active = nav_index_for(active_screen);
  lv_obj_t* nav = lv_obj_create(lv_screen_active());
  lv_obj_set_size(nav, NAV_W, NAV_H);
  lv_obj_set_pos(nav, NAV_X, NAV_Y);
  lv_obj_set_style_bg_color(nav, lv_color_hex(0x0c0b0a), 0);
  lv_obj_set_style_border_width(nav, 0, 0);
  lv_obj_set_style_radius(nav, 0, 0);
  lv_obj_set_style_pad_all(nav, 0, 0);
  obj_set_scrollable(nav, false);
  /* hairline against the content area */
  lv_obj_t* edge = lv_obj_create(nav);
  lv_obj_set_size(edge, 1, NAV_H);
  lv_obj_set_pos(edge, 0, 0);
  lv_obj_set_style_bg_color(edge, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_border_width(edge, 0, 0);
  lv_obj_set_style_radius(edge, 0, 0);
  obj_set_clickable(edge, false);

  const int tab_h = NAV_H / NAV_TABS;
  for (int i = 0; i < NAV_TABS; i++) {
    const int screen = nav_screens[i];
    uint32_t color = i == active ? C_INK : C_DIM;

    lv_obj_t* tab = lv_obj_create(nav);
    lv_obj_set_size(tab, NAV_W, tab_h);
    lv_obj_set_pos(tab, 0, i * tab_h);
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tab, 0, 0);
    lv_obj_set_style_radius(tab, 0, 0);
    lv_obj_set_style_pad_all(tab, 0, 0);
    obj_set_scrollable(tab, false);
    obj_set_clickable(tab, true);
    lv_obj_add_event_cb(tab, nav_click_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)screen);

    const int icon_y = tab_h / 2 - 14;
    if (nav_icons[i] == NULL) {
      if (i == 1)
        nav_thermo(tab, NAV_W / 2, icon_y, color);
      else if (i == 2)
        nav_waves(tab, NAV_W / 2, tab_h / 2 - 10, color);
      else
        nav_house(tab, NAV_W / 2, icon_y, color);
    } else {
      lv_obj_t* icon = lv_label_create(tab);
      lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
      lv_obj_set_style_text_color(icon, lv_color_hex(color), 0);
      lv_label_set_text(icon, nav_icons[i]);
      lv_obj_set_width(icon, NAV_W);
      lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(icon, 0, icon_y);
      obj_set_clickable(icon, false);
    }

    if (i == active) {
      lv_obj_t* rule = lv_obj_create(tab);
      lv_obj_set_size(rule, 3, tab_h - 28);
      lv_obj_set_pos(rule, 0, 14);
      lv_obj_set_style_bg_color(rule, lv_color_hex(C_ACCENT), 0);
      lv_obj_set_style_border_width(rule, 0, 0);
      lv_obj_set_style_radius(rule, 1, 0);
      obj_set_clickable(rule, false);
    }
  }
}
/* ─── Date helpers ──────────────────────────────────────────────────────────
 */

static const char* const WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed",
                                       "Thu", "Fri", "Sat"};
static const char* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static void epoch_to_date(uint32_t epoch, char* out, size_t n) {
  time_t t = (time_t)epoch;
  struct tm tm;
  if (!localtime_r(&t, &tm)) {
    snprintf(out, n, "--- -- ---");
    return;
  }
  snprintf(out, n, "%s %d %s", WEEKDAYS[tm.tm_wday], tm.tm_mday,
           MONTHS[tm.tm_mon]);
}

/* ─── Home screen ───────────────────────────────────────────────────────────
 */

#define STATUS_LABEL_Y 6
#define STATUS_DOT_Y 10
#define STATUS_DOT_SIZE 12
#define STATUS_DOT_LABEL_GAP 8
#define STATUS_MARK_GAP 8

/* Draw one status mark as [dot][label], right-aligned to *right. The mark is
 * inserted to the left of whatever is already there, and *right is moved to the
 * mark's left edge minus the inter-mark gap. Returns the dot so callers can
 * recolour it when the state changes. */
/* A symbol-only mark (no text label), used for the optional WiFi indicator. */
static lv_obj_t* status_symbol_mark(int* right, const char* symbol) {
  lv_point_t size;
  lv_text_get_size(&size, symbol, &lv_font_montserrat_16, 0, 0, LV_COORD_MAX,
                   LV_TEXT_FLAG_NONE);

  int x = *right - size.x;
  lv_obj_t* l = label_make(lv_screen_active(), symbol, x, STATUS_LABEL_Y,
                           &lv_font_montserrat_16, C_OFF);
  *right = x - STATUS_MARK_GAP;
  return l;
}

static lv_obj_t* status_mark(int* right, const char* text) {
  lv_point_t size;
  lv_text_get_size(&size, text, &lv_font_montserrat_16, 0, 0, LV_COORD_MAX,
                   LV_TEXT_FLAG_NONE);

  int label_x = *right - size.x;
  label_make(lv_screen_active(), text, label_x, STATUS_LABEL_Y,
             &lv_font_montserrat_16, C_DIM);

  int dot_x = label_x - STATUS_DOT_LABEL_GAP - STATUS_DOT_SIZE;
  lv_obj_t* dot = lv_obj_create(lv_screen_active());
  lv_obj_set_size(dot, STATUS_DOT_SIZE, STATUS_DOT_SIZE);
  lv_obj_set_pos(dot, dot_x, STATUS_DOT_Y);
  lv_obj_set_style_bg_color(dot, lv_color_hex(C_OK), 0);
  lv_obj_set_style_radius(dot, STATUS_DOT_SIZE / 2, 0);
  lv_obj_set_style_border_width(dot, 0, 0);

  *right = dot_x - STATUS_MARK_GAP;
  return dot;
}

/* Map an interface status to a palette colour. */
static uint32_t status_color(gui_status_t status) {
  switch (status) {
    case GUI_STATUS_OK:
      return C_OK;
    case GUI_STATUS_ERROR:
      return C_HOT;
    default:
      return C_OFF;
  }
}

/* Apply a status to a strip dot, skipping the redraw when it is unchanged. */
static void status_dot_apply(lv_obj_t* dot, gui_status_t status,
                             uint32_t* last_color) {
  if (!dot) return;
  uint32_t color = status_color(status);
  if (color == *last_color) return;
  *last_color = color;
  lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
}

/* Same guard for a symbol label. */
static void status_text_apply(lv_obj_t* label, gui_status_t status,
                              uint32_t* last_color) {
  if (!label) return;
  uint32_t color = status_color(status);
  if (color == *last_color) return;
  *last_color = color;
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

/* Duty provenance keeps the hollow marker for the non-live (EEPROM) state:
 * solid = live, hollow = restored, red = fault. */
static void duty_dot_apply(lv_obj_t* dot, gui_status_t status,
                           uint32_t* last_color) {
  if (!dot) return;
  uint32_t color = status_color(status);
  if (color == *last_color) return;
  *last_color = color;
  if (status == GUI_STATUS_INACTIVE) {
    lv_obj_set_style_bg_color(dot, lv_color_hex(C_OFF), 0);
    lv_obj_set_style_border_width(dot, 2, 0);
    lv_obj_set_style_border_color(dot, lv_color_hex(C_DIM), 0);
  } else {
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
  }
}

/* Sticky CAN bus fault banner (S17.05). Lives on the top layer so it is
 * visible on every screen; it only clears when a good frame arrives. */
static void build_fault_banner(void) {
  /* Child of the home screen, not lv_layer_top(): a top layer forces an
   * extra full-width composite per refresh, which the single-buffered RGB
   * panel shows as high-frequency white stripe tearing. */
  lv_obj_t* b = lv_obj_create(lv_screen_active());
  lv_obj_set_size(b, 440, 48);
  lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_style_bg_color(b, lv_color_hex(C_HOT), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  obj_set_clickable(b, false);
  s_fault_banner_lbl =
      label_make(b, "CAN BUS FAULT", 0, 0, &lv_font_montserrat_16, C_INK);
  lv_obj_center(s_fault_banner_lbl);
  s_fault_banner = b;
  obj_set_hidden(b, true);
}

static void fault_banner_update(const HausState* st) {
  if (!s_fault_banner) return;
  /* Only touch the widget on a state change: an unconditional hide/show
   * invalidates the top strip every second, which the single-buffered RGB
   * panel shows as white tearing near the clock. */
  const int show = st->can.can_fault ? 1 : 0;
  if (show == s_fault_banner_shown) return;
  s_fault_banner_shown = show;
  obj_set_hidden(s_fault_banner, !show);
}

static void build_status(void) {
  s_status_date = label_make(lv_screen_active(), "--- -- ---", 12,
                             STATUS_LABEL_Y, &lv_font_montserrat_16, C_INK);

  /* Right-to-left: WiFi symbol, SD, CAN, then the duty provenance marker.
   * For the duty marker solid means live, hollow means restored EEPROM,
   * red means fault. WiFi has no text label, only the glyph. */
  int right = CONTENT_W - 12;
  s_wifi_icon = status_symbol_mark(&right, LV_SYMBOL_WIFI);
  s_sd_dot = status_mark(&right, "SD");
  s_can_dot = status_mark(&right, "CAN");
  s_duty_src = status_mark(&right, "DUTY");
  /* Free-space warning only when low (D45): hidden otherwise. */
  s_sd_low_lbl = label_make(lv_screen_active(), "", 196, STATUS_LABEL_Y,
                            &lv_font_montserrat_16, C_AMBER);
  obj_set_hidden(s_sd_low_lbl, true);
}

static void clock_long_cb(lv_event_t* e) {
  (void)e;
  s_clock_fmt = (s_clock_fmt + 1) % 4;
  haus_ui_update();
}

static void build_clock(void) {
  /* FONT_CLOCK: 110 px, 21 mm cap height (§8.1.1). */
  s_clock =
      label_make(lv_screen_active(), "--:--", 0, 28, &haus_value_110, C_INK);
  lv_obj_set_width(s_clock, 340);
  lv_obj_set_style_text_align(s_clock, LV_TEXT_ALIGN_CENTER, 0);
  obj_set_clickable(s_clock, true);
  lv_obj_add_event_cb(s_clock, clock_long_cb, LV_EVENT_LONG_PRESSED, NULL);
}

/* True when (x,y) lies inside a rounded rectangle inset by `inset` pixels
 * with corner radius `rad`. */
static bool ui_round_in(int x, int y, int w, int h, int rad, int inset) {
  if (x < inset || y < inset || x >= w - inset || y >= h - inset) return false;
  const bool left = x < inset + rad;
  const bool right = x >= w - inset - rad;
  const bool top = y < inset + rad;
  const bool bottom = y >= h - inset - rad;
  if (!((left || right) && (top || bottom))) return true;
  const int cx = left ? inset + rad : w - 1 - inset - rad;
  const int cy = top ? inset + rad : h - 1 - inset - rad;
  const int dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= rad * rad;
}

/* A flame tongue: a pill, wider at the base by stacking two pills. */
static void build_heater(void) {
  const int HX = 270, HY = 98;

  /* Cabinet glow. */
  lv_obj_t* glow = lv_obj_create(lv_screen_active());
  lv_obj_set_size(glow, 223, 211);
  lv_obj_set_pos(glow, HX + 104, HY + 14);
  lv_obj_set_style_bg_color(glow, lv_color_hex(0xc4783a), 0);
  lv_obj_set_style_bg_opa(glow, 24, 0);
  lv_obj_set_style_radius(glow, 20, 0);
  lv_obj_set_style_border_width(glow, 0, 0);
  obj_set_clickable(glow, false);

  /* Cabinet canvas: 209x193, rounded-rect border. */
  {
    const int cw = 209, ch = 193;
    const uint16_t bw = 2, r = 14;
    s_cabinet = lv_canvas_create(lv_screen_active());
    lv_obj_set_size(s_cabinet, cw, ch);
    lv_obj_set_pos(s_cabinet, HX + 112, HY + 22);
    lv_obj_set_style_border_width(s_cabinet, 0, 0);

    uint8_t* buf = (uint8_t*)lv_malloc(cw * ch * 2);
    lv_canvas_set_buffer(s_cabinet, buf, cw, ch, LV_COLOR_FORMAT_RGB565);

    uint16_t pcol = lv_color_to_u16(lv_color_hex(C_PANEL));
    uint16_t bcol = lv_color_to_u16(lv_color_hex(0x6a5e54));
    for (int i = 0; i < cw * ch; i++) ((uint16_t*)buf)[i] = pcol;
    for (int y = 0; y < ch; y++) {
      for (int x = 0; x < cw; x++) {
        if (ui_round_in(x, y, cw, ch, r, 0) &&
            !ui_round_in(x, y, cw, ch, r - bw, bw)) {
          ((uint16_t*)buf)[y * cw + x] = bcol;
        }
      }
    }
  }

  /* Fire chamber. */
  s_fire = lv_obj_create(s_cabinet);
  lv_obj_set_size(s_fire, 166, 106);
  lv_obj_align(s_fire, LV_ALIGN_TOP_MID, 0, 18);
  lv_obj_set_style_bg_color(s_fire, lv_color_hex(0x1a1412), 0);
  lv_obj_set_style_bg_grad_color(s_fire, lv_color_hex(0x3a2418), 0);
  lv_obj_set_style_bg_grad_dir(s_fire, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_radius(s_fire, 9, 0);
  lv_obj_set_style_border_width(s_fire, 0, 0);
  lv_obj_set_style_pad_all(s_fire, 0, 0);
  obj_set_scrollable(s_fire, false);

  s_flame_glow = lv_obj_create(s_fire);
  lv_obj_set_size(s_flame_glow, 76, 83);
  lv_obj_align(s_flame_glow, LV_ALIGN_BOTTOM_MID, 0, 4);
  lv_obj_set_style_bg_color(s_flame_glow, lv_color_hex(0xc06a2a), 0);
  lv_obj_set_style_radius(s_flame_glow, 38, 0);
  lv_obj_set_style_border_width(s_flame_glow, 0, 0);
  lv_obj_set_style_opa(s_flame_glow, 90, 0);
  obj_set_clickable(s_flame_glow, false);

  const int fw[3] = {23, 36, 23};
  const int fh[3] = {49, 79, 49};
  const int fx[3] = {-27, 0, 27};
  const int fc[3] = {0xd9762a, 0xf0a64b, 0xd9762a};
  for (int i = 0; i < 3; i++) {
    s_flame[i] = lv_obj_create(s_fire);
    lv_obj_set_size(s_flame[i], fw[i], fh[i]);
    lv_obj_align(s_flame[i], LV_ALIGN_BOTTOM_MID, fx[i], 4);
    lv_obj_set_style_bg_color(s_flame[i], lv_color_hex(fc[i]), 0);
    lv_obj_set_style_radius(s_flame[i], fw[i] / 2, 0);
    lv_obj_set_style_border_width(s_flame[i], 0, 0);
    lv_obj_set_style_opa(s_flame[i], 235, 0);
    obj_set_clickable(s_flame[i], false);
  }
  s_flame_core = lv_obj_create(s_fire);
  lv_obj_set_size(s_flame_core, 14, 41);
  lv_obj_align(s_flame_core, LV_ALIGN_BOTTOM_MID, 0, 6);
  lv_obj_set_style_bg_color(s_flame_core, lv_color_hex(0xffe0a0), 0);
  lv_obj_set_style_radius(s_flame_core, 7, 0);
  lv_obj_set_style_border_width(s_flame_core, 0, 0);
  lv_obj_set_style_opa(s_flame_core, 240, 0);
  obj_set_clickable(s_flame_core, false);

  s_cabinet_target =
      label_make(s_cabinet, "--", 0, 0, &lv_font_montserrat_32, C_INK);
  lv_obj_align(s_cabinet_target, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_t* soll =
      label_make(s_cabinet, "Soll", 0, 0, &lv_font_montserrat_16, C_DIM);
  lv_obj_align(soll, LV_ALIGN_BOTTOM_MID, 0, -38);

  /* Pipes. */
  const int py = HY + 270;
  s_pipe_l = lv_obj_create(lv_screen_active());
  lv_obj_set_size(s_pipe_l, 133, 11);
  lv_obj_set_pos(s_pipe_l, HX + 25, py);
  lv_obj_set_style_radius(s_pipe_l, 5, 0);
  lv_obj_set_style_border_width(s_pipe_l, 0, 0);
  obj_set_clickable(s_pipe_l, false);
  s_pipe_lu = lv_obj_create(lv_screen_active());
  lv_obj_set_size(s_pipe_lu, 11, 62);
  lv_obj_set_pos(s_pipe_lu, HX + 148, HY + 214);
  lv_obj_set_style_radius(s_pipe_lu, 5, 0);
  lv_obj_set_style_border_width(s_pipe_lu, 0, 0);
  obj_set_clickable(s_pipe_lu, false);

  s_pipe_r = lv_obj_create(lv_screen_active());
  lv_obj_set_size(s_pipe_r, 133, 11);
  lv_obj_set_pos(s_pipe_r, HX + 274, py);
  lv_obj_set_style_radius(s_pipe_r, 5, 0);
  lv_obj_set_style_border_width(s_pipe_r, 0, 0);
  obj_set_clickable(s_pipe_r, false);
  s_pipe_ru = lv_obj_create(lv_screen_active());
  lv_obj_set_size(s_pipe_ru, 11, 62);
  lv_obj_set_pos(s_pipe_ru, HX + 274, HY + 214);
  lv_obj_set_style_radius(s_pipe_ru, 5, 0);
  lv_obj_set_style_border_width(s_pipe_ru, 0, 0);
  obj_set_clickable(s_pipe_ru, false);

  /* Chamber temperature above the cabinet. */
  s_chamber_temp = label_make(lv_screen_active(), "--", HX + 166, 50,
                              &lv_font_montserrat_32, C_COLD);
  s_chamber_unit = label_make(lv_screen_active(), "°C", 0, 0,
                              &lv_font_montserrat_16, C_COLD);
  lv_obj_align_to(s_chamber_unit, s_chamber_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 4,
                  -2);

  /* Pipe labels. */
  s_water_in = label_make(lv_screen_active(), "--", HX + 20, 404,
                          &lv_font_montserrat_24, C_AMBER);
  s_water_in_lbl = label_make(lv_screen_active(), "water in", HX + 20, 432,
                              &lv_font_montserrat_16, C_DIM);
  s_pump = label_make(lv_screen_active(), "off", HX + 196, 404,
                      &lv_font_montserrat_24, C_OK);
  s_pump_lbl = NULL;
  s_water_out = label_make(lv_screen_active(), "--", HX + 330, 404,
                           &lv_font_montserrat_24, C_HOT);
  s_water_out_lbl = label_make(lv_screen_active(), "water out", HX + 330, 432,
                               &lv_font_montserrat_16, C_DIM);
}
static void build_living(void) {
  hline_w(lv_screen_active(), 12, 180, 256, C_LINE);

  label_make(lv_screen_active(), LV_SYMBOL_TINT, 20, 194,
             &lv_font_montserrat_24, C_COLD);
  s_living_temp = label_make(lv_screen_active(), "--", 56, 190,
                             &lv_font_montserrat_32, C_COLD);
  s_living_unit = label_make(lv_screen_active(), "°C", 0, 0,
                             &lv_font_montserrat_16, C_COLD);
  lv_obj_align_to(s_living_unit, s_living_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 4,
                  -2);
  s_living_temp_tol = label_make(lv_screen_active(), "", 0, 0,
                                 &lv_font_montserrat_16, C_FAINT);
  obj_set_hidden(s_living_temp_tol, true);

  s_living_rh = label_make(lv_screen_active(), "-- %rH", 172, 190,
                           &lv_font_montserrat_24, C_CYAN);
  s_living_rh_tol = label_make(lv_screen_active(), "", 0, 0,
                               &lv_font_montserrat_16, C_FAINT);
  obj_set_hidden(s_living_rh_tol, true);
}

static void build_power(void) {
  hline_w(lv_screen_active(), 12, 260, 256, C_LINE);

  s_power_num =
      label_make(lv_screen_active(), "--", 56, 266, &lv_font_montserrat_48,
                 C_INK);
  s_power_bolt = label_make(lv_screen_active(), LV_SYMBOL_CHARGE, 20, 278,
                            &lv_font_montserrat_32, C_ACCENT);
  s_power_src =
      label_make(lv_screen_active(), "W", 0, 0, &lv_font_montserrat_16, C_DIM);
  align_power_labels();
}

static void align_power_labels(void) {
  if (!s_power_num) return;
  if (s_power_bolt) {
    lv_obj_align_to(s_power_bolt, s_power_num, LV_ALIGN_OUT_LEFT_MID, -10, 0);
  }
  if (s_power_src) {
    lv_obj_align_to(s_power_src, s_power_num, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
  }
}

static void zone_click_cb(lv_event_t* e) {
  int screen = (int)(intptr_t)lv_event_get_user_data(e);
  ui_jump(screen);
}

static void build_zones(void) {
  static const struct {
    int x, y, w, h;
    int screen;
  } zones[5] = {
      {380, 110, 300, 220, UI_SCREEN_CHAMBER},
      {270, 360, 200, 100, UI_SCREEN_WATER_IN},
      {490, 360, 210, 100, UI_SCREEN_WATER_OUT},
      {0, 180, 270, 72, UI_SCREEN_LIVING},
      {0, 260, 270, 72, UI_SCREEN_POWER},
  };

  for (int i = 0; i < 5; i++) {
    s_zone[i] = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(s_zone[i], zones[i].x, zones[i].y);
    lv_obj_set_size(s_zone[i], zones[i].w, zones[i].h);
    lv_obj_set_style_bg_color(s_zone[i], lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(s_zone[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_zone[i], 32, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_zone[i], 0, 0);
    lv_obj_set_style_radius(s_zone[i], 0, 0);
    lv_obj_set_style_pad_all(s_zone[i], 0, 0);
    obj_set_scrollable(s_zone[i], false);
    obj_set_clickable(s_zone[i], true);
    lv_obj_add_event_cb(s_zone[i], zone_click_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)zones[i].screen);
  }
}

static bool ensure_chart_buffers(void);
static void can_wave_tick(void);

lv_obj_t* haus_ui_init(void) {
  ensure_chart_buffers();
  g_current_screen = UI_SCREEN_HOME;
  screen_lock_scroll();
  /* Create a dark background object as the very first child of the screen.
   * Being the first child guarantees it is painted first (bottom-most),
   * so subsequent widgets (status, clock, heater, etc.) always sit on top
   * of the dark background and never reveal white underneath. */
  lv_obj_t* bg = lv_obj_create(lv_screen_active());
  lv_obj_set_size(bg, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(bg, 0, 0);
  lv_obj_set_style_bg_color(bg, lv_color_hex(C_BG), 0);
  lv_obj_set_style_border_width(bg, 0, 0);
  lv_obj_set_style_radius(bg, 0, 0);
  obj_set_clickable(bg, false);
  obj_set_scrollable(bg, false);

  lv_obj_t* root = lv_screen_active();

  build_status();
  build_fault_banner();
  build_status_hotspot();
  build_clock();
  build_heater();
  build_living();
  build_power();
  build_zones();
  nav_build(UI_SCREEN_HOME);

  /* Paint the honest initial state immediately: the widgets above are built
   * with placeholders, and this replaces them from gui_get_state() before the
   * first frame is flushed. Never leave fabricated sample values on screen. */
  haus_ui_update();

  return root;
}

/* One predicate for the flame and the Soll text: both are only meaningful
 * while the heater reports that it is on. The board probe is the room
 * temperature; there is no chamber sensor. */
static bool heater_on(const CanState* can) {
  return can->pwm_fresh && can->pwm_permille < 500 && can->chamber_target_c > 0;
}

void haus_ui_update(void) {
  if (gui_take_redraw()) lv_obj_invalidate(lv_screen_active());
  {
    static HausState banner_st;
    gui_get_state(&banner_st);
    fault_banner_update(&banner_st);
  }
  if (g_current_screen == UI_SCREEN_LOG) {
    log_screen_update();
    return;
  }
  if (g_current_screen == UI_SCREEN_CAN) {
    can_screen_update();
    return;
  }
  if (g_current_screen == UI_SCREEN_SETTINGS) {
    settings_can_refresh();
    return;
  }
  if (g_current_screen != UI_SCREEN_HOME) {
    detail_update();
#ifndef HAUS_HOST
    HausState st;
    gui_get_state(&st);
    if (lv_display_get_inactive_time(NULL) > 3u * 60u * 1000u &&
        !(g_current_screen == UI_SCREEN_CHAMBER && st.can.has_pending)) {
      ui_jump(UI_SCREEN_HOME);
    }
#endif
    return;
  }

  HausState state;
  gui_get_state(&state);
  char buf[48];

  /* Status: date + clock. */
  if (state.can.synced) {
    epoch_to_date(state.can.timestamp, buf, sizeof(buf));
    txt(s_status_date, buf);
    time_t t = (time_t)state.can.timestamp;
    struct tm tm;
    if (localtime_r(&t, &tm)) {
      const bool h12 = (s_clock_fmt == 1 || s_clock_fmt == 2);
      if (h12) {
        int h = tm.tm_hour % 12;
        if (h == 0) h = 12;
        snprintf(buf, sizeof(buf), "%02d:%02d %s", h, tm.tm_min,
                 tm.tm_hour < 12 ? "AM" : "PM");
      } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
      }
      txt(s_clock, buf);
    }
  } else {
    txt(s_status_date, "--- -- ---");
    txt(s_clock, "--:--");
  }
  /* Repaint the clock band every second. A flash write at the minute
   * boundary can drop the framebuffer update, and the clock otherwise keeps
   * the corrupted pixels until the text changes again. */
  if (s_clock) lv_obj_invalidate(s_clock);

  status_dot_apply(s_can_dot, state.can.can_status, &s_last_can_color);
  duty_dot_apply(s_duty_src, state.can.duty_status, &s_last_duty_color);
  status_dot_apply(s_sd_dot, state.can.sd_status, &s_last_sd_color);
  status_text_apply(s_wifi_icon, state.can.wifi_status, &s_last_wifi_color);

  /* Free-space warning: only when the floor is close (D45/S23.12). */
  if (s_sd_low_lbl) {
    if (state.can.sd_low_space) {
      const uint64_t total = state.can.sd_total_bytes;
      const uint64_t used = state.can.sd_used_bytes;
      const uint64_t free_b = total > used ? total - used : 0;
      char warn[24];
      snprintf(warn, sizeof(warn), "%lluM FREE",
               (unsigned long long)(free_b / (1024ULL * 1024ULL)));
      txt(s_sd_low_lbl, warn);
      obj_set_hidden(s_sd_low_lbl, false);
    } else {
      obj_set_hidden(s_sd_low_lbl, true);
    }
  }

  /* Chamber. */
  int16_t chamber = state.can.temp_by_fn[SENSOR_FN_CHAMBER];
  bool chamber_changed;
  if (chamber != HAUS_CAN_TEMP_EMPTY) {
    temp_text(chamber, buf, sizeof(buf), false);
    chamber_changed = txt(s_chamber_temp, buf);
    /* Render valid temperature in C_COLD (blue). */
    lv_obj_set_style_text_color(s_chamber_temp, lv_color_hex(C_COLD), 0);
  } else {
    chamber_changed = txt(s_chamber_temp, "--");
    /* Render sentinel in grey, never as an extreme colour. */
    lv_obj_set_style_text_color(s_chamber_temp, lv_color_hex(0x444444), 0);
  }
  if (s_chamber_unit) txt(s_chamber_unit, s_clock_fmt >= 2 ? "°F" : "°C");
  if (chamber_changed && s_chamber_unit && s_chamber_temp) {
    lv_obj_align_to(s_chamber_unit, s_chamber_temp, LV_ALIGN_OUT_RIGHT_BOTTOM,
                    4, -2);
  }

  /* Soll comes from the control loop; it is the target, not a measurement.
   * It is only shown while the heater is on, so it cannot disagree with the
   * flame (same predicate). */
  if (s_cabinet_target) {
    char tb[16];
    if (heater_on(&state.can))
      snprintf(tb, sizeof(tb), "%d°C", state.can.chamber_target_c / 100);
    else
      snprintf(tb, sizeof(tb), "--");
    txt(s_cabinet_target, tb);
  }

  /* Pipes: colour follows a blue → amber → red ramp based on chamber temp.
   *  < 30 °C (3000) → C_COLD (blue)
   *  30–60 °C (3000–6000) → C_AMBER (amber)
   *  > 60 °C (6000) → C_HOT (red)
   * Only redraw when the colour actually changes. */
  if (s_pipe_l && s_pipe_r && s_pipe_lu && s_pipe_ru) {
    uint32_t pcol;
    if (chamber == HAUS_CAN_TEMP_EMPTY) {
      /* No data → grey */
      pcol = 0x444444;
    } else {
      int32_t tc = chamber; /* × 100 */
      if (tc < 3000)
        pcol = C_COLD;
      else if (tc <= 6000)
        pcol = C_AMBER;
      else
        pcol = C_HOT;
    }
    if (pcol != s_last_pipe_l_color) {
      s_last_pipe_l_color = pcol;
      lv_obj_set_style_bg_color(s_pipe_l, lv_color_hex(pcol), 0);
      lv_obj_set_style_bg_color(s_pipe_lu, lv_color_hex(pcol), 0);
    }
    /* Water-out pipe follows the same ramp but uses the water-out temp.
     * If water-out is empty, fall back to chamber colour. */
    int16_t wout = state.can.temp_by_fn[SENSOR_FN_WATER_OUT];
    if (wout == HAUS_CAN_TEMP_EMPTY) wout = chamber;
    if (wout == HAUS_CAN_TEMP_EMPTY) {
      pcol = 0x444444;
    } else {
      int32_t tc = wout;
      if (tc < 3000)
        pcol = C_COLD;
      else if (tc <= 6000)
        pcol = C_AMBER;
      else
        pcol = C_HOT;
    }
    if (pcol != s_last_pipe_r_color) {
      s_last_pipe_r_color = pcol;
      lv_obj_set_style_bg_color(s_pipe_r, lv_color_hex(pcol), 0);
      lv_obj_set_style_bg_color(s_pipe_ru, lv_color_hex(pcol), 0);
    }
  }

  /* Water in (amber) / water out (hot). */
  int16_t win = state.can.temp_by_fn[SENSOR_FN_WATER_IN];
  int16_t wout = state.can.temp_by_fn[SENSOR_FN_WATER_OUT];
  if (win != HAUS_CAN_TEMP_EMPTY) {
    temp_text(win, buf, sizeof(buf), true);
    txt(s_water_in, buf);
    lv_obj_set_style_text_color(s_water_in, lv_color_hex(C_AMBER), 0);
  } else {
    txt(s_water_in, "--");
    lv_obj_set_style_text_color(s_water_in, lv_color_hex(0x444444), 0);
  }
  if (wout != HAUS_CAN_TEMP_EMPTY) {
    char tb[24];
    temp_text(wout, tb, sizeof(tb), true);
    snprintf(buf, sizeof(buf), "%s", tb);
    txt(s_water_out, buf);
    lv_obj_set_style_text_color(s_water_out, lv_color_hex(C_HOT), 0);
  } else {
    txt(s_water_out, "--");
    lv_obj_set_style_text_color(s_water_out, lv_color_hex(0x444444), 0);
  }
  bool flow = state.can.pwm_permille > 0 && state.can.pwm_permille < 1000;
  txt(s_pump, flow ? "on" : "off");
  if (state.can.ac_fresh) {
    const double vrms = (state.can.ac_rms_dc / 10.0) * 1.1 / 1024.0;
    snprintf(buf, sizeof(buf), "RMS %.3f V", vrms);
  } else {
    snprintf(buf, sizeof(buf), "RMS --");
  }
  if (s_pump_lbl) txt(s_pump_lbl, buf);

  /* Living room. */
  bool living_changed;
  if (state.climate.climate_flags & CLIMATE_VALID_SHT45) {
    temp_text(state.climate.living_c100, buf, sizeof(buf), false);
    living_changed = txt(s_living_temp, buf);
    lv_obj_set_style_text_color(s_living_temp, lv_color_hex(C_COLD), 0);
  } else {
    living_changed = txt(s_living_temp, "--");
    lv_obj_set_style_text_color(s_living_temp, lv_color_hex(0x444444), 0);
  }
  const bool living_f = s_clock_fmt >= 2;
  if (s_living_unit) txt(s_living_unit, living_f ? "°F" : "°C");
  if (s_living_temp_tol)
    txt(s_living_temp_tol, living_f ? "+/-0.2°F" : "+/-0.1°C");
  if (living_changed && s_living_unit && s_living_temp) {
    lv_obj_align_to(s_living_unit, s_living_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 4,
                    -2);
  }
  if (s_living_temp_tol && s_living_unit) {
    lv_obj_align_to(s_living_temp_tol, s_living_unit, LV_ALIGN_OUT_RIGHT_BOTTOM,
                    6, -2);
  }
  if (state.climate.climate_flags & CLIMATE_VALID_SHT45) {
    snprintf(buf, sizeof(buf), "%u %%rH", state.climate.hum_c100 / 100);
    txt(s_living_rh, buf);
    lv_obj_set_style_text_color(s_living_rh, lv_color_hex(C_CYAN), 0);
  } else {
    txt(s_living_rh, "-- %rH");
    lv_obj_set_style_text_color(s_living_rh, lv_color_hex(0x444444), 0);
  }
  if (s_living_rh_tol && s_living_rh) {
    lv_obj_align_to(s_living_rh_tol, s_living_rh, LV_ALIGN_OUT_RIGHT_BOTTOM, 6,
                    -2);
  }

  /* Power (always watts, with the bolt glyph). */
  bool power_changed;
  if (state.can.valid) {
    snprintf(buf, sizeof(buf), "%d", (int)state.can.watts);
    power_changed = txt(s_power_num, buf);
    lv_obj_set_style_text_color(s_power_num, lv_color_hex(C_INK), 0);
    /* The small label next to the number is the unit, never a source
     * badge. Montserrat has no em dash glyph (it would render as a
     * placeholder box), so keep it pure ASCII. */
    txt(s_power_src, "W");
    lv_obj_set_style_text_color(s_power_src, lv_color_hex(C_DIM), 0);
  } else {
    power_changed = txt(s_power_num, "--");
    lv_obj_set_style_text_color(s_power_num, lv_color_hex(0x444444), 0);
    txt(s_power_src, "W");
    lv_obj_set_style_text_color(s_power_src, lv_color_hex(0x444444), 0);
  }
  if (power_changed) align_power_labels();

  /* The flame is time-based, so paint it once here for callers that only
   * refresh the data (the host harness) and let the loop drive it after. */
  haus_ui_animate();


}

void haus_ui_animate(void) {
  if (g_current_screen == UI_SCREEN_CAN) can_wave_tick();

  HausState state;
  gui_get_state(&state);
  /* There is no chamber temperature sensor. The flame shows the *Soll* and
   * only while the heater is on and a 0x280 report is fresh. No frame, a
   * duty >= 500, or no target means no flame. */
  const int16_t target = state.can.chamber_target_c;
  const bool heating = heater_on(&state.can);

  if (!heating) {
    for (int i = 0; i < 3; i++) {
      if (!s_flame[i]) continue;
      if (s_last_flame_opa[i] != 0) {
        s_last_flame_opa[i] = 0;
        lv_obj_set_style_opa(s_flame[i], LV_OPA_0, 0);
      }
    }
    if (s_flame_glow) lv_obj_set_style_opa(s_flame_glow, LV_OPA_0, 0);
    if (s_flame_core) lv_obj_set_style_opa(s_flame_core, LV_OPA_0, 0);
    if (s_fire && s_last_fire_grad != 0) {
      s_last_fire_grad = 0;
      lv_obj_set_style_bg_grad_color(s_fire, lv_color_hex(0x1a1412), 0);
    }
    return;
  }

  double t = (target / 100.0 - 20.0) / 60.0;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  uint32_t warm = ((uint32_t)(0x30 + 0x1a * t) << 16) |
                  ((uint32_t)(0x20 + 0x14 * t) << 8) | 0x18;
  if (s_fire && warm != s_last_fire_grad) {
    s_last_fire_grad = warm;
    lv_obj_set_style_bg_grad_color(s_fire, lv_color_hex(warm), 0);
  }
  if (s_flame_glow) {
    lv_obj_set_style_opa(s_flame_glow, (lv_opa_t)(40 + 60 * t), 0);
  }
  if (s_flame_core) {
    lv_obj_set_style_opa(s_flame_core, (lv_opa_t)(190 + 50 * t), 0);
  }

  /* 6 s breathing cycle, driven from the LVGL tick so the host and the
   * device animate at the same rate. */
  double phase = (lv_tick_get() % 6000) * (2.0 * M_PI / 6000.0);
  for (int i = 0; i < 3; i++) {
    if (!s_flame[i]) continue;
    double breath = 0.5 + 0.5 * sin(phase + i * 2.094);
    uint8_t opa = (uint8_t)(t * (0.45 + 0.55 * breath) * 255);
    if (opa == s_last_flame_opa[i]) continue;
    s_last_flame_opa[i] = opa;
    lv_obj_set_style_opa(s_flame[i], opa, 0);
  }
}

/* ─── Chart / detail header + range selector (shared) ───────────────────────
 */

#define CHART_W 440
#define CHART_H 260
#define CHART_X0 56
#define CHART_X1 430
#define CHART_Y0 16
#define CHART_Y1 210

enum ui_range { UI_RANGE_DAY = 0, UI_RANGE_WEEK, UI_RANGE_MONTH };
enum chart_unit {
  CHART_UNIT_TEMP = 0,
  CHART_UNIT_RH,
  CHART_UNIT_POWER,
  CHART_UNIT_AC_RMS,
  CHART_UNIT_AC_ANGLE
};

typedef struct {
  int a, b;
  int use_target;
  int unit;
  int hourly_ok;
  int band;
  int lines_only; /* no area fill and no envelope shading */
  uint32_t color_a, color_b;
} chart_cfg_t;

static int g_range = UI_RANGE_DAY;
static int g_detail_screen = UI_SCREEN_CHAMBER;

/* CPU-only buffers: 25 KB here used to sit in internal .bss. PSRAM keeps
 * internal SRAM for WiFi/DMA (docs/memory_report.md, S28.19–S28.21). */
static ChartColumn* s_cols = NULL;
static int16_t* s_target = NULL;
static uint8_t* s_target_n = NULL;
static int32_t* s_target_sum = NULL;

#define CHART_SET_BYTES (HAUS_CHART_COLUMNS * sizeof(ChartColumn))
#define TARGET_BYTES (HAUS_CHART_COLUMNS * sizeof(int16_t))
#define TARGET_N_BYTES (HAUS_CHART_COLUMNS * sizeof(uint8_t))
#define TARGET_SUM_BYTES (HAUS_CHART_COLUMNS * sizeof(int32_t))

static bool ensure_chart_buffers(void) {
  if (!s_cols) s_cols = (ChartColumn*)haus_mem_alloc(CHART_SET_BYTES);
  if (!s_target) s_target = (int16_t*)haus_mem_alloc(TARGET_BYTES);
  if (!s_target_n) s_target_n = (uint8_t*)haus_mem_alloc(TARGET_N_BYTES);
  if (!s_target_sum) s_target_sum = (int32_t*)haus_mem_alloc(TARGET_SUM_BYTES);
  return s_cols && s_target && s_target_n && s_target_sum;
}

static lv_obj_t* s_chart_canvas;
static uint8_t* s_chart_pixels;
static lv_obj_t* s_chart_nodata;
static lv_obj_t* s_chart_stale;
static lv_obj_t* s_chart_target_lbl;
static lv_obj_t* s_legend_a;
static lv_obj_t* s_legend_b;
static lv_obj_t* s_legend_t;
static lv_obj_t* s_chart_readout;
static lv_obj_t* s_chart_readout_b;
static lv_obj_t* s_chart_age;
static lv_obj_t* s_axis_lbl[5];
static lv_obj_t* s_x_lbl[4];
static lv_obj_t* s_scrub_line;
static lv_obj_t* s_scrub_bubble;
static lv_obj_t* s_scrub_text;
static uint32_t s_scrub_hide_at;
static lv_obj_t* s_range_lbl[3];
static lv_obj_t* s_range_hl;
static int s_range_w = 440;
static lv_obj_t* s_detail_value;
static lv_obj_t* s_header_badge;
static lv_obj_t* s_header_badge_lbl;
static lv_obj_t* s_above_target;
static lv_obj_t* s_power_unit_lbl;
static lv_obj_t* s_side_h;
static lv_obj_t* s_side_big;
static lv_obj_t* s_side_big2;
static lv_obj_t* s_side_big2_lbl;
static lv_obj_t* s_side_delta;
static lv_obj_t* s_side_delta_lbl;
static lv_obj_t* s_side_hr2;
static lv_obj_t* s_side_hr3;
static lv_obj_t* s_side_l[5];
static lv_obj_t* s_side_legend[3];
static lv_obj_t* s_side_note;
static lv_obj_t* s_living_tab[2];
static int g_living_series = 0;
static lv_obj_t* s_ac_tab[2];
static lv_obj_t* s_ac_hl;
static int s_ac_w = 200;
static int g_ac_series = 0;

/* PWM request UI (S20.01/S20.02). A user change on the detail page sends one
 * 0x240 frame immediately: 1000 for OFF, 0..500 while ON. The in-force value
 * still comes from 0x280; the UI never sends on page entry or on a report.
 *
 * S20.14: the panel applies no ramp, clamp or dead-man to the output.
 * AVR/01_heizung_temp owns PWM safety (N15); the switch and slider only bound
 * the request to 0..500 permille (S20.01/S20.02). Nothing here shapes the
 * duty over time. */
static lv_obj_t* s_pwm_switch;
static lv_obj_t* s_pwm_switch_lbl;
static lv_obj_t* s_pwm_slider;
static lv_obj_t* s_pwm_req_lbl;
static lv_obj_t* s_pwm_force_lbl;
static bool s_pwm_on;
static bool s_pwm_touched;
static bool s_pwm_can_ok;
static uint16_t s_pwm_duty = 500;
static uint16_t s_pwm_last_tx = 0xFFFF;

static lv_obj_t* s_log_label;
static lv_obj_t* s_log_container;
static lv_obj_t* s_log_status;
static lv_obj_t* s_log_sd;
static lv_obj_t* s_log_follow;
static lv_obj_t* s_log_follow_lbl;
static lv_obj_t* s_log_ws;
static lv_obj_t* s_log_level[4];
static lv_obj_t* s_log_modal;
static bool s_log_follow_on = true;
static bool s_log_invert = false;
static uint8_t s_log_level_mask = 0x0F;
static int s_log_scroll = 0;
static uint32_t s_log_last_build;
#define LOG_LINES_MAX 2048
#define LOG_RECOLOR_BYTES 8192
static char* s_log_recolor = NULL;

static double s_axis_lo, s_axis_hi;
static int s_axis_key = -1;
static bool s_chart_force = true;
static uint32_t s_chart_last_sample;
static bool s_chart_hourly;
static uint32_t s_chart_t0, s_chart_t1;
static uint32_t s_chart_now;
static uint32_t s_chart_newest;

static void ui_show_screen(int screen);
static void ui_pop(void);
static void chart_rebuild(void);
static bool ensure_chart_buffers(void);

static uint16_t c565(uint32_t c) { return lv_color_to_u16(lv_color_hex(c)); }

static void chart_px(int x, int y, uint16_t c) {
  if (!s_chart_pixels || x < 0 || y < 0 || x >= CHART_W || y >= CHART_H) return;
  ((uint16_t*)s_chart_pixels)[y * CHART_W + x] = c;
}

static uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a) {
  const int r =
      (((fg >> 11) & 0x1f) * a + ((bg >> 11) & 0x1f) * (255 - a)) / 255;
  const int g = (((fg >> 5) & 0x3f) * a + ((bg >> 5) & 0x3f) * (255 - a)) / 255;
  const int b = ((fg & 0x1f) * a + (bg & 0x1f) * (255 - a)) / 255;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static void chart_blend_px(int x, int y, uint32_t color, uint8_t alpha) {
  if (!s_chart_pixels || x < 0 || y < 0 || x >= CHART_W || y >= CHART_H) return;
  uint16_t* p = &((uint16_t*)s_chart_pixels)[y * CHART_W + x];
  *p = blend565(*p, c565(color), alpha);
}

static void chart_hline(int x0, int x1, int y, uint16_t c) {
  if (x1 < x0) {
    int t = x0;
    x0 = x1;
    x1 = t;
  }
  for (int x = x0; x <= x1; x++) chart_px(x, y, c);
}

static void chart_vline(int x, int y0, int y1, uint16_t c) {
  if (y1 < y0) {
    int t = y0;
    y0 = y1;
    y1 = t;
  }
  for (int y = y0; y <= y1; y++) chart_px(x, y, c);
}

static void chart_line(int x0, int y0, int x1, int y1, uint16_t c) {
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = y1 > y0 ? y0 - y1 : y1 - y0;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    chart_px(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static void chart_dash_line(int x0, int y0, int x1, int y1, uint16_t c) {
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int dy = y1 > y0 ? y1 - y0 : y0 - y1;
  const int steps = dx > dy ? dx : dy;
  if (steps <= 0) {
    chart_px(x0, y0, c);
    return;
  }
  for (int i = 0; i <= steps; i++) {
    if ((i / 4) % 2 == 0) {
      const int x = x0 + (x1 - x0) * i / steps;
      const int y = y0 + (y1 - y0) * i / steps;
      chart_px(x, y, c);
    }
  }
}

static size_t chart_col_for(uint32_t t, uint32_t t0, uint32_t t1) {
  const uint32_t span = t1 > t0 ? t1 - t0 : 1u;
  if (t < t0) t = t0;
  if (t > t1) t = t1;
  size_t col = (size_t)((uint64_t)(t - t0) * HAUS_CHART_COLUMNS / span);
  if (col >= HAUS_CHART_COLUMNS) col = HAUS_CHART_COLUMNS - 1;
  return col;
}

static void chart_cfg_for(int detail, chart_cfg_t* cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->b = -1;
  switch (detail) {
    case UI_SCREEN_CHAMBER:
      cfg->a = 0;
      cfg->use_target = 1;
      cfg->hourly_ok = 1;
      cfg->unit = CHART_UNIT_TEMP;
      cfg->color_a = C_COLD;
      cfg->color_b = C_COLD;
      break;
    case UI_SCREEN_WATER_IN:
    case UI_SCREEN_WATER_OUT:
      cfg->a = 1;
      cfg->b = 2;
      cfg->use_target = 1;
      cfg->hourly_ok = 1;
      cfg->band = 1;
      cfg->lines_only = 1;
      cfg->unit = CHART_UNIT_TEMP;
      cfg->color_a = C_CYAN;
      cfg->color_b = C_HOT;
      break;
    case UI_SCREEN_POWER:
      cfg->a = 7;
      cfg->use_target = 0;
      cfg->hourly_ok = 1;
      cfg->unit = CHART_UNIT_POWER;
      cfg->color_a = C_ACCENT;
      cfg->color_b = C_ACCENT;
      break;
    case UI_SCREEN_LIVING:
      cfg->a = 3 + g_living_series;
      cfg->use_target = 0;
      cfg->hourly_ok = 1;
      cfg->unit = (g_living_series == 0) ? CHART_UNIT_TEMP : CHART_UNIT_RH;
      cfg->color_a = C_CYAN;
      cfg->color_b = cfg->color_a;
      break;
    case UI_SCREEN_AC:
      cfg->a = (g_ac_series == 0) ? GUI_SERIES_AC_RMS : GUI_SERIES_AC_ANGLE;
      cfg->use_target = 0;
      cfg->hourly_ok = 1;
      cfg->unit = (g_ac_series == 0) ? CHART_UNIT_AC_RMS : CHART_UNIT_AC_ANGLE;
      cfg->lines_only = 1;
      cfg->color_a = (g_ac_series == 0) ? C_AMBER : C_VIOLET;
      cfg->color_b = cfg->color_a;
      break;
    default:
      cfg->a = 0;
      cfg->use_target = 0;
      cfg->hourly_ok = 1;
      cfg->unit = CHART_UNIT_TEMP;
      cfg->color_a = C_COLD;
      cfg->color_b = C_COLD;
      break;
  }
}

static size_t chart_sample_reader(void* ctx, size_t first, size_t count,
                                  gui_sample_t* out) {
  const uint32_t start = *(const uint32_t*)ctx;
  return gui_read_samples_chunk(start, first, count, out);
}

static size_t chart_rollup_reader(void* ctx, size_t first, size_t count,
                                  RollupColumn* out) {
  const uint32_t start = *(const uint32_t*)ctx;
  return gui_read_rollup_chunk(start, first, count, out);
}

static void chart_legend_update(const chart_cfg_t* cfg) {
  if (!s_legend_a || !cfg) return;
  const char* a = NULL;
  const char* b = NULL;
  const char* t = NULL;
  switch (g_detail_screen) {
    case UI_SCREEN_WATER_IN:
    case UI_SCREEN_WATER_OUT:
      break;
    case UI_SCREEN_CHAMBER:
      a = "- heater";
      t = "-- Soll";
      break;
    case UI_SCREEN_POWER:
      a = "- power";
      break;
    case UI_SCREEN_LIVING:
      a = (g_living_series == 0) ? "- temp" : "- RH";
      break;
    case UI_SCREEN_AC:
      a = (g_ac_series == 0) ? "- RMS" : "- angle";
      break;
    default:
      break;
  }
  txt(s_legend_a, a ? a : "");
  txt(s_legend_b, b ? b : "");
  txt(s_legend_t, t ? t : "");
  lv_obj_set_style_text_color(s_legend_a, lv_color_hex(cfg->color_a), 0);
  lv_obj_set_style_text_color(s_legend_b, lv_color_hex(cfg->color_b), 0);
  lv_obj_set_style_text_color(s_legend_t, lv_color_hex(C_ACCENT), 0);
  obj_set_hidden(s_legend_a, a == NULL);
  obj_set_hidden(s_legend_b, b == NULL);
  obj_set_hidden(s_legend_t, t == NULL);
}

static void chart_build_target(uint32_t t0, uint32_t t1, bool hourly) {
  if (!ensure_chart_buffers()) return;
  memset(s_target, 0, TARGET_BYTES);
  memset(s_target_n, 0, TARGET_N_BYTES);
  memset(s_target_sum, 0, TARGET_SUM_BYTES);

  if (hourly) {
    const size_t total = (size_t)((t1 - t0) / 3600u) + 1u;
    for (size_t first = 0; first < total; first += 64) {
      int16_t chunk[64];
      const size_t n = total - first < 64 ? total - first : 64;
      gui_read_target_hours_chunk(t0, first, n, chunk);
      for (size_t i = 0; i < n; i++) {
        if (chunk[i] == HAUS_CAN_TEMP_EMPTY || chunk[i] == GUI_EMPTY) continue;
        const uint32_t t = (t0 - t0 % 3600u) + (uint32_t)(first + i) * 3600u;
        const size_t col = chart_col_for(t, t0, t1);
        s_target_sum[col] += chunk[i];
        s_target_n[col]++;
      }
    }
  } else {
    const size_t total = (size_t)((t1 - t0) / 60u) + 1u;
    for (size_t first = 0; first < total; first += HAUS_CHART_CHUNK_SAMPLES) {
      int16_t chunk[HAUS_CHART_CHUNK_SAMPLES];
      const size_t n = total - first < HAUS_CHART_CHUNK_SAMPLES
                           ? total - first
                           : HAUS_CHART_CHUNK_SAMPLES;
      gui_read_target_samples_chunk(t0, first, n, chunk);
      for (size_t i = 0; i < n; i++) {
        if (chunk[i] == HAUS_CAN_TEMP_EMPTY || chunk[i] == GUI_EMPTY) continue;
        const uint32_t t = (t0 - t0 % 60u) + (uint32_t)(first + i) * 60u;
        const size_t col = chart_col_for(t, t0, t1);
        s_target_sum[col] += chunk[i];
        s_target_n[col]++;
      }
    }
  }

  for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) {
    if (s_target_n[i]) {
      s_target[i] = (int16_t)(s_target_sum[i] / s_target_n[i]);
    }
  }
}

static uint32_t local_midnight(uint32_t now) {
  time_t t = (time_t)now;
  struct tm tm;
  if (localtime_r(&t, &tm)) {
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    const time_t m = mktime(&tm);
    if (m > 0) return (uint32_t)m;
  }
  return now > 86400u ? now - 86400u : 0;
}

static void chart_build(void) {
  HausState st;
  gui_get_state(&st);
  uint32_t now = st.can.timestamp;
  if (now == 0) now = (uint32_t)time(NULL);
  if (now == 0) return;
  if (!ensure_chart_buffers()) return;

  /* L1 is today's minute buffer. A rolling 24 h window would read
   * yesterday's slots as today's future slots, so the 1-day view is
   * local midnight .. now. The week/month view still rolls. */
  uint32_t t0;
  const uint32_t t1 = now;
  if (g_range == UI_RANGE_DAY) {
    t0 = local_midnight(now);
  } else {
    const uint32_t span =
        (g_range == UI_RANGE_WEEK) ? 7u * 86400u : 30u * 86400u;
    t0 = now > span ? now - span : 0;
  }

  chart_cfg_t cfg;
  chart_cfg_for(g_detail_screen, &cfg);
  chart_legend_update(&cfg);
  const bool hourly = (g_range != UI_RANGE_DAY) && cfg.hourly_ok;

  uint32_t newest_epoch = 0;
  if (hourly) {
    const size_t total = (size_t)((t1 - t0) / 3600u) + 1u;
    newest_epoch = haus_chart_build_rollup(s_cols, t0, t1, chart_rollup_reader,
                                           (void*)&t0, total, cfg.a, cfg.b);
  } else {
    const size_t total = (size_t)(t1 - t0) / 60u + 1u;
    newest_epoch = haus_chart_build_samples(s_cols, t0, t1, chart_sample_reader,
                                            (void*)&t0, total, cfg.a, cfg.b);
  }

  chart_build_target(t0, t1, hourly);

  s_chart_hourly = hourly;
  s_chart_t0 = t0;
  s_chart_t1 = t1;
  s_chart_now = now;
  s_chart_newest = newest_epoch;
  s_chart_last_sample = gui_history_newest_epoch();
  s_chart_force = false;

  /* If the target series is the only thing present (a setpoint-only range),
   * treat that as data too. */
  bool any = false;
  for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) {
    if (s_cols[i].n[0] || (cfg.b >= 0 && s_cols[i].n[1]) ||
        (cfg.use_target && s_target_n[i])) {
      any = true;
      break;
    }
  }
  (void)any;
}

static int chart_value_y(int32_t v) {
  const double span = s_axis_hi - s_axis_lo;
  if (span <= 0) return CHART_Y1;
  double y = CHART_Y1 - ((double)v - s_axis_lo) * (CHART_Y1 - CHART_Y0) / span;
  if (y < CHART_Y0) y = CHART_Y0;
  if (y > CHART_Y1) y = CHART_Y1;
  return (int)(y + 0.5);
}

static void chart_nice_axis(double lo, double hi) {
  double span = hi - lo;
  if (span < 1.0) span = 1.0;
  const double raw = span / 4.0;
  double mag = 1.0;
  while (mag * 10.0 <= raw) mag *= 10.0;
  while (mag > raw) mag /= 10.0;
  double step = 1.0;
  if (raw <= mag)
    step = mag;
  else if (raw <= 2.0 * mag)
    step = 2.0 * mag;
  else if (raw <= 5.0 * mag)
    step = 5.0 * mag;
  else
    step = 10.0 * mag;

  lo = floor(lo / step) * step;
  hi = ceil(hi / step) * step;
  if (hi <= lo) hi = lo + step;
  s_axis_lo = lo;
  s_axis_hi = hi;
}

static void chart_axis(chart_cfg_t* cfg) {
  const int key = g_detail_screen * 16 + g_range * 4 +
                  (g_detail_screen == UI_SCREEN_LIVING ? g_living_series : 0);
  bool have = false;
  int32_t mn = 0, mx = 0;

  for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) {
    if (s_cols[i].n[0]) {
      if (!have) {
        mn = s_cols[i].mn[0];
        mx = s_cols[i].mx[0];
        have = true;
      }
      if (s_cols[i].mn[0] < mn) mn = s_cols[i].mn[0];
      if (s_cols[i].mx[0] > mx) mx = s_cols[i].mx[0];
    }
    if (cfg->b >= 0 && s_cols[i].n[1]) {
      if (!have) {
        mn = s_cols[i].mn[1];
        mx = s_cols[i].mx[1];
        have = true;
      }
      if (s_cols[i].mn[1] < mn) mn = s_cols[i].mn[1];
      if (s_cols[i].mx[1] > mx) mx = s_cols[i].mx[1];
    }
    if (cfg->use_target && s_target_n[i]) {
      if (!have) {
        mn = s_target[i];
        mx = s_target[i];
        have = true;
      }
      if (s_target[i] < mn) mn = s_target[i];
      if (s_target[i] > mx) mx = s_target[i];
    }
  }

  if (!have) {
    s_axis_lo = 0;
    s_axis_hi = (cfg->unit == CHART_UNIT_POWER) ? 10000 : 1;
    s_axis_key = key;
    return;
  }

  int32_t min_span = 1000;
  if (cfg->unit == CHART_UNIT_POWER) min_span = 10000;
  if (mx - mn < min_span) {
    const int32_t mid = (mn + mx) / 2;
    mn = mid - min_span / 2;
    mx = mid + min_span / 2;
  }

  const bool same = (key == s_axis_key);
  const double band = s_axis_hi - s_axis_lo;
  if (same && band > 0) {
    const double guard = band * 0.12;
    if ((double)mn >= s_axis_lo - guard && (double)mx <= s_axis_hi + guard)
      return;
  }

  double lo = (double)mn;
  double hi = (double)mx;
  const double pad = (hi - lo) * 0.12;
  chart_nice_axis(lo - pad, hi + pad);
  s_axis_key = key;
}

static void chart_format_value(char* buf, size_t n, int32_t v, int unit) {
  switch (unit) {
    case CHART_UNIT_TEMP:
      temp_text((int16_t)v, buf, n, false);
      break;
    case CHART_UNIT_RH:
      snprintf(buf, n, "%.1f", v / 100.0);
      break;
    case CHART_UNIT_POWER:
      if (v >= 10000)
        snprintf(buf, n, "%.2f", v / 10000.0);
      else
        snprintf(buf, n, "%d", (int)(v / 10));
      break;
    case CHART_UNIT_AC_RMS:
      snprintf(buf, n, "%.1f", v / 10.0);
      break;
    case CHART_UNIT_AC_ANGLE:
      snprintf(buf, n, "%.1f", v / 100.0);
      break;
    default:
      snprintf(buf, n, "%d", (int)v);
      break;
  }
}

static void chart_format_axis(char* buf, size_t n, int32_t v, int unit) {
  if (unit == CHART_UNIT_POWER && s_axis_hi >= 10000) {
    snprintf(buf, n, "%.1f", v / 10000.0);
  } else {
    chart_format_value(buf, n, v, unit);
  }
}

static void chart_labels(chart_cfg_t* cfg) {
  char buf[32];
  for (int i = 0; i < 5; i++) {
    const double frac = (double)i / 4.0;
    const int32_t v = (int32_t)(s_axis_hi - frac * (s_axis_hi - s_axis_lo));
    chart_format_axis(buf, sizeof(buf), v, cfg->unit);
    txt(s_axis_lbl[i], buf);
  }

  const char* x[4];
  char dayx[4][8];
  if (g_range == UI_RANGE_DAY) {
    for (int i = 0; i < 4; i++) {
      const uint32_t t =
          s_chart_t0 + (uint32_t)((uint64_t)i * (s_chart_t1 - s_chart_t0) / 3);
      time_t tt = (time_t)t;
      struct tm tm;
      if (localtime_r(&tt, &tm))
        snprintf(dayx[i], sizeof(dayx[i]), "%02d", tm.tm_hour);
      else
        snprintf(dayx[i], sizeof(dayx[i]), "--");
    }
    x[0] = dayx[0];
    x[1] = dayx[1];
    x[2] = dayx[2];
    x[3] = dayx[3];
  } else if (g_range == UI_RANGE_WEEK) {
    x[0] = "-7d";
    x[1] = "-5d";
    x[2] = "-3d";
    x[3] = "now";
  } else {
    x[0] = "-30d";
    x[1] = "-20d";
    x[2] = "-10d";
    x[3] = "now";
  }
  for (int i = 0; i < 4; i++) txt(s_x_lbl[i], x[i]);
}

static void chart_draw(void) {
  if (!s_chart_pixels || !s_cols) return;
  /* Hide the setpoint label before the no-data early return; otherwise the
   * power screen keeps a stale "Soll" legend. */
  if (s_chart_target_lbl) obj_set_hidden(s_chart_target_lbl, true);

  const uint16_t bg = c565(C_BG);
  for (int i = 0; i < CHART_W * CHART_H; i++)
    ((uint16_t*)s_chart_pixels)[i] = bg;

  const uint16_t line = c565(C_LINE);
  for (int i = 0; i <= 4; i++) {
    const int y = CHART_Y0 + i * (CHART_Y1 - CHART_Y0) / 4;
    chart_hline(CHART_X0, CHART_X1, y, line);
  }
  /* Three divisions to match the four x labels below the chart. */
  for (int i = 0; i <= 3; i++) {
    const int x = CHART_X0 + i * (CHART_X1 - CHART_X0) / 3;
    chart_vline(x, CHART_Y0, CHART_Y1, line);
  }
  chart_hline(CHART_X0, CHART_X1, CHART_Y1, c565(C_DIM));
  chart_vline(CHART_X0, CHART_Y0, CHART_Y1, c565(C_DIM));
  chart_vline(CHART_X1, CHART_Y0, CHART_Y1, c565(C_DIM));

  chart_cfg_t cfg;
  chart_cfg_for(g_detail_screen, &cfg);
  chart_axis(&cfg);
  chart_labels(&cfg);

  bool any = false;
  size_t first_any = HAUS_CHART_COLUMNS;
  size_t last_any = 0;
  for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) {
    if (s_cols[i].n[0] || (cfg.b >= 0 && s_cols[i].n[1]) ||
        (cfg.use_target && s_target_n[i])) {
      if (!any) first_any = i;
      last_any = i;
      any = true;
    }
  }
  if (s_chart_nodata) {
    if (any)
      obj_set_hidden(s_chart_nodata, true);
    else
      obj_set_hidden(s_chart_nodata, false);
  }
  if (!any) {
    lv_obj_invalidate(s_chart_canvas);
    return;
  }

  const uint16_t ca = c565(cfg.color_a);
  const uint16_t cb = c565(cfg.color_b);

  for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) {
    const ChartColumn* c = &s_cols[i];
    const int x = CHART_X0 + (int)((uint64_t)i * (CHART_X1 - CHART_X0) /
                                   (HAUS_CHART_COLUMNS - 1));
    const bool gap = (c->n[0] == 0) && (cfg.b < 0 || c->n[1] == 0) &&
                     !(cfg.use_target && s_target_n[i]);
    if (gap) {
      /* Only mark dropouts between real data. Leading and trailing empty
       * columns are "no data yet", not a gap, and must stay clean. */
      if (i < first_any || i > last_any) continue;
      for (int k = 0; k < 3; k++)
        chart_vline(x, CHART_Y1 - 2 - k * 4, CHART_Y1 - 2 - k * 4,
                    c565(C_FAINT));
      continue;
    }

    /* Waterfall fill for the first series. Power is special: fill from the
     * zero line towards the value, red for import and green for export, so
     * the visual area is the quantity, not an arbitrary gradient to the
     * bottom of the plot. */
    if (c->n[0] && !cfg.lines_only) {
      const int32_t mean = c->sum[0] / c->n[0];
      if (cfg.unit == CHART_UNIT_POWER) {
        const int zero_y = chart_value_y(0);
        const int y = chart_value_y(mean);
        const uint32_t fill = mean >= 0 ? 0xd4553a : 0x5caf6e;
        const int from = y < zero_y ? y : zero_y;
        const int to = y < zero_y ? zero_y : y;
        const int span = to - from;
        for (int yy = from; yy <= to; yy++) {
          const int d = yy < zero_y ? zero_y - yy : yy - zero_y;
          const uint8_t a = span > 0 ? (uint8_t)(90 * d / span) : (uint8_t)90;
          for (int dx = -1; dx <= 1; dx++) chart_blend_px(x + dx, yy, fill, a);
        }
      } else {
        const int y = chart_value_y(mean);
        for (int yy = y; yy <= CHART_Y1; yy++) {
          const double f =
              (CHART_Y1 > y) ? (double)(yy - y) / (double)(CHART_Y1 - y) : 1.0;
          const uint8_t a = (uint8_t)(70.0 * (1.0 - f));
          for (int dx = -1; dx <= 1; dx++)
            chart_blend_px(x + dx, yy, cfg.color_a, a);
        }
      }
    }

    /* Envelope treatment. Water is lines-only: no shaded envelope. */
    if (!cfg.lines_only && c->n[0]) {
      const int y0 = chart_value_y(c->mn[0]);
      const int y1 = chart_value_y(c->mx[0]);
      if (g_range == UI_RANGE_DAY && !cfg.band) {
        chart_vline(x, y0, y1, ca);
        chart_px(x, chart_value_y(c->sum[0] / c->n[0]), ca);
      } else {
        for (int y = y0; y <= y1; y++) chart_blend_px(x, y, cfg.color_a, 64);
      }
    }
    if (!cfg.lines_only && cfg.b >= 0 && c->n[1]) {
      const int y0 = chart_value_y(c->mn[1]);
      const int y1 = chart_value_y(c->mx[1]);
      if (g_range == UI_RANGE_DAY && !cfg.band) {
        chart_vline(x, y0, y1, cb);
        chart_px(x, chart_value_y(c->sum[1] / c->n[1]), cb);
      } else {
        for (int y = y0; y <= y1; y++) chart_blend_px(x, y, cfg.color_b, 48);
      }
    }
  }

  HausState chart_state;
  gui_get_state(&chart_state);

  /* Mean lines. */
  for (int s = 0; s < 2; s++) {
    if (s == 1 && cfg.b < 0) break;
    int px = -1, py = 0;
    for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) {
      const int slot = s;
      if (s_cols[i].n[slot] == 0) {
        px = -1;
        continue;
      }
      const int x = CHART_X0 + (int)((uint64_t)i * (CHART_X1 - CHART_X0) /
                                     (HAUS_CHART_COLUMNS - 1));
      const int y = chart_value_y(s_cols[i].sum[slot] / s_cols[i].n[slot]);
      if (px >= 0) chart_line(px, py, x, y, s == 0 ? ca : cb);
      px = x;
      py = y;
    }
  }

  /* Soll: a real series, drawn as a dashed line. */
  if (cfg.use_target) {
    const uint16_t ct = c565(C_ACCENT);
    int px = -1, py = 0;
    for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) {
      if (!s_target_n[i]) {
        px = -1;
        continue;
      }
      const int x = CHART_X0 + (int)((uint64_t)i * (CHART_X1 - CHART_X0) /
                                     (HAUS_CHART_COLUMNS - 1));
      const int y = chart_value_y(s_target[i]);
      if (px >= 0) chart_dash_line(px, py, x, y, ct);
      px = x;
      py = y;
    }
    int last = -1;
    for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++)
      if (s_target_n[i]) last = (int)i;
    if (last >= 0 && s_chart_target_lbl) {
      char val[24], b[40];
      chart_format_value(val, sizeof(val), s_target[last], CHART_UNIT_TEMP);
      snprintf(b, sizeof(b), "Soll %s", val);
      txt(s_chart_target_lbl, b);
      obj_set_hidden(s_chart_target_lbl, false);
    }
  } else if (s_chart_target_lbl) {
    obj_set_hidden(s_chart_target_lbl, true);
  }

  lv_obj_invalidate(s_chart_canvas);
}

static bool chart_live_value(int series, int32_t* out) {
  if (!out) return false;
  HausState st;
  gui_get_state(&st);
  switch (series) {
    case 0:
      *out = st.can.temp_by_fn[SENSOR_FN_CHAMBER];
      break;
    case 1:
      *out = st.can.temp_by_fn[SENSOR_FN_WATER_IN];
      break;
    case 2:
      *out = st.can.temp_by_fn[SENSOR_FN_WATER_OUT];
      break;
    case 3:
      if (!(st.climate.climate_flags & CLIMATE_VALID_SHT45)) return false;
      *out = st.climate.living_c100;
      break;
    case 4:
      if (!(st.climate.climate_flags & CLIMATE_VALID_SHT45)) return false;
      *out = (int32_t)st.climate.hum_c100;
      break;
    case GUI_SERIES_TARGET:
      if (st.can.chamber_target_c <= 0) return false;
      *out = st.can.chamber_target_c;
      break;
    case 7:
      if (!st.can.valid) return false;
      *out = st.can.watts > 0 ? st.can.watts * 10 : 0;
      if (*out > 65535) *out = 65535;
      break;
    case GUI_SERIES_AC_RMS:
      if (!st.can.ac_fresh) return false;
      *out = (int32_t)st.can.ac_rms_dc;
      break;
    case GUI_SERIES_AC_ANGLE:
      if (!st.can.ac_fresh) return false;
      *out = (int32_t)st.can.ac_angle_cdeg;
      break;
    default:
      return false;
  }
  return *out != HAUS_CAN_TEMP_EMPTY && *out != GUI_EMPTY;
}

static void chart_readout_line(lv_obj_t* label, int slot, int series,
                               const char* prefix) {
  if (!label) return;
  chart_cfg_t cfg;
  chart_cfg_for(g_detail_screen, &cfg);
  int32_t mn = 0, mx = 0, now = 0;
  int64_t sum = 0;
  uint32_t n = 0;

  for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) {
    if (!s_cols[i].n[slot]) continue;
    if (n == 0) {
      mn = s_cols[i].mn[slot];
      mx = s_cols[i].mx[slot];
    }
    if (s_cols[i].mn[slot] < mn) mn = s_cols[i].mn[slot];
    if (s_cols[i].mx[slot] > mx) mx = s_cols[i].mx[slot];
    sum += s_cols[i].sum[slot];
    n += s_cols[i].n[slot];
    now = s_cols[i].sum[slot] / s_cols[i].n[slot];
  }

  int32_t live = 0;
  if (chart_live_value(series, &live)) now = live;

  char a[24], b[24], c[24], d[24], line[192];
  if (n == 0) {
    snprintf(line, sizeof(line), "%sno data in this range", prefix);
  } else {
    chart_format_axis(a, sizeof(a), mn, cfg.unit);
    chart_format_axis(b, sizeof(b), mx, cfg.unit);
    chart_format_axis(c, sizeof(c), (int32_t)(sum / (int64_t)n), cfg.unit);
    chart_format_axis(d, sizeof(d), now, cfg.unit);
    snprintf(line, sizeof(line), "%smin %s   max %s   mean %s   now %s", prefix,
             a, b, c, d);
  }
  txt(label, line);
}

/* S23.27: count time above the target from individual samples, not from a
 * bucket mean. Day view is exact (one sample per minute). Week/month use the
 * hourly min/max to estimate the above-target fraction when the bucket
 * straddles the target. */
static uint32_t above_target_seconds(void) {
  uint32_t above = 0;
  if (s_chart_t1 <= s_chart_t0) return 0;

  if (g_range == UI_RANGE_DAY) {
    const size_t total = (size_t)((s_chart_t1 - s_chart_t0) / 60u) + 1u;
    for (size_t first = 0; first < total; first += 64) {
      gui_sample_t chunk[64];
      const size_t n = total - first < 64 ? total - first : 64;
      gui_read_samples_chunk(s_chart_t0, first, n, chunk);
      for (size_t i = 0; i < n; i++) {
        if (chunk[i].epoch < s_chart_t0 || chunk[i].epoch > s_chart_t1)
          continue;
        if (chunk[i].target_c100 <= 0 ||
            chunk[i].temp[0] == HAUS_CAN_TEMP_EMPTY)
          continue;
        if (chunk[i].temp[0] > chunk[i].target_c100) above += 60u;
      }
    }
    return above;
  }

  const size_t total = (size_t)((s_chart_t1 - s_chart_t0) / 3600u) + 1u;
  for (size_t first = 0; first < total; first += 16) {
    RollupColumn chunk[16];
    const size_t n = total - first < 16 ? total - first : 16;
    gui_read_rollup_chunk(s_chart_t0, first, n, chunk);
    for (size_t i = 0; i < n; i++) {
      const RollupColumn* r = &chunk[i];
      if (r->target_c100 <= 0 || r->n[0] == 0) continue;
      if (r->mn[0] > r->target_c100) {
        above += (uint32_t)r->n[0] * 60u;
      } else if (r->mx[0] > r->target_c100 && r->mx[0] > r->mn[0]) {
        /* Linear estimate across the bucket's observed min/max. */
        const int64_t hot = (int64_t)(r->mx[0] - r->target_c100) * r->n[0];
        above += (uint32_t)(hot * 60 / (r->mx[0] - r->mn[0]));
      }
    }
  }
  return above;
}

static void chart_update_readouts(void) {
  if (!s_cols || !s_chart_readout) return;
  chart_cfg_t cfg;
  chart_cfg_for(g_detail_screen, &cfg);

  chart_readout_line(s_chart_readout, 0, cfg.a, cfg.b >= 0 ? "in  " : "");
  if (cfg.b >= 0) chart_readout_line(s_chart_readout_b, 1, cfg.b, "out ");

  uint32_t newest = s_chart_newest;
  if (!newest) {
    /* A target-only range has no sample series; fall back to its column. */
    for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) {
      if (cfg.use_target && s_target_n[i]) newest = s_cols[i].t;
    }
  }
  if (!newest) {
    txt(s_chart_age, "data age --");
    obj_set_hidden(s_chart_stale, true);
    return;
  }

  const uint32_t age = s_chart_now > newest ? s_chart_now - newest : 0;
  char agebuf[96];
  if (age < 90)
    snprintf(agebuf, sizeof(agebuf), "data age now");
  else if (age < 3600)
    snprintf(agebuf, sizeof(agebuf), "data age %u min ago",
             (unsigned)(age / 60));
  else if (age < 86400)
    snprintf(agebuf, sizeof(agebuf), "data age %u h ago",
             (unsigned)(age / 3600));
  else
    snprintf(agebuf, sizeof(agebuf), "data age %u d ago",
             (unsigned)(age / 86400));
  if (g_range != UI_RANGE_DAY) {
    size_t used = strlen(agebuf);
    snprintf(agebuf + used, sizeof(agebuf) - used,
             "  - envelope of hourly aggregates");
  }
  txt(s_chart_age, agebuf);

  if (g_detail_screen == UI_SCREEN_CHAMBER && s_above_target) {
    const uint32_t above_s = above_target_seconds();
    char ab[40];
    snprintf(ab, sizeof(ab), "above target %uh %02um",
             (unsigned)(above_s / 3600), (unsigned)((above_s / 60) % 60));
    txt(s_above_target, ab);
  }

  if (age > 3600) {
    time_t t = (time_t)newest;
    struct tm tm;
    if (localtime_r(&t, &tm)) {
      snprintf(agebuf, sizeof(agebuf), "history as of %02d:%02d", tm.tm_hour,
               tm.tm_min);
      txt(s_chart_stale, agebuf);
      obj_set_hidden(s_chart_stale, false);
    }
  } else {
    obj_set_hidden(s_chart_stale, true);
  }
}

static void chart_scrub_hide(void) {
  s_scrub_hide_at = 0;
  if (s_scrub_line) obj_set_hidden(s_scrub_line, true);
  if (s_scrub_bubble) obj_set_hidden(s_scrub_bubble, true);
}

static void chart_scrub_cb(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_RELEASED) {
    s_scrub_hide_at = lv_tick_get() + 1500;
    return;
  }
  if (!s_scrub_line || !s_scrub_bubble || !s_scrub_text) return;

  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  lv_obj_t* chart = lv_event_get_user_data(e);
  lv_area_t area;
  lv_obj_get_coords(chart, &area);
  int x = p.x - area.x1;
  if (x < CHART_X0) x = CHART_X0;
  if (x > CHART_X1) x = CHART_X1;
  const size_t col = (size_t)((x - CHART_X0) * (HAUS_CHART_COLUMNS - 1) /
                              (CHART_X1 - CHART_X0));

  lv_obj_set_pos(s_scrub_line, x, CHART_Y0);
  obj_set_hidden(s_scrub_line, false);
  s_scrub_hide_at = 0;

  if (s_cols[col].n[0] == 0) {
    txt(s_scrub_text, "no data");
  } else {
    time_t t = (time_t)s_cols[col].t;
    const uint32_t span = s_chart_t1 > s_chart_t0 ? s_chart_t1 - s_chart_t0 : 0;
    uint32_t col_s = span / HAUS_CHART_COLUMNS;
    time_t te = (time_t)((uint32_t)t + col_s);
    if ((uint32_t)te > s_chart_t1) te = (time_t)s_chart_t1;
    struct tm tm, tme;
    char when[24] = "";
    if (localtime_r(&t, &tm) && localtime_r(&te, &tme))
      snprintf(when, sizeof(when), "%02d:%02d-%02d:%02d", tm.tm_hour, tm.tm_min,
               tme.tm_hour, tme.tm_min);
    chart_cfg_t cfg;
    chart_cfg_for(g_detail_screen, &cfg);
    char mn[16], mean[16], mx[16], line[80];
    chart_format_value(mn, sizeof(mn), s_cols[col].mn[0], cfg.unit);
    chart_format_value(mean, sizeof(mean),
                       s_cols[col].sum[0] / s_cols[col].n[0], cfg.unit);
    chart_format_value(mx, sizeof(mx), s_cols[col].mx[0], cfg.unit);
    snprintf(line, sizeof(line), "%s  %s / %s / %s", when, mn, mean, mx);
    txt(s_scrub_text, line);
  }
  const int bw = 300;
  int bx = x - bw / 2;
  if (bx < 0) bx = 0;
  const int chart_w = lv_obj_get_width(chart);
  if (bx + bw > chart_w) bx = chart_w - bw;
  lv_obj_set_pos(s_scrub_bubble, bx, CHART_Y0 - 4);
  obj_set_hidden(s_scrub_bubble, false);
}

static void chart_build_widgets(int top) {
  lv_obj_t* c = lv_obj_create(lv_screen_active());
  lv_obj_set_size(c, CHART_W, CHART_H);
  lv_obj_set_pos(c, 20, top);
  lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_pad_all(c, 0, 0);
  obj_set_scrollable(c, false);
  obj_set_clickable(c, true);
  lv_obj_add_event_cb(c, chart_scrub_cb, LV_EVENT_PRESSED, c);
  lv_obj_add_event_cb(c, chart_scrub_cb, LV_EVENT_PRESSING, c);
  lv_obj_add_event_cb(c, chart_scrub_cb, LV_EVENT_RELEASED, c);

  s_chart_canvas = lv_canvas_create(c);
  lv_obj_set_size(s_chart_canvas, CHART_W, CHART_H);
  lv_obj_set_pos(s_chart_canvas, 0, 0);
  s_chart_pixels = (uint8_t*)lv_malloc(CHART_W * CHART_H * 2);
  if (s_chart_pixels) {
    lv_canvas_set_buffer(s_chart_canvas, s_chart_pixels, CHART_W, CHART_H,
                         LV_COLOR_FORMAT_RGB565);
  }
  obj_set_clickable(s_chart_canvas, false);

  for (int i = 0; i < 5; i++) {
    s_axis_lbl[i] =
        label_make(c, "", 0, CHART_Y0 + i * (CHART_Y1 - CHART_Y0) / 4 - 8,
                   &lv_font_montserrat_16, C_DIM);
    lv_obj_set_width(s_axis_lbl[i], 48);
    lv_obj_set_style_text_align(s_axis_lbl[i], LV_TEXT_ALIGN_RIGHT, 0);
  }
  for (int i = 0; i < 4; i++) {
    s_x_lbl[i] =
        label_make(c, "", 0, CHART_Y1 + 8, &lv_font_montserrat_16, C_DIM);
    lv_obj_set_width(s_x_lbl[i], 44);
    lv_obj_set_style_text_align(s_x_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_x_lbl[i], CHART_X0 + i * (CHART_X1 - CHART_X0) / 3 - 22,
                   CHART_Y1 + 8);
  }

  s_chart_nodata =
      label_make(c, "no data", 0, 0, &lv_font_montserrat_24, C_DIM);
  lv_obj_center(s_chart_nodata);
  s_chart_stale = label_make(c, "", 250, 0, &lv_font_montserrat_16, C_AMBER);
  lv_obj_set_width(s_chart_stale, 180);
  lv_obj_set_style_text_align(s_chart_stale, LV_TEXT_ALIGN_RIGHT, 0);
  obj_set_hidden(s_chart_stale, true);

  s_chart_target_lbl = label_make(c, "Soll", 250, CHART_Y0 + 2,
                                  &lv_font_montserrat_16, C_ACCENT);
  lv_obj_set_width(s_chart_target_lbl, 180);
  lv_obj_set_style_text_align(s_chart_target_lbl, LV_TEXT_ALIGN_RIGHT, 0);

  s_legend_a =
      label_make(c, "", 56, 0, &lv_font_montserrat_16, C_CYAN);
  s_legend_b =
      label_make(c, "", 108, 0, &lv_font_montserrat_16, C_HOT);
  s_legend_t =
      label_make(c, "", 170, 0, &lv_font_montserrat_16, C_ACCENT);
  obj_set_hidden(s_legend_a, true);
  obj_set_hidden(s_legend_b, true);
  obj_set_hidden(s_legend_t, true);

  s_scrub_line = lv_obj_create(c);
  lv_obj_set_size(s_scrub_line, 1, CHART_Y1 - CHART_Y0);
  lv_obj_set_pos(s_scrub_line, CHART_X0, CHART_Y0);
  lv_obj_set_style_bg_color(s_scrub_line, lv_color_hex(C_INK), 0);
  lv_obj_set_style_border_width(s_scrub_line, 0, 0);
  lv_obj_set_style_radius(s_scrub_line, 0, 0);
  obj_set_clickable(s_scrub_line, false);
  obj_set_hidden(s_scrub_line, true);

  s_scrub_bubble = lv_obj_create(c);
  lv_obj_set_size(s_scrub_bubble, 300, 34);
  lv_obj_set_pos(s_scrub_bubble, 0, CHART_Y0 - 4);
  lv_obj_set_style_bg_color(s_scrub_bubble, lv_color_hex(C_PANEL), 0);
  lv_obj_set_style_bg_opa(s_scrub_bubble, 235, 0);
  lv_obj_set_style_border_color(s_scrub_bubble, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_border_width(s_scrub_bubble, 1, 0);
  lv_obj_set_style_radius(s_scrub_bubble, 6, 0);
  lv_obj_set_style_pad_all(s_scrub_bubble, 4, 0);
  obj_set_clickable(s_scrub_bubble, false);
  s_scrub_text =
      label_make(s_scrub_bubble, "", 4, 4, &lv_font_montserrat_16, C_INK);
  obj_set_hidden(s_scrub_bubble, true);
}

static void range_set(int range) {
  if (range < 0 || range > 2) return;
  g_range = range;
  if (s_range_hl) lv_obj_set_x(s_range_hl, range * (s_range_w / 3));
  for (int i = 0; i < 3; i++) {
    if (!s_range_lbl[i]) continue;
    lv_obj_set_style_text_color(s_range_lbl[i],
                                lv_color_hex(i == range ? C_INK : C_DIM), 0);
  }
  s_chart_force = true;
  chart_rebuild();
}

static void range_click_cb(lv_event_t* e) {
  range_set((int)(intptr_t)lv_event_get_user_data(e));
}

static void range_build(int top, int width) {
  s_range_w = width;
  lv_obj_t* seg = lv_obj_create(lv_screen_active());
  lv_obj_set_size(seg, width, 40);
  lv_obj_set_pos(seg, 20, top);
  lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(seg, 1, 0);
  lv_obj_set_style_border_color(seg, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_radius(seg, 8, 0);
  lv_obj_set_style_pad_all(seg, 0, 0);
  obj_set_scrollable(seg, false);

  s_range_hl = lv_obj_create(seg);
  lv_obj_set_size(s_range_hl, width / 3, 40);
  lv_obj_set_pos(s_range_hl, g_range * (width / 3), 0);
  lv_obj_set_style_bg_color(s_range_hl, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_bg_opa(s_range_hl, 32, 0);
  lv_obj_set_style_border_width(s_range_hl, 0, 0);
  lv_obj_set_style_radius(s_range_hl, 0, 0);
  obj_set_clickable(s_range_hl, false);

  const char* names[3] = {"1 day", "1 week", "1 month"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t* tab = lv_obj_create(seg);
    lv_obj_set_size(tab, width / 3, 40);
    lv_obj_set_pos(tab, i * (width / 3), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tab, 0, 0);
    lv_obj_set_style_pad_all(tab, 0, 0);
    obj_set_scrollable(tab, false);
    obj_set_clickable(tab, true);
    lv_obj_add_event_cb(tab, range_click_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)i);
    s_range_lbl[i] = label_make(tab, names[i], 0, 0, &lv_font_montserrat_16,
                                i == g_range ? C_INK : C_DIM);
    lv_obj_set_width(s_range_lbl[i], width / 3);
    lv_obj_set_style_text_align(s_range_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_range_lbl[i], 0, 10);
  }
}

/* ─── PWM request (S20.02) ───────────────────────────────────────────────
 * OFF is the switch, not a slider position: it sends 1000. ON sends the
 * 500..0 slider value. The slider's left end is 500 (minimum heat) and its
 * right end is 0 (full heat); internally it runs 0..500 so the knob moves
 * toward "more heat" to the right. A change sends immediately, but only while
 * the detail page is the current screen. */

static void pwm_control_visual(void) {
  if (s_pwm_switch_lbl) txt(s_pwm_switch_lbl, s_pwm_on ? "ON" : "OFF");
  if (s_pwm_switch) {
    if (s_pwm_can_ok)
      lv_obj_clear_state(s_pwm_switch, LV_STATE_DISABLED);
    else
      lv_obj_add_state(s_pwm_switch, LV_STATE_DISABLED);
    lv_obj_set_style_opa(s_pwm_switch, s_pwm_can_ok ? LV_OPA_COVER : 120, 0);
    lv_obj_set_style_bg_color(s_pwm_switch,
                              lv_color_hex(s_pwm_on ? C_OK : C_OFF), 0);
    lv_obj_set_style_bg_opa(s_pwm_switch, s_pwm_on ? 220 : 80, 0);
  }
  if (s_pwm_slider) {
    if (s_pwm_can_ok && s_pwm_on)
      lv_obj_clear_state(s_pwm_slider, LV_STATE_DISABLED);
    else
      lv_obj_add_state(s_pwm_slider, LV_STATE_DISABLED);
    lv_obj_set_style_opa(s_pwm_slider,
                         (s_pwm_can_ok && s_pwm_on) ? LV_OPA_COVER : 120, 0);
  }
  if (s_pwm_req_lbl) {
    char b[40];
    if (!s_pwm_can_ok)
      snprintf(b, sizeof(b), "CAN not ready");
    else if (s_pwm_on)
      snprintf(b, sizeof(b), "requested %u / 1000", (unsigned)s_pwm_duty);
    else
      snprintf(b, sizeof(b), "requested OFF (1000)");
    txt(s_pwm_req_lbl, b);
  }
}

static void pwm_send_if_changed(void) {
  if (g_current_screen != UI_SCREEN_CHAMBER || !s_pwm_can_ok) return;
  const uint16_t permille = s_pwm_on ? s_pwm_duty : 1000;
  if (permille == s_pwm_last_tx) return;
  if (gui_send_pwm(permille)) s_pwm_last_tx = permille;
}

static void pwm_switch_cb(lv_event_t* e) {
  (void)e;
  s_pwm_touched = true;
  s_pwm_on = !s_pwm_on;
  pwm_control_visual();
  pwm_send_if_changed();
}

static void pwm_slider_cb(lv_event_t* e) {
  (void)e;
  if (!s_pwm_slider) return;
  s_pwm_touched = true;
  s_pwm_duty = (uint16_t)(500 - lv_slider_get_value(s_pwm_slider));
  pwm_control_visual();
  pwm_send_if_changed();
}

static void pwm_force_text(uint16_t f) {
  if (!s_pwm_force_lbl) return;
  char b[40];
  if (f >= 1000)
    snprintf(b, sizeof(b), "in force OFF (1000)");
  else
    snprintf(b, sizeof(b), "in force %u / 1000", (unsigned)f);
  txt(s_pwm_force_lbl, b);
}

static void pwm_control_update(void) {
  if (!s_pwm_slider) return;
  HausState st;
  gui_get_state(&st);
  const uint16_t f = st.can.pwm_permille;
  const bool can_ok = st.can.can_status == GUI_STATUS_OK;
  if (can_ok != s_pwm_can_ok) {
    s_pwm_can_ok = can_ok;
    pwm_control_visual();
  }
  if (!s_pwm_touched) {
    s_pwm_on = (f <= 500);
    s_pwm_duty = (f <= 500) ? f : 500;
    s_pwm_last_tx = f;
    lv_slider_set_value(s_pwm_slider, 500 - s_pwm_duty, LV_ANIM_OFF);
    pwm_control_visual();
  }
  pwm_force_text(f);
}

static void pwm_control_build(int top) {
  (void)top;
  lv_obj_t* root = lv_screen_active();
  hline_w(root, 474, 56, 228, C_LINE);

  label_make(root, "PWM DUTY", 474, 64, &haus_body_21, C_INK);

  s_pwm_switch = lv_obj_create(root);
  lv_obj_set_size(s_pwm_switch, 80, 38);
  lv_obj_set_pos(s_pwm_switch, 622, 58);
  lv_obj_set_style_border_width(s_pwm_switch, 0, 0);
  lv_obj_set_style_radius(s_pwm_switch, 8, 0);
  lv_obj_set_style_pad_all(s_pwm_switch, 0, 0);
  obj_set_scrollable(s_pwm_switch, false);
  obj_set_clickable(s_pwm_switch, true);
  lv_obj_add_event_cb(s_pwm_switch, pwm_switch_cb, LV_EVENT_CLICKED, NULL);
  s_pwm_switch_lbl =
      label_make(s_pwm_switch, "OFF", 0, 0, &lv_font_montserrat_16, C_INK);
  lv_obj_center(s_pwm_switch_lbl);

  s_pwm_slider = lv_slider_create(root);
  lv_obj_set_size(s_pwm_slider, 30, 200);
  lv_obj_set_pos(s_pwm_slider, 500, 110);
  lv_bar_set_orientation(s_pwm_slider, LV_BAR_ORIENTATION_VERTICAL);
  lv_slider_set_range(s_pwm_slider, 0, 500);
  lv_obj_set_style_bg_color(s_pwm_slider, lv_color_hex(C_LINE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_pwm_slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s_pwm_slider, 6, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_pwm_slider, lv_color_hex(C_HOT),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_pwm_slider, lv_color_hex(C_INK), LV_PART_KNOB);
  lv_obj_set_style_width(s_pwm_slider, 26, LV_PART_KNOB);
  lv_obj_set_style_height(s_pwm_slider, 26, LV_PART_KNOB);
  obj_set_clickable(s_pwm_slider, true);
  lv_obj_add_event_cb(s_pwm_slider, pwm_slider_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);

  label_make(root, "0 full", 548, 108, &lv_font_montserrat_16, C_DIM);
  label_make(root, "500 min", 548, 288, &lv_font_montserrat_16, C_DIM);

  s_pwm_req_lbl = label_make(root, "", 474, 330, &haus_body_21, C_INK);
  s_pwm_force_lbl = label_make(root, "", 474, 358, &haus_body_21, C_DIM);

  lv_obj_t* warn =
      label_make(root, "This survives a power cut.", 474, 394,
                 &lv_font_montserrat_16, C_AMBER);
  lv_obj_set_width(warn, 228);
  lv_label_set_long_mode(warn, UI_LABEL_LONG_WRAP);

  s_pwm_touched = false;
  HausState st;
  gui_get_state(&st);
  const uint16_t f = st.can.pwm_permille;
  s_pwm_can_ok = st.can.can_status == GUI_STATUS_OK;
  s_pwm_on = (f <= 500);
  s_pwm_duty = (f <= 500) ? f : 500;
  s_pwm_last_tx = f;
  lv_slider_set_value(s_pwm_slider, 500 - s_pwm_duty, LV_ANIM_OFF);
  pwm_control_visual();
  pwm_force_text(f);
}
static void header_build_at(const char* title, const char* badge,
                            uint32_t badge_color, int title_y) {
  label_make(lv_screen_active(), title, 20, title_y, &lv_font_montserrat_32,
             C_INK);

  if (badge) {
    lv_obj_t* b = lv_obj_create(lv_screen_active());
    lv_obj_set_size(b, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(badge_color), 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_set_style_pad_ver(b, 4, 0);
    lv_obj_t* badge_lbl =
        label_make(b, badge, 0, 0, &lv_font_montserrat_16, badge_color);
    lv_obj_center(badge_lbl);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -102, 6);
    s_header_badge = b;
    s_header_badge_lbl = badge_lbl;
  } else {
    s_header_badge = NULL;
    s_header_badge_lbl = NULL;
  }
}

static void header_build(const char* title, const char* badge,
                         uint32_t badge_color) {
  header_build_at(title, badge, badge_color, 8);
}

static void header_badge_set(const char* text, uint32_t color) {
  if (!s_header_badge || !s_header_badge_lbl) return;
  txt(s_header_badge_lbl, text);
  lv_obj_set_style_text_color(s_header_badge_lbl, lv_color_hex(color), 0);
  lv_obj_set_style_border_color(s_header_badge, lv_color_hex(color), 0);
}

static void detail_value_update(void) {
  if (!s_detail_value) return;
  HausState st;
  gui_get_state(&st);
  char buf[40];
  int16_t v = HAUS_CAN_TEMP_EMPTY;

  switch (g_detail_screen) {
    case UI_SCREEN_CHAMBER:
      v = st.can.temp_by_fn[SENSOR_FN_CHAMBER];
      break;
    case UI_SCREEN_WATER_IN:
      v = st.can.temp_by_fn[SENSOR_FN_WATER_IN];
      break;
    case UI_SCREEN_WATER_OUT:
      v = st.can.temp_by_fn[SENSOR_FN_WATER_OUT];
      break;
    case UI_SCREEN_LIVING:
      if (st.climate.climate_flags & CLIMATE_VALID_SHT45) {
        if (g_living_series == 0)
          temp_text(st.climate.living_c100, buf, sizeof(buf), true);
        else
          snprintf(buf, sizeof(buf), "%.1f %%rh", st.climate.hum_c100 / 100.0);
      } else {
        snprintf(buf, sizeof(buf), "--");
      }
      txt(s_detail_value, buf);
      return;
    case UI_SCREEN_POWER:
      if (st.can.valid) {
        if (st.can.watts >= 1000)
          snprintf(buf, sizeof(buf), "%.2f kW", st.can.watts / 1000.0);
        else
          snprintf(buf, sizeof(buf), "%d W", (int)st.can.watts);
      } else {
        snprintf(buf, sizeof(buf), "--");
      }
      txt(s_detail_value, buf);
      return;
    case UI_SCREEN_AC:
      if (st.can.ac_fresh) {
        if (g_ac_series == 0)
          snprintf(buf, sizeof(buf), "%.1f cnt", st.can.ac_rms_dc / 10.0);
        else
          snprintf(buf, sizeof(buf), "%.0f°", st.can.ac_angle_cdeg / 100.0);
      } else {
        snprintf(buf, sizeof(buf), "--");
      }
      txt(s_detail_value, buf);
      return;
    default:
      return;
  }

  if (v != HAUS_CAN_TEMP_EMPTY) {
    char tb[24];
    temp_text(v, tb, sizeof(tb), true);
    snprintf(buf, sizeof(buf), "%s", tb);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  txt(s_detail_value, buf);
}

static void chart_rebuild(void) {
  if (!s_chart_canvas) return;
  chart_build();
  chart_draw();
  chart_update_readouts();
}

static void side_panel_update(void) {
  if (!s_side_h) return;
  HausState st;
  gui_get_state(&st);
  char b[64];
  obj_set_hidden(s_side_big2_lbl, true);
  obj_set_hidden(s_side_big2, true);
  obj_set_hidden(s_side_delta_lbl, true);
  obj_set_hidden(s_side_delta, true);
  obj_set_hidden(s_side_hr2, true);
  obj_set_hidden(s_side_hr3, true);
  for (int i = 0; i < 3; i++) obj_set_hidden(s_side_legend[i], true);
  const int16_t ch = st.can.temp_by_fn[SENSOR_FN_CHAMBER];
  const int16_t wi = st.can.temp_by_fn[SENSOR_FN_WATER_IN];
  const int16_t wo = st.can.temp_by_fn[SENSOR_FN_WATER_OUT];

  if (g_detail_screen == UI_SCREEN_WATER_IN ||
      g_detail_screen == UI_SCREEN_WATER_OUT) {
    txt(s_side_h, "WATER IN");
    if (wi != HAUS_CAN_TEMP_EMPTY) {
      temp_text(wi, b, sizeof(b), true);
      txt(s_side_big, b);
      lv_obj_set_style_text_color(s_side_big, lv_color_hex(C_CYAN), 0);
    } else {
      txt(s_side_big, "--");
      lv_obj_set_style_text_color(s_side_big, lv_color_hex(C_OFF), 0);
    }

    txt(s_side_big2_lbl, "WATER OUT");
    obj_set_hidden(s_side_big2_lbl, false);
    obj_set_hidden(s_side_hr2, false);
    if (wo != HAUS_CAN_TEMP_EMPTY) {
      temp_text(wo, b, sizeof(b), true);
      txt(s_side_big2, b);
      lv_obj_set_style_text_color(s_side_big2, lv_color_hex(C_HOT), 0);
    } else {
      txt(s_side_big2, "--");
      lv_obj_set_style_text_color(s_side_big2, lv_color_hex(C_OFF), 0);
    }
    obj_set_hidden(s_side_big2, false);

    txt(s_side_delta_lbl, "DELTA");
    obj_set_hidden(s_side_delta_lbl, false);
    obj_set_hidden(s_side_hr3, false);
    if (wi != HAUS_CAN_TEMP_EMPTY && ch != HAUS_CAN_TEMP_EMPTY) {
      snprintf(b, sizeof(b), "%.1f °C", (wi - ch) / 100.0);
      txt(s_side_delta, b);
      lv_obj_set_style_text_color(s_side_delta, lv_color_hex(C_INK), 0);
    } else {
      txt(s_side_delta, "--");
      lv_obj_set_style_text_color(s_side_delta, lv_color_hex(C_OFF), 0);
    }
    obj_set_hidden(s_side_delta, false);

    for (int i = 0; i < 5; i++) txt(s_side_l[i], "");
    txt(s_side_note, wo != HAUS_CAN_TEMP_EMPTY ? "" : "no water-out probe");

    txt(s_side_legend[0], "- water in");
    lv_obj_set_style_text_color(s_side_legend[0], lv_color_hex(C_CYAN), 0);
    obj_set_hidden(s_side_legend[0], wi == HAUS_CAN_TEMP_EMPTY);
    txt(s_side_legend[1], "- water out");
    lv_obj_set_style_text_color(s_side_legend[1], lv_color_hex(C_HOT), 0);
    obj_set_hidden(s_side_legend[1], wo == HAUS_CAN_TEMP_EMPTY);
    txt(s_side_legend[2], "-- Soll");
    lv_obj_set_style_text_color(s_side_legend[2], lv_color_hex(C_ACCENT), 0);
    obj_set_hidden(s_side_legend[2], st.can.chamber_target_c <= 0);
    return;
  }

  if (g_detail_screen == UI_SCREEN_POWER) {
    txt(s_side_h, "POWER");
    if (st.can.valid) {
      if (st.can.watts >= 1000)
        snprintf(b, sizeof(b), "%.2f kW", st.can.watts / 1000.0);
      else
        snprintf(b, sizeof(b), "%d W", (int)st.can.watts);
    } else {
      snprintf(b, sizeof(b), "--");
    }
    txt(s_side_big, b);
    lv_obj_set_style_text_color(s_side_big, lv_color_hex(C_INK), 0);
    txt(s_side_l[0], st.can.power_src == GUI_PWR_SRC_SML ? "SML live"
                                                          : "no source");
    txt(s_side_l[1], "");
    txt(s_side_l[2], "");
    txt(s_side_l[3], "");
    txt(s_side_l[4], "");
    txt(s_side_note, "");
    return;
  }

  if (g_detail_screen == UI_SCREEN_LIVING) {
    txt(s_side_h, "LIVING ROOM");
    if (st.climate.climate_flags & CLIMATE_VALID_SHT45) {
      if (g_living_series == 0) {
        temp_text(st.climate.living_c100, b, sizeof(b), true);
        lv_obj_set_style_text_color(s_side_big, lv_color_hex(C_COLD), 0);
      } else {
        snprintf(b, sizeof(b), "%.1f %%rh", st.climate.hum_c100 / 100.0);
        lv_obj_set_style_text_color(s_side_big, lv_color_hex(C_CYAN), 0);
      }
    } else {
      snprintf(b, sizeof(b), "--");
      lv_obj_set_style_text_color(s_side_big, lv_color_hex(C_OFF), 0);
    }
    txt(s_side_big, b);
    if (st.climate.climate_flags & CLIMATE_VALID_SHT45) {
      char t[20], h[20];
      temp_text(st.climate.living_c100, t, sizeof(t), true);
      snprintf(h, sizeof(h), "%.1f %%rh", st.climate.hum_c100 / 100.0);
      snprintf(b, sizeof(b), "%s   %s", t, h);
    } else {
      snprintf(b, sizeof(b), "--");
    }
    txt(s_side_l[0], b);
    txt(s_side_l[1], "SHT45 - H1");
    txt(s_side_l[2], "");
    txt(s_side_l[3], "");
    txt(s_side_l[4], "");
    txt(s_side_note, "");
    return;
  }

  txt(s_side_h, "");
  txt(s_side_big, "");
  for (int i = 0; i < 5; i++) txt(s_side_l[i], "");
  txt(s_side_note, "");
}

static void detail_update(void) {
  detail_value_update();
  if (g_detail_screen == UI_SCREEN_CHAMBER) {
    pwm_control_update();
    HausState st;
    gui_get_state(&st);
    switch (st.can.pwm_src) {
      case PWM_SRC_CAN:
        header_badge_set("LIVE", C_OK);
        break;
      case PWM_SRC_NRF:
        header_badge_set("NRF", C_AMBER);
        break;
      case PWM_SRC_FAULT:
        header_badge_set("FAULT", C_HOT);
        break;
      default:
        header_badge_set("RESTORED", C_DIM);
        break;
    }
  }

  if (g_detail_screen == UI_SCREEN_LIVING) {
    HausState st;
    gui_get_state(&st);
    header_badge_set(
        "I2C", (st.climate.climate_flags & CLIMATE_VALID_SHT45) ? C_OK : C_OFF);
  } else if (g_detail_screen == UI_SCREEN_POWER) {
    HausState st;
    gui_get_state(&st);
    if (st.can.power_src == GUI_PWR_SRC_SML)
      header_badge_set("SML", C_OK);
    else if (st.can.power_src == GUI_PWR_SRC_NONE)
      header_badge_set("--", C_OFF);
    else
      header_badge_set("ADC", C_AMBER);
  }

  const uint32_t newest = gui_history_newest_epoch();
  if (s_chart_force || newest > s_chart_last_sample)
    chart_rebuild();
  else
    chart_update_readouts();

  side_panel_update();

  if (s_scrub_hide_at && (int32_t)(lv_tick_get() - s_scrub_hide_at) >= 0) {
    chart_scrub_hide();
  }
}

static void living_series_set(int series) {
  if (series < 0 || series > 1) return;
  g_living_series = series;
  for (int i = 0; i < 2; i++) {
    if (!s_living_tab[i]) continue;
    lv_obj_t* tab = lv_obj_get_parent(s_living_tab[i]);
    if (tab) {
      lv_obj_set_style_bg_opa(tab, i == series ? 32 : LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_color(
          tab, lv_color_hex(i == series ? C_ACCENT : C_LINE), 0);
    }
    lv_obj_set_style_text_color(s_living_tab[i],
                                lv_color_hex(i == series ? C_INK : C_DIM), 0);
  }
  s_chart_force = true;
  chart_rebuild();
  detail_value_update();
}

static void living_series_cb(lv_event_t* e) {
  living_series_set((int)(intptr_t)lv_event_get_user_data(e));
}

static void living_tabs_build(int top) {
  static const char* names[2] = {"Temp", "RH"};
  for (int i = 0; i < 2; i++) {
    lv_obj_t* tab = lv_obj_create(lv_screen_active());
    lv_obj_set_size(tab, 140, 30);
    lv_obj_set_pos(tab, 20 + i * 148, top);
    lv_obj_set_style_bg_opa(tab, i == g_living_series ? 32 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(tab, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_border_width(tab, 1, 0);
    lv_obj_set_style_border_color(
        tab, lv_color_hex(i == g_living_series ? C_ACCENT : C_LINE), 0);
    lv_obj_set_style_radius(tab, 6, 0);
    lv_obj_set_style_pad_all(tab, 0, 0);
    obj_set_scrollable(tab, false);
    obj_set_clickable(tab, true);
    lv_obj_add_event_cb(tab, living_series_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)i);
    s_living_tab[i] = label_make(tab, names[i], 0, 0, &lv_font_montserrat_16,
                                 i == g_living_series ? C_INK : C_DIM);
    lv_obj_set_width(s_living_tab[i], 140);
    lv_obj_set_style_text_align(s_living_tab[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_living_tab[i], 0, 6);
  }
}

static void ac_series_set(int series) {
  if (series < 0 || series > 1) return;
  g_ac_series = series;
  if (s_ac_hl) lv_obj_set_x(s_ac_hl, series * (s_ac_w / 2));
  for (int i = 0; i < 2; i++) {
    if (!s_ac_tab[i]) continue;
    if (!s_ac_hl) {
      lv_obj_t* tab = lv_obj_get_parent(s_ac_tab[i]);
      if (tab) {
        lv_obj_set_style_bg_opa(tab, i == series ? 32 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(
            tab, lv_color_hex(i == series ? C_ACCENT : C_LINE), 0);
      }
    }
    lv_obj_set_style_text_color(s_ac_tab[i],
                                lv_color_hex(i == series ? C_INK : C_DIM), 0);
  }
  s_chart_force = true;
  chart_rebuild();
  detail_value_update();
}

static void ac_series_cb(lv_event_t* e) {
  ac_series_set((int)(intptr_t)lv_event_get_user_data(e));
}

/* Same segmented-control look as the range selector (100 x 40 halves). */
static void ac_tabs_build_compact(int top, int left, int width) {
  static const char* names[2] = {"RMS", "Angle"};
  s_ac_w = width;
  lv_obj_t* seg = lv_obj_create(lv_screen_active());
  lv_obj_set_size(seg, width, 40);
  lv_obj_set_pos(seg, left, top);
  lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(seg, 1, 0);
  lv_obj_set_style_border_color(seg, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_radius(seg, 8, 0);
  lv_obj_set_style_pad_all(seg, 0, 0);
  obj_set_scrollable(seg, false);

  lv_obj_t* hl = lv_obj_create(seg);
  s_ac_hl = hl;
  lv_obj_set_size(hl, width / 2, 40);
  lv_obj_set_pos(hl, g_ac_series * (width / 2), 0);
  lv_obj_set_style_bg_color(hl, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_bg_opa(hl, 32, 0);
  lv_obj_set_style_border_width(hl, 0, 0);
  lv_obj_set_style_radius(hl, 0, 0);
  obj_set_clickable(hl, false);

  for (int i = 0; i < 2; i++) {
    lv_obj_t* tab = lv_obj_create(seg);
    lv_obj_set_size(tab, width / 2, 40);
    lv_obj_set_pos(tab, i * (width / 2), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tab, 0, 0);
    lv_obj_set_style_pad_all(tab, 0, 0);
    obj_set_scrollable(tab, false);
    obj_set_clickable(tab, true);
    lv_obj_add_event_cb(tab, ac_series_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)i);
    s_ac_tab[i] = label_make(tab, names[i], 0, 0, &lv_font_montserrat_16,
                             i == g_ac_series ? C_INK : C_DIM);
    lv_obj_set_width(s_ac_tab[i], width / 2);
    lv_obj_set_style_text_align(s_ac_tab[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_ac_tab[i], 0, 10);
  }
}

/* ─── Log screen (S15) ───────────────────────────────────────────────────── */

static char log_level_of(const char* s, size_t n) {
  for (size_t i = 0; i + 2 < n; i++) {
    if (s[i] == '[' && s[i + 2] == ']') {
      const char lv = s[i + 1];
      if (lv == 'E' || lv == 'W' || lv == 'I' || lv == 'D') return lv;
    }
    if (s[i] == ' ' && s[i + 2] == ' ') {
      const char lv = s[i + 1];
      if (lv == 'E' || lv == 'W' || lv == 'I' || lv == 'D') return lv;
    }
    if (s[i] == '!' && i + 1 < n && s[i + 1] == '!') return 'R';
  }
  return 'I';
}

#if LVGL_VERSION_MAJOR >= 10
static uint32_t log_color(char lv) {
  switch (lv) {
    case 'E':
      return 0xff5a5a;
    case 'W':
      return 0xffb347;
    case 'D':
      return 0x6a6a6a;
    case 'R':
      return 0xff5a5a;
    default:
      return 0xc8c8c8;
  }
}
#endif

static uint8_t log_level_bit(char lv) {
  switch (lv) {
    case 'E':
      return 0x01;
    case 'W':
      return 0x02;
    case 'I':
      return 0x04;
    default:
      return 0x08;
  }
}

static void log_restyle_levels(void) {
  for (int i = 0; i < 4; i++) {
    if (!s_log_level[i]) continue;
    const bool on = (s_log_level_mask & (1u << i)) != 0;
    lv_obj_set_style_bg_opa(s_log_level[i], on ? 40 : 0, 0);
    lv_obj_set_style_text_color(s_log_level[i],
                                lv_color_hex(on ? C_INK : C_DIM), 0);
  }
}

static void log_rebuild(void) {
  if (!s_log_label) return;
  int len = 0;
  const char* dump = gui_log_dump(&len);
  if (!dump || len <= 0) {
    txt(s_log_label, "(log empty)");
    txt(s_log_status, "0 B | 0% | dropped 0 | up --");
    return;
  }

  /* Keep the scan bounded: the oldest part of a full 1 MB ring cannot be on
   * screen anyway. */
  size_t begin = 0;
  if (len > 96 * 1024) {
    begin = (size_t)len - 96 * 1024;
    while (begin < (size_t)len && dump[begin] != '\n') begin++;
    if (begin < (size_t)len) begin++;
  }

  static const char** vp = NULL;
  static uint16_t* vl = NULL;
  static char* vlev = NULL;
  if (!vp) {
    vp = (const char**)haus_mem_alloc(sizeof(*vp) * LOG_LINES_MAX);
    vl = (uint16_t*)haus_mem_alloc(sizeof(*vl) * LOG_LINES_MAX);
    vlev = (char*)haus_mem_alloc(sizeof(*vlev) * LOG_LINES_MAX);
    s_log_recolor = (char*)haus_mem_alloc(LOG_RECOLOR_BYTES);
    if (!vp || !vl || !vlev || !s_log_recolor) return;
  }
  int count = 0;

  size_t i = begin;
  while (i < (size_t)len) {
    size_t e = i;
    while (e < (size_t)len && dump[e] != '\n') e++;
    const char* line = dump + i;
    size_t llen = e - i;
    if (llen && line[llen - 1] == '\r') llen--;
    if (llen > 0) {
      const char lv = log_level_of(line, llen);
      if (s_log_level_mask & log_level_bit(lv)) {
        size_t off = 0;
        while (off < llen) {
          size_t take = llen - off;
          if (take > 63) take = 63;
          if (count == LOG_LINES_MAX) {
            memmove(vp, vp + 1, sizeof(vp[0]) * (LOG_LINES_MAX - 1));
            memmove(vl, vl + 1, sizeof(vl[0]) * (LOG_LINES_MAX - 1));
            memmove(vlev, vlev + 1, LOG_LINES_MAX - 1);
            count--;
          }
          vp[count] = line + off;
          vl[count] = (uint16_t)take;
          vlev[count] = lv;
          count++;
          off += take;
        }
      }
    }
    i = e + 1;
  }

  if (count == 0) {
    txt(s_log_label, "(no records at this level)");
  } else {
    const int visible = 26;
    int start;
    if (s_log_follow_on) {
      start = count > visible ? count - visible : 0;
    } else {
      start = count - visible - s_log_scroll;
      if (start < 0) start = 0;
      if (start > count - visible && count > visible) start = count - visible;
    }
    size_t o = 0;
    for (int k = start; k < count && k < start + visible; k++) {
#if LVGL_VERSION_MAJOR >= 10
      const uint32_t col = log_color(vlev[k]);
      int n = snprintf(s_log_recolor + o, LOG_RECOLOR_BYTES - o, "#%06x ",
                       (unsigned)col);
      if (n < 0) break;
      o += (size_t)n;
      if (o + vl[k] + 4 >= LOG_RECOLOR_BYTES) break;
      memcpy(s_log_recolor + o, vp[k], vl[k]);
      o += vl[k];
      s_log_recolor[o++] = '#';
      s_log_recolor[o++] = '\n';
#else
      if (o + vl[k] + 2 >= LOG_RECOLOR_BYTES) break;
      memcpy(s_log_recolor + o, vp[k], vl[k]);
      o += vl[k];
      s_log_recolor[o++] = '\n';
#endif
    }
    s_log_recolor[o] = '\0';
    txt(s_log_label, s_log_recolor);
  }

  const unsigned pct = (unsigned)((uint64_t)len * 100u / (1024u * 1024u));
  HausState st;
  gui_get_state(&st);
  char status[96];
  snprintf(status, sizeof(status), "%dk %u%% d%u up%luh", len / 1024, pct,
           (unsigned)gui_log_dropped(),
           (unsigned long)(st.can.uptime_seconds / 3600u));
  txt(s_log_status, status);

  /* Compact storage field (S23.13): "SD 1.0G/29G ok". */
  if (s_log_sd) {
    const uint64_t total = st.can.sd_total_bytes;
    const uint64_t used = st.can.sd_used_bytes;
    const bool present = strcmp(st.can.sd_card_type, "ABSENT") != 0;
    char sd[40];
    if (!present) {
      snprintf(sd, sizeof(sd), "SD --");
    } else {
      const char* state = st.can.sd_writing
                              ? "wr"
                              : (st.can.sd_fault_since_epoch ? "flt" : "ok");
      snprintf(sd, sizeof(sd), "SD %.1fG/%.0fG %s", (double)used / 1e9,
               (double)total / 1e9, state);
    }
    txt(s_log_sd, sd);
  }
  if (s_log_ws) {
    lv_obj_set_style_text_color(
        s_log_ws,
        lv_color_hex(st.can.wifi_status == GUI_STATUS_OK ? C_OK : C_OFF), 0);
  }
}

static void log_level_cb(lv_event_t* e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  s_log_level_mask ^= (uint8_t)(1u << i);
  log_restyle_levels();
  log_rebuild();
}

static void log_follow_cb(lv_event_t* e) {
  (void)e;
  s_log_follow_on = true;
  s_log_scroll = 0;
  if (s_log_follow_lbl) {
    txt(s_log_follow_lbl, "FOLLOW");
    lv_obj_set_style_text_color(s_log_follow_lbl, lv_color_hex(C_INK), 0);
  }
  log_rebuild();
}

static void log_scroll_cb(lv_event_t* e) {
  (void)e;
  if (!s_log_follow_on) return;
  s_log_follow_on = false;
  if (s_log_follow_lbl) {
    txt(s_log_follow_lbl, "FOLLOW");
    lv_obj_set_style_text_color(s_log_follow_lbl, lv_color_hex(C_AMBER), 0);
  }
  log_rebuild();
}

static void log_invert_cb(lv_event_t* e) {
  (void)e;
  s_log_invert = !s_log_invert;
  lv_obj_set_style_bg_color(lv_screen_active(),
                            lv_color_hex(s_log_invert ? 0xf3ece4 : C_BG), 0);
  lv_obj_set_style_text_color(
      s_log_label, lv_color_hex(s_log_invert ? 0x1a1412 : 0xc8c8c8), 0);
  log_rebuild();
}

static void log_modal_close(void) {
  if (s_log_modal) {
    lv_obj_delete(s_log_modal);
    s_log_modal = NULL;
  }
}

static void log_clear_yes_cb(lv_event_t* e) {
  (void)e;
  gui_log_clear();
  log_modal_close();
  s_log_follow_on = true;
  s_log_scroll = 0;
  log_rebuild();
}

static void log_clear_no_cb(lv_event_t* e) {
  (void)e;
  log_modal_close();
}

static void log_clear_cb(lv_event_t* e) {
  (void)e;
  if (s_log_modal) return;
  s_log_modal = lv_obj_create(lv_screen_active());
  lv_obj_set_size(s_log_modal, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(s_log_modal, 0, 0);
  lv_obj_set_style_bg_color(s_log_modal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_log_modal, 160, 0);
  lv_obj_set_style_border_width(s_log_modal, 0, 0);
  obj_set_scrollable(s_log_modal, false);

  lv_obj_t* panel = lv_obj_create(s_log_modal);
  lv_obj_set_size(panel, 380, 190);
  lv_obj_center(panel);
  lv_obj_set_style_bg_color(panel, lv_color_hex(C_PANEL), 0);
  lv_obj_set_style_radius(panel, 12, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_t* q = label_make(panel, "Clear the log ring?", 20, 24,
                           &lv_font_montserrat_24, C_INK);
  (void)q;
  label_make(panel, "This destroys the evidence.", 20, 64,
             &lv_font_montserrat_16, C_DIM);

  lv_obj_t* yes = lv_obj_create(panel);
  lv_obj_set_size(yes, 150, 52);
  lv_obj_set_pos(yes, 30, 112);
  lv_obj_set_style_bg_color(yes, lv_color_hex(C_HOT), 0);
  lv_obj_set_style_bg_opa(yes, 200, 0);
  lv_obj_set_style_border_width(yes, 0, 0);
  lv_obj_set_style_radius(yes, 8, 0);
  obj_set_clickable(yes, true);
  lv_obj_add_event_cb(yes, log_clear_yes_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* yl =
      label_make(yes, "Confirm", 0, 0, &lv_font_montserrat_16, C_INK);
  lv_obj_center(yl);

  lv_obj_t* no = lv_obj_create(panel);
  lv_obj_set_size(no, 150, 52);
  lv_obj_set_pos(no, 200, 112);
  lv_obj_set_style_bg_opa(no, 0, 0);
  lv_obj_set_style_border_width(no, 1, 0);
  lv_obj_set_style_border_color(no, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_radius(no, 8, 0);
  obj_set_clickable(no, true);
  lv_obj_add_event_cb(no, log_clear_no_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* nl = label_make(no, "Cancel", 0, 0, &lv_font_montserrat_16, C_DIM);
  lv_obj_center(nl);
}

static void log_save_cb(lv_event_t* e) {
  (void)e;
  const int ok = gui_log_save();
  txt(s_log_status, ok ? "saved to SD" : "save failed");
}

static void log_back_cb(lv_event_t* e) {
  (void)e;
  ui_pop();
}

static lv_obj_t* log_button(lv_obj_t* parent, int x, int y, int w, int h,
                            const char* text, lv_event_cb_t cb, int size) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_bg_opa(b, 0, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  obj_set_scrollable(b, false);
  obj_set_clickable(b, true);
  (void)cb;
  lv_obj_t* l = label_make(
      b, text, 0, 0,
      size == 24 ? &lv_font_montserrat_24 : &lv_font_montserrat_16, C_INK);
  lv_obj_center(l);
  return b;
}

lv_obj_t* haus_ui_build_log(void) {
  screen_lock_scroll();
  lv_obj_t* root = lv_screen_active();
  lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);
  g_current_screen = UI_SCREEN_LOG;

  lv_obj_t* bar = lv_obj_create(root);
  lv_obj_set_size(bar, SCREEN_W, 44);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x0c0b0a), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  obj_set_scrollable(bar, false);
  hline(bar, 43, C_LINE);

  lv_obj_t* back = lv_obj_create(bar);
  lv_obj_set_size(back, 44, 44);
  lv_obj_set_pos(back, 0, 0);
  lv_obj_set_style_bg_opa(back, 0, 0);
  lv_obj_set_style_border_width(back, 0, 0);
  obj_set_clickable(back, true);
  lv_obj_add_event_cb(back, log_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* bl =
      label_make(back, LV_SYMBOL_LEFT, 0, 0, &lv_font_montserrat_24, C_INK);
  lv_obj_center(bl);

  label_make(bar, "LOG", 50, 10, &lv_font_montserrat_24, C_INK);

  static const char* names[4] = {"E", "W", "I", "D"};
  for (int i = 0; i < 4; i++) {
    s_log_level[i] =
        log_button(bar, 116 + i * 38, 6, 32, 32, names[i], log_level_cb, 16);
    lv_obj_add_event_cb(s_log_level[i], log_level_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)i);
  }
  log_restyle_levels();

  lv_obj_t* inv = log_button(bar, 590, 6, 50, 32, "INV", log_invert_cb, 16);
  lv_obj_add_event_cb(inv, log_invert_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* clr = log_button(bar, 648, 6, 50, 32, "CLR", log_clear_cb, 16);
  lv_obj_add_event_cb(clr, log_clear_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* save = log_button(bar, 706, 6, 50, 32, "SAVE", log_save_cb, 16);
  lv_obj_add_event_cb(save, log_save_cb, LV_EVENT_CLICKED, NULL);

  s_log_container = lv_obj_create(root);
  lv_obj_set_size(s_log_container, SCREEN_W, SCREEN_H - 84);
  lv_obj_set_pos(s_log_container, 0, 44);
  lv_obj_set_style_bg_opa(s_log_container, 0, 0);
  lv_obj_set_style_border_width(s_log_container, 0, 0);
  lv_obj_set_style_pad_all(s_log_container, 0, 0);
  lv_obj_set_scroll_dir(s_log_container, LV_DIR_ALL);
  lv_obj_add_event_cb(s_log_container, log_scroll_cb, LV_EVENT_SCROLL, NULL);

  s_log_label = label_make(s_log_container, "", 4, 2, &haus_mono_21, 0xc8c8c8);
  lv_obj_set_width(s_log_label, SCREEN_W - 8);
  lv_label_set_long_mode(s_log_label, UI_LABEL_LONG_WRAP);
  ui_label_set_recolor(s_log_label, true);

  lv_obj_t* statusbar = lv_obj_create(root);
  lv_obj_set_size(statusbar, SCREEN_W, 40);
  lv_obj_set_pos(statusbar, 0, SCREEN_H - 40);
  lv_obj_set_style_bg_color(statusbar, lv_color_hex(0x0c0b0a), 0);
  lv_obj_set_style_border_width(statusbar, 0, 0);
  lv_obj_set_style_radius(statusbar, 0, 0);
  lv_obj_set_style_pad_all(statusbar, 0, 0);
  hline(statusbar, 0, C_LINE);
  s_log_status =
      label_make(statusbar, "", 10, 10, &lv_font_montserrat_16, C_DIM);
  s_log_sd =
      label_make(statusbar, "SD --", 420, 10, &lv_font_montserrat_16, C_DIM);
  s_log_ws =
      label_make(statusbar, "WS", 600, 10, &lv_font_montserrat_16, C_OFF);

  s_log_follow =
      log_button(statusbar, 700, 4, 82, 32, "FOLLOW", log_follow_cb, 16);
  lv_obj_add_event_cb(s_log_follow, log_follow_cb, LV_EVENT_CLICKED, NULL);
  s_log_follow_lbl = lv_obj_get_child(s_log_follow, 0);
  if (s_log_follow_lbl)
    lv_obj_set_style_text_color(
        s_log_follow_lbl, lv_color_hex(s_log_follow_on ? C_INK : C_AMBER), 0);

  s_log_last_build = lv_tick_get();
  log_rebuild();
  return root;
}

static void log_screen_update(void) {
  if (!s_log_label) return;
  if ((uint32_t)(lv_tick_get() - s_log_last_build) < 200u) return;
  s_log_last_build = lv_tick_get();
  log_rebuild();
}

static void open_log_cb(lv_event_t* e) {
  (void)e;
  ui_push(UI_SCREEN_LOG);
}

static void build_status_hotspot(void) {
  lv_obj_t* z = lv_obj_create(lv_screen_active());
  lv_obj_set_size(z, SCREEN_W, 32);
  lv_obj_set_pos(z, 0, 0);
  lv_obj_set_style_bg_opa(z, 0, 0);
  lv_obj_set_style_border_width(z, 0, 0);
  lv_obj_set_style_radius(z, 0, 0);
  obj_set_scrollable(z, false);
  obj_set_clickable(z, true);
  lv_obj_add_event_cb(z, open_log_cb, LV_EVENT_LONG_PRESSED, NULL);
}


static void side_panel_build(void) {
  lv_obj_t* root = lv_screen_active();
  s_side_h = label_make(root, "", 474, 56, &haus_body_21, C_DIM);
  hline_w(root, 474, 84, 228, C_LINE);
  s_side_big = label_make(root, "", 474, 94, &haus_value_32, C_INK);
  for (int i = 0; i < 5; i++) {
    s_side_l[i] = label_make(root, "", 474, 140 + i * 28, &haus_body_21, C_DIM);
  }
  s_side_big2_lbl = label_make(root, "", 474, 160, &haus_body_21, C_DIM);
  s_side_hr2 = hline_w(root, 474, 188, 228, C_LINE);
  obj_set_hidden(s_side_hr2, true);
  s_side_big2 = label_make(root, "", 474, 198, &haus_value_32, C_HOT);
  s_side_delta_lbl = label_make(root, "", 474, 250, &haus_body_21, C_DIM);
  s_side_hr3 = hline_w(root, 474, 278, 228, C_LINE);
  obj_set_hidden(s_side_hr3, true);
  s_side_delta = label_make(root, "", 474, 288, &haus_value_32, C_INK);
  s_side_note =
      label_make(root, "", 474, 330, &lv_font_montserrat_16, C_FAINT);
  lv_obj_set_width(s_side_note, 228);
  lv_label_set_long_mode(s_side_note, UI_LABEL_LONG_WRAP);
  for (int i = 0; i < 3; i++) {
    s_side_legend[i] =
        label_make(root, "", 474, 352 + i * 22, &lv_font_montserrat_16, C_DIM);
    obj_set_hidden(s_side_legend[i], true);
  }
}

static lv_obj_t* detail_build(int screen, const char* title, int nav_index,
                              const char* badge, uint32_t badge_color) {
  (void)nav_index;
  screen_lock_scroll();
  lv_obj_t* root = lv_screen_active();
  lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);
  g_detail_screen = screen;
  g_current_screen = screen;

  header_build(title, badge, badge_color);

  const bool water =
      (screen == UI_SCREEN_WATER_IN || screen == UI_SCREEN_WATER_OUT);
  s_detail_value = water
                       ? NULL
                       : label_make(root, "--", 20, 50, &haus_large_52, C_INK);

  if (screen == UI_SCREEN_LIVING) {
    living_tabs_build(108);
  } else if (screen == UI_SCREEN_AC) {
    range_build(108, 300);
    ac_tabs_build_compact(108, 338, 200);
  } else {
    range_build(water ? 56 : 108, 440);
  }

  if (screen == UI_SCREEN_CHAMBER) {
    s_above_target = label_make(root, "above target --", 20, 152,
                                &lv_font_montserrat_16, C_ACCENT);
  } else if (screen == UI_SCREEN_POWER) {
    label_make(root, "power (SML)", 20, 152, &lv_font_montserrat_16, C_DIM);
  }

  s_axis_key = -1;
  chart_build_widgets(water ? 116 : 168);

  if (water || screen == UI_SCREEN_POWER || screen == UI_SCREEN_LIVING) {
    side_panel_build();
  }

  s_chart_readout = label_make(root, "min --   max --   mean --   now --", 20,
                               water ? 380 : 432, &lv_font_montserrat_16,
                               C_DIM);
  s_chart_readout_b = NULL;
  s_chart_age = label_make(root, "data age --", 20, water ? 406 : 458,
                           &lv_font_montserrat_16, C_FAINT);

  if (screen == UI_SCREEN_CHAMBER) pwm_control_build(0);

  nav_build(screen);
  s_chart_force = true;
  detail_update();
  return root;
}
void haus_ui_three_finger(void) { ui_jump(UI_SCREEN_HOME); }

#ifdef UI_SMOKE_TEST
void haus_ui_smoke_step(void) {
  static const int seq[] = {UI_SCREEN_HOME,     UI_SCREEN_CHAMBER,
                            UI_SCREEN_WATER_IN, UI_SCREEN_WATER_OUT,
                            UI_SCREEN_POWER,    UI_SCREEN_LIVING,
                            UI_SCREEN_SETTINGS, UI_SCREEN_LOG};
  static unsigned i = 0;
  ui_jump(seq[i++ % (sizeof(seq) / sizeof(seq[0]))]);
}
#endif

void haus_ui_set_range(int range) {
  if (range < 0 || range > 2) return;
  if (s_range_hl || s_chart_canvas) {
    range_set(range);
  } else {
    g_range = range;
  }
}

lv_obj_t* haus_ui_build_detail(void) {
  return detail_build(UI_SCREEN_CHAMBER, "HEATER", UI_SCREEN_CHAMBER,
                      "RESTORED", C_DIM);
}

lv_obj_t* haus_ui_build_water(int nav_index) {
  return detail_build(UI_SCREEN_WATER_OUT, "WATER", nav_index, "Soll",
                      C_ACCENT);
}

lv_obj_t* haus_ui_build_power(void) {
  return detail_build(UI_SCREEN_POWER, "POWER", UI_SCREEN_POWER, "SML", C_OK);
}

lv_obj_t* haus_ui_build_living(void) {
  return detail_build(UI_SCREEN_LIVING, "LIVING ROOM", UI_SCREEN_LIVING, "I2C",
                      C_OK);
}

lv_obj_t* haus_ui_build_ac(void) {
  return detail_build(UI_SCREEN_AC, "PUMP CURRENT", UI_SCREEN_AC, "AD", C_AMBER);
}

void haus_ui_set_ac_series(int series) { ac_series_set(series); }

static lv_obj_t* s_can_switch_lbl;
static lv_obj_t* s_can_switch;
static lv_obj_t* s_can_fault_lbl;
static lv_obj_t* s_can_modal;
static bool s_can_pending;
static uint32_t s_can_apply_ms;

static void settings_can_visual(bool on) {
  if (s_can_switch_lbl) txt(s_can_switch_lbl, on ? "ON" : "OFF");
  if (s_can_switch) {
    lv_obj_set_style_bg_color(s_can_switch, lv_color_hex(on ? C_OK : C_OFF), 0);
    lv_obj_set_style_bg_opa(s_can_switch, on ? 220 : 80, 0);
  }
}

/* S23.02: show what the CH422G actually reports, not what was requested. A
 * request that does not match the mux after the apply window is a fault. */
static void settings_can_refresh(void) {
  if (!s_can_switch) return;

  bool known = false;
  const bool actual = gui_can_actual(&known);
  const bool requested = gui_can_enabled();
  settings_can_visual(known ? actual : requested);

  bool mismatch = known && (actual != requested);
  if (mismatch && s_can_apply_ms != 0 &&
      (can_ui_now_ms() - s_can_apply_ms) < 2500u) {
    mismatch = false; /* the apply task is still switching */
  }
  if (!mismatch) s_can_apply_ms = 0;
  if (s_can_fault_lbl) txt(s_can_fault_lbl, mismatch ? "MUX FAULT" : "");
}

static void can_modal_close(void) {
  if (s_can_modal) {
    lv_obj_delete(s_can_modal);
    s_can_modal = NULL;
  }
}

static void can_modal_yes_cb(lv_event_t* e) {
  (void)e;
  const bool on = s_can_pending;
  can_modal_close();
  haus_config_set_can_enabled(on);
  gui_set_can_enabled(on);
  s_can_apply_ms = can_ui_now_ms();
  settings_can_refresh();
}

static void can_modal_no_cb(lv_event_t* e) {
  (void)e;
  can_modal_close();
  settings_can_refresh();
}

/* S23.04: the only row that changes the USB mux asks first, and the question
 * names the consequence (§8.14). */
static void settings_can_cb(lv_event_t* e) {
  (void)e;
  if (s_can_modal) return;

  s_can_pending = !gui_can_enabled();
  const bool on = s_can_pending;

  s_can_modal = lv_obj_create(lv_screen_active());
  lv_obj_set_size(s_can_modal, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(s_can_modal, 0, 0);
  lv_obj_set_style_bg_color(s_can_modal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_can_modal, 160, 0);
  lv_obj_set_style_border_width(s_can_modal, 0, 0);
  obj_set_scrollable(s_can_modal, false);

  lv_obj_t* panel = lv_obj_create(s_can_modal);
  lv_obj_set_size(panel, 440, 300);
  lv_obj_center(panel);
  lv_obj_set_style_bg_color(panel, lv_color_hex(C_PANEL), 0);
  lv_obj_set_style_radius(panel, 12, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_border_width(panel, 1, 0);

  label_make(panel, on ? "Turn CAN on?" : "Turn CAN off?", 20, 20,
             &lv_font_montserrat_24, C_INK);
  lv_obj_t* body = label_make(
      panel,
      on ? "USB disappears - no serial, no flashing. If the firmware then "
           "fails, recovery needs a power cycle."
         : "USB returns; there will be no CAN data, no history and no monitor "
           "until you turn it back on.",
      20, 70, &lv_font_montserrat_16, C_DIM);
  lv_obj_set_width(body, 400);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);

  lv_obj_t* yes = lv_obj_create(panel);
  lv_obj_set_size(yes, 150, 52);
  lv_obj_set_pos(yes, 50, 210);
  lv_obj_set_style_bg_color(yes, lv_color_hex(on ? C_OK : C_HOT), 0);
  lv_obj_set_style_bg_opa(yes, 200, 0);
  lv_obj_set_style_border_width(yes, 0, 0);
  lv_obj_set_style_radius(yes, 8, 0);
  obj_set_clickable(yes, true);
  lv_obj_add_event_cb(yes, can_modal_yes_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label_make(yes, on ? "Turn on" : "Turn off", 0, 0,
                           &lv_font_montserrat_16, C_INK));

  lv_obj_t* no = lv_obj_create(panel);
  lv_obj_set_size(no, 150, 52);
  lv_obj_set_pos(no, 240, 210);
  lv_obj_set_style_bg_opa(no, 0, 0);
  lv_obj_set_style_border_width(no, 1, 0);
  lv_obj_set_style_border_color(no, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_radius(no, 8, 0);
  obj_set_clickable(no, true);
  lv_obj_add_event_cb(no, can_modal_no_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label_make(no, "Cancel", 0, 0, &lv_font_montserrat_16, C_DIM));
}

/* ─── Sensor roles screen (S23.06) ───────────────────────────────────────
 * A full 480x800 screen, not a dialog: the rows span the portrait width and
 * the SETTINGS tab in the bottom nav is the way back. */

#define ROLES_MAX 8

static lv_obj_t* s_roles_lbl[ROLES_MAX];

/* The installation has three probes: the heater board, the water-in pipe and
 * the water-out pipe. CHAMBER is kept in the enum for old persisted files and
 * for the chamber zone, which aliases BOARD. */
static const uint8_t k_role_cycle[] = {
    SENSOR_FN_BOARD,
    SENSOR_FN_WATER_IN,
    SENSOR_FN_WATER_OUT,
    SENSOR_FN_NONE,
};
#define ROLE_CYCLE_LEN (sizeof(k_role_cycle) / sizeof(k_role_cycle[0]))

static const char* role_fn_name(uint8_t fn) {
  switch (fn) {
    case SENSOR_FN_CHAMBER:
      return "HEATER";
    case SENSOR_FN_WATER_IN:
      return "WATER IN (PIPE)";
    case SENSOR_FN_WATER_OUT:
      return "WATER OUT (PIPE)";
    case SENSOR_FN_BOARD:
      return "BOARD";
    default:
      return "NONE";
  }
}

static uint8_t roles_bound_count(void) {
  uint8_t bound = 0;
  const uint8_t count = gui_roles_count();
  for (uint8_t i = 0; i < count; i++) {
    uint8_t rom[8];
    uint8_t fn = SENSOR_FN_NONE;
    if (gui_roles_get(i, rom, &fn) && fn != SENSOR_FN_NONE) bound++;
  }
  return bound;
}

static void roles_badge_refresh(void) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%u bound", (unsigned)roles_bound_count());
  header_badge_set(buf, C_DIM);
}

static void roles_row_cb(lv_event_t* e) {
  const uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  uint8_t rom[8];
  uint8_t fn = SENSOR_FN_NONE;
  if (!gui_roles_get(index, rom, &fn)) return;
  uint8_t pos = 0;
  for (uint8_t i = 0; i < ROLE_CYCLE_LEN; i++) {
    if (k_role_cycle[i] == fn) {
      pos = i;
      break;
    }
  }
  const uint8_t next = k_role_cycle[(pos + 1u) % ROLE_CYCLE_LEN];
  gui_roles_set(index, next);
  if (index < ROLES_MAX && s_roles_lbl[index]) {
    if (!gui_roles_get(index, rom, &fn)) fn = next;
    txt(s_roles_lbl[index], role_fn_name(fn));
    lv_obj_set_style_text_color(
        s_roles_lbl[index],
        lv_color_hex(fn == SENSOR_FN_NONE ? C_FAINT : C_INK), 0);
    lv_obj_invalidate(s_roles_lbl[index]);
  }
  roles_badge_refresh();
  gui_request_redraw();
}

lv_obj_t* haus_ui_build_roles(void) {
  screen_lock_scroll();
  lv_obj_t* root = lv_screen_active();
  lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);
  g_current_screen = UI_SCREEN_ROLES;
  memset(s_roles_lbl, 0, sizeof(s_roles_lbl));

  const uint8_t count = gui_roles_count();
  char badge[20];
  snprintf(badge, sizeof(badge), "%u bound", (unsigned)roles_bound_count());
  header_build("SENSOR ROLES", badge, C_DIM);
  label_make(root, "tap a probe to cycle its function", 20, 64,
             &lv_font_montserrat_16, C_DIM);

  if (count == 0) {
    label_make(root, "no probes discovered", 20, 116, &haus_body_21, C_AMBER);
    label_make(root, "enable CAN and bind a probe first", 20, 148,
               &haus_body_21, C_DIM);
  }

  label_make(root, "rank = position in the CAN 0x200 message", 20, 86,
             &lv_font_montserrat_16, C_FAINT);

  const int y0 = 116, step = 84, h = 76;
  for (uint8_t i = 0; i < count && i < ROLES_MAX; i++) {
    uint8_t rom[8];
    uint8_t fn = SENSOR_FN_NONE;
    if (!gui_roles_get(i, rom, &fn)) continue;

    lv_obj_t* row = lv_obj_create(root);
    lv_obj_set_size(row, CONTENT_W - 40, h);
    lv_obj_set_pos(row, 20, y0 + i * step);
    lv_obj_set_style_bg_opa(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    obj_set_scrollable(row, false);
    obj_set_clickable(row, true);
    lv_obj_add_event_cb(row, roles_row_cb, LV_EVENT_CLICKED,
                        (void*)(uintptr_t)i);

    char rank[16];
    snprintf(rank, sizeof(rank), "Rank %u:", (unsigned)i);
    label_make(row, rank, 14, 8, &haus_body_21, C_DIM);

    char id[40];
    snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X%02X%02X", rom[0], rom[1],
             rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
    label_make(row, id, 14, 42, &haus_mono_21, C_DIM);
    s_roles_lbl[i] =
        label_make(row, role_fn_name(fn), 480, 44, &lv_font_montserrat_16,
                   fn == SENSOR_FN_NONE ? C_FAINT : C_INK);
  }

  label_make(root, "roles persist across reboots", 20, 404,
             &lv_font_montserrat_16, C_FAINT);
  nav_build(UI_SCREEN_SETTINGS);
  return root;
}

static void settings_open_roles_cb(lv_event_t* e) {
  (void)e;
  ui_push(UI_SCREEN_ROLES);
}

static void settings_open_can_cb(lv_event_t* e) {
  (void)e;
  ui_push(UI_SCREEN_CAN);
}

static void settings_open_ac_cb(lv_event_t* e) {
  (void)e;
  ui_push(UI_SCREEN_AC);
}

static void settings_open_log_cb(lv_event_t* e) {
  (void)e;
  ui_push(UI_SCREEN_LOG);
}

/* A compact settings row: title on the left, current value on the right.
 * FONTS: the 21 px body floor (§8.1.1). */
static lv_obj_t* settings_value_row(int x, int y, int w, const char* title,
                                    const char* value, lv_event_cb_t cb) {
  lv_obj_t* b = lv_obj_create(lv_screen_active());
  lv_obj_set_size(b, w, 40);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_bg_opa(b, 0, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  obj_set_scrollable(b, false);
  obj_set_clickable(b, true);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  label_make(b, title, 0, 9, &haus_body_21, C_INK);
  lv_obj_t* v =
      label_make(b, value, 0, 9, &haus_body_21, cb ? C_ACCENT : C_DIM);
  /* Actionable rows carry a chevron; static values do not. */
  lv_obj_align(v, LV_ALIGN_TOP_RIGHT, cb ? -26 : 0, 9);
  if (cb) label_make(b, LV_SYMBOL_RIGHT, w - 16, 7, &lv_font_montserrat_16, C_DIM);
  return v;
}

/* ─── Storage helpers (S23) ─────────────────────────────────────────────── */

static void fmt_bytes(uint64_t b, char* out, size_t n) {
  if (b >= 1000000000ULL)
    snprintf(out, n, "%.1f GB", (double)b / 1e9);
  else if (b >= 1000000ULL)
    snprintf(out, n, "%.0f MB", (double)b / 1e6);
  else if (b >= 1000ULL)
    snprintf(out, n, "%.0f kB", (double)b / 1e3);
  else
    snprintf(out, n, "%llu B", (unsigned long long)b);
}

static void epoch_to_datetime(uint32_t epoch, char* out, size_t n) {
  if (epoch < 1700000000u) {
    snprintf(out, n, "---");
    return;
  }
  time_t t = (time_t)epoch;
  struct tm tm;
  if (!localtime_r(&t, &tm)) {
    snprintf(out, n, "---");
    return;
  }
  strftime(out, n, "%d %b %H:%M", &tm);
}

static lv_obj_t* s_backlight_lbl;
static lv_obj_t* s_power_unit_lbl;
static lv_obj_t* s_diag_lbl;
static lv_obj_t* s_panel_lbl;
static int s_settings_power_unit = -1; /* -1 auto, 0 W, 1 kW */

static void settings_backlight_cb(lv_event_t* e) {
  (void)e;
  const bool on = !gui_backlight_on();
  gui_set_backlight(on);
  txt(s_backlight_lbl, on ? "ON" : "OFF");
}

static void settings_power_unit_cb(lv_event_t* e) {
  (void)e;
  s_settings_power_unit++;
  if (s_settings_power_unit > 1) s_settings_power_unit = -1;
  txt(s_power_unit_lbl, s_settings_power_unit < 0
                            ? "auto"
                            : (s_settings_power_unit == 0 ? "W" : "kW"));
}

static void settings_panel_reset_cb(lv_event_t* e) {
  (void)e;
  const bool ok = gui_reset_panel();
  if (s_panel_lbl) txt(s_panel_lbl, ok ? "done" : "failed");
}

static void settings_diag_cb(lv_event_t* e) {
  (void)e;
  txt(s_diag_lbl, gui_log_save() ? "saved" : "failed");
}

static void settings_open_storage_cb(lv_event_t* e) {
  (void)e;
  gui_storage_refresh();
  ui_push(UI_SCREEN_STORAGE);
}

lv_obj_t* haus_ui_build_storage(void) {
  screen_lock_scroll();
  lv_obj_t* root = lv_screen_active();
  lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);
  g_current_screen = UI_SCREEN_STORAGE;

  gui_storage_refresh();
  HausState st;
  gui_get_state(&st);

  const bool mounted = (st.can.sd_card_type[0] != '\0' &&
                        strcmp(st.can.sd_card_type, "ABSENT") != 0);
  header_build("STORAGE", mounted ? st.can.sd_card_type : "ABSENT",
               mounted ? C_OK : C_OFF);

  const uint64_t total = st.can.sd_total_bytes;
  const uint64_t used = st.can.sd_used_bytes;
  const uint64_t free_b = total > used ? total - used : 0;
  const uint64_t floor_b = 50ULL * 1024ULL * 1024ULL; /* S11.12 */
  char a[32], b[32], c[32], line[96];

  const int LX = 20, RX = 368, CW = 324;

  /* ── SD card: the primary history store ─────────────────────────────── */
  label_make(root, "SD CARD", LX, 56, &haus_body_21, C_INK);
  label_make(root, "history", LX + CW - 62, 60, &lv_font_montserrat_16, C_DIM);
  hline_w(root, LX, 82, CW, C_LINE);

  lv_obj_t* bar = lv_obj_create(root);
  lv_obj_set_size(bar, CW, 14);
  lv_obj_set_pos(bar, LX, 88);
  lv_obj_set_style_bg_color(bar, lv_color_hex(C_PANEL), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 3, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  obj_set_clickable(bar, false);
  if (total > 0) {
    int w = (int)((uint64_t)CW * used / total);
    if (w < 2) w = 2;
    if (w > CW) w = CW;
    lv_obj_t* fill = lv_obj_create(bar);
    lv_obj_set_size(fill, w, 14);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_radius(fill, 3, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    obj_set_clickable(fill, false);
  }

  fmt_bytes(used, a, sizeof(a));
  fmt_bytes(total, b, sizeof(b));
  snprintf(line, sizeof(line), "used %s of %s", a, b);
  label_make(root, line, LX, 112, &haus_body_21, C_INK);

  fmt_bytes(free_b, a, sizeof(a));
  fmt_bytes(floor_b, b, sizeof(b));
  if (free_b >= floor_b) {
    snprintf(line, sizeof(line), "free %s   floor %s", a, b);
    label_make(root, line, LX, 140, &lv_font_montserrat_16, C_DIM);
    fmt_bytes(free_b - floor_b, c, sizeof(c));
    snprintf(line, sizeof(line), "headroom %s", c);
    label_make(root, line, LX, 162, &lv_font_montserrat_16, C_DIM);
  } else {
    snprintf(line, sizeof(line), "free %s   BELOW floor %s", a, b);
    label_make(root, line, LX, 140, &lv_font_montserrat_16, C_HOT);
  }

  fmt_bytes(st.can.sd_samples_bytes, a, sizeof(a));
  snprintf(line, sizeof(line), "minute history  %s", a);
  label_make(root, line, LX, 196, &haus_body_21, C_INK);

  fmt_bytes(st.can.sd_rollup_bytes, a, sizeof(a));
  snprintf(line, sizeof(line), "hourly history  %s", a);
  label_make(root, line, LX, 224, &haus_body_21, C_INK);

  fmt_bytes(st.can.sd_logs_bytes, a, sizeof(a));
  snprintf(line, sizeof(line), "log dumps       %s", a);
  label_make(root, line, LX, 252, &haus_body_21, C_INK);

  char when[32];
  epoch_to_datetime(st.can.sd_last_rollover_epoch, when, sizeof(when));
  snprintf(line, sizeof(line), "last rollover   %s", when);
  label_make(root, line, LX, 286, &haus_body_21, C_DIM);

  epoch_to_datetime(st.can.sd_newest_file_epoch, when, sizeof(when));
  snprintf(line, sizeof(line), "newest file     %s", when);
  label_make(root, line, LX, 314, &haus_body_21, C_DIM);

  snprintf(line, sizeof(line), "today %u / %u min",
           (unsigned)st.can.l1_count, (unsigned)HAUS_L1_SAMPLES);
  lv_obj_t* today = label_make(root, line, LX, 348,
                               &lv_font_montserrat_16, C_INK);
  lv_obj_set_width(today, CW);
  lv_label_set_long_mode(today, LV_LABEL_LONG_DOT);

  snprintf(line, sizeof(line), "hourly %u / %u",
           (unsigned)st.can.l2_count, (unsigned)HAUS_L2_COLUMNS);
  lv_obj_t* hourly = label_make(root, line, LX, 372,
                                &lv_font_montserrat_16, C_INK);
  lv_obj_set_width(hourly, CW);
  lv_label_set_long_mode(hourly, LV_LABEL_LONG_DOT);

  if (st.can.sd_fault_since_epoch) {
    epoch_to_datetime(st.can.sd_fault_since_epoch, when, sizeof(when));
    snprintf(line, sizeof(line), "FAULTY since %s", when);
    label_make(root, line, LX, 404, &haus_body_21, C_HOT);
  } else {
    label_make(root, "no storage fault", LX, 404, &haus_body_21, C_OK);
  }

  snprintf(line, sizeof(line), "restored %lu hourly",
           (unsigned long)st.can.last_restore_columns);
  lv_obj_t* restored = label_make(root, line, LX, 430,
                                  &lv_font_montserrat_16, C_FAINT);
  lv_obj_set_width(restored, CW);
  lv_label_set_long_mode(restored, LV_LABEL_LONG_DOT);

  /* ── SPIFFS: independent hourly mirror plus assets ─────────────────── */
  label_make(root, "SPIFFS", RX, 56, &haus_body_21, C_INK);
  label_make(root, "mirror + files", RX + CW - 112, 60,
             &lv_font_montserrat_16, C_DIM);
  hline_w(root, RX, 82, CW, C_LINE);

  lv_obj_t* sbar = lv_obj_create(root);
  lv_obj_set_size(sbar, CW, 14);
  lv_obj_set_pos(sbar, RX, 88);
  lv_obj_set_style_bg_color(sbar, lv_color_hex(C_PANEL), 0);
  lv_obj_set_style_border_width(sbar, 0, 0);
  lv_obj_set_style_radius(sbar, 3, 0);
  lv_obj_set_style_pad_all(sbar, 0, 0);
  obj_set_clickable(sbar, false);
  if (st.can.spiffs_total_bytes > 0) {
    int w = (int)((uint64_t)CW * st.can.spiffs_used_bytes /
                  st.can.spiffs_total_bytes);
    if (w < 2) w = 2;
    if (w > CW) w = CW;
    lv_obj_t* fill = lv_obj_create(sbar);
    lv_obj_set_size(fill, w, 14);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_radius(fill, 3, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    obj_set_clickable(fill, false);
  }

  fmt_bytes(st.can.spiffs_used_bytes, a, sizeof(a));
  fmt_bytes(st.can.spiffs_total_bytes, b, sizeof(b));
  snprintf(line, sizeof(line), "used %s of %s", a, b);
  label_make(root, line, RX, 112, &haus_body_21, C_INK);

  snprintf(line, sizeof(line), "hourly mirror %lu of %u h",
           (unsigned long)st.can.ring_hours, (unsigned)HAUS_RING_HOURS);
  label_make(root, line, RX, 146, &haus_body_21, C_INK);

  epoch_to_datetime(st.can.ring_oldest_epoch, when, sizeof(when));
  snprintf(line, sizeof(line), "ring oldest   %s", when);
  label_make(root, line, RX, 174, &haus_body_21, C_DIM);

  epoch_to_datetime(st.can.ring_newest_epoch, when, sizeof(when));
  snprintf(line, sizeof(line), "ring newest   %s", when);
  label_make(root, line, RX, 202, &haus_body_21, C_DIM);

  hline_w(root, RX, 226, CW, C_LINE);
  gui_spiffs_file_t files[GUI_SPIFFS_FILE_MAX];
  const uint8_t nf = gui_spiffs_files(files, GUI_SPIFFS_FILE_MAX);
  char fcount[24];
  snprintf(fcount, sizeof(fcount), "files %u", (unsigned)nf);
  label_make(root, fcount, RX, 234, &lv_font_montserrat_16, C_DIM);
  const int shown = nf > 6 ? 6 : nf;
  for (int i = 0; i < shown; i++) {
    lv_obj_t* name = label_make(root, files[i].name, RX, 258 + i * 22,
                                &lv_font_montserrat_16, C_DIM);
    lv_obj_set_width(name, CW - 100);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    fmt_bytes(files[i].size, a, sizeof(a));
    lv_obj_t* sz = label_make(root, a, RX + CW - 100, 258 + i * 22,
                              &lv_font_montserrat_16, C_INK);
    lv_obj_set_width(sz, 100);
    lv_obj_set_style_text_align(sz, LV_TEXT_ALIGN_RIGHT, 0);
  }
  if (shown == 0)
    label_make(root, "no files", RX, 258, &lv_font_montserrat_16, C_FAINT);

  lv_obj_t* note = label_make(
      root, "Week/month history survives an absent or failed SD card.", RX, 400,
      &lv_font_montserrat_16, C_FAINT);
  lv_obj_set_width(note, CW);
  lv_label_set_long_mode(note, UI_LABEL_LONG_WRAP);

  nav_build(UI_SCREEN_SETTINGS);
  return root;
}
/* ─── CAN scope (S16) ─────────────────────────────────────────────────────
 * 12 live rows at 44 px / 21 px, plus the last 20 ms ADC waveform at the
 * bottom. Tapping a row opens a full-screen overlay on
 * lv_layer_top() (not a separate LVGL screen). The header carries the bus
 * fault banner; there is deliberately no range selector.
 */

#define CAN_ROWS 14
#define CAN_ROW_H 36
#define CAN_ROWS_Y 76

/* Last 20 ms ADC waveform (0x2C1..0x2C5) below the scope rows. */
#define CAN_WAVE_W 684
#define CAN_WAVE_H 56
#define CAN_WAVE_Y 390

static lv_obj_t* s_can_id_lbl[CAN_ROWS];
static lv_obj_t* s_can_type_lbl[CAN_ROWS];
static lv_obj_t* s_can_text_lbl[CAN_ROWS];
static lv_obj_t* s_can_stats;
static lv_obj_t* s_can_fault_bar;
static lv_obj_t* s_can_overlay;
static uint32_t s_can_next_ms;
static lv_obj_t* s_can_wave_canvas;
static lv_obj_t* s_can_wave_info;
static uint8_t* s_can_wave_pixels;
static uint32_t s_can_wave_gen;
static bool s_can_wave_empty;

static void can_wave_px(int x, int y, uint16_t c) {
  if (!s_can_wave_pixels || x < 0 || y < 0 || x >= CAN_WAVE_W ||
      y >= CAN_WAVE_H) {
    return;
  }
  ((uint16_t*)s_can_wave_pixels)[y * CAN_WAVE_W + x] = c;
}

static void can_wave_line(int x0, int y0, int x1, int y1, uint16_t c) {
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = y1 > y0 ? y0 - y1 : y1 - y0;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    can_wave_px(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static void can_wave_clear(uint16_t bg) {
  if (!s_can_wave_pixels) return;
  uint16_t* p = (uint16_t*)s_can_wave_pixels;
  for (int i = 0; i < CAN_WAVE_W * CAN_WAVE_H; i++) p[i] = bg;
}

/* Poll the assembled waveform at 20 Hz. Redraws only when a new set arrived,
 * so the single-buffered RGB panel is not invalidated continuously. */
static void can_wave_tick(void) {
  if (!s_can_wave_canvas || !s_can_wave_pixels) return;

  can_monitor_adc_wave_t w;
  const bool ok = can_monitor_get_adc_wave(&w);
  if (ok) {
    if (w.generation == s_can_wave_gen) return;
    s_can_wave_gen = w.generation;
    s_can_wave_empty = false;
  } else {
    if (s_can_wave_empty) return;
    s_can_wave_empty = true;
  }

  const uint16_t bg = c565(C_BG);
  can_wave_clear(bg);

  if (!ok) {
    txt(s_can_wave_info, "ADC 20 ms   waiting for waveform");
    lv_obj_invalidate(s_can_wave_canvas);
    return;
  }

  /* Work in the node's Q6 units (1/64 count) and auto-scale to the observed
   * span, with a 4-count minimum so a flat signal does not turn noise into a
   * full-height trace. A typical coil waveform spans under 10 counts. */
  int32_t v[CAN_MONITOR_WAVE_SLOTS];
  int32_t lo = 65535;
  int32_t hi = 0;
  for (int i = 0; i < CAN_MONITOR_WAVE_SLOTS; i++) {
    v[i] = (int32_t)w.slot[i];
    if (v[i] < lo) lo = v[i];
    if (v[i] > hi) hi = v[i];
  }
  int32_t span = hi - lo;
  if (span < 4 * 64) span = 4 * 64;
  const int32_t mid = (lo + hi) / 2;
  const int32_t top = mid + span / 2;
  const int32_t bottom = mid - span / 2;
  const int32_t denom = top - bottom;

  const uint16_t grid = c565(0x262626);
  for (int gy = CAN_WAVE_H / 4; gy < CAN_WAVE_H; gy += CAN_WAVE_H / 4) {
    for (int x = 0; x < CAN_WAVE_W; x++) can_wave_px(x, gy, grid);
  }
  for (int gx = CAN_WAVE_W / 4; gx < CAN_WAVE_W; gx += CAN_WAVE_W / 4) {
    for (int y = 0; y < CAN_WAVE_H; y++) can_wave_px(gx, y, grid);
  }

  const uint16_t line = c565(C_AMBER);
  int px = 0;
  int py = (int)((top - v[0]) * (CAN_WAVE_H - 1) / denom);
  for (int i = 1; i < CAN_MONITOR_WAVE_SLOTS; i++) {
    const int x = i * (CAN_WAVE_W - 1) / (CAN_MONITOR_WAVE_SLOTS - 1);
    const int y = (int)((top - v[i]) * (CAN_WAVE_H - 1) / denom);
    can_wave_line(px, py, x, y, line);
    px = x;
    py = y;
  }

  char buf[64];
  HausState st;
  gui_get_state(&st);
  if (st.can.ac_fresh) {
    snprintf(buf, sizeof(buf), "ADC RMS %.1f cnt  %.1f deg",
             st.can.ac_rms_dc / 10.0, st.can.ac_angle_cdeg / 100.0);
  } else {
    snprintf(buf, sizeof(buf), "ADC 20 ms   %ld..%ld counts",
             (long)((lo + 32) / 64), (long)((hi + 32) / 64));
  }
  txt(s_can_wave_info, buf);
  lv_obj_invalidate(s_can_wave_canvas);
}

static void can_overlay_close(void) {
  if (s_can_overlay) {
    lv_obj_delete(s_can_overlay);
    s_can_overlay = NULL;
  }
}

static void can_overlay_close_cb(lv_event_t* e) {
  (void)e;
  can_overlay_close();
}

static void can_overlay_open(int index) {
  can_id_entry_t snapshot;
  if (!can_monitor_get_entry(index, &snapshot)) return;
  const can_id_entry_t* entry = &snapshot;
  can_overlay_close();

  lv_obj_t* o = lv_obj_create(lv_layer_top());
  s_can_overlay = o;
  lv_obj_set_size(o, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(o, 0, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  obj_set_scrollable(o, false);
  obj_set_clickable(o, true);

  char buf[220];
  if (entry->is_extended) {
    snprintf(buf, sizeof(buf), "CAN 0x%08lX", (unsigned long)entry->identifier);
  } else {
    snprintf(buf, sizeof(buf), "CAN 0x%03lX",
             (unsigned long)(entry->identifier & 0x7FFu));
  }
  label_make(o, buf, 20, 10, &lv_font_montserrat_32, C_INK);

  lv_obj_t* close = lv_obj_create(o);
  lv_obj_set_size(close, 48, 44);
  lv_obj_set_pos(close, SCREEN_W - 58, 10);
  lv_obj_set_style_bg_opa(close, 0, 0);
  lv_obj_set_style_border_width(close, 0, 0);
  lv_obj_set_style_pad_all(close, 0, 0);
  obj_set_scrollable(close, false);
  obj_set_clickable(close, true);
  lv_obj_add_event_cb(close, can_overlay_close_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* x =
      label_make(close, LV_SYMBOL_CLOSE, 0, 0, &lv_font_montserrat_24, C_INK);
  lv_obj_center(x);

  can_monitor_decoded_t d;
  can_monitor_decode(entry->identifier, entry->data, entry->data_len, &d);

  int y = 76;
  snprintf(buf, sizeof(buf), "TYPE      %s", d.label[0] ? d.label : "RAW");
  label_make(o, buf, 20, y, &haus_body_21, C_INK);
  y += 32;
  snprintf(buf, sizeof(buf), "FRAMES    %lu (changed %lu)",
           (unsigned long)entry->frame_count,
           (unsigned long)entry->changed_count);
  label_make(o, buf, 20, y, &haus_body_21, C_INK);
  y += 32;
  snprintf(buf, sizeof(buf), "INTERVAL  %lu ms (%lu/%lu)", entry->interval_ms,
           entry->interval_min_ms, entry->interval_max_ms);
  label_make(o, buf, 20, y, &haus_body_21, C_INK);
  y += 32;
  const uint32_t ago = (can_ui_now_ms() - entry->last_timestamp) / 1000u;
  snprintf(buf, sizeof(buf), "LAST      %lu s ago", (unsigned long)ago);
  label_make(o, buf, 20, y, &haus_body_21, C_INK);
  y += 40;

  label_make(o, "DECODED", 20, y, &lv_font_montserrat_16, C_DIM);
  y += 26;
  lv_obj_t* dec = label_make(o, d.decoded[0] ? d.decoded : "--", 20, y,
                             &haus_body_21, C_INK);
  lv_obj_set_width(dec, SCREEN_W - 40);
  lv_label_set_long_mode(dec, UI_LABEL_LONG_WRAP);
  y += 78;

  if (d.protocol == HAUS_PROTO_SML) {
    const uint16_t base = (uint16_t)(entry->identifier & ~0xFFu);
    const int slot = (base == CAN_ID_STROMZAEHLER_INFO_ALT) ? 1 : 0;
    sml_reassembly_t sml;
    if (can_monitor_get_sml(slot, &sml)) {
      snprintf(buf, sizeof(buf), "SML       %u/%u B, %u frames, %s",
               sml.received_length, sml.expected_length, sml.frames_received,
               sml.complete ? "OK" : (sml.active ? "rx" : "--"));
      label_make(o, buf, 20, y, &haus_body_21, C_DIM);
      y += 32;
    }
  }

  /* Raw bytes, last frame. */
  label_make(o, "RAW", 20, y, &lv_font_montserrat_16, C_DIM);
  y += 24;
  char raw[64];
  size_t off = 0;
  raw[0] = '\0';
  for (uint8_t i = 0; i < entry->data_len && off + 4 <= sizeof(raw); i++) {
    off += snprintf(raw + off, sizeof(raw) - off, "%02X ", entry->data[i]);
  }
  label_make(o, raw[0] ? raw : "--", 20, y, &haus_body_21, C_INK);
}

static void can_row_cb(lv_event_t* e) {
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  can_overlay_open(index);
}

static void can_screen_update(void) {
  const uint32_t now = can_ui_now_ms();
  if ((int32_t)(now - s_can_next_ms) < 0) return;
  s_can_next_ms = now + 250;

  HausState st;
  gui_get_state(&st);
  const bool fault = st.can.can_fault != 0;
  if (s_can_fault_bar) {
    obj_set_hidden(s_can_fault_bar, !fault);
    if (s_can_stats) obj_set_hidden(s_can_stats, fault);
  }

  const can_monitor_counters_t* c = can_monitor_counters();
  if (s_can_stats) {
    char buf[96];
    snprintf(buf, sizeof(buf), "RX %lu  drop %lu  TS %lu  SML %lu/%lu",
             (unsigned long)c->rx_frames, (unsigned long)c->dropped,
             (unsigned long)c->proto_ts, (unsigned long)c->sml_ok,
             (unsigned long)c->sml_incomplete);
    txt(s_can_stats, buf);
  }

  for (int i = 0; i < CAN_ROWS; i++) {
    can_id_entry_t snapshot;
    if (!can_monitor_get_entry(i, &snapshot)) {
      txt(s_can_id_lbl[i], "---");
      txt(s_can_type_lbl[i], "--");
      txt(s_can_text_lbl[i], "--");
      continue;
    }
    const can_id_entry_t* entry = &snapshot;
    char idb[16];
    if (entry->is_extended) {
      snprintf(idb, sizeof(idb), "%08lX", (unsigned long)entry->identifier);
    } else {
      snprintf(idb, sizeof(idb), "%03lX",
               (unsigned long)(entry->identifier & 0x7FFu));
    }
    txt(s_can_id_lbl[i], idb);

    can_monitor_decoded_t d;
    can_monitor_decode(entry->identifier, entry->data, entry->data_len, &d);
    txt(s_can_type_lbl[i], d.label[0] ? d.label : "RAW");
    txt(s_can_text_lbl[i], d.decoded[0] ? d.decoded : "--");
  }
}

lv_obj_t* haus_ui_build_can(void) {
  screen_lock_scroll();
  lv_obj_t* root = lv_screen_active();
  lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);
  g_current_screen = UI_SCREEN_CAN;

  HausState st;
  gui_get_state(&st);
  const bool on = st.can.can_enabled;
  header_build("CAN MONITOR", on ? "ON" : "OFF", on ? C_OK : C_OFF);

  s_can_stats = NULL;
  s_can_fault_bar = NULL;
  s_can_wave_canvas = NULL;
  s_can_wave_info = NULL;
  s_can_wave_pixels = NULL;
  s_can_wave_gen = 0;
  s_can_wave_empty = false;
  for (int i = 0; i < CAN_ROWS; i++) {
    s_can_id_lbl[i] = NULL;
    s_can_type_lbl[i] = NULL;
    s_can_text_lbl[i] = NULL;
  }

  if (!on) {
    label_make(root, "CAN is disabled in Settings.", 20, 80, &haus_body_21,
               C_AMBER);
    label_make(root, "USB remains available.", 20, 118, &haus_body_21, C_DIM);
    label_make(root, "No history, no monitor.", 20, 156, &haus_body_21, C_DIM);
    nav_build(UI_SCREEN_SETTINGS);
    return root;
  }

  s_can_stats = label_make(root, "", 20, 58, &lv_font_montserrat_16, C_DIM);

  /* Header fault banner (S16.09). It replaces the stats line on a fault. */
  lv_obj_t* fb = lv_obj_create(root);
  lv_obj_set_size(fb, CONTENT_W, 20);
  lv_obj_set_pos(fb, 0, 56);
  lv_obj_set_style_bg_color(fb, lv_color_hex(C_HOT), 0);
  lv_obj_set_style_bg_opa(fb, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(fb, 0, 0);
  lv_obj_set_style_radius(fb, 0, 0);
  lv_obj_set_style_pad_all(fb, 0, 0);
  obj_set_scrollable(fb, false);
  obj_set_clickable(fb, false);
  lv_obj_t* fl =
      label_make(fb, "CAN BUS FAULT", 0, 0, &lv_font_montserrat_16, C_INK);
  lv_obj_center(fl);
  s_can_fault_bar = fb;
  obj_set_hidden(fb, true);

  /* Scrollable list: the waveform below stays fixed. */
  lv_obj_t* list = lv_obj_create(root);
  lv_obj_set_size(list, CONTENT_W, CAN_WAVE_Y - 20 - CAN_ROWS_Y);
  lv_obj_set_pos(list, 0, CAN_ROWS_Y);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  obj_set_scrollable(list, true);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

  for (int i = 0; i < CAN_ROWS; i++) {
    lv_obj_t* row = lv_obj_create(list);
    lv_obj_set_size(row, CONTENT_W, CAN_ROW_H);
    lv_obj_set_pos(row, 0, i * CAN_ROW_H);
    lv_obj_set_style_bg_color(
        row, lv_color_hex(i % 2 == 0 ? 0x111111 : 0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    obj_set_scrollable(row, false);
    obj_set_clickable(row, true);
    lv_obj_add_event_cb(row, can_row_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    s_can_id_lbl[i] = label_make(row, "---", 14, 6, &haus_body_21, C_INK);
    lv_obj_set_size(s_can_id_lbl[i], 64, 24);
    s_can_type_lbl[i] = label_make(row, "--", 86, 8, &lv_font_montserrat_16, C_DIM);
    lv_obj_set_size(s_can_type_lbl[i], 110, 22);
    s_can_text_lbl[i] = label_make(row, "--", 204, 8, &lv_font_montserrat_16, C_INK);
    lv_obj_set_size(s_can_text_lbl[i], CONTENT_W - 218, 22);
    lv_label_set_long_mode(s_can_id_lbl[i], LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(s_can_type_lbl[i], LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(s_can_text_lbl[i], LV_LABEL_LONG_DOT);
  }

  s_can_wave_info = label_make(root, "ADC 20 ms   waiting for waveform", 20,
                               CAN_WAVE_Y - 20, &lv_font_montserrat_16, C_DIM);
  s_can_wave_canvas = lv_canvas_create(root);
  lv_obj_set_size(s_can_wave_canvas, CAN_WAVE_W, CAN_WAVE_H);
  lv_obj_set_pos(s_can_wave_canvas, 20, CAN_WAVE_Y);
  s_can_wave_pixels = (uint8_t*)lv_malloc(CAN_WAVE_W * CAN_WAVE_H * 2);
  if (s_can_wave_pixels) {
    lv_canvas_set_buffer(s_can_wave_canvas, s_can_wave_pixels, CAN_WAVE_W,
                         CAN_WAVE_H, LV_COLOR_FORMAT_RGB565);
  }
  obj_set_clickable(s_can_wave_canvas, false);
  can_wave_tick();

  nav_build(UI_SCREEN_SETTINGS);
  can_screen_update();
  return root;
}

lv_obj_t* haus_ui_build_settings(void) {
  screen_lock_scroll();
  lv_obj_t* root = lv_screen_active();
  lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);
  g_current_screen = UI_SCREEN_SETTINGS;
  header_build("SETTINGS", NULL, C_DIM);

  HausState st;
  gui_get_state(&st);

  const int LX = 20, RX = 368, CW = 324;
  label_make(root, "CAN BUS", LX, 58, &haus_body_21, C_INK);
  label_make(root, "Off hands USB back: no CAN data,", LX, 100,
             &lv_font_montserrat_16, C_DIM);
  label_make(root, "no history, no monitor.", LX, 118, &lv_font_montserrat_16,
             C_DIM);
  s_can_switch = lv_obj_create(root);
  lv_obj_set_size(s_can_switch, 82, 36);
  lv_obj_set_pos(s_can_switch, LX + CW - 82, 60);
  lv_obj_set_style_border_width(s_can_switch, 0, 0);
  lv_obj_set_style_radius(s_can_switch, 18, 0);
  lv_obj_set_style_pad_all(s_can_switch, 0, 0);
  obj_set_scrollable(s_can_switch, false);
  obj_set_clickable(s_can_switch, true);
  lv_obj_add_event_cb(s_can_switch, settings_can_cb, LV_EVENT_CLICKED, NULL);
  s_can_switch_lbl = label_make(s_can_switch, st.can.can_enabled ? "ON" : "OFF",
                                0, 0, &lv_font_montserrat_16, C_INK);
  lv_obj_center(s_can_switch_lbl);
  s_can_fault_lbl = label_make(root, "", LX + 150, 68,
                               &lv_font_montserrat_16, C_HOT);
  settings_can_visual(st.can.can_enabled);
  settings_can_refresh();

  const bool mounted = strcmp(st.can.sd_card_type, "ABSENT") != 0;
  int yl = 146, yr = 146;
  const int step = 40;
  settings_value_row(LX, yl, CW, "CAN MONITOR", "live IDs", settings_open_can_cb);
  uint8_t sensors = 0;
  gui_get_sensors(&sensors);
  char sbuf[24];
  snprintf(sbuf, sizeof(sbuf), "%u bound", (unsigned)sensors);
  settings_value_row(RX, yr, CW, "SENSOR ROLES", sbuf, settings_open_roles_cb);
  yl += step;
  yr += step;
  settings_value_row(LX, yl, CW, "PUMP", "monitor", settings_open_ac_cb);
  settings_value_row(RX, yr, CW, "STORAGE", mounted ? "card" : "absent",
                     settings_open_storage_cb);
  yl += step;
  yr += step;
  settings_value_row(LX, yl, CW, "LOG", "ring + SD save", settings_open_log_cb);
  s_panel_lbl = settings_value_row(RX, yr, CW, "PANEL RESET", "timing",
                                   settings_panel_reset_cb);
  yl += step;
  yr += step;
  s_backlight_lbl = settings_value_row(
      LX, yl, CW, "BACKLIGHT", gui_backlight_on() ? "ON" : "OFF",
      settings_backlight_cb);
  s_diag_lbl = settings_value_row(RX, yr, CW, "DIAGNOSTICS", "SAVE RING",
                                  settings_diag_cb);
  yl += step;
  yr += step;
  s_power_unit_lbl = settings_value_row(LX, yl, CW, "POWER UNIT", "auto",
                                        settings_power_unit_cb);
  settings_value_row(RX, yr, CW, "NETWORK", gui_wifi_ip(), NULL);
  yr += step;
  settings_value_row(RX, yr, CW, "FIRMWARE", GIT_HASH, NULL);

  nav_build(UI_SCREEN_SETTINGS);
  return root;
}
void haus_ui_deinit(void) {
  can_overlay_close();
  log_modal_close();
  s_log_modal = NULL;
  lv_obj_t* root = lv_screen_active();
  while (lv_obj_get_child_cnt(root) > 0) {
    lv_obj_delete(lv_obj_get_child(root, 0));
  }
  s_status_date = s_clock = s_can_dot = s_sd_dot = s_wifi_icon = NULL;
  /* The banner is a child of the screen root and was deleted with the
   * other children above; only forget the pointers. */
  s_fault_banner = NULL;
  s_fault_banner_lbl = NULL;
  s_fault_banner_shown = -1;
  s_sd_low_lbl = NULL;
  s_cabinet = s_fire = NULL;
  memset(s_flame, 0, sizeof(s_flame));
  s_flame_glow = s_flame_core = NULL;
  s_chamber_temp = s_chamber_unit = NULL;
  s_cabinet_target = NULL;
  s_water_in = s_water_in_lbl = NULL;
  s_pump = s_pump_lbl = s_water_out = s_water_out_lbl = NULL;
  s_living_temp = s_living_unit = s_living_temp_tol = s_living_rh =
      s_living_rh_tol = NULL;
  s_power_num = s_power_bolt = s_power_src = NULL;
  memset(s_zone, 0, sizeof(s_zone));
  s_last_can_color = s_last_sd_color = s_last_wifi_color = s_last_fire_grad =
      0xFFFFFFFFu;
  memset(s_last_flame_opa, 0xFF, sizeof(s_last_flame_opa));

  if (s_chart_pixels) {
    lv_free(s_chart_pixels);
    s_chart_pixels = NULL;
  }
  if (s_can_wave_pixels) {
    lv_free(s_can_wave_pixels);
    s_can_wave_pixels = NULL;
  }
  s_can_wave_canvas = s_can_wave_info = NULL;
  s_chart_canvas = s_chart_nodata = s_chart_stale = NULL;
  s_chart_target_lbl = s_chart_readout = s_chart_readout_b = s_chart_age = NULL;
  s_legend_a = s_legend_b = s_legend_t = NULL;
  memset(s_axis_lbl, 0, sizeof(s_axis_lbl));
  memset(s_x_lbl, 0, sizeof(s_x_lbl));
  s_scrub_line = s_scrub_bubble = s_scrub_text = NULL;
  s_scrub_hide_at = 0;
  memset(s_range_lbl, 0, sizeof(s_range_lbl));
  s_range_hl = NULL;
  s_detail_value = NULL;
  s_header_badge = s_header_badge_lbl = NULL;
  s_above_target = NULL;
  s_side_h = s_side_big = NULL;
  memset(s_side_l, 0, sizeof(s_side_l));
  s_side_note = NULL;
  s_side_big2 = s_side_big2_lbl = s_side_delta = s_side_delta_lbl = NULL;
  s_side_hr2 = s_side_hr3 = NULL;
  memset(s_side_legend, 0, sizeof(s_side_legend));
  s_pwm_switch = s_pwm_switch_lbl = s_pwm_slider = NULL;
  s_pwm_req_lbl = s_pwm_force_lbl = NULL;
  s_pwm_touched = false;
  s_pwm_can_ok = false;
  s_pwm_last_tx = 0xFFFF;
  memset(s_living_tab, 0, sizeof(s_living_tab));
  memset(s_ac_tab, 0, sizeof(s_ac_tab));
  s_ac_hl = NULL;
  s_panel_lbl = NULL;
  s_log_label = s_log_container = s_log_status = NULL;
  s_log_follow = s_log_follow_lbl = s_log_ws = NULL;
  s_log_sd = NULL;
  memset(s_log_level, 0, sizeof(s_log_level));
  memset(s_roles_lbl, 0, sizeof(s_roles_lbl));
  s_axis_key = -1;
  s_chart_force = true;
}

/* Load a screen. The stack is managed by ui_jump/ui_push/ui_pop. */
static void ui_show_screen(int screen) {
  if (screen < UI_SCREEN_HOME || screen > UI_SCREEN_ROLES) return;
  g_current_screen = screen;

  haus_ui_deinit();

  switch (screen) {
    case UI_SCREEN_HOME:
      haus_ui_init();
      break;
    case UI_SCREEN_CHAMBER:
      haus_ui_build_detail();
      break;
    case UI_SCREEN_WATER_IN:
    case UI_SCREEN_WATER_OUT:
      haus_ui_build_water(screen);
      break;
    case UI_SCREEN_POWER:
      haus_ui_build_power();
      break;
    case UI_SCREEN_LIVING:
      haus_ui_build_living();
      break;
    case UI_SCREEN_AC:
      haus_ui_build_ac();
      break;
    case UI_SCREEN_SETTINGS:
      haus_ui_build_settings();
      break;
    case UI_SCREEN_STORAGE:
      haus_ui_build_storage();
      break;
    case UI_SCREEN_CAN:
      haus_ui_build_can();
      break;
    case UI_SCREEN_LOG:
      haus_ui_build_log();
      break;
    case UI_SCREEN_ROLES:
      haus_ui_build_roles();
      break;
  }
}
