/**
 * @file serial_log.h
 * @brief Runtime gate for the node's serial telemetry.
 *
 * The periodic status lines cost tens of milliseconds per second at 115200 baud
 * when they are written from loop(). That is time the loop cannot spend sampling
 * A0 or keeping the failover waveform on schedule, and on a production node
 * nobody is reading the port anyway.
 *
 * The gate is off at boot. It is armed by a line command on the serial port
 * ("L", see serial_log_poll()), and the last thing setup() prints says so.
 * Telemetry still reaches CAN and the radio regardless; this only decides
 * whether it is also formatted and pushed out the UART.
 *
 * Use log_print()/log_println() in any code reachable from loop(). The
 * one-time boot lines in setup() stay on Serial directly.
 */

#ifndef SERIAL_LOG_H
#define SERIAL_LOG_H

#include <Arduino.h>

/* Defined in serial_log.cpp. Read directly so the check inlines. */
extern bool serial_log_on;

/* Consume any complete command lines from the serial port. Never blocks. */
void serial_log_poll(void);

/* Forwards to the same overloads as Serial.print(ln). Does nothing when off. */
template <typename T> inline void log_print(T v)
{
    if (serial_log_on) Serial.print(v);
}

template <typename T> inline void log_print(T v, int base)
{
    if (serial_log_on) Serial.print(v, base);
}

template <typename T> inline void log_println(T v)
{
    if (serial_log_on) Serial.println(v);
}

template <typename T> inline void log_println(T v, int base)
{
    if (serial_log_on) Serial.println(v, base);
}

inline void log_println()
{
    if (serial_log_on) Serial.println();
}

#endif
