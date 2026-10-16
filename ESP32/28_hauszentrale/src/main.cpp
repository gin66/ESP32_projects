#include "template.h"
#include <esp_heap_caps.h>
#include <esp_flash.h>
#include <lvgl.h>

/* S4.03: Two 800x20 RGB565 draw buffers in internal RAM (not PSRAM) */
static uint8_t s_draw_buf0[800 * 20 * 2];
static uint8_t s_draw_buf1[800 * 20 * 2];

/* LVGL 9.2 needs a millisecond tick source */
static uint32_t lv_tick_cb(void) { return (uint32_t)millis(); }

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("28_hauszentrale S1.01 OK");

    tpl_system_setup(0);  // no deep sleep

    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    Serial.printf("Flash: %d MB, PSRAM: %s\n",
                  flash_size / 1024 / 1024,
                  psramFound() ? "8 MB" : "0 MB");

    tpl_ws7_expander_init();
    tpl_ws7_backlight(false);  // keep backlight off until the panel is up

    /* S4.02: reset the ST7262 through the CH422G before bring-up */
    tpl_ws7_panel_reset();

    /* S5.01-S5.05: GT911 touch initialization */
    const bool touch_ok = (tpl_ws7_touch_init() == ESP_OK);
    Serial.printf("GT911 touch: %s\n", touch_ok ? "ready" : "NOT FOUND (touch disabled)");

    /* S4.01-S4.05: RGB panel + LVGL display */
    lv_init();
    lv_tick_set_cb(lv_tick_cb);
    lv_display_t *disp = tpl_ws7_display_init(TPL_WS7_ROTATION, (uint32_t *)s_draw_buf0, sizeof(s_draw_buf0));
    if (disp) {
        Serial.println("RGB panel + LVGL display initialized");

        /* S5.03: register the GT911 as the pointer input device. Only when the
         * controller answered on the bus, otherwise the poll floods the log. */
        if (touch_ok) {
            lv_indev_t *indev = lv_indev_create();
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(indev, tpl_ws7_touch_read_cb);
            lv_indev_set_display(indev, disp);

            /* Visible cursor follows the finger for calibration */
            lv_obj_t *cursor = lv_obj_create(lv_layer_sys());
            lv_obj_remove_style_all(cursor);
            lv_obj_set_size(cursor, 24, 24);
            lv_obj_set_style_radius(cursor, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(cursor, lv_color_hex(0xFF0000), 0);
            lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);
            lv_indev_set_cursor(indev, cursor);
        }

        /* S4.09: Color bar test pattern, in logical (rotated) coordinates so it
         * fits the panel exactly regardless of orientation. */
        const int32_t w = TPL_WS7_WIDTH;
        const int32_t h = TPL_WS7_HEIGHT;
        Serial.printf("S4.09: color bars, logical %dx%d\n", (int)w, (int)h);

        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        /* Top half: six colour bars */
        const int nbars = 6;
        const int32_t top_h = h / 2;
        lv_color_t colors[nbars] = {
            lv_color_hex(0xFF0000),  /* Red */
            lv_color_hex(0x00FF00),  /* Green */
            lv_color_hex(0x0000FF),  /* Blue */
            lv_color_hex(0xFFFF00),  /* Yellow */
            lv_color_hex(0xFF00FF),  /* Magenta */
            lv_color_hex(0x00FFFF),  /* Cyan */
        };
        for (int i = 0; i < nbars; i++) {
            int32_t y0 = (top_h * i) / nbars;
            int32_t y1 = (top_h * (i + 1)) / nbars;
            lv_obj_t *rect = lv_obj_create(scr);
            lv_obj_set_size(rect, w, y1 - y0);
            lv_obj_set_pos(rect, 0, y0);
            lv_obj_set_style_bg_color(rect, colors[i], 0);
            lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
            lv_obj_remove_flag(rect, LV_OBJ_FLAG_SCROLLABLE);
        }

        /* Bottom half: rotation labels (S4.09/S4.10) */
        const char *rot_names[4] = {"0: portrait", "1: landscape", "2: portrait", "3: landscape"};
        const int32_t bot_h = h - top_h;
        for (int r = 0; r < 4; r++) {
            int32_t y0 = top_h + (bot_h * r) / 4;
            int32_t y1 = top_h + (bot_h * (r + 1)) / 4;
            lv_obj_t *rot_rect = lv_obj_create(scr);
            lv_obj_set_size(rot_rect, w, y1 - y0);
            lv_obj_set_pos(rot_rect, 0, y0);
            lv_obj_set_style_bg_color(rot_rect, lv_color_hex(0x333333), 0);
            lv_obj_set_style_bg_opa(rot_rect, LV_OPA_COVER, 0);
            lv_obj_remove_flag(rot_rect, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *rot_label = lv_label_create(rot_rect);
            lv_label_set_text(rot_label, rot_names[r]);
            lv_obj_align(rot_label, LV_ALIGN_LEFT_MID, 10, 0);
            lv_obj_set_style_text_color(rot_label, lv_color_white(), 0);
        }

        /* Render the first full frame before lighting the panel, otherwise the
         * uninitialised PSRAM framebuffer is what the user sees at boot. */
        lv_refr_now(disp);
        tpl_ws7_backlight(true);

        Serial.println("Color bars + rotation labels drawn");
    } else {
        Serial.println("ERROR: RGB panel initialization failed");
    }

    tpl_wifi_setup(true, true, (gpio_num_t)255);  // no status LED on this board
    tpl_webserver_setup();
    tpl_websocket_setup(NULL, NULL);
#ifdef NET_WATCHDOG
    tpl_net_watchdog_setup();
#endif
    tpl_command_setup(NULL);
}

void loop() {
    lv_task_handler();
    vTaskDelay(5 / portTICK_PERIOD_MS);
}
