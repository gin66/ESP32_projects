/**
 * @file haus_gui.cpp
 * @brief Device implementation of the GUI data interface.
 *
 * Reads real data from the CAN bus, SD card, and NTP/RTC to populate the
 * HausState snapshot. This file is compiled on the ESP32 device.
 *
 * The host harness uses haus_gui_mock.cpp instead (selected by HAUS_HOST).
 */

#include "haus_gui.h"
#include "haus_can.h"
#include "haus_history.h"
#include "haus_rollup.h"
#include "haus_sd.h"
#include "haus_triple_buffer.h"

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <time.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/* ─── Internal state ────────────────────────────────────────────────────────
 *
 *  One triple buffer per producer (whitepaper §10.2). canTask owns CanState,
 *  sensorTask owns ClimateState. gui_update_state() writes every field of its
 *  private slot and publishes it; gui_get_state() acquires both blocks and
 *  copies them into caller memory. Readers never retain a pointer into a slot.
 *
 *  The triple-buffer exchange assumes one reader at a time, so the UI and the
 *  recorder serialise on g_read_mutex. Producers never take that lock.
 */

static TripleBuffer<CanState> g_can;
static TripleBuffer<ClimateState> g_climate;
static std::atomic<bool> g_published{false};

static portMUX_TYPE g_read_mutex_init = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t g_read_mutex = nullptr;

static SemaphoreHandle_t read_mutex(void) {
    portENTER_CRITICAL(&g_read_mutex_init);
    if (!g_read_mutex) {
        static StaticSemaphore_t buf;
        g_read_mutex = xSemaphoreCreateMutexStatic(&buf);
    }
    portEXIT_CRITICAL(&g_read_mutex_init);
    return g_read_mutex;
}

/* Sensor identity cache — updated when identity frames arrive. */
static uint8_t g_sensor_roms[GUI_MAX_SENSORS][8];
static uint8_t g_sensor_bound_count = 0;

/* ─── CAN bus readers (called by gui_update_state) ────────────────────────── */

static void read_temperatures(haus_can_temperature_t *out) {
    /* TODO: Read from CAN bus 0x200 frame.
     * For now, return all HAUS_CAN_TEMP_EMPTY until CAN is integrated. */
    for (int i = 0; i < HAUS_CAN_MAX_SENSORS; i++) {
        out->temperature[i] = HAUS_CAN_TEMP_EMPTY;
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

static void read_living(int16_t *temp, uint16_t *hum, uint32_t *pres,
                        uint8_t *flags) {
    /* TODO: Read from I2C sensors (AHT20 + BMP380).
     * On this board, no local sensors are usable — return invalid. */
    *temp = GUI_EMPTY;
    *hum = 0;
    *pres = 0;
    *flags = 0;
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

void gui_get_state(HausState *out) {
    if (!out) return;
    if (!g_published.load(std::memory_order_acquire)) {
        gui_update_state();
    }

    SemaphoreHandle_t lock = read_mutex();
    xSemaphoreTake(lock, portMAX_DELAY);
    out->can = g_can.acquire();
    out->climate = g_climate.acquire();
    xSemaphoreGive(lock);
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
 *  Call gui_update_state() from a low-priority task (~1 Hz) to refresh the
 *  producer blocks. The UI calls gui_get_state(), which copies them out.
 */

void gui_update_state(void) {
    CanState &can = g_can.write();
    ClimateState &climate = g_climate.write();

    /* System clock. tpl_wifi_setup() starts SNTP, so time() becomes valid
     * once the first sync has landed. Before that the UI must show --:--. */
    const time_t now = time(nullptr);
    can.timestamp = (now > 0) ? (uint32_t)now : 0;
    can.synced = (now > 1700000000);  /* after 2023-11-14 */
    /* No CAN task yet; 0 makes any future valid value explicitly stale. */
    can.last_update_epoch = 0;

    /* Temperatures — no CAN wiring yet, so every slot is empty. */
    haus_can_temperature_t temps;
    read_temperatures(&temps);
    for (int i = 0; i < HAUS_CAN_MAX_SENSORS; i++) {
        can.temp_c100[i] = temps.temperature[i];
        can.fn_of_slot[i] = SENSOR_FN_NONE;
    }
    for (int i = 0; i < SENSOR_FN_COUNT; i++) {
        can.temp_by_fn[i] = HAUS_CAN_TEMP_EMPTY;
    }

    /* PWM — reported-by-0x280 value; nothing received yet. */
    haus_can_pwm_t pwm;
    read_pwm(&pwm);
    can.pwm_permille = pwm.duty_permille;
    can.frequency_hz = pwm.frequency_hz;
    can.pwm_src = (uint8_t)haus_can_pwm_source(pwm.flags);
    can.active = (can.pwm_permille > 0 &&
                          can.pwm_permille < 1000);
    can.pwm_pending = 0xFFFF;
    can.requested_permille = 0xFFFF;
    can.has_pending = false;
    can.pwm_pending_since_ms = 0;

    /* Household power — no SML decode yet. */
    int32_t watts = 0;
    uint8_t power_src = GUI_PWR_SRC_NONE;
    read_power(&watts, &power_src);
    can.watts = watts;
    can.watts_last = watts;
    can.valid = false;
    can.power_src = power_src;
    can.power_dw = 0;

    /* Living room — no usable local sensors on this board. */
    int16_t living_temp = GUI_EMPTY;
    uint16_t living_hum = 0;
    uint32_t living_pres = 0;
    uint8_t climate_flags = 0;
    read_living(&living_temp, &living_hum, &living_pres, &climate_flags);
    climate.living_c100 = living_temp;
    climate.hum_c100 = living_hum;
    climate.pressure_pa = living_pres;
    climate.climate_flags = climate_flags;
    /* sensorTask (S18, abandoned) would stamp this when it reads. */
    climate.last_update_epoch = 0;

    can.can_status = GUI_STATUS_INACTIVE;
    can.duty_status = (can.pwm_src == PWM_SRC_CAN)
                                  ? GUI_STATUS_OK
                                  : GUI_STATUS_INACTIVE;
    /* SD is green when present and readable, grey when absent. Red is reserved
     * for an in-progress write (SD status semantics, §8.15.1). */
    can.sd_status = haus_sd_is_mounted() ? GUI_STATUS_OK
                                         : GUI_STATUS_INACTIVE;
    uint64_t total_bytes = 0;
    uint64_t used_bytes = 0;
    can.sd_low_space = false;
    if (haus_sd_space_get(&total_bytes, &used_bytes)) {
        const uint64_t free_bytes =
            total_bytes > used_bytes ? total_bytes - used_bytes : 0;
        can.sd_low_space = free_bytes < (50ULL * 1024ULL * 1024ULL);
    }

    /* WiFi is optional after boot: the symbol is green only while associated
     * and holding a DHCP lease, grey otherwise. */
    const IPAddress ip = WiFi.localIP();
    const bool ip_valid = (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0);
    can.wifi_status = (WiFi.status() == WL_CONNECTED && ip_valid)
                          ? GUI_STATUS_OK
                          : GUI_STATUS_INACTIVE;

    can.chamber_target_c = 6000;  /* 60 °C default */
    can.water_modelled = false;
    can.uptime_seconds = millis() / 1000;

    /* Counters and fault flags are refreshed by the CAN task in S8.03. */
    can.rx_frames = 0;
    can.tx_frames = 0;
    can.bus_errors = 0;
    can.last_frame_ms = 0;
    can.can_fault = 0;
    can.sd_fault = haus_history_sd_fault() ? 1 : 0;
    can.i2c_fault = 0;

    g_can.publish();
    g_climate.publish();
    g_published.store(true, std::memory_order_release);
}
