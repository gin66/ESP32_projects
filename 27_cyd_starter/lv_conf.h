/**
 * Minimal lv_conf.h for 27_cyd_starter
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*Color depth: 1 (1 byte per pixel), 16 (2 bytes), 24 (3 bytes)*/
#define LV_COLOR_DEPTH 16

/*Enable TFT_eSPI driver*/
#define LV_USE_TFT_ESPI 1

/*Size of the memory available for `lv_obj_set_style()` and `lv_obj_set_ext_click_area()`
  in bytes (e.g. 100MB: 100 * 1024 * 1024)*/
#define LV_DRAW_BUF_SIZE (320 * 480 / 10 * (LV_COLOR_DEPTH / 8))

/*Use custom (extenal) formatter for lv_snprintf*/
#define LV_SPRINTF_USE_FLOAT 0

#endif /*LV_CONF_H*/
