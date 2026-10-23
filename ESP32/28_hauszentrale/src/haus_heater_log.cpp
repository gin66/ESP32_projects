/**
 * @file haus_heater_log.cpp
 * @brief 1 Hz heater experiment trace on SD.
 */

#include "haus_heater_log.h"

#include <Arduino.h>
#include <SD.h>

#include <cstdio>

#include "haus_heater_ctl.h"
#include "haus_sd.h"
#include "tpl_debug_log.h"

#define HEATER_LOG_RETRY_MS (60u * 1000u)

static char s_path[64] = "";
static bool s_ready = false;
static bool s_warned = false;
static uint32_t s_next_retry_ms = 0;
static uint32_t s_seq = 0;

static void temp_field(char *dst, size_t len, int16_t c100, bool valid) {
  if (!valid || c100 == HAUS_CAN_TEMP_EMPTY)
    dst[0] = '\0';
  else
    snprintf(dst, len, "%.2f", c100 / 100.0);
}

static bool log_start(uint32_t now_ms, const HausState *st) {
  if (!haus_sd_is_mounted()) return false;
  if (!haus_sd_lock(2000)) return false;

  const bool have_time = st->can.synced && st->can.timestamp != 0;
  const uint32_t id = have_time ? st->can.timestamp : (now_ms / 1000u);
  char dir[48];
  snprintf(dir, sizeof(dir), "/experiments/heater_%lu", (unsigned long)id);

  SD.mkdir("/experiments");
  SD.mkdir(dir);
  snprintf(s_path, sizeof(s_path), "%s/trace.csv", dir);

  File f = SD.open(s_path, FILE_APPEND);
  const bool ok = (bool)f;
  if (f) f.close();
  haus_sd_unlock();

  if (!ok) {
    if (!s_warned) {
      s_warned = true;
      TPL_LOGW("heater_log", "cannot open %s", s_path);
    }
    s_path[0] = '\0';
    return false;
  }

  s_ready = true;
  s_seq = 0;
  TPL_LOGI("heater_log", "trace %s", s_path);
  return true;
}

void heater_log_init(void) {
  s_path[0] = '\0';
  s_ready = false;
  s_warned = false;
  s_next_retry_ms = 0;
  s_seq = 0;
}

const char *heater_log_path(void) { return s_path; }

void heater_log_tick(uint32_t now_ms, const HausState *st) {
  if (!st) return;
  if (!s_ready) {
    if ((int32_t)(now_ms - s_next_retry_ms) < 0 && s_next_retry_ms != 0)
      return;
    if (!log_start(now_ms, st)) {
      s_next_retry_ms = now_ms + HEATER_LOG_RETRY_MS;
      return;
    }
  }

  const HeaterRuntime *rt = heater_ctl_get();
  const bool room_valid =
      (st->climate.climate_flags & CLIMATE_VALID_SHT45) != 0;
  char water_in[16] = "";
  char water_out[16] = "";
  char room_c[16] = "";
  char room_rh[16] = "";
  char target_c[16] = "";
  temp_field(water_in, sizeof(water_in),
             st->can.temp_by_fn[SENSOR_FN_WATER_IN], true);
  temp_field(water_out, sizeof(water_out),
             st->can.temp_by_fn[SENSOR_FN_WATER_OUT], true);
  if (room_valid) {
    snprintf(room_c, sizeof(room_c), "%.2f", st->climate.living_c100 / 100.0);
    snprintf(room_rh, sizeof(room_rh), "%.2f", st->climate.hum_c100 / 100.0);
  }
  if (rt->soll_valid)
    snprintf(target_c, sizeof(target_c), "%.2f", rt->soll_c100 / 100.0);

  /* pump_state comes from the coil, never from the duty-derived home label. */
  const int pump_state =
      st->can.ac_fresh ? (st->can.ac_rms_dc > 0 ? 1 : 0) : -1;
  const int duty_req = rt->have_sent ? (int)rt->last_sent_duty : -1;
  const int duty_rpt = st->can.pwm_known ? (int)st->can.pwm_permille : -1;
  const bool have_time = st->can.synced && st->can.timestamp != 0;
  const unsigned long long epoch_ms =
      have_time ? (unsigned long long)st->can.timestamp * 1000ull +
                      (now_ms % 1000u)
                : (unsigned long long)now_ms;

  const char *mode = "off";
  switch (rt->mode) {
    case HEATER_MODE_OFF:
      mode = "off";
      break;
    case HEATER_MODE_MANUAL:
      mode = "manual";
      break;
    case HEATER_MODE_AUTO:
      mode = "auto";
      break;
    case HEATER_MODE_OVERRIDE:
      mode = "override";
      break;
  }
  const char *cycle = "idle";
  if (rt->cycle == HEATER_CYCLE_HEATING)
    cycle = "heating";
  else if (rt->cycle == HEATER_CYCLE_COOLDOWN)
    cycle = "cooldown";
  const char *hold = "none";
  switch (rt->hold) {
    case HEATER_HOLD_NONE:
      hold = "none";
      break;
    case HEATER_HOLD_CAN_NOT_GREEN:
      hold = "can";
      break;
    case HEATER_HOLD_SENSORS_INVALID:
      hold = "sensors";
      break;
    case HEATER_HOLD_CLOCK_UNSYNCED:
      hold = "clock";
      break;
    case HEATER_HOLD_MODE_OFF_LOCKED:
      hold = "off-locked";
      break;
    case HEATER_HOLD_SENSOR_STALE:
      hold = "stale";
      break;
    case HEATER_HOLD_SCHED_OFF:
      hold = "sched-off";
      break;
  }

  char note[72];
  snprintf(note, sizeof(note),
           "mode=%s cycle=%s hold=%s result=%d block=%d override=%d", mode,
           cycle, hold, (int)rt->last_result, rt->pump_blocked ? 1 : 0,
           rt->override_active ? 1 : 0);

  char line[256];
  snprintf(line, sizeof(line),
           "%llu,%lu,%d,%d,%s,%s,%s,%s,%u,%d,%d,%d,%s,%s\n", epoch_ms,
           (unsigned long)s_seq, duty_req, duty_rpt, water_in, water_out,
           room_c, room_rh, (unsigned)st->can.ac_rms_dc,
           (int)st->can.ac_angle_cdeg, pump_state, st->can.can_status ==
                                                     GUI_STATUS_OK
                                                 ? 1
                                                 : 0,
           target_c, note);

  if (!haus_sd_is_mounted()) {
    s_ready = false;
    return;
  }
  if (!haus_sd_lock(1000)) return;

  File f = SD.open(s_path, FILE_APPEND);
  if (!f) {
    haus_sd_note_io_error();
    haus_sd_unlock();
    s_ready = false;
    s_path[0] = '\0';
    s_next_retry_ms = now_ms + HEATER_LOG_RETRY_MS;
    return;
  }
  if (f.size() == 0) {
    f.println(
        "epoch_ms,seq,duty_req,duty_rpt,water_in_c,water_out_c,room_c,room_rh,"
        "ac_rms_dc,ac_angle_cdeg,pump_state,can_ok,target_c,note");
  }
  f.print(line);
  f.close();
  haus_sd_unlock();
  s_seq++;
}
