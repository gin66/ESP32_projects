/**
 * @file haus_rollup.h
 * @brief L2 hourly rollup column (whitepaper §7.10).
 *
 * One column per hour, 744 per 31-day window. Seven measured series plus the
 * Soll/setpoint: chamber, water-in, water-out, living room, humidity, pressure,
 * power. Power is kept in decawatts, the same unit as gui_sample_t.power_dw.
 * The target is a real series (future automatic control), carried as a single
 * value per hour. The layout is written and read as a flat block, so both the
 * ESP32 and the host harness must agree on its size exactly.
 */

#ifndef HAUS_ROLLUP_H
#define HAUS_ROLLUP_H

#include <stdint.h>

#define HAUS_HISTORY_SERIES 7
#define HAUS_ROLLUP_COLUMNS 744

typedef struct {
  uint32_t t;                       /* bucket start, unix minutes */
  int32_t mn[HAUS_HISTORY_SERIES];  /* 28 B */
  int32_t mx[HAUS_HISTORY_SERIES];  /* 28 B */
  int32_t sum[HAUS_HISTORY_SERIES]; /* 28 B */
  uint16_t n[HAUS_HISTORY_SERIES];  /* 14 B */
  int16_t target_c100;              /* 2 B, Soll/setpoint */
} RollupColumn;

#if defined(__cplusplus)
static_assert(sizeof(RollupColumn) == 104,
              "RollupColumn must stay 104 bytes (whitepaper §7.10)");
#else
_Static_assert(sizeof(RollupColumn) == 104,
               "RollupColumn must stay 104 bytes (whitepaper §7.10)");
#endif

#endif /* HAUS_ROLLUP_H */
