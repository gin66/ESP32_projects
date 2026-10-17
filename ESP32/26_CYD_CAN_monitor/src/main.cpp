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

#define SW_NAME_REV "26_CYD_CAN_monitor v1.2"

// ************* ESP32_projects template includes *************
#include "haus_can.h"
#include "template.h"

// ************* LVGL and EEZ Studio UI includes *************
#include <lvgl.h>

#include "protocol_detail.h"
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
#define DEBUG_TOUCH \
  0  // Prints the raw ADC point and the point reported to LVGL
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

// Table entry for a unique CAN ID (typedef in protocol_detail.h).

can_id_entry_t g_can_id_table[MAX_CAN_IDS];

/* Exposed to protocol_detail.cpp for array bounds checking. */
int g_max_can_ids = MAX_CAN_IDS;

/* SML reassembly array — exposed to protocol_detail.cpp (non-static, matches
 * header). */
sml_reassembly_t g_sml[SML_MAX_MESSAGES];
int g_sml_count = SML_MAX_MESSAGES;

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

// Heating (AVR/01_heizung_temp) protocol statistics
static uint32_t g_proto_heizung_id_cnt = 0;
static uint32_t g_proto_heizung_temp_cnt = 0;
static uint32_t g_proto_heizung_pwm_cnt = 0;
static uint32_t g_proto_heizung_adc_cnt = 0;
static uint32_t g_proto_heizung_stats_cnt = 0;
static uint32_t g_proto_heizung_ee_cnt = 0;
/* Set when an EEPROM report arrives with the PWM-fault flag, and by the
 * log's own anomaly flag. Both are conditions where the node looks alive
 * but is not delivering what its status claims, so they are surfaced
 * on the stats line rather than left inside a decoded frame. */
static uint8_t g_proto_heizung_pwmfail = 0;
static uint8_t g_proto_heizung_ee_anom = 0;

// Protocol analyzer state
static uint16_t g_sml_base_id = 0;
static uint16_t g_sml_received_length = 0;
static uint8_t g_sml_buffer[258];  // SML message buffer
static bool g_sml_active = false;

/* SML reassembly array — defined in the header, non-static here. */
static bool validate_sml_header(const uint8_t* buf, size_t buf_size) {
  if (buf_size < 16) return false;
  /* SML frames begin with 0x1B 0x1B 0x1B 0x1B 0x01 0x01 0x01 0x01
   * repeated.  Check the first 8 bytes (one full repetition). */
  static const uint8_t sml_header[8] = {0x1b, 0x1b, 0x1b, 0x1b,
                                        0x01, 0x01, 0x01, 0x01};
  return (memcmp(buf, sml_header, 8) == 0);
}

// Display mode
enum proto_display_mode_t : uint8_t {
  PROTO_MODE_RAW = 0,
  PROTO_MODE_ANALYZER = 1
};
static proto_display_mode_t g_proto_display_mode = PROTO_MODE_RAW;

/* Set from the LVGL input-read callback, applied from loop(). The read
 * callback must only report input; mutating the widget tree from inside it is
 * not safe and can corrupt LVGL's internal indev state. */
static volatile bool g_columns_dirty = false;

// WiFi-derived CAN node ID (last octet of IP)
static uint8_t g_can_node_id = 0;

/* Defined in screens.c via the LV_EVENT_CLICKED handler on the "Last Seen"
 * header; declared here because the update path reads it too. */
extern bool g_show_interval;

bool g_show_interval = false;
static volatile bool g_bus_error_flag = false;

// ************* UI Legibility Overrides *************

/* EEZ Studio regenerates screens.c, so the styling tweaks that survive a
 * regeneration live here instead. The header labels get an opaque black
 * plate — the row backgrounds are only 0x111111/0x1A1A1A and thin text over
 * them washes out badly on this panel. */
static void ui_apply_legibility(void) {
  /* Style the screen the UI actually lives on. lv_screen_active() is not
   * reliable here: ui_init() ends with lv_scr_load_anim(), so the active
   * screen during this call can still be the boot screen. */
  lv_obj_t* screen = (objects.main != NULL) ? objects.main : lv_screen_active();
  if (screen != NULL) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  }

  lv_obj_t* labels[] = {objects.lblStats,      objects.lblIP,
                        objects.lblClearBtn,   objects.lblHeaderId,
                        objects.lblHeaderData, objects.lblHeaderTime};
  for (uint32_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
    if (labels[i] == NULL) continue;
    lv_obj_set_style_bg_color(labels[i], lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(labels[i], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(labels[i], 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(labels[i], 0, LV_PART_MAIN);
  }
  Serial.printf("[UI] screen=%p lblStats=%p lblIP=%p clear=%p\n", screen,
                objects.lblStats, objects.lblIP, objects.lblClearBtn);
  lv_obj_set_style_text_color(objects.lblClearBtn, lv_color_hex(0xE0E0E0),
                              LV_PART_MAIN);
  lv_obj_set_style_text_color(objects.lblIP, lv_color_hex(0xB0D0FF),
                              LV_PART_MAIN);
}

// ************* Clear All Counters *************

extern "C" void clear_all_counters() {
  g_can_rx_count = 0;
  g_can_tx_count = 0;
  g_can_error_cnt = 0;
  g_proto_timestamp_cnt = 0;
  g_proto_sml_cnt = 0;
  g_proto_time_broadcast_cnt = 0;
  g_proto_unknown_cnt = 0;
  g_proto_sml_lost_cnt = 0;
  g_proto_sml_empty_cnt = 0;
  g_proto_heizung_id_cnt = 0;
  g_proto_heizung_temp_cnt = 0;
  g_proto_heizung_pwm_cnt = 0;
  g_proto_heizung_adc_cnt = 0;
  g_proto_heizung_stats_cnt = 0;
  g_proto_heizung_ee_cnt = 0;
  g_proto_heizung_pwmfail = 0;
  g_proto_heizung_ee_anom = 0;
  Serial.println("[CAN] All counters cleared");
}

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
        entry->frame_count = 0;
        entry->changed_count = 0;
        entry->interval_min_ms = 0;
        entry->interval_max_ms = 0;
        memset(entry->last_data, 0, sizeof(entry->last_data));
        break;
      }
    }
  }

  if (entry == nullptr) return;

  unsigned long now = millis();
  if (entry->last_timestamp != 0) {
    entry->interval_ms = now - entry->last_timestamp;
    /* Track interval min/max (skip the first reading which is 0) */
    if (entry->interval_ms > 0) {
      if (entry->frame_count == 0 ||
          entry->interval_ms < entry->interval_min_ms) {
        entry->interval_min_ms = entry->interval_ms;
      }
      if (entry->frame_count == 0 ||
          entry->interval_ms > entry->interval_max_ms) {
        entry->interval_max_ms = entry->interval_ms;
      }
    }
  }
  entry->last_timestamp = now;
  entry->last_time = time(nullptr);

  entry->data_changed =
      (entry->data_len != msg->data_length_code ||
       memcmp(entry->data, msg->data, msg->data_length_code) != 0);

  entry->data_len = msg->data_length_code;
  memcpy(entry->data, msg->data, msg->data_length_code);
  memcpy(entry->last_data, msg->data, msg->data_length_code);

  /* Update statistics */
  entry->frame_count++;
  if (entry->data_changed) {
    entry->changed_count++;
  }

  g_can_rx_count++;
}

// ************* Protocol Analyzer *************

typedef struct {
  haus_protocol_t protocol;
  char label[32];
  char decoded[64];
} can_protocol_info_t;

static void decode_protocol(uint32_t id, const uint8_t* data, uint8_t len,
                            can_protocol_info_t* info) {
  memset(info, 0, sizeof(*info));
  info->protocol = HAUS_PROTO_NONE;

  // Cast the CAN-ID directly to haus_protocol_t — the enum value IS the ID.
  haus_protocol_t proto = (haus_protocol_t)(id & 0x7FF);

  // Sensor identity frames occupy 0x180 .. 0x180+HAUS_CAN_MAX_SENSORS-1, one
  // per sensor rank.  They are not a single enum value, so match the range.
  uint8_t rank = haus_can_get_rank((uint16_t)(id & 0x7FF));
  if (rank != (uint8_t)-1) {
    info->protocol = HAUS_PROTO_HEIZUNG_ID;
    snprintf(info->label, sizeof(info->label), "ID 0x180+%u", rank);
    if (len >= 8) {
      haus_can_identity_t id_data;
      haus_can_get_identity(data, &id_data);
      snprintf(info->decoded, sizeof(info->decoded),
               "%02X:%02X:%02X:%02X:%02X:%02X", id_data.rom[1], id_data.rom[2],
               id_data.rom[3], id_data.rom[4], id_data.rom[5], id_data.rom[6]);
    }
    g_proto_heizung_id_cnt++;
    return;
  }

  /* For every enum-matched ID the value itself is the protocol tag. */
  info->protocol = proto;

  switch (proto) {
    /* ── Temperature data (0x200) ───────────────────────────────────── */
    case HAUS_PROTO_HEIZUNG_TEMP: {
      info->label[0] = 'T';
      info->label[1] = 'E';
      info->label[2] = 'M';
      info->label[3] = 'P';
      info->label[4] = '\0';
      if (len >= 8) {
        haus_can_temperature_t t;
        haus_can_get_temperature(data, &t);
        char buf[64];
        buf[0] = '\0';
        for (int i = 0; i < HAUS_CAN_MAX_SENSORS; i++) {
          int16_t val = t.temperature[i];
          if (val == HAUS_CAN_TEMP_EMPTY) {
            strncat(buf, "-- ", sizeof(buf) - strlen(buf) - 1);
          } else {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%.2f ", val / 100.0);
            strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
          }
        }
        snprintf(info->decoded, sizeof(info->decoded), "%s", buf);
      }
      g_proto_heizung_temp_cnt++;
      return;
    }

    /* ── PWM report (0x280) & command (0x240) ───────────────────────── */
    case HAUS_PROTO_HEIZUNG_PWM_RPT:
    case HAUS_PROTO_HEIZUNG_PWM: {
      info->label[0] = 'P';
      info->label[1] = 'W';
      info->label[2] = 'M';
      info->label[3] = '\0';
      if (len >= 5) {
        haus_can_pwm_t p;
        haus_can_get_pwm(data, &p);
        snprintf(info->decoded, sizeof(info->decoded), "duty=%d/1000 f=%dHz %s",
                 p.duty_permille, p.frequency_hz,
                 (p.flags & PWM_FLAG_ACTIVE) ? "ACTIVE" : "OFF");
      }
      g_proto_heizung_pwm_cnt++;
      return;
    }

    /* ── ADC report (0x2C0) ─────────────────────────────────────────── */
    case HAUS_PROTO_HEIZUNG_ADC: {
      info->label[0] = 'A';
      info->label[1] = 'D';
      info->label[2] = 'C';
      info->label[3] = '\0';
      if (len >= 7) {
        haus_can_adc_t a;
        haus_can_get_adc(data, &a);
        snprintf(info->decoded, sizeof(info->decoded),
                 "max=%u min=%u pp=%u flags=0x%02X", a.max_count, a.min_count,
                 a.pp_count, a.flags);
      }
      g_proto_heizung_adc_cnt++;
      return;
    }

    /* ── EEPROM restore-point log (0x2C8) ─────────────────────────────── */
    case HAUS_PROTO_HEIZUNG_EE: {
      info->label[0] = 'E';
      info->label[1] = 'E';
      info->label[2] = '\0';
      if (len >= 8) {
        haus_can_ee_t e;
        const char* src;
        haus_can_get_ee(data, &e);

        switch (e.flags & EE_SRC_MASK) {
          case EE_SRC_VIRGIN << EE_SRC_SHIFT:
            src = "virgin";
            break;
          case EE_SRC_MARKER << EE_SRC_SHIFT:
            src = "marker";
            break;
          case EE_SRC_WINDOW << EE_SRC_SHIFT:
            src = "window";
            break;
          case EE_SRC_ANOMALY << EE_SRC_SHIFT:
            src = "ANOMALY";
            break;
          case EE_SRC_INVALID << EE_SRC_SHIFT:
            src = "bad-newest";
            break;
          default:
            src = "?";
            break;
        }

        snprintf(info->decoded, sizeof(info->decoded),
                 "rest=%u rec=%u arc=%u..%u w=%u %s%s%s", e.restored_duty,
                 e.records, e.oldest, e.newest, e.writes, src,
                 (e.oldest > e.newest) ? " wrap" : "",
                 (e.flags & EE_FLAG_PWMFAIL) ? " PWMFAIL" : "");

        g_proto_heizung_pwmfail =
            (uint8_t)((e.flags & EE_FLAG_PWMFAIL) ? 1 : 0);
        g_proto_heizung_ee_anom =
            (uint8_t)((e.flags & EE_FLAG_ANOMALY) ? 1 : 0);
      }
      g_proto_heizung_ee_cnt++;
      return;
    }

    /* ── Stats report (0x300) ───────────────────────────────────────── */
    case HAUS_PROTO_HEIZUNG_STATS: {
      info->label[0] = 'S';
      info->label[1] = 'T';
      info->label[2] = 'A';
      info->label[3] = 'T';
      info->label[4] = 'S';
      info->label[5] = '\0';
      if (len >= 8) {
        haus_can_stats_t s;
        haus_can_get_stats(data, &s);
        snprintf(info->decoded, sizeof(info->decoded),
                 "sent=%u recv=%u ref=%u eflg=0x%02X canstat=0x%02X", s.sent,
                 s.received, s.refused, s.eflg, s.canstat);
      }
      g_proto_heizung_stats_cnt++;
      return;
    }

    /* ── Timestamp (0x555) ──────────────────────────────────────────── */
    case HAUS_PROTO_TIMESTAMP: {
      info->label[0] = 'T';
      info->label[1] = 'S';
      info->label[2] = '\0';
      if (len >= 4) {
        haus_can_timestamp_t ts;
        haus_can_get_timestamp(data, &ts);
        snprintf(info->decoded, sizeof(info->decoded),
                 "%02d:%02d:%02d (day %d)", ts.hours, ts.minutes, ts.seconds,
                 ts.weekday);
      }
      g_proto_timestamp_cnt++;
      return;
    }

    /* ── SML smart meter (0x600/0x700) ──────────────────────────────── */
    default:
      if ((id & ~0x1FF) == CAN_ID_STROMZAEHLER_INFO_BASIS) {
        info->label[0] = 'S';
        info->label[1] = 'M';
        info->label[2] = 'L';
        info->label[3] = '\0';

        /* Check for empty alive message */
        if (len == 0) {
          snprintf(info->decoded, sizeof(info->decoded), "ALIVE");
          g_proto_sml_empty_cnt++;
          return;
        }

        /* Check first two bytes for message length */
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

          /* Copy data into buffer */
          uint16_t off = current_offset;
          off <<= 3;
          for (uint8_t i = 0; i < len; i++) {
            if (off < sizeof(g_sml_buffer)) {
              g_sml_buffer[off++] = data[i];
            }
          }
          g_sml_received_length += len;

          uint16_t expected = msg_len + 2;

          /* Mirror reassembly progress into g_sml[] (index 0 = 0x600 base,
           * 1 = 0x700) so the detail screen can show it. Without this the
           * array is exposed but never written and SML detail falls back to a
           * raw hex dump. */
          int slot = ((id & ~0xFF) == CAN_ID_STROMZAEHLER_INFO_ALT) ? 1 : 0;
          if (current_offset == 0) {
            g_sml[slot].frames_received = 0;
            g_sml[slot].complete = false;
          }
          g_sml[slot].base_id = id & ~0xFF;
          g_sml[slot].expected_length = expected;
          g_sml[slot].received_length = g_sml_received_length;
          g_sml[slot].frames_received++;
          g_sml[slot].last_frame_time = time(nullptr);
          g_sml[slot].active = true;

          if (g_sml_received_length >= expected) {
            /* Complete message received — validate before clearing. */
            uint16_t saved_len = g_sml_received_length;
            bool ok = (saved_len > 16 &&
                       validate_sml_header(g_sml_buffer, saved_len));
            g_sml_active = false;
            g_sml_received_length = 0;
            g_sml[slot].active = false;
            g_sml[slot].complete = ok;
            g_sml[slot].received_length = saved_len;

            /* Check SML header */
            if (ok) {
              snprintf(info->decoded, sizeof(info->decoded), "SML %d B OK",
                       msg_len);
              g_proto_sml_cnt++;
              return;
            } else {
              snprintf(info->decoded, sizeof(info->decoded),
                       "SML %d B INCOMPLETE", msg_len);
              g_proto_sml_lost_cnt++;
              return;
            }
          }

          snprintf(info->decoded, sizeof(info->decoded), "SML %u/%u B",
                   g_sml_received_length, expected);
          return;
        }
      }
      break;
  }

  /* ── Time broadcast (custom, type = 0x01) ─────────────────────────── */
  if (len >= sizeof(can_time_message_t) && data[0] == CAN_MSG_TYPE_TIME) {
    info->protocol = (haus_protocol_t)0xFFFF; /* non-haus marker */
    snprintf(info->label, sizeof(info->label), "TIME-BCAST");
    can_time_message_t time_msg;
    memcpy(&time_msg, data, sizeof(can_time_message_t));
    snprintf(info->decoded, sizeof(info->decoded), "%02d:%02d:%02d (day %d)",
             time_msg.hours, time_msg.minutes, time_msg.seconds,
             time_msg.weekday);
    g_proto_time_broadcast_cnt++;
    return;
  }

  /* Unknown protocol — show raw data */
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
  // Each byte renders as "XX " — 3 chars — plus the terminating NUL, so a byte
  // fits only while offset+4 <= buf_size. The old `offset < buf_size - 4` was
  // one byte short and silently dropped the 8th byte of a full frame.
  for (uint8_t i = 0; i < len && offset + 4 <= buf_size; i++) {
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
  char stats_buf[192];
  if (g_proto_display_mode == PROTO_MODE_ANALYZER) {
    snprintf(stats_buf, sizeof(stats_buf),
             "Proto: TS:%lu SML:%lu TB:%lu HID:%lu HTmp:%lu "
             "HPWM:%lu HADC:%lu HSt:%lu HEE:%lu%s%s Ukn:%lu",
             g_proto_timestamp_cnt, g_proto_sml_cnt, g_proto_time_broadcast_cnt,
             g_proto_heizung_id_cnt, g_proto_heizung_temp_cnt,
             g_proto_heizung_pwm_cnt, g_proto_heizung_adc_cnt,
             g_proto_heizung_stats_cnt, g_proto_heizung_ee_cnt,
             g_proto_heizung_ee_anom ? " EE-ANOM" : "",
             g_proto_heizung_pwmfail ? " PWMFAIL" : "", g_proto_unknown_cnt);
  } else {
    snprintf(stats_buf, sizeof(stats_buf), "RX: %lu | TX: %lu | Err: %lu",
             g_can_rx_count, g_can_tx_count, g_can_error_cnt);
  }
  static char stats_cache[192];
  label_set_text_cached(objects.lblStats, stats_buf, stats_cache,
                        sizeof(stats_cache));
  lv_obj_set_style_text_color(
      objects.lblStats,
      g_bus_error_flag ? lv_color_hex(0xFF4040) : lv_color_hex(0x00FF00),
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
      color = lv_color_hex(0x7CFF7C);  // highlight, still readable at 14px
    } else {
      color = lv_color_hex(0xE0E0E0);
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
        if (g_show_interval) {
          // "Last Seen" also switches the third column in protocol mode,
          // otherwise the tap looks like it did nothing while in this view
          if (entry->interval_ms > 0) {
            snprintf(decoded_buf, sizeof(decoded_buf), "%.1fs",
                     entry->interval_ms / 1000.0);
          } else {
            snprintf(decoded_buf, sizeof(decoded_buf), "--");
          }
        } else {
          snprintf(decoded_buf, sizeof(decoded_buf), "%s", proto_info.decoded);
        }
        label_set_text_cached(objects.lblTime[i], decoded_buf, time_cache[i],
                              sizeof(time_cache[i]));

        // Color code by protocol type
        switch (proto_info.protocol) {
          case HAUS_PROTO_HEIZUNG_ID:
            lv_obj_set_style_text_color(objects.lblData[i],
                                        lv_color_hex(0xFFFF66), LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i],
                                        lv_color_hex(0xFFFF66), LV_PART_MAIN);
            break;
          case HAUS_PROTO_HEIZUNG_TEMP:
            lv_obj_set_style_text_color(objects.lblData[i],
                                        lv_color_hex(0x66FF66), LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i],
                                        lv_color_hex(0x66FF66), LV_PART_MAIN);
            break;
          case HAUS_PROTO_HEIZUNG_PWM_RPT:
          case HAUS_PROTO_HEIZUNG_PWM:
            lv_obj_set_style_text_color(objects.lblData[i],
                                        lv_color_hex(0xFF99FF), LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i],
                                        lv_color_hex(0xFF99FF), LV_PART_MAIN);
            break;
          case HAUS_PROTO_HEIZUNG_ADC:
            lv_obj_set_style_text_color(objects.lblData[i],
                                        lv_color_hex(0xFFCC66), LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i],
                                        lv_color_hex(0xFFCC66), LV_PART_MAIN);
            break;
          case HAUS_PROTO_HEIZUNG_EE:
            /* Red when the log is in a state the node cannot be trusted from:
             * a lost restore point, or a recorded output-path failure. */
            lv_obj_set_style_text_color(objects.lblData[i],
                                        lv_color_hex((g_proto_heizung_ee_anom ||
                                                      g_proto_heizung_pwmfail)
                                                         ? 0xFF6666
                                                         : 0x66CCCC),
                                        LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i],
                                        lv_color_hex(0x66CCCC), LV_PART_MAIN);
            break;
          case HAUS_PROTO_HEIZUNG_STATS:
            lv_obj_set_style_text_color(objects.lblData[i],
                                        lv_color_hex(0xCCCCCC), LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i],
                                        lv_color_hex(0xCCCCCC), LV_PART_MAIN);
            break;
          case HAUS_PROTO_TIMESTAMP:
            lv_obj_set_style_text_color(objects.lblData[i],
                                        lv_color_hex(0x66E0FF), LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i],
                                        lv_color_hex(0x66E0FF), LV_PART_MAIN);
            break;
          case HAUS_PROTO_SML_ALT:
          case HAUS_PROTO_SML:
            lv_obj_set_style_text_color(objects.lblData[i],
                                        lv_color_hex(0xFFC266), LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i],
                                        lv_color_hex(0xFFC266), LV_PART_MAIN);
            break;
          default:
            /* Non-haus protocols (e.g. TIME-BCAST) keep default color */
            lv_obj_set_style_text_color(objects.lblData[i], color,
                                        LV_PART_MAIN);
            lv_obj_set_style_text_color(objects.lblTime[i], color,
                                        LV_PART_MAIN);
            break;
        }
      } else {
        // Raw mode - show original data. Sized for a full 8-byte frame:
        // "XX " × 8 = 24 chars + NUL.
        char data_buf[32];
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

/* Protocol-analyzer toggle band along the top edge. It must not overlap the
 * Clear button (x >= 262, y 20..36) or tapping Clear would also flip the view
 * mode instead of clearing the counters. */
#define PROTO_TOGGLE_TOP 0
#define PROTO_TOGGLE_BOTTOM 30
#define CLEAR_BTN_X 262
#define TOUCH_DEBOUNCE_MS 40
#define PROTO_TOGGLE_MIN_X 0

static bool in_toggle_band(int32_t x, int32_t y) {
  if (y > PROTO_TOGGLE_BOTTOM || y < PROTO_TOGGLE_TOP) return false;
  return x < CLEAR_BTN_X;  // leave the Clear button its own area
}

/* Long press (>= 1.5 s) on the header band flips the panel's colour
 * inversion — the 2.8" needs INVON and it is worth being able to confirm that
 * on the device instead of per reflash. */
static unsigned long g_last_toggle_time = 0;
static const unsigned long TOGGLE_DEBOUNCE_MS =
    500;  // Minimum 500ms between toggles

static void touchscreen_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
  // Protocol analyzer toggle: track press/release in header area (top 30px)
  static unsigned long g_last_press_time = 0;
  static int32_t g_last_press_y = 0;
  static bool g_header_touched = false;
  static unsigned long g_press_start = 0;
  static bool g_longpress_done = false;
  bool pressed;
  int32_t ly = 0;

  LV_UNUSED(indev);

  // The abstraction handles the full rotation pipeline internally:
  // Touch ADC → XPT2046 rotation → calibration → logical rotation
  tpl_cyd_touch_read(touchscreen, data, &pressed, CYD_WIDTH, CYD_HEIGHT,
                     TFT_ROTATION);

  /* LVGL only raises click events if the input device actually reports a
   * press. Without this the whole UI stays "released" and no LV_EVENT_CLICKED
   * ever fires — the Clear button and the "Last Seen" header tap did nothing,
   * while the header toggle still worked because it reads `pressed` directly.
   *
   * Debounced: the XPT2046 IRQ bounces for a few ms around a press edge and
   * every bounce is a fresh press/release pair for LVGL, which showed up as
   * one tap firing the "Last Seen" handler several times. The state only
   * changes once it has been held for TOUCH_DEBOUNCE_MS. */
  {
    static uint8_t stable = 0;
    static uint8_t candidate = 0;
    static unsigned long candidate_since = 0;
    const uint8_t now_pressed = pressed ? 1 : 0;
    const unsigned long now_ms = millis();

    if (now_pressed != candidate) {
      candidate = now_pressed;
      candidate_since = now_ms;
    } else if (now_pressed != stable &&
               (now_ms - candidate_since) >= TOUCH_DEBOUNCE_MS) {
      stable = candidate;
    }
    data->state = stable ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    pressed = stable ? true : false;
  }

  if (pressed) {
    /* LVGL rotates the point from the driver, so data->point is still the
     * pre-transform value. lv_indev_get_point() returns the processed
     * (logical) point — that is the coordinate system the UI lives in. */
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    int32_t lx = pt.x;
    ly = pt.y;

#if DEBUG_TOUCH
    /* Rate-limited so a stalled host cannot block lv_task_handler(). The raw
     * ADC value and the point handed to LVGL together are enough to derive
     * the exact transform for this board: tap two opposite corners and read
     * the numbers off the serial log. */
    static unsigned long g_touch_log_ms = 0;
    if (millis() - g_touch_log_ms >= 400) {
      g_touch_log_ms = millis();
      TS_Point p = touchscreen.getPoint();
      Serial.printf("[TOUCH] adc=(%d,%d) -> lvgl=(%d,%d)\n", p.x, p.y,
                    (int)data->point.x, (int)data->point.y);
    }
#endif

    // Track if we're touching the header area. While the protocol detail
    // overlay is up, that band belongs to its Back button, so leave it alone.
    if (!is_protocol_detail_visible() && in_toggle_band(lx, ly)) {
      if (g_last_press_time == 0) {
        g_last_press_time = millis();
        g_press_start = millis();
      }
      if (!g_longpress_done && (millis() - g_press_start) >= 1500) {
        g_longpress_done = true;
        tpl_cyd_set_inversion(!tpl_cyd_get_inversion());
        Serial.printf("[UI] colour inversion %s\n",
                      tpl_cyd_get_inversion() ? "ON" : "OFF");
      }
      g_last_press_y = ly;
      g_header_touched = true;

    } else {
      g_header_touched = false;
      g_last_press_time = 0;
      g_press_start = 0;
      g_longpress_done = false;
    }

    g_last_touch_time = millis();
    if (g_backlight_target != BACKLIGHT_FULL) {
      g_backlight_target = BACKLIGHT_FULL;
      g_fade_start_time = millis();
    }
  } else {
    // Check for header tap on release
    if (g_header_touched && !g_longpress_done &&
        (millis() - g_last_press_time < 300) &&
        (millis() - g_last_toggle_time > TOGGLE_DEBOUNCE_MS)) {
      g_proto_display_mode = (g_proto_display_mode == PROTO_MODE_RAW)
                                 ? PROTO_MODE_ANALYZER
                                 : PROTO_MODE_RAW;
      Serial.printf("[CAN] Protocol analyzer %s\n",
                    g_proto_display_mode == PROTO_MODE_ANALYZER ? "ON" : "OFF");
      /* Reposition columns for the new mode — deferred to loop(), see
       * g_columns_dirty. */
      g_columns_dirty = true;
      g_last_toggle_time = millis();
    }
    g_header_touched = false;
    g_last_press_time = 0;
    g_press_start = 0;
    g_longpress_done = false;
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

// Set to 1 to re-enable the boot-time panel selftest, the raw panel-ID probe
// and the LVGL flush tracer. Only needed while chasing display problems, so
// it stays compiled out by default.
#ifndef DISPLAY_DEBUG
#define DISPLAY_DEBUG 0
#endif
#ifndef DISPLAY_SELFTEST_HOLD_MS
#define DISPLAY_SELFTEST_HOLD_MS 8000
#endif

#if DISPLAY_DEBUG

static uint32_t g_flush_cnt = 0;

static void flush_trace_cb(lv_event_t* e) {
  lv_display_t* disp = static_cast<lv_display_t*>(lv_event_get_target(e));
  const lv_area_t* area = static_cast<const lv_area_t*>(lv_event_get_param(e));
  g_flush_cnt++;
  if (g_flush_cnt <= 12) {
    const lv_draw_buf_t* b = lv_display_get_buf_active(disp);
    Serial.printf(
        "[FLUSH] #%lu area=(%d,%d)-(%d,%d) area_w=%d area_h=%d | "
        "buf(w=%u h=%u stride=%u size=%u) | lv_res=%dx%d tft=%dx%d\n",
        g_flush_cnt, area->x1, area->y1, area->x2, area->y2,
        lv_area_get_width(area), lv_area_get_height(area), b->header.w,
        b->header.h, b->header.stride, b->data_size,
        lv_display_get_horizontal_resolution(disp),
        lv_display_get_vertical_resolution(disp), tft.width(), tft.height());
  }
}

/* Paints a known pattern straight through TFT_eSPI, bypassing LVGL entirely.
 * Four quadrants (red / green / blue / white) plus 1-pixel stripes along the
 * left and top edge. If those are clean, SPI + panel + driver init are fine
 * and any remaining problem is in the LVGL side; if they are scrambled, the
 * problem is below LVGL. */
/* Surveys all four TFT_eSPI rotations, one after the other, so the correct
 * one can be read straight off the panel instead of guessed:
 *
 *   - background tint differs per rotation (R0 red, R1 green, R2 blue,
 *     R3 magenta), so you always know which phase you are looking at
 *   - big "R0".."R3" text (font 2, size 3) top-left, same place in all phases
 *   - yellow 30x30 marker at the logical top-left corner
 *   - white 30x30 marker at the logical bottom-right corner
 *   - a 100x100 gradient box (red ramps left->right, green ramps top->bottom)
 *
 * The correct rotation is the one where: the text is upright and readable,
 * yellow is top-left, white is bottom-right and the gradient box runs
 * red along the bottom and green along the right.
 */
/* Boot colour probe, drawn straight through TFT_eSPI with no LVGL involved
 * and no large buffers: top-left black, top-right green, bottom-left white,
 * bottom-right red. If these come back inverted the panel itself inverts and
 * nothing in the LVGL buffer will ever look right. */
static void display_selftest(void) {
  tft.begin();
  tft.setRotation(TFT_ROTATION);

  const int32_t pw = tft.width() / 2;
  const int32_t ph = tft.height() / 2;
  Serial.printf("[COLOURPROBE] panel %dx%d, TFT_ROTATION=%d\n", tft.width(),
                tft.height(), TFT_ROTATION);
  Serial.println("[COLOURPROBE] sent: TL black, TR green, BL white, BR red");

  pinMode(CYD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(CYD_BACKLIGHT_PIN, HIGH);

  tft.fillRect(0, 0, pw, ph, TFT_BLACK);
  tft.fillRect(pw, 0, pw, ph, TFT_GREEN);
  tft.fillRect(0, ph, pw, ph, TFT_WHITE);
  tft.fillRect(pw, ph, pw, ph, TFT_RED);
  delay(DISPLAY_SELFTEST_HOLD_MS);
  tft.fillScreen(TFT_BLACK);
  Serial.println("[COLOURPROBE] cleared, continuing with LVGL");
}

#endif  // DISPLAY_DEBUG

#if DISPLAY_DEBUG
static void log_print_cb(lv_log_level_t level, const char* buf) {
  LV_UNUSED(level);
  // No Serial.flush() here — it blocks the loop for the full 115200-baud
  // transfer, which stalls lv_task_handler() and shows up as touch lag.
  Serial.println(buf);
}
#endif

static void lv_init_display(void) {
  Serial.println("[MAIN] === lv_init_display START ===");
  delay(50);
#if DISPLAY_DEBUG
  /* Forwarding every LVGL message to Serial floods the log (warnings from the
   * refresh path, style asserts, ...) and each line stalls lv_task_handler()
   * while the UART drains. Only useful while debugging the display; without a
   * registered callback LVGL emits nothing at all. */
  lv_log_register_print_cb(log_print_cb);
#endif
  Serial.println("[MAIN] Step 1: lv_init()");
  delay(50);
  lv_init();
  Serial.println("[MAIN] lv_init() done");
  delay(50);
  Serial.println("[MAIN] Step 2: tpl_cyd_display_init()");
  delay(50);

  // Use the CYD abstraction — handles display init, touch init, and rotation
  // pipeline internally (ADC → XPT2046 rotation → calibration → logical
  // rotation) Pass draw buffer to tpl_cyd_display_init — uses global tft object
  // with board-specific pins from user_setup_declared.h (NOT
  // lv_tft_espi_create()).
  lv_display_t* disp =
      tpl_cyd_display_init(TFT_ROTATION, draw_buf, sizeof(draw_buf));
  Serial.printf("[MAIN] disp=%p\n", disp);
  delay(50);
  if (disp == NULL) {
    Serial.println("[MAIN] ERROR: disp is NULL, aborting display init");
    return;
  }

  Serial.println("[MAIN] Step 3: lv_display_set_default()");
  delay(50);
  lv_display_set_default(disp);  // Required: lv_indev_create() and ui_init()
                                 // both rely on the default display.
  /* Drop LVGL's default light theme. It paints the screen near-white
   * (LIGHT_COLOR_SCR = palette_grey lightened) and the text near-black
   * (LIGHT_COLOR_TEXT = palette_grey darkened), which is exactly the
   * "white field, black characters" look — and it wins on any object whose
   * styles we don't set explicitly, e.g. the row labels. Everything in this UI
   * sets its own colours. */
  lv_display_set_theme(disp, NULL);
  Serial.println("[MAIN] lv_display_set_theme(NULL) done");
  delay(50);
  Serial.println("[MAIN] lv_display_set_default() done");
  delay(50);
#if DISPLAY_DEBUG
  lv_display_add_event_cb(disp, flush_trace_cb, LV_EVENT_FLUSH_START, NULL);
#endif
  Serial.println("[MAIN] Step 4: tpl_cyd_touch_init()");
  delay(50);
  tpl_cyd_touch_init(touchscreen, TFT_ROTATION);
  Serial.println("[MAIN] tpl_cyd_touch_init() done");
  delay(50);
  Serial.println("[MAIN] Step 5: lv_indev_create()");
  delay(50);

  // Create and register input device (touch)
  lv_indev_t* indev = lv_indev_create();
  Serial.printf("[MAIN] indev=%p\n", indev);
  delay(50);
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  Serial.println("[MAIN] Step 6: lv_indev_set_read_cb()");
  delay(50);
  lv_indev_set_read_cb(indev, touchscreen_read_cb);
  Serial.println("[MAIN] lv_indev_set_read_cb() done");
  delay(50);
  Serial.println("[MAIN] === lv_init_display DONE ===");
}

// ************* CAN Task (FreeRTOS) *************

static void CANTask(void* parameter) {
  uint32_t alerts_triggered;
  LV_UNUSED(parameter);

  while (true) {
    twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(100));
    twai_status_info_t twaistatus;
    twai_get_status_info(&twaistatus);
    // A lone or failed monitor goes BUS_OFF after 32 failed transmissions;
    // TWAI will not leave that state on its own. Kick off recovery so the
    // bench keeps working after the peer reboots (e.g. during OTA tests).
    if (twaistatus.state == TWAI_STATE_BUS_OFF) {
      Serial.println("[CAN] Bus-off; initiating recovery");
      twai_initiate_recovery();
    }

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

  // Why did we (re)boot? Helps distinguish crashes from brown-out / WDT.
  Serial.printf("[BOOT] boot#%lu reset=%s (0x%04X)\n",
                (unsigned long)tpl_config.bootCount,
                tpl_config.reset_reason_cpu0,
                (unsigned)tpl_config.reset_reason);

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
        /* This callback runs in TaskCommandCore1 (core 1), not the LVGL loop
         * task. LVGL is single-threaded here (LV_USE_OS = LV_OS_NONE), so it
         * must not touch widgets from this task — only raise the flag. */
        g_columns_dirty = true;
        break;
      default:
        break;
    }
  });

  // Backlight first: the panel is transmissive, so nothing is visible until
  // GPIO21 is driven — including the boot-time display selftest.
  tpl_cyd_backlight_init(7, BACKLIGHT_FULL);

  // Initialize CYD RGB LED pins (HIGH = off on both boards)
  tpl_cyd_rgb_init(true);

  // Initialize LVGL display and touchscreen
  lv_init_display();

#if DISPLAY_DEBUG
  /* After tpl_cyd_display_init(), so it verifies the state the UI will
   * actually run in (including the MADCTL INV fix), and before ui_init()
   * paints over it. */
  display_selftest();
#endif

  // Initialize EEZ Studio UI (creates all screen objects)
  ui_init();
  ui_apply_legibility();
  Serial.println("[UI] EEZ Studio UI initialized");

  /* Apply the column layout for the mode we actually boot into. The default
   * mode is RAW, so the headers must read "Data" / "Last Seen" — earlier this
   * was hard-coded to analyzer, which left RAW rows under Type/Decoded
   * headers. */
  ui_apply_columns(g_proto_display_mode == PROTO_MODE_ANALYZER);

  /* Install row click handlers so tapping a row opens the detail screen. */
  ui_install_row_clicks();

  /* Re-assert the panel's colour inversion now that everything is up. The
   * driver applies its rotation (which rewrites MADCTL) from within
   * lv_display_set_rotation(), and the panel comes up inverted regardless of
   * the state written at init — sending INVON again here is what actually
   * makes the colours come out right. */
  tpl_cyd_set_inversion(tpl_cyd_get_inversion());
  tpl_cyd_apply_inversion();
  Serial.printf("[MAIN] colour inversion %s\n",
                tpl_cyd_get_inversion() ? "ON" : "OFF");

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

    /* IMPORTANT: the second parameter of twai_reconfigure_alerts() is the
     * alert *configuration* (which alerts change the driver state), not the
     * alert mask — passing NULL here is invalid. */
    uint32_t alerts_to_enable =
        TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR |
        TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_BUS_RECOVERED;
    uint32_t alerts_config = 0;
    if (twai_reconfigure_alerts(alerts_to_enable, &alerts_config) == ESP_OK) {
      Serial.println("[CAN] Alerts configured");
    }
  }

  xTaskCreate(CANTask, "CANTask", 4096, NULL, 3, NULL);

  // Create time broadcast task
  xTaskCreate(TimeBroadcastTask, "TimeBroadcastTask", 2048, NULL, 4, NULL);

  Serial.println("[26_CYD_CAN_monitor] Setup complete");
}

// ************* On-demand CAN TX (bench) *************
//
// Serial commands (newline-terminated):
//   can <id hex> <hex data>    send an arbitrary standard-ID frame
//   help                       print this list
//
// Only for bench bring-up; nothing is sent automatically.

static uint8_t hex_nibble(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
  return 0xFF;
}

static int parse_hex(const char* s, uint8_t* out, int max_len) {
  int n = 0;
  while (s[0] && s[1] && n < max_len) {
    const uint8_t hi = hex_nibble(s[0]);
    const uint8_t lo = hex_nibble(s[1]);
    if (hi == 0xFF || lo == 0xFF) break;
    out[n++] = (uint8_t)((hi << 4) | lo);
    s += 2;
  }
  return n;
}

static bool send_bench_frame(uint32_t id, const uint8_t* data, uint8_t len) {
  twai_message_t msg;
  memset(&msg, 0, sizeof(msg));
  msg.identifier = id;
  msg.data_length_code = len;
  msg.flags = TWAI_MSG_FLAG_NONE;
  memcpy(msg.data, data, len);
  return twai_transmit(&msg, pdMS_TO_TICKS(100)) == ESP_OK;
}

static void handle_bench_command(char* line) {
  char* cmd = strtok(line, " ");
  if (!cmd) return;

  if (strcmp(cmd, "help") == 0) {
    Serial.println("can <id hex> <hex data>");
    Serial.println("flood <count>  (0x200 frames)");
    Serial.println("stats");
    return;
  }
  if (strcmp(cmd, "stats") == 0) {
    twai_status_info_t st;
    if (twai_get_status_info(&st) == ESP_OK) {
      Serial.printf(
          "[CAN] state=%u tec=%lu rec=%lu rx=%lu tx=%lu err=%lu "
          "bus_errors=%lu rx_missed=%lu\n",
          (unsigned)st.state, (unsigned long)st.tx_error_counter,
          (unsigned long)st.rx_error_counter, (unsigned long)st.msgs_to_rx,
          (unsigned long)st.msgs_to_tx, (unsigned long)st.bus_error_count,
          (unsigned long)st.bus_error_count, (unsigned long)st.rx_missed_count);
    }
    return;
  }
  if (strcmp(cmd, "flood") == 0) {
    char* n_s = strtok(NULL, " ");
    const long count = n_s ? strtol(n_s, NULL, 10) : 1000;
    uint8_t data[8] = {0};
    twai_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.identifier = 0x200;
    msg.data_length_code = 8;
    msg.flags = TWAI_MSG_FLAG_NONE;
    const uint32_t t0 = millis();
    uint32_t sent = 0, dropped = 0;
    for (long i = 0; i < count; i++) {
      memcpy(msg.data, data, sizeof(data));
      if (twai_transmit(&msg, pdMS_TO_TICKS(10)) == ESP_OK)
        sent++;
      else
        dropped++;
    }
    const uint32_t dt = millis() - t0;
    Serial.printf("[TX] flood %lu sent, %lu dropped in %lu ms (%lu/s)\n",
                  (unsigned long)sent, (unsigned long)dropped,
                  (unsigned long)dt,
                  dt ? (unsigned long)(sent * 1000UL / dt) : 0);
    return;
  }
  if (strcmp(cmd, "can") == 0) {
    char* id_s = strtok(NULL, " ");
    char* data_s = strtok(NULL, " ");
    uint8_t data[8];
    int n = 0;
    if (id_s) n = parse_hex(data_s ? data_s : "", data, 8);
    const uint32_t id = id_s ? (uint32_t)strtoul(id_s, NULL, 16) : 0;
    if (!id_s || n <= 0) {
      Serial.println("[TX] usage: can <id hex> <hex data>");
      return;
    }
    const bool ok = send_bench_frame(id, data, (uint8_t)n);
    Serial.printf("[TX] 0x%03X %s\n", (unsigned)id, ok ? "sent" : "FAILED");
    return;
  }
  Serial.println("[TX] unknown command; type help");
}

static void poll_bench_serial(void) {
  static char line[64];
  static size_t len = 0;
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[len] = '\0';
      if (len > 0) handle_bench_command(line);
      len = 0;
      continue;
    }
    if (len < sizeof(line) - 1) line[len++] = c;
  }
}

// ************* Loop *************

void loop() {
  poll_bench_serial();
  static unsigned long last_display_update = 0;
  static unsigned long last_ui_tick = 0;
#if DISPLAY_DEBUG
  static unsigned long last_flush_report = 0;
#endif
  unsigned long now = millis();

  // Apply a pending colour-inversion change here: outside lv_task_handler(),
  // so the LVGL driver's TFT_eSPI instance is not mid-flush transaction.
  tpl_cyd_apply_inversion();

  // LVGL tasks
  lv_tick_inc(now - last_ui_tick);
  last_ui_tick = now;
  lv_task_handler();

  // EEZ Studio UI tick
  ui_tick();

  // Apply a pending mode-toggle column change (raised from the input callback).
  if (g_columns_dirty) {
    g_columns_dirty = false;
    ui_apply_columns(g_proto_display_mode == PROTO_MODE_ANALYZER);
  }

  // Update CAN display every 500ms
  if (now - last_display_update > 500) {
    last_display_update = now;
    update_can_display();
  }

  // Backlight fade
  backlight_update();

#if DISPLAY_DEBUG
  if (now - last_flush_report > 5000) {
    last_flush_report = now;
    Serial.printf("[FLUSH] total=%lu\n", g_flush_cnt);
  }
#endif

  vTaskDelay(5 / portTICK_PERIOD_MS);
}
