/**
 * @file eeprom_log.cpp
 * @brief EEPROM restore-point ring. Pure C, no Arduino dependency.
 *
 * Compiled unchanged for the ATmega328p and for the host test, so the code the
 * tests exercise is byte-for-byte the code that runs on the board.
 *
 * See eeprom_log.h for the design and the invariant that makes the counter
 * cell unnecessary.
 */

#include "eeprom_log.h"

/* ─── Checksum ─────────────────────────────────────────────────────────────── */

static uint8_t rotl8(uint8_t v)
{
    return (uint8_t)((uint8_t)(v << 1) | (uint8_t)(v >> 7));
}

/* Rot-fold rather than plain XOR: XOR alone is blind to swapping word_lo and
 * word_hi, which turns duty 800 into 8192 and leaves the checksum unchanged. */
static uint8_t chk_of(uint16_t word, uint8_t flags)
{
    uint8_t c = (uint8_t)(0x5Au ^ flags);
    c = rotl8(c);
    c = (uint8_t)(c ^ (uint8_t)(word & 0xFFu));
    c = rotl8(c);
    c = (uint8_t)(c ^ (uint8_t)((word >> 8) & 0xFFu));
    return c;
}

/* ─── Record codec ─────────────────────────────────────────────────────────── */

void epl_encode(epl_raw_t* raw, uint16_t duty, uint8_t flags)
{
    uint16_t word = (uint16_t)(duty & EPL_DUTY_MASK)
                  | (uint16_t)(((uint16_t)flags & EPL_FLAGS_MASK) << EPL_FLAG_SHIFT);

    raw->b[0] = (uint8_t)(word & 0xFFu);
    raw->b[1] = (uint8_t)((word >> 8) & 0xFFu);
    raw->b[2] = (uint8_t)0u;                       /* reserved */
    raw->b[3] = chk_of(word, (uint8_t)(flags & EPL_FLAGS_MASK));
}

epl_flags_t epl_rec_flags(const epl_rec_t* rec)
{
    epl_flags_t f;
    f.duty     = rec->duty;
    f.boot     = (uint8_t)((rec->flags & EPL_FLAG_BOOT)     ? 1u : 0u);
    f.pwm_fail = (uint8_t)((rec->flags & EPL_FLAG_PWM_FAIL) ? 1u : 0u);
    f.valid    = rec->valid;
    return f;
}

epl_rec_t epl_decode(const epl_raw_t* raw)
{
    epl_rec_t r;
    uint16_t word = (uint16_t)((uint16_t)raw->b[0] | ((uint16_t)raw->b[1] << 8));
    uint8_t  flags = (uint8_t)((word >> EPL_FLAG_SHIFT) & EPL_FLAGS_MASK);

    /* Erased slots must decode as invalid without being mistaken for corrupt —
     * they are different things and the scan treats them differently. */
    if ((raw->b[0] == EPL_REC_EMPTY) && (raw->b[1] == EPL_REC_EMPTY) &&
        (raw->b[2] == EPL_REC_EMPTY) && (raw->b[3] == EPL_REC_EMPTY)) {
        r.duty = 0u;
        r.flags = 0u;
        r.valid = 0u;
        return r;
    }

    if (raw->b[2] != 0u) {                         /* reserved byte must be 0 */
        r.duty = 0u;
        r.flags = 0u;
        r.valid = 0u;
        return r;
    }

    if (raw->b[3] != chk_of(word, flags)) {
        r.duty = 0u;
        r.flags = 0u;
        r.valid = 0u;
        return r;
    }

    r.duty = (uint16_t)(word & EPL_DUTY_MASK);
    r.flags = flags;
    r.valid = 1u;
    return r;
}

/* ─── Slot access ──────────────────────────────────────────────────────────── */

static epl_raw_t read_raw(const epl_io_t* io, uint16_t slot)
{
    epl_raw_t raw;
    uint16_t base = (uint16_t)(slot * EPL_REC_SIZE);
    uint8_t  i;

    for (i = 0u; i < (uint8_t)EPL_REC_SIZE; i++) {
        raw.b[i] = io->rd(io->ctx, (uint16_t)(base + i));
    }
    return raw;
}

uint8_t epl_slot_is_empty(const epl_io_t* io, uint16_t slot)
{
    epl_raw_t raw = read_raw(io, slot);
    uint8_t   i;

    for (i = 0u; i < (uint8_t)EPL_REC_SIZE; i++) {
        if (raw.b[i] != EPL_REC_EMPTY) return (uint8_t)0;
    }
    return (uint8_t)1;
}

void epl_erase_slot(const epl_io_t* io, uint16_t slot)
{
    uint16_t base = (uint16_t)(slot * EPL_REC_SIZE);
    uint8_t  i;

    for (i = 0u; i < (uint8_t)EPL_REC_SIZE; i++) {
        io->wr(io->ctx, (uint16_t)(base + i), EPL_REC_EMPTY);
    }
}

/* ─── Scan ─────────────────────────────────────────────────────────────────── */

static uint8_t slot_valid(const epl_io_t* io, uint16_t slot)
{
    epl_raw_t raw = read_raw(io, slot);
    return epl_decode(&raw).valid;
}

/*
 * Locate the valid region by finding the EMPTY arc, not by comparing the first
 * and last valid slots.
 *
 * The empty region is exactly one contiguous run (the EPL_VALID_MAX invariant),
 * so the valid region is simply its complement:
 *
 *     empty arc = [e_start .. e_start + L - 1]
 *     oldest    = (e_start + L) % SLOTS
 *     newest    = (e_start - 1 + SLOTS) % SLOTS
 *
 * e_start is the empty slot whose predecessor is valid — the start of the arc,
 * not merely the first empty slot encountered. The distinction matters: for an
 * arc like [1 .. 200] the empty slots are 0 and 201..255, which is ONE run that
 * straddles slot 0. Scanning linearly from slot 0 finds slot 0 first and would
 * measure a run of length 1, concluding wrongly that newest is 255.
 *
 * Using first-vs-last does not work either. For a wrapped region [17..255] + [0]
 * the first valid slot is 0 and the last is 255, so first <= last and the arc
 * looks unwrapped — the gap in the middle is the only evidence, and comparing
 * the two endpoints cannot see it.
 *
 * If the empty region were ever split in two, L would no longer account for
 * every invalid slot, and that is detected below rather than silently
 * mis-locating the write pointer.
 */
uint8_t epl_scan(const epl_io_t* io, epl_state_t* st)
{
    uint16_t s;
    uint16_t valid = 0u;
    uint16_t e_start = 0u;
    uint16_t L = 0u;
    uint8_t  prev_valid = 0u;
    uint8_t  found = 0u;

    st->oldest = 0u;
    st->newest = 0u;
    st->count = 0u;
    st->wrapped = 0u;
    st->anomaly = 0u;

    /* Pass 1: count every valid slot, and find the start of the empty arc.
     *
     * This does NOT stop at e_start. The partition check below needs the true
     * valid count, and stopping early leaves it undercounted — which then makes
     * a perfectly healthy ring look like its valid region has been split.
     *
     * prev_valid is seeded from the LAST slot, not from zero. The ring is
     * circular, so an empty arc sitting at index 0 — [0 .. 15] beside a valid
     * slot 255 — is a proper arc with a valid predecessor. Starting the scan at
     * slot 0 with prev_valid cleared would never notice slot 0 is its start,
     * and would report no empty arc at all for a perfectly ordinary wrapped
     * ring. */
    prev_valid = slot_valid(io, (uint16_t)(EPL_SLOTS - 1u));

    for (s = 0u; s < EPL_SLOTS; s++) {
        if (slot_valid(io, s)) {
            valid++;
            prev_valid = 1u;
        } else if (prev_valid && !found) {
            e_start = s;
            found = 1u;
            /* prev_valid stays set: the run continues, and only the first
             * empty-after-valid is the arc's start. */
        }
    }

    if (valid == 0u) {
        /* Virgin device, or everything erased. Nothing to restore. */
        return (uint8_t)0;
    }

    if (valid >= EPL_SLOTS) {
        /* 100% full: no empty arc exists, so the newest cannot be identified.
         * EPL_VALID_MAX exists to make this unreachable. */
        st->anomaly = 1u;
        st->oldest = 0u;
        st->newest = (uint16_t)(EPL_SLOTS - 1u);
        st->count = valid;
        return (uint8_t)1;
    }

    if (!found) {
        /* Every valid slot has no empty successor, yet the ring is not full.
         * That is the same contradiction from the other side. */
        st->anomaly = 1u;
        st->count = valid;
        return (uint8_t)1;
    }

    /* Pass 2: measure the empty arc. */
    while (L < EPL_SLOTS) {
        uint16_t s2 = (uint16_t)((e_start + L) % EPL_SLOTS);
        if (slot_valid(io, s2)) break;
        L++;
    }

    /* The arc and its complement must partition the ring. If they do not, the
     * valid region is not contiguous — which means the "one empty arc"
     * invariant has been broken and nothing downstream can be trusted. */
    if ((uint16_t)(valid + L) != EPL_SLOTS) {
        st->anomaly = 1u;
        st->count = valid;
        return (uint8_t)1;
    }

    st->oldest = (uint16_t)((e_start + L) % EPL_SLOTS);
    st->newest = (uint16_t)((e_start + EPL_SLOTS - 1u) % EPL_SLOTS);
    st->count = valid;
    st->wrapped = (st->oldest > st->newest) ? (uint8_t)1 : (uint8_t)0;
    return (uint8_t)0;
}

uint8_t epl_in_region(const epl_state_t* st, uint16_t slot)
{
    if (st->count == 0u) return (uint8_t)0;

    if (!st->wrapped) {
        /* Both bounds matter. An un-wrapped arc does not have to start at slot 0:
         * erasing the oldest moves it up without wrapping anything. */
        return ((slot >= st->oldest) && (slot <= st->newest)) ? (uint8_t)1 : (uint8_t)0;
    }
    return ((slot >= st->oldest) || (slot <= st->newest)) ? (uint8_t)1 : (uint8_t)0;
}

/* ─── Restore ──────────────────────────────────────────────────────────────── */

uint8_t epl_restore_ex(const epl_io_t* io, const epl_state_t* st,
                       uint16_t* duty, uint8_t* why)
{
    uint16_t  best;
    epl_raw_t raw;
    epl_rec_t newest;
    uint8_t   i;
    uint8_t   src;

    if (st->count == 0u) {
        src = EPL_SRC_VIRGIN;
        best = EPL_OFF_DUTY;
        goto done;
    }

    /* An anomalous scan means the ring could not be located — the empty arc was
     * not a single run, or there was none. Nothing about the layout can be
     * trusted, so the location that did come back must not be read. Falling back
     * to OFF is the only answer that cannot be a random duty out of the EEPROM.
     *
     * The caller is expected to log this, but the restore itself stays safe. */
    if (st->anomaly) {
        src = EPL_SRC_ANOMALY;
        best = EPL_OFF_DUTY;
        goto done;
    }

    raw = read_raw(io, st->newest);
    newest = epl_decode(&raw);
    if (!newest.valid) {
        src = EPL_SRC_INVALID;
        best = EPL_OFF_DUTY;
        goto done;
    }

    /* The newest record IS the latest decision, so it is tested first and
     * separately. Two reasons it cannot just be the seed of the window walk:
     *
     *   - if it says "deliberately off", that outranks everything older and the
     *     answer is off. Walking from i=1 would skip straight past it and then
     *     break on the next off record, returning off by accident rather than
     *     by decision.
     *   - if it says "heating", it belongs in the minimum. It is one of the
     *     entries that kept the heating on.
     */
    if (newest.duty == EPL_OFF_DUTY) {
        src = EPL_SRC_MARKER;
        best = EPL_OFF_DUTY;
        goto done;
    }

    /* The common case: a boot marker carries the already-resolved answer, so the
     * window is not consulted at all. That is what makes the O(1) restore worth
     * having — a node that keeps rebooting never walks the window. */
    if (newest.flags & EPL_FLAG_BOOT) {
        src = EPL_SRC_MARKER;
        best = newest.duty;
        goto done;
    }

    src = EPL_SRC_WINDOW;

    /* Seed the minimum only from a record whose duty actually reached the load.
     * A PWM-fault record's duty was in force while the output path was dead, so
     * it was never delivered — seeding from it would have the restore honour a
     * value known not to have reached the heater, and would let the lowest
     * undelivered duty in the ring win. Skipped rather than trusted. */
    best = EPL_OFF_DUTY;
    if (!(newest.flags & EPL_FLAG_PWM_FAIL)) best = newest.duty;

    for (i = 1u; i <= EPL_RESTORE_WINDOW; i++) {
        uint16_t  slot = (uint16_t)((st->newest + EPL_SLOTS - (uint16_t)i) % EPL_SLOTS);
        epl_raw_t rraw;
        epl_rec_t r;

        if (!epl_in_region(st, slot)) break;   /* walked off the valid arc */

        rraw = read_raw(io, slot);
        r = epl_decode(&rraw);
        if (!r.valid) continue;                /* corrupt or hole: not a boundary */
        if (r.flags & EPL_FLAG_BOOT) continue;                  /* markers are transparent */
        /* A PWM-fault record is transparent too, and not out of caution: its duty
         * was in force while the output path was dead, so it was never actually
         * delivered to the load. It is not a valid observation of what the heater
         * was doing, and letting it into the minimum would have the restore honour
         * a value that is known not to have reached the load. */
        if (r.flags & EPL_FLAG_PWM_FAIL) continue;
        if (r.duty == EPL_OFF_DUTY) break;     /* an explicit off outranks older */

        if (r.duty < best) best = r.duty;
    }

done:
    if (duty != 0) *duty = best;
    if (why  != 0) *why  = src;
    return (uint8_t)0;
}

uint16_t epl_restore(const epl_io_t* io, const epl_state_t* st)
{
    uint16_t duty;
    (void)epl_restore_ex(io, st, &duty, 0);
    return duty;
}

/* ─── Append ───────────────────────────────────────────────────────────────── */

uint16_t epl_append(const epl_io_t* io, epl_state_t* st, uint16_t duty, uint8_t flags)
{
    epl_raw_t raw;
    uint16_t slot;
    uint16_t base;
    uint8_t  i;

    if (st->anomaly) return (uint16_t)0xFFFFu;

    /* Enforce the cap BEFORE writing, not after: the ring must never be able to
     * reach the ambiguous full state even transiently. */
    if (st->count >= EPL_VALID_MAX) {
        epl_erase_slot(io, st->oldest);
        st->oldest = (uint16_t)((st->oldest + 1u) % EPL_SLOTS);
        st->count--;
    }

    if (st->count == 0u) {
        /* Very first record. newest is 0 on a virgin ring, so deriving the slot
         * from newest+1 would land on 1 and leave slot 0 permanently empty —
         * which shows up as the arc starting at 1 and every "is slot 0
         * written" check failing. */
        slot = 0u;
    } else {
        slot = (uint16_t)((st->newest + 1u) % EPL_SLOTS);
    }

    base = (uint16_t)(slot * EPL_REC_SIZE);

    epl_encode(&raw, duty, flags);

    /* chk is written last. A power cut before it leaves the previous record's
     * checksum against this record's payload, so the slot reads invalid — a
     * hole, not a false boundary. */
    for (i = 0u; i < (uint8_t)3u; i++) {
        io->wr(io->ctx, (uint16_t)(base + i), raw.b[i]);
    }
    io->wr(io->ctx, (uint16_t)(base + 3u), raw.b[3]);

    if (st->count == 0u) st->oldest = slot;

    st->newest = slot;
    st->count++;

    /* Derived, not maintained. The arc wraps exactly when it runs off the end,
     * which is the same statement as oldest having overtaken newest. Trying to
     * track this incrementally across erases is where it goes wrong. */
    st->wrapped = (st->oldest > st->newest) ? (uint8_t)1 : (uint8_t)0;

    return slot;
}