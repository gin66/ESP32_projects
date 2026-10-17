/**
 * @file haus_sd.h
 * @brief Project-specific SD-card mount for the Waveshare ESP32-S3 board.
 *
 * The SD card's chip-select is CH422G IO4, not a GPIO. The recorder owns this
 * handle; it does not use tpl_sd (whose CS is a real GPIO).
 */

#ifndef HAUS_SD_H
#define HAUS_SD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mount the card, if not already mounted. Returns true on success. */
bool haus_sd_mount(void);

/* Unmount and release the expander CS. */
void haus_sd_unmount(void);

/* Mounted and readable? Cheap read-only probe (one byte from a sentinel file),
 * suitable for the 1 Hz status poll. */
bool haus_sd_is_mounted(void);

/* Serialize card access between the 1 Hz probe and the recorder task. */
bool haus_sd_lock(uint32_t timeout_ms);
void haus_sd_unlock(void);

/* Refresh the cached capacity figures. This is the only call that reads the
 * FAT; the storage panel calls it on demand, never the render path. */
void haus_sd_space_refresh(void);

/* Cached capacity figures from the last refresh. Returns false when no valid
 * figures are available (card absent or never refreshed). */
bool haus_sd_space_get(uint64_t* total_bytes, uint64_t* used_bytes);

/* Card type string cached at mount time: "MMC", "SDSC", "SDHC", "UNKNOWN",
 * or "ABSENT" when no card is mounted. */
const char* haus_sd_card_type(void);

/* Write, read back and delete /sd_test.txt. Returns true when the round-trip
 * matches. Call after haus_sd_mount(). */
bool haus_sd_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_SD_H */
