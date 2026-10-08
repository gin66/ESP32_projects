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
#include "can_ids.h"
#include "template.h"

// ************* LVGL and EEZ Studio UI includes *************
#include <lvgl.h>

#include "ui/screens.h"
#include "ui/ui.h"

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

// CYD board abstraction — all display/touch config moved to tpl_display.h
// See: prj_template/docs/cyd_board_abstraction.md

// LVGL draw buffer (resolution-dependent, computed from CYD dimensions)
uint32_t draw_buf[CYD_DRAW_BUF_SIZE / 4];

// Touchscreen — constructed with board-specific pins from tpl_display.h
// SPI bus is initialized by tpl_cyd_touch_init() for 2.8\" boards.
XPT2046_Touchscreen touchscreen(CYD_TOUCH_CS, CYD_TOUCH_IRQ);

// Global TFT_eSPI instance (required by TFT_eSPI library and tpl_display)
TFT_eSPI tft = TFT_eSPI();

#ifndef DEBUG_TOUCH
#define DEBUG_TOUCH 0  // Set to 1 to output touch events to Serial
#endif

// Touch coordinates
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
#define CAN_MSG_TYPE_RAW 0x00

// SML header (smart meter language)
static const uint8_t sml_header[8] = {0x1b, 0x1b, 0x1b, 0x1b,
                                      0x01, 0x01, 0x01, 0x01};

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

// Protocol analyzer statistics
static uint32_t g_proto_timestamp_cnt = 0;
static uint32_t g_proto_sml_cnt = 0;
static uint32_t g_proto_time_broadcast_cnt = 0;
static uint32_t g_proto_unknown_cnt = 0;
static uint32_t g_proto_sml_lost_cnt = 0;
static uint32_t g_proto_sml_empty_cnt = 0;

// Protocol analyzer state
static uint16_t g_sml_base_id = 0;
static uint16_t g_sml_received_length = 0;
static uint8_t g_sml_buffer[258];  // SML message buffer
static bool g_sml_active = false;

// Display mode
enum proto_display_mode_t : uint8_t {
  PROTO_MODE_RAW = 0,
  PROTO_MODE_ANALYZER = 1
};
static proto_display_mode_t g_proto_display_mode = PROTO_MODE_RAW;

// WiFi-derived CAN node ID (last octet of IP)
static uint8_t g_can_node_id = 0;

bool g_show_interval = false;
static volatile bool g_bus_error_flag = false;

// ************* CAN ID Table Functions *************

static void can_table_update(const twai_message_t* msg) {
  can_id_entry_t* entry = nullptr;

  for (int i = 0; i < MAX_CAN_IDS; i++) {
    if (g_can_id_table[i].used &&
        g_can_id_table[i].identifier == msg->identifier) {
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

  entry->data_changed =
      (entry->data_len != msg->data_length_code ||
       memcmp(entry->data, msg->data, msg->data_length_code) != 0);

  entry->data_len = msg->data_length_code;
  memcpy(entry->data, msg->data, msg->data_length_code);
  memcpy(entry->last_data, msg->data, msg->data_length_code);

  g_can_rx_count++;
}

// ************* Protocol Analyzer *************

enum can_protocol_t : uint8_t {
  PROTO_UNKNOWN = 0,
  PROTO_TIMESTAMP = 1,      // 0x555 - time broadcast from 15_power_log
  PROTO_SML = 2,            // 0x600/0x700 - smart meter SML protocol
  PROTO_TIME_BROADCAST = 3  // custom time broadcast (type=0x01)
};

typedef struct {
  can_protocol_t protocol;
  char label[32];
  char decoded[64];
} can_protocol_info_t;

static void decode_protocol(uint32_t id, const uint8_t* data, uint8_t len,
                            can_protocol_info_t* info) {
  memset(info, 0, sizeof(*info));
  info->protocol = PROTO_UNKNOWN;

  // Check for timestamp protocol (0x555)
  if (id == CAN_ID_TIMESTAMP) {
    info->protocol = PROTO_TIMESTAMP;
    snprintf(info->label, sizeof(info->label), "TIMESTAMP");
    if (len >= 4) {
      snprintf(info->decoded, sizeof(info->decoded), "%02d:%02d:%02d (day %d)",
               data[2], data[1], data[0], data[3]);
    }
    g_proto_timestamp_cnt++;
    return;
  }

  // Check for SML smart meter protocol (0x600/0x700)
  if ((id & ~0x1FF) == CAN_ID_STROMZAEHLER_INFO_BASIS) {
    info->protocol = PROTO_SML;
    snprintf(info->label, sizeof(info->label), "SML");

    // Check for empty alive message
    if (len == 0) {
      snprintf(info->decoded, sizeof(info->decoded), "ALIVE");
      g_proto_sml_empty_cnt++;
      return;
    }

    // Check first two bytes for message length
    if (len >= 2) {
      uint16_t msg_len = data[1];
      msg_len <<= 8;
      msg_len += data[0];

      uint16_t current_offset = id & 0xFF;

      if (current_offset == 0) {
        g_sml_received_length = 0;
        g_sml_base_id = id & ~0xFF;
        g_sml_active = true;
        memset(g_sml_buffer, 0, sizeof(g_sml_buffer));
      }

      // Copy data into buffer
      uint16_t off = current_offset;
      off <<= 3;
      for (uint8_t i = 0; i < len; i++) {
        if (off < sizeof(g_sml_buffer)) {
          g_sml_buffer[off++] = data[i];
        }
      }
      g_sml_received_length += len;

      uint16_t expected = msg_len + 2;
      if (g_sml_received_length >= expected) {
        // Complete message received
        g_sml_active = false;
        g_sml_received_length = 0;

        // Check SML header (0x1B 0x1B 0x1B 0x1B 0x01 0x01 0x01 0x01)
        if (g_sml_received_length > 16) {
          snprintf(info->decoded, sizeof(info->decoded), "SML %d B OK",
                   msg_len);
          g_proto_sml_cnt++;
          return;
        } else {
          snprintf(info->decoded, sizeof(info->decoded), "SML %d B INCOMPLETE",
                   msg_len);
          g_proto_sml_lost_cnt++;
          return;
        }
      }

      snprintf(info->decoded, sizeof(info->decoded), "SML %u/%u B",
               g_sml_received_length, expected);
      return;
    }
  }

  // Check for time broadcast protocol (type = 0x01)
  if (len >= 5 && data[0] == CAN_MSG_TYPE_TIME) {
    info->protocol = PROTO_TIME_BROADCAST;
    snprintf(info->label, sizeof(info->label), "TIME-BCAST");
    if (len >= 5) {
      snprintf(info->decoded, sizeof(info->decoded), "%02d:%02d:%02d (day %d)",
               data[4], data[3], data[2], data[1]);
    }
    g_proto_time_broadcast_cnt++;
    return;
  }

  // Unknown protocol - show raw data
  snprintf(info->label, sizeof(info->label), "RAW");
  size_t offset = 0;
  for (uint8_t i = 0; i < len && offset < sizeof(info->decoded) - 4; i++) {
    offset += snprintf(info->decoded + offset, sizeof(info->decoded) - offset,
                       "%02X ", data[i]);
  }
  g_proto_unknown_cnt++;
}

// ************* CAN ID Formatting *************

static void format_can_id(char* buf, size_t buf_size, uint32_t id,
                          bool is_extended) {
  if (is_extended) {
    snprintf(buf, buf_size, "%08X", id);
  } else {
    snprintf(buf, buf_size, "%03X", id & 0x7FF);
  }
}

static void format_data_bytes(char* buf, size_t buf_size, const uint8_t* data,
                              uint8_t len) {
  buf[0] = '\0';
  size_t offset = 0;
  for (uint8_t i = 0; i < len && offset < buf_size - 4; i++) {
    offset += snprintf(buf + offset, buf_size - offset, "%02X ", data[i]);
  }
}

// ************* CAN Time Message Handling *************

static bool parse_time_message(const uint8_t* data, uint8_t len,
                               can_time_message_t* time_msg) {
  if (len < sizeof(can_time_message_t)) return false;
  memcpy(time_msg, data, sizeof(can_time_message_t));
  return time_msg->msg_type == CAN_MSG_TYPE_TIME;
}

static void format_time_string(char* buf, size_t buf_size,
                               const can_time_message_t* time_msg) {
  snprintf(buf, buf_size, "%02d:%02d:%02d (day %d)", time_msg->hours,
           time_msg->minutes, time_msg->seconds, time_msg->weekday);
}

// ************* LVGL Display Update (from CAN ID table) *************

// lv_label_set_text() invalidates the label's area even when the text is
// identical, which forces a redraw. On the 3.5" there are 23 rows, so blindly
// rewriting every label queued ~70 invalidations per cycle and built a redraw
// backlog that stalled lv_task_handler() (and thus the touch poll). Only write
// when the string actually changed.
static void label_set_text_cached(lv_obj_t* label, const char* text,
                                  char* cache, size_t cache_len) {
  if (cache && strncmp(cache, text, cache_len) == 0) return;
  if (cache) {
    strncpy(cache, text, cache_len - 1);
    cache[cache_len - 1] = '\0';
  }
  lv_label_set_text(label, text);
}

static void update_can_display(void) {
  char stats_buf[128];
  if (g_proto_display_mode == PROTO_MODE_ANALYZER) {
    snprintf(stats_buf, sizeof(stats_buf),
             "Proto: TS:%lu SML:%lu TB:%lu Ukn:%lu", g_proto_timestamp_cnt,
             g_proto_sml_cnt, g_proto_time_broadcast_cnt, g_proto_unknown_cnt);
  } else {
    snprintf(stats_buf, sizeof(stats_buf), "RX: %lu | TX: %lu | Err: %lu",
             g_can_rx_count, g_can_tx_count, g_can_error_cnt);
  }
  static char stats_cache[128];
  label_set_text_cached(objects.lblStats, stats_buf, stats_cache,
                        sizeof(stats_cache));
  lv_obj_set_style_text_color(
      objects.lblStats,
      g_bus_error_flag ? lv_color_hex(0xFF0000) : lv_color_hex(0x00FF00),
      LV_PART_MAIN);

  char ip_buf[64];
  snprintf(ip_buf, sizeof(ip_buf), "IP: %u.%u.%u.%u | Node: 0x%02X",
           WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2],
           WiFi.localIP()[3], g_can_node_id);
  static char ip_cache[64];
  label_set_text_cached(objects.lblIP, ip_buf, ip_cache, sizeof(ip_cache));

  for (int i = 0; i < MAX_CAN_IDS; i++) {
    can_id_entry_t* entry = &g_can_id_table[i];
    lv_color_t color;

    if (entry->used && entry->data_changed) {
      color = lv_color_hex(0x00FF00);
    } else {
      color = lv_color_hex(0xCCCCCC);
    }

    // Per-row text caches; MAX_CAN_IDS is 23 on the 3.5", so caching each of
    // the 3 labels per row matters.
    static char id_cache[MAX_CAN_IDS][16];
    static char data_cache[MAX_CAN_IDS][64];
    static char time_cache[MAX_CAN_IDS][64];

    if (entry->used) {
      char id_buf[16];
      format_can_id(id_buf, sizeof(id_buf), entry->identifier,
                    entry->is_extended);
      label_set_text_cached(objects.lblId[i], id_buf, id_cache[i],
                            sizeof(id_cache[i]));

      // Protocol analyzer mode
      if (g_proto_display_mode == PROTO_MODE_ANALYZER) {
        can_protocol_info_t proto_info;
        decode_protocol(entry->identifier, entry->data, entry->data_len,
                        &proto_info);

        char label_buf[32];
        snprintf(label_buf, sizeof(label_buf), "%s", proto_info.label);
        label_set_text_cached(objects.lblData[i], label_buf, data_cache[i],
                              sizeof(data_cache[i]));

        char decoded_buf[64];
        snprintf(decoded_buf, sizeof(decoded_buf), "%s", proto_info.decoded);
        label_set_text_cached(objects.lblTime[i], decoded_buf, time_cache[i],
                              sizeof(time_cache[i]));

        // Color code by protocol type
        switch (proto_info.protocol) {
          case PROTO_TIMESTAMP:
            lv_obj_set_style_text_color(objects.lblData[i],
                                        lv_color_hex(0x00FFFF), LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i],
                                        lv_color_hex(0x00FFFF), LV_PART_MAIN);
            break;
          case PROTO_SML:
            lv_obj_set_style_text_color(objects.lblData[i],
                                        lv_color_hex(0xFF8800), LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i],
                                        lv_color_hex(0xFF8800), LV_PART_MAIN);
            break;
          case PROTO_TIME_BROADCAST:
            lv_obj_set_style_text_color(objects.lblData[i],
                                        lv_color_hex(0x88FF00), LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i],
                                        lv_color_hex(0x88FF00), LV_PART_MAIN);
            break;
          default:
            lv_obj_set_style_text_color(objects.lblData[i], color,
                                        LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i], color,
                                        LV_PART_MAIN);
            break;
        }
      } else {
        // Raw mode - show original data
        char data_buf[24];
        format_data_bytes(data_buf, sizeof(data_buf), entry->data,
                          entry->data_len);
        label_set_text_cached(objects.lblData[i], data_buf, data_cache[i],
                              sizeof(data_cache[i]));

        char time_buf[16];
        if (g_show_interval) {
          if (entry->interval_ms > 0) {
            snprintf(time_buf, sizeof(time_buf), "%.1fs",
                     entry->interval_ms / 1000.0);
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
            snprintf(time_buf, sizeof(time_buf), "%lus",
                     entry->last_timestamp / 1000);
          }
        }
        label_set_text_cached(objects.lblTime[i], time_buf, time_cache[i],
                              sizeof(time_cache[i]));
        lv_obj_set_style_text_color(objects.lblId[i], color, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.lblData[i], color, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.lblTime[i], color, LV_PART_MAIN);
      }
    } else {
      label_set_text_cached(objects.lblId[i], "---", id_cache[i],
                            sizeof(id_cache[i]));
      label_set_text_cached(objects.lblData[i], "--", data_cache[i],
                            sizeof(data_cache[i]));
      label_set_text_cached(objects.lblTime[i], "--", time_cache[i],
                            sizeof(time_cache[i]));
    }
  }
}

// ************* Touchscreen Read Callback *************

// Protocol analyzer mode toggle area (top 30 pixels)
#define PROTO_TOGGLE_TOP 0
#define PROTO_TOGGLE_BOTTOM 30

static unsigned long g_last_toggle_time = 0;
static const unsigned long TOGGLE_DEBOUNCE_MS =
    500;  // Minimum 500ms between toggles

static void touchscreen_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
  // Protocol analyzer toggle: track press/release in header area (top 30px)
  static unsigned long g_last_press_time = 0;
  static int32_t g_last_press_y = 0;
  static bool g_header_touched = false;
  bool pressed;
  int32_t ly = 0;

  // The abstraction handles the full rotation pipeline internally:
  // Touch ADC → XPT2046 rotation → calibration → logical rotation
  tpl_cyd_touch_read(touchscreen, data, &pressed,
                     CYD_WIDTH, CYD_HEIGHT, TFT_ROTATION);

  if (pressed) {
    // Get the rotated coordinates from the abstraction
    int32_t lx = data->point.x;
    ly = data->point.y;

#if DEBUG_TOUCH
    // Rate-limited: printing every poll blocks on the UART TX buffer once the
    // host stops draining, which stalls lv_task_handler() and makes touch lag
    // by whole seconds followed by a burst of queued lines.
    static unsigned long g_touch_log_ms = 0;
    if (millis() - g_touch_log_ms >= 100) {
      g_touch_log_ms = millis();
      TS_Point p = touchscreen.getPoint();
      Serial.printf("[TOUCH] Raw: x=%d y=%d z=%d lx=%d ly=%d (screen %dx%d)\n",
                    p.x, p.y, p.z, lx, ly, CYD_SCREEN_WIDTH, CYD_SCREEN_HEIGHT);
    }
#endif

    // Track if we're touching the header area
    if (ly <= PROTO_TOGGLE_BOTTOM) {
      g_last_press_time = millis();
      g_last_press_y = ly;
      g_header_touched = true;
    } else {
      g_header_touched = false;
    }

    g_last_touch_time = millis();
    if (g_backlight_target != BACKLIGHT_FULL) {
      g_backlight_target = BACKLIGHT_FULL;
      g_fade_start_time = millis();
    }
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
    // Check for header tap on release
    if (g_header_touched && ly <= PROTO_TOGGLE_BOTTOM &&
        (millis() - g_last_press_time < 300) &&
        (millis() - g_last_toggle_time > TOGGLE_DEBOUNCE_MS)) {
      g_proto_display_mode = (g_proto_display_mode == PROTO_MODE_RAW)
                                 ? PROTO_MODE_ANALYZER
                                 : PROTO_MODE_RAW;
      Serial.printf("[CAN] Protocol analyzer %s\n",
                    g_proto_display_mode == PROTO_MODE_ANALYZER ? "ON" : "OFF");
      g_last_toggle_time = millis();
    }
    g_header_touched = false;
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
      int16_t delta =
          (int16_t)g_backlight_target - (int16_t)g_backlight_fade_start;
      g_backlight_current =
          g_backlight_fade_start + (delta * elapsed / fade_duration);
    }
    ledcWrite(7, g_backlight_current);
  }
}

// ************* LVGL Display Initialization *************

static void log_print_cb(lv_log_level_t level, const char* buf) {
  LV_UNUSED(level);
  // No Serial.flush() here — it blocks the loop for the full 115200-baud
  // transfer, which stalls lv_task_handler() and shows up as touch lag.
  Serial.println(buf);
}

static void lv_init_display(void) {
  lv_log_register_print_cb(log_print_cb);

  // Initialize LVGL
  lv_init();

  // Use the CYD abstraction — handles display init, touch init, and rotation
  // pipeline internally (ADC → XPT2046 rotation → calibration → logical rotation)
  lv_display_t* disp = tpl_cyd_display_init(TFT_ROTATION);
  tpl_cyd_touch_init(touchscreen, TFT_ROTATION);

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

    // Clear flag when bus is no longer bus-off or recovering
    if (twaistatus.state == TWAI_STATE_RUNNING) {
      g_bus_error_flag = false;
    }

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
    Serial.printf("[WiFi] Connected! IP: %s\n",
                  WiFi.localIP().toString().c_str());
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

  // Command handler to toggle protocol analyzer mode
  tpl_command_setup([](enum Command cmd) {
    switch (cmd) {
      case CmdIdle:
        // Toggle protocol analyzer mode
        g_proto_display_mode = (g_proto_display_mode == PROTO_MODE_RAW)
                                   ? PROTO_MODE_ANALYZER
                                   : PROTO_MODE_RAW;
        Serial.printf(
            "[CAN] Protocol analyzer %s\n",
            g_proto_display_mode == PROTO_MODE_ANALYZER ? "ON" : "OFF");
        break;
      default:
        break;
    }
  });

  // Initialize LVGL display and touchscreen
  lv_init_display();

  // Initialize CYD backlight PWM (channel 7, full brightness)
  tpl_cyd_backlight_init(7, BACKLIGHT_FULL);

  // Initialize CYD RGB LED pins (HIGH = off on both boards)
  tpl_cyd_rgb_init(true);

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
    uint32_t alerts_to_enable =
        TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR |
        TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_BUS_RECOVERED;
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
