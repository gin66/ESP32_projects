/**
 * @file adc_monitor.h
 * @brief Peak-to-peak tracking of A0 across a fixed measurement window.
 *
 * The input is the secondary of a current-measurement coil carrying 50 Hz AC.
 * What matters about that signal is that it is an *extremum* measurement: the
 * quantity of interest for spike detection is the envelope, so the tracker keeps
 * the running minimum and maximum. The 20 ms shape is a separate set of twenty
 * quartz-timed bins (adc_wave.h), not a replacement for min/max.
 *
 * One window is one second, i.e. 50 line periods. That makes ADmax/ADmin the
 * true envelope extremes. Note the window does not have to land on a cycle
 * boundary to be valid — min and max are per-cycle stationary statistics, so any
 * window appreciably longer than one 20 ms period already contains complete
 * peaks and the result is immune to where the edges fall. Fifty periods is what
 * buys confidence that no single sampling dropout moved the extreme.
 *
 * The 20 ms waveform is a separate set of twenty running averages (adc_wave.h).
 * It needs no window: the pump current is steady, so each slot's average is a
 * snapshot at any time and the report cadence is free to be whatever is
 * convenient.
 *
 * The ADC is left free-running and this module never blocks on a conversion.
 * See the register comments in the .cpp for why that matters to the PWM output.
 */

#ifndef ADC_MONITOR_H
#define ADC_MONITOR_H

#include <Arduino.h>
#include "nrf_payload.h"
#include "adc_wave.h"

// One report window. The waveform is a running average and does not depend on
// this; it only sets the spike frame cadence.
static const unsigned long ADC_WINDOW_MS = 1000UL;

struct AdcStats {
    uint16_t max;        // largest conversion seen in the window
    uint16_t min;        // smallest conversion seen in the window
    uint16_t pp;         // max - min
    uint32_t sum;        // running total, for the serial log's mean only
    uint32_t readings;   // readings folded in (repeats included, see below)
    uint8_t  flags;      // ADC_FLAG_* as accumulated over the window
};

// Configure REFS/MUX/prescaler, start the ADC free-running and open a window.
// Discard the first conversion, which is not trustworthy right after the
// reference changes.
void adc_monitor_begin();

// Fold the most recent conversion into the open window. Call from every loop
// pass — the more often this runs the better, because it only ever reads the
// last finished conversion. A repeat leaves the mean unchanged (value and count
// move together) and cannot change min or max. Waveform repeats are averaged
// into their 1 ms slot along with the fresh conversions.
void adc_monitor_tick();

// True once the open window has reached ADC_WINDOW_MS.
bool adc_monitor_due();

// Close the min/max window into *out and snapshot the running waveform
// averages into bins[20]. The averages are not reset, so they keep smoothing.
// Returns false if the window held no readings at all.
bool adc_monitor_take(AdcStats* out, uint16_t bins[ADC_WAVE_BINS]);

#endif // ADC_MONITOR_H