/**
 * @file haus_ui.c
 * @brief Home screen UI for the 28_hauszentrale host harness.
 *
 * Builds the full 480×800 home screen from gui_get_state() data.
 * Uses only LVGL primitives (no canvas, no SDL window — headless PNG export).
 *
 * Screen layout (480×800):
 *   y=0..47   Status strip (date, time, CAN/SD dots)
 *   y=48..120 Clock label (HH:MM, large)
 *   y=120..320 Heater cabinet (firebox + 3 flame arcs)
 *   y=320..380 Pipes (in/out with flow dashes)
 *   y=380..456 Living room strip (T/RH/P)
 *   y=456..532 Power band (watts + source badge)
 *   y=532..620 Navigation pane (7 icon tabs, 68px each)
 *   y=620..800 Overlay zone buttons (5 zones, invisible)
 *
 * Consumed by host_harness/src/main.c.
 * On-device version will be src/ui/haus_ui.cpp.
 */

#define _POSIX_C_SOURCE 200809L

#include "haus_ui.h"
#include "haus_gui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ─── Screen dimensions ───────────────────────────────────────────────────── */

#define SCREEN_W 480
#define SCREEN_H 800

/* ─── Layout constants ────────────────────────────────────────────────────── */

#define STATUS_H      48
#define CLOCK_Y       52
#define CLOCK_H       72
#define HEATER_Y      130
#define HEATER_H      200
#define PIPE_Y        340
#define PIPE_H        50
#define LIVING_Y      400
#define LIVING_H      60
#define POWER_Y       470
#define POWER_H       60
#define NAV_Y         540
#define NAV_H         80
#define ZONE_Y        630
#define ZONE_H        170

/* ─── Color constants ─────────────────────────────────────────────────────── */

#define COLOR_BG        0x1a1a2e  /* dark background */
#define COLOR_PANEL     0x16213e  /* panel background */
#define COLOR_ACCENT    0xe94560  /* accent (red-pink) */
#define COLOR_TEXT      0xffffff  /* white text */
#define COLOR_TEXT_DIM  0x888888  /* dim text */
#define COLOR_GREY      0x666666  /* grey (sentinel) */
#define COLOR_CAN_GREEN 0x00ff88  /* CAN OK */
#define COLOR_CAN_RED   0xff4444  /* CAN error */
#define COLOR_SD_GREEN  0x88ff88  /* SD OK */
#define COLOR_SD_AMBER  0xffaa44  /* SD low space */

/* Pipe colors (blue → amber → red based on temperature) */
static lv_color_t pipe_color_for_temp(int16_t temp_c100) {
    if (temp_c100 == GUI_EMPTY) return lv_color_hex(COLOR_GREY);
    double c = temp_c100 / 100.0;
    if (c < 30.0) return lv_color_hex(0x4488ff);  /* blue: cold */
    if (c < 45.0) return lv_color_hex(0x44ccff);  /* light blue */
    if (c < 60.0) return lv_color_hex(0xffcc44);  /* amber */
    return lv_color_hex(0xff4444);                  /* red: hot */
}

/* ─── Global UI state ─────────────────────────────────────────────────────── */

/* Status strip */
static lv_obj_t *s_status_bar = NULL;
static lv_obj_t *s_clock_label = NULL;
static lv_obj_t *s_can_dot = NULL;
static lv_obj_t *s_sd_dot = NULL;

/* Heater cabinet */
static lv_obj_t *s_heater_bg = NULL;
static lv_obj_t *s_firebox = NULL;
static lv_obj_t *s_flame[3] = {NULL, NULL, NULL};

/* Pipes */
static lv_obj_t *s_pipe[3] = {NULL, NULL, NULL};
static lv_obj_t *s_flow_dash[3][6] = {{NULL}};

/* Living room strip */
static lv_obj_t *s_living_temp = NULL;
static lv_obj_t *s_living_rh = NULL;
static lv_obj_t *s_living_pres = NULL;

/* Power band */
static lv_obj_t *s_power_watts = NULL;
static lv_obj_t *s_power_src = NULL;

/* Navigation pane */
static lv_obj_t *s_nav_tabs[7] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static int s_current_tab = 0;

/* Overlay zones (invisible buttons for touch) */
static lv_obj_t *s_zones[5] = {NULL, NULL, NULL, NULL, NULL};

/* Animation state (updated each frame) */
static double s_breath_phase = 0.0;  /* 0..2π for 6s breathing cycle */

/* ─── Helpers ─────────────────────────────────────────────────────────────── */

static void set_text_safe(lv_obj_t *label, const char *text) {
    if (label && text) {
        lv_label_set_text(label, text);
    }
}

static void set_visible(lv_obj_t *obj, bool visible) {
    if (obj) {
        if (visible) {
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* Map temperature (c° × 100) to firebox color (dark → bright orange) */
static lv_color_t firebox_color(int16_t temp_c100) {
    if (temp_c100 == GUI_EMPTY) return lv_color_hex(0x2a1a0a);  /* dark */
    double c = temp_c100 / 100.0;
    if (c < 20.0) c = 20.0;
    if (c > 80.0) c = 80.0;
    double t = (c - 20.0) / 60.0;  /* 0..1 */
    uint8_t r = (uint8_t)(40.0 + 180.0 * t);
    uint8_t g = (uint8_t)(20.0 + 100.0 * t);
    uint8_t b = (uint8_t)(10.0 + 20.0 * t);
    return lv_color_hex((r << 16) | (g << 8) | b);
}

/* Map temperature to flame brightness (0.0..1.0) */
static double flame_brightness(int16_t temp_c100) {
    if (temp_c100 == GUI_EMPTY) return 0.0;
    double c = temp_c100 / 100.0;
    if (c < 20.0) c = 20.0;
    if (c > 80.0) c = 80.0;
    return (c - 20.0) / 60.0;
}

/* ─── Status strip (y=0..47) ─────────────────────────────────────────────── */

static void build_status_bar(void) {
    s_status_bar = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_status_bar, SCREEN_W, STATUS_H);
    lv_obj_set_pos(s_status_bar, 0, 0);
    lv_obj_set_style_bg_color(s_status_bar, lv_color_hex(0x0f0f23), 0);
    lv_obj_set_style_border_width(s_status_bar, 0, 0);
    lv_obj_set_scrollbar_mode(s_status_bar, LV_SCROLLBAR_MODE_OFF);

    /* Clock label (centered, large) */
    s_clock_label = lv_label_create(s_status_bar);
    lv_obj_align(s_clock_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(s_clock_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_clock_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_align(s_clock_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_clock_label, "--:--");

    /* CAN dot (left) */
    s_can_dot = lv_obj_create(s_status_bar);
    lv_obj_set_size(s_can_dot, 12, 12);
    lv_obj_set_pos(s_can_dot, 16, STATUS_H / 2 - 6);
    lv_obj_set_style_bg_color(s_can_dot, lv_color_hex(COLOR_CAN_RED), 0);
    lv_obj_set_style_radius(s_can_dot, 6, 0);
    lv_obj_set_style_border_width(s_can_dot, 0, 0);

    /* SD dot (right) */
    s_sd_dot = lv_obj_create(s_status_bar);
    lv_obj_set_size(s_sd_dot, 12, 12);
    lv_obj_set_pos(s_sd_dot, SCREEN_W - 28, STATUS_H / 2 - 6);
    lv_obj_set_style_bg_color(s_sd_dot, lv_color_hex(COLOR_SD_GREEN), 0);
    lv_obj_set_style_radius(s_sd_dot, 6, 0);
    lv_obj_set_style_border_width(s_sd_dot, 0, 0);
}

/* ─── Heater cabinet (y=120..319) ─────────────────────────────────────────── */

static void build_heater(void) {
    /* Cabinet outline (rounded rectangle) */
    s_heater_bg = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_heater_bg, 300, HEATER_H);
    lv_obj_set_pos(s_heater_bg, (SCREEN_W - 300) / 2, HEATER_Y);
    lv_obj_set_style_bg_color(s_heater_bg, lv_color_hex(0x2a2a3e), 0);
    lv_obj_set_style_border_color(s_heater_bg, lv_color_hex(0x444466), 0);
    lv_obj_set_style_border_width(s_heater_bg, 2, 0);
    lv_obj_set_style_radius(s_heater_bg, 12, 0);
    lv_obj_set_style_pad_all(s_heater_bg, 0, 0);

    /* Firebox (inside cabinet) */
    s_firebox = lv_obj_create(s_heater_bg);
    lv_obj_set_size(s_firebox, 200, 120);
    lv_obj_align(s_firebox, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(s_firebox, lv_color_hex(0x2a1a0a), 0);
    lv_obj_set_style_radius(s_firebox, 8, 0);
    lv_obj_set_style_border_width(s_firebox, 0, 0);

    /* 3 flame arcs (stacked, inside firebox) */
    for (int i = 0; i < 3; i++) {
        s_flame[i] = lv_obj_create(s_firebox);
        lv_obj_set_size(s_flame[i], 160 - i * 30, 30);
        lv_obj_align(s_flame[i], LV_ALIGN_BOTTOM_MID, 0, -5 - i * 25);
        lv_obj_set_style_bg_color(s_flame[i], lv_color_hex(0xff8800), 0);
        lv_obj_set_style_radius(s_flame[i], 15, 0);
        lv_obj_set_style_border_width(s_flame[i], 0, 0);
        lv_obj_set_style_opa(s_flame[i], LV_OPA_50, 0);
    }
}

/* ─── Pipes (y=340..389) ─────────────────────────────────────────────────── */

static void build_pipes(void) {
    /* 3 pipes (horizontal lines) */
    for (int i = 0; i < 3; i++) {
        s_pipe[i] = lv_obj_create(lv_screen_active());
        lv_obj_set_size(s_pipe[i], 280, 6);
        lv_obj_set_pos(s_pipe[i], (SCREEN_W - 280) / 2, PIPE_Y + i * 18);
        lv_obj_set_style_bg_color(s_pipe[i], lv_color_hex(0x4488ff), 0);
        lv_obj_set_style_radius(s_pipe[i], 3, 0);
        lv_obj_set_style_border_width(s_pipe[i], 0, 0);
    }

    /* Flow dashes (6 per pipe) */
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 6; j++) {
            s_flow_dash[i][j] = lv_obj_create(lv_screen_active());
            lv_obj_set_size(s_flow_dash[i][j], 12, 4);
            lv_obj_set_pos(s_flow_dash[i][j],
                           (SCREEN_W - 280) / 2 + 20 + j * 42,
                           PIPE_Y + i * 18 + 1);
            lv_obj_set_style_bg_color(s_flow_dash[i][j], lv_color_hex(0xffffff), 0);
            lv_obj_set_style_radius(s_flow_dash[i][j], 2, 0);
            lv_obj_set_style_border_width(s_flow_dash[i][j], 0, 0);
            lv_obj_set_style_opa(s_flow_dash[i][j], LV_OPA_0, 0);  /* hidden */
        }
    }
}

/* ─── Living room strip (y=400..459) ──────────────────────────────────────── */

static void build_living(void) {
    s_living_temp = lv_label_create(lv_screen_active());
    lv_obj_set_pos(s_living_temp, 20, LIVING_Y);
    lv_obj_set_style_text_font(s_living_temp, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_living_temp, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(s_living_temp, "T: --");

    s_living_rh = lv_label_create(lv_screen_active());
    lv_obj_set_pos(s_living_rh, 140, LIVING_Y);
    lv_obj_set_style_text_font(s_living_rh, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_living_rh, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(s_living_rh, "RH: --");

    s_living_pres = lv_label_create(lv_screen_active());
    lv_obj_set_pos(s_living_pres, 260, LIVING_Y);
    lv_obj_set_style_text_font(s_living_pres, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_living_pres, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(s_living_pres, "P: --");
}

/* ─── Power band (y=470..529) ─────────────────────────────────────────────── */

static void build_power(void) {
    s_power_watts = lv_label_create(lv_screen_active());
    lv_obj_set_pos(s_power_watts, 20, POWER_Y);
    lv_obj_set_style_text_font(s_power_watts, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_power_watts, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(s_power_watts, "W: --");

    s_power_src = lv_label_create(lv_screen_active());
    lv_obj_set_pos(s_power_src, 200, POWER_Y);
    lv_obj_set_style_text_font(s_power_src, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_power_src, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_label_set_text(s_power_src, "SML");
}

/* ─── Navigation pane (y=540..619) ────────────────────────────────────────── */

static const char *nav_icons[] = {
    LV_SYMBOL_HOME,       /* 0: Home */
    LV_SYMBOL_EYE_OPEN,   /* 1: Chambers */
    LV_SYMBOL_PLAY,       /* 2: Water */
    LV_SYMBOL_CHARGE,     /* 3: Power */
    LV_SYMBOL_SETTINGS,   /* 4: Living */
    LV_SYMBOL_SAVE,       /* 5: Logs */
    LV_SYMBOL_SHUFFLE,    /* 6: Settings/Gear */
};

static void build_nav(void) {
    /* Nav background */
    lv_obj_t *nav_bg = lv_obj_create(lv_screen_active());
    lv_obj_set_size(nav_bg, SCREEN_W, NAV_H);
    lv_obj_set_pos(nav_bg, 0, NAV_Y);
    lv_obj_set_style_bg_color(nav_bg, lv_color_hex(0x0f0f23), 0);
    lv_obj_set_style_border_color(nav_bg, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(nav_bg, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(nav_bg, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_scrollbar_mode(nav_bg, LV_SCROLLBAR_MODE_OFF);

    /* 7 icon tabs (68px each) */
    for (int i = 0; i < 7; i++) {
        s_nav_tabs[i] = lv_label_create(nav_bg);
        lv_obj_set_pos(s_nav_tabs[i], 16 + i * 68, 16);
        lv_obj_set_style_text_font(s_nav_tabs[i], &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(s_nav_tabs[i], lv_color_hex(COLOR_TEXT_DIM), 0);
        lv_label_set_text(s_nav_tabs[i], nav_icons[i]);
    }

    /* Active tab accent line (bottom) */
    lv_obj_t *accent = lv_obj_create(nav_bg);
    lv_obj_set_size(accent, 68, 3);
    lv_obj_set_pos(accent, 16, NAV_H - 3);
    lv_obj_set_style_bg_color(accent, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_radius(accent, 1, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_user_data(accent, "accent");  /* mark for update */
}

/* ─── Overlay zones (y=630..800, invisible) ───────────────────────────────── */

static void build_zones(void) {
    for (int i = 0; i < 5; i++) {
        s_zones[i] = lv_obj_create(lv_screen_active());
        lv_obj_set_size(s_zones[i], SCREEN_W / 5, ZONE_H);
        lv_obj_set_pos(s_zones[i], i * (SCREEN_W / 5), ZONE_Y);
        lv_obj_set_style_bg_color(s_zones[i], lv_color_hex(0x00000000), 0);  /* invisible */
        lv_obj_set_style_border_width(s_zones[i], 0, 0);
        lv_obj_clear_flag(s_zones[i], LV_OBJ_FLAG_CLICKABLE);  /* not clickable by default */
    }
    /* Make zones clickable for touch */
    for (int i = 0; i < 5; i++) {
        lv_obj_add_flag(s_zones[i], LV_OBJ_FLAG_CLICKABLE);
    }
}

/* ─── Update functions ────────────────────────────────────────────────────── */

static void update_status(const gui_haus_state_t *state) {
    /* Clock: HH:MM from timestamp */
    if (state->time.synced) {
        uint32_t t = state->time.timestamp % 86400;
        uint8_t hh = t / 3600;
        uint8_t mm = (t % 3600) / 60;
        char buf[8];
        snprintf(buf, sizeof(buf), "%02u:%02u", hh, mm);
        set_text_safe(s_clock_label, buf);
    } else {
        set_text_safe(s_clock_label, "--:--");
    }

    /* CAN dot */
    if (s_can_dot) {
        lv_color_t c = state->system.can_available
                       ? lv_color_hex(COLOR_CAN_GREEN)
                       : lv_color_hex(COLOR_CAN_RED);
        lv_obj_set_style_bg_color(s_can_dot, c, 0);
    }

    /* SD dot */
    if (s_sd_dot) {
        lv_color_t c = state->system.sd_available
                       ? (state->system.sd_low_space
                          ? lv_color_hex(COLOR_SD_AMBER)
                          : lv_color_hex(COLOR_SD_GREEN))
                       : lv_color_hex(0x444444);
        lv_obj_set_style_bg_color(s_sd_dot, c, 0);
    }
}

static void update_heater(const gui_haus_state_t *state) {
    int16_t chamber = state->temps.temp[0];

    /* Firebox color from chamber temperature */
    if (s_firebox) {
        lv_obj_set_style_bg_color(s_firebox, firebox_color(chamber), 0);
    }

    /* 3 flame arcs — brightness from chamber temp, breathing animation */
    for (int i = 0; i < 3; i++) {
        if (s_flame[i]) {
            double bright = flame_brightness(chamber);
            /* Breathing: sinusoidal variation, 6s cycle (phase per flame) */
            double breath = 0.5 + 0.5 * sin(s_breath_phase + i * 2.094);  /* 2π/3 offset */
            double alpha = bright * (0.3 + 0.7 * breath);  /* 0.3..1.0 range */
            lv_obj_set_style_opa(s_flame[i],
                                 (uint8_t)(alpha * 255), 0);
            /* Flame color: orange → yellow based on temp */
            if (chamber != GUI_EMPTY) {
                double c = chamber / 100.0;
                if (c > 50.0) {
                    lv_obj_set_style_bg_color(s_flame[i], lv_color_hex(0xffcc44), 0);
                } else {
                    lv_obj_set_style_bg_color(s_flame[i], lv_color_hex(0xff8800), 0);
                }
            } else {
                lv_obj_set_style_bg_color(s_flame[i], lv_color_hex(0x333333), 0);
                lv_obj_set_style_opa(s_flame[i], LV_OPA_0, 0);
            }
        }
    }
}

static void update_pipes(const gui_haus_state_t *state) {
    /* 3 pipes: chamber (i=0), water-in (i=1), water-out (i=2) */
    for (int i = 0; i < 3; i++) {
        int16_t temp = state->temps.temp[i];
        if (s_pipe[i]) {
            lv_obj_set_style_bg_color(s_pipe[i], pipe_color_for_temp(temp), 0);
        }
        /* Flow dashes: visible only when duty > 0 (flow present) */
        bool has_flow = state->pwm.duty_permille > 0 && state->pwm.duty_permille < 1000;
        for (int j = 0; j < 6; j++) {
            if (s_flow_dash[i][j]) {
                if (has_flow) {
                    /* Animate dashes: shift position based on phase */
                    double offset = sin(s_breath_phase * 2.0 + j * 1.047) * 10.0;
                    lv_obj_set_pos(s_flow_dash[i][j],
                                   (SCREEN_W - 280) / 2 + 20 + j * 42 + (int)offset,
                                   PIPE_Y + i * 18 + 1);
                    lv_obj_set_style_opa(s_flow_dash[i][j], LV_OPA_70, 0);
                } else {
                    lv_obj_set_style_opa(s_flow_dash[i][j], LV_OPA_0, 0);
                }
            }
        }
    }
}

static void update_living(const gui_haus_state_t *state) {
    char buf[32];

    /* Temperature */
    if (state->living.temperature == GUI_EMPTY) {
        set_text_safe(s_living_temp, "T: -");
    } else {
        snprintf(buf, sizeof(buf), "T: %4.1f°C",
                 state->living.temperature / 100.0);
        set_text_safe(s_living_temp, buf);
    }

    /* Humidity */
    if (state->living.humidity == 0) {
        set_text_safe(s_living_rh, "RH: -");
    } else {
        snprintf(buf, sizeof(buf), "RH: %u%%", state->living.humidity);
        set_text_safe(s_living_rh, buf);
    }

    /* Pressure */
    if (state->living.pressure == 0) {
        set_text_safe(s_living_pres, "P: -");
    } else {
        snprintf(buf, sizeof(buf), "P: %lu", state->living.pressure / 100);
        set_text_safe(s_living_pres, buf);
    }
}

static void update_power(const gui_haus_state_t *state) {
    char buf[32];

    /* Watts */
    if (!state->power.valid) {
        set_text_safe(s_power_watts, "W: -");
    } else {
        double kw = state->power.watts / 1000.0;
        if (kw >= 1.0) {
            snprintf(buf, sizeof(buf), "%.1f kW", kw);
        } else {
            snprintf(buf, sizeof(buf), "%d W", state->power.watts);
        }
        set_text_safe(s_power_watts, buf);
    }

    /* Source badge */
    switch (state->power.source) {
        case GUI_PWR_SRC_SML:
            set_text_safe(s_power_src, "SML");
            break;
        case GUI_PWR_SRC_ADC:
            set_text_safe(s_power_src, "ADC");
            break;
        default:
            set_text_safe(s_power_src, "-");
            break;
    }
}

static void update_nav(int tab) {
    if (tab == s_current_tab) return;
    s_current_tab = tab;

    for (int i = 0; i < 7; i++) {
        if (s_nav_tabs[i]) {
            lv_color_t c = (i == tab) ? lv_color_hex(COLOR_ACCENT)
                                      : lv_color_hex(COLOR_TEXT_DIM);
            lv_obj_set_style_text_color(s_nav_tabs[i], c, 0);
        }
    }

    /* Move accent line to active tab */
    lv_obj_t *root = lv_screen_active();
    for (lv_obj_t *obj = lv_obj_get_child(root, 0); obj;
         obj = lv_obj_get_child(obj, 0)) {
        const char *ud = lv_obj_get_user_data(obj);
        if (ud && strcmp(ud, "accent") == 0) {
            lv_obj_set_pos(obj, 16 + tab * 68, NAV_H - 3);
            break;
        }
    }
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

lv_obj_t *haus_ui_init(void) {
    lv_obj_t *root = lv_screen_active();

    /* Background */
    lv_obj_set_style_bg_color(root, lv_color_hex(COLOR_BG), 0);

    build_status_bar();
    build_heater();
    build_pipes();
    build_living();
    build_power();
    build_nav();
    build_zones();

    return root;
}

void haus_ui_update(void) {
    const gui_haus_state_t *state = gui_get_state();

    /* Advance breathing phase (6s cycle at ~1Hz = 2π/6 per call) */
    s_breath_phase += 2.094;  /* 2π / 6 */
    if (s_breath_phase > 6.283) s_breath_phase -= 6.283;

    update_status(state);
    update_heater(state);
    update_pipes(state);
    update_living(state);
    update_power(state);
}

void haus_ui_deinit(void) {
    /* Delete all objects by recreating the screen */
    lv_obj_t *root = lv_screen_active();
    /* Delete all children */
    while (lv_obj_get_child_cnt(root) > 0) {
        lv_obj_delete(lv_obj_get_child(root, 0));
    }
    /* Reset globals */
    s_status_bar = s_clock_label = s_can_dot = s_sd_dot = NULL;
    s_heater_bg = s_firebox = NULL;
    memset(s_flame, 0, sizeof(s_flame));
    memset(s_pipe, 0, sizeof(s_pipe));
    memset(s_flow_dash, 0, sizeof(s_flow_dash));
    s_living_temp = s_living_rh = s_living_pres = NULL;
    s_power_watts = s_power_src = NULL;
    memset(s_nav_tabs, 0, sizeof(s_nav_tabs));
    memset(s_zones, 0, sizeof(s_zones));
    s_current_tab = 0;
    s_breath_phase = 0.0;
}
