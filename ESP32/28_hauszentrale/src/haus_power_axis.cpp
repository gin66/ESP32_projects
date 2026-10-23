/**
 * @file haus_power_axis.cpp
 * @brief Power chart scaling (linear or log with a 0 W linear band).
 */

#include "haus_power_axis.h"

#include <math.h>

/* Decawatts. 100 dw = 10 W. The log curve starts above this value; below it
 * the axis is linear so 0 W has a normal position and small standby draws
 * remain distinguishable. */
static const int32_t kLogLinearDw = 100;

static double log_fraction(const haus_power_axis_t* axis, double value) {
  const double top = (double)axis->hi;
  if (top <= (double)kLogLinearDw) {
    return top > 0.0 ? value / top : 0.0;
  }
  if (value <= (double)kLogLinearDw) {
    return value <= 0.0 ? 0.0 : (value / (double)kLogLinearDw) * 0.2;
  }
  const double span = log10(top / (double)kLogLinearDw);
  if (span <= 0.0) return 0.0;
  return 0.2 + 0.8 * log10(value / (double)kLogLinearDw) / span;
}

void haus_power_axis_build(haus_power_axis_t* axis, int32_t mn, int32_t mx,
                           bool log) {
  if (!axis) return;
  if (mx < mn) {
    const int32_t t = mn;
    mn = mx;
    mx = t;
  }
  axis->log = log;
  if (!log) {
    axis->lo = mn;
    axis->hi = mx;
    for (int i = 0; i < HAUS_POWER_AXIS_TICKS; i++) {
      axis->ticks[i] =
          mn + (int32_t)((int64_t)(mx - mn) * i / (HAUS_POWER_AXIS_TICKS - 1));
    }
    return;
  }

  axis->lo = 0;
  int32_t top = 50000; /* 5 kW */
  while (top < mx && top < 5000000) top *= 2;
  if (top < 50000) top = 50000;
  axis->hi = top;
  axis->ticks[0] = 0;
  axis->ticks[1] = 100;      /* 10 W */
  axis->ticks[2] = 1000;     /* 100 W */
  axis->ticks[3] = 10000;    /* 1 kW */
  axis->ticks[4] = top;
}

double haus_power_axis_fraction(const haus_power_axis_t* axis, int32_t value) {
  if (!axis || axis->hi <= axis->lo) return 0.0;
  double v = (double)value;
  if (v < (double)axis->lo) v = (double)axis->lo;
  if (v > (double)axis->hi) v = (double)axis->hi;
  double f;
  if (axis->log) {
    f = log_fraction(axis, v);
  } else {
    f = (v - (double)axis->lo) / ((double)axis->hi - (double)axis->lo);
  }
  if (f < 0.0) f = 0.0;
  if (f > 1.0) f = 1.0;
  return f;
}

int32_t haus_power_axis_value(const haus_power_axis_t* axis, double fraction) {
  if (!axis || axis->hi <= axis->lo) return axis ? axis->lo : 0;
  if (fraction < 0.0) fraction = 0.0;
  if (fraction > 1.0) fraction = 1.0;
  if (!axis->log) {
    return (int32_t)lround((double)axis->lo +
                           fraction * ((double)axis->hi - (double)axis->lo));
  }
  const double top = (double)axis->hi;
  if (top <= (double)kLogLinearDw) {
    return (int32_t)lround(fraction * top);
  }
  if (fraction <= 0.2) {
    return (int32_t)lround(fraction / 0.2 * (double)kLogLinearDw);
  }
  const double span = log10(top / (double)kLogLinearDw);
  return (int32_t)lround((double)kLogLinearDw *
                         pow(10.0, (fraction - 0.2) / 0.8 * span));
}
