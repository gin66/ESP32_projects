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