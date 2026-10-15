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

/* Liveness handshake, set by the ISRs and cleared by the consumer.
 *
 * These are NOT edge tallies. Each ISR sets its own byte on every entry, and
 * pwm_out_alive() reports true only when BOTH are set, clearing them as it does
 * so. The timer running is therefore proven in the only way that matters: two
 * compare matches actually arrived. Nothing has to reason about what the counter
 * should have reached, or which duty makes a counter legitimately stationary.
 *
 * That last point is the whole reason this exists, and it is why there are no
 * edge tallies alongside it. A running total of rising and falling edges was the
 * previous signal, and it carried the same defect it was meant to detect: it only
 * means something while both edges are supposed to occur, so a counter frozen at
 * an endpoint duty looks identical to a frozen counter on a dead output path.
 * Keeping it as a secondary figure would have reintroduced that ambiguity through
 * the back door, with the authority of a number on the stats line. One signal,
 * one meaning. */
static volatile uint8_t liveRise = 0;
static volatile uint8_t liveFall = 0;

/* Whether each compare is allowed to touch the pin. The interrupts stay enabled
 * at every duty; at an endpoint the pin is held by the port register instead and
 * these keep the ISRs from disturbing it. */
static volatile uint8_t riseEnabled = 0;
static volatile uint8_t fallEnabled = 0;

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
ISR(TIMER1_COMPA_vect) {                      // TOP reached, pulse starts
    liveRise = 1u;
    if (riseEnabled) PORTD |= PORT_BIT;
}
ISR(TIMER1_COMPB_vect) {                      // duty reached, pulse ends
    liveFall = 1u;
    if (fallEnabled) PORTD &= ~PORT_BIT;
}

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

    // At an endpoint the pin is held by the port register — exact 0 % and exact
    // 100 %, no edge artefacts — while the compare interrupts stay enabled and
    // both edge enables go false, so the ISRs keep proving the timer is alive
    // without touching the pin. OCR1B goes to mid-scale purely to give the two
    // matches somewhere clearly separate to happen.
    //
    // It used to be parked at 0 instead. That puts the falling match at the wrap,
    // one tick from the rising match at TOP — technically two matches, but close
    // enough that the handshake depends on where the poll lands. Mid-scale keeps
    // them half a period apart at every duty, so no combination of duty and poll
    // phase can make them ambiguous.
    if (permille == 0u || permille == 1000u) {
        riseEnabled = 0u;
        fallEnabled = 0u;
        if (permille == 0u) PORTD &= ~PORT_BIT;
        else                 PORTD |=  PORT_BIT;
        TCIFR1  = _BV(TOV1) | _BV(OCF1A) | _BV(OCF1B);
        OCR1B   = (uint16_t)(PWM_TICKS / 2UL);
        TIMSK1  = _BV(OCIE1A) | _BV(OCIE1B);
        interrupts();
        return;
    }

    // Interior duty, clamped to 1…99 %. 1 and 99 % rather than 1…999 because
    // near the endpoints the two matches crowd together: at 1 % the falling match
    // is 160 ticks into a 16000-tick period, a hair away from the wrap. Clamping
    // keeps a real minimum separation, which is what makes the liveness handshake
    // readable at any duty instead of only at comfortable ones.
    if (permille < 10u)   permille = 10u;
    if (permille > 990u)  permille = 990u;

    // 10…990 permille maps to 160…15840 ticks, comfortably inside the period, so
    // the falling match always precedes the rising one by a clear margin.
    uint16_t ticks = (uint16_t)((uint32_t)permille * PWM_TICKS / 1000UL);

    riseEnabled = 1u;
    fallEnabled = 1u;
    PORTD &= ~PORT_BIT;             // begin from a known edge
    TCIFR1 = _BV(TOV1) | _BV(OCF1A) | _BV(OCF1B);
    OCR1B  = ticks;
    TIMSK1 = _BV(OCIE1A) | _BV(OCIE1B);

    interrupts();
}

/* True once both compare matches have arrived since the last call, which also
 * clears them — so a caller polling this gets "did anything happen at all",
 * not a count it has to difference itself. Cleared only on success: clearing
 * unconditionally would let a poll landing between the two edges report a false
 * negative. */
bool pwm_out_alive(void)
{
    bool alive;

    noInterrupts();
    alive = (liveRise != 0u) && (liveFall != 0u);
    liveRise = 0u; liveFall = 0u;
    interrupts();

    return alive;
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
static uint32_t      foRising  = 0;
static uint32_t      foFalling = 0;

/* Is the PIN actually being switched? Not the same question as "is the timer
 * running", and the two have come apart now: at an endpoint the compare
 * interrupts stay enabled to keep liveness honest, but the pin is held by the
 * port register and never moves. Confusing the two is what made a failover — where
 * the counter is stopped entirely and the loop square-waves the pin — report
 * itself to the receiver as "endpoint, held". */
bool pwm_out_modulating(void)
{
    if (foActive) return true;
    return (riseEnabled != 0u) && (fallEnabled != 0u);
}

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
        foRising++;
    } else {
        PORTD &= ~PORT_BIT;
        span = foLowUs;
        foFalling++;
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

uint32_t pwm_out_failover_rising(void)
{
    return foRising;
}

uint32_t pwm_out_failover_falling(void)
{
    return foFalling;
}

void pwm_out_failover_end(void)
{
    noInterrupts();
    foActive = 0;
    riseEnabled = 0u;
    fallEnabled = 0u;
    interrupts();
    pwm_out_begin();   /* reprograms the counter and re-arms the compare pair */
}

void pwm_out_edges_resume(void)
{
    noInterrupts();
    riseEnabled = 0u;
    fallEnabled = 0u;
    interrupts();
}

uint8_t  pwm_out_timsk1()        { return (uint8_t)TIMSK1; }
uint8_t  pwm_out_tccr1a()        { return (uint8_t)TCCR1A; }
uint8_t  pwm_out_tccr1b()        { return (uint8_t)TCCR1B; }
uint16_t pwm_out_icr1()          { return ICR1; }
uint16_t pwm_out_ocr1a()         { return OCR1A; }
uint16_t pwm_out_ocr1b()         { return OCR1B; }