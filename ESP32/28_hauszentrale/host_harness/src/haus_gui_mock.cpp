/**
 * @file haus_gui_mock.cpp
 * @brief Host harness implementation of the GUI data interface.
 *
 * Generates synthetic but realistic heater data so the LVGL UI can be
 * designed and verified without hardware. Selected by compiling with
 * -DHAUS_HOST in the host CMakeLists.txt.
 *
 * On the device, haus_gui.cpp is compiled instead (no HAUS_HOST define).
 */

#define _POSIX_C_SOURCE 200809L

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "dump_data.h"
#include "haus_can.h"
#include "haus_config.h"
#include "haus_gui.h"
#include "haus_history.h"
#include "haus_rollup.h"
#include "haus_triple_buffer.h"

/* ─── Synthetic heater model ────────────────────────────────────────────────
 *
 *  Simulates a real wall-hung boiler over 24 hours:
 *
 *    00:00–06:00  Night setback (low temp, low duty)
 *    06:00–08:00  Morning ramp (chamber heats up, duty peaks)
 *    08:00–17:00  Day plateau (maintain target, moderate duty)
 *    17:00–21:00  Evening ramp (second heating peak)
 *    21:00–00:00  Night setback
 *
 *  Water temperatures follow the chamber with a delay and offset.
 *  SML power tracks the PWM duty with some noise.
 */

/* ─── Time helpers ──────────────────────────────────────────────────────────
 */

/* Simulated "current time" — fixed so every rendered screen sees the same
 * clock. It used to advance 1 s per gui_update_state() call, but the history
 * is seeded once, so later screens (LIVING ROOM) showed a bogus "data age
 * 3 min ago" that was an artefact of the harness, not the device.
 * Start at 08:00 on Oct 14, 2024 (epoch = 1728892800). */
static uint32_t g_sim_time = 1728892800u; /* 2024-10-14 08:00:00 UTC */

/* Home-screen variants: -1 keeps the synthetic model. */
static int g_target_override = -1;
static int g_pwm_override = -1;
static bool g_pwm_fresh_override = true;

extern "C" void mock_set_home_case(int target_c100, int pwm_permille,
                                   bool fresh) {
  g_target_override = target_c100;
  g_pwm_override = pwm_permille;
  g_pwm_fresh_override = fresh;
}
static const uint32_t DAY_SECONDS = 86400u;

/* Get time-of-day in seconds (0–86399). */
static uint32_t tod(uint32_t epoch) { return epoch % DAY_SECONDS; }

/* Get day-of-year (0–365) from epoch. */
static uint32_t doy(uint32_t epoch) { return (epoch / DAY_SECONDS) % 365; }

/* ─── Chamber temperature model ─────────────────────────────────────────────
 *
 *  Returns c° (× 100) for the chamber at a given epoch.
 *  Follows a sinusoidal pattern with morning/evening peaks.
 */
/* The setpoint is a real series: it is 15 °C at night, ramps to 60 °C in the
 * morning, holds during the day, and drops in the evening. It is the third
 * line on the water chart and will later be driven by the control loop. */
static int16_t mock_target_temp(uint32_t epoch) {
  uint32_t t = tod(epoch);
  double target = 0.0;

  if (t < 21600) {
    target = 15.0;
  } else if (t < 28800) {
    double progress = (double)(t - 21600) / 7200.0;
    target = 15.0 + 45.0 * (1.0 / (1.0 + exp(-8.0 * (progress - 0.5))));
  } else if (t < 61200) {
    target = 60.0;
  } else if (t < 75600) {
    double progress = (double)(t - 61200) / 14400.0;
    target = 60.0 - 45.0 * (1.0 / (1.0 + exp(-6.0 * (progress - 0.5))));
  } else {
    target = 15.0;
  }

  if (target < -40.0) target = -40.0;
  if (target > 90.0) target = 90.0;
  return (int16_t)(target * 100.0);
}

static int16_t mock_chamber_temp(uint32_t epoch) {
  double target = mock_target_temp(epoch) / 100.0;

  /* Add small noise (±0.2°C) and slow drift. */
  double noise = sin(epoch * 0.001) * 0.2 + sin(epoch * 0.0037) * 0.1;
  target += noise;

  if (target < -40.0) target = -40.0;
  if (target > 90.0) target = 90.0;
  return (int16_t)(target * 100.0);
}

/* ─── Water temperatures ────────────────────────────────────────────────────
 *
 *  Water-in follows the chamber with ~5°C offset (cooler, returns from
 * radiators). Water-out follows the chamber with ~5°C offset (hotter, leaves
 * boiler).
 */
static int16_t mock_water_in_temp(uint32_t epoch) {
  int16_t chamber = mock_chamber_temp(epoch);
  if (chamber == GUI_EMPTY) return GUI_EMPTY;
  /* Water in is typically 5–8°C cooler than chamber output. */
  double chamber_c = chamber / 100.0;
  double water_in = chamber_c - 5.0 + sin(epoch * 0.01) * 0.5;
  return (int16_t)(water_in * 100.0);
}

static int16_t mock_water_out_temp(uint32_t epoch) {
  int16_t chamber = mock_chamber_temp(epoch);
  if (chamber == GUI_EMPTY) return GUI_EMPTY;
  /* Water out is typically 3–5°C hotter than chamber target. */
  double chamber_c = chamber / 100.0;
  double water_out = chamber_c + 3.0 + sin(epoch * 0.013) * 0.3;
  return (int16_t)(water_out * 100.0);
}

/* ─── PWM duty cycle ────────────────────────────────────────────────────────
 *
 *  The boiler modulates PWM duty to maintain chamber temperature.
 *  High duty when far from target, low duty when near target.
 */
static uint16_t mock_pwm_duty(uint32_t epoch) {
  int16_t chamber = mock_chamber_temp(epoch);
  int16_t target = mock_target_temp(epoch);

  if (chamber == GUI_EMPTY) return 0;

  double chamber_c = chamber / 100.0;
  double target_c = target / 100.0;
  double diff = target_c - chamber_c;

  /* Duty is proportional to how far below target we are.
   * Max duty 475 permille when far below target.
   * Drops to 25 permille when near target. */
  double duty = 25.0 + 450.0 * (1.0 / (1.0 + exp(-0.3 * diff)));

  /* Add small noise. */
  duty += sin(epoch * 0.05) * 10.0;

  if (duty < 0) duty = 0;
  if (duty > 1000) duty = 1000;

  return (uint16_t)duty;
}

/* ─── Household power (synthetic SML on host) ───────────────────────────── */

static uint32_t mock_power_w(uint32_t epoch) {
  const uint32_t t = tod(epoch);
  double base = 620.0;
  if (t > 21600 && t < 28800)
    base = 900.0 + 700.0 * (double)(t - 21600) / 7200.0;
  else if (t >= 28800 && t < 61200)
    base = 700.0;
  else if (t >= 61200 && t < 75600)
    base = 1200.0 + 600.0 * (double)(t - 61200) / 14400.0;
  else
    base = 450.0;
  base += sin(epoch * 0.011) * 60.0 + sin(epoch * 0.037) * 25.0;
  if (base < 0) base = 0;
  return (uint32_t)base;
}

/* ─── Living room (synthetic on host; no sensors on the real board) ─────────
 *
 *  The physical panel has no usable local sensors (whitepaper Q24), so the
 *  device renders the living-room zone as "—". For UI design and screenshot
 *  verification the host generates plausible values instead of a blank zone.
 */

static int16_t mock_living_temp(uint32_t epoch) {
  double t = 21.8 + sin(epoch * 0.0004) * 0.8;
  return (int16_t)(t * 100.0);
}

static uint16_t mock_living_humidity(uint32_t epoch) {
  double rh = 47.0 + sin(epoch * 0.0007) * 4.0;
  return (uint16_t)(rh * 100.0);
}

static uint32_t mock_living_pressure(uint32_t epoch) {
  double p = 1013.0 + sin(epoch * 0.0002) * 6.0;
  return (uint32_t)(p * 100.0);
}

/* ─── Historical data generation ────────────────────────────────────────────
 *
 *  Pre-compute 30 days of minute-resolution samples for chart queries.
 *  Stored in a flat array: 30 days × 1440 minutes = 43200 samples.
 *  Each sample is 24 bytes, so ~1 MB total.
 */

#define MAX_HISTORY_DAYS 30
#define SAMPLES_PER_DAY 1440
#define MAX_HISTORY_SAMPLES (MAX_HISTORY_DAYS * SAMPLES_PER_DAY)

static gui_sample_t g_history[MAX_HISTORY_SAMPLES];
static bool g_history_initialized = false;

static void init_history(void) {
  if (g_history_initialized) return;

  /* Generate 30 days of synthetic data starting from 30 days ago. */
  uint32_t now = g_sim_time - (g_sim_time % 60u);
  /* Exactly MAX_HISTORY_SAMPLES minute records, ending at `now`. */
  uint32_t start_epoch =
      now - ((uint32_t)(MAX_HISTORY_SAMPLES - 1) * 60u);

  int idx = 0;
  for (uint32_t t = start_epoch; t <= now; t += 60) {
    if (idx >= MAX_HISTORY_SAMPLES) break;

    g_history[idx].epoch = t;
    g_history[idx].temp[0] = mock_chamber_temp(t);
    g_history[idx].temp[1] = mock_water_in_temp(t);
    g_history[idx].temp[2] = mock_water_out_temp(t);
    g_history[idx].living_c100 = mock_living_temp(t);
    g_history[idx].hum_c100 = mock_living_humidity(t);
    g_history[idx].pressure_pa = mock_living_pressure(t);
    g_history[idx].power_dw = mock_power_w(t) * 10u;
    g_history[idx].target_c100 = mock_target_temp(t);
    g_history[idx].ac_rms_dc =
        (uint16_t)(1368 + (int)(sin(t * 0.001) * 20.0));
    g_history[idx].ac_angle_cdeg =
        (int16_t)(5500 + (int)(sin(t * 0.002) * 120.0));
    /* No SML/ADC power source exists yet. Do not fabricate watts: the power
     * screen must exercise the same empty state as the device. */
    g_history[idx].flags = SAMPLE_FLAG_CLIMATE_AHT20 |
                           SAMPLE_FLAG_CLIMATE_BMP280;
    g_history[idx].stale_mask = 0;

    /* Simulate occasional gaps (card removal, SD errors). */
    if (tod(t) > 50000 && tod(t) < 51000) {
      g_history[idx].flags |= SAMPLE_FLAG_GAP;
    }

    idx++;
  }

  g_history_initialized = true;
}

/* ─── Synthetic log ring (S15) ───────────────────────────────────────────── */

#define MOCK_LOG_BYTES 65536
static char g_mock_log[MOCK_LOG_BYTES];
static bool g_mock_log_ready = false;
static std::vector<char> g_dump_log;
static const char* g_log_ptr = g_mock_log;
static size_t g_log_len = 0;

static void init_mock_log(void) {
  if (g_mock_log_ready) return;

  int dump_len = 0;
  const char* dumped = dump_log(&dump_len);
  if (dumped && dump_len > 0) {
    g_dump_log.assign(dumped, dumped + dump_len);
    g_dump_log.push_back('\0');
    g_log_ptr = g_dump_log.data();
    g_log_len = (size_t)dump_len;
    g_mock_log_ready = true;
    return;
  }

  static const char* levels = "EWID";
  static const char* tags[] = {"esp_lcd", "twai", "haus",   "sd",
                               "boot",    "wifi", "history"};
  size_t off = 0;
  for (int i = 0; i < 320 && off < MOCK_LOG_BYTES - 128; i++) {
    const char lv = levels[i % 4];
    const char* tag = tags[i % 7];
    int n = snprintf(g_mock_log + off, MOCK_LOG_BYTES - off,
                     "%d.%03d %c %s: sample record %d value %.2f degC\n",
                     1234 + i / 10, (i * 37) % 1000, lv, tag, i,
                     52.0 + (i % 17) * 0.35);
    if (n > 0) off += (size_t)n;
  }
  g_log_ptr = g_mock_log;
  g_log_len = strlen(g_mock_log);
  g_mock_log_ready = true;
}

const char* gui_log_dump(int* out_len) {
  init_mock_log();
  if (out_len) *out_len = (int)g_log_len;
  return g_log_ptr;
}

int gui_log_length(void) {
  init_mock_log();
  return (int)g_log_len;
}

void gui_log_clear(void) {
  g_dump_log.clear();
  g_mock_log[0] = '\0';
  g_log_ptr = g_mock_log;
  g_log_len = 0;
  g_mock_log_ready = true;
}

int gui_log_save(void) { return 1; }

uint32_t gui_log_dropped(void) { return dump_loaded() ? 0 : 21418; }

/* ─── Sensor info ───────────────────────────────────────────────────────────
 */

static gui_sensor_info_t g_sensors[GUI_MAX_SENSORS] = {
    {.rank = 0,
     .rom = {0x28, 0xFF, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC},
     .function_label = "chamber",
     .bound = true},
    {.rank = 1,
     .rom = {0x28, 0xFF, 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56},
     .function_label = "water-in",
     .bound = true},
    {.rank = 2,
     .rom = {0x28, 0xFF, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC},
     .function_label = "water-out",
     .bound = true},
    {.rank = 3,
     .rom = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
     .function_label = "",
     .bound = false},
};

/* ─── State snapshot ────────────────────────────────────────────────────────
 */

static TripleBuffer<CanState> g_can;
static bool g_can_enabled = true;
static TripleBuffer<ClimateState> g_climate;

static int16_t rollup_mean(const RollupColumn* col, int series) {
  if (!col || series < 0 || series >= HAUS_HISTORY_SERIES ||
      col->n[series] == 0)
    return GUI_EMPTY;
  return (int16_t)(col->sum[series] / col->n[series]);
}

/* Apply a real dump to the producer blocks. Minute samples are preferred for
 * "now"; when only a newer hourly column exists, use its means. */
static void apply_dump_state(CanState& can, ClimateState& climate) {
  gui_sample_t sample;
  RollupColumn rollup;
  const bool has_sample = dump_latest_sample(&sample);
  const bool has_rollup = dump_latest_rollup(&rollup);
  if (!has_sample && !has_rollup) return;

  const uint32_t sample_epoch = has_sample ? sample.epoch : 0;
  const uint32_t rollup_epoch = has_rollup ? rollup.t * 60u : 0;
  const bool use_rollup = has_rollup && rollup_epoch > sample_epoch;
  const uint32_t now = use_rollup ? rollup_epoch : sample_epoch;
  if (now == 0) return;

  int16_t temp0 = HAUS_CAN_TEMP_EMPTY;
  int16_t temp1 = HAUS_CAN_TEMP_EMPTY;
  int16_t temp2 = HAUS_CAN_TEMP_EMPTY;
  int16_t target = 0;
  int16_t living = HAUS_CAN_TEMP_EMPTY;
  uint16_t hum = 0;
  uint16_t power_dw = 0;
  uint16_t ac_rms = 0;
  int16_t ac_angle = 0;

  if (use_rollup) {
    temp0 = rollup_mean(&rollup, 0);
    temp1 = rollup_mean(&rollup, 1);
    temp2 = rollup_mean(&rollup, 2);
    living = rollup_mean(&rollup, 3);
    const int16_t h = rollup_mean(&rollup, 4);
    hum = h == GUI_EMPTY ? 0 : (uint16_t)h;
    power_dw = rollup.n[6] ? (uint16_t)(rollup.sum[6] / rollup.n[6]) : 0;
    ac_rms = rollup.n[7] ? (uint16_t)(rollup.sum[7] / rollup.n[7]) : 0;
    ac_angle = rollup.n[8] ? (int16_t)(rollup.sum[8] / rollup.n[8]) : 0;
    target = rollup.target_c100;
  } else {
    temp0 = sample.temp[0];
    temp1 = sample.temp[1];
    temp2 = sample.temp[2];
    target = sample.target_c100;
    const bool climate_valid =
        (sample.flags & (SAMPLE_FLAG_CLIMATE_AHT20 | SAMPLE_FLAG_CLIMATE_BMP280)) !=
        0;
    if (climate_valid) {
      living = sample.living_c100;
      hum = sample.hum_c100;
    }
    power_dw = sample.power_dw;
    ac_rms = sample.ac_rms_dc;
    ac_angle = sample.ac_angle_cdeg;
  }

  can.timestamp = now;
  can.synced = true;
  can.last_update_epoch = now;
  can.temp_c100[0] = temp0;
  can.temp_c100[1] = temp1;
  can.temp_c100[2] = temp2;
  can.temp_c100[3] = HAUS_CAN_TEMP_EMPTY;
  can.fn_of_slot[0] = temp0 == HAUS_CAN_TEMP_EMPTY ? SENSOR_FN_NONE
                                                  : SENSOR_FN_CHAMBER;
  can.fn_of_slot[1] = temp1 == HAUS_CAN_TEMP_EMPTY ? SENSOR_FN_NONE
                                                  : SENSOR_FN_WATER_IN;
  can.fn_of_slot[2] = temp2 == HAUS_CAN_TEMP_EMPTY ? SENSOR_FN_NONE
                                                  : SENSOR_FN_WATER_OUT;
  can.fn_of_slot[3] = SENSOR_FN_NONE;
  for (int i = 0; i < SENSOR_FN_COUNT; i++)
    can.temp_by_fn[i] = HAUS_CAN_TEMP_EMPTY;
  can.temp_by_fn[SENSOR_FN_CHAMBER] = temp0;
  can.temp_by_fn[SENSOR_FN_BOARD] = temp0;
  can.temp_by_fn[SENSOR_FN_WATER_IN] = temp1;
  can.temp_by_fn[SENSOR_FN_WATER_OUT] = temp2;
  can.chamber_target_c = target;
  can.watts = (int32_t)power_dw / 10;
  can.watts_last = can.watts;
  can.power_dw = power_dw;
  can.valid = power_dw != 0;
  can.power_src = can.valid ? GUI_PWR_SRC_SML : GUI_PWR_SRC_NONE;
  can.ac_rms_dc = ac_rms;
  can.ac_angle_cdeg = ac_angle;
  can.ac_fresh = ac_rms != 0 || ac_angle != 0;

  climate.living_c100 = living;
  climate.hum_c100 = hum;
  climate.pressure_pa = 0;
  climate.climate_flags = living == HAUS_CAN_TEMP_EMPTY ? 0 : CLIMATE_VALID_SHT45;
  climate.last_update_epoch = now;

  const uint32_t oldest = dump_oldest_epoch();
  can.uptime_seconds = oldest != 0 && now > oldest ? now - oldest : 0;

  const uint64_t sd_used = dump_sd_bytes();
  const uint64_t spiffs_used = dump_spiffs_bytes();
  if (sd_used) can.sd_used_bytes = sd_used;
  if (spiffs_used) can.spiffs_used_bytes = (uint32_t)spiffs_used;

  /* Screenshot variants override the Soll/duty after the real dump. */
  if (g_target_override >= 0) can.chamber_target_c = (int16_t)g_target_override;
  if (g_pwm_override >= 0) {
    can.pwm_permille = (uint16_t)g_pwm_override;
    can.pwm_fresh = g_pwm_fresh_override;
    can.pwm_src = can.pwm_fresh ? PWM_SRC_CAN : PWM_SRC_EEPROM;
    can.active = can.pwm_fresh && can.pwm_permille < 1000;
  }
}

void gui_update_state(void) {
  CanState& can = g_can.write();
  ClimateState& climate = g_climate.write();

  /* Keep the simulated clock fixed; see the comment at g_sim_time. */

  /* Populate the CAN producer block from mock functions. */
  can.timestamp = g_sim_time;
  can.synced = true;
  can.last_update_epoch = g_sim_time;

  /* Temperatures. */
  can.temp_c100[0] = mock_chamber_temp(g_sim_time);
  can.temp_c100[1] = mock_water_in_temp(g_sim_time);
  can.temp_c100[2] = mock_water_out_temp(g_sim_time);
  can.temp_c100[3] = HAUS_CAN_TEMP_EMPTY;
  can.fn_of_slot[0] = SENSOR_FN_CHAMBER;
  can.fn_of_slot[1] = SENSOR_FN_WATER_IN;
  can.fn_of_slot[2] = SENSOR_FN_WATER_OUT;
  can.fn_of_slot[3] = SENSOR_FN_NONE;

  for (int i = 0; i < SENSOR_FN_COUNT; i++) {
    can.temp_by_fn[i] = HAUS_CAN_TEMP_EMPTY;
  }
  can.temp_by_fn[SENSOR_FN_CHAMBER] = can.temp_c100[0];
  can.temp_by_fn[SENSOR_FN_WATER_IN] = can.temp_c100[1];
  can.temp_by_fn[SENSOR_FN_WATER_OUT] = can.temp_c100[2];

  /* PWM. */
  uint16_t duty = (g_pwm_override >= 0) ? (uint16_t)g_pwm_override
                                        : mock_pwm_duty(g_sim_time);
  can.pwm_permille = duty;
  can.frequency_hz = 20000; /* 20 kHz */
  can.pwm_fresh = g_pwm_fresh_override;
  can.pwm_src = can.pwm_fresh ? PWM_SRC_CAN : PWM_SRC_EEPROM;
  can.active = can.pwm_fresh && (duty < 1000);
  can.pwm_pending = 0xFFFF;
  can.requested_permille = 0xFFFF;
  can.has_pending = false;
  can.pwm_pending_since_ms = 0;
  can.ac_rms_dc = 1368;
  can.ac_angle_cdeg = 5500;
  can.ac_fresh = true;

  /* Synthetic SML power for the host design screens. */
  can.watts = mock_power_w(g_sim_time);
  can.watts_last = can.watts;
  can.valid = true;
  can.power_src = GUI_PWR_SRC_SML;
  can.power_dw = can.watts * 10u;
  /* Prefer the logged meter registers; older dumps have no energy.csv. */
  double consumed = 0.0;
  double produced = 0.0;
  if (dump_energy_latest(&consumed, &produced)) {
    can.energy_consumption_kwh = consumed;
    can.energy_production_kwh = produced;
    can.energy_consumption_valid = true;
    can.energy_production_valid = true;
  } else {
    can.energy_consumption_kwh = 12345.678;
    can.energy_production_kwh = 2345.678;
    can.energy_consumption_valid = true;
    can.energy_production_valid = true;
  }
  can.sml_voltage_v[0] = 230.1;
  can.sml_voltage_v[1] = 229.8;
  can.sml_voltage_v[2] = 230.4;
  can.sml_current_a[0] = 1.23;
  can.sml_current_a[1] = 0.98;
  can.sml_current_a[2] = 1.45;
  can.sml_phase_deg[0] = 0.0;
  can.sml_phase_deg[1] = -120.0;
  can.sml_phase_deg[2] = 120.0;
  can.sml_frequency_hz = 50.02;
  can.sml_voltage_valid = true;
  can.sml_current_valid = true;
  can.sml_phase_valid = true;
  can.sml_frequency_valid = true;
  can.sml_history_kwh[0] = 12.345;
  can.sml_history_kwh[1] = 86.789;
  can.sml_history_kwh[2] = 345.678;
  can.sml_history_kwh[3] = 4123.456;
  can.sml_history_mask = 0x0F;

  /* Living room (synthetic values for host UI verification). */
  climate.living_c100 = mock_living_temp(g_sim_time);
  climate.hum_c100 = mock_living_humidity(g_sim_time);
  climate.pressure_pa = mock_living_pressure(g_sim_time);
  climate.climate_flags = CLIMATE_VALID_AHT20 | CLIMATE_VALID_BMP280;
  climate.last_update_epoch = g_sim_time;

  /* System: exercise all three indicator states in the host render. */
  can.can_status = GUI_STATUS_OK;
  can.duty_status = GUI_STATUS_INACTIVE;
  can.sd_status = GUI_STATUS_OK;
  can.wifi_status = GUI_STATUS_OK;
  can.sd_low_space = false;

  /* Storage panel: the bench card is a 59.6 GB SDHC. */
  snprintf(can.sd_card_type, sizeof(can.sd_card_type), "SDHC");
  can.sd_total_bytes = 64000000000ULL;
  can.sd_used_bytes = 1180000000ULL;
  can.sd_samples_bytes = 900000000ULL;
  can.sd_rollup_bytes = 250000000ULL;
  can.sd_logs_bytes = 30000000ULL;
  can.sd_last_rollover_epoch = g_sim_time - 3600u;
  can.sd_fault_since_epoch = 0;
  can.sd_newest_file_epoch = g_sim_time - 86400u;
  can.sd_writing = false;
  can.spiffs_total_bytes = 3u * 1024u * 1024u;
  can.spiffs_used_bytes = HAUS_RING_BYTES;
  can.ring_bytes = HAUS_RING_BYTES;
  can.ring_hours = 30u * 24u;
  can.ring_oldest_epoch = g_sim_time - 30u * 86400u;
  can.ring_newest_epoch = g_sim_time - 3600u;
  can.l1_count = 720;
  can.l2_count = 168;
  can.l1_first_epoch = g_sim_time - 12u * 3600u;
  can.l1_last_epoch = g_sim_time - 60u;
  can.l2_first_epoch = g_sim_time - 7u * 86400u;
  can.l2_last_epoch = g_sim_time - 3600u;
  can.last_restore_columns = 168;

  /* Chamber target. */
  can.chamber_target_c = (g_target_override >= 0)
                             ? (int16_t)g_target_override
                             : mock_target_temp(g_sim_time);

  /* Water in/out are measured. */
  can.water_modelled = false;

  /* Uptime (seconds since simulated start). */
  can.uptime_seconds = g_sim_time - 1728892800u;

  /* Counters and fault flags are zero in the mock. */
  can.rx_frames = 0;
  can.tx_frames = 0;
  can.bus_errors = 0;
  can.last_frame_ms = 0;
  can.can_enabled = g_can_enabled;
  can.can_fault = 0;
  can.sd_fault = 0;
  can.i2c_fault = 0;

  if (dump_loaded()) apply_dump_state(can, climate);

  g_can.publish();
  g_climate.publish();
}

void gui_get_state(HausState* out) {
  if (!out) return;
  gui_update_state();
  out->can = g_can.acquire();
  out->climate = g_climate.acquire();
}

uint8_t gui_spiffs_files(gui_spiffs_file_t* out, uint8_t max) {
  static const struct {
    const char* name;
    uint32_t size;
  } files[] = {
      {"config.ini", 512},
      {"sensor_roles.bin", 128},
      {"rollup_ring.bin", HAUS_RING_BYTES},
      {"index.html", 3405},
      {"serverindex.html", 5120},
  };
  if (!out || max == 0) return 0;
  uint8_t n = 0;
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]) && n < max; i++) {
    snprintf(out[n].name, sizeof(out[n].name), "%s", files[i].name);
    out[n].size = files[i].size;
    n++;
  }
  return n;
}

int gui_get_history(const gui_history_request_t* req) {
  if (!req || !req->samples || !req->out_count) return -1;
  *req->out_count = 0;

  if (dump_loaded()) {
    const uint32_t oldest = dump_oldest_epoch();
    const uint32_t newest = dump_newest_epoch();
    uint32_t start = req->start_epoch;
    uint32_t end = req->end_epoch;
    if (start < oldest) start = oldest;
    if (end > newest) end = newest;
    if (start > end) return 0;

    const uint32_t total = (end - start) / 60u + 1u;
    uint32_t step = 1;
    if (total > req->max_samples)
      step = (total + req->max_samples - 1u) / req->max_samples;
    uint16_t written = 0;
    for (uint32_t t = start; t <= end && written < req->max_samples;
         t += 60u * step) {
      gui_sample_t one[1];
      dump_read_samples_chunk(t, 0, 1, one);
      req->samples[written++] = one[0];
    }
    *req->out_count = written;
    return 0;
  }

  init_history();

  /* Find the range of samples that fall within [start_epoch, end_epoch]. */
  int start_idx = -1, end_idx = -1;

  for (int i = 0; i < MAX_HISTORY_SAMPLES; i++) {
    if (g_history[i].epoch >= req->start_epoch && start_idx == -1) {
      start_idx = i;
    }
    if (start_idx != -1 && g_history[i].epoch <= req->end_epoch) {
      end_idx = i;
    }
  }

  if (start_idx == -1 || end_idx == -1) return 0; /* No data in range. */

  int total = end_idx - start_idx + 1;

  /* Downsample across the whole range so the chart spans [start, end]. */
  int step = 1;
  if (total > (int)req->max_samples) {
    step = (total + (int)req->max_samples - 1) / (int)req->max_samples;
  }

  int written = 0;
  for (int i = start_idx; i <= end_idx && written < (int)req->max_samples;
       i += step) {
    req->samples[written++] = g_history[i];
  }

  *req->out_count = (uint16_t)written;
  return 0;
}

/* ─── Streaming readers for the chart builder ───────────────────────────────
 */

static int history_index_for(uint32_t epoch) {
  init_history();
  if (epoch < g_history[0].epoch) return -1;
  const uint32_t idx = (epoch - g_history[0].epoch) / 60u;
  if (idx >= MAX_HISTORY_SAMPLES) return -1;
  return g_history[idx].epoch == epoch ? (int)idx : -1;
}

size_t gui_read_samples_chunk(uint32_t start_epoch, size_t first, size_t count,
                              gui_sample_t* out) {
  if (!out || count == 0) return 0;
  if (dump_loaded())
    return dump_read_samples_chunk(start_epoch, first, count, out);
  const uint32_t base = start_epoch - (start_epoch % 60u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t epoch = base + (uint32_t)(first + i) * 60u;
    const int idx = history_index_for(epoch);
    if (idx >= 0) {
      out[i] = g_history[idx];
    } else {
      memset(&out[i], 0, sizeof(out[i]));
      out[i].epoch = epoch;
      out[i].temp[0] = out[i].temp[1] = out[i].temp[2] = GUI_EMPTY;
      out[i].living_c100 = GUI_EMPTY;
      out[i].target_c100 = GUI_EMPTY;
      out[i].flags = SAMPLE_FLAG_GAP;
    }
  }
  return count;
}

size_t gui_read_rollup_chunk(uint32_t start_epoch, size_t first, size_t count,
                             RollupColumn* out) {
  if (!out || count == 0) return 0;
  if (dump_loaded())
    return dump_read_rollup_chunk(start_epoch, first, count, out);
  const uint32_t base = start_epoch - (start_epoch % 3600u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t hour = base + (uint32_t)(first + i) * 3600u;
    memset(&out[i], 0, sizeof(out[i]));
    out[i].t = hour / 60u;
    for (uint32_t m = 0; m < 60; m++) {
      const int idx = history_index_for(hour + m * 60u);
      if (idx < 0) continue;
      const gui_sample_t* s = &g_history[idx];
      if (s->temp[0] != GUI_EMPTY) {
        const int32_t v = s->temp[0];
        if (out[i].n[0] == 0) out[i].mn[0] = out[i].mx[0] = v;
        if (v < out[i].mn[0]) out[i].mn[0] = v;
        if (v > out[i].mx[0]) out[i].mx[0] = v;
        out[i].sum[0] += v;
        out[i].n[0]++;
      }
      if (s->temp[1] != GUI_EMPTY) {
        const int32_t v = s->temp[1];
        if (out[i].n[1] == 0) out[i].mn[1] = out[i].mx[1] = v;
        if (v < out[i].mn[1]) out[i].mn[1] = v;
        if (v > out[i].mx[1]) out[i].mx[1] = v;
        out[i].sum[1] += v;
        out[i].n[1]++;
      }
      if (s->temp[2] != GUI_EMPTY) {
        const int32_t v = s->temp[2];
        if (out[i].n[2] == 0) out[i].mn[2] = out[i].mx[2] = v;
        if (v < out[i].mn[2]) out[i].mn[2] = v;
        if (v > out[i].mx[2]) out[i].mx[2] = v;
        out[i].sum[2] += v;
        out[i].n[2]++;
      }
      if (!(s->stale_mask & SAMPLE_STALE_AC)) {
        const int32_t v = s->ac_rms_dc;
        if (out[i].n[7] == 0) out[i].mn[7] = out[i].mx[7] = v;
        if (v < out[i].mn[7]) out[i].mn[7] = v;
        if (v > out[i].mx[7]) out[i].mx[7] = v;
        out[i].sum[7] += v;
        out[i].n[7]++;
        const int32_t a = s->ac_angle_cdeg;
        if (out[i].n[8] == 0) out[i].mn[8] = out[i].mx[8] = a;
        if (a < out[i].mn[8]) out[i].mn[8] = a;
        if (a > out[i].mx[8]) out[i].mx[8] = a;
        out[i].sum[8] += a;
        out[i].n[8]++;
      }
    }
  }
  return count;
}

size_t gui_read_target_samples_chunk(uint32_t start_epoch, size_t first,
                                     size_t count, int16_t* out) {
  if (!out || count == 0) return 0;
  if (dump_loaded())
    return dump_read_target_samples_chunk(start_epoch, first, count, out);
  const uint32_t base = start_epoch - (start_epoch % 60u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t epoch = base + (uint32_t)(first + i) * 60u;
    const int idx = history_index_for(epoch);
    out[i] = idx >= 0 ? g_history[idx].target_c100 : GUI_EMPTY;
  }
  return count;
}

size_t gui_read_target_hours_chunk(uint32_t start_epoch, size_t first,
                                   size_t count, int16_t* out) {
  if (!out || count == 0) return 0;
  if (dump_loaded())
    return dump_read_target_hours_chunk(start_epoch, first, count, out);
  const uint32_t base = start_epoch - (start_epoch % 3600u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t hour = base + (uint32_t)(first + i) * 3600u;
    out[i] = history_index_for(hour) >= 0 ? mock_target_temp(hour) : GUI_EMPTY;
  }
  return count;
}

uint32_t gui_history_newest_epoch(void) {
  if (dump_loaded()) return dump_newest_epoch();
  init_history();
  return g_history[MAX_HISTORY_SAMPLES - 1].epoch;
}

bool gui_history_has_data(uint32_t start_epoch, uint32_t end_epoch) {
  if (dump_loaded()) {
    return end_epoch >= dump_oldest_epoch() &&
           start_epoch <= dump_newest_epoch();
  }
  init_history();
  return end_epoch >= g_history[0].epoch &&
         start_epoch <= g_history[MAX_HISTORY_SAMPLES - 1].epoch;
}

const gui_sensor_info_t* gui_get_sensors(uint8_t* count) {
  if (count) *count = 4; /* three bound probes + one unbound slot */
  return g_sensors;
}

bool gui_water_model_available(void) { return false; }

bool gui_can_enabled(void) { return g_can_enabled; }

/* No mux on the host: the requested state is the actual state. */
bool gui_can_actual(bool* known) {
  if (known) *known = true;
  return g_can_enabled;
}

static uint8_t s_mock_roles[4] = {SENSOR_FN_BOARD, SENSOR_FN_WATER_IN,
                                  SENSOR_FN_WATER_OUT, SENSOR_FN_NONE};
static uint8_t s_mock_roms[4][8] = {
    {0x28, 0x1B, 0xF7, 0x57, 0x04, 0xE1, 0x3C, 0x26},
    {0x28, 0x76, 0xC2, 0x57, 0x04, 0xE1, 0x3C, 0xF9},
    {0x28, 0xB9, 0xEE, 0x81, 0xE3, 0xE1, 0x3C, 0x2E},
    {0, 0, 0, 0, 0, 0, 0, 0},
};
static uint8_t s_role_count = 3;
static bool s_roles_ready = false;

static void init_roles(void) {
  if (s_roles_ready) return;
  s_roles_ready = true;
  if (!dump_loaded()) return;
  const uint8_t count = dump_roles_count();
  if (count == 0 || count > 4) return;
  s_role_count = count;
  for (uint8_t i = 0; i < count; i++) {
    dump_roles_get(i, s_mock_roms[i], &s_mock_roles[i]);
  }
}

uint8_t gui_roles_count(void) {
  init_roles();
  return s_role_count;
}

bool gui_roles_get(uint8_t index, uint8_t rom[8], uint8_t* function) {
  init_roles();
  if (index >= s_role_count) return false;
  if (rom) memcpy(rom, s_mock_roms[index], 8);
  if (function) *function = s_mock_roles[index];
  return true;
}

bool gui_roles_set(uint8_t index, uint8_t function) {
  init_roles();
  if (index >= s_role_count || function >= SENSOR_FN_COUNT) return false;
  s_mock_roles[index] = function;
  return true;
}

const char* gui_wifi_ip(void) { return "192.168.1.100"; }

/* Persistence lives in the firmware's SPIFFS config; the host keeps it in
 * memory so the Settings screen renders and toggles. */
static bool g_cfg_can = false;
bool haus_config_can_enabled(void) { return g_cfg_can; }
void haus_config_set_can_enabled(bool enabled) { g_cfg_can = enabled; }

void gui_request_redraw(void) {}

bool gui_take_redraw(void) { return false; }

void gui_set_can_enabled(bool enabled) { g_can_enabled = enabled; }

void gui_storage_refresh(void) {}

static bool g_backlight_on = true;

bool gui_backlight_on(void) { return g_backlight_on; }

void gui_set_backlight(bool on) { g_backlight_on = on; }

bool gui_send_pwm(uint16_t duty_permille) {
  return duty_permille <= 500 || duty_permille == 1000;
}

bool gui_reset_panel(void) { return true; }
