/**
 * @file haus_heater.cpp
 * @brief Scheduled flow law (v1, §5.3) and authority management (0.4) for the
 *        28_hauszentrale panel.
 *
 * Implements the day/night schedule that determines the boiler flow
 * temperature (Soll) and maps it to a duty value via the adopted inverse
 * table.
 *
 * Authority management (0.4) handles mode transitions, boot restoration,
 * foreign writer handling, and hold conditions.
 *
 * See §5.1, §5.2, and §5.3 of whitepaper_auto_heating.md.
 */

#include "haus_heater.h"
#include "haus_water_model.h"

#include <ctime>
#include <cstring>

/* ─── CET/CEST timezone handling ─────────────────────────────────────────── */

/* Convert a UTC epoch to local time (CET/CEST).
 *
 * The firmware TZ is "CET-1CEST,M3.5.0/2:00,M10.5.0/3:00".
 * CET = UTC+1, CEST = UTC+2.
 *
 * Returns the local time broken down, or false if the conversion fails. */
static bool epoch_to_local_time(uint32_t utc_epoch, struct tm *result) {
  time_t utc_t = (time_t)utc_epoch;
  struct tm utc_tm;
  gmtime_r(&utc_t, &utc_tm);

  /* Find the last Sunday of March and October for DST transitions. */
  struct tm march_first = utc_tm;
  march_first.tm_mon = 2;  /* March */
  march_first.tm_mday = 1;
  mktime(&march_first);

  const int march_first_wday = march_first.tm_wday;
  const int last_sunday_march = 31 - (31 - march_first.tm_mday + march_first_wday) % 7;
  struct tm dst_start = march_first;
  dst_start.tm_mday = last_sunday_march;
  dst_start.tm_hour = 1;
  dst_start.tm_min = 0;
  dst_start.tm_isdst = -1;
  mktime(&dst_start);

  struct tm oct_first = utc_tm;
  oct_first.tm_mon = 9;  /* October */
  oct_first.tm_mday = 1;
  mktime(&oct_first);

  const int oct_first_wday = oct_first.tm_wday;
  const int last_sunday_oct = 31 - (31 - oct_first.tm_mday + oct_first_wday) % 7;
  struct tm dst_end = oct_first;
  dst_end.tm_mday = last_sunday_oct;
  dst_end.tm_hour = 1;
  dst_end.tm_min = 0;
  dst_end.tm_isdst = -1;
  mktime(&dst_end);

  time_t dst_start_t = mktime(&dst_start);
  time_t dst_end_t = mktime(&dst_end);

  /* Apply offset: +2 during CEST, +1 during CET. */
  const bool is_dst = (utc_t >= dst_start_t && utc_t < dst_end_t);
  const int offset_sec = is_dst ? 7200 : 3600;

  time_t local_epoch = utc_t + offset_sec;
  struct tm *local = localtime_r(&local_epoch, result);
  return local != nullptr;
}

/* ─── Public API: Scheduled flow law ─────────────────────────────────────── */

void heater_schedule_default(HeaterSchedule *schedule) {
  memset(schedule, 0, sizeof(*schedule));
  schedule->day_start_hour = 6;
  schedule->night_start_hour = 22;
  schedule->T_day_c100 = 5500;     /* 55 °C */
  schedule->night_setback_c100 = 1500;  /* 15 K */
  schedule->night_floor_c100 = 4200;    /* 42 °C floor */
}

bool heater_schedule_compute(const HeaterSchedule *schedule,
                              uint32_t utc_epoch,
                              bool clock_synced,
                              HeaterMode mode,
                              bool frost_active,
                              uint16_t *out_duty,
                              HeaterScheduleReason *out_reason) {
  if (!schedule || !out_duty || !out_reason) return false;

  const WaterModel *wm = water_model_default();
  if (!wm) return false;

  /* Mode OFF: schedule is not applied, send real OFF immediately. */
  if (mode == HEATER_MODE_OFF) {
    *out_duty = 1000;
    *out_reason = HEATER_SCHED_MODE_OFF;
    return true;
  }

  /* Unsynced clock: hold the restored duty, do not apply the day flow. */
  if (!clock_synced || utc_epoch == 0) {
    *out_duty = 1000;  /* hold — real OFF until synced */
    *out_reason = HEATER_SCHED_UNSYNCED;
    return false;
  }

  /* Convert UTC epoch to local time for schedule evaluation. */
  struct tm local_tm;
  if (!epoch_to_local_time(utc_epoch, &local_tm)) {
    *out_duty = 1000;
    *out_reason = HEATER_SCHED_UNSYNCED;
    return false;
  }

  const uint8_t local_hour = (uint8_t)local_tm.tm_hour;
  const bool is_day = (local_hour >= schedule->day_start_hour &&
                       local_hour < schedule->night_start_hour);

  /* Frost demand overrides the schedule, independent of day/night. */
  if (frost_active) {
    /* Frost demand uses the night floor temperature (42 °C) as the frost
     * setpoint. This is a modest heat level — enough to prevent freezing
     * damage without wasting energy. */
    const int16_t frost_target_c100 = schedule->night_floor_c100;

    /* Binary search for the duty that produces the frost temperature.
     * The inverse table is monotonic: higher duty → lower temperature. */
    uint16_t lo = 100, hi = 500;
    while (lo < hi) {
      const uint16_t mid = (lo + hi + 1) / 2;
      int16_t mid_target;
      if (wm->target_from_duty(mid, &mid_target) &&
          mid_target >= frost_target_c100) {
        lo = mid;
      } else {
        hi = mid - 1;
      }
    }
    *out_duty = lo;
    *out_reason = HEATER_SCHED_FROST;
    return true;
  }

  /* Day/Night schedule evaluation. */
  if (is_day) {
    /* Day flow: T_flow = T_day → duty via the adopted inverse table.
     * Binary search for the duty that produces T_day. */
    const int16_t day_target_c100 = schedule->T_day_c100;
    uint16_t lo = 100, hi = 500;
    while (lo < hi) {
      const uint16_t mid = (lo + hi + 1) / 2;
      int16_t mid_target;
      if (wm->target_from_duty(mid, &mid_target) &&
          mid_target >= day_target_c100) {
        lo = mid;
      } else {
        hi = mid - 1;
      }
    }
    *out_duty = lo;
    *out_reason = HEATER_SCHED_DAY;
    return true;
  }

  /* Night flow: T_flow = T_day − setback, floored at night_floor. */
  int16_t night_flow_c100 = schedule->T_day_c100 - schedule->night_setback_c100;
  if (night_flow_c100 < schedule->night_floor_c100) {
    night_flow_c100 = schedule->night_floor_c100;
  }

  /* Binary search for the duty that produces the night flow temperature. */
  uint16_t lo = 100, hi = 500;
  while (lo < hi) {
    const uint16_t mid = (lo + hi + 1) / 2;
    int16_t mid_target;
    if (wm->target_from_duty(mid, &mid_target) &&
        mid_target >= night_flow_c100) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  *out_duty = lo;
  *out_reason = HEATER_SCHED_NIGHT;
  return true;
}

/* ─── Public API: Authority management (0.4) ─────────────────────────────── */

/* Default override max duration: 2 hours (7200 seconds). */
#define HEATER_DEFAULT_OVERRIDE_MAX 7200

void heater_authority_default(HeaterAuthority *auth) {
  if (!auth) return;
  memset(auth, 0, sizeof(*auth));
  auth->mode = HEATER_MODE_OFF;
  auth->saved_duty = 1000;
  auth->override_max_seconds = HEATER_DEFAULT_OVERRIDE_MAX;
  auth->can_green = false;
  auth->sensors_valid = false;
  auth->clock_synced = false;
  auth->mode_off_locked = false;
  auth->last_transmitted_duty = 1000;
  auth->hold_reason = HEATER_HOLD_NONE;
  auth->eeprom_last_write_epoch = 0;
}

bool heater_handle_manual_off(HeaterAuthority *auth,
                               uint16_t current_duty_on_bus) {
  if (!auth) return false;

  /* Switch to OFF mode, lock it. */
  auth->mode = HEATER_MODE_OFF;
  auth->mode_off_locked = true;
  auth->hold_reason = HEATER_HOLD_NONE;
  auth->saved_duty = 1000;  /* OFF has no saved duty */
  auth->override_start_epoch = 0;  /* Cancel any active OVERRIDE */

  /* If the bus already shows 1000, no transmit needed. */
  if (current_duty_on_bus == 1000) {
    return false;
  }

  /* Bus shows a heating duty — send 1000 once. */
  auth->last_transmitted_duty = 1000;
  return true;
}

bool heater_handle_manual_duty(HeaterAuthority *auth,
                                uint16_t user_duty,
                                uint16_t current_duty_on_bus) {
  if (!auth) return false;

  /* Switch to MANUAL mode, unlock OFF. */
  auth->mode = HEATER_MODE_MANUAL;
  auth->mode_off_locked = false;
  auth->hold_reason = HEATER_HOLD_NONE;
  auth->saved_duty = user_duty;
  auth->override_start_epoch = 0;  /* Cancel any active OVERRIDE */

  /* If the bus already shows this duty, no transmit needed. */
  if (current_duty_on_bus == user_duty) {
    return false;
  }

  /* Bus shows a different duty — send user_duty once. */
  auth->last_transmitted_duty = user_duty;
  return true;
}

bool heater_try_enter_auto(HeaterAuthority *auth, uint16_t computed_duty) {
  if (!auth) return false;

  /* Check all preconditions for AUTO entry. */
  if (!auth->can_green) {
    auth->hold_reason = HEATER_HOLD_CAN_NOT_GREEN;
    return false;
  }
  if (!auth->sensors_valid) {
    auth->hold_reason = HEATER_HOLD_SENSORS_INVALID;
    return false;
  }
  if (!auth->clock_synced) {
    auth->hold_reason = HEATER_HOLD_CLOCK_UNSYNCED;
    return false;
  }
  if (auth->mode_off_locked) {
    auth->hold_reason = HEATER_HOLD_MODE_OFF_LOCKED;
    return false;
  }

  /* All preconditions met — enter AUTO. */
  auth->mode = HEATER_MODE_AUTO;
  auth->hold_reason = HEATER_HOLD_NONE;
  auth->override_start_epoch = 0;  /* Cancel any active OVERRIDE */
  auth->last_transmitted_duty = computed_duty;
  return true;
}

bool heater_handle_foreign_writer(HeaterAuthority *auth,
                                   uint32_t monotonic_now,
                                   uint16_t duty_from_bus) {
  (void)duty_from_bus;
  if (!auth) return false;

  /* If already in OVERRIDE and within bounds, no change. */
  if (auth->mode == HEATER_MODE_OVERRIDE &&
      auth->override_start_epoch > 0 &&
      (monotonic_now - auth->override_start_epoch) < auth->override_max_seconds) {
    return false;
  }

  /* Switch to OVERRIDE, record start time. */
  auth->mode = HEATER_MODE_OVERRIDE;
  auth->override_start_epoch = monotonic_now;
  auth->hold_reason = HEATER_HOLD_NONE;
  /* Do not transmit — yield to foreign writer. */
  return true;
}

bool heater_check_override_expiry(HeaterAuthority *auth,
                                   uint32_t monotonic_now) {
  if (!auth) return false;

  /* Only check if in OVERRIDE with a recorded start time. */
  if (auth->mode != HEATER_MODE_OVERRIDE || auth->override_start_epoch == 0) {
    return false;
  }

  /* Check if OVERRIDE has expired. */
  if ((monotonic_now - auth->override_start_epoch) < auth->override_max_seconds) {
    return false;  /* Still within bounds. */
  }

  /* OVERRIDE expired — try to return to AUTO. */
  /* Note: we don't change mode here; the caller decides based on
   * preconditions. We just clear the override state. */
  auth->override_start_epoch = 0;

  /* Try to enter AUTO (this will check preconditions and set hold if needed). */
  /* We return true to indicate the mode transition happened (to AUTO or hold). */
  /* The actual mode is determined by try_enter_auto. */
  return true;
}

bool heater_handle_sensor_loss(HeaterAuthority *auth, bool stale) {
  if (!auth) return false;

  /* Update the sensor validity flag. */
  auth->sensors_valid = !stale;

  /* Only matters when in AUTO mode. */
  if (auth->mode != HEATER_MODE_AUTO) {
    return false;
  }

  if (stale) {
    /* Sensor data is stale — set hold. */
    auth->hold_reason = HEATER_HOLD_SENSOR_STALE;
    return true;
  }

  /* Sensors recovered — clear the sensor-stale hold. */
  if (auth->hold_reason == HEATER_HOLD_SENSOR_STALE) {
    auth->hold_reason = HEATER_HOLD_NONE;
    return true;
  }

  return false;
}

bool heater_handle_clock_unsynced(HeaterAuthority *auth, bool synced) {
  if (!auth) return false;

  /* Update the clock sync flag. */
  auth->clock_synced = synced;

  /* Only matters when in AUTO mode. */
  if (auth->mode != HEATER_MODE_AUTO) {
    return false;
  }

  if (!synced) {
    /* Clock unsynced — set hold. */
    auth->hold_reason = HEATER_HOLD_CLOCK_UNSYNCED;
    return true;
  }

  /* Clock recovered — clear the unsynced hold. */
  if (auth->hold_reason == HEATER_HOLD_CLOCK_UNSYNCED) {
    auth->hold_reason = HEATER_HOLD_NONE;
    return true;
  }

  return false;
}

bool heater_handle_can_state(HeaterAuthority *auth, bool green) {
  if (!auth) return false;

  /* Update the CAN state flag. */
  auth->can_green = green;

  /* Only matters when in AUTO mode. */
  if (auth->mode != HEATER_MODE_AUTO) {
    return false;
  }

  if (!green) {
    /* CAN not healthy — set hold. */
    auth->hold_reason = HEATER_HOLD_CAN_NOT_GREEN;
    return true;
  }

  /* CAN recovered — clear the CAN hold. */
  if (auth->hold_reason == HEATER_HOLD_CAN_NOT_GREEN) {
    auth->hold_reason = HEATER_HOLD_NONE;
    return true;
  }

  return false;
}

bool heater_get_transmit_duty(const HeaterAuthority *auth,
                               uint16_t computed_duty,
                               uint16_t current_duty_on_bus,
                               uint16_t *out_duty,
                               bool *out_send,
                               HeaterTxSource *out_src) {
  if (!auth || !out_duty || !out_send || !out_src) return false;

  switch (auth->mode) {
    case HEATER_MODE_OFF:
      /* OFF mode: transmit 1000. */
      *out_duty = 1000;
      *out_send = (current_duty_on_bus != 1000);
      *out_src = HEATER_TX_AUTO;  /* Schedule-driven OFF */
      return true;

    case HEATER_MODE_MANUAL:
      /* MANUAL mode: transmit saved_duty. */
      *out_duty = auth->saved_duty;
      *out_send = (current_duty_on_bus != auth->saved_duty);
      *out_src = HEATER_TX_MANUAL;
      return true;

    case HEATER_MODE_AUTO: {
      /* AUTO mode: transmit computed duty if preconditions met. */
      *out_duty = computed_duty;
      bool can_transmit = auth->can_green && auth->sensors_valid &&
                          auth->clock_synced && !auth->mode_off_locked;
      *out_send = can_transmit && (current_duty_on_bus != computed_duty);
      *out_src = HEATER_TX_AUTO;
      return true;
    }

    case HEATER_MODE_OVERRIDE:
      /* OVERRIDE: yield to foreign writer, do not transmit. */
      *out_duty = 1000;  /* dummy — we don't transmit */
      *out_send = false;
      *out_src = HEATER_TX_NRF;
      return true;
  }

  *out_duty = 1000;
  *out_send = false;
  *out_src = HEATER_TX_NONE;
  return false;
}

/* EEPROM write cooldown: minimum seconds between EEPROM writes. */
#define HEATER_EEPROM_WRITE_COOLDOWN 60

/* Maximum duty decrease per EEPROM write (lowest-permille walkback). */
#define HEATER_EEPROM_MAX_DECREASE 1

bool heater_handle_boot_restore(HeaterAuthority *auth,
                                 HeaterMode persisted_mode,
                                 uint16_t persisted_duty,
                                 uint16_t current_duty_on_bus,
                                 bool can_green,
                                 bool sensors_valid,
                                 bool clock_synced,
                                 uint16_t *out_duty,
                                 bool *out_send,
                                 HeaterTxSource *out_src) {
  if (!auth || !out_duty || !out_send || !out_src) return false;

  /* Restore the persisted mode and state. */
  auth->mode = persisted_mode;
  auth->saved_duty = persisted_duty;
  auth->can_green = can_green;
  auth->sensors_valid = sensors_valid;
  auth->clock_synced = clock_synced;
  auth->mode_off_locked = (persisted_mode == HEATER_MODE_OFF);
  auth->hold_reason = HEATER_HOLD_NONE;
  /* Save boot time before clearing override state — needed for EEPROM write
   * window check. */
  uint32_t boot_epoch = auth->override_start_epoch;
  auth->override_start_epoch = 0;

  /* Handle each persisted mode according to boot transmit rules. */
  switch (persisted_mode) {
    case HEATER_MODE_OFF:
      /* OFF + report is 1000: send nothing. */
      /* OFF + report is heating duty: send 1000 once. */
      *out_duty = 1000;
      *out_send = (current_duty_on_bus != 1000);
      *out_src = HEATER_TX_EEPROM;
      return true;

    case HEATER_MODE_MANUAL: {
      /* Send saved_duty only when report differs.
       * Respect the EEPROM write window (60 s) and lowest-permille walkback. */
      uint16_t tx_duty = persisted_duty;
      bool should_send = (current_duty_on_bus != persisted_duty);

      if (should_send) {
        /* Check 60-second write window: if the last EEPROM write was recent,
         * the boiler likely already has the value from that write. Don't
         * transmit again — just show PWM_SRC_EEPROM. */
        if (auth->eeprom_last_write_epoch > 0 &&
            (auth->eeprom_last_write_epoch + HEATER_EEPROM_WRITE_COOLDOWN >
             boot_epoch)) {
          *out_duty = persisted_duty;
          *out_send = false;
          *out_src = HEATER_TX_EEPROM;
          return true;
        }

        /* Lowest-permille walkback: if the persisted duty is lower than the
         * current bus duty, don't jump directly — walk back at most 1
         * permille per write. This prevents sudden large drops that could
         * confuse the boiler's control loop. */
        if (persisted_duty < current_duty_on_bus) {
          const uint16_t max_decrease =
              (current_duty_on_bus > persisted_duty + HEATER_EEPROM_MAX_DECREASE)
                  ? HEATER_EEPROM_MAX_DECREASE
                  : (current_duty_on_bus - persisted_duty);
          tx_duty = current_duty_on_bus - max_decrease;
          if (tx_duty < persisted_duty) {
            tx_duty = persisted_duty;
          }
        }
      }

      *out_duty = tx_duty;
      *out_send = (current_duty_on_bus != tx_duty);
      *out_src = HEATER_TX_EEPROM;
      return true;
    }

    case HEATER_MODE_AUTO:
      /* Send nothing until first legal decision; show PWM_SRC_EEPROM. */
      *out_duty = 1000;  /* dummy — no transmit on boot for AUTO */
      *out_send = false;
      *out_src = HEATER_TX_EEPROM;
      return true;

    case HEATER_MODE_OVERRIDE:
      /* Handle PWM_SRC_NRF and foreign 0x280 → yield, don't overwrite remote. */
      *out_duty = 1000;  /* dummy — yield to foreign writer */
      *out_send = false;
      *out_src = HEATER_TX_NRF;
      return true;
  }

  *out_duty = 1000;
  *out_send = false;
  *out_src = HEATER_TX_NONE;
  return false;
}

bool heater_is_mode_off_locked(const HeaterAuthority *auth) {
  if (!auth) return true;
  return auth->mode_off_locked;
}

HeaterHoldReason heater_get_hold_reason(const HeaterAuthority *auth) {
  if (!auth) return HEATER_HOLD_NONE;
  return auth->hold_reason;
}
