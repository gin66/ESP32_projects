#include <string.h>

#include "actions.h"
#include "fonts.h"
#include "images.h"
#include "styles.h"
#include "ui.h"
#include "vars.h"

objects_t objects;

static void header_tap_cb(lv_event_t* e) {
  extern bool g_show_interval;
  g_show_interval = !g_show_interval;
}

static void clear_button_tap_cb(lv_event_t* e) {
  extern void clear_all_counters();
  clear_all_counters();
}

void create_screen_main() {
  lv_obj_t* screen = lv_screen_active();
  objects.main = screen;
  /* Colour alone is not enough — the theme leaves bg_opa transparent, which
   * is why LVGL's white default showed through instead of black. */
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  objects.lblIP = lv_label_create(screen);
  lv_obj_set_pos(objects.lblIP, 5, 3);
  lv_obj_set_size(objects.lblIP, 310, 16);
  lv_obj_set_style_text_color(objects.lblIP, lv_color_hex(0xAAAAAA),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(objects.lblIP, &lv_font_montserrat_14,
                             LV_PART_MAIN);
  lv_label_set_text(objects.lblIP, "IP: --.--.--.-- | Node: ---");

  objects.lblStats = lv_label_create(screen);
  lv_obj_set_pos(objects.lblStats, 5, 20);
  /* Full width, and DOT (not WRAP): the analyzer status string is long, and
   * with WRAP on a 16px-high label the second line was painted below the
   * black plate and overlapped the header row. DOT truncates instead. */
  lv_obj_set_size(objects.lblStats, 314, 16);
  lv_label_set_long_mode(objects.lblStats, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(objects.lblStats, lv_color_hex(0x00FF00),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(objects.lblStats, &lv_font_montserrat_12,
                             LV_PART_MAIN);
  /* Green status text needs a solid black plate behind it */
  lv_obj_set_style_bg_color(objects.lblStats, lv_color_hex(0x000000),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(objects.lblStats, LV_OPA_COVER, LV_PART_MAIN);
  lv_label_set_text(objects.lblStats, "RX: 0 | TX: 0 | Err: 0");

  /* Clear button — top right corner */
  objects.lblClearBtn = lv_label_create(screen);
  lv_obj_set_pos(objects.lblClearBtn, 265, 20);
  lv_obj_set_size(objects.lblClearBtn, 45, 16);
  lv_obj_set_style_text_color(objects.lblClearBtn, lv_color_hex(0xFF4444),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(objects.lblClearBtn, &lv_font_montserrat_14,
                             LV_PART_MAIN);
  lv_label_set_text(objects.lblClearBtn, "Clear");
  lv_obj_add_flag(objects.lblClearBtn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(objects.lblClearBtn, clear_button_tap_cb,
                      LV_EVENT_CLICKED, NULL);

  int header_y = 40;
  objects.lblHeaderId = lv_label_create(screen);
  lv_obj_set_pos(objects.lblHeaderId, 5, header_y);
  lv_obj_set_size(objects.lblHeaderId, 40, 16);
  lv_obj_set_style_text_color(objects.lblHeaderId, lv_color_hex(0x888888),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(objects.lblHeaderId, &lv_font_montserrat_14,
                             LV_PART_MAIN);
  lv_label_set_text(objects.lblHeaderId, "ID");

  objects.lblHeaderData = lv_label_create(screen);
  lv_obj_set_pos(objects.lblHeaderData, 47, header_y);
  lv_obj_set_size(objects.lblHeaderData, 155, 16);
  lv_obj_set_style_text_color(objects.lblHeaderData, lv_color_hex(0x888888),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(objects.lblHeaderData, &lv_font_montserrat_14,
                             LV_PART_MAIN);
  lv_label_set_text(objects.lblHeaderData, "Data");

  objects.lblHeaderTime = lv_label_create(screen);
  lv_obj_set_pos(objects.lblHeaderTime, 204, header_y);
  lv_obj_set_size(objects.lblHeaderTime, 112, 16);
  lv_obj_set_style_text_color(objects.lblHeaderTime, lv_color_hex(0x888888),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(objects.lblHeaderTime, &lv_font_montserrat_14,
                             LV_PART_MAIN);
  lv_label_set_text(objects.lblHeaderTime, "Last Seen");
  lv_obj_add_flag(objects.lblHeaderTime, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(objects.lblHeaderTime, header_tap_cb, LV_EVENT_CLICKED,
                      NULL);

  /* Flat layout — no scrolling (resistive touch).  Both screens are 320px wide.
   *  2.8" (240×320, 270° landscape): 9 rows
   *  3.5" (320×480,   0° portrait):  23 rows — only vertical space differs
   */
  int row_height = ROW_HEIGHT;
  int list_width = 314;
  int col_data_x = 47;
  int col_data_w = 155;
  int col_time_x = 204;
  int col_time_w = 112;

  for (int i = 0; i < MAX_CAN_IDS; i++) {
    int y = 58 + i * row_height;

    lv_obj_t* bg = lv_obj_create(screen);
    lv_obj_set_pos(bg, 3, y);
    lv_obj_set_size(bg, list_width, row_height);
    lv_obj_set_style_bg_color(
        bg, lv_color_hex(i % 2 == 0 ? 0x111111 : 0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);

    objects.lblId[i] = lv_label_create(screen);
    lv_obj_set_pos(objects.lblId[i], 5, y + 1);
    lv_obj_set_size(objects.lblId[i], 40, 16);
    lv_obj_set_style_text_color(objects.lblId[i], lv_color_hex(0xCCCCCC),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(objects.lblId[i], &lv_font_montserrat_14,
                               LV_PART_MAIN);
    lv_label_set_text(objects.lblId[i], "---");

    objects.lblData[i] = lv_label_create(screen);
    lv_obj_set_pos(objects.lblData[i], col_data_x, y + 1);
    lv_obj_set_size(objects.lblData[i], col_data_w, 16);
    lv_obj_set_style_text_color(objects.lblData[i], lv_color_hex(0xCCCCCC),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(objects.lblData[i], &lv_font_montserrat_14,
                               LV_PART_MAIN);
    lv_label_set_text(objects.lblData[i], "--");

    objects.lblTime[i] = lv_label_create(screen);
    lv_obj_set_pos(objects.lblTime[i], col_time_x, y + 1);
    lv_obj_set_size(objects.lblTime[i], col_time_w, 16);
    lv_obj_set_style_text_color(objects.lblTime[i], lv_color_hex(0xCCCCCC),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(objects.lblTime[i], &lv_font_montserrat_14,
                               LV_PART_MAIN);
    lv_label_set_text(objects.lblTime[i], "--");
  }
}

void tick_screen_main() {
  // Periodic updates for the main screen - called from ui_tick()
  // Use this to update dynamic content like CAN message counts
}

void tick_screen_by_id(enum ScreensEnum screenId) {
  switch (screenId) {
    case SCREEN_ID_MAIN:
      tick_screen_main();
      break;
    default:
      break;
  }
}

void tick_screen(int screen_index) {
  if (screen_index == SCREEN_ID_MAIN - 1) {
    tick_screen_main();
  }
}

void create_screens() { create_screen_main(); }
