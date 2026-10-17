/**
 * @file haus_chart.h
 * @brief Downsampled chart column (whitepaper §9.3).
 *
 * Two series per column at most: the water-out screen overlays a second line,
 * and the living-room screen switches series rather than overlaying them.
 */

#ifndef HAUS_CHART_H
#define HAUS_CHART_H

#include <stdint.h>

#define HAUS_CHART_COLUMNS 440
#define HAUS_CHART_SERIES 2

#define CHART_COL_GAP 0x01
#define CHART_COL_PARTIAL 0x02

typedef struct {
    uint32_t t;                        /* bucket start, unix seconds */
    int16_t  mn[HAUS_CHART_SERIES];    /* 4 B */
    int16_t  mx[HAUS_CHART_SERIES];    /* 4 B */
    int32_t  sum[HAUS_CHART_SERIES];   /* 8 B */
    uint16_t n[HAUS_CHART_SERIES];     /* 4 B */
    uint8_t  flags;                    /* 1 B */
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

#endif /* HAUS_CHART_H */
