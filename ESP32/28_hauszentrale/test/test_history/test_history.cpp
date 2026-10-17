/**
 * @file test_history.cpp
 * @brief Host tests for the L1/L2 store, SD rollover and SPIFFS ring.
 *
 * The same src/haus_history.cpp the ESP32 build uses is compiled here with
 * HAUS_HOST. SD and SPIFFS are simulated in memory by that build.
 */

#include "haus_history.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static HausState fresh_state(uint32_t epoch) {
    HausState st;
    memset(&st, 0, sizeof(st));
    st.can.temp_by_fn[SENSOR_FN_CHAMBER] = 5000;
    st.can.temp_by_fn[SENSOR_FN_WATER_IN] = 4000;
    st.can.temp_by_fn[SENSOR_FN_WATER_OUT] = 6000;
    st.can.pwm_permille = 500;
    st.can.watts = 1000;
    st.can.valid = true;
    st.can.last_update_epoch = epoch;
    st.climate.climate_flags = CLIMATE_VALID_AHT20 | CLIMATE_VALID_BMP280;
    st.climate.living_c100 = 2100;
    st.climate.hum_c100 = 4500;
    st.climate.pressure_pa = 101325;
    st.climate.last_update_epoch = epoch;
    return st;
}

static uint32_t local_midnight(int year, int mon, int mday);

/* ─── A: a simulated day ──────────────────────────────────────────────────── */

static void test_day(uint32_t start) {
    SECTION("A1  1 440 minute samples, 24 hourly columns");

    for (uint32_t i = 0; i < HAUS_L1_SAMPLES; i++) {
        const uint32_t t = start + i * 60u;
        HausState st = fresh_state(t);
        CHECK(haus_history_sample(&st, t), "sample %u rejected", (unsigned)i);
    }

    CHECK(haus_history_l1_count() == HAUS_L1_SAMPLES,
          "L1 count = %u, want %u", (unsigned)haus_history_l1_count(),
          (unsigned)HAUS_L1_SAMPLES);
    CHECK(haus_history_sd_write_count() == 0,
          "writes before midnight = %u, want 0",
          (unsigned)haus_history_sd_write_count());

    RollupColumn *l2 = haus_history_l2();
    for (uint32_t h = 0; h < 24; h++) {
        const uint32_t slot = ((start / 3600u) + h) % HAUS_L2_COLUMNS;
        const RollupColumn &c = l2[slot];
        CHECK(c.t == (start / 60u) + h * 60u,
              "hour %u t = %u", (unsigned)h, (unsigned)c.t);
        CHECK(c.n[0] == 60, "hour %u n[0] = %u", (unsigned)h,
              (unsigned)c.n[0]);
        CHECK(c.mn[0] == 5000 && c.mx[0] == 5000,
              "hour %u min/max = %d/%d", (unsigned)h, (int)c.mn[0],
              (int)c.mx[0]);
        CHECK(c.sum[0] == 5000 * 60, "hour %u sum[0] = %d", (unsigned)h,
              (int)c.sum[0]);
        CHECK(c.n[3] == 60 && c.mn[3] == 2100,
              "hour %u living n/min = %u/%d", (unsigned)h,
              (unsigned)c.n[3], (int)c.mn[3]);
        CHECK(c.n[5] == 60 && c.sum[5] == 101325 * 60,
              "hour %u pressure n/sum = %u/%d", (unsigned)h,
              (unsigned)c.n[5], (int)c.sum[5]);
    }

    const uint32_t last = start + (HAUS_L1_SAMPLES - 1u) * 60u;
    HausState again = fresh_state(last);
    CHECK(haus_history_sample(&again, last), "idempotent sample rejected");
    CHECK(haus_history_l1_count() == HAUS_L1_SAMPLES,
          "idempotent sample changed L1 count");
}

/* ─── B: midnight rollover ────────────────────────────────────────────────── */

static uint32_t test_rollover(uint32_t start) {
    SECTION("B1  midnight writes the day file and the rollup exactly once");

    const uint32_t next = start + 86400u;
    HausState st = fresh_state(next);
    CHECK(haus_history_sample(&st, next), "midnight sample rejected");

    CHECK(haus_history_l1_count() == 1,
          "L1 after midnight = %u, want 1",
          (unsigned)haus_history_l1_count());
    CHECK(haus_history_sd_write_count() == 2,
          "writes = %u, want 2", (unsigned)haus_history_sd_write_count());
    CHECK(strcmp(haus_history_last_day_path(), "/data_20241014.bin") == 0,
          "day path = '%s'", haus_history_last_day_path());
    CHECK(haus_history_test_day_bytes() == HAUS_DAY_FILE_BYTES,
          "day bytes = %u", (unsigned)haus_history_test_day_bytes());
    CHECK(haus_history_test_rollup_bytes() == HAUS_ROLLUP_FILE_BYTES,
          "rollup bytes = %u", (unsigned)haus_history_test_rollup_bytes());
    CHECK(HAUS_L1_BYTES == 34560u, "L1 size = %u", (unsigned)HAUS_L1_BYTES);
    CHECK(HAUS_L2_BYTES == 65472u, "L2 size = %u", (unsigned)HAUS_L2_BYTES);
    CHECK(HAUS_DAY_FILE_BYTES == 34652u, "day file = %u",
          (unsigned)HAUS_DAY_FILE_BYTES);
    CHECK(HAUS_ROLLUP_FILE_BYTES == 65520u, "rollup file = %u",
          (unsigned)HAUS_ROLLUP_FILE_BYTES);

    return next;
}

/* ─── C: stale marking ────────────────────────────────────────────────────── */

static void test_stale(uint32_t next) {
    SECTION("C1  a source silent for 60 s is marked stale and kept out of L2");

    const uint32_t t = next + 60u;  /* 00:01 */
    HausState st = fresh_state(t);
    st.can.last_update_epoch = t - 120u;     /* stale */
    st.climate.last_update_epoch = t - 10u;  /* fresh */
    CHECK(haus_history_sample(&st, t), "stale sample rejected");

    const gui_sample_t *l1 = haus_history_l1();
    const gui_sample_t &s = l1[(t % 86400u) / 60u];
    CHECK((s.flags & SAMPLE_FLAG_STALE) != 0, "STALE flag missing");
    CHECK((s.stale_mask & SAMPLE_STALE_CHAMBER) != 0, "chamber not stale");
    CHECK((s.stale_mask & SAMPLE_STALE_WATER_IN) != 0, "water-in not stale");
    CHECK((s.stale_mask & SAMPLE_STALE_WATER_OUT) != 0, "water-out not stale");
    CHECK((s.stale_mask & SAMPLE_STALE_POWER) != 0, "power not stale");
    CHECK((s.stale_mask & SAMPLE_STALE_LIVING) == 0, "living wrongly stale");
    CHECK((s.stale_mask & SAMPLE_STALE_HUMIDITY) == 0,
          "humidity wrongly stale");
    CHECK((s.stale_mask & SAMPLE_STALE_PRESSURE) == 0,
          "pressure wrongly stale");

    /* The 00:00 sample was fresh, so n[0] stays 1, not 2. */
    const uint32_t slot = (t / 3600u) % HAUS_L2_COLUMNS;
    CHECK(haus_history_l2()[slot].n[0] == 1,
          "stale minute entered L2: n[0] = %u",
          (unsigned)haus_history_l2()[slot].n[0]);
}

static void test_stale_recovery(uint32_t next) {
    SECTION("C2  stale clears on the next fresh sample; clock-back is not stale");

    const uint32_t t = next + 120u;  /* 00:02 */
    HausState st = fresh_state(t);   /* both sources refreshed now */
    CHECK(haus_history_sample(&st, t), "fresh sample rejected");

    const gui_sample_t *l1 = haus_history_l1();
    const gui_sample_t &s = l1[(t % 86400u) / 60u];
    CHECK((s.flags & SAMPLE_FLAG_STALE) == 0, "STALE flag did not clear");
    CHECK(s.stale_mask == 0, "stale mask = 0x%02x, want 0",
          (unsigned)s.stale_mask);

    /* 00:00 fresh + 00:02 fresh; the 00:01 stale minute was excluded. */
    const uint32_t slot = (t / 3600u) % HAUS_L2_COLUMNS;
    CHECK(haus_history_l2()[slot].n[0] == 2,
          "L2 n[0] = %u, want 2", (unsigned)haus_history_l2()[slot].n[0]);

    const uint32_t back = t + 60u;  /* 00:03, clock stepped backwards */
    st = fresh_state(back);
    st.can.last_update_epoch = back + 100u;
    st.climate.last_update_epoch = back + 100u;
    CHECK(haus_history_sample(&st, back), "clock-back sample rejected");
    const gui_sample_t &b = l1[(back % 86400u) / 60u];
    CHECK((b.flags & SAMPLE_FLAG_STALE) == 0,
          "future timestamp marked stale");
}

/* ─── D: SPIFFS ring order ────────────────────────────────────────────────── */

static void test_ring_order(uint32_t start, uint32_t next) {
    SECTION("D1  newest 24 ring columns are loaded oldest-first");

    RollupColumn ring[24];
    memset(ring, 0, sizeof(ring));
    const size_t got = haus_history_ring_load_at(ring, 24, next - 1u);
    CHECK(got == 24, "ring load = %u, want 24", (unsigned)got);

    for (size_t i = 0; i < got; i++) {
        CHECK(ring[i].t == (start / 60u) + (uint32_t)i * 60u,
              "ring[%u].t = %u", (unsigned)i, (unsigned)ring[i].t);
        CHECK(ring[i].n[0] == 60, "ring[%u].n[0] = %u", (unsigned)i,
              (unsigned)ring[i].n[0]);
    }
}

/* ─── E: ring wrap ────────────────────────────────────────────────────────── */

static void test_ring_wrap(uint32_t start) {
    SECTION("D2  the 8 760-hour index overwrites the oldest slot");

    CHECK(haus_history_init(), "history init failed");
    const uint32_t base_hour = start / 3600u;
    const uint32_t count = HAUS_RING_HOURS + 24u;

    for (uint32_t i = 0; i < count; i++) {
        RollupColumn col;
        memset(&col, 0, sizeof(col));
        col.t = (base_hour + i) * 60u;
        col.n[0] = 1;
        col.mn[0] = col.mx[0] = col.sum[0] = (int32_t)i;
        CHECK(haus_history_ring_append(&col), "append %u failed", (unsigned)i);
    }

    RollupColumn out[24];
    memset(out, 0, sizeof(out));
    const uint32_t now = (base_hour + count - 1u) * 3600u + 59u;
    const size_t got = haus_history_ring_load_at(out, 24, now);
    CHECK(got == 24, "wrap load = %u, want 24", (unsigned)got);

    for (uint32_t i = 0; i < 24u; i++) {
        const uint32_t expected = count - 24u + i;
        CHECK(out[i].t == (base_hour + expected) * 60u,
              "wrap[%u].t = %u, want %u", (unsigned)i, (unsigned)out[i].t,
              (unsigned)((base_hour + expected) * 60u));
        CHECK(out[i].sum[0] == (int32_t)expected,
              "wrap[%u].sum = %d, want %u", (unsigned)i, (int)out[i].sum[0],
              (unsigned)expected);
    }
}

/* ─── D3: boot restore ────────────────────────────────────────────────────── */

static void test_restore(uint32_t start, uint32_t next) {
    SECTION("D3  SD rollup restores L2 before the clock is valid");

    CHECK(haus_history_init(), "history init failed");
    const size_t restored = haus_history_restore(0); /* before NTP */
    CHECK(restored == (size_t)HAUS_L2_COLUMNS, "restored = %u, want %u",
          (unsigned)restored, (unsigned)HAUS_L2_COLUMNS);

    const uint32_t slot = (start / 3600u) % (uint32_t)HAUS_L2_COLUMNS;
    const RollupColumn &c = haus_history_l2()[slot];
    CHECK(c.t == start / 60u, "restored t = %u, want %u", (unsigned)c.t,
          (unsigned)(start / 60u));
    CHECK(c.n[0] == 60, "restored n[0] = %u, want 60", (unsigned)c.n[0]);

    /* A second init clears L2; restore must refill it from the saved rollup. */
    CHECK(haus_history_init(), "history init failed");
    CHECK(haus_history_l2()[slot].t == 0, "init did not clear L2");
    CHECK(haus_history_restore(next) == (size_t)HAUS_L2_COLUMNS,
          "second restore failed");

    /* A mid-day boot replays today's day file into L1 as well. */
    CHECK(haus_history_init(), "history init failed");
    CHECK(haus_history_restore(start + 12u * 3600u) ==
              (size_t)HAUS_L2_COLUMNS,
          "mid-day restore failed");
    CHECK(haus_history_l1_count() == HAUS_L1_SAMPLES,
          "L1 after day replay = %u", (unsigned)haus_history_l1_count());
    const uint32_t idx = (start % 86400u) / 60u;
    CHECK(haus_history_l1()[idx].epoch == start,
          "L1 replay first epoch = %u, want %u",
          (unsigned)haus_history_l1()[idx].epoch, (unsigned)start);
    CHECK(haus_history_l2()[slot].n[0] == 60,
          "day replay double-counted L2: n[0] = %u",
          (unsigned)haus_history_l2()[slot].n[0]);
}

/* ─── D4: CRC tail ────────────────────────────────────────────────────────── */

static void test_crc_tail(uint32_t start) {
    SECTION("D4  a corrupt 64-record block discards the day-file tail");

    CHECK(haus_history_init(), "history init failed");
    haus_history_test_corrupt_day(100); /* block 1 */
    const size_t restored = haus_history_restore(start + 12u * 3600u);
    CHECK(restored == (size_t)HAUS_L2_COLUMNS,
          "rollup restore = %u", (unsigned)restored);
    CHECK(haus_history_l1_count() == 64u,
          "L1 after corrupt block = %u, want 64",
          (unsigned)haus_history_l1_count());
}

/* ─── E: time edges ───────────────────────────────────────────────────────── */

static uint32_t local_midnight(int year, int mon, int mday) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = mday;
    tm.tm_isdst = -1;
    return (uint32_t)mktime(&tm);
}

static void test_time_edges(void) {
    SECTION("E1  local midnight through leap day and DST");

    CHECK(haus_history_init(), "history init failed");
    const uint32_t leap0 = local_midnight(2024, 2, 29);
    const uint32_t leap1 = local_midnight(2024, 3, 1);
    HausState st = fresh_state(leap0);
    CHECK(haus_history_sample(&st, leap0), "leap-day sample rejected");
    st = fresh_state(leap1);
    CHECK(haus_history_sample(&st, leap1), "next-day sample rejected");
    CHECK(strcmp(haus_history_last_day_path(), "/data_20240229.bin") == 0,
          "leap-day path = '%s'", haus_history_last_day_path());

    CHECK(haus_history_init(), "history init failed");
    const uint32_t spring0 = local_midnight(2024, 3, 31);
    const uint32_t spring1 = local_midnight(2024, 4, 1);
    CHECK((uint32_t)(spring1 - spring0) == 23u * 3600u,
          "spring day = %u s, want 23 h", (unsigned)(spring1 - spring0));
    st = fresh_state(spring0);
    CHECK(haus_history_sample(&st, spring0), "spring sample rejected");
    st = fresh_state(spring1);
    CHECK(haus_history_sample(&st, spring1), "post-spring sample rejected");
    CHECK(strcmp(haus_history_last_day_path(), "/data_20240331.bin") == 0,
          "spring path = '%s'", haus_history_last_day_path());

    CHECK(haus_history_init(), "history init failed");
    const uint32_t autumn0 = local_midnight(2024, 10, 27);
    const uint32_t autumn1 = local_midnight(2024, 10, 28);
    CHECK((uint32_t)(autumn1 - autumn0) == 25u * 3600u,
          "autumn day = %u s, want 25 h", (unsigned)(autumn1 - autumn0));
    st = fresh_state(autumn0);
    CHECK(haus_history_sample(&st, autumn0), "autumn sample rejected");
    st = fresh_state(autumn1);
    CHECK(haus_history_sample(&st, autumn1), "post-autumn sample rejected");
    CHECK(strcmp(haus_history_last_day_path(), "/data_20241027.bin") == 0,
          "autumn path = '%s'", haus_history_last_day_path());
}

/* ─── F: L2 ring wrap ─────────────────────────────────────────────────────── */

static void test_l2_wrap(uint32_t start) {
    SECTION("F1  the 744-column L2 ring overwrites its oldest hour");

    CHECK(haus_history_init(), "history init failed");
    for (uint32_t h = 0; h <= (uint32_t)HAUS_L2_COLUMNS; h++) {
        const uint32_t t = start + h * 3600u;
        HausState st = fresh_state(t);
        CHECK(haus_history_sample(&st, t), "hour %u rejected", (unsigned)h);
    }

    const uint32_t slot = (start / 3600u) % (uint32_t)HAUS_L2_COLUMNS;
    const uint32_t newest_t = (start / 60u) + (uint32_t)HAUS_L2_COLUMNS * 60u;
    const RollupColumn &c = haus_history_l2()[slot];
    CHECK(c.t == newest_t, "L2 slot t = %u, want %u", (unsigned)c.t,
          (unsigned)newest_t);
    CHECK(c.n[0] == 1, "L2 slot n[0] = %u, want 1", (unsigned)c.n[0]);
}

/* ─── G: write failure ────────────────────────────────────────────────────── */

static void test_write_failure(void) {
    SECTION("G1  failed SD writes do not stop the rollover");

    CHECK(haus_history_init(), "history init failed");
    haus_history_test_fail_writes(true);

    const uint32_t d0 = local_midnight(2024, 10, 14);
    const uint32_t d1 = local_midnight(2024, 10, 15);
    HausState st = fresh_state(d0);
    CHECK(haus_history_sample(&st, d0), "first sample rejected");
    st = fresh_state(d1);
    CHECK(haus_history_sample(&st, d1), "rollover sample rejected");

    CHECK(haus_history_sd_write_count() == 0,
          "write count = %u, want 0", (unsigned)haus_history_sd_write_count());
    CHECK(haus_history_l1_count() == 1,
          "L1 count = %u, want 1", (unsigned)haus_history_l1_count());
    CHECK(strcmp(haus_history_last_day_path(), "/data_20241014.bin") == 0,
          "failed-write path = '%s'", haus_history_last_day_path());

    haus_history_test_fail_writes(false);
}

int main(void) {
    CHECK(sizeof(gui_sample_t) == 24u, "gui_sample_t = %u",
          (unsigned)sizeof(gui_sample_t));
    CHECK(sizeof(RollupColumn) == 88u, "RollupColumn = %u",
          (unsigned)sizeof(RollupColumn));

    (void)setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();

    CHECK(haus_history_init(), "history init failed");
    const uint32_t start = local_midnight(2024, 10, 14);

    test_day(start);
    const uint32_t next = test_rollover(start);
    test_stale(next);
    test_stale_recovery(next);
    test_ring_order(start, next);
    test_restore(start, next);
    test_crc_tail(start);
    test_ring_wrap(start);
    test_time_edges();
    test_l2_wrap(start);
    test_write_failure();

    if (g_failures != 0u) {
        printf("%u/%u checks FAILED\n", (unsigned)g_failures,
               (unsigned)g_checks);
        return 1;
    }
    printf("%u checks passed\n", (unsigned)g_checks);
    return 0;
}
