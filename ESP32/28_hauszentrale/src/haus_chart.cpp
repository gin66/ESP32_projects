/**
 * @file haus_chart.cpp
 * @brief Streaming downsample into 440 ChartColumn envelopes.
 *
 * Chunk buffers are static: a 512-sample chunk (12 KB) and a 64-column rollup
 * chunk (5.6 KB). No heap is used here.
 */

#include "haus_chart.h"

#include <string.h>

#include "haus_mem.h"

/* Scratch is CPU-only; keep it out of internal .bss (S28.20). */
static gui_sample_t* s_sample_chunk = NULL;
static RollupColumn* s_rollup_chunk = NULL;

static bool ensure_chunks(void) {
  if (!s_sample_chunk)
    s_sample_chunk = (gui_sample_t*)haus_mem_alloc(sizeof(gui_sample_t) *
                                                   HAUS_CHART_CHUNK_SAMPLES);
  if (!s_rollup_chunk)
    s_rollup_chunk = (RollupColumn*)haus_mem_alloc(sizeof(RollupColumn) *
                                                   HAUS_CHART_CHUNK_ROLLUP);
  return s_sample_chunk != NULL && s_rollup_chunk != NULL;
}

static void reset_columns(ChartColumn* cols, uint32_t t0, uint32_t t1) {
  const uint32_t span = t1 > t0 ? t1 - t0 : 1u;
  for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) {
    memset(&cols[i], 0, sizeof(cols[i]));
    cols[i].t = t0 + (uint32_t)((uint64_t)i * span / HAUS_CHART_COLUMNS);
  }
}

static size_t column_for(uint32_t t, uint32_t t0, uint32_t t1) {
  const uint32_t span = t1 > t0 ? t1 - t0 : 1u;
  if (t < t0) t = t0;
  if (t > t1) t = t1;
  size_t col = (size_t)((uint64_t)(t - t0) * HAUS_CHART_COLUMNS / span);
  if (col >= HAUS_CHART_COLUMNS) col = HAUS_CHART_COLUMNS - 1;
  return col;
}

static bool sample_value(const gui_sample_t* s, int series, int32_t* value) {
  if (!s || !value) return false;

  switch (series) {
    case 0:
      if (s->stale_mask & SAMPLE_STALE_CHAMBER) return false;
      if (s->temp[0] == HAUS_CAN_TEMP_EMPTY || s->temp[0] == GUI_EMPTY)
        return false;
      *value = s->temp[0];
      return true;
    case 1:
      if (s->stale_mask & SAMPLE_STALE_WATER_IN) return false;
      if (s->temp[1] == HAUS_CAN_TEMP_EMPTY || s->temp[1] == GUI_EMPTY)
        return false;
      *value = s->temp[1];
      return true;
    case 2:
      if (s->stale_mask & SAMPLE_STALE_WATER_OUT) return false;
      if (s->temp[2] == HAUS_CAN_TEMP_EMPTY || s->temp[2] == GUI_EMPTY)
        return false;
      *value = s->temp[2];
      return true;
    case 3:
      if (!(s->flags & SAMPLE_FLAG_CLIMATE_AHT20)) return false;
      if (s->stale_mask & SAMPLE_STALE_LIVING) return false;
      if (s->living_c100 == HAUS_CAN_TEMP_EMPTY || s->living_c100 == GUI_EMPTY)
        return false;
      *value = s->living_c100;
      return true;
    case 4:
      if (!(s->flags & SAMPLE_FLAG_CLIMATE_AHT20)) return false;
      if (s->stale_mask & SAMPLE_STALE_HUMIDITY) return false;
      *value = (int32_t)s->hum_c100;
      return true;
    case 5:
      if (!(s->flags & SAMPLE_FLAG_CLIMATE_BMP280)) return false;
      if (s->stale_mask & SAMPLE_STALE_PRESSURE) return false;
      *value = (int32_t)s->pressure_pa;
      return true;
    case GUI_SERIES_TARGET:
      if (s->target_c100 == HAUS_CAN_TEMP_EMPTY || s->target_c100 == GUI_EMPTY)
        return false;
      *value = s->target_c100;
      return true;
    case 7:
      if (s->stale_mask & SAMPLE_STALE_POWER) return false;
      if (s->flags & SAMPLE_FLAG_GAP) return false;
      *value = (int32_t)s->power_dw; /* deciwatts (0.1 W) */
      return true;
    case GUI_SERIES_AC_RMS:
      if (s->stale_mask & SAMPLE_STALE_AC) return false;
      *value = (int32_t)s->ac_rms_dc; /* 0.1 ADC count */
      return true;
    case GUI_SERIES_AC_ANGLE:
      if (s->stale_mask & SAMPLE_STALE_AC) return false;
      *value = (int32_t)s->ac_angle_cdeg; /* 0.01 degree */
      return true;
    default:
      return false;
  }
}

static int16_t clamp16(int32_t value) {
  if (value > INT16_MAX) return INT16_MAX;
  if (value < INT16_MIN) return INT16_MIN;
  return (int16_t)value;
}

static void add_value(ChartColumn* col, int slot, int32_t value,
                      uint32_t epoch, uint32_t* newest) {
  const int16_t v = clamp16(value);
  if (col->n[slot] == 0) {
    col->mn[slot] = v;
    col->mx[slot] = v;
  } else {
    if (v < col->mn[slot]) col->mn[slot] = v;
    if (v > col->mx[slot]) col->mx[slot] = v;
  }
  col->sum[slot] += value;
  col->n[slot]++;
  if (epoch > *newest) *newest = epoch;
}

static void add_rollup(ChartColumn* col, int slot, const RollupColumn* r,
                       int src, uint32_t epoch, uint32_t* newest) {
  if (r->n[src] == 0) return;
  const int16_t mn = clamp16(r->mn[src]);
  const int16_t mx = clamp16(r->mx[src]);
  if (col->n[slot] == 0) {
    col->mn[slot] = mn;
    col->mx[slot] = mx;
  } else {
    if (mn < col->mn[slot]) col->mn[slot] = mn;
    if (mx > col->mx[slot]) col->mx[slot] = mx;
  }
  col->sum[slot] += r->sum[src];
  col->n[slot] += r->n[src];
  if (epoch > *newest) *newest = epoch;
}

static void mark_gaps(ChartColumn* cols, int series_b) {
  for (size_t i = 0; i < HAUS_CHART_COLUMNS; i++) {
    const bool a_empty = cols[i].n[0] == 0;
    const bool b_empty = (series_b < 0) || (cols[i].n[1] == 0);
    if (a_empty && b_empty) cols[i].flags |= CHART_COL_GAP;
  }
}

uint32_t haus_chart_build_samples(ChartColumn* cols, uint32_t t0, uint32_t t1,
                                  haus_chart_sample_reader reader, void* ctx,
                                  size_t total, int series_a, int series_b) {
  if (!cols || !reader) return 0;
  if (!ensure_chunks()) return 0;
  reset_columns(cols, t0, t1);
  uint32_t newest = 0;

  for (size_t first = 0; first < total;) {
    const size_t want = total - first;
    const size_t got = reader(
        ctx, first,
        want < HAUS_CHART_CHUNK_SAMPLES ? want : HAUS_CHART_CHUNK_SAMPLES,
        s_sample_chunk);
    if (got == 0) break;

    for (size_t i = 0; i < got; i++) {
      const gui_sample_t* s = &s_sample_chunk[i];
      const size_t col_idx = column_for(s->epoch, t0, t1);
      ChartColumn* col = &cols[col_idx];

      int32_t value = 0;
      if (series_a >= 0 && sample_value(s, series_a, &value)) {
        add_value(col, 0, value, s->epoch, &newest);
      }
      if (series_b >= 0 && sample_value(s, series_b, &value)) {
        add_value(col, 1, value, s->epoch, &newest);
      }
    }
    first += got;
  }

  mark_gaps(cols, series_b);
  return newest;
}

/* Chart series 7 is power; the rollup stores it as series 6. AC RMS/angle
 * are 8 and 9 in the chart and 7 and 8 in the rollup. Target (6) is built
 * separately and must not resolve to a rollup slot. */
static int rollup_source(int chart_series) {
  if (chart_series == 7) return 6;
  if (chart_series == GUI_SERIES_AC_RMS) return 7;
  if (chart_series == GUI_SERIES_AC_ANGLE) return 8;
  if (chart_series == GUI_SERIES_TARGET) return -1;
  return chart_series;
}

uint32_t haus_chart_build_rollup(ChartColumn* cols, uint32_t t0, uint32_t t1,
                                 haus_chart_rollup_reader reader, void* ctx,
                                 size_t total, int series_a, int series_b) {
  if (!cols || !reader) return 0;
  if (!ensure_chunks()) return 0;
  reset_columns(cols, t0, t1);
  uint32_t newest = 0;

  const int src_a = rollup_source(series_a);
  const int src_b = rollup_source(series_b);

  for (size_t first = 0; first < total;) {
    const size_t want = total - first;
    const size_t got =
        reader(ctx, first,
               want < HAUS_CHART_CHUNK_ROLLUP ? want : HAUS_CHART_CHUNK_ROLLUP,
               s_rollup_chunk);
    if (got == 0) break;

    for (size_t i = 0; i < got; i++) {
      const RollupColumn* r = &s_rollup_chunk[i];
      const uint32_t t = r->t * 60u;
      const size_t col_idx = column_for(t, t0, t1);
      ChartColumn* col = &cols[col_idx];

      if (src_a >= 0) add_rollup(col, 0, r, src_a, t, &newest);
      if (src_b >= 0) add_rollup(col, 1, r, src_b, t, &newest);
    }
    first += got;
  }

  mark_gaps(cols, series_b);
  return newest;
}
