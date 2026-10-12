/**
 * @file protocol_detail.h
 * @brief Protocol detail screen overlay + row click handling for CAN bus monitor.
 *
 * Creates a reusable full-screen overlay (via lv_layer_top) showing the
 * complete decoded message for any CAN-ID.  Column repositioning for
 * analyzer mode is also provided here.
 *
 * Works on both 2.8" (320×240) and 3.5" (320×480) CYD boards.
 */

#ifndef PROTOCOL_DETAIL_H
#define PROTOCOL_DETAIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>
#include <time.h>

/* SML reassembly constants. */
#define SML_MAX_BYTES 258
#define SML_MAX_MESSAGES 2   // 0x600 and 0x700

/* Forward declarations — defined in main.cpp. */
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
  uint32_t frame_count;
  uint32_t changed_count;
  unsigned long interval_min_ms;
  unsigned long interval_max_ms;
} can_id_entry_t;

/* SML reassembly struct — defined in main.cpp. */
typedef struct {
    uint16_t base_id;
    uint16_t expected_length;
    uint16_t received_length;
    uint8_t  buffer[258];
    bool     active;
    bool     complete;
    time_t   last_frame_time;
    uint16_t frames_received;
} sml_reassembly_t;

/* Extern globals from main.cpp. */
extern can_id_entry_t g_can_id_table[];
extern int g_max_can_ids;
extern sml_reassembly_t g_sml[];
extern int g_sml_count;

/* API: open / close / query. */
void open_protocol_detail(uint32_t table_index);
void close_protocol_detail(void);
bool is_protocol_detail_visible(void);
uint32_t get_detail_table_index(void);

/* Column repositioning for analyzer vs. raw mode. */
void ui_apply_columns(bool analyzer);

/* Install click handlers on all row labels. */
void ui_install_row_clicks(void);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_DETAIL_H */