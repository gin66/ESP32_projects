/**
 * @file dump_data.h
 * @brief Read the device's SD/SPIFFS dump into the host harness.
 *
 * The dump directory is produced by the device's HTTP endpoints:
 *
 *   dump_<date>/sd/data_YYYYMMDD.bin   minute samples, CRC-blocked
 *   dump_<date>/sd/rollup.bin          744 hourly L2 columns, CRC-blocked
 *   dump_<date>/sd/log_*.txt           saved debug log
 *   dump_<date>/spiffs/rollup_ring.bin 12-month hourly ring
 *   dump_<date>/spiffs/sensor_roles.bin ROM -> function table
 *
 * All parsers accept the on-device layouts unchanged. Older day files used
 * 24-byte samples (no 0x2C waveform fields); both sizes are handled.
 */

#ifndef HOST_HARNESS_DUMP_DATA_H
#define HOST_HARNESS_DUMP_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "haus_gui.h"
#include "haus_rollup.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load a dump directory. Pass NULL to search HAUS_DUMP, ../dump_* and
 * ./dump_*, newest name first. Returns false when no usable data was found. */
bool dump_load(const char* dir);

bool dump_loaded(void);
const char* dump_directory(void);

/* Newest epoch represented by minute samples or hourly columns. */
uint32_t dump_newest_epoch(void);
uint32_t dump_oldest_epoch(void);

/* Latest complete sample, or false when the dump has no minute data. */
bool dump_latest_sample(gui_sample_t* out);

/* Latest hourly column (ring first, then SD rollup). */
bool dump_latest_rollup(RollupColumn* out);

/* Streaming readers with the same contract as gui_read_*_chunk(). Minute
 * samples missing from a day file fall back to the containing hourly column,
 * so a dump taken before the current day file was written still renders. */
size_t dump_read_samples_chunk(uint32_t start_epoch, size_t first, size_t count,
                               gui_sample_t* out);
size_t dump_read_rollup_chunk(uint32_t start_epoch, size_t first, size_t count,
                              RollupColumn* out);
size_t dump_read_target_samples_chunk(uint32_t start_epoch, size_t first,
                                      size_t count, int16_t* out);
size_t dump_read_target_hours_chunk(uint32_t start_epoch, size_t first,
                                    size_t count, int16_t* out);

/* Saved debug log, NUL-terminated. Returns NULL when the dump has none. */
const char* dump_log(int* out_len);

/* sensor_roles.bin. Returns 0 when the file is absent or invalid. */
uint8_t dump_roles_count(void);
bool dump_roles_get(uint8_t index, uint8_t rom[8], uint8_t* function);

/* Sizes of the actual dump files, for the Storage panel. */
uint64_t dump_sd_bytes(void);
uint64_t dump_spiffs_bytes(void);

/* Newest SML energy registers from sd/energy.csv, in kWh. Returns false when
 * the file is absent (older dumps). */
bool dump_energy_latest(double* consumed_kwh, double* produced_kwh);

#ifdef __cplusplus
}
#endif

#endif /* HOST_HARNESS_DUMP_DATA_H */
