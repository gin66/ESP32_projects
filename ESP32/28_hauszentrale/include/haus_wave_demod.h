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

#ifdef __cplusplus
}
#endif

#endif /* HAUS_WAVE_DEMOD_H */
