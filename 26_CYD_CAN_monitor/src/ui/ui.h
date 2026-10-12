#ifndef EEZ_LVGL_UI_GUI_H
#define EEZ_LVGL_UI_GUI_H

#include <lvgl.h>

/* C-compatible CYD screen dimensions — mirrors tpl_display.h for
 * ui.c/screens.c. screens.h uses CYD_SCREEN_WIDTH/HEIGHT (conflict-free)
 * instead of TFT_WIDTH/TFT_HEIGHT (owned by TFT_eSPI library).
 */
#if defined(IS_ESP32_CYD_35)
#define CYD_SCREEN_WIDTH 320
#define CYD_SCREEN_HEIGHT 480
#else
#define CYD_SCREEN_WIDTH 240
#define CYD_SCREEN_HEIGHT 320
#endif

#if defined(EEZ_FOR_LVGL)
#include <eez/flow/lvgl_api.h>
#endif

#if !defined(EEZ_FOR_LVGL)
#include "screens.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void ui_init();
void ui_tick();

#if !defined(EEZ_FOR_LVGL)
void loadScreen(enum ScreensEnum screenId);
#endif

#ifdef __cplusplus
}
#endif

#endif  // EEZ_LVGL_UI_GUI_H
