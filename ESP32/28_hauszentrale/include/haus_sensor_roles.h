/**
 * @file haus_sensor_roles.h
 * @brief ROM → function table for the CAN DS18B20 probes (S9.03/S9.04).
 *
 * The rank a probe appears at is transient; the function belongs to the probe.
 * This table is the local, editable half of the chain described in §7.7.
 */

#ifndef HAUS_SENSOR_ROLES_H
#define HAUS_SENSOR_ROLES_H

#include <stdbool.h>
#include <stdint.h>

#include "haus_gui.h"
#include "haus_sensor_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAUS_SENSOR_ROLE_PATH "/sensor_roles.bin"

/* Load the table from SPIFFS; seed it with the three known ROMs on first boot.
 * Returns true when the table is usable. */
bool haus_sensor_roles_init(void);

/* Resolve a ROM to a SensorFn. Returns SENSOR_FN_NONE when unbound. */
uint8_t haus_sensor_roles_resolve(const uint8_t rom[8]);

/* Persist the current table (used after a role edit). */
bool haus_sensor_roles_save(void);

/* Change one binding and persist it (S23.06). */
bool haus_sensor_roles_set(uint8_t index, uint8_t function);

/* Number of entries and read access for a future editor (S23.06). */
uint8_t haus_sensor_roles_count(void);
const haus_sensor_role_t* haus_sensor_roles_at(uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_SENSOR_ROLES_H */
