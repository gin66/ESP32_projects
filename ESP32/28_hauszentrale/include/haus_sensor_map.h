/**
 * @file haus_sensor_map.h
 * @brief Pure rank → ROM → function resolution (S9), no Arduino dependency.
 *
 * The producer side fills one haus_sensor_rank_t per 0x200 slot from the
 * 0x180+r identity frames. This module applies the two rules of §7.7:
 *   - rank → ROM expires after 90 s,
 *   - ROM → function comes from the local role table,
 * and resolves each function at the newest rank only, so a stale identity at
 * the old rank cannot fight a fresh one at the new rank.
 *
 * Shared by the device (haus_gui.cpp) and test/test_sensor_map.
 */

#ifndef HAUS_SENSOR_MAP_H
#define HAUS_SENSOR_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "haus_gui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAUS_SENSOR_ID_TTL_MS (90u * 1000u)
#define HAUS_SENSOR_MAX_ROLES 8

typedef struct {
  uint8_t rom[8];
  uint8_t function; /* SensorFn */
} haus_sensor_role_t;

typedef struct {
  bool seen;
  uint32_t seen_ms;
  uint8_t rom[8];
  int16_t temp_c100;
} haus_sensor_rank_t;

/* Kept across calls so the caller can report shifts and expiry once. */
typedef struct {
  uint8_t last_rank[SENSOR_FN_COUNT];
} haus_sensor_map_state_t;

typedef struct {
  uint8_t fn_of_slot[HAUS_CAN_MAX_SENSORS];
  int16_t temp_by_fn[SENSOR_FN_COUNT];
  bool expired[SENSOR_FN_COUNT];
  bool shifted[SENSOR_FN_COUNT];
  uint8_t from_rank[SENSOR_FN_COUNT];
  uint8_t to_rank[SENSOR_FN_COUNT];
} haus_sensor_map_output_t;

void haus_sensor_map_reset(haus_sensor_map_state_t* state);

/* Resolve the four ranks at `now_ms`. `roles` is the local ROM → function
 * table. Returns the number of functions currently bound. */
uint8_t haus_sensor_map_resolve(
    const haus_sensor_rank_t ranks[HAUS_CAN_MAX_SENSORS], uint32_t now_ms,
    const haus_sensor_role_t* roles, uint8_t role_count,
    haus_sensor_map_state_t* state, haus_sensor_map_output_t* out);

const char* haus_sensor_fn_name(uint8_t function);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_SENSOR_MAP_H */
