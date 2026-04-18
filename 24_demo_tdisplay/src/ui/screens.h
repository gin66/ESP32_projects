#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
  lv_obj_t* main;
  lv_obj_t* lbl_hostname;
  lv_obj_t* lbl_time;
  lv_obj_t* lbl_wifi;
  lv_obj_t* lbl_ip;
  lv_obj_t* lbl_voltage;
  lv_obj_t* lbl_heap;
  lv_obj_t* lbl_uptime;
  lv_obj_t* lbl_btn;
  lv_obj_t* bar_heap;
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

#endif
