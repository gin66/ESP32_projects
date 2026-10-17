/**
 * @file haus_gui.cpp
 * @brief Device implementation of the GUI data interface.
 *
 * Reads real data from the CAN bus, SD card, and NTP/RTC to populate the
 * gui_haus_state_t snapshot. This file is compiled on the ESP32 device.
 *
 * The host harness uses haus_gui_mock.cpp instead (selected by HAUS_HOST).
 */

#include "haus_gui.h"
#include "haus_can.h"

#include <Arduino.h>
#include <time.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

/* ─── Internal state ────────────────────────────────────────────────────────
 *
 *  All data is collected into this single struct by gui_update_state() which
 *  is called from a low-priority task (or the main loop) every ~1 second.
 *  gui_get_state() returns a const pointer — the UI never modifies state.
 */

static gui_haus_state_t g_state;
static bool g_state_valid = false;

/* Sensor identity cache — updated when identity frames arrive. */
static uint8_t g_sensor_roms[GUI_MAX_SENSORS][8];
static uint8_t g_sensor_bound_count = 0;

/* ─── CAN bus readers (called by gui_update_state) ────────────────────────── */

static void read_temperatures(haus_can_temperature_t *out) {
    /* TODO: Read from CAN bus 0x200 frame.
     * For now, return all GUI_EMPTY until CAN is integrated. */
    for (int i = 0; i < HAUS_CAN_MAX_SENSORS; i++) {
        out->temperature[i] = GUI_EMPTY;
    }
}

static void read_pwm(haus_can_pwm_t *out) {
    /* TODO: Read from CAN bus 0x280 frame.
     * For now, return "off" with EEPROM source. */
    out->duty_permille = 0;
    out->frequency_hz = 0;
    out->flags = PWM_SRC_EEPROM;
}

static void read_power(int32_t *watts, uint8_t *source) {
    /* TODO: Read from SML decoder (0x600) or ADC (0x2C0).
     * For now, return no data. */
    *watts = 0;
    *source = GUI_PWR_SRC_NONE;
}

static void read_living(int16_t *temp, uint16_t *hum, uint32_t *pres) {
    /* TODO: Read from I2C sensors (AHT20 + BMP380).
     * On this board, no local sensors are usable — return GUI_EMPTY. */
    *temp = GUI_EMPTY;
    *hum = 0;
    *pres = 0;
}

/* ─── SD history reader (called by gui_get_history) ───────────────────────── */

static int read_day_file(const char *filename, gui_sample_t *samples,
                         uint16_t max_samples, uint16_t *out_count) {
    /* TODO: Open /data_YYYYMMDD.bin from SD, read samples into buffer.
     * For now, return 0 samples. */
    (void)filename;
    (void)max_samples;
    if (out_count) *out_count = 0;
    return 0;
}

static int read_rollup_file(gui_sample_t *samples, uint16_t max_samples,
                            uint16_t *out_count) {
    /* TODO: Open /rollup.bin from SD, read hourly aggregates.
     * For now, return 0 samples. */
    (void)max_samples;
    if (out_count) *out_count = 0;
    return 0;
}

/* ─── Public API ──────────────────────────────────────────────────────────── */

const gui_haus_state_t *gui_get_state(void) {
    if (!g_state_valid) {
        gui_update_state();
    }
    return &g_state;
}

int gui_get_history(const gui_history_request_t *req) {
    /* TODO: Merge day files and rollup to fill the requested range. */
    (void)read_day_file;
    (void)read_rollup_file;
    (void)req;
    return 0;
}

const gui_sensor_info_t *gui_get_sensors(uint8_t *count) {
    /* TODO: Load sensor_roles[] from SPIFFS, resolve rank → ROM → label.
     * For now, return empty. */
    (void)count;
    return NULL;
}

bool gui_water_model_available(void) {
    /* TODO: Return true if WaterModel is installed and active.
     * For now, false — water-out shows "—". */
    return false;
}

/* ─── Update function (called from task, NOT from UI thread) ────────────────
 *
 *  Call gui_update_state() from a low-priority task (~1 Hz) to refresh g_state.
 *  The UI calls gui_get_state() which returns a const pointer — no locking
 *  needed because g_state is only written by one task and read by many.
 */

void gui_update_state(void) {
    /* System clock. tpl_wifi_setup() starts SNTP, so time() becomes valid
     * once the first sync has landed. Before that the UI must show --:--. */
    const time_t now = time(nullptr);
    g_state.time.timestamp = (now > 0) ? (uint32_t)now : 0;
    g_state.time.synced = (now > 1700000000);  /* after 2023-11-14 */

    /* Temperatures — no CAN wiring yet, so every slot is empty. */
    haus_can_temperature_t temps;
    read_temperatures(&temps);
    for (int i = 0; i < 4; i++) {
        g_state.temps.temp[i] = temps.temperature[i];
    }
    g_state.temps.sensor_count = 0;

    /* PWM — reported-by-0x280 value; nothing received yet. */
    haus_can_pwm_t pwm;
    read_pwm(&pwm);
    g_state.pwm.duty_permille = pwm.duty_permille;
    g_state.pwm.frequency_hz = pwm.frequency_hz;
    g_state.pwm.source = GUI_PWM_SRC_EEPROM;
    g_state.pwm.active = false;
    g_state.pwm.requested_permille = 0xFFFF;
    g_state.pwm.has_pending = false;

    /* Household power — no SML decode yet. */
    int32_t watts = 0;
    uint8_t power_src = GUI_PWR_SRC_NONE;
    read_power(&watts, &power_src);
    g_state.power.watts = watts;
    g_state.power.watts_last = watts;
    g_state.power.valid = false;
    g_state.power.source = power_src;

    /* Living room — no usable local sensors on this board. */
    int16_t living_temp = GUI_EMPTY;
    uint16_t living_hum = 0;
    uint32_t living_pres = 0;
    read_living(&living_temp, &living_hum, &living_pres);
    g_state.living.temperature = living_temp;
    g_state.living.humidity = living_hum;
    g_state.living.pressure = living_pres;
    g_state.living.valid = false;

    g_state.system.can_status = GUI_STATUS_INACTIVE;
    g_state.system.duty_status = (g_state.pwm.source == GUI_PWM_SRC_CAN)
                                     ? GUI_STATUS_OK
                                     : GUI_STATUS_INACTIVE;
    g_state.system.sd_status = GUI_STATUS_INACTIVE;
    g_state.system.sd_low_space = false;

    g_state.chamber_target_c = 6000;  /* 60 °C default */
    g_state.water_modelled = false;
    g_state.uptime_seconds = millis() / 1000;

    g_state_valid = true;
}
