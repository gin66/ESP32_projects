/**
 * @file eeprom_avr.h
 * @brief AVR binding for eeprom_log.
 *
 * Deliberately not folded into eeprom_log.h: that header stays free of Arduino
 * so the host test can compile the algorithm unchanged, and declaring an AVR
 * function there would force the test to resolve it.
 */

#ifndef EEPROM_AVR_H
#define EEPROM_AVR_H

#include "eeprom_log.h"

// The real EEPROM, with EEPROM.update() (skip-if-unchanged) semantics.
const epl_io_t* eeprom_log_avr_io(void);

#endif /* EEPROM_AVR_H */