/**
 * @file pwm_fault.h
 * @brief Escalation for a dead PWM output path, as a pure state machine.
 *
 * `pwmStalled` means the PWM is not working at all: the Timer1 compare vectors
 * have stopped, so the duty in force is not being delivered to the load whatever
 * the status frames claim. That is a hardware-path failure, and it escalates
 * rather than taking one action:
 *
 *   1. first occurrence  -> latch, log, starve the watchdog and reboot.
 *      A reset can genuinely fix this: pwm_out_begin() reprograms TCCR1A,
 *      ICR1, OCR1A, OCR1B, TCCR1B and TIMSK1 from scratch, clearing whatever
 *      was transient.
 *   2. fault again on the next boot -> not transient.
 *   3. safe mode -> a software square wave on the pin with the timer stopped.
 *      Unavoidable: if the compare vectors are dead there is no way to make
 *      edges with them.
 *
 * This is pure logic with no Arduino, no clock and no serial, so the host test
 * can exercise every transition — including the one that reboots the board,
 * which is otherwise unobservable from the bench.
 */

#ifndef PWM_FAULT_H
#define PWM_FAULT_H

#include <stdint.h>

/* No edge progress for this long counts as a stall. At 1 kHz there are two ISR
 * entries per millisecond, so 250 ms is ~500 missed entries — far outside
 * anything attributable to a long blocking call in the loop. */
#define PWM_STALL_LIMIT_MS  ((uint32_t)250u)

/* Consecutive boots on which the stall has been *observed* before safe mode.
 * 1 means "try a single reset first" — so the fault is given exactly one retry
 * before the pin is taken over. */
#define PWM_FAILOVER_AFTER  ((uint8_t)1u)

/* Consecutive latched faults that escalate to safe mode without an observation.
 *
 * This covers the case the observation path cannot: if the watchdog is firing
 * during setup(), the loop never reaches pwm_liveness_tick(), so no fault is
 * ever *seen* and the stall-based escalation never triggers — while the node
 * resets forever. Safe mode does not depend on any of the things that would be
 * broken, so it is the right landing spot for that too. */
#define PWM_BOOT_LOOP_FAILOVER ((uint8_t)3u)

/* Duty of the failover square wave, in permille.
 *
 * 200, not 500. The measured transfer curve puts commanded duty 20 % at ~7 V,
 * comfortably above the ~2.5 V pump cutoff, while 50 % lands near 3.3 V where
 * the heater may not be driven at all — a failover that does not actually turn
 * the heater on is worse than useless, because it reports FAULT while delivering
 * nothing. At 1 kHz one permille is one microsecond of high time, so this value
 * is the waveform's high time directly. */
#define PWM_FAILOVER_PERMILLE ((uint16_t)200u)

typedef enum {
    PWM_FAULT_RUN = 0,   /* no fault: keep monitoring                    */
    PWM_FAULT_REBOOT,    /* first occurrence: latch it and reboot       */
    PWM_FAULT_FAILOVER   /* it survived a reset: take over the pin       */
} pwm_fault_action_t;

/* ─── Liveness ─────────────────────────────────────────────────────────────── */

/* Edge-progress tracker.
 *
 * `runs` is the cumulative PWM ISR entry count; the monitor compares successive
 * samples rather than keeping its own timer, because it has no clock of its own
 * and the caller's is already counting. */
typedef struct {
    uint32_t last_runs;
    uint32_t last_ok_ms;
    uint8_t  stalled;
} pwm_monitor_t;

/* Fold one observation. Returns non-zero if this observation is a stall.
 *
 * `active` is whether the counter is supposed to be generating edges at all.
 * It must be passed in rather than deduced here: at 0 or 1000 permille both
 * interrupts are deliberately off and demanding edges would report a fault
 * that does not exist. */
uint8_t pwm_monitor_observe(pwm_monitor_t* m, uint32_t runs, uint32_t now_ms,
                            uint8_t active);

/* ─── Escalation ───────────────────────────────────────────────────────────── */

/* Called once at boot with the retained latch from the previous run.
 *
 * `last_boot_fault` and `runs` are the retained values — .noinit, which a
 * watchdog reset preserves and only a power-on reset clears. A power loss is a
 * fresh start, so there is nothing to gain from making these EEPROM-resident;
 * the EEPROM log carries the fault for reporting, not for this decision.
 *
 * Both are taken by pointer and written back: the run count has to persist
 * across the reboot that pwm_fault_on_stall() triggers, so returning it by
 * value loses exactly the state the escalation depends on.
 *
 * Returns FAILOVER only for the boot-loop case above. It does NOT escalate on a
 * single latch, because at boot all that is known is that a fault happened
 * *before* — not that it is happening now. Deciding failover here would escalate
 * on the first fault and make the retry in step 1 pointless. */
pwm_fault_action_t pwm_fault_boot(uint8_t* last_boot_fault, uint8_t* runs);

/* Fold an *observed* stall into the retained counters and decide what to do.
 * Sets *last_boot_fault so the next boot can see it. */
pwm_fault_action_t pwm_fault_on_stall(uint8_t* last_boot_fault, uint8_t* runs);

/* ─── Failover waveform ────────────────────────────────────────────────────── */

/* Split a permille duty into the high/low half-times of a 1 kHz square wave.
 * Saturating rather than wrapping: a wrapped low time would be a nonsense
 * waveform, and the caller passes a value it may not have clamped. */
void pwm_fault_waveform(uint16_t permille, uint16_t* high_us, uint16_t* low_us);

#endif /* PWM_FAULT_H */