/**
 * @file haus_heater_ctl.h
 * @brief Runtime AUTO controller: schedule, anti-short-cycling, failure holds,
 *        pump guard, and the runtime snapshot used by HTTP and the UI.
 *
 * The controller is portable: it takes a plain input struct and sends through
 * an injected callback, so host tests can drive it without CAN or LVGL.
 *
 * See §5.1, §5.6, §5.7, and §5.9 of whitepaper_auto_heating.md.
 */

#ifndef HAUS_HEATER_CTL_H
#define HAUS_HEATER_CTL_H

#include <stdbool.h>
#include <stdint.h>

#include "haus_heater.h"

/* PWM source value for a foreign nRF writer (mirrors PWM_SRC_NRF). */
#define HEATER_CTL_PWM_SRC_NRF 0x04u
/* PWM source value for a boiler fault (mirrors PWM_SRC_FAULT). */
#define HEATER_CTL_PWM_SRC_FAULT 0x06u

/* Maximum time AUTO yields to a foreign writer (2 h). */
#define HEATER_OVERRIDE_MAX_S 7200u

/* Inputs sampled once per second from CanState / ClimateState. */
typedef struct {
  bool can_green;         /**< CAN bus healthy and communicating.        */
  bool clock_synced;      /**< SNTP clock valid.                         */
  uint32_t utc_epoch;     /**< Current unix seconds (0 = unknown).       */
  bool pwm_known;         /**< A 0x280 report has been seen.             */
  uint8_t pwm_src;        /**< PWM source (PWM_SRC_*).                   */
  uint16_t pwm_permille;  /**< Last duty reported by 0x280.              */
  bool ac_fresh;          /**< A complete coil waveform is recent.       */
  uint16_t ac_rms_dc;     /**< 0x2C coil RMS, 0.1 ADC count.             */
  bool water_valid;       /**< Both pipe temperatures are present.       */
  int16_t water_in_c100;  /**< Pipe-wall temperature, c°.                */
  int16_t water_out_c100; /**< Pipe-wall temperature, c°.                */
  bool room_valid;        /**< SHT45 room reading is valid.              */
  int16_t room_c100;      /**< Room temperature, c°.                     */
} HeaterCtlInputs;

/* Pump guard parameters. Disabled by default until the installed switch
 * position and coil threshold are recorded (§5.7). */
typedef struct {
  bool enabled;
  uint16_t rms_min_dc;   /**< "Pump not running" below this RMS.        */
  uint16_t grace_s;      /**< Ignore after an on-edge (default 180).    */
  uint16_t confirm_s;    /**< Low-current time before latching (600).   */
  uint16_t hold_s;       /**< Stay at 1000 after the latch (1800).      */
  int16_t delta_c100;    /**< ΔT below this looks blocked (200).        */
} HeaterPumpParams;

/* Snapshot for the HEATER page and /heater JSON. */
typedef struct {
  HeaterMode mode;              /**< Effective mode (may be OVERRIDE).    */
  HeaterCycleState cycle;       /**< IDLE / HEATING / COOLDOWN.           */
  HeaterCycleResult last_result;/**< Last step result.                    */
  HeaterScheduleReason sched_reason; /**< Day/night/off reason.           */
  HeaterHoldReason hold;        /**< AUTO HOLD reason (NONE when clear).  */
  bool can_green;
  bool clock_synced;
  bool override_active;
  bool pump_blocked;
  uint16_t sched_duty;          /**< Schedule request (100..500 or 1000). */
  uint16_t planned_duty;        /**< Cycle output after limits.           */
  uint16_t last_sent_duty;      /**< Last frame the panel sent.           */
  bool have_sent;               /**< last_sent_duty is meaningful.        */
  int16_t soll_c100;            /**< Soll hint for planned_duty.          */
  bool soll_valid;              /**< soll_c100 is meaningful.             */
  uint32_t last_send_s;
  uint8_t edges_last_hour;
  uint32_t min_on_remaining_s;
  uint32_t min_off_remaining_s;
  uint32_t duty_interval_remaining_s;
  uint32_t override_remaining_s;
  uint32_t pump_blocked_remaining_s;
} HeaterRuntime;

typedef bool (*HeaterCtlSendFn)(uint16_t duty_permille);

#ifdef __cplusplus
extern "C" {
#endif

void heater_ctl_init(void);
void heater_ctl_set_sender(HeaterCtlSendFn fn);
void heater_pump_params_default(HeaterPumpParams *params);
void heater_ctl_set_pump_params(const HeaterPumpParams *params);
void heater_ctl_tick(uint32_t now_s, const HeaterCtlInputs *in);

/* Record a frame sent outside the AUTO controller (manual switch/slider).
 * Keeps duty_req and the 0x280 conflict check honest. */
void heater_ctl_note_sent(uint16_t duty);

const HeaterRuntime *heater_ctl_get(void);

/* Stable short names for logs, the UI, and HTTP. */
const char *heater_mode_name(HeaterMode mode);
const char *heater_cycle_state_name(HeaterCycleState state);
const char *heater_hold_name(HeaterHoldReason hold);
const char *heater_sched_reason_name(HeaterScheduleReason reason);
const char *heater_result_name(HeaterCycleResult result);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_HEATER_CTL_H */
