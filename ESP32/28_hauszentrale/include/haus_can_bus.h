/**
 * @file haus_can_bus.h
 * @brief TWAI bring-up and RX/TX task for the 28_hauszentrale panel (S17).
 *
 * The CAN transceiver is behind the FSUSB42UMX mux: IO5 high routes GPIO 19/20
 * to CAN, IO5 low gives them back to USB. Starting the bus therefore means
 * switching the mux and installing the TWAI driver, not just calling
 * twai_start().
 */

#ifndef HAUS_CAN_BUS_H
#define HAUS_CAN_BUS_H

#include <stdbool.h>
#include <stdint.h>

#include "haus_can.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool running;
  bool bus_fault; /* sticky until a good frame is received */
  bool valid;     /* at least one temperature frame seen */
  uint32_t timestamp;         /* local unix time of the last update */
  uint32_t last_update_epoch; /* same, used for staleness */
  uint32_t last_frame_ms;
  int16_t temp_c100[HAUS_CAN_MAX_SENSORS];
  uint16_t pwm_permille;
  uint16_t frequency_hz;
  uint8_t pwm_flags;
  uint32_t rx_frames;
  uint32_t tx_frames;
  uint32_t bus_errors;
  uint8_t twai_state; /* TWAI_STATE_* */
  uint32_t tx_error_counter;
  uint32_t rx_error_counter;
} haus_can_snapshot_t;

/* Switch the mux to CAN, install/start TWAI and create the RX task. Idempotent.
 * Blocking for about 200 ms (mux settle). */
bool haus_can_bus_start(void);

/* Stop TWAI and give GPIO 19/20 back to USB. Idempotent. */
void haus_can_bus_stop(void);

bool haus_can_bus_running(void);

/* Copy the latest decode snapshot. Never blocks the caller. */
void haus_can_bus_get(haus_can_snapshot_t *out);

/* Enable/disable from Settings. */
void haus_can_bus_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_CAN_BUS_H */
