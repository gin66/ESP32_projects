/**
 * @file haus_ui.h
 * @brief LVGL screen construction for the 28_hauszentrale panel.
 *
 * This is the productive UI, shared by the ESP32 firmware and the macOS host
 * harness (host_harness/). Both build the SAME source file
 * (src/ui/haus_ui.c) against the data interface in haus_gui.h, so whatever the
 * harness renders is exactly what the device runs.
 *
 * Screens are 800x480 landscape. Every screen except the log screen carries
 * the right-hand navigation rail (x=712, w=88, six vertical tabs).
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

#ifdef UI_SMOKE_TEST
void haus_ui_smoke_step(void);
#endif

/** Build the power and living-room detail screens. */
lv_obj_t* haus_ui_build_power(void);
lv_obj_t* haus_ui_build_living(void);
lv_obj_t* haus_ui_build_ac(void);
void haus_ui_set_ac_series(int series);

/** Power chart scale: -1 auto, 0 W, 1 kW, 2 logarithmic (0 W special). */
void haus_ui_set_power_scale(int mode);
lv_obj_t* haus_ui_build_settings(void);
lv_obj_t* haus_ui_build_storage(void);
lv_obj_t* haus_ui_build_can(void);
lv_obj_t* haus_ui_build_roles(void);
lv_obj_t* haus_ui_build_sml(void);
lv_obj_t* haus_ui_build_heater_cfg(void);

/** Build the full-bleed log screen (S15). */
lv_obj_t* haus_ui_build_log(void);

/** Delete every object the UI created. */
void haus_ui_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_UI_H */
