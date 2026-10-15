/**
 * @file test_eeprom_log.cpp
 * @brief Tests for the EEPROM restore-point ring.
 *
 * Grouped by the failure each block is defending against:
 *
 *   A  virgin device
 *   B  sequential fill, no wrap
 *   C  wrap detection
 *   D  the VALID_MAX invariant — the ring must never become ambiguous
 *   E  torn writes at every byte offset (power cut mid-record)
 *   F  corruption must not look like erasure
 *   G  restore: boot marker, window walk, off-boundary, virgin
 *   H  randomised model check with power cuts
 *
 * Block H is the one that matters. A and B can be reasoned about on paper; H
 * compares the scan against a shadow model across thousands of randomised
 * append-and-cut sequences, which is the only practical way to find the case
 * where the empty arc stops being exactly one arc.
 *
 * Types: fixed-width only. `int` is 16-bit on the ATmega and 32-bit here, so
 * using it would make this harness disagree with the firmware for reasons that
 * have nothing to do with the algorithm.
 */

#include <stdio.h>
#include <stdint.h>
#include "eeprom_log.h"
#include "eeprom_mock.h"

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

static void reset_all(eeprom_mock_t* m, epl_state_t* st, epl_io_t* io)
{
    mock_init(m);
    *io = mock_io(m);
    epl_scan(io, st);
}

/* ─── A: blank start ───────────────────────────────────────────────────────── */

/*
 * A board that has never run this firmware is the one case where the restore has
 * nothing legitimate to read, and it is worth being thorough about it because
 * "blank" is not only 0xFF.
 *
 *   virgin      — AVR EEPROM erases to all-ones; this is the real first boot
 *   all-zero    — not an erase state, but a blank part that something wrote 0 to
 *   random      — EEPROM from unrelated prior firmware, or a part never erased
 *   partial     — a few good records followed by blank
 *
 * The safety property in every case is the same: **restore must never yield a
 * duty that was not deliberately written.** A wild permille driving a heater is
 * the worst outcome this module could produce, so it is asserted rather than
 * reasoned about.
 */
static void test_blank_start(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;
    uint16_t      i;

    SECTION("A1  virgin EEPROM (all 0xFF, the real erase state)");
    reset_all(&m, &st, &io);

    CHECK(st.count == 0u, "virgin count = %u, want 0", st.count);
    CHECK(st.anomaly == 0u, "virgin flagged anomalous");
    CHECK(epl_restore(&io, &st) == EPL_OFF_DUTY,
          "virgin restore = %u, want EPL_OFF_DUTY", epl_restore(&io, &st));
    CHECK(epl_slot_is_empty(&io, 0u) == 1u, "slot 0 should read as erased");

    /* And the first append must land on slot 0, not slot 1. */
    CHECK(epl_append(&io, &st, 400u, (uint8_t)0) == 0u,
          "first append did not use slot 0");
    epl_scan(&io, &st);
    CHECK(st.oldest == 0u, "after first write oldest = %u, want 0", st.oldest);
    CHECK(st.newest == 0u, "after first write newest = %u, want 0", st.newest);
    CHECK(st.count == 1u, "after first write count = %u, want 1", st.count);

    SECTION("A2  all-zero EEPROM");
    mock_init(&m);
    for (i = 0u; i < (uint16_t)EPL_EEPROM_BYTES; i++) mock_poke(&m, i, 0x00u);
    io = mock_io(&m);
    epl_scan(&io, &st);
    CHECK(st.count == 0u, "all-zero count = %u, want 0 (no record may validate)",
          st.count);
    CHECK(epl_restore(&io, &st) == EPL_OFF_DUTY,
          "all-zero restore = %u, want OFF", epl_restore(&io, &st));
    CHECK(epl_slot_is_empty(&io, 0u) == 0u,
          "all-zero slot must not count as erased");

    SECTION("A3  random EEPROM (prior firmware, or never erased)");
    {
        uint32_t seed = 0x2468ACE0u;
        uint8_t  false_accepts = 0u;

        for (i = 0u; i < (uint16_t)EPL_EEPROM_BYTES; i++) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            mock_poke(&m, i, (uint8_t)(seed & 0xFFu));
        }
        io = mock_io(&m);
        (void)epl_scan(&io, &st);

        /* Whatever the scan makes of it, the restore must be safe. The danger is
         * reading a "newest" out of an unlocatable ring and returning a random
         * permille, which is why epl_restore refuses on anomaly. */
        {
            uint16_t duty = epl_restore(&io, &st);
            CHECK(duty == EPL_OFF_DUTY,
                  "random EEPROM restored duty %u — must never honour a "
                  "ring it could not locate", duty);
        }

        /* The 1-in-256 false-accept rate is real and worth stating: with 256
         * random slots, roughly one record will pass the checksum by chance. */
        for (i = 0u; i < EPL_SLOTS; i++) {
            if (epl_slot_is_empty(&io, i) == 1u) false_accepts++;
        }
        printf("     random EEPROM: %u of %u slots happened to be all-0xFF\n",
               (unsigned)false_accepts, (unsigned)EPL_SLOTS);
    }

    SECTION("A4  partial log followed by blank");
    reset_all(&m, &st, &io);
    epl_append(&io, &st, 275u, (uint8_t)0);
    epl_append(&io, &st, 290u, (uint8_t)0);
    epl_scan(&io, &st);

    CHECK(st.count == 2u, "partial count = %u, want 2", st.count);
    CHECK(st.wrapped == 0u, "partial should not be wrapped");
    CHECK(st.oldest == 0u, "partial oldest = %u, want 0", st.oldest);
    CHECK(st.newest == 1u, "partial newest = %u, want 1", st.newest);
    /* Two heating records and no marker, so the window applies and the minimum
     * of both is the answer. */
    CHECK(epl_restore(&io, &st) == 275u, "partial restore = %u, want 275",
          epl_restore(&io, &st));
}

/* ─── B: sequential fill, no wrap ──────────────────────────────────────────── */

static void test_sequential_no_wrap(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;
    uint16_t      n;

    SECTION("B  sequential fill, no wrap");
    reset_all(&m, &st, &io);

    for (n = 0u; n < 5u; n++) {
        epl_append(&io, &st, (uint16_t)(200u + n), (uint8_t)0);
    }

    CHECK(st.wrapped == 0u, "wrapped before reaching the end");
    CHECK(st.oldest == 0u, "oldest = %u, want 0", st.oldest);
    CHECK(st.newest == 4u, "newest = %u, want 4", st.newest);
    CHECK(st.count == 5u, "count = %u, want 5", st.count);

    /* Re-scan from scratch must agree with the incrementally maintained state. */
    epl_scan(&io, &st);
    CHECK(st.wrapped == 0u, "rescan: wrapped");
    CHECK(st.oldest == 0u, "rescan: oldest = %u, want 0", st.oldest);
    CHECK(st.newest == 4u, "rescan: newest = %u, want 4", st.newest);
    CHECK(st.count == 5u, "rescan: count = %u, want 5", st.count);
}

/* ─── C: wrap detection ────────────────────────────────────────────────────── */

static void test_wrap(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;
    uint32_t      n;
    uint32_t      guard;

    SECTION("C  wrap detection");
    reset_all(&m, &st, &io);

    /*
     * Note this cannot be done by counting: EPL_VALID_MAX (240) is below the
     * slot count (256), so the ring never fills — every append past 240 frees
     * the oldest first. The newest slot cycles 0..255 indefinitely instead, and
     * the wrap is observed by watching for it rather than by filling up.
     */
    guard = 0u;
    while (!st.wrapped && guard < (uint32_t)EPL_SLOTS * 3u) {
        epl_append(&io, &st, (uint16_t)(300u + (n % 500u)), (uint8_t)0);
        n++;
        guard++;
    }

    CHECK(st.wrapped == 1u, "no wrap observed after %lu appends", (unsigned long)guard);

    /* The first wrap must land exactly at slot 0. */
    epl_scan(&io, &st);
    CHECK(st.wrapped == 1u, "rescan lost the wrap");
    CHECK(st.newest < st.oldest,
          "wrapped arc should have newest(%u) < oldest(%u)", st.newest, st.oldest);
    CHECK(st.count <= EPL_VALID_MAX,
          "count = %u above VALID_MAX = %u", st.count, EPL_VALID_MAX);
    CHECK(st.count < EPL_SLOTS,
          "count = %u reached the ambiguous full state", st.count);

    /* Keep going well past the wrap and confirm it stays coherent. */
    for (n = 0u; n < (uint32_t)EPL_SLOTS * 2u; n++) {
        epl_append(&io, &st, (uint16_t)(300u + (n % 500u)), (uint8_t)0);
    }

    {
        epl_state_t after;
        epl_scan(&io, &after);
        CHECK(after.wrapped == (uint8_t)(after.oldest > after.newest ? 1u : 0u),
              "after heavy wrapping: wrapped=%u oldest=%u newest=%u",
              after.wrapped, after.oldest, after.newest);
        CHECK(after.count <= EPL_VALID_MAX, "count = %u above VALID_MAX", after.count);
        CHECK(epl_restore(&io, &after) <= EPL_OFF_DUTY,
              "restore out of range: %u", epl_restore(&io, &after));
    }
}

/* ─── D: the VALID_MAX invariant ───────────────────────────────────────────── */

static void test_valid_max_invariant(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;
    uint32_t      n;
    uint16_t      empty_slots = 0u;

    SECTION("D  VALID_MAX invariant over a long run");
    reset_all(&m, &st, &io);

    /*
     * Ten full revolutions. After every single append the ring must satisfy the
     * one property the whole design rests on: at least one slot is erased, so
     * the empty region is exactly one arc and "wrapped <=> first valid != 0"
     * holds.
     */
    for (n = 0u; n < (uint32_t)EPL_SLOTS * 10u; n++) {
        epl_append(&io, &st, (uint16_t)(100u + (n % 800u)), (uint8_t)(n & 1u));

        CHECK(st.count <= EPL_VALID_MAX,
              "iter %lu: count = %u above VALID_MAX = %u",
              (unsigned long)n, st.count, EPL_VALID_MAX);
        CHECK(st.anomaly == 0u, "iter %lu: anomaly set", (unsigned long)n);

        /* A boot marker must not be able to make the newest unidentifiable. */
        CHECK(epl_in_region(&st, st.newest) == 1u,
              "iter %lu: newest not inside its own region", (unsigned long)n);
    }

    empty_slots = (uint16_t)(EPL_SLOTS - st.count);
    CHECK(empty_slots > 0u,
          "long run left %u empty slots; the scan would be ambiguous", empty_slots);

    /* Re-scan and confirm the maintained state survived a reboot. */
    {
        epl_state_t after;
        epl_scan(&io, &after);
        CHECK(after.count == st.count, "rescan count %u != %u", after.count, st.count);
        CHECK(after.newest == st.newest, "rescan newest %u != %u", after.newest, st.newest);
        CHECK(after.oldest == st.oldest, "rescan oldest %u != %u", after.oldest, st.oldest);
        CHECK(after.wrapped == st.wrapped, "rescan wrapped %u != %u",
              after.wrapped, st.wrapped);
    }
}

/* ─── E: torn writes ───────────────────────────────────────────────────────── */

/*
 * Power cut part-way through every record. The checksum byte is written last, so
 * a cut anywhere in bytes 0..2 must leave the slot invalid — a hole, never a
 * false valid record and never something the scan mistakes for erasure.
 */
static void test_torn_writes(void)
{
    uint16_t cut;

    SECTION("E  torn writes at every byte offset");

    for (cut = 0u; cut <= (uint16_t)4u; cut++) {
        eeprom_mock_t m;
        epl_state_t   st, after;
        epl_io_t      io;
        uint16_t      written;
        uint32_t      n;

        mock_init(&m);
        io = mock_io(&m);
        epl_scan(&io, &st);

        /*
         * Write `cut` records cleanly, then arm a byte budget of exactly `cut`
         * so the collapse lands inside the next one. A record is 4 writes, so
         * budget = cut means cut bytes of that record land and the rest do not.
         *
         * cut = 0..3 -> torn: payload partly written, chk (written last) never
         *              arrives, so the slot must read invalid.
         * cut = 4    -> complete: nothing is cut at all.
         */
        for (n = 0u; n < (uint32_t)cut; n++) {
            epl_append(&io, &st, (uint16_t)(250u + n), (uint8_t)0);
        }
        written = st.count;

        mock_cut_after(&m, (uint16_t)cut);
        epl_append(&io, &st, 999u, (uint8_t)0);

        if (cut == 4u) {
            /* Nothing was cut, so this one is just an ordinary record. */
            CHECK(m.powered_off == 0u, "cut=4: budget of 4 should not cut");
            epl_scan(&io, &after);
            CHECK(after.count == written + 1u,
                  "cut=4: complete record should be valid (%u vs %u)",
                  after.count, written + 1u);
            continue;
        }

        CHECK(m.powered_off == 1u, "cut=%u: mock never cut", cut);

        /* A cut before chk lands must not have produced a valid record. */
        epl_scan(&io, &after);
        CHECK(after.count == written,
              "cut=%u: torn record counted as valid (%u vs %u)",
              cut, after.count, written);

        if (cut == 0u) {
            /* Not one byte landed, so the slot is untouched and therefore still
             * erased. That is correct, not a hole: there is no partial record
             * to be confused with anything. */
            CHECK(epl_slot_is_empty(&io, (uint16_t)written) == 1u,
                  "cut=0: untouched slot should still be erased");
            CHECK(written == 0u, "cut=0 expected no prior records");
            continue;
        }

        CHECK(after.newest == (uint16_t)(written - 1u),
              "cut=%u: newest = %u, want the last intact record %u",
              cut, after.newest, written - 1u);

        /* At least one byte landed, so the slot is neither erased nor valid —
         * a corrupt hole. Conflating it with erasure would split the empty arc.
         */
        CHECK(epl_slot_is_empty(&io, (uint16_t)written) == 0u,
              "cut=%u: partially written slot reported as erased", cut);
    }
}

/* ─── F: corruption is not erasure ─────────────────────────────────────────── */

static void test_corrupt_not_erased(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;
    epl_rec_t     r;
    epl_raw_t     raw;

    SECTION("F  corruption must not look like erasure");
    reset_all(&m, &st, &io);
    epl_append(&io, &st, 350u, (uint8_t)0);
    epl_scan(&io, &st);

    CHECK(epl_slot_is_empty(&io, 0u) == 0u, "written slot reported as erased");

    /* Flip a payload byte: the record must stop being valid, but must NOT become
     * empty. Merging the two would split the empty arc in two. */
    mock_poke(&m, 0u, (uint8_t)(m.mem[0u] ^ 0x55u));
    raw.b[0] = m.mem[0];
    raw.b[1] = m.mem[1];
    raw.b[2] = m.mem[2];
    raw.b[3] = m.mem[3];
    r = epl_decode(&raw);
    CHECK(r.valid == 0u, "corrupted record still decodes as valid");
    CHECK(epl_slot_is_empty(&io, 0u) == 0u,
          "corrupted slot reported as erased — would split the empty arc");
}

/* ─── G: restore ───────────────────────────────────────────────────────────── */

static void test_restore_boot_marker(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;

    SECTION("G1  restore from a boot marker is O(1)");
    reset_all(&m, &st, &io);

    epl_append(&io, &st, 200u, (uint8_t)0);
    epl_append(&io, &st, 400u, (uint8_t)0);
    /* A boot marker carrying the resolved answer. */
    epl_append(&io, &st, 200u, (uint8_t)1);
    epl_scan(&io, &st);

    CHECK(epl_restore(&io, &st) == 200u,
          "restore = %u, want the boot marker's 200", epl_restore(&io, &st));

    /* The window below must not be consulted: if a newer-than-window record
     * disagrees, the marker still wins. */
    epl_scan(&io, &st);
    CHECK(st.newest == 2u, "newest = %u, want 2", st.newest);
}

static void test_restore_window(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;
    uint16_t      n;

    SECTION("G2  restore window walks back and takes the minimum");
    reset_all(&m, &st, &io);

    /* No boot marker, so the window applies. Duties fall, so the newest is already
     * the minimum and the window must not pull anything lower in. */
    for (n = 0u; n < 6u; n++) {
        epl_append(&io, &st, (uint16_t)(300u - (n * 20u)), (uint8_t)0);
    }
    epl_scan(&io, &st);

    /* 300,280,260,240,220,200 -> newest is 200 and it is also the min. */
    CHECK(epl_restore(&io, &st) == 200u, "window restore = %u, want 200",
          epl_restore(&io, &st));

    SECTION("G2b  the window pulls the minimum down when duties rise");
    reset_all(&m, &st, &io);

    /* Duties rise, so the oldest in the window is the minimum. */
    for (n = 0u; n < 5u; n++) {
        epl_append(&io, &st, (uint16_t)(200u + (n * 20u)), (uint8_t)0);
    }
    epl_scan(&io, &st);

    /* 200,220,240,260,280 -> min over all five is 200 */
    CHECK(epl_restore(&io, &st) == 200u, "rising-window restore = %u, want 200",
          epl_restore(&io, &st));

    SECTION("G3  an off record stops the walk");
    reset_all(&m, &st, &io);

    epl_append(&io, &st, 150u, (uint8_t)0);   /* older heat */
    epl_append(&io, &st, EPL_OFF_DUTY, (uint8_t)0);  /* explicit off */
    epl_append(&io, &st, 250u, (uint8_t)0);   /* newest heat */
    epl_scan(&io, &st);

    /* Walk from 250 hits the OFF record and stops; 150 is older and excluded. */
    CHECK(epl_restore(&io, &st) == 250u, "off-boundary restore = %u, want 250",
          epl_restore(&io, &st));

    SECTION("G4  boot markers are transparent to the walk");
    reset_all(&m, &st, &io);

    epl_append(&io, &st, 210u, (uint8_t)0);
    epl_append(&io, &st, 500u, (uint8_t)0);
    epl_append(&io, &st, 220u, (uint8_t)0);
    epl_append(&io, &st, 600u, (uint8_t)1);   /* marker in the middle of the walk */
    epl_append(&io, &st, 230u, (uint8_t)0);
    epl_scan(&io, &st);

    /* newest is a plain record, so the window runs: 230, skip marker, 220, 500,
     * 210 -> min = 210 */
    CHECK(epl_restore(&io, &st) == 210u, "marker-transparent restore = %u, want 210",
          epl_restore(&io, &st));

    SECTION("G5  window is bounded, not open-ended");
    reset_all(&m, &st, &io);

    /* 20 heating records; only the last 10 are in scope. The oldest is very low
     * and must NOT be visible. */
    for (n = 0u; n < 20u; n++) {
        epl_append(&io, &st, (uint16_t)(500u - n), (uint8_t)0);
    }
    epl_scan(&io, &st);

    /* records 19..10 are in the window: duties 481..490, min = 481.
     * Record 9 (duty 491) is the 11th back and out of scope. */
    CHECK(epl_restore(&io, &st) == 481u,
          "bounded window restore = %u, want 481 (record 9's 491 must not show)",
          epl_restore(&io, &st));
}

/* ─── H: randomised model check with power cuts ────────────────────────────── */

static uint32_t rng_state = 0x13572468u;

static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/*
 * Shadow model: the test tracks what the newest record should be, independently
 * of the implementation, and compares after every operation — including after a
 * simulated power cut at a random byte offset.
 */
static void test_randomised(void)
{
    uint32_t round;

    SECTION("H  randomised append/cut vs shadow model");

    for (round = 0u; round < 2000u; round++) {
        eeprom_mock_t m;
        epl_state_t   st, after;
        epl_io_t      io;
        uint16_t      duty;
        uint8_t       boot;
        uint16_t      ops_before_cut;
        uint32_t      i;

        mock_init(&m);
        io = mock_io(&m);
        epl_scan(&io, &st);

        /* A burst of clean appends, then a cut somewhere inside one of them. */
        ops_before_cut = (uint16_t)(rng() % 12u);

        for (i = 0u; i < (uint32_t)ops_before_cut; i++) {
            duty = (uint16_t)(1u + (rng() % (EPL_OFF_DUTY - 1u)));  /* a heating duty */
            boot = (uint8_t)(rng() & 1u);
            epl_append(&io, &st, duty, boot);
        }

        /* Arm a cut part-way through the next record. */
        if ((rng() & 1u) != 0u) {
            mock_cut_after(&m, (uint16_t)(rng() % 5u));
        }

        duty = (uint16_t)(1u + (rng() % (EPL_OFF_DUTY - 1u)));
        boot = (uint8_t)(rng() & 1u);
        epl_append(&io, &st, duty, boot);

        /* Whatever happened, the ring must remain scannable and unambiguous. */
        CHECK(epl_scan(&io, &after) == 0u,
              "round %lu: scan reported an anomaly", (unsigned long)round);
        CHECK(after.count <= EPL_VALID_MAX,
              "round %lu: count %u above VALID_MAX", (unsigned long)round, after.count);
        CHECK(after.count < EPL_SLOTS,
              "round %lu: ring reached the ambiguous full state", (unsigned long)round);

        /* The valid region must be exactly one arc: old and new must bracket it
         * consistently. If the empty space were ever split in two, this fails.
         *
         * The wrap test is oldest > newest — NOT oldest != 0. An un-wrapped arc
         * moves off slot 0 as soon as the oldest slot is erased. */
        if (after.count > 0u) {
            CHECK(after.wrapped == (uint8_t)(after.oldest > after.newest ? 1u : 0u),
                  "round %lu: wrapped=%u but oldest=%u newest=%u",
                  (unsigned long)round, after.wrapped, after.oldest, after.newest);
            CHECK(epl_in_region(&after, after.newest) == 1u,
                  "round %lu: newest outside the region", (unsigned long)round);
            CHECK(epl_in_region(&after, after.oldest) == 1u,
                  "round %lu: oldest outside the region", (unsigned long)round);

            /* Per-slot invariants.
             *
             *   inside the arc  -> must not be erased. An erased slot inside the
             *      region would mean the arc was not detected correctly.
             *
             *   outside the arc -> must not decode valid. It may legitimately be
             *      a torn write from the power cut rather than erased, so
             *      "is erased" is too strong — but at most one such hole can
             *      exist, because only the single in-flight append could have
             *      been interrupted.
             */
            {
                uint16_t s;
                uint16_t holes = 0u;

                for (s = 0u; s < EPL_SLOTS; s++) {
                    uint8_t in = epl_in_region(&after, s);
                    epl_raw_t raw;
                    uint8_t  j;

                    for (j = 0u; j < (uint8_t)EPL_REC_SIZE; j++) {
                        raw.b[j] = m.mem[(uint16_t)(s * EPL_REC_SIZE + j)];
                    }

                    if (in) {
                        CHECK(epl_slot_is_empty(&io, s) == 0u,
                              "round %lu: slot %u inside the arc is erased",
                              (unsigned long)round, s);
                    } else {
                        CHECK(epl_decode(&raw).valid == 0u,
                              "round %lu: slot %u outside the arc decodes valid",
                              (unsigned long)round, s);
                        if (epl_slot_is_empty(&io, s) == 0u) holes++;
                    }
                }

                CHECK(holes <= 1u,
                      "round %lu: %u non-erased slots outside the arc; only a "
                      "single torn write is possible",
                      (unsigned long)round, holes);
            }
        }
    }
}

/* ─── Long soak: repeated reboots across a wrapping ring ───────────────────── */

static void test_reboot_soak(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;
    uint32_t      boot;
    uint16_t      duty = 320u;
    uint8_t       ok = 1u;

    SECTION("H2  reboot soak across many wraps");

    mock_init(&m);
    io = mock_io(&m);
    epl_scan(&io, &st);

    /* Each "boot" restores, folds the answer into a boot marker, then appends —
     * the real usage pattern. 400 boots covers the ring several times over. */
    for (boot = 0u; boot < 400u; boot++) {
        epl_state_t after;
        uint16_t    restored = epl_restore(&io, &st);

        /* Duty drifts as the upstream controller would. */
        duty = (uint16_t)(200u + (uint32_t)(boot % 600u));
        if (restored > EPL_OFF_DUTY) restored = EPL_OFF_DUTY;

        epl_scan(&io, &after);
        st = after;

        if (epl_append(&io, &st, duty, (uint8_t)0) == 0xFFFFu) ok = 0u;

        /* The folded marker is what the *next* boot reads. */
        epl_append(&io, &st, restored, (uint8_t)1);

        if (epl_scan(&io, &st) != 0u) ok = 0u;
        if (st.count > EPL_VALID_MAX) ok = 0u;
    }

    CHECK(ok == 1u, "reboot soak broke an invariant");
    CHECK(st.count < EPL_SLOTS, "reboot soak filled the ring");
}

/* ─── I: wear ──────────────────────────────────────────────────────────────── */

/*
 * Wear is the reason there is no counter cell, and concentration — not the raw
 * date — is the property that makes a cell the thing that fails first. That is
 * an emergent property of the append/reuse pattern, so it has to be measured
 * rather than asserted in a comment.
 *
 * The run below models ten years of the nominal 8 h heartbeat:
 *     10 y * 365 d * 3 writes/day = 10 950 records
 * Each record is 4 bytes written, some of which are no-ops where the duty did
 * not change, so the real figure is below that.
 */
static void test_wear_distribution(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;
    uint32_t      n;
    uint32_t      worst;
    uint32_t      skew;
    uint16_t      used;

    SECTION("I1  wear spread over 10 years of 8 h writes");
    reset_all(&m, &st, &io);

    for (n = 0u; n < 10950u; n++) {
        /* Duty changes every few records, as the upstream controller would, so
         * some bytes legitimately skip the update() no-op path. */
        uint16_t duty = (uint16_t)(200u + ((n % 7u) * 50u));
        uint8_t  boot = (uint8_t)(n & 1u);

        if (epl_append(&io, &st, duty, boot) == 0xFFFFu) {
            CHECK(0u, "append failed at record %lu", (unsigned long)n);
            break;
        }
    }

    worst = mock_max_cell_writes(&m);
    skew  = mock_wear_skew(&m);
    used  = mock_cells_used(&m);

    printf("     total=%lu  worst cell=%lu (addr %u)  cells used=%u/%u  skew=%lu/1000\n",
           (unsigned long)mock_total_writes(&m), (unsigned long)worst,
           (unsigned)mock_max_cell_addr(&m), (unsigned)used,
           (unsigned)EPL_EEPROM_BYTES, (unsigned long)skew);

    CHECK(mock_wear_exceeded(&m) == 0u,
          "a cell reached the %lu-cycle datasheet limit",
          (unsigned long)MOCK_WEAR_LIMIT);

    /* The whole point: no cell should come close. Ten years of heartbeats put
     * 10950 records through the ring; each record costs ~8 byte writes (3
     * payload + 4 erase + 1 reserved), spread over 256 slots, so the expected
     * worst case is ~85 writes. At the datasheet's 100 000 that is over a
     * thousand years. */
    CHECK(worst < (uint32_t)200u,
          "worst cell at %lu writes is far above the ~85 expected from an even ring",
          (unsigned long)worst);

    /* Every cell in the ring should have been used — a design that leaves cells
     * idle while others wear out is concentrating in a different way. */
    CHECK(used > (uint16_t)(EPL_EEPROM_BYTES - 8u),
          "only %u of %u cells used; writes are not spread",
          (unsigned)used, (unsigned)EPL_EEPROM_BYTES);

    /* Even spread scores 1000. Anything much above means a hot cell. */
    CHECK(skew < (uint32_t)3000u,
          "wear skew %lu/1000 indicates concentration", (unsigned long)skew);
}

/*
 * The rejected alternative, measured rather than argued about.
 *
 * A 2-byte monotonic counter makes "where is the newest record" pure arithmetic
 * and needs no scan at all — which is genuinely better in every respect except
 * one: it takes *every* write. Ten years of heartbeats puts one cell at ~11 000
 * cycles while the ring cells are at ~43. It survives ten years on the datasheet
 * figure, and it is still the wrong design, because the day the cadence changes
 * (the duty-change guard permits 24 writes/day) that one cell is the thing that
 * fails, and its failure takes down the whole chain.
 *
 * Keeping this test means the decision is re-checked rather than remembered.
 */
static void test_counter_cell_would_concentrate(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;
    uint32_t      n;
    uint32_t      counter_cell;
    uint32_t      ring_worst;
    uint16_t      addr;

    SECTION("I2  a counter cell would concentrate wear (rejected design)");
    reset_all(&m, &st, &io);

    /* Same ten years of records, then charge a hypothetical fixed counter cell for
     * every append — which is what the counter design would actually cost,
     * since every write bumps it.
     *
     * The ring's worst cell is measured BEFORE the counter is charged, because
     * cell 0 is also a record slot and would otherwise mask the comparison. */
    for (n = 0u; n < 10950u; n++) {
        uint16_t duty = (uint16_t)(200u + ((n % 7u) * 50u));
        epl_append(&io, &st, duty, (uint8_t)(n & 1u));
    }

    ring_worst = mock_max_cell_writes(&m);

    /* The counter design, in full: one increment per record, for ten years. */
    for (n = 0u; n < 10950u; n++) {
        m.cell_writes[0]++;
        m.total_writes++;
    }

    counter_cell = (uint32_t)m.cell_writes[0];
    addr = mock_max_cell_addr(&m);

    printf("     counter cell=%lu  ring worst=%lu (addr %u)  ratio=%lu x\n",
           (unsigned long)counter_cell, (unsigned long)ring_worst,
           (unsigned)addr,
           (unsigned long)(ring_worst ? (counter_cell / ring_worst) : 0u));

    /* This is the comparison that decided the design. Both survive ten years,
     * but one of them has a cell carrying the entire design's endurance. */
    CHECK(counter_cell > (ring_worst * 10u),
          "counter cell should dominate; got %lu vs ring %lu",
          (unsigned long)counter_cell, (unsigned long)ring_worst);

    /* And it is the cell the whole log depends on: if address 0 dies, nothing
     * can be located any more. */
    CHECK(mock_max_cell_addr(&m) == 0u,
          "worst cell is %u, expected the counter cell at 0", (unsigned)addr);
}

/* ─── J: restore source reporting ───────────────────────────────────────────── */

/*
 * The duty alone cannot distinguish "came up off because that was the decision"
 * from "came up off because the log was empty / unreadable / corrupt". Those are
 * the same number and completely different states, and the status frames carry
 * the reason precisely because of it — so the reason is asserted here.
 */
static void test_restore_source(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;
    uint16_t      duty;
    uint8_t       why;

    SECTION("J1  source is reported for every path");

    /* virgin */
    reset_all(&m, &st, &io);
    (void)epl_restore_ex(&io, &st, &duty, &why);
    CHECK(why == EPL_SRC_VIRGIN, "virgin source = %u", why);
    CHECK(duty == EPL_OFF_DUTY, "virgin duty = %u", duty);

    /* marker */
    reset_all(&m, &st, &io);
    epl_append(&io, &st, 275u, (uint8_t)EPL_FLAG_BOOT);
    epl_scan(&io, &st);
    (void)epl_restore_ex(&io, &st, &duty, &why);
    CHECK(why == EPL_SRC_MARKER, "marker source = %u", why);
    CHECK(duty == 275u, "marker duty = %u, want 275", duty);

    /* window */
    reset_all(&m, &st, &io);
    epl_append(&io, &st, 240u, (uint8_t)0);
    epl_append(&io, &st, 260u, (uint8_t)0);
    epl_scan(&io, &st);
    (void)epl_restore_ex(&io, &st, &duty, &why);
    CHECK(why == EPL_SRC_WINDOW, "window source = %u", why);
    CHECK(duty == 240u, "window duty = %u, want 240", duty);

    /* a deliberate off is attributed to the newest decision, not the window */
    reset_all(&m, &st, &io);
    epl_append(&io, &st, 300u, (uint8_t)0);
    epl_append(&io, &st, EPL_OFF_DUTY, (uint8_t)0);
    epl_scan(&io, &st);
    (void)epl_restore_ex(&io, &st, &duty, &why);
    CHECK(why == EPL_SRC_MARKER, "deliberate off source = %u, want MARKER", why);
    CHECK(duty == EPL_OFF_DUTY, "deliberate off duty = %u", duty);

    SECTION("J2  a corrupt newest record falls back, and says so");
    reset_all(&m, &st, &io);
    epl_append(&io, &st, 300u, (uint8_t)0);
    epl_append(&io, &st, 320u, (uint8_t)0);
    epl_scan(&io, &st);
    CHECK(st.newest == 1u, "precondition: newest = %u, want 1", st.newest);

    /* Tear the newest record's payload, leaving its checksum stale — the
     * signature of a power cut before the checksum byte landed.
     *
     * The scan skips it rather than treating it as erased, so the previous
     * record becomes the newest. That is the designed behaviour: the hole is a
     * hole, not a boundary, and the arc stays contiguous.
     *
     * Note the source here is WINDOW, not INVALID. EPL_SRC_INVALID guards a
     * newest that fails to decode, which cannot happen on this path at all —
     * the scan only ever designates slots that decoded. It is defensive against
     * the EEPROM changing between scan and restore, which single-threaded
     * firmware cannot do, and it is left in rather than removed because it is
     * the difference between returning OFF and returning a random permille if
     * that ever stops being true. */
    mock_poke(&m, (uint16_t)(st.newest * EPL_REC_SIZE),
              (uint8_t)(m.mem[(uint16_t)(st.newest * EPL_REC_SIZE)] ^ 0xFFu));

    epl_scan(&io, &st);
    CHECK(st.count == 1u, "after tearing the newest, count = %u, want 1", st.count);
    CHECK(st.newest == 0u, "newest = %u, want the previous record at 0", st.newest);
    CHECK(st.anomaly == 0u, "a single hole must not look anomalous");

    (void)epl_restore_ex(&io, &st, &duty, &why);
    CHECK(why == EPL_SRC_WINDOW, "torn-newest source = %u, want WINDOW", why);
    CHECK(duty == 300u, "torn-newest restored %u, want the previous 300", duty);

    SECTION("J3  an anomalous ring is reported and never honoured");
    {
        /* Fill every slot, writing straight through the io interface rather than
         * via epl_append. Going through append cannot produce this state — the
         * VALID_MAX guard erases before the cap is reached, which is the whole
         * point of the guard. Writing past it is the only way to check that the
         * scan actually detects the condition the guard is there to prevent,
         * rather than silently returning some arbitrary slot. */
        uint16_t s;

        mock_init(&m);
        io = mock_io(&m);

        for (s = 0u; s < EPL_SLOTS; s++) {
            epl_raw_t raw;
            uint16_t base = (uint16_t)(s * EPL_REC_SIZE);
            uint8_t  j;

            epl_encode(&raw, (uint16_t)(100u + s), (uint8_t)0);
            for (j = 0u; j < (uint8_t)EPL_REC_SIZE; j++) {
                io.wr(io.ctx, (uint16_t)(base + j), raw.b[j]);
            }
        }

        CHECK(epl_scan(&io, &st) != 0u, "100%% full ring should report non-zero");
        CHECK(st.anomaly == 1u, "100%% full ring did not raise the anomaly flag");

        (void)epl_restore_ex(&io, &st, &duty, &why);
        CHECK(why == EPL_SRC_ANOMALY, "anomalous source = %u, want ANOMALY", why);
        CHECK(duty == EPL_OFF_DUTY,
              "anomalous ring restored duty %u — must be OFF, never a random slot",
              duty);

        /* And the append path must refuse rather than scribble into a ring it
         * cannot locate. That is what stops a recoverable anomaly becoming a
         * permanent one. */
        CHECK(epl_append(&io, &st, 400u, (uint8_t)0) == 0xFFFFu,
              "append into an anomalous ring was accepted");
    }
}


/* ─── K: the PWM fault marker in the ring ──────────────────────────────────── */

/*
 * The .noinit escalation latch is cleared by any power-on reset, so a PWM
 * failure followed by a power blip would leave no trace and could not be
 * reported. The ring carries EPL_FLAG_PWM_FAIL for exactly that case, and it has
 * to survive three things: the reboot the escalation triggers, a power cut
 * mid-write, and the restore walk.
 */
static void test_fault_marker(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;
    uint16_t      duty;
    uint8_t       why;

    SECTION("K1  a fault record is readable after a reboot");
    reset_all(&m, &st, &io);
    epl_append(&io, &st, 300u, (uint8_t)0);
    epl_append(&io, &st, 300u, (uint8_t)EPL_FLAG_PWM_FAIL);
    epl_scan(&io, &st);

    CHECK(st.count == 2u, "fault record not counted (%u)", st.count);

    /* A reboot: drop all volatile state and rescan, exactly as setup() does. */
    epl_scan(&io, &st);
    CHECK(st.count == 2u, "fault record lost across a rescan (%u)", st.count);
    CHECK(st.newest == 1u, "fault record is not the newest (%u)", st.newest);

    /* And the reader can find it. */
    {
        epl_raw_t raw;
        epl_rec_t rec;
        uint8_t  j;
        for (j = 0u; j < (uint8_t)EPL_REC_SIZE; j++) {
            raw.b[j] = m.mem[(uint16_t)(st.newest * EPL_REC_SIZE + j)];
        }
        rec = epl_decode(&raw);
        CHECK(rec.valid == 1u, "fault record did not decode");
        CHECK((rec.flags & EPL_FLAG_PWM_FAIL) != 0u, "fault flag not set on read");
    }

    SECTION("K2  a fault record is skipped by the restore walk");
    /* Its duty was in force while the output path was dead, so it never reached
     * the load. It must not be treated as an observation of what the heater was
     * doing — honouring it would mean restoring a value known not to have been
     * delivered. */
    reset_all(&m, &st, &io);
    epl_append(&io, &st, 600u, (uint8_t)0);                          /* real    */
    epl_append(&io, &st, 600u, (uint8_t)EPL_FLAG_PWM_FAIL);          /* NOT real */
    epl_scan(&io, &st);

    /* The walk skips the fault record, so it falls back to the record below it
     * rather than reporting 600 from a duty that was never delivered. */
    CHECK(epl_restore(&io, &st) == 600u,
          "with only a real and a fault record, restore = %u",
          epl_restore(&io, &st));

    SECTION("K3  a fault record cannot pull the minimum down");
    reset_all(&m, &st, &io);
    epl_append(&io, &st, 400u, (uint8_t)0);
    epl_append(&io, &st, 400u, (uint8_t)0);
    epl_append(&io, &st, 50u,  (uint8_t)EPL_FLAG_PWM_FAIL);   /* undelivered */
    epl_scan(&io, &st);

    /* 50 would win the minimum if the fault record counted. */
    CHECK(epl_restore(&io, &st) == 400u,
          "an undelivered duty of 50 was restored (%u) instead of 400",
          epl_restore(&io, &st));

    SECTION("K4  a torn fault record is rejected, not half-believed");
    reset_all(&m, &st, &io);
    epl_append(&io, &st, 300u, (uint8_t)0);
    epl_append(&io, &st, 300u, (uint8_t)EPL_FLAG_PWM_FAIL);
    epl_scan(&io, &st);

    /* Cut power inside the fault record's payload. */
    {
        uint16_t written = st.count;
        epl_state_t after;

        mock_cut_after(&m, 2u);
        epl_append(&io, &st, 300u, (uint8_t)EPL_FLAG_PWM_FAIL);
        CHECK(m.powered_off == 1u, "mock never cut");

        epl_scan(&io, &after);
        CHECK(after.count == written,
              "a torn fault record was counted as valid (%u vs %u)",
              after.count, written);
        CHECK(epl_slot_is_empty(&io, (uint16_t)written) == 0u,
              "torn fault slot reported as erased");
    }

    SECTION("K5  the flag and the boot flag coexist");
    reset_all(&m, &st, &io);
    epl_append(&io, &st, 350u, (uint8_t)(EPL_FLAG_BOOT | EPL_FLAG_PWM_FAIL));
    epl_scan(&io, &st);
    (void)epl_restore_ex(&io, &st, &duty, &why);
    CHECK(why == EPL_SRC_MARKER, "combined flags gave source %u", why);
    CHECK(duty == 350u, "combined flags restored %u, want 350", duty);
}


/* ─── L: the format tag, and why it replaces a manual erase ───────────────── */

/*
 * The question "when do we need to erase the EEPROM" has a short answer: almost
 * never. Changing the record layout and bumping EPL_FORMAT makes every existing
 * record fail to decode, so the ring reads as virgin and the node starts clean.
 *
 * Without the tag this would be unsafe rather than merely inconvenient. A
 * "reserved, must be 0" byte catches nothing, because old records carry 0 there
 * too. And a record that survives the checksum by accident is not a remote
 * prospect: at 1-in-256 per slot, near-certain across 256 slots. A phantom
 * record carries an invented duty, and the restore would put it on the heater.
 */
static void test_format_tag(void)
{
    epl_raw_t raw;
    epl_rec_t rec;

    SECTION("L1  a record of this format decodes");
    epl_encode(&raw, 400u, (uint8_t)0);
    CHECK(raw.b[2] == EPL_FORMAT, "format tag = 0x%02X, want 0x%02X",
          raw.b[2], EPL_FORMAT);
    rec = epl_decode(&raw);
    CHECK(rec.valid == 1u, "current-format record did not decode");

    SECTION("L2  a record from any other layout is rejected, not misread");
    {
        uint16_t old;

        for (old = 0u; old < 256u; old++) {
            epl_raw_t legacy = raw;
            legacy.b[2] = (uint8_t)old;
            rec = epl_decode(&legacy);
            if ((uint8_t)old != EPL_FORMAT) {
                CHECK(rec.valid == 0u,
                      "legacy format tag 0x%02X decoded as valid", (unsigned)old);
            }
        }
    }

    SECTION("L3  a whole ring in a foreign format reads as virgin, not as data");
    {
        eeprom_mock_t m;
        epl_state_t   st;
        epl_io_t      io;
        uint16_t      slot;

        /* Write a plausible-looking ring, then stamp every slot with a foreign
         * format tag — exactly what a firmware update would leave behind. */
        reset_all(&m, &st, &io);
        for (slot = 0u; slot < 40u; slot++) {
            epl_append(&io, &st, (uint16_t)(150u + slot), (uint8_t)0);
        }
        epl_scan(&io, &st);
        CHECK(st.count == 40u, "precondition: %u records", st.count);

        for (slot = 0u; slot < 40u; slot++) {
            mock_poke(&m, (uint16_t)(slot * EPL_REC_SIZE + 2u), 0x00u);
        }
        epl_scan(&io, &st);

        CHECK(st.count == 0u,
              "foreign-format ring reported %u records instead of 0", st.count);
        CHECK(st.anomaly == 0u, "foreign-format ring flagged anomalous — it is not");
        CHECK(epl_restore(&io, &st) == EPL_OFF_DUTY,
              "foreign-format ring restored %u, want OFF", epl_restore(&io, &st));

        /* And the node is usable immediately, which is the whole point. */
        CHECK(epl_append(&io, &st, 275u, (uint8_t)EPL_FLAG_BOOT) != 0xFFFFu,
              "could not append to a foreign-format ring");
        epl_scan(&io, &st);
        CHECK(st.count == 1u, "count after refilling = %u, want 1", st.count);
    }
}

/* ─── M: erase and anomaly are different operations ────────────────────────── */

/*
 * These were once one command named "anomaly" which erased the log to 0xFF — and
 * an erased ring is VIRGIN, not anomalous, so it exercised the wrong branch while
 * appearing to work. Both are asserted here so they cannot be conflated again.
 */
static void test_erase_vs_anomaly(void)
{
    eeprom_mock_t m;
    epl_state_t   st;
    epl_io_t      io;
    uint16_t      slot;

    SECTION("M1  erasing every slot gives VIRGIN, not an anomaly");
    reset_all(&m, &st, &io);
    for (slot = 0u; slot < 20u; slot++) {
        epl_append(&io, &st, (uint16_t)(200u + slot), (uint8_t)0);
    }
    epl_scan(&io, &st);
    CHECK(st.count == 20u, "precondition: %u records", st.count);

    for (slot = 0u; slot < EPL_SLOTS; slot++) epl_erase_slot(&io, slot);
    epl_scan(&io, &st);

    CHECK(st.count == 0u, "erased ring count = %u, want 0", st.count);
    CHECK(st.anomaly == 0u,
          "erased ring flagged anomalous — there is a 256-slot empty arc, so it "
          "is plainly locatable");
    CHECK(epl_restore(&io, &st) == EPL_OFF_DUTY,
          "erased ring restored %u, want OFF", epl_restore(&io, &st));

    SECTION("M2  every slot valid gives the anomaly, and is never honoured");
    mock_init(&m);
    io = mock_io(&m);
    for (slot = 0u; slot < EPL_SLOTS; slot++) {
        epl_raw_t raw;
        uint16_t base = (uint16_t)(slot * EPL_REC_SIZE);
        uint8_t  j;
        epl_encode(&raw, (uint16_t)(100u + slot), (uint8_t)0);
        for (j = 0u; j < (uint8_t)EPL_REC_SIZE; j++) {
            io.wr(io.ctx, (uint16_t)(base + j), raw.b[j]);
        }
    }
    epl_scan(&io, &st);

    CHECK(st.anomaly == 1u, "full ring did not raise the anomaly flag");
    CHECK(epl_restore(&io, &st) == EPL_OFF_DUTY,
          "full ring restored a duty out of an unlocatable log");
    CHECK(epl_append(&io, &st, 300u, (uint8_t)0) == 0xFFFFu,
          "append into an unlocatable ring was accepted");

    SECTION("M3  an erase recovers it completely");
    /* The operator action after a test round. This has to actually restore the
     * node, not just silence the symptom: in the anomalous state epl_append()
     * refuses, so the duty stops being persisted entirely and the next power cut
     * would restore OFF. An erase that left the ring unwritable would be worse
     * than no erase at all. */
    for (slot = 0u; slot < EPL_SLOTS; slot++) epl_erase_slot(&io, slot);
    epl_scan(&io, &st);

    CHECK(st.count == 0u, "after erase count = %u, want 0", st.count);
    CHECK(st.anomaly == 0u, "after erase the anomaly flag is still set");
    CHECK(epl_restore(&io, &st) == EPL_OFF_DUTY,
          "after erase restore = %u, want OFF", epl_restore(&io, &st));

    CHECK(epl_append(&io, &st, 275u, (uint8_t)EPL_FLAG_BOOT) != 0xFFFFu,
          "append refused after an erase — node still degraded");

    epl_scan(&io, &st);
    CHECK(st.count == 1u, "count after refilling = %u, want 1", st.count);
    CHECK(st.anomaly == 0u, "anomaly came back after refilling");
    CHECK(epl_restore(&io, &st) == 275u,
          "after erase+refill restore = %u, want 275", epl_restore(&io, &st));

    SECTION("M4  erase then write, repeatedly, never degrades");
    /* Enough cycles that a wear- or wrap-related leak in the cap would show. */
    {
        uint32_t n;
        reset_all(&m, &st, &io);
        for (n = 0u; n < 2000u; n++) {
            if (epl_append(&io, &st, (uint16_t)(100u + (n % 800u)),
                           (uint8_t)0) == 0xFFFFu) {
                CHECK(0u, "append failed at %lu", (unsigned long)n);
                break;
            }
            if ((n % 250u) == 0u) {
                uint16_t k;
                for (k = 0u; k < EPL_SLOTS; k++) epl_erase_slot(&io, k);
                epl_scan(&io, &st);
            }
        }
        epl_scan(&io, &st);
        CHECK(st.anomaly == 0u, "repeated erase/write cycles degraded the ring");
        CHECK(st.count <= EPL_VALID_MAX, "count %u above VALID_MAX", st.count);
    }
}

/* ─── main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("eeprom_log ring tests\n");
    printf("  slots=%u  valid_max=%u  rec_size=%u  window=%u  wear_limit=%lu\n\n",
           (unsigned)EPL_SLOTS, (unsigned)EPL_VALID_MAX,
           (unsigned)EPL_REC_SIZE, (unsigned)EPL_RESTORE_WINDOW,
           (unsigned long)MOCK_WEAR_LIMIT);

    test_blank_start();
    test_sequential_no_wrap();
    test_wrap();
    test_valid_max_invariant();
    test_torn_writes();
    test_corrupt_not_erased();
    test_restore_boot_marker();
    test_restore_window();
    test_restore_source();
    test_randomised();
    test_fault_marker();
    test_format_tag();
    test_erase_vs_anomaly();
    test_reboot_soak();
    test_wear_distribution();
    test_counter_cell_would_concentrate();

    printf("\n%lu checks, %lu failures\n",
           (unsigned long)g_checks, (unsigned long)g_failures);
    return (g_failures == 0u) ? 0 : 1;
}