#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Row layout constants */
#define ROW_HEIGHT 18
#define HEADER_Y 58
#define ROW_BUFFER 4  /* px safety margin at bottom */

/* Logical height after LVGL rotation (portrait = TFT_HEIGHT, landscape = TFT_WIDTH).
 * 2.8" (240×320, 270°): (240 - 58 - 4) / 18 = 9  rows
 * 3.5" (320×480,   0°): (480 - 58 - 4) / 18 = 23 rows
 */
#define LOGICAL_HEIGHT (TFT_ROTATION == 0 || TFT_ROTATION == 180 ? TFT_HEIGHT : TFT_WIDTH)
#define MAX_CAN_IDS ((LOGICAL_HEIGHT - HEADER_Y - ROW_BUFFER) / ROW_HEIGHT)

typedef struct _objects_t {
    lv_obj_t* main;
    lv_obj_t* lblIP;
    lv_obj_t* lblStats;
    lv_obj_t* lblHeaderId;
    lv_obj_t* lblHeaderData;
    lv_obj_t* lblHeaderTime;
    lv_obj_t* lblId[MAX_CAN_IDS];
    lv_obj_t* lblData[MAX_CAN_IDS];
    lv_obj_t* lblTime[MAX_CAN_IDS];
} objects_t;

extern objects_t objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
};

void create_screen_main();
void tick_screen_main();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/
