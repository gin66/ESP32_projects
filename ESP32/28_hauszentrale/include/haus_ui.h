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
lv_obj_t *haus_ui_init(void);

/** Refresh the home screen from gui_get_state() (~1 Hz). */
void haus_ui_update(void);

/** Advance the flame/breathing animation (~20 Hz). */
void haus_ui_animate(void);

/** Build the chamber envelope/history screen. */
lv_obj_t *haus_ui_build_chart(void);

/** Build the chamber detail screen (target line, readouts). */
lv_obj_t *haus_ui_build_detail(void);

/** Delete every object the UI created. */
void haus_ui_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_UI_H */
