/**
 * @file haus_sensor_roles.cpp
 * @brief SPIFFS-backed ROM → function table (S9.03).
 */

#include "haus_sensor_roles.h"

#include <Arduino.h>
#include <SPIFFS.h>

#include <cstring>

#include "tpl_debug_log.h"

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t version;
  uint8_t count;
  uint8_t reserved;
  haus_sensor_role_t roles[4];
} RolesFile;

#define ROLES_MAGIC 0x52534E48u /* "HNSR" */
#define ROLES_VERSION 2

static haus_sensor_role_t s_roles[4] = {
    {{0x28, 0x1B, 0xF7, 0x57, 0x04, 0xE1, 0x3C, 0x26}, SENSOR_FN_CHAMBER},
    {{0x28, 0x76, 0xC2, 0x57, 0x04, 0xE1, 0x3C, 0xF9}, SENSOR_FN_WATER_IN},
    {{0x28, 0xB9, 0xEE, 0x81, 0xE3, 0xE1, 0x3C, 0x2E}, SENSOR_FN_BOARD},
    {{0, 0, 0, 0, 0, 0, 0, 0}, SENSOR_FN_NONE},
};
static uint8_t s_count = 3;

uint8_t haus_sensor_roles_count(void) { return s_count; }

const haus_sensor_role_t* haus_sensor_roles_at(uint8_t index) {
  return index < s_count ? &s_roles[index] : NULL;
}

bool haus_sensor_roles_save(void) {
  RolesFile file;
  memset(&file, 0, sizeof(file));
  file.magic = ROLES_MAGIC;
  file.version = ROLES_VERSION;
  file.count = s_count;
  memcpy(file.roles, s_roles, sizeof(s_roles));

  File f = SPIFFS.open(HAUS_SENSOR_ROLE_PATH, "w");
  if (!f) {
    TPL_LOGE("roles", "cannot write %s", HAUS_SENSOR_ROLE_PATH);
    return false;
  }
  const size_t wrote = f.write((const uint8_t*)&file, sizeof(file));
  f.close();
  if (wrote != sizeof(file)) {
    TPL_LOGE("roles", "short write (%u/%u)", (unsigned)wrote,
             (unsigned)sizeof(file));
    return false;
  }
  TPL_LOGI("roles", "saved %u entries", (unsigned)s_count);
  return true;
}

bool haus_sensor_roles_set(uint8_t index, uint8_t function) {
  if (index >= s_count || function >= SENSOR_FN_COUNT) return false;
  if (s_roles[index].function == function) return true;
  s_roles[index].function = function;
  TPL_LOGI("roles", "role %u -> %s", (unsigned)index,
           haus_sensor_fn_name(function));
  return haus_sensor_roles_save();
}

bool haus_sensor_roles_init(void) {
  File f = SPIFFS.open(HAUS_SENSOR_ROLE_PATH, "r");
  if (f) {
    RolesFile file;
    const size_t got = f.read((uint8_t*)&file, sizeof(file));
    f.close();
    if (got == sizeof(file) && file.magic == ROLES_MAGIC &&
        file.version == ROLES_VERSION && file.count <= 4) {
      s_count = file.count;
      memcpy(s_roles, file.roles, sizeof(s_roles));
      TPL_LOGI("roles", "loaded %u entries from SPIFFS", (unsigned)s_count);
      return true;
    }
    TPL_LOGW("roles", "invalid %s; reseeding", HAUS_SENSOR_ROLE_PATH);
  }

  /* First boot: seed the three ROMs from the installed heater (§7.7). */
  if (!haus_sensor_roles_save()) return false;
  TPL_LOGI("roles", "seeded %u entries", (unsigned)s_count);
  return true;
}

uint8_t haus_sensor_roles_resolve(const uint8_t rom[8]) {
  if (!rom) return SENSOR_FN_NONE;
  for (uint8_t i = 0; i < s_count; i++) {
    if (memcmp(s_roles[i].rom, rom, 8) == 0) return s_roles[i].function;
  }
  return SENSOR_FN_NONE;
}
