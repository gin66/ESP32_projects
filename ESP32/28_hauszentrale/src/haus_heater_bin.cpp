/**
 * @file haus_heater_bin.cpp
 * @brief SPIFFS-backed heater configuration persistence (/heater.bin).
 *
 * Implements versioned binary persistence for heater mode, the last manual
 * duty, and the schedule. Follows the pattern of day-file/rollup.bin (not
 * sensor_roles.bin which has no CRC).
 *
 * The encoding/decoding is split from the flash I/O so the host test can
 * verify the format, CRC, and rejection rules without SPIFFS.
 *
 * See §8 of whitepaper_auto_heating.md.
 */

#include "haus_heater_bin.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <SPIFFS.h>

#include "tpl_debug_log.h"
#endif

#include <cstring>

/* ─── CRC32 (ISO 3309 / ITU-V.42, same as PKZIP) ─────────────────────────── */

static uint32_t crc32_compute(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320u : 0);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

/* ─── File layout helpers ────────────────────────────────────────────────── */

/* Packed file structure: 4-byte magic + 2-byte version + 4-byte CRC32 +
 * 11-byte payload = 21 bytes total. */
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t version;
  uint32_t crc32;
  HeaterBinPayload payload;
} HeaterBinFile;

#if defined(__cplusplus)
static_assert(sizeof(HeaterBinFile) == HAUS_HEATER_BIN_FILE_SIZE,
              "HeaterBinFile must be exactly 21 bytes");
#else
_Static_assert(sizeof(HeaterBinFile) == HAUS_HEATER_BIN_FILE_SIZE,
               "HeaterBinFile must be exactly 21 bytes");
#endif

/* ─── Field-range checks ─────────────────────────────────────────────────── */

static bool mode_is_valid(uint8_t mode) {
  return mode <= (uint8_t)HEATER_MODE_OVERRIDE;
}

static bool payload_is_valid(const HeaterBinPayload *p) {
  if (!mode_is_valid(p->mode))
    return false;
  if (p->day_start_hour > 23)
    return false;
  if (p->night_start_hour > 23)
    return false;
  if (p->saved_duty > 1000)
    return false;

  /* c° fields: 0..100 °C for flow temperatures; setback 0..40 K. */
  if (p->T_day_c100 < 0 || p->T_day_c100 > 10000)
    return false;
  if (p->night_setback_c100 < 0 || p->night_setback_c100 > 4000)
    return false;
  if (p->night_floor_c100 < 0 || p->night_floor_c100 > 10000)
    return false;
  return true;
}

static void payload_from_config(HeaterBinPayload *p,
                                const HeaterConfig *config) {
  std::memset(p, 0, sizeof(*p));
  p->mode = (uint8_t)config->mode;
  p->day_start_hour = config->schedule.day_start_hour;
  p->night_start_hour = config->schedule.night_start_hour;
  p->saved_duty = config->saved_duty;
  p->T_day_c100 = config->schedule.T_day_c100;
  p->night_setback_c100 = config->schedule.night_setback_c100;
  p->night_floor_c100 = config->schedule.night_floor_c100;
}

static void config_from_payload(const HeaterBinPayload *p,
                                HeaterConfig *config) {
  config->mode = (HeaterMode)p->mode;
  config->saved_duty = p->saved_duty;
  config->schedule.day_start_hour = p->day_start_hour;
  config->schedule.night_start_hour = p->night_start_hour;
  config->schedule.T_day_c100 = p->T_day_c100;
  config->schedule.night_setback_c100 = p->night_setback_c100;
  config->schedule.night_floor_c100 = p->night_floor_c100;
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

void heater_config_default(HeaterConfig *config) {
  if (!config)
    return;
  config->mode = HEATER_MODE_OFF;
  config->saved_duty = 1000;
  heater_schedule_default(&config->schedule);
}

/* ─── Runtime cache ──────────────────────────────────────────────────────── */

static HeaterConfig s_config;
static bool s_config_ready = false;

void heater_config_init(void) {
  if (s_config_ready)
    return;

  heater_config_default(&s_config);
  if (!heater_bin_load(&s_config)) {
    /* Missing or invalid file: keep defaults and write them once so the next
     * boot restores the safe OFF state instead of retrying forever. */
    heater_bin_save(&s_config);
  }
  s_config_ready = true;
}

const HeaterConfig *heater_config_get(void) {
  if (!s_config_ready)
    heater_config_init();
  return &s_config;
}

bool heater_config_apply(const HeaterConfig *config) {
  if (!config)
    return false;
  if (!s_config_ready) {
    heater_config_default(&s_config);
    s_config_ready = true;
  }
  s_config = *config;
  return heater_bin_save(&s_config);
}

bool heater_config_set_mode(HeaterMode mode) {
  if (!s_config_ready)
    heater_config_init();
  s_config.mode = mode;
  return heater_bin_save(&s_config);
}

bool heater_config_set_saved_duty(uint16_t saved_duty) {
  if (!s_config_ready)
    heater_config_init();
  s_config.saved_duty = saved_duty;
  return heater_bin_save(&s_config);
}

size_t heater_bin_encode(uint8_t *buf, size_t buf_len,
                         const HeaterConfig *config) {
  if (!buf || !config || buf_len < HAUS_HEATER_BIN_FILE_SIZE)
    return 0;

  HeaterBinFile file;
  std::memset(&file, 0, sizeof(file));
  file.magic = HAUS_HEATER_BIN_MAGIC;
  file.version = HAUS_HEATER_BIN_VERSION;
  payload_from_config(&file.payload, config);
  file.crc32 = crc32_compute((const uint8_t *)&file.payload,
                             HAUS_HEATER_BIN_PAYLOAD_SIZE);

  std::memcpy(buf, &file, sizeof(file));
  return sizeof(file);
}

bool heater_bin_decode(const uint8_t *buf, size_t buf_len,
                       HeaterConfig *config) {
  if (!buf || !config || buf_len < HAUS_HEATER_BIN_FILE_SIZE)
    return false;

  HeaterBinFile file;
  std::memcpy(&file, buf, sizeof(file));

  if (file.magic != HAUS_HEATER_BIN_MAGIC)
    return false;
  if (file.version != HAUS_HEATER_BIN_VERSION)
    return false;

  const uint32_t computed_crc = crc32_compute((const uint8_t *)&file.payload,
                                              HAUS_HEATER_BIN_PAYLOAD_SIZE);
  if (computed_crc != file.crc32)
    return false;
  if (!payload_is_valid(&file.payload))
    return false;

  HeaterConfig decoded;
  config_from_payload(&file.payload, &decoded);
  *config = decoded;
  return true;
}

bool heater_bin_load(HeaterConfig *config) {
  if (!config)
    return false;

#ifdef ARDUINO
  File f = SPIFFS.open(HAUS_HEATER_BIN_PATH, "r");
  if (!f) {
    /* File absent — first boot, use defaults. */
    return false;
  }

  uint8_t buf[HAUS_HEATER_BIN_FILE_SIZE];
  const size_t got = f.read(buf, sizeof(buf));
  f.close();

  if (got != sizeof(buf)) {
    TPL_LOGW("heater_bin", "short read (%u/%u)", (unsigned)got,
             (unsigned)sizeof(buf));
    return false;
  }

  if (!heater_bin_decode(buf, sizeof(buf), config)) {
    TPL_LOGW("heater_bin", "rejecting invalid %s", HAUS_HEATER_BIN_PATH);
    return false;
  }

  TPL_LOGI("heater_bin", "loaded mode=%d, duty=%u, day=%u:%02u, T_day=%d °C",
           (int)config->mode, (unsigned)config->saved_duty,
           config->schedule.day_start_hour, config->schedule.night_start_hour,
           (int)(config->schedule.T_day_c100 / 100));
  return true;
#else
  /* Host build: no SPIFFS. The pure encode/decode path is tested instead. */
  (void)config;
  return false;
#endif
}

bool heater_bin_save(const HeaterConfig *config) {
  if (!config)
    return false;

#ifdef ARDUINO
  uint8_t buf[HAUS_HEATER_BIN_FILE_SIZE];
  const size_t encoded = heater_bin_encode(buf, sizeof(buf), config);
  if (encoded != sizeof(buf)) {
    TPL_LOGE("heater_bin", "cannot encode config");
    return false;
  }

  /* Write atomically via a temporary file. */
  File f = SPIFFS.open(HAUS_HEATER_BIN_PATH ".tmp", "w");
  if (!f) {
    TPL_LOGE("heater_bin", "cannot open %s.tmp for writing",
             HAUS_HEATER_BIN_PATH);
    return false;
  }

  const size_t wrote = f.write(buf, encoded);
  f.close();

  if (wrote != encoded) {
    TPL_LOGE("heater_bin", "short write (%u/%u)", (unsigned)wrote,
             (unsigned)encoded);
    SPIFFS.remove(HAUS_HEATER_BIN_PATH ".tmp");
    return false;
  }

  if (!SPIFFS.rename(HAUS_HEATER_BIN_PATH ".tmp", HAUS_HEATER_BIN_PATH)) {
    TPL_LOGE("heater_bin", "rename failed");
    SPIFFS.remove(HAUS_HEATER_BIN_PATH ".tmp");
    return false;
  }

  TPL_LOGI("heater_bin", "saved mode=%d, duty=%u, day=%u:%02u, T_day=%d °C",
           (int)config->mode, (unsigned)config->saved_duty,
           config->schedule.day_start_hour, config->schedule.night_start_hour,
           (int)(config->schedule.T_day_c100 / 100));
  return true;
#else
  /* Host build: no SPIFFS. The pure encode/decode path is tested instead. */
  (void)config;
  return false;
#endif
}

bool heater_bin_remove(void) {
#ifdef ARDUINO
  bool ok = SPIFFS.remove(HAUS_HEATER_BIN_PATH);
  ok = SPIFFS.remove(HAUS_HEATER_BIN_PATH ".tmp") || ok;
  return ok;
#else
  /* Host build: no SPIFFS, nothing to remove. */
  return true;
#endif
}
