/**
 * @file haus_ui.c
 * @brief Productive LVGL screens for the 28_hauszentrale panel (480x800).
 *
 * Shared by the ESP32 firmware and the macOS host harness. Builds every screen
 * from the pure-data interface in haus_gui.h; no device or host specifics.
 *
 * Layout follows docs/ui/mockup.html:
 *   y=0..32    Status strip (date, CAN/SD marks)
 *   y=32..172  Clock (HH:MM)
 *   y=172..580 Heater cabinet (fire + pipes)
 *   y=580..656 Living-room band
 *   y=656..712 Power band
 *   y=712..800 Bottom navigation pane (6 tabs, smartphone-style)
 */

#define _POSIX_C_SOURCE 200809L

#include "haus_ui.h"
#include "haus_gui.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ─── Geometry ────────────────────────────────────────────────────────────── */

#define SCREEN_W 480
#define SCREEN_H 800

#define NAV_Y 712
#define NAV_H 88
#define NAV_TABS 6

/* ─── Palette (from docs/ui/mockup.html) ──────────────────────────────────── */

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
#define C_PANEL 0x1a1412

/* ─── Bottom navigation ───────────────────────────────────────────────────── */

static const char *nav_icons[NAV_TABS] = {
    LV_SYMBOL_HOME,     /* 0 home */
    LV_SYMBOL_CHARGE,   /* 1 chamber */
    NULL,               /* 2 water: both in and out (drawn as waves) */
    LV_SYMBOL_POWER,    /* 3 power */
    LV_SYMBOL_EYE_OPEN, /* 4 living (monitoring/observation) */
    LV_SYMBOL_SETTINGS, /* 5 gear */
};

/* Home-screen widgets (NULL when the home screen is not built). */
static lv_obj_t *s_status_date;
static lv_obj_t *s_clock;
static lv_obj_t *s_can_dot;
static lv_obj_t *s_sd_dot;
static lv_obj_t *s_cabinet;
static lv_obj_t *s_fire;
static lv_obj_t *s_flame[3];
static lv_obj_t *s_chamber_temp;
static lv_obj_t *s_chamber_unit;
static lv_obj_t *s_water_in;
static lv_obj_t *s_water_in_lbl;
static lv_obj_t *s_pump;
static lv_obj_t *s_pump_lbl;
static lv_obj_t *s_water_out;
static lv_obj_t *s_water_out_lbl;
static lv_obj_t *s_living_temp;
static lv_obj_t *s_living_unit;
static lv_obj_t *s_living_rh;
static lv_obj_t *s_living_pa;
static lv_obj_t *s_power_num;
static lv_obj_t *s_power_bolt;
static lv_obj_t *s_power_src;

static void align_power_labels(void);

/* Last values pushed into the widget tree. Every setter below is guarded by
 * these caches: lv_label_set_text() and lv_obj_set_style_*() invalidate the
 * object unconditionally, even for an identical value. On the RGB panel a
 * redundant invalidation is a visible redraw, so repeatedly refreshed screens
 * must only touch what actually changed. */
static uint32_t s_last_can_color = 0xFFFFFFFFu;
static uint32_t s_last_sd_color = 0xFFFFFFFFu;
static uint32_t s_last_fire_grad = 0xFFFFFFFFu;
static uint8_t s_last_flame_opa[3] = {0xFF, 0xFF, 0xFF};

/* ─── Small helpers ───────────────────────────────────────────────────────── */

static bool txt(lv_obj_t *label, const char *text) {
    if (!label || !text) return false;
    const char *cur = lv_label_get_text(label);
    if (cur && strcmp(cur, text) == 0) return false;
    lv_label_set_text(label, text);
    return true;
}

/* LVGL 10 replaced the generic flag API with per-flag setters. The firmware
 * still builds against LVGL 9.2, so keep both paths working. */
#if LVGL_VERSION_MAJOR >= 10
static void obj_set_clickable(lv_obj_t *o, bool en) {
    lv_obj_set_clickable(o, en);
}
static void obj_set_scrollable(lv_obj_t *o, bool en) {
    lv_obj_set_scrollable(o, en);
}
#else
static void obj_set_clickable(lv_obj_t *o, bool en) {
    if (en) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    }
}
static void obj_set_scrollable(lv_obj_t *o, bool en) {
    if (en) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    } else {
        lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    }
}
#endif

static lv_obj_t *label_make(lv_obj_t *parent, const char *text, int x, int y,
                            const lv_font_t *font, uint32_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

/* A 1-pixel separator line. */
static void hline(lv_obj_t *parent, int y, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, SCREEN_W, 1);
    lv_obj_set_pos(o, 0, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    obj_set_clickable(o, false);
    obj_set_scrollable(o, false);
}

/* Three stacked waves standing in for the water series (in and out). LVGL's
 * built-in Montserrat subset has no wave glyph, so draw it. The points array is
 * static because lv_line stores the pointer instead of copying it. */
static void nav_waves(lv_obj_t *nav, int cx, int y, uint32_t color) {
    static const lv_point_precise_t wave_pts[] = {
        {0, 2}, {4, 0}, {8, 2}, {12, 0}, {16, 2},
        {20, 0}, {24, 2}, {28, 0}, {32, 2},
    };
    for (int r = 0; r < 3; r++) {
        lv_obj_t *w = lv_line_create(nav);
        lv_line_set_points(w, wave_pts, 9);
        lv_obj_set_pos(w, cx - 16, y + r * 7);
        lv_obj_set_style_line_width(w, 2, 0);
        lv_obj_set_style_line_color(w, lv_color_hex(color), 0);
        lv_obj_set_style_line_rounded(w, true, 0);
        obj_set_clickable(w, false);
    }
}

static void nav_build(int active) {
    lv_obj_t *nav = lv_obj_create(lv_screen_active());
    lv_obj_set_size(nav, SCREEN_W, NAV_H);
    lv_obj_set_pos(nav, 0, NAV_Y);
    lv_obj_set_style_bg_color(nav, lv_color_hex(0x0c0b0a), 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_pad_all(nav, 0, 0);
    obj_set_scrollable(nav, false);
    hline(nav, 0, C_LINE);

    int tab_w = SCREEN_W / NAV_TABS;
    for (int i = 0; i < NAV_TABS; i++) {
        uint32_t color = i == active ? C_INK : C_DIM;
        if (nav_icons[i] == NULL) {
            nav_waves(nav, i * tab_w + tab_w / 2, NAV_H / 2 - 10, color);
        } else {
            lv_obj_t *icon = lv_label_create(nav);
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(icon, lv_color_hex(color), 0);
            lv_label_set_text(icon, nav_icons[i]);
            lv_obj_set_width(icon, tab_w);
            lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(icon, i * tab_w, NAV_H / 2 - 14);
        }

        if (i == active) {
            lv_obj_t *rule = lv_obj_create(nav);
            lv_obj_set_size(rule, tab_w - 28, 3);
            lv_obj_set_pos(rule, i * tab_w + 14, NAV_H - 3);
            lv_obj_set_style_bg_color(rule, lv_color_hex(C_ACCENT), 0);
            lv_obj_set_style_border_width(rule, 0, 0);
            lv_obj_set_style_radius(rule, 1, 0);
            obj_set_clickable(rule, false);
        }
    }
}

/* ─── Date helpers ────────────────────────────────────────────────────────── */

static const char *const WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu",
                                       "Fri", "Sat"};
static const char *const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static void epoch_to_date(uint32_t epoch, char *out, size_t n) {
    time_t t = (time_t)epoch;
    struct tm tm;
    if (!localtime_r(&t, &tm)) {
        snprintf(out, n, "--- -- ---");
        return;
    }
    snprintf(out, n, "%s %d %s", WEEKDAYS[tm.tm_wday], tm.tm_mday,
             MONTHS[tm.tm_mon]);
}

/* ─── Home screen ─────────────────────────────────────────────────────────── */

static void build_status(void) {
    s_status_date = label_make(lv_screen_active(), "--- -- ---", 12, 6,
                               &lv_font_montserrat_16, C_INK);

    lv_obj_t *can = label_make(lv_screen_active(), "CAN", SCREEN_W - 112, 6,
                               &lv_font_montserrat_16, C_DIM);
    (void)can;
    s_can_dot = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_can_dot, 12, 12);
    lv_obj_set_pos(s_can_dot, SCREEN_W - 132, 10);
    lv_obj_set_style_bg_color(s_can_dot, lv_color_hex(C_OK), 0);
    lv_obj_set_style_radius(s_can_dot, 6, 0);
    lv_obj_set_style_border_width(s_can_dot, 0, 0);

    label_make(lv_screen_active(), "SD", SCREEN_W - 40, 6,
               &lv_font_montserrat_16, C_DIM);
    s_sd_dot = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_sd_dot, 12, 12);
    lv_obj_set_pos(s_sd_dot, SCREEN_W - 60, 10);
    lv_obj_set_style_bg_color(s_sd_dot, lv_color_hex(C_OK), 0);
    lv_obj_set_style_radius(s_sd_dot, 6, 0);
    lv_obj_set_style_border_width(s_sd_dot, 0, 0);
}

static void build_clock(void) {
    s_clock = label_make(lv_screen_active(), "--:--", 0, 0,
                         &lv_font_montserrat_48, C_INK);
    lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, 52);
}

/* A flame tongue: a pill, wider at the base by stacking two pills. */
static void build_heater(void) {
    /* Cabinet glow */
    lv_obj_t *glow = lv_obj_create(lv_screen_active());
    lv_obj_set_size(glow, 248, 234);
    lv_obj_set_pos(glow, 116, 188);
    lv_obj_set_style_bg_color(glow, lv_color_hex(0xc4783a), 0);
    lv_obj_set_style_bg_opa(glow, 24, 0);
    lv_obj_set_style_radius(glow, 22, 0);
    lv_obj_set_style_border_width(glow, 0, 0);
    obj_set_clickable(glow, false);

    /* Cabinet */
    s_cabinet = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_cabinet, 232, 214);
    lv_obj_set_pos(s_cabinet, 124, 196);
    lv_obj_set_style_bg_color(s_cabinet, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_radius(s_cabinet, 16, 0);
    lv_obj_set_style_border_width(s_cabinet, 2, 0);
    lv_obj_set_style_border_color(s_cabinet, lv_color_hex(0x6a5e54), 0);
    lv_obj_set_style_pad_all(s_cabinet, 0, 0);
    obj_set_scrollable(s_cabinet, false);

    /* Fire chamber (warm gradient) */
    s_fire = lv_obj_create(s_cabinet);
    lv_obj_set_size(s_fire, 184, 118);
    lv_obj_align(s_fire, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_color(s_fire, lv_color_hex(0x1a1412), 0);
    lv_obj_set_style_bg_grad_color(s_fire, lv_color_hex(0x3a2418), 0);
    lv_obj_set_style_bg_grad_dir(s_fire, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(s_fire, 10, 0);
    lv_obj_set_style_border_width(s_fire, 0, 0);
    lv_obj_set_style_pad_all(s_fire, 0, 0);
    obj_set_scrollable(s_fire, false);

    /* Flame: a soft glow plus three tongues and a bright core. */
    lv_obj_t *glow2 = lv_obj_create(s_fire);
    lv_obj_set_size(glow2, 84, 92);
    lv_obj_align(glow2, LV_ALIGN_BOTTOM_MID, 0, 4);
    lv_obj_set_style_bg_color(glow2, lv_color_hex(0xc06a2a), 0);
    lv_obj_set_style_radius(glow2, 42, 0);
    lv_obj_set_style_border_width(glow2, 0, 0);
    lv_obj_set_style_opa(glow2, 90, 0);
    obj_set_clickable(glow2, false);

    const int fw[3] = {26, 40, 26};
    const int fh[3] = {54, 88, 54};
    const int fx[3] = {-30, 0, 30};
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
    /* Bright core. */
    lv_obj_t *core = lv_obj_create(s_fire);
    lv_obj_set_size(core, 16, 46);
    lv_obj_align(core, LV_ALIGN_BOTTOM_MID, 0, 6);
    lv_obj_set_style_bg_color(core, lv_color_hex(0xffe0a0), 0);
    lv_obj_set_style_radius(core, 8, 0);
    lv_obj_set_style_border_width(core, 0, 0);
    lv_obj_set_style_opa(core, 240, 0);
    obj_set_clickable(core, false);

    /* Soll / target (below the fire, inside the cabinet) */
    lv_obj_t *target = label_make(s_cabinet, "60°C", 0, 0,
                                  &lv_font_montserrat_32, C_INK);
    lv_obj_align(target, LV_ALIGN_BOTTOM_MID, 0, -6);

    lv_obj_t *soll = label_make(s_cabinet, "Soll", 0, 0,
                                &lv_font_montserrat_16, C_DIM);
    lv_obj_align(soll, LV_ALIGN_BOTTOM_MID, 0, -42);

    /* Pipes: left (water in, amber) and right (water out, red). */
    lv_obj_t *pipe_l = lv_obj_create(lv_screen_active());
    lv_obj_set_size(pipe_l, 148, 12);
    lv_obj_set_pos(pipe_l, 28, 466);
    lv_obj_set_style_bg_color(pipe_l, lv_color_hex(C_AMBER), 0);
    lv_obj_set_style_radius(pipe_l, 6, 0);
    lv_obj_set_style_border_width(pipe_l, 0, 0);
    obj_set_clickable(pipe_l, false);
    lv_obj_t *pipe_lu = lv_obj_create(lv_screen_active());
    lv_obj_set_size(pipe_lu, 12, 62);
    lv_obj_set_pos(pipe_lu, 164, 410);
    lv_obj_set_style_bg_color(pipe_lu, lv_color_hex(C_AMBER), 0);
    lv_obj_set_style_radius(pipe_lu, 6, 0);
    lv_obj_set_style_border_width(pipe_lu, 0, 0);
    obj_set_clickable(pipe_lu, false);

    lv_obj_t *pipe_r = lv_obj_create(lv_screen_active());
    lv_obj_set_size(pipe_r, 148, 12);
    lv_obj_set_pos(pipe_r, 304, 466);
    lv_obj_set_style_bg_color(pipe_r, lv_color_hex(C_HOT), 0);
    lv_obj_set_style_radius(pipe_r, 6, 0);
    lv_obj_set_style_border_width(pipe_r, 0, 0);
    obj_set_clickable(pipe_r, false);
    lv_obj_t *pipe_ru = lv_obj_create(lv_screen_active());
    lv_obj_set_size(pipe_ru, 12, 62);
    lv_obj_set_pos(pipe_ru, 304, 410);
    lv_obj_set_style_bg_color(pipe_ru, lv_color_hex(C_HOT), 0);
    lv_obj_set_style_radius(pipe_ru, 6, 0);
    lv_obj_set_style_border_width(pipe_ru, 0, 0);
    obj_set_clickable(pipe_ru, false);

    /* Flow dashes along the horizontal pipes. */
    for (int j = 0; j < 6; j++) {
        lv_obj_t *d1 = lv_obj_create(lv_screen_active());
        lv_obj_set_size(d1, 12, 4);
        lv_obj_set_pos(d1, 40 + j * 22, 470);
        lv_obj_set_style_bg_color(d1, lv_color_hex(C_INK), 0);
        lv_obj_set_style_radius(d1, 2, 0);
        lv_obj_set_style_border_width(d1, 0, 0);
        lv_obj_set_style_opa(d1, 150, 0);
        obj_set_clickable(d1, false);

        lv_obj_t *d2 = lv_obj_create(lv_screen_active());
        lv_obj_set_size(d2, 12, 4);
        lv_obj_set_pos(d2, 316 + j * 22, 470);
        lv_obj_set_style_bg_color(d2, lv_color_hex(C_INK), 0);
        lv_obj_set_style_radius(d2, 2, 0);
        lv_obj_set_style_border_width(d2, 0, 0);
        lv_obj_set_style_opa(d2, 150, 0);
        obj_set_clickable(d2, false);
    }

    /* Chamber temperature (left of the cabinet). */
    s_chamber_temp = label_make(lv_screen_active(), "--", 24, 250,
                                &lv_font_montserrat_32, C_COLD);
    s_chamber_unit = label_make(lv_screen_active(), "°C", 0, 0,
                                &lv_font_montserrat_16, C_COLD);
    lv_obj_align_to(s_chamber_unit, s_chamber_temp,
                    LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -2);

    /* Pipe labels. */
    s_water_in = label_make(lv_screen_active(), "--", 24, 508,
                            &lv_font_montserrat_24, C_AMBER);
    s_water_in_lbl = label_make(lv_screen_active(), "water in", 24, 540,
                                &lv_font_montserrat_16, C_DIM);

    s_pump = label_make(lv_screen_active(), "off", 220, 508,
                        &lv_font_montserrat_24, C_OK);
    s_pump_lbl = label_make(lv_screen_active(), "Vpp --", 200, 540,
                            &lv_font_montserrat_16, C_DIM);

    s_water_out = label_make(lv_screen_active(), "--", 348, 508,
                             &lv_font_montserrat_24, C_HOT);
    s_water_out_lbl = label_make(lv_screen_active(), "water out", 348, 540,
                                 &lv_font_montserrat_16, C_DIM);
}

static void build_living(void) {
    hline(lv_screen_active(), 580, C_LINE);

    lv_obj_t *glyph = label_make(lv_screen_active(), LV_SYMBOL_TINT, 20, 600,
                                 &lv_font_montserrat_24, C_COLD);
    (void)glyph;
    s_living_temp = label_make(lv_screen_active(), "--", 56, 596,
                               &lv_font_montserrat_32, C_COLD);
    s_living_unit = label_make(lv_screen_active(), "°C", 0, 0,
                               &lv_font_montserrat_16, C_COLD);
    lv_obj_align_to(s_living_unit, s_living_temp,
                    LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -2);

    s_living_rh = label_make(lv_screen_active(), "-- rh", 300, 590,
                             &lv_font_montserrat_16, C_CYAN);
    s_living_pa = label_make(lv_screen_active(), "-- hPa", 300, 616,
                             &lv_font_montserrat_16, C_VIOLET);
}

static void build_power(void) {
    hline(lv_screen_active(), 656, C_LINE);

    /* Centered group: bolt glyph + watts + unit. The small labels are aligned
     * to the vertical middle of the number rather than to its baseline, so the
     * unit does not sit low against the 48 px digits. */
    s_power_num = label_make(lv_screen_active(), "--", 0, 0,
                             &lv_font_montserrat_48, C_INK);
    lv_obj_align(s_power_num, LV_ALIGN_TOP_MID, 0, 656);

    s_power_bolt = label_make(lv_screen_active(), LV_SYMBOL_CHARGE, 0, 0,
                              &lv_font_montserrat_32, C_ACCENT);
    s_power_src = label_make(lv_screen_active(), "W", 0, 0,
                             &lv_font_montserrat_16, C_DIM);
    align_power_labels();
}

static void align_power_labels(void) {
    if (!s_power_num) return;
    if (s_power_bolt) {
        lv_obj_align_to(s_power_bolt, s_power_num, LV_ALIGN_OUT_LEFT_MID,
                        -10, 0);
    }
    if (s_power_src) {
        lv_obj_align_to(s_power_src, s_power_num, LV_ALIGN_OUT_RIGHT_MID,
                        8, 0);
    }
}

static void build_zones(void) {
    /* Five invisible tappable zones over the heater/living area. */
    for (int i = 0; i < 5; i++) {
        lv_obj_t *z = lv_obj_create(lv_screen_active());
        lv_obj_set_size(z, SCREEN_W / 5, 380);
        lv_obj_set_pos(z, i * (SCREEN_W / 5), 200);
        lv_obj_set_style_bg_opa(z, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(z, 0, 0);
        obj_set_clickable(z, true);
    }
}

lv_obj_t *haus_ui_init(void) {
    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);

    build_status();
    build_clock();
    build_heater();
    build_living();
    build_power();
    nav_build(0);

    /* Paint the honest initial state immediately: the widgets above are built
     * with placeholders, and this replaces them from gui_get_state() before the
     * first frame is flushed. Never leave fabricated sample values on screen. */
    haus_ui_update();

    return root;
}

void haus_ui_update(void) {
    const gui_haus_state_t *state = gui_get_state();
    char buf[48];

    /* Status: date + clock. */
    if (state->time.synced) {
        epoch_to_date(state->time.timestamp, buf, sizeof(buf));
        txt(s_status_date, buf);
        time_t t = (time_t)state->time.timestamp;
        struct tm tm;
        if (localtime_r(&t, &tm)) {
            snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
            txt(s_clock, buf);
        }
    } else {
        txt(s_status_date, "--- -- ---");
        txt(s_clock, "--:--");
    }

    if (s_can_dot) {
        uint32_t color = state->system.can_available ? C_OK : C_HOT;
        if (color != s_last_can_color) {
            s_last_can_color = color;
            lv_obj_set_style_bg_color(s_can_dot, lv_color_hex(color), 0);
        }
    }
    if (s_sd_dot) {
        uint32_t color = state->system.sd_available ? C_OK : 0x444444;
        if (color != s_last_sd_color) {
            s_last_sd_color = color;
            lv_obj_set_style_bg_color(s_sd_dot, lv_color_hex(color), 0);
        }
    }

    /* Chamber. */
    int16_t chamber = state->temps.temp[0];
    bool chamber_changed;
    if (chamber != GUI_EMPTY) {
        snprintf(buf, sizeof(buf), "%.1f", chamber / 100.0);
        chamber_changed = txt(s_chamber_temp, buf);
    } else {
        chamber_changed = txt(s_chamber_temp, "--");
    }
    if (chamber_changed && s_chamber_unit && s_chamber_temp) {
        lv_obj_align_to(s_chamber_unit, s_chamber_temp,
                        LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -2);
    }

    /* Water in (amber) / water out (modelled, hot). */
    int16_t win = state->temps.temp[1];
    int16_t wout = state->temps.temp[2];
    if (win != GUI_EMPTY) {
        snprintf(buf, sizeof(buf), "%.1f °C", win / 100.0);
        txt(s_water_in, buf);
    } else {
        txt(s_water_in, "--");
    }
    if (wout != GUI_EMPTY) {
        snprintf(buf, sizeof(buf), "~%.1f °C", wout / 100.0);
        txt(s_water_out, buf);
    } else {
        txt(s_water_out, "--");
    }
    bool flow = state->pwm.duty_permille > 0 && state->pwm.duty_permille < 1000;
    txt(s_pump, flow ? "on" : "off");
    snprintf(buf, sizeof(buf), "Vpp %u", state->pwm.duty_permille);
    txt(s_pump_lbl, buf);

    /* Living room. */
    bool living_changed;
    if (state->living.temperature != GUI_EMPTY) {
        snprintf(buf, sizeof(buf), "%.1f", state->living.temperature / 100.0);
        living_changed = txt(s_living_temp, buf);
    } else {
        living_changed = txt(s_living_temp, "--");
    }
    if (living_changed && s_living_unit && s_living_temp) {
        lv_obj_align_to(s_living_unit, s_living_temp,
                        LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -2);
    }
    if (state->living.humidity) {
        snprintf(buf, sizeof(buf), "%u rh", state->living.humidity / 100);
        txt(s_living_rh, buf);
    } else {
        txt(s_living_rh, "-- rh");
    }
    if (state->living.pressure) {
        snprintf(buf, sizeof(buf), "%lu hPa",
                 (unsigned long)(state->living.pressure / 100));
        txt(s_living_pa, buf);
    } else {
        txt(s_living_pa, "-- hPa");
    }

    /* Power (always watts, with the bolt glyph). */
    bool power_changed;
    if (state->power.valid) {
        snprintf(buf, sizeof(buf), "%d", (int)state->power.watts);
        power_changed = txt(s_power_num, buf);
    } else {
        power_changed = txt(s_power_num, "--");
    }
    if (power_changed) align_power_labels();

    /* The flame is time-based, so paint it once here for callers that only
     * refresh the data (the host harness) and let the loop drive it after. */
    haus_ui_animate();
}

void haus_ui_animate(void) {
    const gui_haus_state_t *state = gui_get_state();
    const int16_t chamber = state->temps.temp[0];

    if (chamber == GUI_EMPTY) {
        for (int i = 0; i < 3; i++) {
            if (!s_flame[i]) continue;
            if (s_last_flame_opa[i] != 0) {
                s_last_flame_opa[i] = 0;
                lv_obj_set_style_opa(s_flame[i], LV_OPA_0, 0);
            }
        }
        return;
    }

    double t = (chamber / 100.0 - 20.0) / 60.0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    uint32_t warm = ((uint32_t)(0x30 + 0x1a * t) << 16) |
                    ((uint32_t)(0x20 + 0x14 * t) << 8) | 0x18;
    if (s_fire && warm != s_last_fire_grad) {
        s_last_fire_grad = warm;
        lv_obj_set_style_bg_grad_color(s_fire, lv_color_hex(warm), 0);
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

/* ─── Chart / detail header + range selector (shared) ─────────────────────── */

static void header_build(const char *title, const char *badge, uint32_t badge_color) {
    lv_obj_t *back = label_make(lv_screen_active(), LV_SYMBOL_LEFT, 14, 14,
                                &lv_font_montserrat_32, C_INK);
    (void)back;
    label_make(lv_screen_active(), title, 60, 18, &lv_font_montserrat_32, C_INK);

    if (badge) {
        lv_obj_t *b = lv_obj_create(lv_screen_active());
        lv_obj_set_size(b, LV_SIZE_CONTENT, 36);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(badge_color), 0);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_set_style_pad_hor(b, 10, 0);
        lv_obj_set_style_pad_ver(b, 4, 0);
        lv_obj_t *bl = label_make(b, badge, 0, 0, &lv_font_montserrat_16,
                                  badge_color);
        lv_obj_center(bl);
        lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -14, 14);
    }
}

static void range_build(int top) {
    lv_obj_t *seg = lv_obj_create(lv_screen_active());
    lv_obj_set_size(seg, 440, 44);
    lv_obj_set_pos(seg, 20, top);
    lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(seg, 1, 0);
    lv_obj_set_style_border_color(seg, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_radius(seg, 8, 0);
    lv_obj_set_style_pad_all(seg, 0, 0);
    obj_set_scrollable(seg, false);

    const char *names[3] = {"1 day", "1 week", "1 month"};
    int w = 440 / 3;
    for (int i = 0; i < 3; i++) {
        if (i > 0) {
            lv_obj_t *div = lv_obj_create(seg);
            lv_obj_set_size(div, 1, 44);
            lv_obj_set_pos(div, i * w, 0);
            lv_obj_set_style_bg_color(div, lv_color_hex(C_LINE), 0);
            lv_obj_set_style_border_width(div, 0, 0);
            obj_set_clickable(div, false);
        }
        lv_obj_t *l = label_make(seg, names[i], 0, 0,
                                 &lv_font_montserrat_16,
                                 i == 0 ? C_INK : C_DIM);
        lv_obj_set_width(l, w);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, i * w, 12);
        if (i == 0) {
            lv_obj_t *hl = lv_obj_create(seg);
            lv_obj_set_size(hl, w, 44);
            lv_obj_set_pos(hl, 0, 0);
            lv_obj_set_style_bg_color(hl, lv_color_hex(C_ACCENT), 0);
            lv_obj_set_style_bg_opa(hl, 32, 0);
            lv_obj_set_style_border_width(hl, 0, 0);
            lv_obj_set_style_radius(hl, 0, 0);
            lv_obj_move_background(hl);
        }
    }
}

/* Draw axes + history envelope for the chamber temperature. */
static void chart_build(int top, bool with_target) {
    const int w = 440, h = 260;
    lv_obj_t *c = lv_obj_create(lv_screen_active());
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, 20, top);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    obj_set_scrollable(c, false);

    /* axes */
    static lv_point_precise_t yax[2] = {{64, 12}, {64, 200}};
    static lv_point_precise_t xax[2] = {{64, 200}, {420, 200}};
    lv_obj_t *yl = lv_line_create(c);
    lv_line_set_points(yl, yax, 2);
    lv_obj_set_style_line_color(yl, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_line_width(yl, 1, 0);
    lv_obj_t *xl = lv_line_create(c);
    lv_line_set_points(xl, xax, 2);
    lv_obj_set_style_line_color(xl, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_line_width(xl, 1, 0);

    label_make(c, "60", 8, 30, &lv_font_montserrat_16, C_DIM);
    label_make(c, "40", 8, 112, &lv_font_montserrat_16, C_DIM);
    label_make(c, "20", 8, 188, &lv_font_montserrat_16, C_DIM);
    label_make(c, "00", 56, 214, &lv_font_montserrat_16, C_DIM);
    label_make(c, "06", 148, 214, &lv_font_montserrat_16, C_DIM);
    label_make(c, "12", 226, 214, &lv_font_montserrat_16, C_DIM);
    label_make(c, "18", 306, 214, &lv_font_montserrat_16, C_DIM);
    label_make(c, "24", 390, 214, &lv_font_montserrat_16, C_DIM);

    if (with_target) {
        static lv_point_precise_t tp[2] = {{64, 36}, {420, 36}};
        lv_obj_t *tl = lv_line_create(c);
        lv_line_set_points(tl, tp, 2);
        lv_obj_set_style_line_color(tl, lv_color_hex(C_DIM), 0);
        lv_obj_set_style_line_width(tl, 1, 0);
        lv_obj_set_style_line_dash_width(tl, 6, 0);
        lv_obj_set_style_line_dash_gap(tl, 6, 0);
    }

    /* History envelope from the shared data interface. */
    const gui_haus_state_t *st = gui_get_state();
    static gui_sample_t samples[440];
    static lv_point_precise_t pts[440];
    uint16_t count = 0;
    gui_history_request_t req = {
        .start_epoch = st->time.timestamp - 86400u,
        .end_epoch = st->time.timestamp,
        .samples = samples,
        .max_samples = 440,
        .out_count = &count,
    };
    gui_get_history(&req);

    if (count > 1) {
        for (uint16_t i = 0; i < count; i++) {
            double x = 64.0 + ((double)i / (count - 1)) * 356.0;
            int16_t t = samples[i].temp[0];
            double y = 200.0;
            if (t != GUI_EMPTY) {
                double cc = t / 100.0;
                y = 200.0 - ((cc - 15.0) / 65.0) * 188.0;
                if (y < 12.0) y = 12.0;
                if (y > 200.0) y = 200.0;
            }
            pts[i].x = (lv_value_precise_t)x;
            pts[i].y = (lv_value_precise_t)y;
        }
        lv_obj_t *line = lv_line_create(c);
        lv_line_set_points(line, pts, count);
        lv_obj_set_style_line_color(line, lv_color_hex(C_COLD), 0);
        lv_obj_set_style_line_width(line, 2, 0);
    }
}

lv_obj_t *haus_ui_build_chart(void) {
    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);

    header_build("CHAMBER", "1 d", C_OK);
    const gui_haus_state_t *st = gui_get_state();
    char buf[32];
    if (st->temps.temp[0] != GUI_EMPTY)
        snprintf(buf, sizeof(buf), "%.1f °C", st->temps.temp[0] / 100.0);
    else
        snprintf(buf, sizeof(buf), "--");
    label_make(lv_screen_active(), buf, 20, 56, &lv_font_montserrat_48, C_COLD);

    range_build(120);
    label_make(lv_screen_active(), "above target  --", 20, 172,
               &lv_font_montserrat_16, C_DIM);
    chart_build(204, false);

    label_make(lv_screen_active(), "min --    max --    mean --",
               20, 476, &lv_font_montserrat_16, C_DIM);

    nav_build(1);
    return root;
}

lv_obj_t *haus_ui_build_detail(void) {
    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);

    header_build("CHAMBER", "RESTORED", C_DIM);
    const gui_haus_state_t *st = gui_get_state();
    char buf[32];
    if (st->temps.temp[0] != GUI_EMPTY)
        snprintf(buf, sizeof(buf), "%.1f °C", st->temps.temp[0] / 100.0);
    else
        snprintf(buf, sizeof(buf), "--");
    label_make(lv_screen_active(), buf, 20, 56, &lv_font_montserrat_48, C_COLD);

    range_build(120);
    snprintf(buf, sizeof(buf), "target %.1f °C (dashed)",
             st->chamber_target_c / 100.0);
    label_make(lv_screen_active(), buf, 20, 172,
               &lv_font_montserrat_16, C_ACCENT);
    chart_build(204, true);

    label_make(lv_screen_active(), "min --   mean --   max --   now --",
               20, 476, &lv_font_montserrat_16, C_DIM);
    label_make(lv_screen_active(), "data age --", 20, 500,
               &lv_font_montserrat_16, C_FAINT);

    nav_build(1);
    return root;
}

void haus_ui_deinit(void) {
    lv_obj_t *root = lv_screen_active();
    while (lv_obj_get_child_cnt(root) > 0) {
        lv_obj_delete(lv_obj_get_child(root, 0));
    }
    s_status_date = s_clock = s_can_dot = s_sd_dot = NULL;
    s_cabinet = s_fire = NULL;
    memset(s_flame, 0, sizeof(s_flame));
    s_chamber_temp = s_chamber_unit = s_water_in = s_water_in_lbl = NULL;
    s_pump = s_pump_lbl = s_water_out = s_water_out_lbl = NULL;
    s_living_temp = s_living_unit = s_living_rh = s_living_pa = NULL;
    s_power_num = s_power_bolt = s_power_src = NULL;
    s_last_can_color = s_last_sd_color = s_last_fire_grad = 0xFFFFFFFFu;
    memset(s_last_flame_opa, 0xFF, sizeof(s_last_flame_opa));
}
