/**
 * @file haus_power_axis.h
 * @brief Power chart scaling: linear or logarithmic with a 0 W special case.
 *
 * All values are decawatts (0.1 W), the unit stored in gui_sample_t.power_dw
 * and RollupColumn series 6. The log axis keeps a small linear band from 0 to
 * 10 W so 0 W can be drawn and read as an ordinary tick instead of log(0).
 * Above 10 W the axis is logarithmic.
 */

#ifndef HAUS_POWER_AXIS_H
#define HAUS_POWER_AXIS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAUS_POWER_AXIS_TICKS 5

typedef struct {
  bool log;
  int32_t lo;                     /* decawatts */
  int32_t hi;                     /* decawatts */
  int32_t ticks[HAUS_POWER_AXIS_TICKS]; /* bottom (lo) to top (hi) */
} haus_power_axis_t;

/* Build an axis that covers at least [mn, mx]. Log mode always includes 0 and
 * uses the fixed major ticks 0, 10 W, 100 W, 1 kW and the decade above the
 * data. */
void haus_power_axis_build(haus_power_axis_t* axis, int32_t mn, int32_t mx,
                           bool log);

/* Map a value to 0 (lo) … 1 (hi). Values outside the axis are clamped. */
double haus_power_axis_fraction(const haus_power_axis_t* axis, int32_t value);

/* Inverse of haus_power_axis_fraction(). */
int32_t haus_power_axis_value(const haus_power_axis_t* axis, double fraction);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_POWER_AXIS_H */
