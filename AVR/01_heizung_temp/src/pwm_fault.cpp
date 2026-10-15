/**
 * @file pwm_fault.cpp
 * @brief PWM output-path fault escalation. Pure C, no Arduino dependency.
 *
 * Compiled unchanged for the host test and for the ATmega328p, so the reboot
 * transition — which cannot be observed from the bench — is covered by the same
 * code that runs on the board. See pwm_fault.h for the design.
 */

#include "pwm_fault.h"

void pwm_boot_state_init(pwm_boot_state_t* st)
{
    if (st == 0) return;
    st->magic             = PWM_BOOT_MAGIC;
    st->boot_count        = 1u;
    st->pwm_fault_last_boot = 0u;
    st->pwm_fault_runs    = 0u;
}

uint8_t pwm_boot_state_trusted(const pwm_boot_state_t* st, uint8_t porf)
{
    if (st == 0) return 0u;
    /* Power-on always means we may not trust it: the contents are whatever RAM
     * powered up as. Checking it in addition to the magic costs nothing and
     * covers the case where SRAM happens to hold the pattern. */
    if (porf) return 0u;
    return (uint8_t)((st->magic == PWM_BOOT_MAGIC) ? 1u : 0u);
}

uint8_t pwm_monitor_observe(pwm_monitor_t* m, uint8_t alive)
{
    if (alive) {
        m->stalled = 0u;
        return 0u;
    }
    m->stalled = 1u;
    return 1u;
}

pwm_fault_action_t pwm_fault_boot(uint8_t* last_boot_fault, uint8_t* runs)
{
    if (!*last_boot_fault) {
        /* The previous run got far enough to reach the monitor and saw no fault,
         * so whatever the history was it no longer describes the hardware. */
        *runs = 0u;
        return PWM_FAULT_RUN;
    }

    if (*runs < 0xFFu) (*runs)++;

    /* Consumed here; only a fresh observation re-arms it. */
    *last_boot_fault = 0u;

    /* Boot-loop case: the latch kept re-arming across resets, which means the
     * watchdog is firing before the monitor can ever run. */
    if (*runs >= PWM_BOOT_LOOP_FAILOVER) return PWM_FAULT_FAILOVER;

    /* Otherwise stay in RUN. Whether the fault is still there is not knowable
     * until the monitor says so. */
    return PWM_FAULT_RUN;
}

pwm_fault_action_t pwm_fault_on_stall(uint8_t* last_boot_fault, uint8_t* runs)
{
    /* Surviving one reboot is the test. Anything the stall fired on the previous
     * run that has not recurred was fixed by the reset, so the count resets. */
    if (*runs >= PWM_FAILOVER_AFTER) return PWM_FAULT_FAILOVER;

    *last_boot_fault = 1u;
    return PWM_FAULT_REBOOT;
}

void pwm_fault_waveform(uint16_t permille, uint16_t* high_us, uint16_t* low_us)
{
    uint16_t period = (uint16_t)1000u;   /* 1 kHz, matching PWM_OUT_HZ */
    uint16_t h = permille;               /* at 1 kHz, 1 permille = 1 us high */

    if (h >= period) h = (uint16_t)(period - 1u);

    if (high_us != 0) *high_us = h;
    if (low_us  != 0) *low_us = (uint16_t)(period - h);
}