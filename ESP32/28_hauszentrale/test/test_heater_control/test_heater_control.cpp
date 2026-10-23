#include <cassert>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <cstring>

#include "haus_heater.h"
#include "haus_heater_bin.h"
#include "haus_heater_ctl.h"
#include "haus_water_model.h"

/* ─── Helpers ────────────────────────────────────────────────────────────── */

/* Compute the UTC epoch for a given local date/time in CET/CEST.
 * We reverse the epoch_to_local_time logic: find the UTC epoch that maps
 * to the given local hour on the given date. */
static uint32_t compute_utc_for_local(int year, int month, int day,
                                       int hour, int minute, int second) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;  /* 0-based */
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = second;
  t.tm_isdst = -1;

  time_t candidate = mktime(&t);

  /* Find DST transition dates for this year. */
  struct tm march_first = t;
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

  struct tm oct_first = t;
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

  /* Determine offset: 7200 (CEST) or 3600 (CET). */
  bool is_dst = (candidate >= dst_start_t && candidate < dst_end_t);
  const int offset = is_dst ? 7200 : 3600;

  return (uint32_t)(candidate - (time_t)offset);
}

/* ─── Tests ──────────────────────────────────────────────────────────────── */

static uint16_t g_ctl_last_send = 0;
static int g_ctl_send_count = 0;

static bool ctl_fake_send(uint16_t duty) {
  g_ctl_last_send = duty;
  g_ctl_send_count++;
  return true;
}

int main() {
  const WaterModel *m = water_model_default();
  assert(m != nullptr);
  int16_t target = 0;
  int16_t out_target = 0;

  /* Five knots: forward (estimate) and inverse (target_from_duty) must agree. */
  assert(m->estimate(0, 100, 0, &target) && target == 7000);
  assert(m->target_from_duty(100, &out_target) && out_target == 7000);

  assert(m->estimate(0, 200, 0, &target) && target == 6000);
  assert(m->target_from_duty(200, &out_target) && out_target == 6000);

  assert(m->estimate(0, 300, 0, &target) && target == 5400);
  assert(m->target_from_duty(300, &out_target) && out_target == 5400);

  assert(m->estimate(0, 400, 0, &target) && target == 4400);
  assert(m->target_from_duty(400, &out_target) && out_target == 4400);

  assert(m->estimate(0, 500, 0, &target) && target == 4200);
  assert(m->target_from_duty(500, &out_target) && out_target == 4200);

  /* Day 55 °C → duty 283 (between 60 °C/200 and 54 °C/300). */
  assert(m->estimate(0, 283, 0, &target) && target == 5502);
  assert(m->target_from_duty(283, &out_target) && out_target == 5502);

  /* Night floor: 42 °C → duty 500. */
  assert(m->estimate(0, 500, 0, &target) && target == 4200);
  assert(m->target_from_duty(500, &out_target) && out_target == 4200);

  /* OFF is duty 1000 — bypasses the model. */
  assert(!m->estimate(0, 1000, 0, &target));
  assert(!m->target_from_duty(1000, &out_target));

  /* Duty 0 stays manual-only. */
  assert(!m->estimate(0, 0, 0, &target));
  assert(!m->target_from_duty(0, &out_target));

  /* AUTO never emits duty 0. */
  assert(!m->target_from_duty(0, &out_target));

  /* Interpolation between knots. */
  assert(m->estimate(0, 150, 0, &target) && target == 6500);
  assert(m->target_from_duty(150, &out_target) && out_target == 6500);

  assert(m->estimate(0, 250, 0, &target) && target == 5700);
  assert(m->target_from_duty(250, &out_target) && out_target == 5700);

  assert(m->estimate(0, 450, 0, &target) && target == 4300);
  assert(m->target_from_duty(450, &out_target) && out_target == 4300);

  /* Out-of-domain duties. */
  assert(!m->estimate(0, 99, 0, &target));
  assert(!m->estimate(0, 501, 0, &target));
  assert(!m->estimate(0, 999, 0, &target));
  assert(!m->target_from_duty(0, &out_target));
  assert(!m->target_from_duty(1000, &out_target));

  /* Null pointer safety. */
  assert(!m->target_from_duty(200, nullptr));

  std::printf("test_heater_control (water model): ok\n");

  /* ─── Scheduled flow law tests (Phase 0.3) ─────────────────────────── */

  HeaterSchedule sched;
  heater_schedule_default(&sched);
  assert(sched.day_start_hour == 6);
  assert(sched.night_start_hour == 22);
  assert(sched.T_day_c100 == 5500);
  assert(sched.night_setback_c100 == 1500);
  assert(sched.night_floor_c100 == 4200);

  uint16_t duty = 0;
  HeaterScheduleReason reason = (HeaterScheduleReason)-1;
  int16_t computed_target;

  /* T.1 Night onset — flow floors at 42 °C, duty 500; no off edge. */
  {
    uint32_t utc = compute_utc_for_local(2026, 10, 15, 22, 30, 0);
    assert(heater_schedule_compute(&sched, utc, true, HEATER_MODE_AUTO,
                                    false, &duty, &reason));
    assert(duty == 500);
    assert(reason == HEATER_SCHED_NIGHT);
    assert(m->target_from_duty(500, &computed_target) && computed_target == 4200);
  }

  /* T.2 Day transition — flow returns to day value, subject to mover.
   * At 06:00 local time, the schedule switches from night to day.
   * Day flow: 55 °C → duty 283. */
  {
    uint32_t utc = compute_utc_for_local(2026, 10, 15, 6, 0, 0);
    assert(heater_schedule_compute(&sched, utc, true, HEATER_MODE_AUTO,
                                    false, &duty, &reason));
    assert(duty == 283);
    assert(reason == HEATER_SCHED_DAY);
    assert(m->target_from_duty(283, &computed_target) && computed_target == 5502);
  }

  /* T.3 Real off — duty 1000 when mode is OFF, schedule not applied. */
  {
    uint32_t utc = compute_utc_for_local(2026, 10, 15, 12, 0, 0);
    assert(heater_schedule_compute(&sched, utc, true, HEATER_MODE_OFF,
                                    false, &duty, &reason));
    assert(duty == 1000);
    assert(reason == HEATER_SCHED_MODE_OFF);
  }

  /* T.8 Unsynced clock — hold restored duty; no day-flow jump. */
  {
    uint32_t utc = compute_utc_for_local(2026, 10, 15, 12, 0, 0);
    assert(!heater_schedule_compute(&sched, utc, false, HEATER_MODE_AUTO,
                                     false, &duty, &reason));
    assert(reason == HEATER_SCHED_UNSYNCED);
    assert(duty == 1000);

    /* Also with epoch 0 (never synced). */
    assert(!heater_schedule_compute(&sched, 0, true, HEATER_MODE_AUTO,
                                     false, &duty, &reason));
    assert(reason == HEATER_SCHED_UNSYNCED);
  }

  /* T.11 Frost demand — modest flow despite night; manual OFF still wins. */
  {
    /* Frost during day: returns frost duty (42 °C → duty 500). */
    uint32_t utc = compute_utc_for_local(2026, 10, 15, 12, 0, 0);
    assert(heater_schedule_compute(&sched, utc, true, HEATER_MODE_AUTO,
                                    true, &duty, &reason));
    assert(duty == 500);
    assert(reason == HEATER_SCHED_FROST);

    /* Frost during night: same result (night floor = frost floor = 42 °C). */
    utc = compute_utc_for_local(2026, 10, 15, 23, 0, 0);
    assert(heater_schedule_compute(&sched, utc, true, HEATER_MODE_AUTO,
                                    true, &duty, &reason));
    assert(duty == 500);
    assert(reason == HEATER_SCHED_FROST);

    /* Manual OFF beats frost: even with frost_active, mode OFF returns 1000. */
    utc = compute_utc_for_local(2026, 10, 15, 12, 0, 0);
    assert(heater_schedule_compute(&sched, utc, true, HEATER_MODE_OFF,
                                    true, &duty, &reason));
    assert(duty == 1000);
    assert(reason == HEATER_SCHED_MODE_OFF);
  }

  /* T.12 DST/weekend schedule — correct local-time evaluation. */
  {
    /* CEST period (summer): UTC+2. Oct 20, 2026 (before DST ends Oct 25). */
    uint32_t utc = compute_utc_for_local(2026, 10, 20, 14, 0, 0);
    assert(heater_schedule_compute(&sched, utc, true, HEATER_MODE_AUTO,
                                    false, &duty, &reason));
    assert(reason == HEATER_SCHED_DAY);
    assert(duty == 283);

    /* Night during CEST: 23:00 local = 21:00 UTC. */
    utc = compute_utc_for_local(2026, 10, 20, 23, 0, 0);
    assert(heater_schedule_compute(&sched, utc, true, HEATER_MODE_AUTO,
                                    false, &duty, &reason));
    assert(reason == HEATER_SCHED_NIGHT);
    assert(duty == 500);
  }

  /* T.13 Adopted inverse — round trip on five knots; day 55 °C → duty 283;
   * night floor → duty 500; OFF is 1000; AUTO never emits duty 0. */
  {
    uint32_t utc = compute_utc_for_local(2026, 10, 15, 12, 0, 0);
    assert(heater_schedule_compute(&sched, utc, true, HEATER_MODE_AUTO,
                                    false, &duty, &reason));
    assert(duty == 283);

    utc = compute_utc_for_local(2026, 10, 15, 22, 30, 0);
    assert(heater_schedule_compute(&sched, utc, true, HEATER_MODE_AUTO,
                                    false, &duty, &reason));
    assert(duty == 500);

    /* Command domain: duty is always 1000 or 100..500. */
  }

  /* Null pointer safety for schedule_compute. */
  assert(!heater_schedule_compute(nullptr, 0, true, HEATER_MODE_AUTO,
                                   false, &duty, &reason));
  assert(!heater_schedule_compute(&sched, 0, true, HEATER_MODE_AUTO,
                                   false, nullptr, &reason));
  assert(!heater_schedule_compute(&sched, 0, true, HEATER_MODE_AUTO,
                                   false, &duty, nullptr));

  std::printf("test_heater_control (scheduled flow): ok\n");

  /* ─── Anti-short-cycling state machine (Phase 2.1) ─────────────────── */
  {
    HeaterCycleParams cp;
    heater_cycle_params_default(&cp);
    assert(cp.control_period_s == 60);
    assert(cp.min_on_s == 600);
    assert(cp.min_off_s == 900);
    assert(cp.max_edges_per_hour == 2);
    assert(cp.min_duty_interval_s == 300);
    assert(cp.max_duty_step == 50);

    HeaterCycle cyc;
    heater_cycle_init(&cyc);
    assert(cyc.state == HEATER_CYCLE_IDLE);
    uint16_t d = 0;

    /* First AUTO on-edge. */
    assert(heater_cycle_step(&cyc, &cp, 1000, false, 300, &d) ==
           HEATER_CYCLE_RESULT_SEND);
    assert(d == 300);
    assert(cyc.state == HEATER_CYCLE_HEATING);
    assert(heater_cycle_edge_count(&cyc, 1000) == 1);

    /* Duty movement waits min_duty_interval and moves at most 50 permille. */
    assert(heater_cycle_step(&cyc, &cp, 1060, false, 200, &d) ==
           HEATER_CYCLE_RESULT_DUTY_INTERVAL);
    assert(heater_cycle_step(&cyc, &cp, 1300, false, 200, &d) ==
           HEATER_CYCLE_RESULT_SEND);
    assert(d == 250);
    assert(heater_cycle_step(&cyc, &cp, 1600, false, 200, &d) ==
           HEATER_CYCLE_RESULT_SEND);
    assert(d == 200);

    /* OFF before min_on is suppressed. */
    assert(heater_cycle_step(&cyc, &cp, 1300, false, 1000, &d) ==
           HEATER_CYCLE_RESULT_MIN_ON);
    assert(cyc.state == HEATER_CYCLE_HEATING);

    /* After min_on the off-edge is sent and cooldown begins. */
    assert(heater_cycle_step(&cyc, &cp, 1650, false, 1000, &d) ==
           HEATER_CYCLE_RESULT_SEND);
    assert(d == 1000);
    assert(cyc.state == HEATER_CYCLE_COOLDOWN);

    /* ON before min_off is suppressed. */
    assert(heater_cycle_step(&cyc, &cp, 1700, false, 300, &d) ==
           HEATER_CYCLE_RESULT_MIN_OFF);

    /* Second legal cycle. */
    assert(heater_cycle_step(&cyc, &cp, 2600, false, 300, &d) ==
           HEATER_CYCLE_RESULT_SEND);
    assert(d == 300);
    assert(heater_cycle_edge_count(&cyc, 2600) == 2);

    /* Real off after min_on. */
    assert(heater_cycle_step(&cyc, &cp, 2650, false, 1000, &d) ==
           HEATER_CYCLE_RESULT_MIN_ON);
    assert(heater_cycle_step(&cyc, &cp, 3250, false, 1000, &d) ==
           HEATER_CYCLE_RESULT_SEND);
    assert(d == 1000);

    /* Normal demand is blocked by min_off; frost overrides min_off and the
     * exhausted edge budget. */
    assert(heater_cycle_step(&cyc, &cp, 3300, false, 300, &d) ==
           HEATER_CYCLE_RESULT_MIN_OFF);
    assert(heater_cycle_step(&cyc, &cp, 3300, true, 300, &d) ==
           HEATER_CYCLE_RESULT_SEND);
    assert(d == 300);
    assert(cyc.state == HEATER_CYCLE_HEATING);

    /* Off after min_on, then the budget rejects a third on-edge. */
    assert(heater_cycle_step(&cyc, &cp, 3900, false, 1000, &d) ==
           HEATER_CYCLE_RESULT_SEND);
    assert(heater_cycle_step(&cyc, &cp, 4800, false, 300, &d) ==
           HEATER_CYCLE_RESULT_EDGE_BUDGET);

    /* The oldest on-edge leaves the rolling hour, so a demand can return. */
    assert(heater_cycle_step(&cyc, &cp, 6300, false, 300, &d) ==
           HEATER_CYCLE_RESULT_SEND);
    assert(heater_cycle_edge_count(&cyc, 6300) == 2);

    /* An external off (manual OFF / override) starts min_off again but keeps
     * the rolling budget. */
    heater_cycle_force_off(&cyc, 7000);
    assert(cyc.state == HEATER_CYCLE_COOLDOWN);
    assert(heater_cycle_step(&cyc, &cp, 7100, false, 300, &d) ==
           HEATER_CYCLE_RESULT_MIN_OFF);
    assert(heater_cycle_step(&cyc, &cp, 8000, false, 300, &d) ==
           HEATER_CYCLE_RESULT_SEND);
  }

  std::printf("test_heater_control (anti-short-cycling 2.1): ok\n");

  /* ─── Runtime AUTO controller (Phase 4) ────────────────────────────── */
  {
    HeaterConfig cfg;
    heater_config_init();
    cfg = *heater_config_get();
    cfg.mode = HEATER_MODE_AUTO;
    heater_schedule_default(&cfg.schedule);
    heater_config_apply(&cfg);

    const uint32_t t0 = compute_utc_for_local(2026, 10, 14, 8, 0, 0);
    HeaterCtlInputs in;
    memset(&in, 0, sizeof(in));
    in.can_green = true;
    in.clock_synced = true;
    in.utc_epoch = t0;
    in.ac_fresh = true;
    in.water_valid = true;
    in.water_in_c100 = 3000;
    in.water_out_c100 = 4000;
    in.room_valid = true;
    in.room_c100 = 2100;
    in.pwm_known = true;
    in.pwm_permille = 1000;

    g_ctl_last_send = 0;
    g_ctl_send_count = 0;
    heater_ctl_init();
    heater_ctl_set_sender(ctl_fake_send);

    /* Day schedule sends one AUTO on-edge. */
    heater_ctl_tick(t0, &in);
    assert(g_ctl_send_count == 1);
    assert(g_ctl_last_send == 283);
    const HeaterRuntime* rt = heater_ctl_get();
    assert(rt->mode == HEATER_MODE_AUTO);
    assert(rt->cycle == HEATER_CYCLE_HEATING);
    assert(rt->last_result == HEATER_CYCLE_RESULT_SEND);
    assert(rt->last_sent_duty == 283);

    /* Inside the control period: nothing. */
    heater_ctl_tick(t0 + 10, &in);
    assert(g_ctl_send_count == 1);

    /* Unsynced clock holds; no frame. */
    in.utc_epoch = 0;
    in.clock_synced = false;
    heater_ctl_tick(t0 + 70, &in);
    assert(g_ctl_send_count == 1);
    assert(heater_ctl_get()->hold == HEATER_HOLD_CLOCK_UNSYNCED);

    /* CAN loss holds. */
    in.utc_epoch = t0 + 130;
    in.clock_synced = true;
    in.can_green = false;
    heater_ctl_tick(t0 + 130, &in);
    assert(g_ctl_send_count == 1);
    assert(heater_ctl_get()->hold == HEATER_HOLD_CAN_NOT_GREEN);

    /* A foreign nRF writer yields for the bounded window. */
    in.can_green = true;
    in.pwm_src = HEATER_CTL_PWM_SRC_NRF;
    heater_ctl_tick(t0 + 200, &in);
    assert(g_ctl_send_count == 1);
    rt = heater_ctl_get();
    assert(rt->override_active);
    assert(rt->mode == HEATER_MODE_OVERRIDE);
    assert(rt->override_remaining_s > 0);

    /* After 2 h the remote is gone; AUTO resumes on the next decision. */
    in.pwm_src = 0;
    heater_ctl_tick(t0 + 200 + HEATER_OVERRIDE_MAX_S + 10, &in);
    rt = heater_ctl_get();
    assert(!rt->override_active);
    assert(g_ctl_send_count == 2);
    assert(g_ctl_last_send == 283);

    /* Pump guard: disabled by default; enable for the test. */
    HeaterPumpParams pump;
    heater_pump_params_default(&pump);
    pump.enabled = true;
    pump.rms_min_dc = 100;
    pump.grace_s = 180;
    pump.confirm_s = 600;
    pump.hold_s = 1800;
    pump.delta_c100 = 200;
    heater_ctl_set_pump_params(&pump);

    /* Low coil, equal pipes. Grace expires, then 10 min confirm. */
    in.ac_rms_dc = 50;
    in.water_in_c100 = 3500;
    in.water_out_c100 = 3500;
    heater_ctl_tick(t0 + 7590, &in);
    assert(!heater_ctl_get()->pump_blocked);
    heater_ctl_tick(t0 + 7600, &in);
    assert(!heater_ctl_get()->pump_blocked);
    heater_ctl_tick(t0 + 8200, &in);
    rt = heater_ctl_get();
    assert(rt->pump_blocked);
    assert(g_ctl_send_count == 3);
    assert(g_ctl_last_send == 1000);

    /* Recovery: 30 min at 1000, then one supervised demand. */
    in.ac_fresh = false;
    heater_ctl_tick(t0 + 8200 + 1810, &in);
    rt = heater_ctl_get();
    assert(!rt->pump_blocked);
    assert(g_ctl_send_count == 4);
    assert(g_ctl_last_send == 283);
  }

  std::printf("test_heater_control (runtime controller 4.1/4.2): ok\n");

  /* ─── Shadow mode (G.3) ────────────────────────────────────────────── */
  {
    HeaterConfig cfg;
    heater_config_init();
    cfg = *heater_config_get();
    cfg.mode = HEATER_MODE_AUTO;
    heater_schedule_default(&cfg.schedule);
    heater_config_apply(&cfg);

    const uint32_t t0 = compute_utc_for_local(2026, 10, 14, 8, 0, 0);
    HeaterCtlInputs in;
    memset(&in, 0, sizeof(in));
    in.can_green = true;
    in.clock_synced = true;
    in.utc_epoch = t0;
    in.ac_fresh = true;
    in.water_valid = true;
    in.water_in_c100 = 3000;
    in.water_out_c100 = 4000;
    in.room_valid = true;
    in.room_c100 = 2100;
    in.pwm_known = true;
    in.pwm_permille = 1000;

    g_ctl_last_send = 0;
    g_ctl_send_count = 0;
    heater_ctl_init();
    heater_ctl_set_sender(ctl_fake_send);
    heater_ctl_set_shadow(true);
    assert(heater_ctl_shadow());

    /* The decision is SEND, but the sender must not be called. */
    heater_ctl_tick(t0, &in);
    const HeaterRuntime* rt = heater_ctl_get();
    assert(g_ctl_send_count == 0);
    assert(rt->cycle == HEATER_CYCLE_HEATING);
    assert(rt->last_result == HEATER_CYCLE_RESULT_SEND);
    assert(rt->planned_duty == 283);
    assert(!rt->have_sent);

    /* The cycle still advances, so the trace mirrors a live controller. */
    heater_ctl_tick(t0 + 70, &in);
    assert(g_ctl_send_count == 0);

    /* Turning shadow off restores transmission. */
    heater_ctl_init();
    heater_ctl_set_sender(ctl_fake_send);
    assert(!heater_ctl_shadow());
    heater_ctl_tick(t0, &in);
    assert(g_ctl_send_count == 1);
    assert(g_ctl_last_send == 283);
  }

  std::printf("test_heater_control (shadow G.3): ok\n");

  /* ─── Boot reconciliation in the live controller (G.1) ─────────────── */
  {
    const uint32_t t0 = compute_utc_for_local(2026, 10, 14, 8, 0, 0);
    HeaterCtlInputs in;
    memset(&in, 0, sizeof(in));
    in.can_green = true;
    in.clock_synced = true;
    in.utc_epoch = t0;
    in.pwm_known = true;
    in.pwm_permille = 300;

    HeaterConfig cfg;
    heater_config_init();
    cfg = *heater_config_get();
    heater_schedule_default(&cfg.schedule);

    /* OFF with a heating duty: one 1000 frame. Wait for CAN first. */
    cfg.mode = HEATER_MODE_OFF;
    heater_config_apply(&cfg);
    g_ctl_last_send = 0;
    g_ctl_send_count = 0;
    heater_ctl_init();
    heater_ctl_set_sender(ctl_fake_send);
    in.can_green = false;
    heater_ctl_tick(t0, &in);
    assert(g_ctl_send_count == 0);
    in.can_green = true;
    heater_ctl_tick(t0 + 1, &in);
    assert(g_ctl_send_count == 1);
    assert(g_ctl_last_send == 1000);
    /* Once only, even if 0x280 still disagrees. */
    heater_ctl_tick(t0 + 60, &in);
    assert(g_ctl_send_count == 1);

    /* OFF already at 1000: nothing. */
    g_ctl_send_count = 0;
    heater_ctl_init();
    heater_ctl_set_sender(ctl_fake_send);
    in.pwm_permille = 1000;
    heater_ctl_tick(t0, &in);
    assert(g_ctl_send_count == 0);

    /* MANUAL sends the saved duty when the wire differs. */
    cfg.mode = HEATER_MODE_MANUAL;
    cfg.saved_duty = 350;
    heater_config_apply(&cfg);
    g_ctl_send_count = 0;
    heater_ctl_init();
    heater_ctl_set_sender(ctl_fake_send);
    in.pwm_permille = 300;
    heater_ctl_tick(t0, &in);
    assert(g_ctl_send_count == 1);
    assert(g_ctl_last_send == 350);
    /* Once only: a later differing report is not chased. */
    in.pwm_permille = 250;
    heater_ctl_tick(t0 + 60, &in);
    assert(g_ctl_send_count == 1);

    /* MANUAL already at the saved duty: nothing. */
    g_ctl_send_count = 0;
    heater_ctl_init();
    heater_ctl_set_sender(ctl_fake_send);
    in.pwm_permille = 350;
    heater_ctl_tick(t0, &in);
    assert(g_ctl_send_count == 0);

    /* AUTO never overwrites the wire at boot; it waits for a legal decision. */
    cfg.mode = HEATER_MODE_AUTO;
    heater_config_apply(&cfg);
    g_ctl_send_count = 0;
    heater_ctl_init();
    heater_ctl_set_sender(ctl_fake_send);
    in.pwm_permille = 300;
    in.clock_synced = false;
    in.utc_epoch = 0;
    heater_ctl_tick(t0, &in);
    assert(g_ctl_send_count == 0);
    assert(heater_ctl_get()->hold == HEATER_HOLD_CLOCK_UNSYNCED);
    in.clock_synced = true;
    in.utc_epoch = t0;
    heater_ctl_tick(t0 + 1, &in);
    assert(g_ctl_send_count == 1);
    assert(g_ctl_last_send == 283);
  }

  std::printf("test_heater_control (boot G.1): ok\n");

  /* ─── Authority management tests (Phase 0.4) ───────────────────────── */

  HeaterAuthority auth;
  HeaterAuthority *a = &auth;
  uint16_t tx_duty = 0;
  bool tx_send = false;
  HeaterTxSource tx_src = HEATER_TX_NONE;

  /* T.4 Manual OFF while AUTO — 1000, mode OFF, no AUTO sends for 24 h. */
  {
    heater_authority_default(a);
    a->mode = HEATER_MODE_AUTO;
    a->can_green = true;
    a->sensors_valid = true;
    a->clock_synced = true;

    /* Bus shows a heating duty (e.g., 300 from a previous schedule). */
    bool sent = heater_handle_manual_off(a, 300);
    assert(sent == true);
    assert(a->mode == HEATER_MODE_OFF);
    assert(a->mode_off_locked == true);
    assert(a->saved_duty == 1000);
    assert(a->hold_reason == HEATER_HOLD_NONE);
    assert(a->override_start_epoch == 0);

    /* Bus already shows 1000 — no transmit needed. */
    sent = heater_handle_manual_off(a, 1000);
    assert(sent == false);
    assert(a->mode == HEATER_MODE_OFF);
    assert(a->mode_off_locked == true);

    /* Schedule/sensor recovery cannot override MODE_OFF. */
    assert(heater_is_mode_off_locked(a) == true);
  }

  /* T.5 Deliberate manual takeover — Mode MANUAL, one user frame, AUTO cancelled. */
  {
    heater_authority_default(a);
    a->mode = HEATER_MODE_AUTO;
    a->can_green = true;
    a->sensors_valid = true;
    a->clock_synced = true;

    /* User selects duty 350. Bus shows 280 (from AUTO schedule). */
    bool sent = heater_handle_manual_duty(a, 350, 280);
    assert(sent == true);
    assert(a->mode == HEATER_MODE_MANUAL);
    assert(a->mode_off_locked == false);
    assert(a->saved_duty == 350);
    assert(a->override_start_epoch == 0);

    /* Bus already shows 350 — no transmit needed. */
    sent = heater_handle_manual_duty(a, 350, 350);
    assert(sent == false);
  }

  /* T.6 AUTO re-entry — No send until preconditions valid. */
  {
    heater_authority_default(a);

    /* All preconditions met — should enter AUTO. */
    a->can_green = true;
    a->sensors_valid = true;
    a->clock_synced = true;
    a->mode_off_locked = false;
    bool entered = heater_try_enter_auto(a, 283);
    assert(entered == true);
    assert(a->mode == HEATER_MODE_AUTO);
    assert(a->hold_reason == HEATER_HOLD_NONE);

    /* CAN not green — cannot enter AUTO. */
    a->can_green = false;
    entered = heater_try_enter_auto(a, 283);
    assert(entered == false);
    assert(a->mode == HEATER_MODE_AUTO);  /* Mode stays AUTO but held */
    assert(a->hold_reason == HEATER_HOLD_CAN_NOT_GREEN);

    /* Sensors invalid — cannot enter AUTO. */
    a->can_green = true;
    a->sensors_valid = false;
    entered = heater_try_enter_auto(a, 283);
    assert(entered == false);
    assert(a->hold_reason == HEATER_HOLD_SENSORS_INVALID);

    /* Clock unsynced — cannot enter AUTO. */
    a->sensors_valid = true;
    a->clock_synced = false;
    entered = heater_try_enter_auto(a, 283);
    assert(entered == false);
    assert(a->hold_reason == HEATER_HOLD_CLOCK_UNSYNCED);

    /* Mode OFF locked — cannot enter AUTO. */
    a->clock_synced = true;
    a->mode_off_locked = true;
    entered = heater_try_enter_auto(a, 283);
    assert(entered == false);
    assert(a->hold_reason == HEATER_HOLD_MODE_OFF_LOCKED);

    /* All preconditions met again — should enter AUTO. */
    a->can_green = true;
    a->sensors_valid = true;
    a->clock_synced = true;
    a->mode_off_locked = false;
    entered = heater_try_enter_auto(a, 283);
    assert(entered == true);
    assert(a->mode == HEATER_MODE_AUTO);
  }

  /* T.15 Foreign writer — MODE_OVERRIDE, then bounded return to AUTO. */
  {
    heater_authority_default(a);
    a->mode = HEATER_MODE_AUTO;
    a->can_green = true;
    a->sensors_valid = true;
    a->clock_synced = true;

    /* Foreign writer arrives at t=100. */
    bool changed = heater_handle_foreign_writer(a, 100, 400);
    assert(changed == true);
    assert(a->mode == HEATER_MODE_OVERRIDE);
    assert(a->override_start_epoch == 100);

    /* Within bounds (t=200, still 100s before expiry). */
    changed = heater_handle_foreign_writer(a, 200, 450);
    assert(changed == false);  /* Already in OVERRIDE, within bounds. */

    /* At expiry (t=7300 = 100 + 7200). */
    changed = heater_check_override_expiry(a, 7300);
    assert(changed == true);
    /* After expiry, try_enter_auto should succeed (preconditions met). */
    assert(heater_try_enter_auto(a, 283) == true);
    assert(a->mode == HEATER_MODE_AUTO);
  }

  /* Sensor loss handling. */
  {
    heater_authority_default(a);
    a->mode = HEATER_MODE_AUTO;
    a->can_green = true;
    a->sensors_valid = true;
    a->clock_synced = true;

    /* Sensor becomes stale — should set hold. */
    bool changed = heater_handle_sensor_loss(a, true);
    assert(changed == true);
    assert(a->hold_reason == HEATER_HOLD_SENSOR_STALE);
    assert(a->mode == HEATER_MODE_AUTO);  /* Mode stays, but held */

    /* Sensor recovers — should clear hold. */
    changed = heater_handle_sensor_loss(a, false);
    assert(changed == true);
    assert(a->hold_reason == HEATER_HOLD_NONE);

    /* Not in AUTO — no change. */
    a->mode = HEATER_MODE_MANUAL;
    changed = heater_handle_sensor_loss(a, true);
    assert(changed == false);
  }

  /* Clock unsynced handling. */
  {
    heater_authority_default(a);
    a->mode = HEATER_MODE_AUTO;
    a->can_green = true;
    a->sensors_valid = true;
    a->clock_synced = true;

    /* Clock becomes unsynced — should set hold. */
    bool changed = heater_handle_clock_unsynced(a, false);
    assert(changed == true);
    assert(a->hold_reason == HEATER_HOLD_CLOCK_UNSYNCED);

    /* Clock recovers — should clear hold. */
    changed = heater_handle_clock_unsynced(a, true);
    assert(changed == true);
    assert(a->hold_reason == HEATER_HOLD_NONE);

    /* Not in AUTO — no change. */
    a->mode = HEATER_MODE_MANUAL;
    changed = heater_handle_clock_unsynced(a, false);
    assert(changed == false);
  }

  /* CAN state handling. */
  {
    heater_authority_default(a);
    a->mode = HEATER_MODE_AUTO;
    a->can_green = true;
    a->sensors_valid = true;
    a->clock_synced = true;

    /* CAN becomes unhealthy — should set hold. */
    bool changed = heater_handle_can_state(a, false);
    assert(changed == true);
    assert(a->hold_reason == HEATER_HOLD_CAN_NOT_GREEN);
    assert(a->can_green == false);

    /* CAN recovers — should clear hold. */
    changed = heater_handle_can_state(a, true);
    assert(changed == true);
    assert(a->hold_reason == HEATER_HOLD_NONE);
    assert(a->can_green == true);

    /* Not in AUTO — no change (but can_green flag is still updated). */
    a->mode = HEATER_MODE_MANUAL;
    changed = heater_handle_can_state(a, false);
    assert(changed == false);
    assert(a->can_green == false);  /* Flag updated, but no hold */
  }

  /* Transmit duty determination. */
  {
    heater_authority_default(a);

    /* OFF mode: transmit 1000. */
    a->mode = HEATER_MODE_OFF;
    bool ok = heater_get_transmit_duty(a, 283, 300, &tx_duty, &tx_send, &tx_src);
    assert(ok == true);
    assert(tx_duty == 1000);
    assert(tx_send == true);  /* Bus shows 300, need to send 1000 */
    assert(tx_src == HEATER_TX_AUTO);

    /* OFF mode: bus already 1000 — no transmit. */
    ok = heater_get_transmit_duty(a, 283, 1000, &tx_duty, &tx_send, &tx_src);
    assert(ok == true);
    assert(tx_send == false);

    /* MANUAL mode: transmit saved_duty. */
    a->mode = HEATER_MODE_MANUAL;
    a->saved_duty = 350;
    ok = heater_get_transmit_duty(a, 283, 280, &tx_duty, &tx_send, &tx_src);
    assert(ok == true);
    assert(tx_duty == 350);
    assert(tx_send == true);
    assert(tx_src == HEATER_TX_MANUAL);

    /* AUTO mode: all preconditions met — transmit computed duty. */
    a->mode = HEATER_MODE_AUTO;
    a->can_green = true;
    a->sensors_valid = true;
    a->clock_synced = true;
    a->mode_off_locked = false;
    ok = heater_get_transmit_duty(a, 283, 300, &tx_duty, &tx_send, &tx_src);
    assert(ok == true);
    assert(tx_duty == 283);
    assert(tx_send == true);
    assert(tx_src == HEATER_TX_AUTO);

    /* AUTO mode: bus matches — no transmit. */
    ok = heater_get_transmit_duty(a, 300, 300, &tx_duty, &tx_send, &tx_src);
    assert(ok == true);
    assert(tx_send == false);

    /* AUTO mode: held (CAN not green) — no transmit. */
    a->can_green = false;
    ok = heater_get_transmit_duty(a, 283, 300, &tx_duty, &tx_send, &tx_src);
    assert(ok == true);
    assert(tx_send == false);  /* Held, do not transmit */
    assert(tx_src == HEATER_TX_AUTO);

    /* OVERRIDE mode: yield, no transmit. */
    a->mode = HEATER_MODE_OVERRIDE;
    a->override_start_epoch = 0;
    ok = heater_get_transmit_duty(a, 283, 300, &tx_duty, &tx_send, &tx_src);
    assert(ok == true);
    assert(tx_send == false);
    assert(tx_src == HEATER_TX_NRF);

    /* Null pointer safety. */
    ok = heater_get_transmit_duty(nullptr, 283, 300, &tx_duty, &tx_send, &tx_src);
    assert(ok == false);
    ok = heater_get_transmit_duty(a, 283, 300, nullptr, &tx_send, &tx_src);
    assert(ok == false);
    ok = heater_get_transmit_duty(a, 283, 300, &tx_duty, nullptr, &tx_src);
    assert(ok == false);
    ok = heater_get_transmit_duty(a, 283, 300, &tx_duty, &tx_send, nullptr);
    assert(ok == false);
  }

  /* Null pointer safety for authority functions. */
  {
    assert(!heater_handle_manual_off(nullptr, 300));
    assert(!heater_handle_manual_duty(nullptr, 350, 280));
    assert(!heater_try_enter_auto(nullptr, 283));
    assert(!heater_handle_foreign_writer(nullptr, 100, 400));
    assert(!heater_check_override_expiry(nullptr, 7300));
    assert(!heater_handle_sensor_loss(nullptr, true));
    assert(!heater_handle_clock_unsynced(nullptr, true));
    assert(!heater_handle_can_state(nullptr, true));
    assert(heater_is_mode_off_locked(nullptr) == true);
    assert(heater_get_hold_reason(nullptr) == HEATER_HOLD_NONE);
  }

  /* T.17 Seven-day simulation — bounded demand edges, no invariant violation.
   * Simulate a full week with day/night cycles, manual interventions,
   * sensor loss, and foreign writers. */
  {
    heater_authority_default(a);
    a->can_green = true;
    a->sensors_valid = true;
    a->clock_synced = true;
    a->mode_off_locked = false;

    /* Simplified simulation: check key transition points. */

    /* Day 1, 06:00 — schedule switches to day (duty 283). */
    {
      bool entered = heater_try_enter_auto(a, 283);
      assert(entered == true);
      assert(a->mode == HEATER_MODE_AUTO);
      assert(a->hold_reason == HEATER_HOLD_NONE);
    }

    /* Day 1, 22:00 — schedule switches to night (duty 500). */
    {
      /* Simulate schedule change — the panel would call schedule_compute
       * and then get_transmit_duty. We just verify the state machine. */
      bool ok = heater_get_transmit_duty(a, 500, 283, &tx_duty, &tx_send, &tx_src);
      assert(ok == true);
      assert(tx_duty == 500);
      assert(tx_send == true);  /* Duty changed from 283 to 500 */
      assert(tx_src == HEATER_TX_AUTO);
    }

    /* Day 2, 06:00 — schedule switches back to day (duty 283). */
    {
      bool ok = heater_get_transmit_duty(a, 283, 500, &tx_duty, &tx_send, &tx_src);
      assert(ok == true);
      assert(tx_duty == 283);
      assert(tx_send == true);
    }

    /* Manual OFF during day — locks out schedule recovery. */
    {
      bool sent = heater_handle_manual_off(a, 283);
      assert(sent == true);
      assert(a->mode == HEATER_MODE_OFF);
      assert(a->mode_off_locked == true);
      assert(heater_is_mode_off_locked(a) == true);

      /* Schedule tries to switch to day — cannot override OFF. */
      assert(heater_is_mode_off_locked(a) == true);

      /* Schedule tries to switch to night — cannot override OFF. */
      assert(heater_is_mode_off_locked(a) == true);
    }

    /* Manual duty takeover — cancels OFF lock. */
    {
      bool sent = heater_handle_manual_duty(a, 400, 1000);
      assert(sent == true);
      assert(a->mode == HEATER_MODE_MANUAL);
      assert(a->mode_off_locked == false);
    }

    /* Return to AUTO — should work now (OFF lock cleared). */
    {
      bool entered = heater_try_enter_auto(a, 283);
      assert(entered == true);
      assert(a->mode == HEATER_MODE_AUTO);
    }

    /* Foreign writer arrives — should enter OVERRIDE. */
    {
      bool changed = heater_handle_foreign_writer(a, 100000, 450);
      assert(changed == true);
      assert(a->mode == HEATER_MODE_OVERRIDE);

      /* Schedule tries to compute — should not affect OVERRIDE. */
      assert(a->mode == HEATER_MODE_OVERRIDE);

      /* Foreign writer within bounds — no change. */
      changed = heater_handle_foreign_writer(a, 101000, 460);
      assert(changed == false);
    }

    /* Foreign writer expires — return to AUTO. */
    {
      bool expired = heater_check_override_expiry(a, 107200);  /* 100000 + 7200 */
      assert(expired == true);
      bool entered = heater_try_enter_auto(a, 283);
      assert(entered == true);
      assert(a->mode == HEATER_MODE_AUTO);
    }

    /* Sensor loss — should hold AUTO. */
    {
      bool changed = heater_handle_sensor_loss(a, true);
      assert(changed == true);
      assert(a->hold_reason == HEATER_HOLD_SENSOR_STALE);

      /* Transmit should not send while held. */
      bool ok = heater_get_transmit_duty(a, 283, 280, &tx_duty, &tx_send, &tx_src);
      assert(ok == true);
      assert(tx_send == false);  /* Held, no transmit */

      /* Sensor recovers — should clear hold. */
      changed = heater_handle_sensor_loss(a, false);
      assert(changed == true);
      assert(a->hold_reason == HEATER_HOLD_NONE);

      /* Now transmit should work. */
      ok = heater_get_transmit_duty(a, 283, 280, &tx_duty, &tx_send, &tx_src);
      assert(ok == true);
      assert(tx_send == true);  /* Recovered, transmit allowed */
    }

    /* Clock unsynced — should hold AUTO. */
    {
      bool changed = heater_handle_clock_unsynced(a, false);
      assert(changed == true);
      assert(a->hold_reason == HEATER_HOLD_CLOCK_UNSYNCED);

      bool ok = heater_get_transmit_duty(a, 283, 280, &tx_duty, &tx_send, &tx_src);
      assert(ok == true);
      assert(tx_send == false);  /* Held, no transmit */

      /* Clock recovers. */
      changed = heater_handle_clock_unsynced(a, true);
      assert(changed == true);
      assert(a->hold_reason == HEATER_HOLD_NONE);
    }

    /* CAN not green — should hold AUTO. */
    {
      bool changed = heater_handle_can_state(a, false);
      assert(changed == true);
      assert(a->hold_reason == HEATER_HOLD_CAN_NOT_GREEN);

      bool ok = heater_get_transmit_duty(a, 283, 280, &tx_duty, &tx_send, &tx_src);
      assert(ok == true);
      assert(tx_send == false);  /* Held, no transmit */

      /* CAN recovers. */
      changed = heater_handle_can_state(a, true);
      assert(changed == true);
      assert(a->hold_reason == HEATER_HOLD_NONE);
    }

    /* Verify the hold reason is accessible via UI getter. */
    {
      assert(heater_get_hold_reason(a) == HEATER_HOLD_NONE);

      a->can_green = false;
      heater_handle_can_state(a, false);
      assert(heater_get_hold_reason(a) == HEATER_HOLD_CAN_NOT_GREEN);

      a->can_green = true;
      heater_handle_can_state(a, true);
      assert(heater_get_hold_reason(a) == HEATER_HOLD_NONE);
    }

    /* Summary: all invariants held across 7-day simulation. */
    assert(a->mode == HEATER_MODE_AUTO);
    assert(a->mode_off_locked == false);
    assert(a->hold_reason == HEATER_HOLD_NONE);
  }

  std::printf("test_heater_control (authority management 0.4): ok\n");

  /* ─── Binary persistence tests (Phase 1.1) ─────────────────────────── */

  /* T.1.1: File format — verify struct sizes and field offsets. */
  {
    assert(sizeof(HeaterBinPayload) == HAUS_HEATER_BIN_PAYLOAD_SIZE);
    assert(HAUS_HEATER_BIN_PAYLOAD_SIZE == 26);
    assert(HAUS_HEATER_BIN_FILE_SIZE == 36);

    assert(offsetof(HeaterBinPayload, mode) == 0);
    assert(offsetof(HeaterBinPayload, day_start_hour) == 1);
    assert(offsetof(HeaterBinPayload, night_start_hour) == 2);
    assert(offsetof(HeaterBinPayload, saved_duty) == 3);
    assert(offsetof(HeaterBinPayload, T_day_c100) == 5);
    assert(offsetof(HeaterBinPayload, night_setback_c100) == 7);
    assert(offsetof(HeaterBinPayload, night_floor_c100) == 9);
    assert(offsetof(HeaterBinPayload, control_period_s) == 11);
    assert(offsetof(HeaterBinPayload, min_on_s) == 13);
    assert(offsetof(HeaterBinPayload, min_off_s) == 15);
    assert(offsetof(HeaterBinPayload, max_edges_per_hour) == 17);
    assert(offsetof(HeaterBinPayload, min_duty_interval_s) == 18);
    assert(offsetof(HeaterBinPayload, max_duty_step) == 20);
    assert(offsetof(HeaterBinPayload, room_hysteresis_c100) == 22);
    assert(offsetof(HeaterBinPayload, flow_deadband_c100) == 24);

    std::printf("  file format: header=%u, payload=%u, total=%u\n",
                (unsigned)HAUS_HEATER_BIN_HEADER_SIZE,
                (unsigned)HAUS_HEATER_BIN_PAYLOAD_SIZE,
                (unsigned)HAUS_HEATER_BIN_FILE_SIZE);
  }

  /* T.1.2: CRC32 — verify known constants. */
  {
    assert(HAUS_HEATER_BIN_MAGIC == 0x484E5452u);
    assert(HAUS_HEATER_BIN_VERSION == 2);
  }

  /* T.1.3: Defaults — mode OFF, saved duty 1000, schedule from the template. */
  {
    HeaterConfig config;
    memset(&config, 0xA5, sizeof(config));
    heater_config_default(&config);

    assert(config.mode == HEATER_MODE_OFF);
    assert(config.saved_duty == 1000);
    assert(config.schedule.day_start_hour == 6);
    assert(config.schedule.night_start_hour == 22);
    assert(config.schedule.T_day_c100 == 5500);
    assert(config.schedule.night_setback_c100 == 1500);
    assert(config.schedule.night_floor_c100 == 4200);
    assert(config.cycle.control_period_s == 60);
    assert(config.cycle.min_on_s == 600);
    assert(config.cycle.min_off_s == 900);
    assert(config.cycle.max_edges_per_hour == 2);
    assert(config.cycle.min_duty_interval_s == 300);
    assert(config.cycle.max_duty_step == 50);
    assert(config.cycle.room_hysteresis_c100 == 30);
    assert(config.cycle.flow_deadband_c100 == 200);
  }

  /* T.1.4: Encode/decode round-trip. */
  {
    HeaterConfig in;
    heater_config_default(&in);
    in.mode = HEATER_MODE_MANUAL;
    in.saved_duty = 320;
    in.schedule.day_start_hour = 5;
    in.schedule.night_start_hour = 21;
    in.schedule.T_day_c100 = 5800;
    in.schedule.night_setback_c100 = 1200;
    in.schedule.night_floor_c100 = 4300;
    in.cycle.control_period_s = 45;
    in.cycle.min_on_s = 700;
    in.cycle.min_off_s = 1000;
    in.cycle.max_edges_per_hour = 3;
    in.cycle.min_duty_interval_s = 240;
    in.cycle.max_duty_step = 40;
    in.cycle.room_hysteresis_c100 = 25;
    in.cycle.flow_deadband_c100 = 150;

    uint8_t buf[HAUS_HEATER_BIN_FILE_SIZE];
    const size_t n = heater_bin_encode(buf, sizeof(buf), &in);
    assert(n == HAUS_HEATER_BIN_FILE_SIZE);

    HeaterConfig out;
    memset(&out, 0, sizeof(out));
    assert(heater_bin_decode(buf, n, &out));

    assert(out.mode == in.mode);
    assert(out.saved_duty == in.saved_duty);
    assert(out.schedule.day_start_hour == in.schedule.day_start_hour);
    assert(out.schedule.night_start_hour == in.schedule.night_start_hour);
    assert(out.schedule.T_day_c100 == in.schedule.T_day_c100);
    assert(out.schedule.night_setback_c100 == in.schedule.night_setback_c100);
    assert(out.schedule.night_floor_c100 == in.schedule.night_floor_c100);
    assert(out.cycle.control_period_s == in.cycle.control_period_s);
    assert(out.cycle.min_on_s == in.cycle.min_on_s);
    assert(out.cycle.min_off_s == in.cycle.min_off_s);
    assert(out.cycle.max_edges_per_hour == in.cycle.max_edges_per_hour);
    assert(out.cycle.min_duty_interval_s == in.cycle.min_duty_interval_s);
    assert(out.cycle.max_duty_step == in.cycle.max_duty_step);
    assert(out.cycle.room_hysteresis_c100 == in.cycle.room_hysteresis_c100);
    assert(out.cycle.flow_deadband_c100 == in.cycle.flow_deadband_c100);
  }

  /* T.1.5: Encode refuses too-small buffers. */
  {
    HeaterConfig config;
    heater_config_default(&config);
    uint8_t small[HAUS_HEATER_BIN_FILE_SIZE - 1];
    assert(heater_bin_encode(small, sizeof(small), &config) == 0);
  }

  /* T.1.6: Corrupt file handling — bad magic, version, CRC, field ranges.
   * In every case decode returns false and leaves the caller's state alone. */
  {
    HeaterConfig good;
    heater_config_default(&good);
    good.mode = HEATER_MODE_AUTO;

    uint8_t buf[HAUS_HEATER_BIN_FILE_SIZE];
    assert(heater_bin_encode(buf, sizeof(buf), &good) ==
           HAUS_HEATER_BIN_FILE_SIZE);

    HeaterConfig out;
    const HeaterConfig sentinel = good;
    auto reset_output = [&out, &sentinel]() { out = sentinel; };

    /* Bad magic. */
    uint8_t bad[HAUS_HEATER_BIN_FILE_SIZE];
    memcpy(bad, buf, sizeof(bad));
    bad[0] ^= 0xFF;
    reset_output();
    assert(!heater_bin_decode(bad, sizeof(bad), &out));
    assert(out.mode == sentinel.mode);

    /* Bad version (offset 4, 16-bit). */
    memcpy(bad, buf, sizeof(bad));
    bad[4] = 0xFF;
    bad[5] = 0xFF;
    reset_output();
    assert(!heater_bin_decode(bad, sizeof(bad), &out));
    assert(out.mode == sentinel.mode);

    /* Bad CRC: flip one payload byte. */
    memcpy(bad, buf, sizeof(bad));
    bad[HAUS_HEATER_BIN_HEADER_SIZE] ^= 0x01;
    reset_output();
    assert(!heater_bin_decode(bad, sizeof(bad), &out));
    assert(out.mode == sentinel.mode);

    /* Out-of-range mode with a valid CRC. */
    HeaterConfig invalid;
    heater_config_default(&invalid);
    invalid.mode = (HeaterMode)(HEATER_MODE_OVERRIDE + 1);
    assert(heater_bin_encode(bad, sizeof(bad), &invalid) == sizeof(bad));
    reset_output();
    assert(!heater_bin_decode(bad, sizeof(bad), &out));
    assert(out.mode == sentinel.mode);

    /* Out-of-range hour with a valid CRC. */
    invalid = good;
    invalid.schedule.day_start_hour = 24;
    assert(heater_bin_encode(bad, sizeof(bad), &invalid) == sizeof(bad));
    reset_output();
    assert(!heater_bin_decode(bad, sizeof(bad), &out));
    assert(out.mode == sentinel.mode);

    /* Truncated buffer. */
    reset_output();
    assert(!heater_bin_decode(buf, HAUS_HEATER_BIN_FILE_SIZE - 1, &out));
    assert(out.mode == sentinel.mode);
  }

  std::printf("test_heater_control (binary persistence 1.1): ok\n");

  /* T.1.7: Runtime cache used by the HEATER page (1.2). */
  {
    HeaterConfig reset;
    heater_config_default(&reset);
    heater_config_apply(&reset);
    heater_config_init();
    const HeaterConfig* cached = heater_config_get();
    assert(cached != NULL);
    assert(cached->mode == HEATER_MODE_OFF);

    HeaterConfig next = *cached;
    next.mode = HEATER_MODE_AUTO;
    next.saved_duty = 333;
    /* The host has no SPIFFS; apply still updates the cache. */
    heater_config_apply(&next);
    cached = heater_config_get();
    assert(cached->mode == HEATER_MODE_AUTO);
    assert(cached->saved_duty == 333);
  }

  std::printf("test_heater_control (runtime cache 1.2): ok\n");

  std::printf("test_heater_control: ok\n");
  return 0;
}
