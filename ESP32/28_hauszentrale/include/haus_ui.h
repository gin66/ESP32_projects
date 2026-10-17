/**
 * @file haus_ui.h
 * @brief LVGL screen construction for the 28_hauszentrale panel.
 *
 * This is the productive UI, shared by the ESP32 firmware and the macOS host
 * harness (host_harness/). Both build the SAME source file
 * (src/ui/haus_ui.c) against the data interface in haus_gui.h, so whatever the
 * harness renders is exactly what the device runs.
 *
 * Screens are 480x800 portrait. Every screen except the log screen carries the
 * smartphone-style bottom navigation pane (y=712, h=88).
 */

#ifndef HAUS_UI_H
#define HAUS_UI_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build the home screen onto the active screen.
 * Returns the root object (== lv_screen_active()).
 */
lv_obj_t* haus_ui_init(void);

/** Refresh the home screen from gui_get_state() (~1 Hz). */
void haus_ui_update(void);

/** Advance the flame/breathing animation (~20 Hz). */
void haus_ui_animate(void);

/** Build the chamber detail screen (target line, readouts). */
lv_obj_t* haus_ui_build_detail(void);

/** Build the combined water screen (water in / water out / Soll). */
lv_obj_t* haus_ui_build_water(int nav_index);

/** Select the chart range: 0 = 1 day, 1 = 1 week, 2 = 1 month. */
void haus_ui_set_range(int range);

/** Three-finger gesture: return to Home from anywhere. */
void haus_ui_three_finger(void);

/** Content-dimming screensaver (S26.06 / D43). The backlight stays lit; any
 * touch should call haus_ui_set_dimmed(false). */
void haus_ui_set_dimmed(bool dimmed);
bool haus_ui_dimmed(void);

#ifdef UI_SMOKE_TEST
void haus_ui_smoke_step(void);
#endif

/** Build the power and living-room detail screens. */
lv_obj_t* haus_ui_build_power(void);
lv_obj_t* haus_ui_build_living(void);
lv_obj_t* haus_ui_build_settings(void);
lv_obj_t* haus_ui_build_storage(void);
lv_obj_t* haus_ui_build_can(void);

/** Build the full-bleed log screen (S15). */
lv_obj_t* haus_ui_build_log(void);

/** Delete every object the UI created. */
void haus_ui_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_UI_H */
