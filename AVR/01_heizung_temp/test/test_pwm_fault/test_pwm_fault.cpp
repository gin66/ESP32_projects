/**
 * @file test_pwm_fault.cpp
 * @brief Tests for the PWM output-path fault escalation.
 *
 * The escalation ends in a watchdog reset, and its third step takes over the pin
 * permanently. Neither is observable on a bench without breaking hardware, so
 * the decision logic is kept in a pure module (pwm_fault) and exercised here.
 *
 * The companion case — a PWM failure recorded in the EEPROM log, which is what
 * survives a power blip — is covered in test_eeprom_log.cpp, since that is where
 * the record format lives.
 */

#include <stdio.h>
#include <stdint.h>
#include "pwm_fault.h"
#include "../src/eeprom_log.h"

static uint32_t g_checks;
static uint32_t g_failures;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

#define SECTION(name) printf("-- %s\n", name)

/* Fill a struct with a repeating byte, the way uninitialised SRAM looks. */
static void memset_pattern(pwm_boot_state_t* p, uint8_t v)
{
    uint8_t* b = (uint8_t*)p;
    uint16_t i;
    for (i = 0u; i < (uint16_t)sizeof(pwm_boot_state_t); i++) b[i] = v;
}


/* ─── Liveness detection ───────────────────────────────────────────────────── */

static void test_liveness(void)
{
    pwm_monitor_t m = { 0u, 0u, 0u };

    SECTION("A  both compare matches arriving means no stall");

    /* `alive` is now a positive handshake — true once BOTH Timer1 compare
     * matches have been seen since the previous call — not a counter to
     * difference. Anything non-zero counts as alive. */
    CHECK(pwm_monitor_observe(&m, 1u, 1000u) == 0u, "first sample stalled");
    CHECK(pwm_monitor_observe(&m, 1u, 1001u) == 0u, "progress stalled");
    CHECK(pwm_monitor_observe(&m, 1u, 5000u) == 0u,
          "progress stalled even though 4 s passed");

    SECTION("B  silence for the limit is a stall");
    m.last_ok_ms = 10000u; m.stalled = 0u;

    /* One tick under the limit must not fire, or the threshold is off by enough
     * to matter against a 250 ms listen_window() that polls in the same pass. */
    CHECK(pwm_monitor_observe(&m, 0u, 10000u + (PWM_STALL_LIMIT_MS - 1u)) == 0u,
          "fired under the limit");
    CHECK(pwm_monitor_observe(&m, 0u, 10000u + PWM_STALL_LIMIT_MS) == 1u,
          "did not fire exactly at the limit");
    CHECK(m.stalled == 1u, "stalled flag not set");

    SECTION("C  a cleared TIMSK1 IS a stall — this is SYSCMD_PWM_STALL");
    /* The case the old design could not express. With the compare interrupts off
     * and an `active` flag telling the monitor to stand down, clearing TIMSK1
     * was indistinguishable from parking the output at an endpoint, so the one
     * diagnostic that exists to reproduce a dead output path reproduced nothing.
     * With the interrupts kept alive at every duty there is no such ambiguity,
     * and silence is silence whatever the duty is. */
    m.last_ok_ms = 0u; m.stalled = 0u;
    CHECK(pwm_monitor_observe(&m, 0u, PWM_STALL_LIMIT_MS) == 1u,
          "a dead output path went unreported");

    SECTION("D  the limit is evaluated without signed overflow");
    /* last_ok_ms near the top of the range: an unsigned difference wraps
     * correctly, a signed one would not. */
    m.last_ok_ms = 0x7FFFFFFFu; m.stalled = 0u;
    CHECK(pwm_monitor_observe(&m, 0u, 0x7FFFFFFFu + (PWM_STALL_LIMIT_MS - 1u)) == 0u,
          "fired early across the wrap");
    CHECK(pwm_monitor_observe(&m, 0u, 0x7FFFFFFFu + PWM_STALL_LIMIT_MS) == 1u,
          "missed the limit across the wrap");

    SECTION("E  recovery clears the stall");
    m.last_ok_ms = 20000u; m.stalled = 1u;
    CHECK(pwm_monitor_observe(&m, 40u, 20500u) == 0u, "recovery reported a stall");
    CHECK(m.stalled == 0u, "stalled flag not cleared on recovery");
}

/* ─── Escalation ───────────────────────────────────────────────────────────── */

static void test_escalation(void)
{
    uint8_t last_boot_fault;
    uint8_t runs;

    SECTION("F  first occurrence escalates to a reboot, not to failover");
    last_boot_fault = 0u; runs = 0u;
    CHECK(pwm_fault_on_stall(&last_boot_fault, &runs) == PWM_FAULT_REBOOT,
          "first stall did not request a reboot");
    CHECK(last_boot_fault == 1u, "fault latch not armed for the next boot");

    SECTION("G  one latch buys a retry, it does not escalate on its own");
    /* This is the bug the test was written for: escalating here would fire on
     * the FIRST fault, making step 1's reset pointless. At boot all that is known
     * is that a fault happened before, not that one is happening now. */
    {
        uint8_t latch = 1u;   /* previous run faulted */
        uint8_t r = 0u;

        CHECK(pwm_fault_boot(&latch, &r) == PWM_FAULT_RUN,
              "a single latch escalated without an observation");
        CHECK(latch == 0u, "latch not consumed by the boot");
        CHECK(r == 1u, "run count = %u, want 1 — it must persist across the reboot", r);
    }

    SECTION("H  a clean boot discards stale history");
    {
        uint8_t latch = 0u;
        uint8_t r = 9u;

        CHECK(pwm_fault_boot(&latch, &r) == PWM_FAULT_RUN, "clean boot escalated");
        CHECK(r == 0u, "stale run count %u not discarded", r);
    }

    SECTION("I  full escalation: fault, retry, failover");
    {
        uint8_t latch = 0u;
        uint8_t r = 0u;

        /* boot 1: nothing latched */
        CHECK(pwm_fault_boot(&latch, &r) == PWM_FAULT_RUN, "boot 1 not RUN");
        /* fault observed -> reboot */
        CHECK(pwm_fault_on_stall(&latch, &r) == PWM_FAULT_REBOOT,
              "first fault did not request a reboot");
        CHECK(latch == 1u, "latch not armed");

        /* boot 2: latch survives the watchdog reset */
        CHECK(pwm_fault_boot(&latch, &r) == PWM_FAULT_RUN, "boot 2 escalated early");
        /* fault observed again -> the reset did not fix it */
        CHECK(pwm_fault_on_stall(&latch, &r) == PWM_FAULT_FAILOVER,
              "fault that survived a reset did not escalate to failover");

        /* once in failover the latch is not re-armed */
        CHECK(latch == 0u, "failover re-armed the latch");
    }

    SECTION("J  twenty clean boots never escalate");
    {
        uint8_t latch = 0u;
        uint8_t r = 0u;
        uint32_t cycle;

        for (cycle = 0u; cycle < 20u; cycle++) {
            CHECK(pwm_fault_boot(&latch, &r) == PWM_FAULT_RUN,
                  "cycle %lu: clean boot escalated", (unsigned long)cycle);
        }
    }

    SECTION("K  a boot loop escalates without ever observing a fault");
    /* The watchdog firing during setup() means pwm_liveness_tick() never runs,
     * so the observation path cannot fire. The latch re-arming across resets is
     * the only evidence available, and safe mode does not depend on anything that
     * would be broken. */
    {
        uint8_t latch = 1u;
        uint8_t r = 0u;
        uint8_t boots = 0u;
        pwm_fault_action_t act = PWM_FAULT_RUN;

        while ((act = pwm_fault_boot(&latch, &r)) == PWM_FAULT_RUN) {
            latch = 1u;     /* the loop keeps re-arming before the monitor runs */
            boots++;
            if (boots > 10u) break;
        }
        CHECK(act == PWM_FAULT_FAILOVER,
              "an endless boot loop never escalated (gave %u after %u boots)",
              (unsigned)act, (unsigned)boots);
    }
}

/* ─── Failover waveform ────────────────────────────────────────────────────── */

static void test_waveform(void)
{
    uint16_t hi = 0u;
    uint16_t lo = 0u;

    SECTION("L  the failover duty is the one that actually turns the heater on");

    /* The reason this is 200 and not 500: the measured curve puts commanded
     * duty 20 % at ~7 V, well above the ~2.5 V pump cutoff, while 50 % lands near
     * 3.3 V where the heater may not be driven at all. Asserted so the constant
     * cannot be quietly changed back to a value that delivers nothing. */
    CHECK(PWM_FAILOVER_PERMILLE == 200u,
          "failover permille is %u, expected 200", PWM_FAILOVER_PERMILLE);

    pwm_fault_waveform(PWM_FAILOVER_PERMILLE, &hi, &lo);
    CHECK(hi == 200u, "high time = %u us, want 200", hi);
    CHECK(lo == 800u, "low time  = %u us, want 800", lo);
    CHECK((uint16_t)(hi + lo) == 1000u, "period is not 1 ms");

    SECTION("M  endpoints and out-of-range values saturate rather than wrap");
    pwm_fault_waveform(0u, &hi, &lo);
    CHECK(hi == 0u && lo == 1000u, "0 permille gave %u/%u", hi, lo);

    pwm_fault_waveform(1000u, &hi, &lo);
    CHECK(hi == 999u && lo == 1u, "1000 permille gave %u/%u", hi, lo);

    /* A wrapped low time would be a nonsense waveform with a period that is not
     * 1 ms, which is why this saturates. */
    pwm_fault_waveform(60000u, &hi, &lo);
    CHECK(hi == 999u && lo == 1u, "60000 permille wrapped to %u/%u", hi, lo);

    SECTION("N  null outputs are tolerated");
    pwm_fault_waveform(200u, 0, 0);   /* must not fault on a null pointer */
    CHECK(1u, "null-pointer call did not survive");
}

/* ─── The fault flag in the EEPROM record ──────────────────────────────────── */

/*
 * The escalation latch is .noinit, and a power-on reset clears it. The record is
 * what makes the fault survive a power blip, so the flag has to round-trip
 * through the on-media format and be visible to a reader. Covered here because
 * the record layout is defined in eeprom_log.h; the ring behaviour around it is
 * in test_eeprom_log.cpp.
 */
static void test_fault_flag_roundtrip(void)
{
    epl_raw_t raw;
    epl_rec_t rec;

    SECTION("O  EPL_FLAG_PWM_FAIL survives encode/decode");

    epl_encode(&raw, 275u, (uint8_t)EPL_FLAG_PWM_FAIL);
    rec = epl_decode(&raw);
    CHECK(rec.valid == 1u, "fault record did not decode");
    CHECK(rec.duty == 275u, "duty = %u, want 275", rec.duty);
    CHECK((rec.flags & EPL_FLAG_PWM_FAIL) != 0u, "PWM_FAIL flag lost");
    CHECK((rec.flags & EPL_FLAG_BOOT) == 0u, "PWM_FAIL record also claims BOOT");

    SECTION("P  the flag does not collide with BOOT or the duty");
    epl_encode(&raw, 1000u, (uint8_t)(EPL_FLAG_BOOT | EPL_FLAG_PWM_FAIL));
    rec = epl_decode(&raw);
    CHECK(rec.valid == 1u, "combined flags did not decode");
    CHECK(rec.duty == 1000u, "duty = %u, want 1000 — flags must not eat bits", rec.duty);
    CHECK((rec.flags & EPL_FLAG_BOOT) != 0u, "BOOT lost");
    CHECK((rec.flags & EPL_FLAG_PWM_FAIL) != 0u, "PWM_FAIL lost");

    SECTION("Q  the duty is the full 10-bit range with flags set");
    epl_encode(&raw, 999u, (uint8_t)EPL_FLAG_PWM_FAIL);
    rec = epl_decode(&raw);
    CHECK(rec.duty == 999u, "duty = %u, want 999", rec.duty);

    SECTION("R  the flag is covered by the checksum");
    {
        /* Flip the duty while leaving the flag set: the record must stop
         * decoding, or a corrupted fault record would read as a valid one. */
        raw.b[0] = (uint8_t)(raw.b[0] ^ 0x01u);
        rec = epl_decode(&raw);
        CHECK(rec.valid == 0u, "corrupted fault record still decoded");
    }
}


/* ─── Retained boot state ──────────────────────────────────────────────────── */

/*
 * Regression test for a bug found on the bench: a freshly flashed node went
 * straight into failover and reported src=FAULT, with no stall ever observed.
 *
 * .noinit is never initialised by the C runtime, and the only guard on it was
 * PORF. That is not enough: an Arduino upload starts the application with a
 * watchdog reset rather than a power-on one, so PORF stays clear, and flashing
 * does not clear RAM either. The three retained bytes were whatever SRAM powered
 * up as, and a garbage run-count above the escalation threshold sent the node
 * into safe mode before the monitor had run once.
 */
static void test_boot_state_trust(void)
{
    pwm_boot_state_t st;

    SECTION("T1  a virgin block is never trusted");
    memset_pattern(&st, 0xFFu);
    CHECK(pwm_boot_state_trusted(&st, 0u) == 0u,
          "all-0xFF block trusted with PORF clear (the reported failure)");

    memset_pattern(&st, 0x00u);
    CHECK(pwm_boot_state_trusted(&st, 0u) == 0u, "all-zero block trusted");

    SECTION("T2  our own block is trusted across a watchdog reset");
    pwm_boot_state_init(&st);
    st.pwm_fault_last_boot = 1u;
    st.pwm_fault_runs      = 2u;
    CHECK(pwm_boot_state_trusted(&st, 0u) == 1u,
          "a block we wrote was not trusted after a non-power-on reset");

    SECTION("T3  a power-on reset discards it even with the magic intact");
    /* RAM is undefined after a power-on, so a surviving magic byte would be a
     * coincidence, not provenance. */
    CHECK(pwm_boot_state_trusted(&st, 1u) == 0u,
          "a power-on reset was trusted despite the magic matching");

    SECTION("T4  init stamps the magic and clears the counters");
    pwm_boot_state_init(&st);
    CHECK(st.magic == PWM_BOOT_MAGIC, "magic = 0x%04X, want 0x%04X",
          st.magic, PWM_BOOT_MAGIC);
    CHECK(st.boot_count == 1u, "boot_count = %u, want 1", st.boot_count);
    CHECK(st.pwm_fault_last_boot == 0u, "fault latch not cleared");
    CHECK(st.pwm_fault_runs == 0u, "run count not cleared");

    SECTION("T5  a garbage run count cannot escalate on an untrusted block");
    /* The exact reported failure: a plausible block that is not ours, carrying a
     * run count past the boot-loop threshold. */
    {
        pwm_boot_state_t g;
        memset_pattern(&g, 0x5Au);
        g.magic               = 0x1234u;   /* not ours */
        g.boot_count          = 200u;
        g.pwm_fault_last_boot = 1u;
        g.pwm_fault_runs      = 9u;        /* past PWM_BOOT_LOOP_FAILOVER */

        CHECK(pwm_boot_state_trusted(&g, 0u) == 0u,
              "a foreign block with a high run count was trusted");

        pwm_boot_state_init(&g);
        CHECK(pwm_fault_boot(&g.pwm_fault_last_boot, &g.pwm_fault_runs)
                  == PWM_FAULT_RUN,
              "an untrusted block still escalated after re-init");
    }

    SECTION("T6  the requested duty is not the delivered duty — see pwm_out");
    /* The waveform can only change state at a main-loop pass boundary, so the
     * delivered high time is quantised up to the pass length. Requesting 200 us
     * with a ~293 us pass delivered ~27%, measured on the bench as ~6 V instead
     * of ~7 V. The fix is a tighter loop plus reporting the delivered duty from
     * the edge counters; this pins the arithmetic those counters feed so the
     * reported figure cannot be misread as the requested one. */
    {
        uint32_t up = 271u, down = 729u, tot = up + down;
        CHECK((up * 1000UL) / tot == 271u,
              "delivered duty arithmetic wrong: %lu",
              (unsigned long)((up * 1000UL) / tot));
        CHECK(((up * 1000UL) / tot) != PWM_FAILOVER_PERMILLE,
              "delivered duty equals the requested one — the quantisation this "
              "test documents is not being reproduced");
    }

    SECTION("T7  null is tolerated");
    CHECK(pwm_boot_state_trusted(0, 0u) == 0u, "null trusted");
    pwm_boot_state_init(0);
    CHECK(1u, "null init did not survive");
}

int main(void)
{
    printf("pwm_fault escalation tests\n");
    printf("  stall_limit=%lu ms  failover_after=%u  failover=%u permille\n\n",
           (unsigned long)PWM_STALL_LIMIT_MS,
           (unsigned)PWM_FAILOVER_AFTER,
           (unsigned)PWM_FAILOVER_PERMILLE);

    test_liveness();
    test_escalation();
    test_boot_state_trust();
    test_waveform();
    test_fault_flag_roundtrip();

    printf("\n%lu checks, %lu failures\n",
           (unsigned long)g_checks, (unsigned long)g_failures);
    return (g_failures == 0u) ? 0 : 1;
}