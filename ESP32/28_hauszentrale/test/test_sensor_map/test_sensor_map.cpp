/**
 * @file test_sensor_map.cpp
 * @brief Virtual-CAN tests for S9: rank → ROM → function resolution.
 *
 * The 0x180+r identity frames and 0x200 slots are synthesised here; no board is
 * involved. Covers S9.01–S9.04, S9.06 and the duplicate-ROM case that produced
 * a rank-shift flapping loop on the bench.
 */

#include <cstdio>
#include <cstring>

#include "haus_sensor_map.h"

static int g_checks = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static const uint8_t ROM_CHAMBER[8] = {0x28, 0x1B, 0xF7, 0x57,
                                       0x04, 0xE1, 0x3C, 0x26};
static const uint8_t ROM_WATER_IN[8] = {0x28, 0x76, 0xC2, 0x57,
                                        0x04, 0xE1, 0x3C, 0xF9};
static const uint8_t ROM_BOARD[8] = {0x28, 0xB9, 0xEE, 0x81,
                                     0xE3, 0xE1, 0x3C, 0x2E};
static const uint8_t ROM_UNKNOWN[8] = {0x28, 0x00, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x01};

static const haus_sensor_role_t ROLES[] = {
    {{0x28, 0x1B, 0xF7, 0x57, 0x04, 0xE1, 0x3C, 0x26}, SENSOR_FN_CHAMBER},
    {{0x28, 0x76, 0xC2, 0x57, 0x04, 0xE1, 0x3C, 0xF9}, SENSOR_FN_WATER_IN},
    {{0x28, 0xB9, 0xEE, 0x81, 0xE3, 0xE1, 0x3C, 0x2E}, SENSOR_FN_BOARD},
};
static const uint8_t ROLE_COUNT = 3;

static void set_rank(haus_sensor_rank_t* r, const uint8_t* rom, int16_t temp,
                     uint32_t seen_ms) {
  r->seen = true;
  r->seen_ms = seen_ms;
  std::memcpy(r->rom, rom, 8);
  r->temp_c100 = temp;
}

static void clear_rank(haus_sensor_rank_t* r) {
  std::memset(r, 0, sizeof(*r));
}

static void make_ranks(haus_sensor_rank_t ranks[HAUS_CAN_MAX_SENSORS]) {
  for (int i = 0; i < HAUS_CAN_MAX_SENSORS; i++) clear_rank(&ranks[i]);
}

/* A — the three installed ROMs bind to chamber / water-in / board (S9.01). */
static int test_bind(void) {
  haus_sensor_rank_t ranks[HAUS_CAN_MAX_SENSORS];
  make_ranks(ranks);
  set_rank(&ranks[0], ROM_CHAMBER, 6123, 1000);
  set_rank(&ranks[1], ROM_WATER_IN, 4211, 1000);
  set_rank(&ranks[2], ROM_BOARD, 2350, 1000);

  haus_sensor_map_state_t state;
  haus_sensor_map_reset(&state);
  haus_sensor_map_output_t out;
  const uint8_t bound =
      haus_sensor_map_resolve(ranks, 1000, ROLES, ROLE_COUNT, &state, &out);

  CHECK(bound == 3);
  CHECK(out.fn_of_slot[0] == SENSOR_FN_CHAMBER);
  CHECK(out.fn_of_slot[1] == SENSOR_FN_WATER_IN);
  CHECK(out.fn_of_slot[2] == SENSOR_FN_BOARD);
  CHECK(out.temp_by_fn[SENSOR_FN_CHAMBER] == 6123);
  CHECK(out.temp_by_fn[SENSOR_FN_WATER_IN] == 4211);
  CHECK(out.temp_by_fn[SENSOR_FN_BOARD] == 2350);
  CHECK(out.temp_by_fn[SENSOR_FN_WATER_OUT] == HAUS_CAN_TEMP_EMPTY);
  CHECK(!out.shifted[SENSOR_FN_CHAMBER]);
  CHECK(!out.expired[SENSOR_FN_CHAMBER]);
  return 0;
}

/* B — an identity with no local binding stays unbound (S9.06). */
static int test_unbound(void) {
  haus_sensor_rank_t ranks[HAUS_CAN_MAX_SENSORS];
  make_ranks(ranks);
  set_rank(&ranks[0], ROM_UNKNOWN, 1000, 1000);

  haus_sensor_map_state_t state;
  haus_sensor_map_reset(&state);
  haus_sensor_map_output_t out;
  const uint8_t bound =
      haus_sensor_map_resolve(ranks, 1000, ROLES, ROLE_COUNT, &state, &out);

  CHECK(bound == 0);
  CHECK(out.fn_of_slot[0] == SENSOR_FN_NONE);
  CHECK(out.temp_by_fn[SENSOR_FN_CHAMBER] == HAUS_CAN_TEMP_EMPTY);
  return 0;
}

/* C — the same ROM at two ranks resolves at the newest only, once (bench bug). */
static int test_duplicate_newest_wins(void) {
  haus_sensor_rank_t ranks[HAUS_CAN_MAX_SENSORS];
  make_ranks(ranks);
  set_rank(&ranks[0], ROM_CHAMBER, 5000, 1000);

  haus_sensor_map_state_t state;
  haus_sensor_map_reset(&state);
  haus_sensor_map_output_t out;
  haus_sensor_map_resolve(ranks, 1000, ROLES, ROLE_COUNT, &state, &out);

  set_rank(&ranks[3], ROM_CHAMBER, 6100, 2000);
  const uint8_t bound =
      haus_sensor_map_resolve(ranks, 2000, ROLES, ROLE_COUNT, &state, &out);

  CHECK(bound == 1);
  CHECK(out.fn_of_slot[0] == SENSOR_FN_NONE);
  CHECK(out.fn_of_slot[3] == SENSOR_FN_CHAMBER);
  CHECK(out.temp_by_fn[SENSOR_FN_CHAMBER] == 6100);
  CHECK(out.shifted[SENSOR_FN_CHAMBER]);
  CHECK(out.from_rank[SENSOR_FN_CHAMBER] == 0);
  CHECK(out.to_rank[SENSOR_FN_CHAMBER] == 3);

  /* The next cycle with the same state must not report a shift again. */
  haus_sensor_map_resolve(ranks, 2500, ROLES, ROLE_COUNT, &state, &out);
  CHECK(!out.shifted[SENSOR_FN_CHAMBER]);
  CHECK(out.fn_of_slot[3] == SENSOR_FN_CHAMBER);
  return 0;
}

/* D — rank → ROM expires after 90 s and the zone is demoted (S9.02). */
static int test_expiry(void) {
  haus_sensor_rank_t ranks[HAUS_CAN_MAX_SENSORS];
  make_ranks(ranks);
  set_rank(&ranks[0], ROM_CHAMBER, 6123, 1000);

  haus_sensor_map_state_t state;
  haus_sensor_map_reset(&state);
  haus_sensor_map_output_t out;
  haus_sensor_map_resolve(ranks, 1000, ROLES, ROLE_COUNT, &state, &out);
  CHECK(out.fn_of_slot[0] == SENSOR_FN_CHAMBER);

  /* Exactly at the TTL is still valid. */
  haus_sensor_map_resolve(ranks, 1000 + HAUS_SENSOR_ID_TTL_MS, ROLES,
                          ROLE_COUNT, &state, &out);
  CHECK(out.fn_of_slot[0] == SENSOR_FN_CHAMBER);

  /* One millisecond later it is unknown, not a stale binding. */
  haus_sensor_map_resolve(ranks, 1001 + HAUS_SENSOR_ID_TTL_MS, ROLES,
                          ROLE_COUNT, &state, &out);
  CHECK(out.fn_of_slot[0] == SENSOR_FN_NONE);
  CHECK(out.temp_by_fn[SENSOR_FN_CHAMBER] == HAUS_CAN_TEMP_EMPTY);
  CHECK(out.expired[SENSOR_FN_CHAMBER]);
  return 0;
}

/* E — a sentinel slot is preserved, never turned into a temperature (S9.07). */
static int test_sentinel(void) {
  haus_sensor_rank_t ranks[HAUS_CAN_MAX_SENSORS];
  make_ranks(ranks);
  set_rank(&ranks[0], ROM_CHAMBER, HAUS_CAN_TEMP_EMPTY, 1000);

  haus_sensor_map_state_t state;
  haus_sensor_map_reset(&state);
  haus_sensor_map_output_t out;
  haus_sensor_map_resolve(ranks, 1000, ROLES, ROLE_COUNT, &state, &out);

  CHECK(out.fn_of_slot[0] == SENSOR_FN_CHAMBER);
  CHECK(out.temp_by_fn[SENSOR_FN_CHAMBER] == HAUS_CAN_TEMP_EMPTY);
  CHECK(out.temp_by_fn[SENSOR_FN_CHAMBER] != -32768 + 1);
  return 0;
}

int main(void) {
  if (test_bind())
    return 1;
  if (test_unbound())
    return 1;
  if (test_duplicate_newest_wins())
    return 1;
  if (test_expiry())
    return 1;
  if (test_sentinel())
    return 1;
  std::printf("%d checks passed\n", g_checks);
  return 0;
}
