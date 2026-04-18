#include "ui.h"

static lv_obj_t* active_screen = NULL;
static enum ScreensEnum active_screen_id = SCREEN_ID_MAIN;

void loadScreen(enum ScreensEnum screenId) {
  active_screen_id = screenId;
  switch (screenId) {
    case SCREEN_ID_MAIN:
      create_screen_main();
      active_screen = objects.main;
      lv_scr_load(objects.main);
      break;
  }
}

void ui_init() { create_screens(); loadScreen(SCREEN_ID_MAIN); }

void ui_tick() { tick_screen_by_id(active_screen_id); }
