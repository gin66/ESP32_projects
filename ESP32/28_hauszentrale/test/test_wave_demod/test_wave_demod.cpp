/**
 * @file test_wave_demod.cpp
 * @brief PC-only tests for the 0x2C waveform demodulator (S29.01).
 *
 * A  known sine gives the expected RMS and angle
 * B  a DC offset does not change the AC result
 * C  a flat window yields zero RMS and zero angle
 * D  captured bench waveforms decode to the expected effective value
 *
 * The captured waves are 20 u16 slots in units of 1/64 ADC count, exactly as
 * received in 0x2C1..0x2C5. The last set also had ADC min/max 337..810,
 * pp=473 and 2779 readings in the 1.1 V range; those min/max values are not
 * used here.
 */

#include "haus_wave_demod.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint32_t g_checks;
static uint32_t g_failures;

#define CHECK(cond, ...)                                                      \
  do {                                                                        \
    g_checks++;                                                               \
    if (!(cond)) {                                                            \
      g_failures++;                                                           \
      printf("  FAIL %s:%d: ", __FILE__, __LINE__);                           \
      printf(__VA_ARGS__);                                                    \
      printf("\n");                                                           \
    }                                                                         \
  } while (0)

#define SECTION(name) printf("-- %s\n", name)

static void check_near(float actual, float expected, float tol,
                       const char* what, int index) {
  CHECK(fabsf(actual - expected) <= tol, "%s[%d]: got %.3f expected %.3f",
        what, index, (double)actual, (double)expected);
}

static float wrap_deg(float angle) {
  while (angle > 180.0f) angle -= 360.0f;
  while (angle <= -180.0f) angle += 360.0f;
  return angle;
}

static void check_angle(float actual, float expected, float tol, int index) {
  CHECK(fabsf(wrap_deg(actual - expected)) <= tol,
        "angle[%d]: got %.3f expected %.3f", index, (double)actual,
        (double)expected);
}

static void fill_sine(uint16_t slot[HAUS_WAVE_BINS], float mean, float amp,
                      float phase_deg) {
  const float step = (float)(2.0 * M_PI) / (float)HAUS_WAVE_BINS;
  const float phase = phase_deg * (float)(M_PI / 180.0);
  for (size_t i = 0; i < HAUS_WAVE_BINS; i++) {
    const float v = mean + amp * cosf(step * (float)i + phase);
    long q = lroundf(v * 64.0f);
    if (q < 0) q = 0;
    if (q > 65535) q = 65535;
    slot[i] = (uint16_t)q;
  }
}

static void test_known_sine(void) {
  SECTION("known sine");

  static const float phases[] = {0.0f, 30.0f, 90.0f, 180.0f, -45.0f, 135.0f};
  uint16_t slot[HAUS_WAVE_BINS];
  for (size_t p = 0; p < sizeof(phases) / sizeof(phases[0]); p++) {
    fill_sine(slot, 512.0f, 100.0f, phases[p]);
    haus_wave_demod_t d;
    CHECK(haus_wave_demod(slot, &d), "demod failed");
    check_near(d.mean_counts, 512.0f, 0.02f, "mean", (int)p);
    check_near(d.amplitude_counts, 100.0f, 0.05f, "amplitude", (int)p);
    check_near(d.rms_counts, 70.7107f, 0.05f, "rms", (int)p);
    check_angle(d.angle_deg, phases[p], 0.05f, (int)p);
  }

  /* A sine is a cosine at -90 degrees. */
  fill_sine(slot, 512.0f, 100.0f, 0.0f);
  for (size_t i = 0; i < HAUS_WAVE_BINS; i++) {
    const float v = 512.0f + 100.0f * sinf((float)(2.0 * M_PI) *
                                           (float)i / 20.0f);
    slot[i] = (uint16_t)lroundf(v * 64.0f);
  }
  haus_wave_demod_t d;
  CHECK(haus_wave_demod(slot, &d), "demod failed");
  check_near(d.rms_counts, 70.7107f, 0.05f, "sine rms", 0);
  check_angle(d.angle_deg, -90.0f, 0.05f, 0);
}

static void test_dc_and_flat(void) {
  SECTION("DC offset and flat window");

  uint16_t low[HAUS_WAVE_BINS];
  uint16_t high[HAUS_WAVE_BINS];
  fill_sine(low, 300.0f, 80.0f, 20.0f);
  fill_sine(high, 800.0f, 80.0f, 20.0f);

  haus_wave_demod_t dl;
  haus_wave_demod_t dh;
  CHECK(haus_wave_demod(low, &dl), "demod failed");
  CHECK(haus_wave_demod(high, &dh), "demod failed");
  check_near(dl.rms_counts, dh.rms_counts, 0.01f, "dc-invariant rms", 0);
  check_angle(dl.angle_deg, dh.angle_deg, 0.01f, 0);

  uint16_t flat[HAUS_WAVE_BINS];
  for (size_t i = 0; i < HAUS_WAVE_BINS; i++) flat[i] = 640u; /* 10 counts */
  haus_wave_demod_t df;
  CHECK(haus_wave_demod(flat, &df), "demod failed");
  check_near(df.mean_counts, 10.0f, 0.001f, "flat mean", 0);
  check_near(df.rms_counts, 0.0f, 0.001f, "flat rms", 0);
  check_near(df.angle_deg, 0.0f, 0.001f, "flat angle", 0);
}

static void test_captured_waves(void) {
  SECTION("captured bench waves");

  /* Ten seconds of the live 50 Hz coil, one complete 20 ms window per row. */
  static const uint16_t waves[][HAUS_WAVE_BINS] = {
      {26900, 25230, 24311, 25080, 27217, 30382, 33759, 37306, 41223, 44521,
       47128, 48753, 49604, 48823, 46819, 43669, 40058, 36591, 32805, 29391},
      {27047, 25417, 24390, 24957, 26859, 29929, 33411, 36774, 40723, 44187,
       46856, 48590, 49627, 48993, 47012, 44062, 40480, 37006, 33315, 29761},
      {27290, 25576, 24455, 24842, 26556, 29528, 33090, 36306, 40323, 43843,
       46579, 48408, 49546, 49201, 47326, 44495, 40934, 37568, 33667, 30036},
      {27659, 25739, 24496, 24664, 26336, 29034, 32613, 36117, 39818, 43428,
       46336, 48220, 49408, 49308, 47667, 44781, 41385, 38021, 34152, 30593},
      {27974, 25996, 24683, 24588, 25947, 28626, 32182, 35477, 39303, 42910,
       45984, 48031, 49340, 49464, 47943, 45338, 41761, 38408, 34703, 31021},
      {28343, 26202, 24740, 24458, 25743, 28239, 31555, 35042, 38648, 42444,
       45616, 47765, 49211, 49554, 48168, 45713, 42313, 38956, 35347, 31417},
      {28895, 26406, 24949, 24384, 25476, 27796, 31154, 34583, 38219, 42070,
       45232, 47517, 49089, 49623, 48428, 46192, 42796, 39484, 35871, 31915},
      {29153, 26726, 25153, 24341, 25222, 27370, 30637, 34142, 37586, 41484,
       44805, 47292, 48844, 49611, 48766, 46533, 43316, 39898, 36365, 32553},
      {29663, 27031, 25339, 24353, 24960, 27011, 30069, 33557, 36976, 40846,
       44274, 46926, 48646, 49594, 48968, 46943, 43893, 40345, 37000, 33131},
      {30125, 27455, 25610, 24409, 24752, 26602, 29494, 32954, 36523, 40213,
       43779, 46574, 48401, 49540, 49143, 47278, 44559, 40880, 37725, 33875},
      {30552, 27745, 25716, 24549, 24571, 26328, 29006, 32661, 35902, 39780,
       43445, 46240, 48239, 49556, 49249, 47700, 44908, 41278, 38172, 34169},
      {30749, 27750, 25914, 24547, 24624, 26212, 28987, 32416, 35793, 39573,
       43254, 46179, 48155, 49406, 49370, 47822, 44919, 41610, 38095, 34391},
  };

  printf("  idx    mean     rms    amp   angle\n");
  for (size_t i = 0; i < sizeof(waves) / sizeof(waves[0]); i++) {
    haus_wave_demod_t d;
    CHECK(haus_wave_demod(waves[i], &d), "demod failed at %d", (int)i);
    printf("  %2d  %7.3f %7.3f %7.3f %7.2f\n", (int)i, (double)d.mean_counts,
           (double)d.rms_counts, (double)d.amplitude_counts,
           (double)d.angle_deg);
    check_near(d.mean_counts, 577.9f, 0.5f, "captured mean", (int)i);
    check_near(d.rms_counts, 136.7f, 0.4f, "captured rms", (int)i);
    check_near(d.amplitude_counts, 193.4f, 0.6f, "captured amp", (int)i);
    CHECK(d.angle_deg > 115.0f && d.angle_deg < 150.0f,
          "captured angle[%d]: %.2f outside 115..150", (int)i,
          (double)d.angle_deg);
  }

  /* The phase drifts with the AVR's free-running 20 ms slot clock. Record the
   * direction here so a future mains-locked implementation has a baseline. */
  haus_wave_demod_t first;
  haus_wave_demod_t last;
  haus_wave_demod(waves[0], &first);
  haus_wave_demod(waves[sizeof(waves) / sizeof(waves[0]) - 1], &last);
  CHECK(last.angle_deg < first.angle_deg,
        "captured angle should decrease over the ten seconds");
}

int main(void) {
  printf("haus_wave_demod tests\n");
  test_known_sine();
  test_dc_and_flat();
  test_captured_waves();

  printf("\n%u checks, %u failures\n", (unsigned)g_checks,
         (unsigned)g_failures);
  return g_failures == 0 ? 0 : 1;
}
