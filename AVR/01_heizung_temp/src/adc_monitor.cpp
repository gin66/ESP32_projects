#include <Arduino.h>
#include "adc_monitor.h"
#include "adc_wave.h"

// ─── Channel ─────────────────────────────────────────────────────────────────
// A0 is ADC0 on the ATmega328p. MUX is programmed once in begin() rather than
// left to analogRead(), so nothing that calls analogRead() elsewhere can quietly
// move the converter to another channel.
#define ADC_CHANNEL 0

// Full-scale count. A conversion reading this has pinned against AREF.
#define ADC_FULL_SCALE 1023

static AdcStats     win;
static AdcBins      wave;
static AdcWaveClock wave_clk;
static unsigned long windowStartMs;
static bool        priming;    // discard the first conversion after REFS changes

// ─── Reference: internal 1.1 V bandgap ───────────────────────────────────────
// REFS1:0 = 11 is "Internal 1.1V voltage reference with external capacitor at
// AREF pin" per the ATmega328P datasheet (Section 23, ADMUX, Table 23-3). One
// LSB is then 1.100 V / 1024 = 1074.22 uV.
//
// The 2.56 V figure that circulates for this part belongs to the older classic
// megaAVR family (ATmega8/16/32); their ADMUX table really does say 2.56 V. The
// ATmega328P register description says 1.1 V, and so does the ADC block diagram
// in Microchip's ATmega328P errata/clarification DS80001060. Taking the classic
// megaAVR text for this part is an easy and silent mistake.
//
// The data sheet's restriction on this setting is the one that matters here, and
// it is absolute: "The internal voltage reference options may not be used if an
// external reference voltage is being applied to the AREF pin." Nothing may drive
// AREF while this is selected — that would tie a generated internal source
// against an external one. AREF must carry only the datasheet's bypass capacitor
// to ground, with no external supply on it.
//
// MUX is written outright rather than preserved: this module owns the channel
// selection, and building ADMUX from scratch also leaves ADLAR clear, which is
// what makes ADCW read as ADC9..ADC0 instead of left-aligned.

void adc_monitor_begin()
{
    // REFS1:REFS0 = 11 -> internal 1.1 V. Nothing may drive AREF in this mode.
    ADMUX = (uint8_t)(_BV(REFS1) | _BV(REFS0) | (ADC_CHANNEL & 0x0F));

    // ADPS2:0 = 111 -> /128 -> 125 kHz ADC clock -> 13 cycles -> 104 us per
    // conversion, i.e. 9615 samples/s. /128 is the slowest clock that still
    // settles a high-impedance source within those 13 cycles, so it is the
    // accuracy-optimal choice; a faster clock buys sample rate this signal does
    // not need (50 Hz needs 100/s for Nyquist, and we want oversampling so a
    // peak is not missed between reads).
    //
    // ADATE free-runs the converter: each completed conversion immediately
    // starts the next, so the sampling grid is locked to the crystal and never
    // rides on how long a loop pass happened to take. One write of ADSC starts
    // the first conversion and nothing needs writing after that.
    //
    // ADIE is deliberately left off. An interrupt per conversion would be
    // 9615 ISRs/s landing on a 1 kHz PWM whose documented duty resolution is
    // 1 us, and ISR entry latency is exactly the few-microsecond jitter that
    // distorts the edge timing. Polling costs one load and no jitter at all.
    ADCSRA = (uint8_t)((ADCSRA & ~(_BV(ADPS0) | _BV(ADPS1) | _BV(ADPS2)))
                       | (_BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0))
                       | _BV(ADEN) | _BV(ADATE));

    win.max      = 0;
    win.min      = 0xFFFF;     // inverse extremes, so the first reading always lands
    win.pp       = 0;
    win.sum      = 0;
    win.readings = 0;
    win.flags    = 0;

    windowStartMs = millis();
    priming       = true;
    adc_wave_bins_clear(&wave);
    adc_wave_clock_reset(&wave_clk, (uint32_t)micros());

    ADCSRA |= _BV(ADSC);
}

// ─── Fold one reading into the open window ───────────────────────────────────
static void accumulate(uint16_t v)
{
    // The first conversion after REFS changes is not trustworthy — the sample
    // capacitor has not settled against the new reference yet. Arduino's own
    // analogRead() throws one away for the same reason.
    if (priming) { priming = false; return; }

    if (v > win.max) win.max = v;
    if (v < win.min) win.min = v;

    win.sum      += v;
    win.readings++;

    if (v >= ADC_FULL_SCALE) win.flags |= ADC_FLAG_RAILED;

    // Same kept samples as min/max. The slot comes from the quartz phase, so a
    // slow pass drops samples and does not slide the 20 ms waveform.
    adc_wave_add(&wave, &wave_clk, v, (uint32_t)micros());
}

void adc_monitor_tick()
{
    // A single unguarded 16-bit read. ADCW is the contiguous ADCL/ADCH pair and
    // reads low byte first, which is the order the datasheet requires; and the
    // result register is not written until a conversion *finishes*, so reading
    // mid-conversion yields the previous complete result rather than a torn one.
    // That is what makes this safe to call with no status polling at all.
    accumulate(ADCW);
}

bool adc_monitor_due()
{
    return (unsigned long)(millis() - windowStartMs) >= ADC_WINDOW_MS;
}

bool adc_monitor_take(AdcStats* out, uint16_t bins[ADC_WAVE_BINS])
{
    if (out == nullptr || bins == nullptr) return false;

    *out = win;

    // The waveform is a running average, so a report is a snapshot and the
    // averages keep smoothing instead of being cleared here.
    adc_wave_snapshot(&wave, bins);

    win.max      = 0;
    win.min      = 0xFFFF;
    win.pp       = 0;
    win.sum      = 0;
    win.readings = 0;
    win.flags    = 0;

    windowStartMs = millis();

    return out->readings > 0;
}