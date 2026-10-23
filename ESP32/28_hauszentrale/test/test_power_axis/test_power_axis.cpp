/**
 * @file test_power_axis.cpp
 * @brief Host tests for the power chart scaling.
 *
 * A  the log axis keeps 0 W as an ordinary tick
 * B  the log major ticks are 0, 10 W, 100 W, 1 kW and the top decade
 * C  fraction() and value() are inverses
 * D  linear scaling still spans [lo, hi]
 * E  values outside the axis are clamped
 */

#include <cmath>
#include <cstdio>

#include "haus_power_axis.h"

static int g_checks;
static int g_failures;

#define CHECK(cond, ...)                                      \
  do {                                                        \
    g_checks++;                                               \
    if (!(cond)) {                                            \
      g_failures++;                                           \
      std::printf("  FAIL %s:%d: ", __FILE__, __LINE__);      \
      std::printf(__VA_ARGS__);                               \
      std::printf("\n");                                      \
    }                                                         \
  } while (0)

static bool near(double a, double b, double eps) {
  return std::fabs(a - b) <= eps;
}

int main() {
  haus_power_axis_t a;
  haus_power_axis_build(&a, 0, 20000, true); /* 0 … 2000 W */

  CHECK(a.log, "axis should be logarithmic");
  CHECK(a.lo == 0, "log axis starts at 0 W");
  CHECK(a.hi >= 20000, "axis covers the data");
  CHECK(a.ticks[0] == 0, "0 W tick");
  CHECK(a.ticks[1] == 100, "10 W tick");
  CHECK(a.ticks[2] == 1000, "100 W tick");
  CHECK(a.ticks[3] == 10000, "1 kW tick");
  CHECK(a.ticks[4] == a.hi, "top tick matches hi");

  /* 0 W is a real point, not -inf. */
  CHECK(near(haus_power_axis_fraction(&a, 0), 0.0, 1e-9), "0 W -> 0");
  /* The 0…10 W band is linear and occupies the bottom fifth. */
  CHECK(near(haus_power_axis_fraction(&a, 50), 0.1, 1e-9),
        "5 W -> 0.10");
  CHECK(near(haus_power_axis_fraction(&a, 100), 0.2, 1e-9),
        "10 W -> 0.20");
  CHECK(near(haus_power_axis_fraction(&a, 50000), 1.0, 1e-9),
        "top -> 1.0");

  /* Log decade spacing: 100 W and 1 kW are equal factors from 10 W. */
  const double f100 = haus_power_axis_fraction(&a, 1000);
  const double f1k = haus_power_axis_fraction(&a, 10000);
  CHECK(f100 > 0.45 && f100 < 0.55, "100 W about mid-band (%.3f)", f100);
  CHECK(f1k > f100 && f1k < 1.0, "1 kW above 100 W and below top");

  /* Inverse round-trip on the tick values. */
  for (int i = 0; i < HAUS_POWER_AXIS_TICKS; i++) {
    const double f = haus_power_axis_fraction(&a, a.ticks[i]);
    const int32_t v = haus_power_axis_value(&a, f);
    CHECK(near((double)v, (double)a.ticks[i], 1.0),
          "round-trip %d -> %.3f -> %d", a.ticks[i], f, v);
  }

  haus_power_axis_t lin;
  haus_power_axis_build(&lin, 0, 20000, false);
  CHECK(near(haus_power_axis_fraction(&lin, 0), 0.0, 1e-9), "linear 0");
  CHECK(near(haus_power_axis_fraction(&lin, 10000), 0.5, 1e-9),
        "linear mid");
  CHECK(near(haus_power_axis_fraction(&lin, 20000), 1.0, 1e-9), "linear top");
  CHECK(lin.ticks[0] == 0 && lin.ticks[4] == 20000, "linear ticks");

  CHECK(near(haus_power_axis_fraction(&a, -500), 0.0, 1e-9),
        "negative clamps to 0");
  CHECK(near(haus_power_axis_fraction(&a, 999999), 1.0, 1e-9),
        "above top clamps to 1");

  std::printf("test_power_axis: %d checks, %d failures\n", g_checks,
              g_failures);
  return g_failures == 0 ? 0 : 1;
}
