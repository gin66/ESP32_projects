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

/* Simulated "current time" — advances 1 second per gui_update_state() call.
 * Start at 08:00 on Oct 14, 2024 (epoch = 1728892800). */
static uint32_t g_sim_time = 1728892800u; /* 2024-10-14 08:00:00 UTC */
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
 * boiler). Water-out is modelled (not measured) on this board.
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
  uint32_t start_epoch = now - ((uint32_t)MAX_HISTORY_DAYS * DAY_SECONDS);

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
    g_history[idx].power_dw = 0;
    g_history[idx].target_c100 = mock_target_temp(t);
    /* No SML/ADC power source exists yet. Do not fabricate watts: the power
     * screen must exercise the same empty state as the device. */
    g_history[idx].flags = SAMPLE_FLAG_CLIMATE_AHT20 |
                           SAMPLE_FLAG_CLIMATE_BMP280 | SAMPLE_FLAG_GAP;
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

static void init_mock_log(void) {
  if (g_mock_log_ready) return;
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
  g_mock_log_ready = true;
}

const char* gui_log_dump(int* out_len) {
  init_mock_log();
  if (out_len) *out_len = (int)strlen(g_mock_log);
  return g_mock_log;
}

int gui_log_length(void) {
  init_mock_log();
  return (int)strlen(g_mock_log);
}

void gui_log_clear(void) {
  g_mock_log[0] = '\0';
  g_mock_log_ready = true;
}

int gui_log_save(void) { return 1; }

uint32_t gui_log_dropped(void) { return 21418; }

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

void gui_update_state(void) {
  CanState& can = g_can.write();
  ClimateState& climate = g_climate.write();

  /* Update simulated time (advance ~1 second per call). */
  g_sim_time += 1;

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
  uint16_t duty = mock_pwm_duty(g_sim_time);
  can.pwm_permille = duty;
  can.frequency_hz = 20000; /* 20 kHz */
  can.pwm_src = PWM_SRC_CAN;
  can.active = (duty > 0 && duty < 1000);
  can.pwm_pending = 0xFFFF;
  can.requested_permille = 0xFFFF;
  can.has_pending = false;
  can.pwm_pending_since_ms = 0;

  /* Power: no SML/ADC source yet, so no fabricated watts. */
  can.watts = 0;
  can.watts_last = 0;
  can.valid = false;
  can.power_src = GUI_PWR_SRC_NONE;
  can.power_dw = 0;

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
  can.chamber_target_c = mock_target_temp(g_sim_time);

  /* Water model. */
  can.water_modelled = true;

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

  g_can.publish();
  g_climate.publish();
}

void gui_get_state(HausState* out) {
  if (!out) return;
  gui_update_state();
  out->can = g_can.acquire();
  out->climate = g_climate.acquire();
}

int gui_get_history(const gui_history_request_t* req) {
  init_history();

  if (!req || !req->samples || !req->out_count) return -1;

  *req->out_count = 0;

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
    }
  }
  return count;
}

size_t gui_read_target_samples_chunk(uint32_t start_epoch, size_t first,
                                     size_t count, int16_t* out) {
  if (!out || count == 0) return 0;
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
  const uint32_t base = start_epoch - (start_epoch % 3600u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t hour = base + (uint32_t)(first + i) * 3600u;
    out[i] = history_index_for(hour) >= 0 ? mock_target_temp(hour) : GUI_EMPTY;
  }
  return count;
}

uint32_t gui_history_newest_epoch(void) {
  init_history();
  return g_history[MAX_HISTORY_SAMPLES - 1].epoch;
}

bool gui_history_has_data(uint32_t start_epoch, uint32_t end_epoch) {
  init_history();
  return end_epoch >= g_history[0].epoch &&
         start_epoch <= g_history[MAX_HISTORY_SAMPLES - 1].epoch;
}

const gui_sensor_info_t* gui_get_sensors(uint8_t* count) {
  if (count) *count = 4; /* three bound probes + one unbound slot */
  return g_sensors;
}

bool gui_water_model_available(void) {
  return true; /* Water model is installed in the mock. */
}

bool gui_can_enabled(void) { return g_can_enabled; }

/* No mux on the host: the requested state is the actual state. */
bool gui_can_actual(bool* known) {
  if (known) *known = true;
  return g_can_enabled;
}

static uint8_t s_mock_roles[3] = {SENSOR_FN_BOARD, SENSOR_FN_WATER_IN,
                                  SENSOR_FN_WATER_OUT};
static const uint8_t s_mock_roms[3][8] = {
    {0x28, 0x1B, 0xF7, 0x57, 0x04, 0xE1, 0x3C, 0x26},
    {0x28, 0x76, 0xC2, 0x57, 0x04, 0xE1, 0x3C, 0xF9},
    {0x28, 0xB9, 0xEE, 0x81, 0xE3, 0xE1, 0x3C, 0x2E},
};

uint8_t gui_roles_count(void) { return 3; }

bool gui_roles_get(uint8_t index, uint8_t rom[8], uint8_t* function) {
  if (index >= 3) return false;
  if (rom) memcpy(rom, s_mock_roms[index], 8);
  if (function) *function = s_mock_roles[index];
  return true;
}

bool gui_roles_set(uint8_t index, uint8_t function) {
  if (index >= 3 || function >= SENSOR_FN_COUNT) return false;
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
