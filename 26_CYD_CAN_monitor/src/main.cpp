/*
 * 26_CYD_CAN_monitor
 * ===================
 *
 * CYD ESP32 CAN Bus Monitor with LVGL Display (EEZ Studio UI)
 *
 * Two 2.8" Cheap Yellow Display boards connected via CAN bus:
 *   - CAN RX: GPIO 27
 *   - CAN TX: GPIO 22
 *   - Transceivers: SN65HVD230/232 with 120 Ohm termination
 *   - Baud rate: 125 kbps (standard CAN)
 *
 * Each board uses its WiFi IP last octet as its CAN node ID.
 * Boards exchange time data over CAN and display received messages.
 *
 * Hardware:
 *   CYD Board 1 (e.g. IP 192.168.1.26) -> CAN ID 0x1A (26)
 *   CYD Board 2 (e.g. IP 192.168.1.27) -> CAN ID 0x1B (27)
 *
 * Board 1 sends time with CAN ID 0x1A, displays messages from 0x1B.
 * Board 2 sends time with CAN ID 0x1B, displays messages from 0x1A.
 *
 * Based on:
 *   15_power_log_eink  - CAN bus (TWAI driver)
 *   19_CYD-project     - LVGL, TFT_eSPI, XPT2046, EEZ Studio UI
 */

#include <Arduino.h>
#include <SPI.h>
#include <driver/twai.h>
#include <esp_crc.h>

#define SW_NAME_REV "26_CYD_CAN_monitor v1.0"

// ************* ESP32_projects template includes *************
#include "template.h"

// ************* LVGL and EEZ Studio UI includes *************
#include <lvgl.h>
#include "ui/ui.h"
#include "ui/screens.h"

// ************* TFT display and touchscreen *************
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// CAN bus pins (from platformio.ini build flags)
#ifndef CAN_TX_PIN
#define CAN_TX_PIN 22
#endif
#ifndef CAN_RX_PIN
#define CAN_RX_PIN 27
#endif

// Display constants
#ifndef TFT_WIDTH
#define TFT_WIDTH 240
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 320
#endif
#ifndef TFT_ROTATION
#define TFT_ROTATION 3
#endif
// Panels are natively portrait; the UI is landscape (rotation 3),
// so the active screen dimensions are the swapped TFT dimensions.
#if TFT_ROTATION == 0 || TFT_ROTATION == 180
  /* Portrait mode: width = TFT_WIDTH, height = TFT_HEIGHT */
  #define SCREEN_WIDTH TFT_WIDTH
  #define SCREEN_HEIGHT TFT_HEIGHT
#else
  /* Landscape mode (90°/270°): width = TFT_HEIGHT, height = TFT_WIDTH */
  #define SCREEN_WIDTH TFT_HEIGHT
  #define SCREEN_HEIGHT TFT_WIDTH
#endif

// LVGL draw buffer
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

// Touchscreen object (the display's TFT_eSPI instance is owned by the LVGL driver)
SPIClass touchscreenSPI = SPIClass(VSPI);
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

// Touchscreen pins (CYD standard)

// Touch coordinates
int touch_x, touch_y, touch_z;

// Backlight control (CYD uses LEDC PWM)
#ifdef TFT_BL
#define BACKLIGHT_PIN TFT_BL
#else
#define BACKLIGHT_PIN tpl_backlightPin
#endif
#define BACKLIGHT_DIM 51
#define BACKLIGHT_FULL 255
#define BACKLIGHT_TIMEOUT_MS 60000
#define BACKLIGHT_FADE_UP_MS 500
#define BACKLIGHT_FADE_DOWN_MS 10000
unsigned long g_last_touch_time = 0;
unsigned long g_fade_start_time = 0;
uint8_t g_backlight_current = BACKLIGHT_DIM;
uint8_t g_backlight_fade_start = BACKLIGHT_DIM;
uint8_t g_backlight_target = BACKLIGHT_DIM;

// ************* CAN Bus Configuration *************

// CAN message types
#define CAN_MSG_TYPE_TIME 0x01
#define CAN_MSG_TYPE_RAW  0x00

// CAN message structure for time exchange
// Format: [type:1][sec:1][min:1][hour:1][day:1][reserved:3]
typedef struct __attribute__((packed)) {
    uint8_t msg_type;
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t weekday;  // 0=Sunday
    uint8_t reserved[3];
} can_time_message_t;

// Table entry for a unique CAN ID
typedef struct {
    bool used;
    uint32_t identifier;
    bool is_extended;
    uint8_t data_len;
    uint8_t data[8];
    uint8_t last_data[8];
    unsigned long last_timestamp;
    time_t last_time;
    unsigned long interval_ms;
    bool data_changed;
} can_id_entry_t;

static can_id_entry_t g_can_id_table[MAX_CAN_IDS];

// Statistics
static uint32_t g_can_rx_count = 0;
static uint32_t g_can_tx_count = 0;
static uint32_t g_can_error_cnt = 0;

// WiFi-derived CAN node ID (last octet of IP)
static uint8_t g_can_node_id = 0;

bool g_show_interval = false;
static bool g_bus_error_flag = false;

// ************* CAN ID Table Functions *************

static void can_table_update(const twai_message_t* msg) {
    can_id_entry_t* entry = nullptr;

    for (int i = 0; i < MAX_CAN_IDS; i++) {
        if (g_can_id_table[i].used && g_can_id_table[i].identifier == msg->identifier) {
            entry = &g_can_id_table[i];
            break;
        }
    }

    if (entry == nullptr) {
        for (int i = 0; i < MAX_CAN_IDS; i++) {
            if (!g_can_id_table[i].used) {
                entry = &g_can_id_table[i];
                entry->used = true;
                entry->identifier = msg->identifier;
                entry->is_extended = (msg->flags & TWAI_MSG_FLAG_EXTD) != 0;
                entry->data_len = 0;
                entry->last_timestamp = 0;
                entry->interval_ms = 0;
                entry->data_changed = false;
                memset(entry->last_data, 0, sizeof(entry->last_data));
                break;
            }
        }
    }

    if (entry == nullptr) return;

    unsigned long now = millis();
    if (entry->last_timestamp != 0) {
        entry->interval_ms = now - entry->last_timestamp;
    }
    entry->last_timestamp = now;
    entry->last_time = time(nullptr);

    entry->data_changed = (entry->data_len != msg->data_length_code ||
                           memcmp(entry->data, msg->data, msg->data_length_code) != 0);

    entry->data_len = msg->data_length_code;
    memcpy(entry->data, msg->data, msg->data_length_code);
    memcpy(entry->last_data, msg->data, msg->data_length_code);

    g_can_rx_count++;
}

// ************* CAN ID Formatting *************

static void format_can_id(char* buf, size_t buf_size, uint32_t id, bool is_extended) {
    if (is_extended) {
        snprintf(buf, buf_size, "%08X", id);
    } else {
        snprintf(buf, buf_size, "%03X", id & 0x7FF);
    }
}

static void format_data_bytes(char* buf, size_t buf_size, const uint8_t* data, uint8_t len) {
    buf[0] = '\0';
    size_t offset = 0;
    for (uint8_t i = 0; i < len && offset < buf_size - 4; i++) {
        offset += snprintf(buf + offset, buf_size - offset, "%02X ", data[i]);
    }
}

// ************* CAN Time Message Handling *************

static bool parse_time_message(const uint8_t* data, uint8_t len, can_time_message_t* time_msg) {
    if (len < sizeof(can_time_message_t)) return false;
    memcpy(time_msg, data, sizeof(can_time_message_t));
    return time_msg->msg_type == CAN_MSG_TYPE_TIME;
}

static void format_time_string(char* buf, size_t buf_size, const can_time_message_t* time_msg) {
    snprintf(buf, buf_size, "%02d:%02d:%02d (day %d)",
             time_msg->hours, time_msg->minutes, time_msg->seconds,
             time_msg->weekday);
}

// ************* LVGL Display Update (from CAN ID table) *************

static void update_can_display(void) {
    char stats_buf[64];
    snprintf(stats_buf, sizeof(stats_buf),
             "RX: %lu | TX: %lu | Err: %lu",
             g_can_rx_count, g_can_tx_count, g_can_error_cnt);
    lv_label_set_text(objects.lblStats, stats_buf);
    lv_obj_set_style_text_color(objects.lblStats,
        g_bus_error_flag ? lv_color_hex(0xFF0000) : lv_color_hex(0x00FF00), LV_PART_MAIN);

    char ip_buf[64];
    snprintf(ip_buf, sizeof(ip_buf),
             "IP: %u.%u.%u.%u | Node: 0x%02X",
             WiFi.localIP()[0], WiFi.localIP()[1],
             WiFi.localIP()[2], WiFi.localIP()[3],
             g_can_node_id);
    lv_label_set_text(objects.lblIP, ip_buf);

    for (int i = 0; i < MAX_CAN_IDS; i++) {
        can_id_entry_t* entry = &g_can_id_table[i];
        lv_color_t color;

        if (entry->used && entry->data_changed) {
            color = lv_color_hex(0x00FF00);
        } else {
            color = lv_color_hex(0xCCCCCC);
        }

        if (entry->used) {
            char id_buf[16];
            format_can_id(id_buf, sizeof(id_buf), entry->identifier, entry->is_extended);
            lv_label_set_text(objects.lblId[i], id_buf);

            char data_buf[24];
            format_data_bytes(data_buf, sizeof(data_buf), entry->data, entry->data_len);
            lv_label_set_text(objects.lblData[i], data_buf);

            char time_buf[16];
            if (g_show_interval) {
                if (entry->interval_ms > 0) {
                    snprintf(time_buf, sizeof(time_buf), "%.1fs", entry->interval_ms / 1000.0);
                } else {
                    snprintf(time_buf, sizeof(time_buf), "--");
                }
            } else {
                if (entry->last_time > 1600000000) {
                    struct tm timeinfo;
                    localtime_r(&entry->last_time, &timeinfo);
                    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
                             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                } else {
                    snprintf(time_buf, sizeof(time_buf), "%lus", entry->last_timestamp / 1000);
                }
            }
            lv_label_set_text(objects.lblTime[i], time_buf);
        } else {
            lv_label_set_text(objects.lblId[i], "---");
            lv_label_set_text(objects.lblData[i], "--");
            lv_label_set_text(objects.lblTime[i], "--");
        }

        lv_obj_set_style_text_color(objects.lblId[i], color, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.lblData[i], color, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.lblTime[i], color, LV_PART_MAIN);
    }
}

// ************* Touchscreen Read Callback *************

static void touchscreen_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    if (touchscreen.tirqTouched() && touchscreen.touched()) {
        TS_Point p = touchscreen.getPoint();

        // Touch in the logical (rotated) screen orientation
        int32_t lx = constrain(map(p.x, 200, 3700, 1, SCREEN_WIDTH), 0, SCREEN_WIDTH - 1);
        int32_t ly = constrain(map(p.y, 240, 3800, 1, SCREEN_HEIGHT), 0, SCREEN_HEIGHT - 1);

        // LVGL rotates indev points by the display rotation, so convert the
        // logical coordinates back into the panel's native frame here; LVGL
        // then rotates them forward again to the logical orientation.
        switch (lv_display_get_rotation(lv_indev_get_display(indev))) {
            case LV_DISPLAY_ROTATION_90:
                touch_x = ly;
                touch_y = TFT_HEIGHT - 1 - lx;
                break;
            case LV_DISPLAY_ROTATION_180:
                touch_x = TFT_WIDTH - 1 - lx;
                touch_y = TFT_HEIGHT - 1 - ly;
                break;
            case LV_DISPLAY_ROTATION_270:
                touch_x = TFT_WIDTH - 1 - ly;
                touch_y = lx;
                break;
            default:
                touch_x = lx;
                touch_y = ly;
                break;
        }

        touch_z = p.z;
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = touch_x;
        data->point.y = touch_y;

        g_last_touch_time = millis();
        if (g_backlight_target != BACKLIGHT_FULL) {
            g_backlight_target = BACKLIGHT_FULL;
            g_fade_start_time = millis();
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ************* Backlight Control *************

static void backlight_update(void) {
    unsigned long now = millis();

    // Check timeout: start fading down after BACKLIGHT_TIMEOUT_MS of no touch
    if (g_backlight_target == BACKLIGHT_FULL &&
        (now - g_last_touch_time > BACKLIGHT_TIMEOUT_MS)) {
        g_backlight_fade_start = g_backlight_current;
        g_backlight_target = BACKLIGHT_DIM;
        g_fade_start_time = now;
    }

    // Handle fade animation
    if (g_backlight_current != g_backlight_target) {
        unsigned long fade_duration = (g_backlight_target == BACKLIGHT_FULL)
                                          ? BACKLIGHT_FADE_UP_MS
                                          : BACKLIGHT_FADE_DOWN_MS;
        unsigned long elapsed = now - g_fade_start_time;

        if (elapsed >= fade_duration) {
            g_backlight_current = g_backlight_target;
        } else {
            int16_t delta = (int16_t)g_backlight_target - (int16_t)g_backlight_fade_start;
            g_backlight_current = g_backlight_fade_start + (delta * elapsed / fade_duration);
        }
        ledcWrite(7, g_backlight_current);
    }
}

// ************* LVGL Display Initialization *************

static void log_print_cb(lv_log_level_t level, const char* buf) {
    LV_UNUSED(level);
    Serial.println(buf);
    Serial.flush();
}

static void lv_init_display(void) {
    lv_log_register_print_cb(log_print_cb);

    // Initialize touchscreen SPI and touchscreen
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreen.begin(touchscreenSPI);
    touchscreen.setRotation(TFT_ROTATION);

    // Initialize LVGL
    lv_init();

    // The driver owns its own TFT_eSPI instance and must be given the panel's
    // native (portrait) resolution. Rotation is applied below via LVGL, which
    // then swaps the logical resolution to landscape and drives the driver's
    // setRotation().
    lv_display_t* disp = lv_tft_espi_create(TFT_WIDTH, TFT_HEIGHT, draw_buf, sizeof(draw_buf));

#if TFT_ROTATION == 1
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
#elif TFT_ROTATION == 2
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
#elif TFT_ROTATION == 3
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
#else
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);
#endif

    // Create and register input device (touch)
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchscreen_read_cb);
}

// ************* CAN Task (FreeRTOS) *************

static void CANTask(void* parameter) {
    uint32_t alerts_triggered;
    uint32_t alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS |
                                 TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL |
                                 TWAI_ALERT_BUS_RECOVERED;

    twai_reconfigure_alerts(alerts_to_enable, NULL);

    while (true) {
        twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(100));
        twai_status_info_t twaistatus;
        twai_get_status_info(&twaistatus);

        // Track errors
        if (alerts_triggered & TWAI_ALERT_ERR_PASS) {
            g_can_error_cnt++;
        }
        if (alerts_triggered & TWAI_ALERT_BUS_ERROR) {
            g_can_error_cnt++;
            g_bus_error_flag = true;
            Serial.printf("[CAN] Bus error count: %d\n", twaistatus.bus_error_count);
        }
        if (alerts_triggered & TWAI_ALERT_BUS_RECOVERED) {
            g_bus_error_flag = false;
            Serial.println("[CAN] Bus recovered");
        }
        if (alerts_triggered & TWAI_ALERT_RX_QUEUE_FULL) {
            g_can_error_cnt++;
            Serial.printf("[CAN] RX queue full. Buffered: %d, Missed: %d\n",
                          twaistatus.msgs_to_rx, twaistatus.rx_missed_count);
        }

        // Process received messages
        if (alerts_triggered & TWAI_ALERT_RX_DATA) {
            twai_message_t message;
            while (twai_receive(&message, 0) == ESP_OK) {
                can_table_update(&message);
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ************* Time Broadcast Task *************

static void TimeBroadcastTask(void* parameter) {
    struct tm timeinfo;
    time_t now;
    int last_sec = -1;

    while (true) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // Every second

        time(&now);
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_sec != last_sec) {
            last_sec = timeinfo.tm_sec;

            // Build CAN time message
            twai_message_t message;
            memset(&message, 0, sizeof(message));

            message.flags = TWAI_MSG_FLAG_NONE;
            message.self = 0;  // Not our own message
            message.identifier = g_can_node_id;
            message.data_length_code = sizeof(can_time_message_t);

            can_time_message_t time_msg;
            time_msg.msg_type = CAN_MSG_TYPE_TIME;
            time_msg.seconds = timeinfo.tm_sec;
            time_msg.minutes = timeinfo.tm_min;
            time_msg.hours = timeinfo.tm_hour;
            time_msg.weekday = timeinfo.tm_wday;
            memset(time_msg.reserved, 0, sizeof(time_msg.reserved));

            memcpy(message.data, &time_msg, sizeof(time_msg));

            twai_status_info_t status;
            twai_get_status_info(&status);

            if (status.msgs_to_tx < 5) {
                if (twai_transmit(&message, pdMS_TO_TICKS(100)) == ESP_OK) {
                    g_can_tx_count++;
                }
            }
        }
    }
}

// ************* WiFi-Derived CAN Node ID *************

static uint8_t get_can_node_id_from_wifi(void) {
    // Use the last octet of the WiFi IP as the CAN node ID
    // This allows two boards to self-identify on the CAN bus
    IPAddress ip = WiFi.localIP();
    return ip[3];  // Last octet
}

// ************* Setup *************

void setup() {
    Serial.begin(115200);
    Serial.println(SW_NAME_REV);

    // Initialize template system
    tpl_system_setup(0);  // no deep sleep

    // WiFi with OTA
    tpl_wifi_setup(true, true, (gpio_num_t)255);

    // Wait for WiFi to connect
    int connect_attempts = 0;
    while (WiFi.status() != WL_CONNECTED && connect_attempts < 30) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        connect_attempts++;
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        g_can_node_id = get_can_node_id_from_wifi();
        Serial.printf("[CAN] Node ID derived from IP: 0x%02X\n", g_can_node_id);
    } else {
        Serial.println("[CAN] WiFi not connected, using fallback node ID 0x1A");
        g_can_node_id = 0x1A;  // Fallback
    }

    // Web server and WebSocket
    tpl_webserver_setup();
    tpl_websocket_setup(NULL, NULL);
    tpl_net_watchdog_setup();
    tpl_command_setup(NULL);

    // Initialize LVGL display and touchscreen
    lv_init_display();

    // Initialize backlight PWM (channel 7)
    ledcSetup(7, 5000, 8);
    ledcAttachPin(BACKLIGHT_PIN, 7);
    ledcWrite(7, BACKLIGHT_DIM);

    // Initialize EEZ Studio UI (creates all screen objects)
    ui_init();
    Serial.println("[UI] EEZ Studio UI initialized");

    // Install TWAI (CAN) driver
    Serial.println("[CAN] Initializing TWAI driver...");

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);

    // Use 125 kbps timing (standard CAN, not CAN-FD)
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS();

    // Accept all messages (no filtering by ID)
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        Serial.println("[CAN] ERROR: Failed to install TWAI driver");
    } else if (twai_start() != ESP_OK) {
        Serial.println("[CAN] ERROR: Failed to start TWAI driver");
    } else {
        Serial.println("[CAN] TWAI driver started successfully");

        // Reconfigure alerts for message reception and errors
        uint32_t alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS |
                                     TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL |
                                     TWAI_ALERT_BUS_RECOVERED;
        if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK) {
            Serial.println("[CAN] Alerts configured");
        }
    }

    // Create CAN receive task
    xTaskCreate(CANTask, "CANTask", 4096, NULL, 5, NULL);

    // Create time broadcast task
    xTaskCreate(TimeBroadcastTask, "TimeBroadcastTask", 2048, NULL, 4, NULL);

    Serial.println("[26_CYD_CAN_monitor] Setup complete");
}

// ************* Loop *************

void loop() {
    static unsigned long last_display_update = 0;
    static unsigned long last_ui_tick = 0;
    unsigned long now = millis();

    // LVGL tasks
    lv_tick_inc(now - last_ui_tick);
    last_ui_tick = now;
    lv_task_handler();

    // EEZ Studio UI tick
    ui_tick();

    // Update CAN display every 500ms
    if (now - last_display_update > 500) {
        last_display_update = now;
        update_can_display();
    }

    // Backlight fade
    backlight_update();

    vTaskDelay(5 / portTICK_PERIOD_MS);
}
