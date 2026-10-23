/**
 * @file haus_heater_bin.h
 * @brief SPIFFS-backed heater configuration persistence (/heater.bin).
 *
 * Versioned binary file that stores the heater's authority mode, the last
 * manual duty, the schedule (day flow, night setback, night floor), and the
 * anti-short-cycling parameters so the panel can restore its previous state
 * after a reboot.
 *
 * File format (little-endian, packed):
 *
 *   Offset  Size  Field
 *   ------  ----  -----
 *   0       4     magic  (0x484E5452u = "HNTQ")
 *   4       2     version (currently 2)
 *   6       4     CRC32 over payload (bytes 10..end)
 *   10      1     mode (HeaterMode)
 *   11      1     day_start_hour
 *   12      1     night_start_hour
 *   13      2     saved_duty
 *   15      2     T_day_c100 (c°)
 *   17      2     night_setback_c100 (c°)
 *   19      2     night_floor_c100 (c°)
 *   21      2     control_period_s
 *   23      2     min_on_s
 *   25      2     min_off_s
 *   27      1     max_edges_per_hour
 *   28      2     min_duty_interval_s
 *   30      2     max_duty_step
 *   32      2     room_hysteresis_c100
 *   34      2     flow_deadband_c100
 *
 * Total payload: 26 bytes.
 * Total file: 36 bytes (10-byte header + 26-byte payload).
 *
 * Version 1 (21-byte file, no cycle parameters) is rejected; the panel then
 * comes up in safe OFF defaults.
 *
 * On bad file (magic mismatch, version mismatch, CRC error, out-of-range
 * fields):
 *   - Reject and keep previous mode in RAM.
 *   - Never come up as AUTO from a corrupt file.
 *
 * See §8 of whitepaper_auto_heating.md.
 */

#ifndef HAUS_HEATER_BIN_H
#define HAUS_HEATER_BIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "haus_heater.h"

/* ─── Payload structure (26 bytes, packed) ──────────────────────────────── */
/* Defined outside extern "C" so it's visible to both C and C++ host tests. */

typedef struct __attribute__((packed)) {
  uint8_t mode;               /**< HeaterMode (byte 0).                  */
  uint8_t day_start_hour;     /**< Hour when day flow begins (byte 1).   */
  uint8_t night_start_hour;   /**< Hour when night setback begins (byte 2). */
  uint16_t saved_duty;        /**< Last manual duty (byte 3).             */
  int16_t T_day_c100;         /**< Day flow temperature in c° (byte 5).  */
  int16_t night_setback_c100; /**< Night setback in c° (byte 7).        */
  int16_t night_floor_c100;   /**< Night floor temperature in c° (byte 9). */
  uint16_t control_period_s;  /**< Anti-short-cycling tick (byte 11).    */
  uint16_t min_on_s;          /**< Minimum HEATING time (byte 13).      */
  uint16_t min_off_s;         /**< Minimum off time (byte 15).          */
  uint8_t max_edges_per_hour; /**< AUTO on-edges per hour (byte 17).    */
  uint16_t min_duty_interval_s; /**< Min duty-move interval (byte 18).  */
  uint16_t max_duty_step;     /**< Max permille per move (byte 20).      */
  uint16_t room_hysteresis_c100; /**< Future trim (byte 22).            */
  uint16_t flow_deadband_c100; /**< Future trim (byte 24).              */
} HeaterBinPayload;

/* File layout constants. */
#define HAUS_HEATER_BIN_HEADER_SIZE 10  /* magic(4) + version(2) + CRC32(4) */
#define HAUS_HEATER_BIN_PAYLOAD_SIZE 26 /* mode + hours + duty + 3 c° +      \
                                          cycle parameters */
#define HAUS_HEATER_BIN_FILE_SIZE 36    /* header + payload */

/* Magic number: "HNTQ" (Heater NTC Query — mnemonic for heater config). */
#define HAUS_HEATER_BIN_MAGIC 0x484E5452u

/* Current file format version. */
#define HAUS_HEATER_BIN_VERSION 2

/* SPIFFS path. */
#define HAUS_HEATER_BIN_PATH "/heater.bin"

/* Runtime configuration: persisted mode, manual duty, and schedule. */
typedef struct {
  HeaterMode mode;          /**< Authority mode (OFF on first boot). */
  uint16_t saved_duty;      /**< Last manual duty (100..1000).        */
  HeaterSchedule schedule;  /**< Day/night flow schedule.            */
  HeaterCycleParams cycle;  /**< Anti-short-cycling parameters.      */
} HeaterConfig;

/* Fill config with safe defaults: mode OFF, saved duty 1000, schedule default.
 */
void heater_config_default(HeaterConfig *config);

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Runtime cache ──────────────────────────────────────────────────────── */

/* Load /heater.bin into the module cache once. If the file is missing or
 * invalid the cache holds defaults (mode OFF) and the defaults are written
 * back so the next boot restores the same safe state.
 *
 * Safe to call more than once; only the first call reads the file. */
void heater_config_init(void);

/* Current configuration (never NULL). Valid after heater_config_init(). */
const HeaterConfig *heater_config_get(void);

/* Replace the cached configuration and persist it. Returns true when the
 * file was written. Callers request a full redraw when the write succeeded
 * (the SPIFFS write can invalidate the active framebuffer). */
bool heater_config_apply(const HeaterConfig *config);

/* Convenience setters that keep the other fields and persist. */
bool heater_config_set_mode(HeaterMode mode);
bool heater_config_set_saved_duty(uint16_t saved_duty);

/* ─── Pure serialisation (no flash access, testable on the host) ─────────── */

/* Encode a config into buf. Returns bytes written (HAUS_HEATER_BIN_FILE_SIZE)
 * or 0 when buf is NULL/too small or config is NULL. */
size_t heater_bin_encode(uint8_t *buf, size_t buf_len,
                         const HeaterConfig *config);

/* Decode a buffer into config. Returns true only when the magic, version,
 * CRC32, and field ranges are all valid. On failure the outputs are not
 * modified. */
bool heater_bin_decode(const uint8_t *buf, size_t buf_len,
                       HeaterConfig *config);

/* ─── Flash persistence ──────────────────────────────────────────────────── */

/* Load the heater configuration from SPIFFS.
 *
 * Returns:
 *   true  — file was valid and loaded successfully.
 *   false — file is absent, corrupt, or version mismatch; no state changed.
 *
 * If the file does not exist yet (first boot):
 *   - Returns false.
 *   - The caller keeps its in-RAM defaults (mode = OFF).
 */
bool heater_bin_load(HeaterConfig *config);

/* Save the heater configuration to SPIFFS.
 *
 * The file is written atomically via a temporary file:
 *   1. Write to /heater.tmp
 *   2. Close and verify the write
 *   3. Rename /heater.tmp → /heater.bin
 *
 * If the rename fails, the old file is preserved.
 */
bool heater_bin_save(const HeaterConfig *config);

/* Remove the heater configuration file (for reset).
 *
 * Returns true when the file was removed or did not exist.
 */
bool heater_bin_remove(void);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_HEATER_BIN_H */
