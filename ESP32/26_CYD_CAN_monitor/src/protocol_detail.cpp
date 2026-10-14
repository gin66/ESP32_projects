/**
 * @file protocol_detail.cpp
 * @brief Protocol detail screen overlay + row click handling for CAN bus monitor.
 *
 * Creates a reusable full-screen overlay (via lv_layer_top) showing the
 * complete decoded message for any CAN-ID.  Column repositioning for
 * analyzer mode is also provided here.
 */

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>

#include "protocol_detail.h"
#include "tpl_display.h"
#include "ui/screens.h"
#include "haus_can.h"

/* ── Forward declarations ─────────────────────────────────────────────── */
static void populate_detail_screen(uint32_t table_index);

/* ── Detail overlay state ─────────────────────────────────────────────── */

static lv_obj_t* g_detail_screen = NULL;
static lv_obj_t* g_detail_lbl = NULL;   // single multi-line label
static uint32_t g_detail_table_index = (uint32_t)-1;
static bool g_detail_visible = false;

/* ── Back button callback ─────────────────────────────────────────────── */

static void detail_back_cb(lv_event_t* e) {
  LV_UNUSED(e);
  close_protocol_detail();
}

/* ── API: open / close / query ────────────────────────────────────────── */

void open_protocol_detail(uint32_t table_index) {
  if (table_index >= (uint32_t)g_max_can_ids) return;
  if (!g_can_id_table[table_index].used) return;

  g_detail_table_index = table_index;

  if (g_detail_screen == NULL) {
    /* Create the overlay lazily — once, on first open. */
    g_detail_screen = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_detail_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(g_detail_screen, 0, 0);
    lv_obj_set_style_bg_color(g_detail_screen, lv_color_hex(0x000000),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_detail_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_detail_screen, 6, LV_PART_MAIN);
    /* A scrollable container treats a resistive-touch tap with a little finger
     * wobble as a scroll and swallows the click. The overlay has no scrolling
     * content, so disable it — otherwise the Back button never fires. */
    lv_obj_remove_flag(g_detail_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Back button — a real, visibly tappable button (filled background +
     * border + accent text), not a bare label. */
    lv_obj_t* back = lv_obj_create(g_detail_screen);
    lv_obj_remove_flag(back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(back, 104, 34);
    lv_obj_set_pos(back, 6, 4);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1E3A5F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(back, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(back, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(back, lv_color_hex(0x66E0FF), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x2E5687), LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, detail_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x9FD8FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(back_lbl);

    /* Detail text label (multi-line, wraps). Larger font; the layout is kept
     * compact so it still fits the 2.8" (320×240 logical) screen. */
    g_detail_lbl = lv_label_create(g_detail_screen);
    lv_obj_set_pos(g_detail_lbl, 6, 44);
    lv_obj_set_size(g_detail_lbl, LV_HOR_RES - 12, LV_VER_RES - 50);
    lv_label_set_long_mode(g_detail_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(g_detail_lbl, lv_color_hex(0xE0E0E0),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(g_detail_lbl, &lv_font_montserrat_18,
                               LV_PART_MAIN);
    lv_obj_set_style_text_line_space(g_detail_lbl, 0, LV_PART_MAIN);

    /* Initially hidden. */
    lv_obj_add_flag(g_detail_screen, LV_OBJ_FLAG_HIDDEN);
  }

  /* Populate the detail screen with data for this table index. */
  populate_detail_screen(table_index);

  /* Show the overlay. */
  lv_obj_move_foreground(g_detail_screen);
  lv_obj_remove_flag(g_detail_screen, LV_OBJ_FLAG_HIDDEN);
  g_detail_visible = true;
}

void close_protocol_detail(void) {
  if (g_detail_screen != NULL) {
    lv_obj_add_flag(g_detail_screen, LV_OBJ_FLAG_HIDDEN);
    g_detail_visible = false;
  }
}

bool is_protocol_detail_visible(void) {
  return g_detail_visible;
}

uint32_t get_detail_table_index(void) {
  return g_detail_table_index;
}

/* ── Column repositioning helper ──────────────────────────────────────── */

/**
 * Reposition header + data/time labels for analyzer vs. raw mode.
 * Call after ui_init() and on every mode toggle.
 *
 * Analyzer mode:  ID=48px  Type=64px  Decoded=190px
 * Raw mode:       ID=40px  Data=155px LastSeen=112px
 */
void ui_apply_columns(bool analyzer) {
  int type_w = analyzer ? 64 : 155;
  int decoded_x = analyzer ? 121 : 204;
  int decoded_w = analyzer ? 190 : 112;

  /* Header labels. */
  lv_obj_set_size(objects.lblHeaderData, type_w, 16);
  lv_obj_set_pos(objects.lblHeaderTime, decoded_x, 40);
  lv_obj_set_size(objects.lblHeaderTime, decoded_w, 16);
  lv_label_set_text(objects.lblHeaderData, analyzer ? "Type" : "Data");
  lv_label_set_text(objects.lblHeaderTime, analyzer ? "Decoded" : "Last Seen");

  /* Row labels. */
  for (int i = 0; i < g_max_can_ids; i++) {
    lv_obj_set_size(objects.lblData[i], type_w, 16);
    lv_obj_set_pos(objects.lblTime[i], decoded_x, 58 + i * ROW_HEIGHT + 1);
    lv_obj_set_size(objects.lblTime[i], decoded_w, 16);
  }
}

/* ── Row click handlers ───────────────────────────────────────────────── */

/**
 * Install click handlers on all row labels so tapping a row opens
 * the detail screen for that CAN-ID.
 *
 * Call once after ui_init().
 */
void ui_install_row_clicks(void) {
  /* Row click callback — uses the label's user_data for the index. */
  auto row_click_cb = [](lv_event_t* e) {
    lv_obj_t* lbl = (lv_obj_t*)lv_event_get_target(e);
    uint32_t idx = (uint32_t)(uintptr_t)lv_obj_get_user_data(lbl);
    if (idx < (uint32_t)g_max_can_ids && g_can_id_table[idx].used) {
      open_protocol_detail(idx);
    }
  };

  for (int i = 0; i < g_max_can_ids; i++) {
    lv_obj_t* row[] = {objects.lblId[i], objects.lblData[i], objects.lblTime[i]};
    for (int c = 0; c < 3; c++) {
      lv_obj_add_flag(row[c], LV_OBJ_FLAG_CLICKABLE);
      /* Tap affordance: a subtle accent flash while the row is held, so a
       * tappable row reads differently from a static one. */
      lv_obj_set_style_bg_color(row[c], lv_color_hex(0x66E0FF),
                                LV_STATE_PRESSED);
      lv_obj_set_style_bg_opa(row[c], LV_OPA_40, LV_STATE_PRESSED);
      lv_obj_set_user_data(row[c], (void*)(uintptr_t)i);
      lv_obj_add_event_cb(row[c], row_click_cb, LV_EVENT_CLICKED, NULL);
    }
  }
}

/* ── populate_detail_screen implementation ────────────────────────────── */

/* Helper: format time from time_t. */
static void format_time_from_time_t(char* buf, size_t buf_size, time_t t) {
  if (t > 1600000000) {
    struct tm ti;
    localtime_r(&t, &ti);
    snprintf(buf, buf_size, "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
  } else {
    snprintf(buf, buf_size, "--");
  }
}

/* Helper: format a CAN ID string. */
static void format_can_id_str(char* buf, size_t buf_size, uint32_t id,
                              bool is_extended) {
  if (is_extended) {
    snprintf(buf, buf_size, "%08X", id);
  } else {
    snprintf(buf, buf_size, "%03X", id & 0x7FF);
  }
}

/* Helper: get protocol label and decoded text for a CAN-ID. */
static void get_protocol_info(uint32_t id, const uint8_t* data, uint8_t len,
                              char* label, size_t label_size,
                              char* decoded, size_t decoded_size) {
  memset(label, 0, label_size);
  memset(decoded, 0, decoded_size);

  /* Cast the CAN-ID directly to haus_protocol_t. */
  haus_protocol_t proto = (haus_protocol_t)(id & 0x7FF);
  uint8_t rank = haus_can_get_rank((uint16_t)(id & 0x7FF));

  if (rank != (uint8_t)-1) {
    /* Sensor identity frames (0x180 + rank). */
    snprintf(label, label_size, "ID 0x180+%u", rank);
    if (len >= 8) {
      haus_can_identity_t id_data;
      haus_can_get_identity(data, &id_data);
      snprintf(decoded, decoded_size,
               "%02X:%02X:%02X:%02X:%02X:%02X",
               id_data.rom[1], id_data.rom[2], id_data.rom[3],
               id_data.rom[4], id_data.rom[5], id_data.rom[6]);
    }
    return;
  }

  switch (proto) {
    case HAUS_PROTO_HEIZUNG_TEMP: {
      snprintf(label, label_size, "TEMP");
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
        snprintf(decoded, decoded_size, "%s", buf);
      }
      return;
    }

    case HAUS_PROTO_HEIZUNG_PWM_RPT:
    case HAUS_PROTO_HEIZUNG_PWM: {
      snprintf(label, label_size, "PWM");
      if (len >= 5) {
        haus_can_pwm_t p;
        haus_can_get_pwm(data, &p);
        snprintf(decoded, decoded_size,
                 "duty=%d/1000 f=%dHz %s",
                 p.duty_permille, p.frequency_hz,
                 (p.flags & PWM_FLAG_ACTIVE) ? "ACTIVE" : "OFF");
      }
      return;
    }

    case HAUS_PROTO_HEIZUNG_ADC: {
      snprintf(label, label_size, "ADC");
      if (len >= 7) {
        haus_can_adc_t a;
        haus_can_get_adc(data, &a);
        snprintf(decoded, decoded_size,
                 "max=%u min=%u pp=%u flags=0x%02X",
                 a.max_count, a.min_count, a.pp_count, a.flags);
      }
      return;
    }

    case HAUS_PROTO_HEIZUNG_EE: {
      snprintf(label, label_size, "EEPROM");
      if (len >= 8) {
        haus_can_ee_t e;
        haus_can_get_ee(data, &e);
        const char *src;
        switch (e.flags & EE_SRC_MASK) {
          case EE_SRC_VIRGIN  << EE_SRC_SHIFT: src = "virgin";    break;
          case EE_SRC_MARKER  << EE_SRC_SHIFT: src = "marker";    break;
          case EE_SRC_WINDOW  << EE_SRC_SHIFT: src = "window";    break;
          case EE_SRC_ANOMALY << EE_SRC_SHIFT: src = "ANOMALY";   break;
          case EE_SRC_INVALID << EE_SRC_SHIFT: src = "bad-newest";break;
          default:                                 src = "?";       break;
        }
        snprintf(decoded, decoded_size,
                 "restored=%u rec=%u arc=%u..%u w=%u %s%s%s",
                 e.restored_duty, e.records, e.oldest, e.newest, e.writes, src,
                 (e.flags & EE_FLAG_WRAPPED) ? " wrapped" : "",
                 (e.flags & EE_FLAG_ANOMALY) ? " RESTORE-LOST" : "");
      }
      return;
    }

    case HAUS_PROTO_HEIZUNG_STATS: {
      snprintf(label, label_size, "STATS");
      if (len >= 8) {
        haus_can_stats_t s;
        haus_can_get_stats(data, &s);
        snprintf(decoded, decoded_size,
                 "sent=%u recv=%u ref=%u eflg=0x%02X canstat=0x%02X",
                 s.sent, s.received, s.refused, s.eflg, s.canstat);
      }
      return;
    }

    case HAUS_PROTO_TIMESTAMP: {
      snprintf(label, label_size, "TS");
      if (len >= 4) {
        haus_can_timestamp_t ts;
        haus_can_get_timestamp(data, &ts);
        snprintf(decoded, decoded_size,
                 "%02d:%02d:%02d (day %d)", ts.hours, ts.minutes, ts.seconds,
                 ts.weekday);
      }
      return;
    }

    case HAUS_PROTO_SML:
    case HAUS_PROTO_SML_ALT: {
      snprintf(label, label_size, "SML");
      /* Show the current SML reassembly status. */
      uint16_t base = id & ~0xFF;
      bool found = false;
      for (int m = 0; m < g_sml_count; m++) {
        if (g_sml[m].base_id == base && g_sml[m].active) {
          snprintf(decoded, decoded_size,
                   "SML %u/%u B (%u frames)",
                   g_sml[m].received_length, g_sml[m].expected_length,
                   g_sml[m].frames_received);
          found = true;
          break;
        }
      }
      if (!found) {
        /* Show raw bytes as hex dump. */
        size_t offset = 0;
        for (uint8_t i = 0; i < len && offset + 4 <= decoded_size; i++) {
          offset += snprintf(decoded + offset, decoded_size - offset,
                             "%02X ", data[i]);
        }
      }
      return;
    }

    default:
      /* Unknown protocol — show raw data. */
      snprintf(label, label_size, "RAW");
      size_t offset = 0;
      for (uint8_t i = 0; i < len && offset + 4 <= decoded_size; i++) {
        offset += snprintf(decoded + offset, decoded_size - offset,
                           "%02X ", data[i]);
      }
      return;
  }
}

/* ── populate_detail_screen (the main display function) ───────────────── */

void populate_detail_screen(uint32_t table_index) {
  if (table_index >= (uint32_t)g_max_can_ids) return;

  can_id_entry_t* entry = &g_can_id_table[table_index];
  if (!entry->used) return;

  /* Build the detail text. */
  char text[512];
  text[0] = '\0';
  size_t offset = 0;

  /* Header info. */
  char id_buf[16];
  format_can_id_str(id_buf, sizeof(id_buf), entry->identifier, entry->is_extended);

  char time_buf[32];
  format_time_from_time_t(time_buf, sizeof(time_buf), entry->last_time);

  /* The font here (montserrat_18) has no glyph for U+2500 box-drawing
   * characters, which LVGL renders as "boxes" and logs as
   * "glyph dsc. not found". Stay on pure ASCII and keep the layout compact
   * so it still fits the 2.8" screen. */
  offset += snprintf(text + offset, sizeof(text) - offset,
                     "CAN ID  0x%s\n"
                     "Time    %s\n"
                     "Interv  %lu ms\n"
                     "Frames  %lu (changed %lu)\n"
                     "Range   %lu / %lu ms\n",
                     id_buf, time_buf,
                     entry->interval_ms,
                     entry->frame_count, entry->changed_count,
                     entry->interval_min_ms, entry->interval_max_ms);

  /* Protocol-specific decoded data. */
  char label[32];
  char decoded[128];
  get_protocol_info(entry->identifier, entry->data, entry->data_len,
                    label, sizeof(label), decoded, sizeof(decoded));

  offset += snprintf(text + offset, sizeof(text) - offset,
                     "Type    %s\n"
                     "Data    %s\n",
                     label, decoded);

  /* Raw bytes. */
  offset += snprintf(text + offset, sizeof(text) - offset, "Raw     ");
  for (uint8_t i = 0; i < entry->data_len && offset + 4 <= sizeof(text); i++) {
    offset += snprintf(text + offset, sizeof(text) - offset,
                       "%02X ", entry->data[i]);
  }
  if (offset >= sizeof(text) - 4) {
    strncat(text, "...", sizeof(text) - offset - 1);
  }

  /* Set the label text. */
  if (g_detail_lbl != NULL) {
    lv_label_set_text(g_detail_lbl, text);
  }
}