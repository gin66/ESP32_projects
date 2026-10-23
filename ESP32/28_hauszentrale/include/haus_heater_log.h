/**
 * @file haus_heater_log.h
 * @brief 1 Hz heater experiment trace on SD (`experiments/<id>/trace.csv`).
 *
 * Keeps `duty_req` (what the panel sent) and `duty_rpt` (what 0x280 reported)
 * apart, records pipe-wall temperatures, room, coil RMS/angle, and the Soll
 * hint. It is a trace store, not the minute record.
 *
 * See §6.3 of whitepaper_auto_heating.md.
 */

#ifndef HAUS_HEATER_LOG_H
#define HAUS_HEATER_LOG_H

#include <stdint.h>

#include "haus_gui.h"

void heater_log_init(void);

/* Call once per second, after heater_ctl_tick(). Does nothing when the card
 * is absent; retries a failed start every 60 s. */
void heater_log_tick(uint32_t now_ms, const HausState *st);

/* Current trace path, or "" when no experiment is open (for /heater JSON). */
const char *heater_log_path(void);

#endif /* HAUS_HEATER_LOG_H */
