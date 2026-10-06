#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

objects_t objects;

static void header_tap_cb(lv_event_t* e) {
    extern bool g_show_interval;
    g_show_interval = !g_show_interval;
}

void create_screen_main() {
    lv_obj_t *screen = lv_screen_active();
    objects.main = screen;
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);

    objects.lblIP = lv_label_create(screen);
    lv_obj_set_pos(objects.lblIP, 5, 3);
    lv_obj_set_size(objects.lblIP, 310, 16);
    lv_obj_set_style_text_color(objects.lblIP, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_text_font(objects.lblIP, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(objects.lblIP, "IP: --.--.--.-- | Node: ---");

    objects.lblStats = lv_label_create(screen);
    lv_obj_set_pos(objects.lblStats, 5, 20);
    lv_obj_set_size(objects.lblStats, 310, 16);
    lv_obj_set_style_text_color(objects.lblStats, lv_color_hex(0x00FF00), LV_PART_MAIN);
    lv_obj_set_style_text_font(objects.lblStats, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(objects.lblStats, "RX: 0 | TX: 0 | Err: 0");

    int header_y = 40;
    objects.lblHeaderId = lv_label_create(screen);
    lv_obj_set_pos(objects.lblHeaderId, 5, header_y);
    lv_obj_set_size(objects.lblHeaderId, 40, 16);
    lv_obj_set_style_text_color(objects.lblHeaderId, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(objects.lblHeaderId, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(objects.lblHeaderId, "ID");

    objects.lblHeaderData = lv_label_create(screen);
    lv_obj_set_pos(objects.lblHeaderData, 47, header_y);
    lv_obj_set_size(objects.lblHeaderData, 155, 16);
    lv_obj_set_style_text_color(objects.lblHeaderData, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(objects.lblHeaderData, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(objects.lblHeaderData, "Data");

    objects.lblHeaderTime = lv_label_create(screen);
    lv_obj_set_pos(objects.lblHeaderTime, 204, header_y);
    lv_obj_set_size(objects.lblHeaderTime, 112, 16);
    lv_obj_set_style_text_color(objects.lblHeaderTime, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(objects.lblHeaderTime, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(objects.lblHeaderTime, "Last Seen");
    lv_obj_set_clickable(objects.lblHeaderTime, true);
    lv_obj_add_event_cb(objects.lblHeaderTime, header_tap_cb, LV_EVENT_CLICKED, NULL);

    int row_start_y = 60;
    int row_height = 22;
    for (int i = 0; i < MAX_CAN_IDS; i++) {
        int y = row_start_y + i * row_height;

        lv_obj_t* bg = lv_obj_create(screen);
        lv_obj_set_pos(bg, 3, y);
        lv_obj_set_size(bg, 314, row_height);
        lv_obj_set_style_bg_color(bg, lv_color_hex(i % 2 == 0 ? 0x111111 : 0x1A1A1A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bg, 255, LV_PART_MAIN);
        lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);

        objects.lblId[i] = lv_label_create(screen);
        lv_obj_set_pos(objects.lblId[i], 5, y + 2);
        lv_obj_set_size(objects.lblId[i], 40, 18);
        lv_obj_set_style_text_color(objects.lblId[i], lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        lv_obj_set_style_text_font(objects.lblId[i], &lv_font_montserrat_16, LV_PART_MAIN);
        lv_label_set_text(objects.lblId[i], "---");

        objects.lblData[i] = lv_label_create(screen);
        lv_obj_set_pos(objects.lblData[i], 47, y + 2);
        lv_obj_set_size(objects.lblData[i], 155, 18);
        lv_obj_set_style_text_color(objects.lblData[i], lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        lv_obj_set_style_text_font(objects.lblData[i], &lv_font_montserrat_14, LV_PART_MAIN);
        lv_label_set_text(objects.lblData[i], "--");

        objects.lblTime[i] = lv_label_create(screen);
        lv_obj_set_pos(objects.lblTime[i], 204, y + 2);
        lv_obj_set_size(objects.lblTime[i], 112, 18);
        lv_obj_set_style_text_color(objects.lblTime[i], lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        lv_obj_set_style_text_font(objects.lblTime[i], &lv_font_montserrat_14, LV_PART_MAIN);
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

void create_screens() {
    create_screen_main();
}
