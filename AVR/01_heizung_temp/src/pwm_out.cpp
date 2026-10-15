#include "pwm_out.h"

#include "pwm_fault.h"   /* the failover waveform arithmetic, kept pure */

// Prescaler 1 → 16 MHz counter. TOP = 16000 ticks → 16000 µs = exactly 1 kHz.
// The duty is adjustable in 1 µs steps (0.00625 %).
#define PWM_TICKS 16000UL
#define PORT_BIT  _BV(PORTD7)

// avr-libc renamed the timer flag registers TIFRx → TCIFRx. Support both so the
// code builds against older and newer toolchains.
#if !defined(TCIFR1) && defined(TIFR1)
#define TCIFR1 TIFR1
#endif

static volatile uint32_t rising = 0;
static volatile uint32_t falling = 0;

// ── Why both edges are compare matches, and why OCR1A sits on TOP ───────────
//
// The first attempt ran this in fast PWM (mode 14, TOP = ICR1) and raised the
// edge from TIMER1_OVF. Measured on the bench, OCIE1A fired reliably — exactly
// 5000 falling edges per 5 s, i.e. 1 kHz — while TOV1 never fired at all.
// Rising edges stayed at zero and the output sat permanently low, with
// TIMSK1 = 0b011 proving TOIE1 had been enabled and never cleared.
//
// Rather than rely on overflow semantics that demonstrably failed, the rising
// edge is now taken from a compare match too. OCR1A is deliberately programmed
// to equal TOP: in mode 14 the counter reaches ICR1 before it wraps, so
// matching OCR1A *is* the wrap instant. OCR1B carries the duty.
//
// One AVR trap to remember: WGM0 and CS10 are the same bit (TCCR1B bit 0), so
// the clock select unavoidably sets a wave-shape bit. That makes every mode
// with WGM3:0 = 0000 — including CTC modes 0, 2, 4 and 12 — unreachable with any
// prescaler. Mode 14 with WGM3:0 = 0001 (prescaler 1) is valid, and is the
// encoding Arduino's own core uses for Timer1 PWM on pins 9/10.
//
// Port D is shared with the 1-Wire bus on D4, so these are read-modify-write
// operations on a byte another piece of code also touches. That is safe only
// because OneWire performs its bit-banging writes inside cli()/sei() critical
// sections. Any future non-atomic PORTD write in the main loop would race here.
ISR(TIMER1_COMPA_vect) { rising++;  PORTD |=  PORT_BIT; }  // TOP reached, pulse starts
ISR(TIMER1_COMPB_vect) { falling++; PORTD &= ~PORT_BIT; }  // duty reached, pulse ends

// Silence both edges. Must be called with interrupts globally disabled: an
// already-pending compare match would otherwise drop the pin after we set it.
static inline void pwm_stop_edges()
{
    TIMSK1 = 0;                                   // disable both interrupts
    TCIFR1 = _BV(TOV1) | _BV(OCF1A) | _BV(OCF1B); // and drop what is pending
}

void pwm_out_begin()
{
    pinMode(PWM_OUT_PIN, OUTPUT);

    noInterrupts();

    // COM1A/COM1B bits = 00 leave OC1A and OC1B as plain ports. Enabling either
    // PWM output would drive D9 and D10, which are the nRF24's CSN and CE.
    TCCR1A = 0;

    // TOP, and both compare levels, must be programmed BEFORE the clock starts.
    // Writing TCCR1B first would run the counter against a zero TOP.
    ICR1   = PWM_TICKS;            // mode 14 TOP
    OCR1A  = PWM_TICKS;            // match at TOP  -> rising edge
    OCR1B  = 0;                    // duty, start at 0 %
    TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);   // mode 14, prescaler 1 — starts it

    pwm_stop_edges();
    PORTD &= ~PORT_BIT;

    interrupts();
}

void pwm_out_set(uint16_t permille)
{
    if (permille > 1000) permille = 1000;

    noInterrupts();

    // The endpoints need no timing at all, so drive the port directly and leave
    // both interrupts off — exact 0 % and exact 100 %, with no per-period
    // interrupt cost and no edge artefacts.
    if (permille == 0) {
        pwm_stop_edges();
        PORTD &= ~PORT_BIT;
        interrupts();
        return;
    }

    if (permille == 1000) {
        pwm_stop_edges();
        PORTD |= PORT_BIT;
        interrupts();
        return;
    }

    // Interior duty. 1…999 permille maps to 16…15984 ticks, which can never
    // reach TOP, so the falling match always precedes the rising one.
    uint16_t ticks = (uint16_t)((uint32_t)permille * PWM_TICKS / 1000UL);

    PORTD &= ~PORT_BIT;             // begin from a known edge
    TCIFR1 = _BV(TOV1) | _BV(OCF1A) | _BV(OCF1B);
    OCR1B  = ticks;
    TIMSK1 = _BV(OCIE1A) | _BV(OCIE1B);

    interrupts();
}

bool pwm_out_active()
{
    return (TIMSK1 & (_BV(OCIE1A) | _BV(OCIE1B))) != 0;
}

// ─── Failover: software square wave, timer stopped ───────────────────────────
//
// State for the non-blocking waveform. The pin is driven directly here so this
// module keeps sole ownership of it; main.cpp only calls begin/tick.
static uint16_t      foHighUs  = 0;
static uint16_t      foLowUs   = 0;
static unsigned long foNextUs  = 0;
static uint8_t       foStep    = 0;   /* 0 = next edge is rising */
static uint8_t       foActive  = 0;

void pwm_out_failover_begin(uint16_t permille)
{
    noInterrupts();
    pwm_stop_edges();          /* TIMSK1 = 0 and drop what is pending */
    TCCR1B = 0;                /* counter off entirely, not merely unmasked */
    PORTD &= ~PORT_BIT;
    interrupts();

    pwm_fault_waveform(permille, &foHighUs, &foLowUs);

    foStep   = 0;
    foNextUs = micros() + (unsigned long)foHighUs;
    foActive = 1;
}

void pwm_out_failover_tick(void)
{
    unsigned long now;
    unsigned long span;

    if (!foActive) return;

    now = micros();

    /* Wrap-safe: micros() rolls at ~71 minutes, and the signed form is exact
     * for any interval below ~35 of those. The deadline is one millisecond
     * away, so the margin is enormous. */
    if ((long)(now - foNextUs) < 0) return;

    foStep = (uint8_t)(foStep ^ 1u);
    if (foStep) {
        PORTD |=  PORT_BIT;
        span = foHighUs;
    } else {
        PORTD &= ~PORT_BIT;
        span = foLowUs;
    }

    foNextUs += span;

    /* If the loop was too slow to hit the deadline, move it forward by one
     * span instead of allowing a burst of catch-up toggles — a burst would put
     * several edges back-to-back and wreck the very duty this is trying to
     * deliver. */
    if ((long)(foNextUs - now) < 0) foNextUs = now + span;
}

uint8_t pwm_out_failover_active(void)
{
    return foActive;
}

void pwm_out_failover_end(void)
{
    foActive = 0;
    pwm_out_begin();   /* reprograms the counter and re-arms the compare pair */
}

uint32_t pwm_out_rising_edges()
{
    // 32-bit atomic reads: a bare return of a volatile uint32_t is four
    // non-atomic instructions on AVR, and a Timer1 compare firing mid-read
    // yields a torn count — an edge tally that never could have happened.
    noInterrupts();
    uint32_t v = rising;
    interrupts();
    return v;
}

uint32_t pwm_out_falling_edges()
{
    noInterrupts();
    uint32_t v = falling;
    interrupts();
    return v;
}

// Combined ISR entry count — the liveness signal the main loop's PWM health
// monitor uses. Derived from the two counters above rather than a third one, so
// it costs nothing inside either ISR.
uint32_t pwm_out_isr_runs()
{
    noInterrupts();
    uint32_t v = rising + falling;
    interrupts();
    return v;
}

uint8_t  pwm_out_timsk1()        { return (uint8_t)TIMSK1; }
uint8_t  pwm_out_tccr1a()        { return (uint8_t)TCCR1A; }
uint8_t  pwm_out_tccr1b()        { return (uint8_t)TCCR1B; }
uint16_t pwm_out_icr1()          { return ICR1; }
uint16_t pwm_out_ocr1a()         { return OCR1A; }
uint16_t pwm_out_ocr1b()         { return OCR1B; }