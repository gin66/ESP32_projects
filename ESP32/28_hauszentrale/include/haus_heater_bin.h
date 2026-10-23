/**
 * @file haus_heater_bin.h
 * @brief SPIFFS-backed heater configuration persistence (/heater.bin).
 *
 * Versioned binary file that stores the heater's authority mode, the last
 * manual duty, and the schedule (day flow, night setback, night floor) so the
 * panel can restore its previous state after a reboot.
 *
 * File format (little-endian, packed):
 *
 *   Offset  Size  Field
 *   ------  ----  -----
 *   0       4     magic  (0x484E5452u = "HNTQ")
 *   4       2     version (currently 1)
 *   6       4     CRC32 over payload (bytes 10..end)
 *   10      1     mode (HeaterMode)
 *   11      1     day_start_hour
 *   12      1     night_start_hour
 *   13      2     saved_duty
 *   15      2     T_day_c100 (c°)
 *   17      2     night_setback_c100 (c°)
 *   19      2     night_floor_c100 (c°)
 *
 * Total payload: 11 bytes.
 * Total file: 21 bytes (10-byte header + 11-byte payload).
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

/* ─── Payload structure (11 bytes, packed) ──────────────────────────────── */
/* Defined outside extern "C" so it's visible to both C and C++ host tests. */

typedef struct __attribute__((packed)) {
  uint8_t mode;               /**< HeaterMode (byte 0).                  */
  uint8_t day_start_hour;     /**< Hour when day flow begins (byte 1).   */
  uint8_t night_start_hour;   /**< Hour when night setback begins (byte 2). */
  uint16_t saved_duty;        /**< Last manual duty (byte 3).             */
  int16_t T_day_c100;         /**< Day flow temperature in c° (byte 5).  */
  int16_t night_setback_c100; /**< Night setback in c° (byte 7).        */
  int16_t night_floor_c100;   /**< Night floor temperature in c° (byte 9). */
} HeaterBinPayload;

/* File layout constants. */
#define HAUS_HEATER_BIN_HEADER_SIZE 10  /* magic(4) + version(2) + CRC32(4) */
#define HAUS_HEATER_BIN_PAYLOAD_SIZE 11 /* mode + hours + duty + 3 c° fields  \
                                         */
#define HAUS_HEATER_BIN_FILE_SIZE 21    /* header + payload */

/* Magic number: "HNTQ" (Heater NTC Query — mnemonic for heater config). */
#define HAUS_HEATER_BIN_MAGIC 0x484E5452u

/* Current file format version. */
#define HAUS_HEATER_BIN_VERSION 1

/* SPIFFS path. */
#define HAUS_HEATER_BIN_PATH "/heater.bin"

/* Runtime configuration: persisted mode, manual duty, and schedule. */
typedef struct {
  HeaterMode mode;         /**< Authority mode (OFF on first boot). */
  uint16_t saved_duty;     /**< Last manual duty (100..1000).        */
  HeaterSchedule schedule; /**< Day/night flow schedule.            */
} HeaterConfig;

/* Fill config with safe defaults: mode OFF, saved duty 1000, schedule default.
 */
void heater_config_default(HeaterConfig *config);

#ifdef __cplusplus
extern "C" {
#endif

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
