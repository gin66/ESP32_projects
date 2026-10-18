#include "adc_wave.h"

#include <string.h>

void adc_wave_clock_reset(AdcWaveClock* c, uint32_t now_us)
{
    if (c == nullptr) return;
    c->last_us = now_us;
    c->sub_us  = 0;
    c->slot    = 0;
}

void adc_wave_bins_clear(AdcBins* b)
{
    if (b == nullptr) return;
    memset(b, 0, sizeof(*b));
}

void adc_wave_add(AdcBins* b, AdcWaveClock* c, uint16_t sample, uint32_t time_us)
{
    uint32_t acc;
    uint8_t  slot;
    uint16_t a;
    uint16_t x;

    if (b == nullptr || c == nullptr) return;

    /* micros() wrap is just an unsigned subtraction of elapsed time. The slot
     * index is advanced one step per whole millisecond, so there is no phase
     * modulo and no absolute clock value anywhere. */
    acc = (uint32_t)c->sub_us + (time_us - c->last_us);
    c->last_us = time_us;

    slot = c->slot;
    while (acc >= ADC_WAVE_BIN_US) {
        acc -= ADC_WAVE_BIN_US;
        if (++slot >= ADC_WAVE_BINS) slot = 0;
    }
    c->sub_us = (uint16_t)acc;
    c->slot   = slot;

    a = b->a[slot];
    x = (uint16_t)(sample << ADC_WAVE_Q_SHIFT);

    /* A slot with no value yet starts at this conversion. A slot that is
     * simply not sampled is left alone, so it carries its last average.
     *
     * Shift the magnitude rather than divide the signed difference: both fit
     * in 16 bits, and this is the same truncation towards zero without a
     * libgcc divide. */
    if (a == 0u) {
        b->a[slot] = x;
    } else if (x >= a) {
        b->a[slot] = (uint16_t)(a + ((x - a) >> ADC_WAVE_EMA_SHIFT));
    } else {
        b->a[slot] = (uint16_t)(a - ((a - x) >> ADC_WAVE_EMA_SHIFT));
    }
}

void adc_wave_snapshot(const AdcBins* b, uint16_t out[ADC_WAVE_BINS])
{
    if (b == nullptr || out == nullptr) return;
    memcpy(out, b->a, sizeof(b->a));
}
