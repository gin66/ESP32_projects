/**
 * @file haus_sht45.h
 * @brief Sensirion SHT45 local climate sensor (temperature + humidity).
 *
 * Sits on the shared I2C bus (SDA 8 / SCL 9) at 0x44. The address does not
 * collide with the CH422G (0x23/0x24/0x26/0x38) or the GT911 (0x5D/0x14).
 */

#ifndef HAUS_SHT45_H
#define HAUS_SHT45_H

#include <stdbool.h>
#include <stdint.h>

#define HAUS_SHT45_ADDR 0x44

#ifdef __cplusplus
extern "C" {
#endif

/* Probe the part and issue a soft reset. Returns true when it ACKs. */
bool haus_sht45_init(void);

/* Non-blocking poll: completes the measurement started by the previous call
 * and starts the next one (~8.3 ms conversion, one call per second). Returns
 * true while the cached reading is valid; *temp_c100 and *hum_c100 always
 * carry the last good values. */
bool haus_sht45_poll(int16_t *temp_c100, uint16_t *hum_c100);

bool haus_sht45_present(void);

#ifdef __cplusplus
}
#endif

#endif
