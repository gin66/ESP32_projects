/**
 * @file eeprom_mock.h
 * @brief Host-side EEPROM with power-cut injection and per-cell wear counting.
 *
 * Two things this mock exists to model that the bench cannot:
 *
 * 1. POWER CUTS. The failure this log must survive is loss of supply part-way
 *    through a record write. In the firmware that is one field assignment; in
 *    the test it lets us cut at every byte offset, thousands of times.
 *
 * 2. WEAR. The whole reason there is no counter cell is concentration of writes,
 *    and concentration is exactly the property that cannot be eyeballed from
 *    the code — it is an emergent property of the append/reuse pattern. Counting
 *    writes per cell turns the prose claim "spread across 256 slots" into
 *    something the harness measures and fails on.
 *
 *    Wear is counted per byte, and only when the value actually changes, which
 *    is precisely what EEPROM.update() does on real hardware. A write of the
 *    same value costs no erase cycle and is not counted.
 *
 * The wear figure to fail against is the ATmega328p datasheet's 100 000
 * erase/write cycles per cell.
 */

#ifndef EEPROM_MOCK_H
#define EEPROM_MOCK_H

#include <stdint.h>
#include "eeprom_log.h"

/* ATmega328p datasheet: 100 000 erase/write cycles per EEPROM cell. */
#define MOCK_WEAR_LIMIT  ((uint32_t)100000u)

typedef struct {
    uint8_t  mem[EPL_EEPROM_BYTES];
    uint16_t cell_writes[EPL_EEPROM_BYTES];   /* erase/write cycles per byte */
    uint32_t total_writes;                    /* across all cells           */
    uint16_t writes;                          /* bytes written since reset  */
    uint16_t budget;                          /* writes before power cut; 0xFFFF = never */
    uint8_t  powered_off;
} eeprom_mock_t;

void      mock_init(eeprom_mock_t* m);
void      mock_cut_after(eeprom_mock_t* m, uint16_t writes);   /* 0xFFFF = never */
epl_io_t  mock_io(eeprom_mock_t* m);

/* Direct poke, bypassing the write path — for tests that need to corrupt a byte
 * deliberately, and so must not be charged wear for it. */
void      mock_poke(eeprom_mock_t* m, uint16_t addr, uint8_t value);

/* ─── Wear measurement ─────────────────────────────────────────────────────── */

/* Worst single cell. This is the number that decides when the log dies. */
uint32_t  mock_max_cell_writes(const eeprom_mock_t* m);

/* Index of that worst cell, for diagnostics. */
uint16_t  mock_max_cell_addr(const eeprom_mock_t* m);

uint32_t  mock_total_writes(const eeprom_mock_t* m);

/* Cells that have been written at least once. */
uint16_t  mock_cells_used(const eeprom_mock_t* m);

/* 1 if any cell has reached the datasheet endurance limit. */
uint8_t   mock_wear_exceeded(const eeprom_mock_t* m);

/* Worst-cell / mean-across-used-cells, scaled by 1000 to stay in fixed width.
 * 1000 means perfectly even spread; higher means concentration. A design with a
 * hot cell scores high here even if nothing has burned out yet — which is
 * precisely the failure mode a counter cell would have had. */
uint32_t  mock_wear_skew(const eeprom_mock_t* m);

#endif /* EEPROM_MOCK_H */