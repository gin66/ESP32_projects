/**
 * @file adc_wave.h
 * @brief Twenty 1 ms bins of the pump-current coil with a running average each.
 *
 * The ADC is 10-bit, so each conversion is shifted left by six before it enters
 * the average. That puts the value in a 16-bit accumulator with six fractional
 * bits (units of 1/64 ADC count) and uses the full range of the wire format.
 *
 * A slot's value is a simple exponential average. The first conversion in an
 * empty slot seeds it; later ones move it by one 32nd of the difference. The
 * implementation shifts the magnitude instead of dividing a signed difference,
 * so no libgcc divide is linked:
 *
 *   a = ad << 6                              (a was 0, i.e. no value yet)
 *   d = |(ad << 6) - a|
 *   a += d >> ADC_WAVE_EMA_SHIFT             (sign taken from the difference)
 *
 * a stays a 16-bit value in units of 1/64 ADC count. Shift right by 6 (with
 * rounding) for counts.
 *
 * The loop folds in roughly 150 samples per slot per second (about three per
 * 20 ms period). With a divisor of 32 that is ~64 effective samples, a time
 * constant of about 0.2 s, and a 99 % response in about 1 s, which matches the
 * report cadence. A smaller divisor is noisier than the available sample rate
 * can support; a larger one lags a real current change past the report.
 *
 * No count and no period bookkeeping is needed. A slot that gets no conversion
 * for a while is simply not updated and keeps its last value. The report is a
 * snapshot of the twenty running averages, so it does not have to be taken on a
 * period boundary or at any particular interval.
 *
 * The slot index is the sample's position inside the 20 ms cycle. It wraps
 * 0..19 and advances once per 1000 us; a sub-millisecond remainder carries the
 * rest. That index plus the remainder is the only timebase, and it is advanced
 * from the micros() delta between calls, never from an absolute millis()/20 or
 * a micros() modulo.
 */

#ifndef ADC_WAVE_H
#define ADC_WAVE_H

#include <stdint.h>

#define ADC_WAVE_BINS      20u
#define ADC_WAVE_CYCLE_US  20000u
#define ADC_WAVE_BIN_US    1000u
#define ADC_WAVE_FRAMES    5u
#define ADC_WAVE_Q_SHIFT   6u   /* 10-bit ADC -> 16-bit Q6 */
/* Power of two: the averager shifts the magnitude instead of dividing, so no
 * libgcc divide is linked. 32 is ~64 effective samples per slot. */
#define ADC_WAVE_EMA_SHIFT 5u
#define ADC_WAVE_EMA_DIV   (1u << ADC_WAVE_EMA_SHIFT)

struct AdcBins {
    uint16_t a[ADC_WAVE_BINS]; /* running average per slot, units of 1/64 count */
};

struct AdcWaveClock {
    uint32_t last_us; /* micros() at the previous sample */
    uint16_t sub_us;  /* 0..999, time since the slot last advanced */
    uint8_t  slot;    /* 0..ADC_WAVE_BINS-1, advances once per millisecond */
};

/* Seed last_us with micros() so the first delta is the time since boot, not a
 * whole uptime. */
void adc_wave_clock_reset(AdcWaveClock* c, uint32_t now_us);

/* Clear the running averages to "no value yet". The phase is not touched. */
void adc_wave_bins_clear(AdcBins* b);

/* Fold one sample. time_us is micros() at the read. */
void adc_wave_add(AdcBins* b, AdcWaveClock* c, uint16_t sample, uint32_t time_us);

/* Copy the current running averages. Does not reset them. */
void adc_wave_snapshot(const AdcBins* b, uint16_t out[ADC_WAVE_BINS]);

#endif
