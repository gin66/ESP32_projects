/**
 * @file sml_power.h
 * @brief SML smart-meter decoder lifted from 15_power_log_eink (S19.01, R26).
 *
 * Pure decoder: it takes one complete SML file body and publishes the three
 * OBIS values this house uses. Framing, reassembly and CRC belong to the
 * caller (the CAN monitor / CAN task), so libsml never runs inside the RX
 * drain loop (S19.06).
 *
 * The body is exactly what project 15 passes to sml_file_parse(): after the
 * eight-byte SML escape header, before the two-byte CRC.
 */

#ifndef SML_POWER_H
#define SML_POWER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "haus_can.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Three-deep reassembly pool: 3 x 258 B = 774 B (S19.04). */
#define SML_POWER_POOL 3
#define SML_POWER_BYTES 258

typedef struct {
  double consumption_wh; /* 1-0:1.8.0/255 */
  double production_wh;  /* 1-0:2.8.0/255 */
  double current_w;      /* 1-0:16.7.0/255, signed by the status bit */

  /* Phase quantities (L1/L2/L3). */
  double voltage_v[3];   /* 1-0:32/52/72.7.0/255 */
  double current_a[3];   /* 1-0:31/51/71.7.0/255 */
  double phase_deg[3];   /* 1-0:81.7.1/2/3/255 */
  double frequency_hz;   /* 1-0:14.7.0/255 */

  /* Historical consumption registers, Wh: day/week/month/year. */
  double history_wh[4];  /* 1-0:1.8.0/96 .. /99 */
  bool has_voltage;
  bool has_current;
  bool has_phase;
  bool has_frequency;
  uint8_t history_mask;  /* bit 0 day, 1 week, 2 month, 3 year */
  uint32_t messages;     /* file bodies handed to libsml */
  uint32_t parse_errors; /* sml_file_parse() returned NULL */
  uint32_t errors;       /* header/CRC/length rejected (sml_error_cnt) */
  uint32_t header_errors; /* subset of errors: escape header missing */
  uint32_t crc_errors;    /* subset of errors: header OK, CRC mismatch */
  uint32_t lost_bytes;   /* partial message abandoned (sml_lost_bytes_cnt) */
  uint32_t empty_messages;
  bool valid; /* at least one of the three OBIS values matched */
  bool has_consumption; /* 1-0:1.8.0/255 seen */
  bool has_production;  /* 1-0:2.8.0/255 seen */
} sml_power_reading_t;

void sml_power_reset(void);

/**
 * Feed one received CAN frame. Reassembles SML messages in a three-deep pool
 * (one slot per base id, plus a spare) and parses on completion. Call this
 * from the CAN decode task, never from the TWAI RX drain loop (S19.06).
 */
void sml_power_on_frame(uint32_t id, const uint8_t* data, uint8_t len);

/**
 * Parse one complete SML file body and update the reading.
 * Returns true when at least one OBIS value was published.
 */
bool sml_power_parse(const uint8_t* body, size_t len);

/** Latest decoded values. Never NULL; use .valid to gate the display. */
const sml_power_reading_t* sml_power_reading(void);

#ifdef __cplusplus
}
#endif

#endif /* SML_POWER_H */
