/**
 * @file adc_monitor.h
 * @brief Peak-to-peak tracking of A0 across a fixed measurement window.
 *
 * The input is the secondary of a current-measurement coil carrying 50 Hz AC.
 * What matters about that signal is that it is an *extremum* measurement: the
 * quantity of interest is the envelope, so the tracker keeps the running minimum
 * and maximum rather than an average. An average would report the mean of a sine
 * and throw away precisely the part that is worth measuring.
 *
 * One window is one second, i.e. 50 whole line periods. That makes ADmax/ADmin
 * the true envelope extremes. Note the window does not have to land on a cycle
 * boundary to be valid — min and max are per-cycle stationary statistics, so any
 * window appreciably longer than one 20 ms period already contains complete
 * peaks and the result is immune to where the edges fall. Fifty periods is what
 * buys confidence that no single sampling dropout moved the extreme.
 *
 * The ADC is left free-running and this module never blocks on a conversion.
 * See the register comments in the .cpp for why that matters to the PWM output.
 */

#ifndef ADC_MONITOR_H
#define ADC_MONITOR_H

#include <Arduino.h>
#include "nrf_payload.h"

// One report window. Matches neither the temperature cycle nor the PWM cadence;
// the ADC frame has its own clock and is simply told apart by its magic.
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
// last finished conversion and repeats are harmless to all three statistics (a
// value seen twice moves the mean and the count by the same amount, and cannot
// change min or max at all).
void adc_monitor_tick();

// True once the open window has reached ADC_WINDOW_MS.
bool adc_monitor_due();

// Close the open window into *out and start a new one. Returns false if the
// window held no readings at all — publishing zeroes for that would look
// identical to a genuine short-circuit reading, which is the one thing this
// module exists to avoid being confused with.
bool adc_monitor_take(AdcStats* out);

#endif // ADC_MONITOR_H