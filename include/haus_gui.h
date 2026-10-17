/**
 * @file haus_gui.h
 * @brief Pure-data GUI interface for the 28_hauszentrale panel.
 *
 * Shared by the ESP32 firmware (haus_gui.cpp) and the macOS host harness
 * (haus_gui_mock.cpp). Builds every screen from this interface; no device
 * or host specifics.
 *
 * Data structures:
 *   gui_sample_t      — 24 bytes, one minute of sensor data
 *   SensorFn          — the function a sensor is bound to
 *   CanState          — immutable block owned by canTask
 *   ClimateState      — immutable block owned by sensorTask
 *   HausState         — merged read-only view, copied out per call
 *   gui_sensor_info_t — bound sensor (rank, ROM, label)
 */

#ifndef HAUS_GUI_H
#define HAUS_GUI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Constants ─────────────────────────────────────────────────────────── */

#define GUI_EMPTY      0x7FFF   /* Sentinel: no valid sample temperature */
#define GUI_MAX_SENSORS  8

/* Canonical CAN constants. Mirrored here because haus_can.h is C++-only and
 * this header is also included by haus_ui.c. Keep both copies identical. */
#ifndef HAUS_CAN_MAX_SENSORS
#define HAUS_CAN_MAX_SENSORS 4
#endif
#ifndef HAUS_CAN_TEMP_EMPTY
#define HAUS_CAN_TEMP_EMPTY INT16_MIN
#endif

/* Status indicator state. The app decides the state; the UI maps it to a
 * palette colour (green / red / grey). */
typedef enum {
    GUI_STATUS_INACTIVE = 0,  /* grey: not present or not used */
    GUI_STATUS_OK,            /* green: healthy / available */
    GUI_STATUS_ERROR,         /* red: fault */
} gui_status_t;

/* ─── Sample (24 bytes, static_assert on both host and device) ──────────── */

typedef struct {
    uint32_t epoch;            /* 0: seconds since boot / real timestamp */
    int16_t temp[4];          /* 4: chamber, water_in, water_out, padding */
    uint16_t duty_permille;   /* 12: 0–1000 */
    int32_t watts;            /* 14: SML household watts */
    uint8_t flags;            /* 18: bit 0 = modelled, bit 1 = GAP */
    uint8_t padding[5];       /* 19–23: reserved */
} gui_sample_t;
/* static_assert(sizeof(gui_sample_t) == 24); */

/* ─── Sensor function (whitepaper §10.2) ─────────────────────────────────── */

/* C spelling of the whitepaper's `enum class SensorFn`. Values are stable and
 * may be stored in CanState.fn_of_slot. */
typedef enum {
    SENSOR_FN_CHAMBER = 0,
    SENSOR_FN_WATER_IN,
    SENSOR_FN_WATER_OUT,
    SENSOR_FN_BOARD,
    SENSOR_FN_NONE,
    SENSOR_FN_COUNT
} SensorFn;

/* Climate validity bits (whitepaper §7.4 / D24). */
#define CLIMATE_VALID_AHT20  0x01
#define CLIMATE_VALID_BMP280 0x02

/* ─── Producer blocks (whitepaper §10.2) ─────────────────────────────────── */

/* Owner: canTask. Written completely each cycle, published as one block. */
typedef struct {
    uint32_t timestamp;
    bool     synced;

    /* Temperatures: HAUS_CAN_TEMP_EMPTY means "no sensor in this slot". */
    int16_t  temp_c100[HAUS_CAN_MAX_SENSORS];
    uint8_t  fn_of_slot[HAUS_CAN_MAX_SENSORS];  /* SensorFn, or SENSOR_FN_NONE */
    int16_t  temp_by_fn[SENSOR_FN_COUNT];

    uint16_t pwm_permille;      /* reported by 0x280, never what we sent */
    uint16_t frequency_hz;
    uint8_t  pwm_src;           /* PWM_SRC_EEPROM / _CAN / _NRF / _FAULT */
    bool     active;
    uint16_t pwm_pending;       /* slider position; 0xFFFF == none pending */
    uint16_t requested_permille;
    bool     has_pending;
    uint32_t pwm_pending_since_ms;

    uint16_t power_dw;          /* decawatts, same unit as Sample.power_dw */
    uint8_t  power_src;         /* NONE / SML / ADC */
    int32_t  watts;
    int32_t  watts_last;
    bool     valid;

    gui_status_t can_status;
    gui_status_t duty_status;
    gui_status_t sd_status;
    gui_status_t wifi_status;
    bool         sd_low_space;
    int16_t      chamber_target_c;
    bool         water_modelled;
    uint32_t     uptime_seconds;

    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t bus_errors;
    uint32_t last_frame_ms;
    uint8_t  can_fault;         /* sticky until a frame is received again */
    uint8_t  sd_fault;
    uint8_t  i2c_fault;
} CanState;

/* Owner: sensorTask. Written completely each cycle, published as one block. */
typedef struct {
    int16_t  living_c100;       /* × 100 °C */
    uint16_t hum_c100;          /* × 100 %rh */
    uint32_t pressure_pa;       /* Pa */
    uint8_t  climate_flags;     /* CLIMATE_VALID_* */
} ClimateState;

/* Read-only merged view, assembled per call by gui_get_state(). */
typedef struct {
    CanState     can;
    ClimateState climate;
} HausState;

/* ─── Sensor info ───────────────────────────────────────────────────────── */

typedef struct {
    uint8_t rank;
    uint8_t rom[8];
    char function_label[16];
    bool bound;
} gui_sensor_info_t;

/* ─── History request ───────────────────────────────────────────────────── */

typedef struct {
    uint32_t start_epoch;
    uint32_t end_epoch;
    gui_sample_t *samples;
    uint16_t max_samples;
    uint16_t *out_count;
} gui_history_request_t;

/* ─── API ───────────────────────────────────────────────────────────────── */

/* Update state from sensors / CAN / SD. Call periodically (~1 Hz). */
void gui_update_state(void);

/* Copy the current merged snapshot into caller-owned memory.
 * Readers never retain a pointer into a producer block. */
void gui_get_state(HausState *out);

/* Get historical samples within [start_epoch, end_epoch].
 * Returns 0 on success, -1 on bad args. */
int gui_get_history(const gui_history_request_t *req);

/* Get bound sensor list. */
const gui_sensor_info_t *gui_get_sensors(uint8_t *count);

/* Water model available? */
bool gui_water_model_available(void);

/* PWM source constants */
#define GUI_PWM_SRC_CAN    1
#define GUI_PWM_SRC_EEPROM 2

/* Power source constants */
#define GUI_PWR_SRC_NONE   0
#define GUI_PWR_SRC_SML    1

#ifdef __cplusplus
}  /* extern "C" */

#include <type_traits>
static_assert(std::is_trivially_copyable<CanState>::value,
              "CanState must be trivially copyable");
static_assert(std::is_trivially_copyable<ClimateState>::value,
              "ClimateState must be trivially copyable");
static_assert(std::is_trivially_copyable<HausState>::value,
              "HausState must be trivially copyable");
static_assert(std::is_trivially_copyable<gui_sample_t>::value,
              "gui_sample_t must be trivially copyable");
#endif

#endif /* HAUS_GUI_H */
