/**
 * @file pwm_out.h
 * @brief Timer1 software PWM on a free GPIO.
 *
 * The ATmega328p has exactly one 16-bit timer, and both of its hardware PWM
 * outputs are D9/D10 — already occupied by the nRF24 (CSN and CE). Duty
 * resolution beyond 8 bits is therefore only reachable in software.
 *
 * Timer1 is used here purely as a 16-bit counter running fast PWM mode with
 * OC1A's output pin disconnected: the overflow interrupt raises the edge and
 * the compare match lowers it, so the pin is driven by two interrupts per
 * period. Nothing else claims Timer1 — RF24 only ever calls millis() and
 * delayMicroseconds(), which are Timer0 and a calibrated loop.
 */

#ifndef PWM_OUT_H
#define PWM_OUT_H

#include <Arduino.h>

#define PWM_OUT_PIN   7      // PD7 — free; D9/D10 belong to the nRF24
#define PWM_OUT_HZ    1000UL

// Start the output. Duty begins at 0 %.
void pwm_out_begin();

// Set the duty cycle in permille (0…1000).
void pwm_out_set(uint16_t permille);

// True while the counter is generating edges, i.e. 0 < duty < 100 %. At the two
// endpoints both interrupts are off and the port is driven directly instead.
bool pwm_out_active();

// Failover output: a software square wave on the pin, with the timer stopped.
//
// For the case where the Timer1 compare vectors are not running at all. It
// deliberately avoids every Timer1 *compare* mechanism — no compare match, no
// interrupt — and drives the port directly, because the whole premise is that
// the timer path cannot be trusted. The timebase is micros(), which is a polled
// read of the Timer0 counter and not a compare match, so it does not depend on
// the thing that failed.
//
// NOT a blocking loop. An earlier revision spun here forever, which meant a node
// in safe mode was unreachable: no serial, no CAN, no radio, no watchdog report,
// and no way out but a power cycle. On the wire that is indistinguishable from a
// crashed node, which defeats the purpose of a diagnostic state. The waveform is
// advanced by pwm_out_failover_tick() from the main loop instead, so everything
// else keeps running and the fault stays observable.

// Stop the counter entirely and start the failover waveform at `permille`.
void pwm_out_failover_begin(uint16_t permille);

// Advance the waveform by however much time has passed. Non-blocking; call once
// per main-loop pass, as early as possible, because the resolution of the duty is
// the loop's iteration time. Self-corrects if the loop is too slow to hit the
// deadline: the deadline is moved forward rather than allowing a burst of
// catch-up toggles.
void pwm_out_failover_tick(void);

// 1 while the failover waveform is driving the pin.
uint8_t pwm_out_failover_active(void);

/* Counters for the failover waveform.
 *
 * The requested duty is not the delivered duty. The waveform can only change
 * state at a main-loop pass boundary, so the delivered high time is quantised up
 * to the pass time — and the pass time is not constant, because CAN polling and
 * serial reporting share the loop with it. Measured on the bench that turned a
 * requested 20% into about 27%, i.e. roughly 6 V instead of 7 V on the board's
 * own measured curve.
 *
 * These count rising and falling edges so the delivered duty can be *reported*
 * rather than assumed:  duty = rising / (rising + falling). Anything derived from
 * the requested value is fiction.
 */
uint32_t pwm_out_failover_rising(void);
uint32_t pwm_out_failover_falling(void);

// Leave failover and hand the pin back to the timer, re-running pwm_out_begin()'s
// register programming. Used by the bench command that tries to revive a node: if
// the path is genuinely broken the liveness monitor will simply put it back into
// failover, which is the correct and self-limiting outcome.
void pwm_out_failover_end(void);
//
// At 1 kHz one permille is exactly one microsecond of high time, so the duty
// argument is the waveform's high time directly — no conversion, and no way for
// "50%" to silently mean something other than 50%.
//
// Non-blocking. Call begin once, then tick from the main loop.

// Edge counters. They are the proof that Timer1 is counting and that both
// interrupts are actually attached: at 1 kHz each should climb by ~1 per ms
// while modulating. A frozen pair localises the fault to the timer, whereas a
// climbing pair with no voltage means the port write or the wiring.
//
// Read under a brief critical section: a bare 32-bit volatile read is four
// non-atomic instructions on AVR, and a compare interrupt firing mid-read
// returns a torn count that never actually occurred.
uint32_t pwm_out_rising_edges();
uint32_t pwm_out_falling_edges();

// Combined ISR entry count — the liveness signal the main loop's PWM health
// monitor uses. Derived from the two counters above rather than a third one, so
// it costs nothing inside either ISR.
//
// This is what a watchdog fed from the main loop cannot see: the loop can spin
// forever while these stop incrementing, leaving the duty output frozen and the
// watchdog perfectly happy.
uint32_t pwm_out_isr_runs();

// Raw TIMSK1, so a diagnostic can confirm both compare interrupts are still
// enabled rather than being cleared by something else on the way past.
uint8_t pwm_out_timsk1();

// Raw timer registers. These exist so a diagnostic can confirm the timer is
// actually counting and configured as intended, rather than inferring it from
// edge counters alone.
uint8_t  pwm_out_tccr1a();
uint8_t  pwm_out_tccr1b();
uint16_t pwm_out_icr1();
uint16_t pwm_out_ocr1a();
uint16_t pwm_out_ocr1b();

#endif // PWM_OUT_H