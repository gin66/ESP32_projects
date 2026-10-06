#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CAN_IDS 8

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
