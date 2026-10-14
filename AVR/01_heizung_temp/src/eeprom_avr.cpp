/**
 * @file eeprom_avr.cpp
 * @brief Binds eeprom_log to the real ATmega328p EEPROM.
 *
 * All the logic lives in eeprom_log.cpp so it can be compiled for the host and
 * tested. This file is the entire hardware dependency, deliberately kept to
 * twenty lines.
 *
 * EEPROM.update() rather than EEPROM.write(): update() skips the erase/write
 * cycle when the byte already holds the value being written. The duty rarely
 * changes between appends, so a rewritten slot costs far less than three full
 * 3.3 ms cycles — which matters because those cycles stall the CPU and delay
 * the Timer1 compare vectors.
 */

#include <EEPROM.h>
#include "eeprom_log.h"

static uint8_t avr_rd(void* ctx, uint16_t addr)
{
    (void)ctx;
    return EEPROM.read(addr);
}

static void avr_wr(void* ctx, uint16_t addr, uint8_t value)
{
    (void)ctx;
    EEPROM.update(addr, value);
}

#include "eeprom_avr.h"

const epl_io_t* eeprom_log_avr_io(void)
{
    static epl_io_t io;
    static uint8_t   ready;

    if (!ready) {
        io.ctx = 0;
        io.rd = avr_rd;
        io.wr = avr_wr;
        ready = 1;
    }
    return &io;
}