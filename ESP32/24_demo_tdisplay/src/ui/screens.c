#include "screens.h"
#include <stdio.h>
#include <string.h>

objects_t objects;

static lv_style_t style_title;
static lv_style_t style_value;
static lv_style_t style_small;

static void init_styles() {
  lv_style_init(&style_title);
  lv_style_set_text_font(&style_title, &lv_font_montserrat_14);
  lv_style_set_text_color(&style_title, lv_color_hex(0xFFFFFF));

  lv_style_init(&style_value);
  lv_style_set_text_font(&style_value, &lv_font_montserrat_12);
  lv_style_set_text_color(&style_value, lv_color_hex(0x00FF00));

  lv_style_init(&style_small);
  lv_style_set_text_font(&style_small, &lv_font_montserrat_10);
  lv_style_set_text_color(&style_small, lv_color_hex(0xAAAAAA));
}

void create_screen_main() {
  lv_obj_t* scr = lv_obj_create(0);
  objects.main = scr;
  lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(scr, 0, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_all(scr, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(scr, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(scr, 0, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* lbl;

  lbl = lv_label_create(scr);
  objects.lbl_hostname = lbl;
  lv_obj_add_style(lbl, &style_title, 0);
  lv_label_set_text(lbl, HOSTNAME);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl = lv_label_create(scr);
  objects.lbl_time = lbl;
  lv_obj_add_style(lbl, &style_title, 0);
  lv_label_set_text(lbl, "--:--:--");
  lv_obj_align(lbl, LV_ALIGN_TOP_RIGHT, 0, 0);

  lv_obj_t* sep1 = lv_obj_create(scr);
  lv_obj_set_size(sep1, LV_PCT(100), 1);
  lv_obj_set_style_bg_color(sep1, lv_color_hex(0x444444), 0);
  lv_obj_set_style_border_width(sep1, 0, 0);
  lv_obj_set_style_pad_all(sep1, 0, 0);
  lv_obj_align(sep1, LV_ALIGN_TOP_LEFT, 0, 20);

  lbl = lv_label_create(scr);
  objects.lbl_wifi = lbl;
  lv_obj_add_style(lbl, &style_value, 0);
  lv_label_set_text(lbl, LV_SYMBOL_WIFI " ...");
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 25);

  lbl = lv_label_create(scr);
  objects.lbl_ip = lbl;
  lv_obj_add_style(lbl, &style_small, 0);
  lv_label_set_text(lbl, "IP: ---");
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 43);

  lv_obj_t* sep2 = lv_obj_create(scr);
  lv_obj_set_size(sep2, LV_PCT(100), 1);
  lv_obj_set_style_bg_color(sep2, lv_color_hex(0x444444), 0);
  lv_obj_set_style_border_width(sep2, 0, 0);
  lv_obj_set_style_pad_all(sep2, 0, 0);
  lv_obj_align(sep2, LV_ALIGN_TOP_LEFT, 0, 60);

  lbl = lv_label_create(scr);
  lv_obj_add_style(lbl, &style_small, 0);
  lv_label_set_text(lbl, "ADC (GPIO34)");
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 65);

  lbl = lv_label_create(scr);
  objects.lbl_voltage = lbl;
  lv_obj_add_style(lbl, &style_value, 0);
  lv_label_set_text(lbl, "--- mV");
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 79);

  lv_obj_t* bar = lv_bar_create(scr);
  objects.bar_heap = bar;
  lv_obj_set_size(bar, LV_PCT(100), 6);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x222222), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, -34);

  lbl = lv_label_create(scr);
  objects.lbl_heap = lbl;
  lv_obj_add_style(lbl, &style_small, 0);
  lv_label_set_text(lbl, "Heap: ---");
  lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 0, -42);

  lbl = lv_label_create(scr);
  objects.lbl_uptime = lbl;
  lv_obj_add_style(lbl, &style_small, 0);
  lv_label_set_text(lbl, "UP ---");
  lv_obj_align(lbl, LV_ALIGN_BOTTOM_RIGHT, 0, -42);

  lbl = lv_label_create(scr);
  lv_obj_add_style(lbl, &style_small, 0);
  lv_label_set_text(lbl, "BTN1:GPIO35 BTN2:GPIO0");
  lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 0, -18);

  lbl = lv_label_create(scr);
  objects.lbl_btn = lbl;
  lv_obj_add_style(lbl, &style_value, 0);
  lv_label_set_text(lbl, "");
  lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 0, -4);
}

void tick_screen_main() {}

void tick_screen_by_id(enum ScreensEnum screenId) {
  switch (screenId) {
    case SCREEN_ID_MAIN:
      tick_screen_main();
      break;
  }
}

void tick_screen(int screen_index) {
  tick_screen_by_id((enum ScreensEnum)(screen_index + 1));
}

void create_screens() {
  init_styles();
  create_screen_main();
}
