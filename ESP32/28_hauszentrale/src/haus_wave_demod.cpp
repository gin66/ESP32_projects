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
