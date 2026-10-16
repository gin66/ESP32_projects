/**
 * @file haus_ui.h
 * @brief Home screen UI for the 28_hauszentrale host harness.
 *
 * Builds the full 480×800 home screen from gui_get_state() data.
 * Consumed by host_harness/src/main.c.
 *
 * Screen layout (480×800):
 *   y=0..47   Status strip (time, CAN/SD dots)
 *   y=48..120 Clock label (HH:MM, large)
 *   y=120..320 Heater cabinet (firebox + 3 flame arcs)
 *   y=320..380 Pipes (in/out with flow dashes)
 *   y=380..456 Living room strip (T/RH/P)
 *   y=456..532 Power band (watts + source badge)
 *   y=532..620 Navigation pane (7 icon tabs, 68px each)
 *   y=620..800 Overlay zone buttons (5 zones, invisible)
 */

#ifndef HAUS_UI_H
#define HAUS_UI_H

#include <lvgl.h>

/**
 * Initialize the home screen UI.
 *
 * Creates all LVGL objects and sets up the layout.
 * Must be called after lv_init() and display creation.
 *
 * @return lv_obj_t* root container (set as screen active or child)
 */
lv_obj_t *haus_ui_init(void);

/**
 * Update the home screen with fresh state from gui_get_state().
 *
 * Called from the main loop (~1 Hz) to refresh labels, colors, and animations.
 * Does NOT recreate objects — only updates properties.
 */
void haus_ui_update(void);

/**
 * Deinitialize the home screen UI.
 *
 * Deletes all LVGL objects created by haus_ui_init().
 * Called before lv_deinit().
 */
void haus_ui_deinit(void);

#endif /* HAUS_UI_H */
