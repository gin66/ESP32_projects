#include "adc_wave.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static int g_checks = 0;

static void check(bool ok, const char* what)
{
    g_checks++;
    if (!ok) {
        printf("FAIL %s\n", what);
        g_fail++;
    }
}

/* 10-bit conversion in the accumulator's Q6 scale. */
static uint16_t q6(uint16_t ad)
{
    return (uint16_t)(ad << ADC_WAVE_Q_SHIFT);
}

/* ─── slot timebase ───────────────────────────────────────────────────────── */

/* The slot index advances once per whole millisecond. If one is missed, it must
 * advance by two; a 20 ms gap must land on the same slot. */
static void test_slot_advance()
{
    AdcBins b;
    AdcWaveClock clk;

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, 0u);

    adc_wave_add(&b, &clk, 0u, 0u);
    check(clk.slot == 0u && clk.sub_us == 0u, "slot starts at 0");

    adc_wave_add(&b, &clk, 0u, 999u);
    check(clk.slot == 0u && clk.sub_us == 999u, "999 us is still slot 0");

    adc_wave_add(&b, &clk, 0u, 1000u);
    check(clk.slot == 1u && clk.sub_us == 0u, "1000 us advances one slot");

    /* One whole millisecond is missing: advance two, not one. */
    adc_wave_add(&b, &clk, 0u, 3000u);
    check(clk.slot == 3u && clk.sub_us == 0u, "a missed ms advances two slots");

    /* A whole cycle later is the same slot. */
    adc_wave_add(&b, &clk, 0u, 23000u);
    check(clk.slot == 3u && clk.sub_us == 0u, "20 ms gap keeps the slot");

    /* 18 ms + 100 us: wrap 3 -> 1 and keep the remainder. */
    adc_wave_add(&b, &clk, 0u, 41100u);
    check(clk.slot == 1u && clk.sub_us == 100u, "wrap keeps the remainder");
}

/* ─── the running average itself ──────────────────────────────────────────── */

static void test_seed_and_steps()
{
    AdcBins b;
    AdcWaveClock clk;

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, 0u);

    adc_wave_add(&b, &clk, 160u, 0u);
    check(b.a[0] == q6(160u), "first sample seeds the slot in Q6");

    /* 10240 + (0 - 10240) / 32 = 9920, then 9920 + (0 - 9920) / 32 = 9610. */
    adc_wave_add(&b, &clk, 0u, 100u);
    check(b.a[0] == 9920u, "ema step 1");
    adc_wave_add(&b, &clk, 0u, 200u);
    check(b.a[0] == 9610u, "ema step 2");

    check(b.a[1] == 0u, "untouched slot stays empty");
}

static void test_seed_nonzero_slot()
{
    AdcBins b;
    AdcWaveClock clk;

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, 0u);

    adc_wave_add(&b, &clk, 0u, 0u);       /* primes slot 0 */
    adc_wave_add(&b, &clk, 777u, 5000u);
    check(b.a[5] == q6(777u), "first sample seeds a non-zero slot");
    check(b.a[0] == 0u, "slot 0 not touched");
}

static void test_upward_steps()
{
    AdcBins b;
    AdcWaveClock clk;

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, 0u);

    adc_wave_add(&b, &clk, 160u, 0u);       /* 10240 */
    adc_wave_add(&b, &clk, 320u, 100u);     /* +320  = 10560 */
    check(b.a[0] == 10560u, "ema upward step 1");
    adc_wave_add(&b, &clk, 320u, 200u);     /* +310  = 10870 */
    check(b.a[0] == 10870u, "ema upward step 2");
}

/* A constant input is a fixed point of the average. */
static void test_fixed_point()
{
    AdcBins b;
    AdcWaveClock clk;
    uint16_t i;

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, 0u);

    adc_wave_add(&b, &clk, 500u, 0u);
    for (i = 0; i < 200u; i++) {
        adc_wave_add(&b, &clk, 500u, (uint32_t)(i + 1u));
    }
    check(b.a[0] == q6(500u), "constant input does not move the average");
}

/* Q6 keeps six fractional bits, so a step reaches within 31/64 count of the
 * target instead of parking up to 31 whole counts away. */
static void test_converges_down()
{
    AdcBins b;
    AdcWaveClock clk;
    uint16_t i;

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, 0u);

    adc_wave_add(&b, &clk, 1000u, 0u);
    for (i = 0; i < 600u; i++) {
        adc_wave_add(&b, &clk, 0u, (uint32_t)(i + 1u));
    }
    check(b.a[0] > 0u && b.a[0] < ADC_WAVE_EMA_DIV,
          "downward step stops within the floor");
}

static void test_converges_up()
{
    AdcBins b;
    AdcWaveClock clk;
    uint16_t i;

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, 0u);

    adc_wave_add(&b, &clk, 1u, 0u);
    for (i = 0; i < 300u; i++) {
        adc_wave_add(&b, &clk, 1000u, (uint32_t)(i + 1u));
    }
    check(b.a[0] > q6(1000u) - ADC_WAVE_EMA_DIV && b.a[0] <= q6(1000u),
          "upward step stops within the floor");
}

/* ─── slots over time ─────────────────────────────────────────────────────── */

/* A slot with no conversion in a period carries its last average. */
static void test_hold()
{
    AdcBins b;
    AdcWaveClock clk;
    uint16_t out[ADC_WAVE_BINS];

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, 0u);

    adc_wave_add(&b, &clk, 100u, 0u);      /* slot 0 */
    adc_wave_add(&b, &clk, 200u, 1000u);   /* slot 1 */

    /* Skip to slot 0 of the next 20 ms period; slot 1 gets nothing. */
    adc_wave_add(&b, &clk, 100u, 20000u);

    adc_wave_snapshot(&b, out);
    check(out[0] == q6(100u), "slot 0 kept its average");
    check(out[1] == q6(200u), "slot 1 carried over the missing period");
}

/* A gap over a whole period must still land in the right slot. */
static void test_long_dt()
{
    AdcBins b;
    AdcWaveClock clk;

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, 0u);

    adc_wave_add(&b, &clk, 100u, 0u);         /* slot 0 */

    /* 2 periods + 5 ms later: slot 5 of the cycle. */
    adc_wave_add(&b, &clk, 700u, 45000u);
    check(b.a[5] == q6(700u), "long gap lands in slot 5");
    check(b.a[0] == q6(100u), "slot 0 untouched by the gap");
    check(b.a[6] == 0u, "neighbouring slot untouched");
}

/* Exactly one period later is the same slot, not the next one. */
static void test_exact_period()
{
    AdcBins b;
    AdcWaveClock clk;

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, 0u);

    adc_wave_add(&b, &clk, 0u, 0u);           /* primes slot 0 */
    adc_wave_add(&b, &clk, 640u, 7000u);      /* slot 7, 40960 */
    adc_wave_add(&b, &clk, 320u, 27000u);     /* -640 = 40320 */
    check(b.a[7] == 40320u, "exact period hits the same slot");
}

/* ─── snapshot semantics ──────────────────────────────────────────────────── */

static void test_snapshot_does_not_reset()
{
    AdcBins b;
    AdcWaveClock clk;
    uint16_t before[ADC_WAVE_BINS];
    uint16_t after[ADC_WAVE_BINS];

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, 0u);

    adc_wave_add(&b, &clk, 160u, 0u);
    adc_wave_snapshot(&b, before);
    adc_wave_add(&b, &clk, 0u, 100u);
    adc_wave_snapshot(&b, after);

    check(before[0] == 10240u, "snapshot before");
    check(after[0] == 9920u, "averaging continues after a snapshot");
}

static void test_clear_and_empty_snapshot()
{
    AdcBins b;
    uint16_t out[ADC_WAVE_BINS];
    uint8_t i;

    memset(&b, 0xA5, sizeof(b));
    adc_wave_bins_clear(&b);
    adc_wave_snapshot(&b, out);
    for (i = 0; i < ADC_WAVE_BINS; i++) {
        if (out[i] != 0u) {
            check(false, "cleared bins are all zero");
            return;
        }
    }
    check(true, "cleared bins are all zero");
}

/* ─── micros() wrap and irregular sampling ────────────────────────────────── */

static uint16_t g_ref[ADC_WAVE_BINS];

static void ref_apply(uint8_t slot, uint16_t sample)
{
    uint16_t a = g_ref[slot];
    uint16_t x = q6(sample);

    if (a == 0u) {
        g_ref[slot] = x;
    } else if (x >= a) {
        g_ref[slot] = (uint16_t)(a + ((x - a) >> ADC_WAVE_EMA_SHIFT));
    } else {
        g_ref[slot] = (uint16_t)(a - ((a - x) >> ADC_WAVE_EMA_SHIFT));
    }
}

static uint32_t g_rng = 0x12345678u;

static uint32_t rnd()
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng;
}

/* Random, irregular sample times across a micros() wrap, checked against an
 * independent continuous-time model. This exercises the incremental ms slot,
 * multi-millisecond gaps and both directions of the average. */
static void test_random_phase_and_wrap()
{
    AdcBins b;
    AdcWaveClock clk;
    uint32_t wire = 0xFFFFFFFFu - 30000u;
    uint64_t cont = 0;
    uint16_t i;

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, wire);
    memset(g_ref, 0, sizeof(g_ref));
    g_rng = 0x12345678u;

    for (i = 0; i < 5000u; i++) {
        uint32_t dt = rnd() % 400u;
        uint8_t slot;
        uint16_t sample;

        if ((i % 97u) == 0u) dt = 20000u + (rnd() % 3000u);

        wire += dt;
        cont += dt;

        /* Host-side reference: position from total elapsed time. Division is
         * fine here and keeps this model independent of the stepped slot. */
        slot = (uint8_t)((cont % ADC_WAVE_CYCLE_US) / ADC_WAVE_BIN_US);
        sample = (uint16_t)(rnd() % 1024u);

        adc_wave_add(&b, &clk, sample, wire);
        ref_apply(slot, sample);

        if (memcmp(b.a, g_ref, sizeof(g_ref)) != 0) {
            check(false, "random phase matches the reference model");
            return;
        }
    }
    check(true, "random phase matches the reference model");
}

/* ─── defensive behaviour ─────────────────────────────────────────────────── */

static void test_null_safety()
{
    AdcBins b;
    AdcWaveClock clk;
    uint16_t out[ADC_WAVE_BINS];

    adc_wave_bins_clear(&b);
    adc_wave_clock_reset(&clk, 0u);

    adc_wave_clock_reset(nullptr, 0u);
    adc_wave_bins_clear(nullptr);
    adc_wave_add(nullptr, &clk, 1u, 0u);
    adc_wave_add(&b, nullptr, 1u, 0u);
    adc_wave_snapshot(nullptr, out);
    adc_wave_snapshot(&b, nullptr);
    check(b.a[0] == 0u, "null arguments have no effect");
}

int main()
{
    test_slot_advance();
    test_seed_and_steps();
    test_seed_nonzero_slot();
    test_upward_steps();
    test_fixed_point();
    test_converges_down();
    test_converges_up();
    test_hold();
    test_long_dt();
    test_exact_period();
    test_snapshot_does_not_reset();
    test_clear_and_empty_snapshot();
    test_random_phase_and_wrap();
    test_null_safety();
    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
