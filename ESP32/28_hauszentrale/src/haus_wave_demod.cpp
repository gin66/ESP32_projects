/**
 * @file haus_wave_demod.cpp
 * @brief Sine/cosine demodulation of the 20-bin ADC waveform (S29.01).
 *
 * x[n] = mean + A*cos(2*pi*n/20 + phi)
 *
 * I = sum (x[n] - mean) * cos(2*pi*n/20)
 * Q = sum (x[n] - mean) * sin(2*pi*n/20)
 *
 * For a whole number of cycles, I = (N/2)*A*cos(phi) and
 * Q = -(N/2)*A*sin(phi), hence phi = atan2(-Q, I). The peak amplitude is
 * 2*hypot(I,Q)/N and the effective value is amplitude/sqrt(2).
 */

#include "haus_wave_demod.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool haus_wave_demod(const uint16_t slot[HAUS_WAVE_BINS],
                     haus_wave_demod_t* out) {
  if (!slot || !out) return false;

  uint32_t sum = 0;
  for (size_t i = 0; i < HAUS_WAVE_BINS; i++) sum += slot[i];
  const float n = (float)HAUS_WAVE_BINS;
  const float mean = (float)sum / (64.0f * n);

  float i_acc = 0.0f;
  float q_acc = 0.0f;
  const float step = (float)(2.0 * M_PI) / n;
  for (size_t k = 0; k < HAUS_WAVE_BINS; k++) {
    const float v = (float)slot[k] / 64.0f - mean;
    const float phase = step * (float)k;
    i_acc += v * cosf(phase);
    q_acc += v * sinf(phase);
  }

  const float amplitude = (2.0f / n) * hypotf(i_acc, q_acc);
  const float rms = amplitude / sqrtf(2.0f);
  const float angle =
      amplitude > 1e-6f ? atan2f(-q_acc, i_acc) * (float)(180.0 / M_PI) : 0.0f;

  out->mean_counts = mean;
  out->rms_counts = rms;
  out->amplitude_counts = amplitude;
  out->angle_deg = angle;
  return true;
}

float haus_wave_angle_step(float prev_deg, float now_deg) {
  float delta = now_deg - prev_deg;
  while (delta <= -180.0f) delta += 360.0f;
  while (delta > 180.0f) delta -= 360.0f;
  return delta;
}

void haus_angle_rate_reset(haus_angle_rate_t* track) {
  if (!track) return;
  memset(track, 0, sizeof(*track));
}

bool haus_angle_rate_observe(haus_angle_rate_t* track, float angle_deg,
                             float rms_counts, uint32_t t_ms) {
  if (!track) return false;
  /* A flat window forces angle 0. Leaving the reference alone keeps the next
   * real cycle from looking like a jump to or from that zero. */
  if (!(rms_counts >= HAUS_ANGLE_RATE_MIN_RMS)) return false;
  if (!track->have_angle) {
    track->have_angle = true;
    track->angle_deg = angle_deg;
    track->t_ms = t_ms;
    return false;
  }

  const uint32_t dt = t_ms - track->t_ms;
  if (dt < HAUS_ANGLE_RATE_MIN_DT_MS) return false;
  if (dt > HAUS_ANGLE_RATE_MAX_DT_MS) {
    track->angle_deg = angle_deg;
    track->t_ms = t_ms;
    return false;
  }

  haus_angle_step_t* step = &track->step[track->next];
  step->ddeg = haus_wave_angle_step(track->angle_deg, angle_deg);
  step->dt_ms = dt;
  step->t_ms = t_ms;
  track->next = (uint8_t)((track->next + 1u) % HAUS_ANGLE_RATE_STEPS);
  if (track->count < (uint8_t)HAUS_ANGLE_RATE_STEPS) {
    track->count = (uint8_t)(track->count + 1u);
  }
  track->angle_deg = angle_deg;
  track->t_ms = t_ms;
  return true;
}

bool haus_angle_rate_cdeg_s(const haus_angle_rate_t* track, uint32_t now_ms,
                            uint32_t window_ms, int16_t* cdeg_s) {
  if (!track || !cdeg_s || track->count == 0) return false;
  const uint32_t window =
      window_ms == 0u ? (uint32_t)HAUS_ANGLE_RATE_WINDOW_MS : window_ms;
  double sum_deg = 0.0;
  uint32_t sum_ms = 0;
  for (uint8_t i = 0; i < track->count; i++) {
    const haus_angle_step_t* step = &track->step[i];
    const uint32_t age = now_ms - step->t_ms;
    if (age > window) continue;
    sum_deg += (double)step->ddeg;
    sum_ms += step->dt_ms;
  }
  if (sum_ms == 0u) return false;
  const double per_s = sum_deg * 1000.0 / (double)sum_ms;
  long rounded = lround(per_s * 100.0);
  if (rounded > 32767L) rounded = 32767L;
  if (rounded < -32767L) rounded = -32767L;
  *cdeg_s = (int16_t)rounded;
  return true;
}
