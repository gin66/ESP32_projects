/**
 * @file haus_gui.h
 * @brief Pure data interface between the CAN/SD sensor layer and the LVGL UI.
 *
 * The UI layer includes ONLY this header. No ESP-IDF, no FreeRTOS, no LVGL,
 * no CAN driver — just C++ POD structs and a handful of getter functions.
 *
 * Two implementations exist behind this single header:
 *
 *   Device  (ESP32):  haus_gui.cpp  — reads real CAN bus, SD rollup, NTP.
 *   Host    (macOS):  haus_gui_mock.cpp  — generates synthetic heater data.
 *
 * Selection is controlled by the HAUS_HOST preprocessor define set in the
 * host CMakeLists.txt.
 *
 * ─── Data flow ───────────────────────────────────────────────────────────────
 *
 *   CAN bus ──► haus_can.h structs ──► haus_gui.cpp ──► gui_get_state()
 *   SD rollup ────────────────────────► gui_get_history()
 *   NTP / RTC ───────────────────────► gui_get_state()->time
 *
 * On the host harness the same getters return synthetic but realistic data
 * so the UI can be designed and verified without hardware.
 *
 * ─── Size contracts ──────────────────────────────────────────────────────────
 *
 *   gui_sample_t  == 24 bytes  (S8.04, S11.02)
 *   RollupColumn  == 88 bytes  (S11.01)
 *   ChartColumn   == 28 bytes  (S12.01)
 *
 * These sizes are asserted at compile time in both implementations.
 */

#ifndef HAUS_GUI_H
#define HAUS_GUI_H

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 *  Constants
 * ═══════════════════════════════════════════════════════════════════════════ */

#define GUI_MAX_SENSORS  4
#define GUI_MAX_LABEL    32
#define GUI_EMPTY        INT16_MIN  /* no sensor / no data */

/* PWM source badges (matching haus_can.h). */
#define GUI_PWM_SRC_CAN     0x02
#define GUI_PWM_SRC_EEPROM  0x00
#define GUI_PWM_SRC_NRF     0x04
#define GUI_PWM_SRC_FAULT   0x06

/* Power source badges. */
#define GUI_PWR_SRC_SML     0x01
#define GUI_PWR_SRC_ADC     0x02
#define GUI_PWR_SRC_NONE    0x00

/* Sensor roles (resolved from sensor_roles[] on SPIFFS). */
#define GUI_SENSOR_CHAMBER  0
#define GUI_SENSOR_WATER_IN 1
#define GUI_SENSOR_WATER_OUT 2
#define GUI_SENSOR_SENTINEL 3

/* ═══════════════════════════════════════════════════════════════════════════
 *  Real-time readings (from CAN bus, updated ~1 Hz)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Temperature readings from up to 4 sensors.
 *
 * Each slot maps to a sensor rank (0–3). A slot of GUI_EMPTY means no
 * sensor is bound to that rank. The UI resolves rank → ROM → function
 * label via gui_get_sensors().
 */
typedef struct {
    int16_t temp[GUI_MAX_SENSORS];    /* c° = °C × 100, GUI_EMPTY = no data */
    uint8_t sensor_count;              /* how many slots have valid data */
} gui_temperatures_t;

/* PWM duty cycle state.
 *
 * duty_permille is the value currently in force on the heater.
 * requested_permille (0xFFFF = none) is the pending value from the UI
 * slider that has not yet been confirmed and sent to the bus.
 */
typedef struct {
    uint16_t duty_permille;            /* 0 = off, 1000 = full on */
    uint16_t frequency_hz;
    uint8_t  source;                   /* GUI_PWM_SRC_* badge */
    bool     active;                   /* currently modulating? */
    uint16_t requested_permille;       /* pending from slider, 0xFFFF = none */
    bool     has_pending;
} gui_pwm_t;

/* Household power from SML smart meter or ADC. */
typedef struct {
    int32_t  watts;                    /* household watts */
    int32_t  watts_last;               /* previous reading (for delta) */
    bool     valid;
    uint8_t  source;                   /* GUI_PWR_SRC_SML, ADC, NONE */
} gui_power_t;

/* Living room climate (from I2C sensors — may be permanently — on this board).
 *
 * temperature, humidity, pressure. All c° or × 100.
 */
typedef struct {
    int16_t  temperature;              /* c° */
    uint16_t humidity;                 /* %rh × 100 */
    uint32_t pressure;                 /* hPa × 100 */
    bool     valid;
} gui_living_t;

/* System-wide status. */
typedef struct {
    bool     can_available;
    bool     sd_available;
    bool     sd_low_space;            /* below 50 MB floor */
} gui_system_t;

/* Time from NTP / RTC. */
typedef struct {
    uint32_t timestamp;                /* epoch seconds */
    bool     synced;                   /* NTP locked? */
} gui_time_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  Historical data (from SD rollup/day files)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * One minute-resolution sample — exactly 24 bytes (S8.04, S11.02).
 * This is the unit that gets written to /data_YYYYMMDD.bin on SD.
 */
typedef struct {
    uint32_t epoch;                    /* seconds since boot (or real epoch) */
    int16_t  temp[GUI_MAX_SENSORS];   /* c°, GUI_EMPTY = no data */
    uint16_t duty_permille;
    int32_t  watts;
    uint8_t  flags;                    /* bit 0 = modelled, bit 1 = GAP */
} gui_sample_t;

static_assert(sizeof(gui_sample_t) == 24,
              "gui_sample_t must be exactly 24 bytes (S8.04)");

/* Range query — the UI allocates a gui_sample_t array and passes a pointer.
 * Zero samples returned means no data for that range. */
typedef struct {
    uint32_t start_epoch;
    uint32_t end_epoch;
    gui_sample_t *samples;             /* caller-allocated buffer */
    uint16_t max_samples;
    uint16_t *out_count;               /* how many samples we got */
} gui_history_request_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  Resolved sensor info (rank → ROM → function label)
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t  rank;
    uint8_t  rom[8];
    char     function_label[GUI_MAX_LABEL];  /* "chamber", "water-in", … */
    bool     bound;                       /* resolved from sensor_roles[] */
} gui_sensor_info_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  THE snapshot — one struct, all the UI needs
 *
 *  gui_get_state() returns a pointer to this. The UI reads it every frame.
 *  On the device it reflects live CAN + SD + NTP state.
 *  On the host it reflects synthetic but realistic heater data.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    gui_time_t        time;
    gui_temperatures_t temps;
    gui_pwm_t          pwm;
    gui_power_t        power;
    gui_living_t       living;
    gui_system_t       system;

    int16_t chamber_target_c;     /* configurable heater target (c°) */
    bool     water_modelled;      /* water-out is modelled, not measured */
    uint32_t uptime_seconds;
} gui_haus_state_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  API
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Get the current snapshot. Thread-safe on device, returns latest state.
 * The pointer remains valid until the next call. */
const gui_haus_state_t *gui_get_state(void);

/* Query historical data. Returns 0 on success, non-zero on error.
 * Fills *out_count with the number of samples written to samples[]. */
int gui_get_history(const gui_history_request_t *req);

/* Get resolved sensor list. Returns the array; *count = number of sensors. */
const gui_sensor_info_t *gui_get_sensors(uint8_t *count);

/* Get water model status. */
bool gui_water_model_available(void);

#ifdef __cplusplus
}
#endif

#endif /* HAUS_GUI_H */
