/**
 * @file eeprom_log.h
 * @brief Restore-point log in EEPROM, for surviving a power cut.
 *
 * Purpose. This node has no usable notion of "the temperature of the house" —
 * the DS18B20s are ranked by ROM address, there is no setpoint anywhere in the
 * firmware, and no code path compares a reading to a threshold. Frost
 * protection is therefore not implementable here. What the upstream controller
 * decides has to survive a restart of this board, or a power cut during a cold
 * night leaves the house with no heat and nothing to notice it.
 *
 * So: the last commanded duty is persisted, and restored on boot. That is a
 * state-transfer mechanism, not a thermostat. It is explicitly NOT a failsafe
 * on its own — the dead-man timeout is what stops a stale restore from
 * becoming permanent. See §3.0 / §3.1 of doc/risk_analysis.md.
 *
 * ─── Storage ────────────────────────────────────────────────────────────────
 * A ring of fixed-size records across the whole EEPROM. No counter cell, no
 * erase-on-wrap, nothing that concentrates wear: every record slot takes one
 * write per full revolution, ~1 per 341 days at the 8 h cadence.
 *
 * A 2-byte monotonic counter would have made "where is the newest record"
 * pure arithmetic, but it would also have taken *every* write on one cell.
 * Concentration, not the date, is what makes a cell the thing that fails first
 * — and its failure would take down the whole chain.
 *
 * ─── The invariant that makes a counter unnecessary ─────────────────────────
 * The valid region is one contiguous arc of the ring, and it is ALWAYS strictly
 * shorter than the ring. EPL_VALID_MAX leaves 16 slots of guaranteed empty
 * space. That is not about wear — it is what makes the scan unambiguous:
 *
 *   the empty region is exactly one arc, therefore there is exactly one
 *   empty -> valid transition in circular order, therefore
 *
 *       wrapped  <=>  first_valid_slot != 0
 *
 *   and newest is simply the last valid slot. One comparison, no terminator
 *   hunt. If the ring were allowed to reach 100% full the empty arc would
 *   vanish and "first valid slot" would stop meaning anything.
 *
 * ─── Why erased and corrupt must stay distinguishable ───────────────────────
 * The scan finds the empty arc by looking for all-0xFF records. A record that
 * fails its checksum is NOT treated as empty: skipping it silently would merge
 * two separate empty arcs, which is exactly the ambiguity above. So a torn
 * write leaves a hole, not a false boundary — and a hole makes the scan report
 * an anomaly rather than silently mis-locate the write pointer.
 *
 * ─── Types ──────────────────────────────────────────────────────────────────
 * Fixed-width only, deliberately. `int` is 16-bit on the ATmega328p and 32-bit
 * on the host, so any use of it makes this file behave differently in the test
 * build than on the hardware it exists to protect. Same for `short`, `long`
 * and `size_t` in the interface.
 */

#ifndef EEPROM_LOG_H
#define EEPROM_LOG_H

#include <stdint.h>

/* ─── Geometry ─────────────────────────────────────────────────────────────
 * ATmega328p: 1024 bytes of EEPROM, 4-byte pages. A 4-byte record lines up with
 * exactly one page; a 3-byte record straddles a page half the time.
 */
#define EPL_EEPROM_BYTES   ((uint16_t)1024u)
#define EPL_REC_SIZE       ((uint16_t)4u)
#define EPL_SLOTS          ((uint16_t)(EPL_EEPROM_BYTES / EPL_REC_SIZE))   /* 256 */

/* Hard cap on the valid region. Not a wear number — see the file header.
 * The append path enforces this BEFORE writing, so the ring can never reach
 * the ambiguous 100%-full state. */
#define EPL_VALID_MAX      ((uint16_t)(EPL_SLOTS - 16u))                   /* 240 */

/* How far back the restore walks when the newest record is not a boot marker.
 * Normally unused: a boot marker carries the already-resolved answer. */
#define EPL_RESTORE_WINDOW ((uint8_t)10u)

/* The duty that means "deliberately not heating". Everything in this module is
 * expressed relative to this constant, never to a physical direction — which is
 * what keeps the whole log independent of the unresolved drive-polarity question
 * (risk analysis §7 Batch 1.2). Changing it later is a one-line change. */
#define EPL_OFF_DUTY       ((uint16_t)1000u)

/* ─── Record layout (4 bytes) ───────────────────────────────────────────────
 *   [0..1] word = duty(10 bits) | flags << 10            little-endian
 *   [2]    reserved, always 0
 *   [3]    chk, written LAST
 *
 * duty is 0..1000 so 10 bits, leaving 6 spare flag bits (11..15). Bit 10 is
 * BOOT; bits 11..15 are reserved for future flags without changing the layout.
 *
 * chk is a 1-byte rot-folded checksum rather than a plain XOR, because a plain
 * XOR is blind to a byte swap: swapping word_lo and word_hi leaves a pure XOR
 * unchanged and a corrupt duty sails through. Two extra rotations close that.
 * It catches partial writes, stuck cells and single-byte rot.
 */
#define EPL_FLAG_BOOT      ((uint16_t)0x0400u)
#define EPL_DUTY_MASK      ((uint16_t)0x03FFu)
#define EPL_REC_EMPTY      ((uint8_t)0xFFu)   /* AVR EEPROM erase sets all bits */

/* ─── EEPROM access ──────────────────────────────────────────────────────────
 * Injected rather than calling <EEPROM.h> directly, for two reasons: the host
 * test needs to substitute a mock, and the fault-injection the algorithm most
 * needs to be tested against (power loss part-way through a record) is
 * impossible to produce otherwise.
 */
typedef struct {
    void*    ctx;
    uint8_t (*rd)(void* ctx, uint16_t addr);
    void    (*wr)(void* ctx, uint16_t addr, uint8_t value);
} epl_io_t;

/* ─── Scanned region ───────────────────────────────────────────────────────── */
typedef struct {
    uint16_t oldest;    /* first slot of the valid arc   */
    uint16_t newest;    /* last slot of the valid arc    */
    uint16_t count;     /* valid records; < EPL_SLOTS is an invariant, not a hope */
    uint8_t  wrapped;   /* 1 if the arc runs past slot 0 */
    uint8_t  anomaly;   /* 1 if the ring is 100% full and the newest is unknowable */
} epl_state_t;

/* One decoded record. */
typedef struct {
    uint16_t duty;
    uint8_t  boot;
    uint8_t  valid;
} epl_rec_t;

/* Raw 4 bytes of a slot, as stored. */
typedef struct {
    uint8_t b[4];
} epl_raw_t;

/* ─── API ──────────────────────────────────────────────────────────────────── */

/* Decode one slot. valid = 0 for both an erased slot and a corrupt one — the
 * caller must not conflate them, and epl_slot_is_empty() exists so it can
 * avoid doing so. */
epl_rec_t epl_decode(const epl_raw_t* raw);

/* Encode into 4 bytes. chk is placed last so the caller can order the writes. */
void epl_encode(epl_raw_t* raw, uint16_t duty, uint8_t boot);

/* 1 if every byte of the slot is 0xFF — i.e. genuinely erased. */
uint8_t epl_slot_is_empty(const epl_io_t* io, uint16_t slot);

/* 1 if the slot lies inside the scanned valid arc. */
uint8_t epl_in_region(const epl_state_t* st, uint16_t slot);

/* Locate the valid arc. Fills st; returns 0 on success, non-zero if the ring is
 * 100% full (st->anomaly set), which the append path must treat as a fault
 * rather than as a writable state. */
uint8_t epl_scan(const epl_io_t* io, epl_state_t* st);

/* The duty to restore, given a scanned state.
 *   count == 0            -> EPL_OFF_DUTY (virgin device, nothing to restore)
 *   newest is a boot mark -> that record's duty, already resolved at that boot
 *   otherwise             -> walk back <= EPL_RESTORE_WINDOW, skipping boot
 *                            markers, stopping at the first EPL_OFF_DUTY record
 *                            and taking the minimum of the heating records. */
uint16_t epl_restore(const epl_io_t* io, const epl_state_t* st);

/* Append one record. Enforces EPL_VALID_MAX by erasing the oldest slot first,
 * so the ring can never reach the ambiguous full state. Returns the slot
 * written, or 0xFFFF on fault. st is updated in place. */
uint16_t epl_append(const epl_io_t* io, epl_state_t* st, uint16_t duty, uint8_t boot);

/* Erase one slot back to 0xFF. */
void epl_erase_slot(const epl_io_t* io, uint16_t slot);

#endif /* EEPROM_LOG_H */