/**
 * @file eeprom_mock.cpp
 * @brief Host EEPROM mock: power-cut injection and per-cell wear counting.
 *
 * Note on types: `int` is 16-bit on the ATmega328p and 32-bit here. Using it
 * anywhere in this test would silently change the arithmetic compared to the
 * firmware it is checking, which is the exact class of bug the harness exists
 * to catch. Fixed-width types only, and -Wconversion is on in the Makefile to
 * keep it that way.
 */

#include <string.h>
#include "eeprom_mock.h"

static uint8_t mock_rd(void* ctx, uint16_t addr)
{
    eeprom_mock_t* m = (eeprom_mock_t*)ctx;

    /* A powered-off AVR EEPROM still reads whatever was last committed. It is
     * not volatile — power loss does not clear it. That is exactly the property
     * the log depends on, so reads keep working after the cut. */
    return m->mem[addr];
}

static void mock_wr(void* ctx, uint16_t addr, uint8_t value)
{
    eeprom_mock_t* m = (eeprom_mock_t*)ctx;

    if (m->powered_off) return;

    if (m->budget != 0xFFFFu) {
        if (m->writes >= m->budget) {
            /* Supply collapsed before this byte landed. */
            m->powered_off = 1;
            return;
        }
    }

    /* EEPROM.update() semantics: no change, no erase/write cycle, no wear. */
    if (m->mem[addr] != value) {
        m->mem[addr] = value;
        m->cell_writes[addr]++;
        m->total_writes++;
    }
    m->writes++;
}

void mock_init(eeprom_mock_t* m)
{
    memset(m->mem, 0xFF, sizeof(m->mem));   /* AVR EEPROM erases to all-ones */
    memset(m->cell_writes, 0, sizeof(m->cell_writes));
    m->total_writes = 0u;
    m->writes = 0u;
    m->budget = 0xFFFFu;
    m->powered_off = 0u;
}

void mock_cut_after(eeprom_mock_t* m, uint16_t writes)
{
    m->budget = writes;
    m->writes = 0u;
    m->powered_off = 0u;
}

void mock_poke(eeprom_mock_t* m, uint16_t addr, uint8_t value)
{
    m->mem[addr] = value;   /* deliberately uncounted: not a real write cycle */
}

epl_io_t mock_io(eeprom_mock_t* m)
{
    epl_io_t io;
    io.ctx = m;
    io.rd = mock_rd;
    io.wr = mock_wr;
    return io;
}

/* ─── Wear measurement ─────────────────────────────────────────────────────── */

uint32_t mock_max_cell_writes(const eeprom_mock_t* m)
{
    uint32_t worst = 0u;
    uint16_t i;

    for (i = 0u; i < (uint16_t)EPL_EEPROM_BYTES; i++) {
        if ((uint32_t)m->cell_writes[i] > worst) worst = (uint32_t)m->cell_writes[i];
    }
    return worst;
}

uint16_t mock_max_cell_addr(const eeprom_mock_t* m)
{
    uint16_t worst_addr = 0u;
    uint32_t worst = 0u;
    uint16_t i;

    for (i = 0u; i < (uint16_t)EPL_EEPROM_BYTES; i++) {
        if ((uint32_t)m->cell_writes[i] > worst) {
            worst = (uint32_t)m->cell_writes[i];
            worst_addr = i;
        }
    }
    return worst_addr;
}

uint32_t mock_total_writes(const eeprom_mock_t* m)
{
    return m->total_writes;
}

uint16_t mock_cells_used(const eeprom_mock_t* m)
{
    uint16_t used = 0u;
    uint16_t i;

    for (i = 0u; i < (uint16_t)EPL_EEPROM_BYTES; i++) {
        if (m->cell_writes[i] != 0u) used++;
    }
    return used;
}

uint8_t mock_wear_exceeded(const eeprom_mock_t* m)
{
    return (mock_max_cell_writes(m) >= MOCK_WEAR_LIMIT) ? (uint8_t)1 : (uint8_t)0;
}

uint32_t mock_wear_skew(const eeprom_mock_t* m)
{
    uint32_t worst = mock_max_cell_writes(m);
    uint32_t total = m->total_writes;
    uint16_t used = mock_cells_used(m);

    if ((used == 0u) || (total == 0u)) return (uint32_t)0u;

    /* worst / mean, x1000. Perfectly even spread returns 1000. */
    return (worst * 1000u) / (total / (uint32_t)used);
}