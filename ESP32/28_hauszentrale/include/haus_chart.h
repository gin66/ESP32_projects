/**
 * @file haus_chart.h
 * @brief Downsampled chart column (whitepaper §9.3).
 *
 * Two series per column at most: the water-out screen overlays a second line,
 * and the living-room screen switches series rather than overlaying them.
 */

#ifndef HAUS_CHART_H
#define HAUS_CHART_H

#include <stddef.h>
#include <stdint.h>

#include "haus_gui.h"
#include "haus_rollup.h"

#define HAUS_CHART_COLUMNS 440
#define HAUS_CHART_SERIES 2
#define HAUS_CHART_CHUNK_SAMPLES 512
#define HAUS_CHART_CHUNK_ROLLUP 64

#define CHART_COL_GAP 0x01
#define CHART_COL_PARTIAL 0x02

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t t;                     /* bucket start, unix seconds */
  int16_t mn[HAUS_CHART_SERIES];  /* 4 B */
  int16_t mx[HAUS_CHART_SERIES];  /* 4 B */
  int32_t sum[HAUS_CHART_SERIES]; /* 8 B */
  uint16_t n[HAUS_CHART_SERIES];  /* 4 B */
  uint8_t flags;                  /* 1 B */
} ChartColumn;

#if defined(__cplusplus)
static_assert(sizeof(ChartColumn) == 28,
              "ChartColumn must stay 28 bytes (whitepaper §9.3)");
static_assert(HAUS_CHART_COLUMNS * sizeof(ChartColumn) == 12320,
              "440 chart columns must be 12 320 bytes");
#else
_Static_assert(sizeof(ChartColumn) == 28,
               "ChartColumn must stay 28 bytes (whitepaper §9.3)");
_Static_assert(HAUS_CHART_COLUMNS * sizeof(ChartColumn) == 12320,
               "440 chart columns must be 12 320 bytes");
#endif

/* Streaming readers: fill `out` with [first, first+count) records and return
 * the number written. The builder calls these in fixed-size chunks. */
typedef size_t (*haus_chart_sample_reader)(void *ctx, size_t first,
                                           size_t count, gui_sample_t *out);
typedef size_t (*haus_chart_rollup_reader)(void *ctx, size_t first,
                                           size_t count, RollupColumn *out);

/* Build a 440-column envelope from raw minute samples or pre-aggregated
 * rollup columns. series_a/series_b are source series indices (0…5); pass -1
 * for no second series. */
void haus_chart_build_samples(ChartColumn *cols, uint32_t t0, uint32_t t1,
                              haus_chart_sample_reader reader, void *ctx,
                              size_t total, int series_a, int series_b);
void haus_chart_build_rollup(ChartColumn *cols, uint32_t t0, uint32_t t1,
                             haus_chart_rollup_reader reader, void *ctx,
                             size_t total, int series_a, int series_b);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_CHART_H */
