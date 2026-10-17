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

#include "haus_gui.h"
#include <cmath>
#include <cstring>
#include <cstdio>

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

/* ─── Time helpers ────────────────────────────────────────────────────────── */

/* Simulated "current time" — advances 1 second per gui_update_state() call.
 * Start at 08:00 on Oct 14, 2024 (epoch = 1728892800). */
static uint32_t g_sim_time = 1728892800u;  /* 2024-10-14 08:00:00 UTC */
static const uint32_t DAY_SECONDS = 86400u;

/* Get time-of-day in seconds (0–86399). */
static uint32_t tod(uint32_t epoch) {
    return epoch % DAY_SECONDS;
}

/* Get day-of-year (0–365) from epoch. */
static uint32_t doy(uint32_t epoch) {
    return (epoch / DAY_SECONDS) % 365;
}

/* ─── Chamber temperature model ─────────────────────────────────────────────
 *
 *  Returns c° (× 100) for the chamber at a given epoch.
 *  Follows a sinusoidal pattern with morning/evening peaks.
 */
static int16_t mock_chamber_temp(uint32_t epoch) {
    uint32_t t = tod(epoch);

    /* Target curve: night 15°C, morning ramp to 60°C by 07:30,
     * plateau 60°C 08:00–17:00, evening drop to 15°C by 22:00. */
    double target = 0.0;

    if (t < 21600) {
        /* 00:00–06:00: night setback 15°C */
        target = 15.0;
    } else if (t < 28800) {
        /* 06:00–08:00: morning ramp 15→60°C (sigmoid) */
        double progress = (double)(t - 21600) / 7200.0;
        target = 15.0 + 45.0 * (1.0 / (1.0 + exp(-8.0 * (progress - 0.5))));
    } else if (t < 61200) {
        /* 08:00–17:00: plateau 60°C */
        target = 60.0;
    } else if (t < 75600) {
        /* 17:00–22:00: evening drop 60→15°C */
        double progress = (double)(t - 61200) / 14400.0;
        target = 60.0 - 45.0 * (1.0 / (1.0 + exp(-6.0 * (progress - 0.5))));
    } else {
        /* 22:00–00:00: night setback 15°C */
        target = 15.0;
    }

    /* Add small noise (±0.2°C) and slow drift. */
    double noise = sin(epoch * 0.001) * 0.2 + sin(epoch * 0.0037) * 0.1;
    target += noise;

    /* Clamp to valid range. */
    if (target < -40.0) target = -40.0;
    if (target > 90.0) target = 90.0;

    return (int16_t)(target * 100.0);
}

/* ─── Water temperatures ────────────────────────────────────────────────────
 *
 *  Water-in follows the chamber with ~5°C offset (cooler, returns from radiators).
 *  Water-out follows the chamber with ~5°C offset (hotter, leaves boiler).
 *  Water-out is modelled (not measured) on this board.
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
    int16_t target = 6000;  /* 60°C default target */

    if (chamber == GUI_EMPTY) return 0;

    double chamber_c = chamber / 100.0;
    double target_c = target / 100.0;
    double diff = target_c - chamber_c;

    /* Duty is proportional to how far below target we are.
     * Max duty 800 permille (80%) when far below target.
     * Drops to 50 permille (5%) when near target. */
    double duty = 50.0 + 750.0 * (1.0 / (1.0 + exp(-0.3 * diff)));

    /* Add small noise. */
    duty += sin(epoch * 0.05) * 10.0;

    if (duty < 0) duty = 0;
    if (duty > 1000) duty = 1000;

    return (uint16_t)duty;
}

/* ─── SML power ─────────────────────────────────────────────────────────────
 *
 *  Household watts from smart meter. Correlates with PWM duty
 *  (higher duty = higher power), with some noise.
 */
static int32_t mock_sml_watts(uint32_t epoch) {
    uint16_t duty = mock_pwm_duty(epoch);

    /* Base load ~200W (controls, pumps) + boiler power proportional to duty.
     * Max boiler power ~3000W at full duty. */
    double base = 200.0;
    double boiler = 3000.0 * (duty / 1000.0);

    /* Add noise and some random spikes. */
    double noise = sin(epoch * 0.02) * 50.0 + sin(epoch * 0.07) * 30.0;

    int32_t watts = (int32_t)(base + boiler + noise);
    if (watts < 0) watts = 0;

    return watts;
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
#define SAMPLES_PER_DAY  1440
#define MAX_HISTORY_SAMPLES (MAX_HISTORY_DAYS * SAMPLES_PER_DAY)

static gui_sample_t g_history[MAX_HISTORY_SAMPLES];
static bool g_history_initialized = false;

static void init_history(void) {
    if (g_history_initialized) return;

    /* Generate 30 days of synthetic data starting from 30 days ago. */
    uint32_t now = g_sim_time;
    uint32_t start_epoch = now - ((uint32_t)MAX_HISTORY_DAYS * DAY_SECONDS);

    int idx = 0;
    for (uint32_t t = start_epoch; t <= now; t += 60) {
        if (idx >= MAX_HISTORY_SAMPLES) break;

        g_history[idx].epoch = t;
        g_history[idx].temp[0] = mock_chamber_temp(t);
        g_history[idx].temp[1] = mock_water_in_temp(t);
        g_history[idx].temp[2] = mock_water_out_temp(t);
        g_history[idx].temp[3] = GUI_EMPTY;  /* sentinel */
        g_history[idx].duty_permille = mock_pwm_duty(t);
        g_history[idx].watts = mock_sml_watts(t);
        g_history[idx].flags = 0x00;  /* no modelled, no GAP */

        /* Simulate occasional gaps (card removal, SD errors). */
        if (tod(t) > 50000 && tod(t) < 51000) {
            g_history[idx].flags |= 0x02;  /* GAP flag */
            g_history[idx].watts = 0;
        }

        idx++;
    }

    g_history_initialized = true;
}

/* ─── Sensor info ─────────────────────────────────────────────────────────── */

static gui_sensor_info_t g_sensors[GUI_MAX_SENSORS] = {
    { .rank = 0, .rom = {0x28, 0xFF, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC},
      .function_label = "chamber", .bound = true },
    { .rank = 1, .rom = {0x28, 0xFF, 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56},
      .function_label = "water-in", .bound = true },
    { .rank = 2, .rom = {0x28, 0xFF, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC},
      .function_label = "water-out", .bound = true },
    { .rank = 3, .rom = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      .function_label = "", .bound = false },
};

/* ─── State snapshot ──────────────────────────────────────────────────────── */

static gui_haus_state_t g_state;

void gui_update_state(void) {
    /* Update simulated time (advance ~1 second per call). */
    g_sim_time += 1;

    /* Populate state from mock functions. */
    g_state.time.timestamp = g_sim_time;
    g_state.time.synced = true;

    /* Temperatures. */
    g_state.temps.temp[0] = mock_chamber_temp(g_sim_time);
    g_state.temps.temp[1] = mock_water_in_temp(g_sim_time);
    g_state.temps.temp[2] = mock_water_out_temp(g_sim_time);
    g_state.temps.temp[3] = GUI_EMPTY;

    /* Count valid sensors. */
    g_state.temps.sensor_count = 0;
    for (int i = 0; i < 3; i++) {
        if (g_state.temps.temp[i] != GUI_EMPTY) {
            g_state.temps.sensor_count++;
        }
    }

    /* PWM. */
    uint16_t duty = mock_pwm_duty(g_sim_time);
    g_state.pwm.duty_permille = duty;
    g_state.pwm.frequency_hz = 20000;  /* 20 kHz */
    g_state.pwm.source = GUI_PWM_SRC_CAN;
    g_state.pwm.active = (duty > 0 && duty < 1000);
    g_state.pwm.requested_permille = 0xFFFF;
    g_state.pwm.has_pending = false;

    /* Power. */
    int32_t watts = mock_sml_watts(g_sim_time);
    g_state.power.watts = watts;
    g_state.power.watts_last = mock_sml_watts(g_sim_time - 60);
    g_state.power.valid = (watts > 0);
    g_state.power.source = GUI_PWR_SRC_SML;

    /* Living room (synthetic values for host UI verification). */
    g_state.living.temperature = mock_living_temp(g_sim_time);
    g_state.living.humidity = mock_living_humidity(g_sim_time);
    g_state.living.pressure = mock_living_pressure(g_sim_time);
    g_state.living.valid = (g_state.living.temperature != GUI_EMPTY);

    /* System. */
    /* System: exercise all three indicator states in the host render. */
    g_state.system.can_status = GUI_STATUS_OK;
    g_state.system.duty_status = GUI_STATUS_INACTIVE;
    g_state.system.sd_status = GUI_STATUS_ERROR;
    g_state.system.sd_low_space = false;

    /* Chamber target. */
    g_state.chamber_target_c = 6000;  /* 60°C */

    /* Water model. */
    g_state.water_modelled = true;

    /* Uptime (seconds since simulated start). */
    g_state.uptime_seconds = g_sim_time - 1728892800u;
}

const gui_haus_state_t *gui_get_state(void) {
    gui_update_state();
    return &g_state;
}

int gui_get_history(const gui_history_request_t *req) {
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

    if (start_idx == -1 || end_idx == -1) return 0;  /* No data in range. */

    int total = end_idx - start_idx + 1;

    /* Downsample across the whole range so the chart spans [start, end]. */
    int step = 1;
    if (total > (int)req->max_samples) {
        step = (total + (int)req->max_samples - 1) / (int)req->max_samples;
    }

    int written = 0;
    for (int i = start_idx; i <= end_idx && written < (int)req->max_samples; i += step) {
        req->samples[written++] = g_history[i];
    }

    *req->out_count = (uint16_t)written;
    return 0;
}

const gui_sensor_info_t *gui_get_sensors(uint8_t *count) {
    if (count) *count = GUI_MAX_SENSORS;
    return g_sensors;
}

bool gui_water_model_available(void) {
    return true;  /* Water model is installed in the mock. */
}
