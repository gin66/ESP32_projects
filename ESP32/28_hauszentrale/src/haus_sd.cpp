/**
 * @file haus_sd.cpp
 * @brief Project-specific SD-card mount for the Waveshare ESP32-S3 board.
 *
 * The board's SD_CS is CH422G IO4 (active LOW). The Arduino SD library needs a
 * GPIO for CS, so a spare GPIO is passed as a dummy and the expander bit is
 * asserted for the whole mount. The SD SPI bus uses the board pins
 * MOSI 11 / SCLK 12 / MISO 13.
 *
 * The bus pins and the expander CS are supplied to tpl_sd as a TplSdBackend;
 * tpl_sd owns the mount and the /sd web endpoints. This module adds the
 * recorder's locking, sentinel probe and space cache around that mount.
 */

#include "haus_sd.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "tpl_debug_log.h"
#include "tpl_sd.h"
#include "tpl_ws7.h"

#define HAUS_SD_DUMMY_CS 15
#define HAUS_SD_PROBE_PATH "/.sd_probe"
#define HAUS_SD_PROBE_BYTE 'H'

static SPIClass s_sd_spi(HSPI);
static bool s_bus_open = false;
static bool s_mounted = false;
static bool s_ever_mounted = false;
static uint64_t s_total_bytes = 0;
static uint64_t s_used_bytes = 0;
static bool s_space_valid = false;
static char s_card_type[8] = "ABSENT";

/* tpl_sd CS/bus backend: CH422G IO4 must be push-pull and asserted (active
 * LOW) for the whole session. The SD library toggles the dummy GPIO; only one
 * device is on this bus. */
static bool haus_sd_bus_begin() {
  if (tpl_ws7_sd_cs_enable_oc_pp() != ESP_OK) {
    TPL_LOGE("sd", "CS push-pull setup failed");
    return false;
  }
  if (tpl_ws7_sd_select(true) != ESP_OK) {
    TPL_LOGE("sd", "CS assert failed");
    return false;
  }
  s_sd_spi.begin(TPL_WS7_SD_SCLK, TPL_WS7_SD_MISO, TPL_WS7_SD_MOSI,
                 HAUS_SD_DUMMY_CS);
  s_bus_open = true;
  return true;
}

static void haus_sd_bus_end() {
  if (s_bus_open) {
    s_sd_spi.end();
    s_bus_open = false;
  }
  tpl_ws7_sd_select(false);
}

static const TplSdBackend s_sd_backend = {
    &s_sd_spi, HAUS_SD_DUMMY_CS, haus_sd_bus_begin, haus_sd_bus_end};

bool haus_sd_mount(void) {
  if (s_mounted && haus_sd_is_mounted()) return true;
  if (s_mounted) haus_sd_unmount();

  if (!tpl_sd_setup(&s_sd_backend)) {
    TPL_LOGE("sd", "mount failed (card absent or not responding)");
    return false;
  }

  /* The probe write must not race a web request that is already allowed in
   * (tpl_sd_initialized is published before this returns). */
  if (!tpl_sd_lock(2000)) {
    TPL_LOGE("sd", "probe lock failed");
    tpl_sd_end();
    return false;
  }
  uint8_t type = SD.cardType();

  /* A one-byte sentinel makes removal detection a cheap read instead of a
   * full FAT scan (totalBytes) on every 1 Hz status tick. */
  File probe = SD.open(HAUS_SD_PROBE_PATH, FILE_WRITE);
  if (!probe) {
    TPL_LOGE("sd", "could not create probe file");
    tpl_sd_unlock();
    tpl_sd_end();
    return false;
  }
  const size_t probe_wrote = probe.write((uint8_t)HAUS_SD_PROBE_BYTE);
  probe.close();
  tpl_sd_unlock();

  if (probe_wrote != 1) {
    TPL_LOGE("sd", "could not write probe file");
    tpl_sd_end();
    return false;
  }

  const char* type_str = "UNKNOWN";
  if (type == CARD_MMC)
    type_str = "MMC";
  else if (type == CARD_SD)
    type_str = "SDSC";
  else if (type == CARD_SDHC)
    type_str = "SDHC";
  snprintf(s_card_type, sizeof(s_card_type), "%s", type_str);

  s_mounted = true;
  s_ever_mounted = true;
  haus_sd_space_refresh();

  TPL_LOGI("sd", "mounted: %s, %llu MB total, %llu MB used\n", type_str,
           (unsigned long long)(s_total_bytes / (1024ULL * 1024ULL)),
           (unsigned long long)(s_used_bytes / (1024ULL * 1024ULL)));

  return true;
}

void haus_sd_unmount(void) {
  if (!tpl_sd_end()) {
    TPL_LOGW("sd", "unmount skipped, lock busy");
    return;
  }
  s_mounted = false;
  s_space_valid = false;
  snprintf(s_card_type, sizeof(s_card_type), "ABSENT");
}

bool haus_sd_lock(uint32_t timeout_ms) { return tpl_sd_lock(timeout_ms); }

void haus_sd_unlock(void) { tpl_sd_unlock(); }

bool haus_sd_is_mounted(void) {
  if (!s_mounted) return false;
  /* newlib abort()s inside fopen when the FILE lock cannot be allocated.
   * That is a reboot, not a "card missing" result. Skip the probe while
   * internal RAM has no contiguous block for it and keep the last mount. */
  const size_t largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (largest < 4096) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      TPL_LOGW("sd", "probe skipped, internal largest %u B", (unsigned)largest);
    }
    return true;
  }
  if (!haus_sd_lock(100)) {
    /* Another task owns the card (browse, flush). That is not evidence that
     * the card was pulled; keep the last known state. */
    static bool logged = false;
    if (!logged) {
      logged = true;
      TPL_LOGW("sd", "probe skipped, lock busy");
    }
    return true;
  }

  /* Cheap read-only probe: one byte, not a FAT scan. A pulled card makes
   * open/read fail within the SPI timeout. */
  File probe = SD.open(HAUS_SD_PROBE_PATH, FILE_READ);
  const int c = probe ? probe.read() : -1;
  if (probe) probe.close();
  haus_sd_unlock();
  return c == HAUS_SD_PROBE_BYTE;
}

void haus_sd_space_refresh(void) {
  if (!s_mounted) {
    s_space_valid = false;
    return;
  }
  /* Non-blocking: a FAT walk must never make the recorder wait. */
  if (!tpl_sd_lock(0)) return;
  /* The only FAT-scanning calls; the storage panel invokes this on demand. */
  s_total_bytes = SD.totalBytes();
  s_used_bytes = SD.usedBytes();
  s_space_valid = (s_total_bytes != 0);
  tpl_sd_unlock();
}

bool haus_sd_space_get(uint64_t* total_bytes, uint64_t* used_bytes) {
  if (!s_space_valid) return false;
  if (total_bytes) *total_bytes = s_total_bytes;
  if (used_bytes) *used_bytes = s_used_bytes;
  return true;
}

const char* haus_sd_card_type(void) { return s_card_type; }

bool haus_sd_present(void) { return s_mounted; }

bool haus_sd_ever_mounted(void) { return s_ever_mounted; }

void haus_sd_note_io_error(void) {
  if (!s_mounted) return;
  TPL_LOGW("sd", "I/O error; marking card absent until remount");
  if (!tpl_sd_end()) {
    TPL_LOGW("sd", "unmount deferred, lock busy");
    return;
  }
  s_mounted = false;
  s_space_valid = false;
  snprintf(s_card_type, sizeof(s_card_type), "ABSENT");
}

bool haus_sd_selftest(void) {
  if (!s_mounted) return false;
  if (!tpl_sd_lock(2000)) return false;

  const char* path = "/sd_test.txt";
  const char* payload = "hauszentrale sd bring-up\n";

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    TPL_LOGE("sd", "self-test: open for write failed");
    tpl_sd_unlock();
    return false;
  }
  const size_t wrote = f.print(payload);
  f.close();
  if (wrote != strlen(payload)) {
    TPL_LOGE("sd", "self-test: short write");
    tpl_sd_unlock();
    return false;
  }

  f = SD.open(path, FILE_READ);
  if (!f) {
    TPL_LOGE("sd", "self-test: open for read failed");
    tpl_sd_unlock();
    return false;
  }
  String back = f.readString();
  f.close();
  SD.remove(path);

  if (back != payload) {
    TPL_LOGE("sd", "self-test: read-back mismatch (%u bytes)\n",
             (unsigned)back.length());
    tpl_sd_unlock();
    return false;
  }

  tpl_sd_unlock();
  TPL_LOGI("sd", "self-test: write/read/delete OK");
  return true;
}
