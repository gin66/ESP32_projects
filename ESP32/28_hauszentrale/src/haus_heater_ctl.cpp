/**
 * @file haus_heater_ctl.cpp
 * @brief Runtime AUTO controller and snapshot (Phase 4).
 */

#include "haus_heater_ctl.h"

#include "haus_heater_bin.h"
#include "haus_water_model.h"

#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>

#include "tpl_debug_log.h"
#define CTL_LOG(...) TPL_LOGI("heater_ctl", __VA_ARGS__)
#else
#define CTL_LOG(...) ((void)0)
#endif

static HeaterCycle s_cycle;
static HeaterRuntime s_rt;
static HeaterMode s_cfg_mode = HEATER_MODE_OFF;
static uint32_t s_last_run_s = 0;
static bool s_initialized = false;
static HeaterCtlSendFn s_send = nullptr;

static bool s_override = false;
static uint32_t s_override_until_s = 0;
static bool s_fault_seen = false;

static uint16_t s_expected_duty = 1000;
static bool s_expected_valid = false;
static bool s_mismatch_logged = false;

static HeaterPumpParams s_pump;
static bool s_pump_blocked = false;
static uint32_t s_pump_blocked_until_s = 0;
static uint32_t s_pump_bad_since_s = 0;
static int16_t s_room_at_block_c100 = 0;

static uint32_t elapsed_since(uint32_t now_s, uint32_t then_s) {
  return (uint32_t)(now_s - then_s);
}

static uint32_t remaining_s(uint32_t now_s, uint32_t start_s,
                            uint32_t duration_s) {
  const uint32_t elapsed = elapsed_since(now_s, start_s);
  return elapsed >= duration_s ? 0 : (duration_s - elapsed);
}

void heater_pump_params_default(HeaterPumpParams *params) {
  if (!params) return;
  params->enabled = false;
  params->rms_min_dc = 0;
  params->grace_s = 180;
  params->confirm_s = 600;
  params->hold_s = 1800;
  params->delta_c100 = 200;
}

void heater_ctl_init(void) {
  heater_cycle_init(&s_cycle);
  memset(&s_rt, 0, sizeof(s_rt));
  heater_pump_params_default(&s_pump);
  const HeaterConfig *cfg = heater_config_get();
  s_cfg_mode = cfg->mode;
  s_last_run_s = 0;
  s_override = false;
  s_override_until_s = 0;
  s_fault_seen = false;
  s_expected_valid = false;
  s_mismatch_logged = false;
  s_pump_blocked = false;
  s_pump_blocked_until_s = 0;
  s_pump_bad_since_s = 0;
  s_rt.mode = cfg->mode;
  s_rt.cycle = s_cycle.state;
  s_initialized = true;
}

void heater_ctl_set_sender(HeaterCtlSendFn fn) { s_send = fn; }

void heater_ctl_set_pump_params(const HeaterPumpParams *params) {
  if (!params) return;
  s_pump = *params;
}

static void ctl_send(uint32_t now_s, uint16_t duty) {
  if (s_send && s_send(duty)) {
    s_expected_duty = duty;
    s_expected_valid = true;
    s_mismatch_logged = false;
    s_rt.last_sent_duty = duty;
    s_rt.have_sent = true;
    s_rt.last_send_s = now_s;
  } else {
    CTL_LOG("send failed duty=%u", (unsigned)duty);
  }
}

static void pump_guard_update(uint32_t now_s, const HeaterCtlInputs *in) {
  if (!s_pump.enabled) {
    s_pump_blocked = false;
    s_pump_bad_since_s = 0;
    return;
  }

  if (s_pump_blocked) {
    /* Clear early when the room has fallen further (a valve opened). */
    if (in->room_valid && s_room_at_block_c100 > 0 &&
        in->room_c100 < (int16_t)(s_room_at_block_c100 - 50)) {
      s_pump_blocked = false;
      s_pump_blocked_until_s = 0;
      CTL_LOG("pump guard cleared: room fell");
      return;
    }
    if ((int32_t)(now_s - s_pump_blocked_until_s) >= 0) {
      s_pump_blocked = false;
      s_pump_blocked_until_s = 0;
      CTL_LOG("pump guard recovered: one supervised demand");
    }
    return;
  }

  /* The guard only runs on a real AUTO heat demand, outside the Taktsperre
   * grace, with a fresh coil sample and valid pipe readings. */
  if (s_cycle.state != HEATER_CYCLE_HEATING || !in->ac_fresh ||
      !in->water_valid)
    return;
  if (elapsed_since(now_s, s_cycle.state_since) < s_pump.grace_s) return;

  const int16_t delta =
      (int16_t)((in->water_out_c100 >= in->water_in_c100)
                    ? (in->water_out_c100 - in->water_in_c100)
                    : (in->water_in_c100 - in->water_out_c100));
  const bool low_current = in->ac_rms_dc < s_pump.rms_min_dc;

  if (!low_current || delta >= s_pump.delta_c100) {
    s_pump_bad_since_s = 0;
    return;
  }

  if (s_pump_bad_since_s == 0 || s_pump_bad_since_s > now_s)
    s_pump_bad_since_s = now_s;
  if (elapsed_since(now_s, s_pump_bad_since_s) < s_pump.confirm_s) return;

  /* Latch: drop the AUTO demand for the hold period. */
  s_pump_blocked = true;
  s_pump_blocked_until_s = now_s + s_pump.hold_s;
  s_room_at_block_c100 = in->room_c100;
  s_pump_bad_since_s = 0;
  heater_cycle_force_off(&s_cycle, now_s);
  ctl_send(now_s, 1000);
  CTL_LOG("PUMP BLOCKED: coil low for %us, hold %us",
          (unsigned)s_pump.confirm_s, (unsigned)s_pump.hold_s);
}

void heater_ctl_tick(uint32_t now_s, const HeaterCtlInputs *in) {
  if (!s_initialized) heater_ctl_init();
  if (!in) return;

  const HeaterConfig *cfg = heater_config_get();
  const uint16_t period = cfg->cycle.control_period_s
                              ? cfg->cycle.control_period_s
                              : 60;

  if (cfg->mode != s_cfg_mode) {
    CTL_LOG("mode %d -> %d", (int)s_cfg_mode, (int)cfg->mode);
    s_cfg_mode = cfg->mode;
    s_override = false;
    s_override_until_s = 0;
    s_pump_blocked = false;
    s_pump_blocked_until_s = 0;
    s_pump_bad_since_s = 0;
    if (cfg->mode == HEATER_MODE_OFF)
      heater_cycle_force_off(&s_cycle, now_s);
    s_last_run_s = now_s - period; /* decide on the next tick */
  }

  s_rt.can_green = in->can_green;
  s_rt.clock_synced = in->clock_synced;
  s_rt.hold = HEATER_HOLD_NONE;

  const bool auto_mode = (cfg->mode == HEATER_MODE_AUTO);

  /* A foreign writer yields AUTO for a bounded time. */
  if (auto_mode && !s_override && in->pwm_known &&
      in->pwm_src == HEATER_CTL_PWM_SRC_NRF) {
    s_override = true;
    s_override_until_s = now_s + HEATER_OVERRIDE_MAX_S;
    heater_cycle_force_off(&s_cycle, now_s);
    CTL_LOG("OVERRIDE: nrf writer; yield %us",
            (unsigned)HEATER_OVERRIDE_MAX_S);
  }
  if (s_override && (int32_t)(now_s - s_override_until_s) >= 0) {
    s_override = false;
    s_override_until_s = 0;
    s_last_run_s = now_s - period;
    CTL_LOG("OVERRIDE expired: back to auto");
  }
  if (!auto_mode) {
    s_override = false;
    s_override_until_s = 0;
  }

  /* A boiler fault is a disturbance, never plant gain. Log it once. */
  const bool fault = in->pwm_known && in->pwm_src == HEATER_CTL_PWM_SRC_FAULT;
  if (fault && !s_fault_seen) {
    s_fault_seen = true;
    CTL_LOG("disturbance: boiler fault reported on 0x280");
  } else if (!fault && s_fault_seen) {
    s_fault_seen = false;
  }

  /* 0x280 disagreeing with our last frame is logged once, not retried. */
  if (s_expected_valid && in->pwm_known &&
      in->pwm_permille != s_expected_duty) {
    if (!s_mismatch_logged) {
      s_mismatch_logged = true;
      CTL_LOG("0x280 conflict: expected %u, reported %u (holding)",
              (unsigned)s_expected_duty, (unsigned)in->pwm_permille);
    }
  } else if (in->pwm_known && in->pwm_permille == s_expected_duty) {
    s_mismatch_logged = false;
  }

  const bool can_hold = auto_mode && !in->can_green;
  const bool clock_hold =
      auto_mode && (!in->clock_synced || in->utc_epoch == 0);

  uint16_t sched_duty = 1000;
  HeaterScheduleReason sched_reason = HEATER_SCHED_UNSYNCED;
  if (auto_mode && !can_hold && !clock_hold && !s_override) {
    pump_guard_update(now_s, in);

    if (!s_pump_blocked) {
      if (heater_schedule_compute(&cfg->schedule, in->utc_epoch,
                                  in->clock_synced, cfg->mode, false,
                                  &sched_duty, &sched_reason)) {
        s_rt.sched_duty = sched_duty;
        s_rt.sched_reason = sched_reason;

        if (elapsed_since(now_s, s_last_run_s) >= period) {
          s_last_run_s = now_s;
          uint16_t planned = 1000;
          const HeaterCycleResult result = heater_cycle_step(
              &s_cycle, &cfg->cycle, now_s, false, sched_duty, &planned);
          s_rt.last_result = result;
          s_rt.planned_duty = planned;
          if (result == HEATER_CYCLE_RESULT_SEND) {
            CTL_LOG("AUTO send %u (sched %s, cycle %d)", (unsigned)planned,
                    (int)sched_reason, (int)s_cycle.state);
            ctl_send(now_s, planned);
          } else {
            CTL_LOG("AUTO hold: %d (req %u, cycle %d)", (int)result,
                    (unsigned)sched_duty, (int)s_cycle.state);
          }
        }
      } else {
        s_rt.sched_reason = sched_reason;
        s_rt.hold = HEATER_HOLD_CLOCK_UNSYNCED;
      }
    }
  }

  if (s_pump_blocked) {
    s_rt.hold = HEATER_HOLD_NONE;
  } else if (can_hold) {
    s_rt.hold = HEATER_HOLD_CAN_NOT_GREEN;
  } else if (clock_hold) {
    s_rt.hold = HEATER_HOLD_CLOCK_UNSYNCED;
  }

  /* Soll hint for the planned duty (never a pipe value). */
  const WaterModel *wm = water_model_default();
  s_rt.soll_valid = s_rt.planned_duty <= 500 && wm &&
                    wm->target_from_duty(s_rt.planned_duty, &s_rt.soll_c100);
  if (!s_rt.soll_valid) s_rt.soll_c100 = 0;

  /* Publish the snapshot. */
  s_rt.mode = s_override ? HEATER_MODE_OVERRIDE : cfg->mode;
  s_rt.cycle = s_cycle.state;
  s_rt.override_active = s_override;
  s_rt.pump_blocked = s_pump_blocked;
  s_rt.edges_last_hour = heater_cycle_edge_count(&s_cycle, now_s);
  s_rt.override_remaining_s =
      (s_override && (int32_t)(s_override_until_s - now_s) > 0)
          ? (s_override_until_s - now_s)
          : 0;
  s_rt.pump_blocked_remaining_s =
      (s_pump_blocked && (int32_t)(s_pump_blocked_until_s - now_s) > 0)
          ? (s_pump_blocked_until_s - now_s)
          : 0;

  s_rt.min_on_remaining_s =
      (s_cycle.state == HEATER_CYCLE_HEATING)
          ? remaining_s(now_s, s_cycle.state_since, cfg->cycle.min_on_s)
          : 0;
  s_rt.min_off_remaining_s =
      s_cycle.have_last_off
          ? remaining_s(now_s, s_cycle.last_off_s, cfg->cycle.min_off_s)
          : 0;
  s_rt.duty_interval_remaining_s =
      (s_cycle.state == HEATER_CYCLE_HEATING)
          ? remaining_s(now_s, s_cycle.last_duty_change_s,
                        cfg->cycle.min_duty_interval_s)
          : 0;
}

void heater_ctl_note_sent(uint16_t duty) {
  if (!s_initialized) heater_ctl_init();
  s_rt.last_sent_duty = duty;
  s_rt.have_sent = true;
  s_expected_duty = duty;
  s_expected_valid = true;
  s_mismatch_logged = false;
}

const HeaterRuntime *heater_ctl_get(void) { return &s_rt; }
