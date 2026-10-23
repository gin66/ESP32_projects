/**
 * @file can_monitor.h
 * @brief Project-local CAN scope: RX ring, per-ID table, decoder, SML state.
 *
 * Scope decision (2026-10-19): this stays inside 28_hauszentrale. The module
 * is too entangled with this project's UI and memory budget to be a template
 * asset, and project 26 keeps its own implementation.
 *
 * Threading contract
 * ------------------
 * Producer: the TWAI RX drain in can_task (single writer).
 * Consumer: canMon task (single reader).
 * The ring is a lock-free SPSC queue with two atomic byte indices. The
 * producer only moves the write index; the reader only moves the read index.
 * A full ring drops the frame and increments counters.dropped; it never
 * blocks the CAN task.
 */

#ifndef CAN_MONITOR_H
#define CAN_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "haus_can.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Rows the scope keeps (13 rows fit on the 480x800 panel). */
#define CAN_MONITOR_MAX_IDS 24

/** SPSC ring depth. 127 usable frames, enough for a 32-frame burst plus slack.
 */
#define CAN_MONITOR_RING_SLOTS 128

/** SML reassembly buffer, one per base id (0x600 and 0x700). */
#define CAN_MONITOR_SML_BYTES 258
#define CAN_MONITOR_SML_SLOTS 2

/** One unique CAN identifier as shown in the scope. */
typedef struct {
  bool used;
  uint32_t identifier;
  bool is_extended;
  uint8_t data_len;
  uint8_t data[8];
  uint8_t last_data[8];
  unsigned long last_timestamp;
  bool data_changed;
  uint32_t frame_count;
  uint32_t changed_count;
  unsigned long interval_ms;
  unsigned long interval_min_ms;
  unsigned long interval_max_ms;
} can_id_entry_t;

/** Reassembly state for one SML base id. Updated on the CAN task, read by UI.
 */
typedef struct {
  uint16_t base_id;
  uint16_t expected_length; /* message length + the two length bytes */
  uint16_t received_length;
  uint16_t frames_received;
  bool active;
  bool complete;
  uint8_t buffer[CAN_MONITOR_SML_BYTES];
} sml_reassembly_t;

/** One complete 20 ms ADC waveform (0x2C1..0x2C5).
 *
 * Slots are in units of 1/64 ADC count (the node shifts the 10-bit value left
 * by 6). generation increments each time a complete five-frame set is
 * published; the renderer redraws only when it changes.
 */
#define CAN_MONITOR_WAVE_SLOTS 20
typedef struct {
  bool valid;
  uint32_t generation;
  uint32_t seen_ms; /* millis() when the five-frame set completed */
  uint16_t slot[CAN_MONITOR_WAVE_SLOTS];
} can_monitor_adc_wave_t;

/** Decoded view of one identifier. Pure: no counters, no side effects. */
typedef struct {
  haus_protocol_t protocol;
  char label[16];
  char decoded[96];
} can_monitor_decoded_t;

/** Counters owned by the RX/decoder path (never by the renderer). */
typedef struct {
  uint32_t rx_frames;
  uint32_t dropped;
  uint32_t sml_ok;
  uint32_t sml_incomplete;
  uint32_t sml_empty;
  uint32_t sml_hdr_err; /* complete frame set, wrong SML escape header */
  uint32_t sml_crc_err; /* complete frame set, header OK, CRC mismatch */
  uint32_t proto_id;
  uint32_t proto_temp;
  uint32_t proto_pwm;
  uint32_t proto_adc;
  uint32_t proto_wave;
  uint32_t wave_dropped;
  uint32_t proto_ee;
  uint32_t proto_stats;
  uint32_t proto_ts;
  uint32_t proto_sml;
  uint32_t proto_unknown;
  uint32_t proto_time_bcast;
  uint8_t pwmfail;
  uint8_t ee_anom;
} can_monitor_counters_t;

/** Board-neutral frame crossing the producer/consumer ring. */
typedef struct {
  uint32_t id;
  uint8_t len;
  uint8_t ext; /* non-zero for an extended identifier */
  uint8_t data[8];
} can_monitor_frame_t;

/** Create the consumer task. Idempotent; call before the CAN task starts. */
void can_monitor_start(void);

/** Producer side: enqueue one received frame. Never blocks. */
void can_monitor_push(const can_monitor_frame_t* frame);

/** Drop all rows, SML state and counters (Clear). */
void can_monitor_reset(void);

/** Copy one row for the renderer. Index < can_monitor_count(). */
bool can_monitor_get_entry(int index, can_id_entry_t* out);
int can_monitor_count(void);

/** Copy SML state for slot 0 (0x600) / 1 (0x700). */
bool can_monitor_get_sml(int slot, sml_reassembly_t* out);

/** Copy the latest complete ADC waveform. Returns false until five in-order
 *  frames (0x2C1..0x2C5) have been assembled. */
bool can_monitor_get_adc_wave(can_monitor_adc_wave_t* out);

/** Latest 0x2C0 min/max/pp. pp_count is raw ADC counts, not amps. */
typedef struct {
  bool valid;
  uint32_t seen_ms;
  uint16_t pp_count;
  uint16_t max_count;
  uint16_t min_count;
} can_monitor_adc_span_t;

bool can_monitor_get_adc_span(can_monitor_adc_span_t* out);

/** Snapshot of the counters (read-only, updated by the consumer). */
const can_monitor_counters_t* can_monitor_counters(void);

/**
 * Decode one identifier into a label + human text. Pure: the only state it
 * reads is the SML reassembly the RX path already produced.
 */
void can_monitor_decode(uint32_t id, const uint8_t* data, uint8_t len,
                        can_monitor_decoded_t* out);

#ifdef __cplusplus
}
#endif

#endif /* CAN_MONITOR_H */
