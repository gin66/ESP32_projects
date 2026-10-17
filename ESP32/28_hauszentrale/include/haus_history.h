/**
 * @file haus_history.h
 * @brief L1/L2 history store and the independent SPIFFS hourly ring.
 *
 * L1 is today's minute samples; L2 is the 31-day hourly rollup used by the
 * week/month charts. The SPIFFS ring is the second bookkeeping store: it holds
 * a year of hourly columns and is written independently of the SD card.
 */

#ifndef HAUS_HISTORY_H
#define HAUS_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "haus_gui.h"
#include "haus_rollup.h"

#define HAUS_L1_SAMPLES 1440
#define HAUS_L1_BYTES (HAUS_L1_SAMPLES * sizeof(gui_sample_t))
#define HAUS_L2_COLUMNS HAUS_ROLLUP_COLUMNS
#define HAUS_L2_BYTES (HAUS_L2_COLUMNS * sizeof(RollupColumn))

/* Twelve months of hours. The ring is indexed by epoch hour, so no write
 * pointer has to be persisted or updated. */
#define HAUS_RING_HOURS (24 * 365)
#define HAUS_RING_BYTES (HAUS_RING_HOURS * sizeof(RollupColumn))
#define HAUS_RING_PATH "/rollup_ring.bin"

#ifdef __cplusplus
extern "C" {
#endif

/* Allocate L1/L2 once (PSRAM on the device) and open/initialise the ring.
 * Safe to call more than once; never reallocates. */
bool haus_history_init(void);

/* Fixed buffers. NULL until haus_history_init() succeeds. */
gui_sample_t *haus_history_l1(void);
RollupColumn *haus_history_l2(void);

/* Append a completed hour column to the SPIFFS ring. */
bool haus_history_ring_append(const RollupColumn *col);

/* Load up to max_columns newest columns into out, oldest first. Returns the
 * number written. Uses the wall clock; returns 0 before the clock is valid. */
size_t haus_history_ring_load(RollupColumn *out, size_t max_columns);

/* Same, with an explicit "now" so the rollover/ring order is testable. */
size_t haus_history_ring_load_at(RollupColumn *out, size_t max_columns,
                                 uint32_t now_epoch);

/* Record one minute from the merged state. Idempotent within a minute. */
bool haus_history_sample(const HausState *state, uint32_t epoch_seconds);

/* Number of valid minute entries currently in L1 (0…1440). */
size_t haus_history_l1_count(void);

/* Append the current hour's column to the ring immediately (used when a day
 * or hour boundary arrives without a minute-59 sample). */
void haus_history_flush_hour(uint32_t epoch_seconds);

/* Start the device recorder task. No-op on the host build. */
void haus_history_start_recorder(void);

/* Number of successful daily file writes (day file + rollup) since boot. Used
 * by the simulated-day test; should be exactly 2 per rollover. */
uint32_t haus_history_sd_write_count(void);

/* Path of the last day file written, e.g. "/data_20241014.bin". */
const char *haus_history_last_day_path(void);

#ifdef HAUS_HOST
/* Test-only: sizes of the last simulated day/rollup writes, and fault
 * injection for the write path. */
uint32_t haus_history_test_day_bytes(void);
uint32_t haus_history_test_rollup_bytes(void);
void haus_history_test_fail_writes(bool fail);
#endif

#ifdef __cplusplus
}
#endif

#endif /* HAUS_HISTORY_H */
