/**
 * @file haus_sensor_map.cpp
 * @brief Pure rank → ROM → function resolution. See haus_sensor_map.h.
 */

#include "haus_sensor_map.h"

#include <string.h>

const char* haus_sensor_fn_name(uint8_t function) {
  switch (function) {
    case SENSOR_FN_CHAMBER:
      return "chamber";
    case SENSOR_FN_WATER_IN:
      return "water-in";
    case SENSOR_FN_WATER_OUT:
      return "water-out";
    case SENSOR_FN_BOARD:
      return "board";
    default:
      return "unbound";
  }
}

void haus_sensor_map_reset(haus_sensor_map_state_t* state) {
  if (!state) return;
  memset(state->last_rank, 0xFF, sizeof(state->last_rank));
}

uint8_t haus_sensor_map_resolve(
    const haus_sensor_rank_t ranks[HAUS_CAN_MAX_SENSORS], uint32_t now_ms,
    const haus_sensor_role_t* roles, uint8_t role_count,
    haus_sensor_map_state_t* state, haus_sensor_map_output_t* out) {
  if (!ranks || !roles || !state || !out) return 0;

  memset(out, 0, sizeof(*out));
  for (int i = 0; i < HAUS_CAN_MAX_SENSORS; i++)
    out->fn_of_slot[i] = SENSOR_FN_NONE;
  for (int f = 0; f < SENSOR_FN_COUNT; f++)
    out->temp_by_fn[f] = HAUS_CAN_TEMP_EMPTY;

  int best_rank[SENSOR_FN_COUNT];
  uint32_t best_seen[SENSOR_FN_COUNT];
  for (int f = 0; f < SENSOR_FN_COUNT; f++) {
    best_rank[f] = -1;
    best_seen[f] = 0;
  }

  /* Learn rank → ROM. Expired identities are demoted to unknown (S9.02). */
  for (int r = 0; r < HAUS_CAN_MAX_SENSORS; r++) {
    if (!ranks[r].seen) continue;
    if (now_ms - ranks[r].seen_ms > HAUS_SENSOR_ID_TTL_MS) continue;

    uint8_t fn = SENSOR_FN_NONE;
    for (uint8_t i = 0; i < role_count && i < HAUS_SENSOR_MAX_ROLES; i++) {
      if (memcmp(roles[i].rom, ranks[r].rom, 8) == 0) {
        fn = roles[i].function;
        break;
      }
    }
    if (fn == SENSOR_FN_NONE) continue; /* seen but unbound locally (S9.06) */

    /* A ROM seen at two ranks resolves at the newest one only. */
    if (best_rank[fn] < 0 || ranks[r].seen_ms >= best_seen[fn]) {
      best_seen[fn] = ranks[r].seen_ms;
      best_rank[fn] = r;
    }
  }

  uint8_t bound = 0;
  for (int fn = 0; fn < SENSOR_FN_COUNT; fn++) {
    const int r = best_rank[fn];
    if (r < 0) {
      if (state->last_rank[fn] != 0xFF) out->expired[fn] = true;
      state->last_rank[fn] = 0xFF;
      continue;
    }
    out->fn_of_slot[r] = (uint8_t)fn;
    out->temp_by_fn[fn] = ranks[r].temp_c100;
    bound++;
    if (state->last_rank[fn] != 0xFF && state->last_rank[fn] != r) {
      out->shifted[fn] = true;
      out->from_rank[fn] = state->last_rank[fn];
      out->to_rank[fn] = (uint8_t)r;
    }
    state->last_rank[fn] = (uint8_t)r;
  }
  return bound;
}
