/**
 * @file test_chart.cpp
 * @brief Host tests for the streaming chart downsampler.
 *
 * A  streaming chunk limit (512 samples / 64 rollup columns)
 * B  sentinel and stale samples are excluded
 * C  GAP columns are marked, not interpolated
 * D  rollup min/max/sum/n are merged into the chart columns
 */

#include "haus_chart.h"
#include "haus_gui.h"
#include "haus_rollup.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint32_t g_checks;
static uint32_t g_failures;
static size_t g_max_sample_chunk;
static size_t g_max_rollup_chunk;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

#define SECTION(name) printf("-- %s\n", name)

#define DAY_SAMPLES 1440
#define DAY_SECONDS 86400u

static gui_sample_t g_samples[DAY_SAMPLES];
static RollupColumn g_rollups[HAUS_ROLLUP_COLUMNS];

static size_t read_samples(void *ctx, size_t first, size_t count,
                           gui_sample_t *out) {
    (void)ctx;
    if (first >= DAY_SAMPLES) return 0;
    size_t n = DAY_SAMPLES - first;
    if (n > count) n = count;
    if (n > g_max_sample_chunk) g_max_sample_chunk = n;
    memcpy(out, &g_samples[first], n * sizeof(gui_sample_t));
    return n;
}

static size_t read_rollups(void *ctx, size_t first, size_t count,
                           RollupColumn *out) {
    (void)ctx;
    if (first >= HAUS_ROLLUP_COLUMNS) return 0;
    size_t n = HAUS_ROLLUP_COLUMNS - first;
    if (n > count) n = count;
    if (n > g_max_rollup_chunk) g_max_rollup_chunk = n;
    memcpy(out, &g_rollups[first], n * sizeof(RollupColumn));
    return n;
}

static void prepare_day(uint32_t start) {
    memset(g_samples, 0, sizeof(g_samples));
    for (size_t i = 0; i < DAY_SAMPLES; i++) {
        g_samples[i].epoch = start + (uint32_t)i * 60u;
        g_samples[i].temp[0] = (int16_t)(3000 + (int32_t)(i % 100u));
        g_samples[i].temp[1] = (int16_t)(4000 + (int32_t)(i % 50u));
        g_samples[i].temp[2] = HAUS_CAN_TEMP_EMPTY;
    }

    /* 600–659: no sensor. 800–809: source stale. */
    for (size_t i = 600; i < 660; i++) g_samples[i].temp[0] = HAUS_CAN_TEMP_EMPTY;
    for (size_t i = 800; i < 810; i++)
        g_samples[i].stale_mask |= SAMPLE_STALE_CHAMBER;
}

static void test_samples(uint32_t start) {
    SECTION("A1  samples are streamed in 512-record chunks");
    static ChartColumn cols[HAUS_CHART_COLUMNS];
    g_max_sample_chunk = 0;

    haus_chart_build_samples(cols, start, start + DAY_SECONDS, read_samples,
                             NULL, DAY_SAMPLES, 0, -1);

    CHECK(g_max_sample_chunk <= HAUS_CHART_CHUNK_SAMPLES,
          "sample chunk = %u, limit %u", (unsigned)g_max_sample_chunk,
          (unsigned)HAUS_CHART_CHUNK_SAMPLES);
    CHECK(sizeof(ChartColumn) == 28u, "ChartColumn = %u",
          (unsigned)sizeof(ChartColumn));

    uint32_t total_n = 0;
    for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) total_n += cols[i].n[0];
    CHECK(total_n == DAY_SAMPLES - 60u - 10u,
          "total n = %u, want %u", (unsigned)total_n,
          (unsigned)(DAY_SAMPLES - 70u));

    SECTION("B1  sentinel and stale samples are excluded");
    CHECK(cols[0].n[0] > 0, "first column empty");
    const uint32_t first_val = (uint32_t)cols[0].mn[0];
    CHECK(first_val >= 3000u && first_val <= 3099u,
          "first min = %u", (unsigned)first_val);

    SECTION("C1  missing and stale spans become GAP columns");
    CHECK((cols[190].flags & CHART_COL_GAP) != 0,
          "column 190 should be a gap");
    CHECK((cols[245].flags & CHART_COL_GAP) != 0,
          "column 245 should be a gap");
}

static void test_rollups(uint32_t start) {
    SECTION("D1  rollups are streamed in 64-column chunks");
    static ChartColumn cols[HAUS_CHART_COLUMNS];
    g_max_rollup_chunk = 0;

    const uint32_t base_min = start / 60u;
    memset(g_rollups, 0, sizeof(g_rollups));
    for (size_t i = 0; i < HAUS_ROLLUP_COLUMNS; i++) {
        g_rollups[i].t = base_min + (uint32_t)i * 60u;
        if (i >= 100 && i <= 104) continue;  /* empty span -> gap */
        g_rollups[i].n[0] = 60;
        g_rollups[i].mn[0] = (int32_t)(1000 + (int32_t)i);
        g_rollups[i].mx[0] = (int32_t)(2000 + (int32_t)i);
        g_rollups[i].sum[0] = (g_rollups[i].mn[0] + g_rollups[i].mx[0]) * 30;
    }

    haus_chart_build_rollup(cols, start,
                            start + (uint32_t)HAUS_ROLLUP_COLUMNS * 3600u,
                            read_rollups, NULL, HAUS_ROLLUP_COLUMNS, 0, -1);

    CHECK(g_max_rollup_chunk <= HAUS_CHART_CHUNK_ROLLUP,
          "rollup chunk = %u, limit %u", (unsigned)g_max_rollup_chunk,
          (unsigned)HAUS_CHART_CHUNK_ROLLUP);

    uint32_t total_n = 0;
    for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) total_n += cols[i].n[0];
    CHECK(total_n == (uint32_t)(HAUS_ROLLUP_COLUMNS - 5u) * 60u,
          "total n = %u, want %u", (unsigned)total_n,
          (unsigned)((HAUS_ROLLUP_COLUMNS - 5u) * 60u));

    SECTION("D2  the empty hour is marked GAP");
    CHECK((cols[60].flags & CHART_COL_GAP) != 0,
          "column 60 should be a gap (hours 100-104 empty)");
}

int main(void) {
    CHECK(sizeof(ChartColumn) == 28u, "ChartColumn = %u",
          (unsigned)sizeof(ChartColumn));
    CHECK(HAUS_CHART_COLUMNS * sizeof(ChartColumn) == 12320u,
          "chart bytes = %u",
          (unsigned)(HAUS_CHART_COLUMNS * sizeof(ChartColumn)));

    const uint32_t start = 1728864000u; /* 2024-10-14 00:00 UTC */

    prepare_day(start);
    test_samples(start);
    test_rollups(start);

    if (g_failures != 0u) {
        printf("%u/%u checks FAILED\n", (unsigned)g_failures,
               (unsigned)g_checks);
        return 1;
    }
    printf("%u checks passed\n", (unsigned)g_checks);
    return 0;
}
