/**
 * @file haus_wave_demod.h
 * @brief Demodulate the 20 ms / 20-bin ADC waveform from 0x2C1..0x2C5.
 *
 * Pure host/device code: no Arduino or CAN dependency. The input is the
 * 20-slot running average published by the AVR in units of 1/64 ADC count.
 * One slot is one millisecond, so the window spans one 50 Hz mains cycle.
 *
 * The result is the AC effective (RMS) value and the phase angle of the
 * demodulated vector relative to slot 0. Slot 0 is the AVR's 20 ms quartz
 * cycle, not a mains zero crossing, so the angle is a relative phase and can
 * drift; see docs/todos.md S29.01.
 */

#ifndef HAUS_WAVE_DEMOD_H
#define HAUS_WAVE_DEMOD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAUS_WAVE_BINS 20u
#define HAUS_WAVE_MAINS_HZ 50.0f

typedef struct {
  float mean_counts;      /* DC offset, in ADC counts */
  float rms_counts;       /* AC effective value, in ADC counts */
  float amplitude_counts; /* peak amplitude of the fundamental, in counts */
  float angle_deg;        /* phase of the fundamental relative to slot 0 */
} haus_wave_demod_t;

/* Returns false only for a null pointer. A flat or all-zero window is valid
 * and yields zero RMS and zero angle. */
bool haus_wave_demod(const uint16_t slot[HAUS_WAVE_BINS],
                     haus_wave_demod_t* out);

/* Signed step from prev_deg to now_deg, wrapped into (−180, 180]. */
float haus_wave_angle_step(float prev_deg, float now_deg);

/* In-RAM reduction of successive waveforms. The minute file stores one rate;
 * these steps are not persisted. A flat window (RMS below 1 count) is ignored
 * so a forced angle of 0 cannot invent a jump. */
#define HAUS_ANGLE_RATE_STEPS 64u
#define HAUS_ANGLE_RATE_MIN_DT_MS 200u
#define HAUS_ANGLE_RATE_MAX_DT_MS 5000u
#define HAUS_ANGLE_RATE_MIN_RMS 1.0f
#define HAUS_ANGLE_RATE_WINDOW_MS 60000u

typedef struct {
  float ddeg;
  uint32_t dt_ms;
  uint32_t t_ms;
} haus_angle_step_t;

typedef struct {
  bool have_angle;
  float angle_deg;
  uint32_t t_ms;
  uint8_t count;
  uint8_t next;
  haus_angle_step_t step[HAUS_ANGLE_RATE_STEPS];
} haus_angle_rate_t;

void haus_angle_rate_reset(haus_angle_rate_t* track);

/* Record one waveform. Returns true only when a step was stored. */
bool haus_angle_rate_observe(haus_angle_rate_t* track, float angle_deg,
                             float rms_counts, uint32_t t_ms);

/* Mean rate of steps whose time is inside the last window_ms (inclusive).
 * window_ms 0 selects HAUS_ANGLE_RATE_WINDOW_MS. *cdeg_s is 0.01 °/s.
 * Returns false when the window holds no step. */
bool haus_angle_rate_cdeg_s(const haus_angle_rate_t* track, uint32_t now_ms,
                            uint32_t window_ms, int16_t* cdeg_s);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_WAVE_DEMOD_H */
