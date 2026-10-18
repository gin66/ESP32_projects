#include "serial_log.h"

bool serial_log_on = false;

/* Commands are one line, terminated by CR or LF:
 *
 *   L       toggle
 *   L1      on
 *   L0      off
 *   ?       print the command list
 *
 * The acknowledgement always goes out on Serial directly, including the "OFF"
 * one, so turning the log off is visible rather than indistinguishable from a
 * node that has stopped talking. */
void serial_log_poll(void)
{
    static char    line[8];
    static uint8_t n = 0;

    while (Serial.available()) {
        int c = Serial.read();
        if (c < 0) break;

        if (c != '\n' && c != '\r') {
            if (n < (uint8_t)(sizeof(line) - 1u)) line[n++] = (char)c;
            continue;
        }

        line[n] = '\0';

        if (n == 0u) {
            continue;
        } else if ((line[0] == 'L' || line[0] == 'l') && n == 1u) {
            serial_log_on = !serial_log_on;
        } else if ((line[0] == 'L' || line[0] == 'l') && n == 2u &&
                   (line[1] == '0' || line[1] == '1')) {
            serial_log_on = (line[1] == '1');
        } else if (line[0] == '?') {
            Serial.println(F("[TX] log: L=toggle  L1=on  L0=off"));
            n = 0;
            continue;
        } else {
            n = 0;
            continue;
        }

        Serial.print(F("[TX] serial log "));
        Serial.println(serial_log_on ? F("ON") : F("OFF"));
        n = 0;
    }
}
