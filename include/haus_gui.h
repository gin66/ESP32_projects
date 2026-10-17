/**
 * @file haus_gui.h
 * @brief Pure-data GUI interface for the 28_hauszentrale panel.
 *
 * Shared by the ESP32 firmware (haus_gui.cpp) and the macOS host harness
 * (haus_gui_mock.cpp). Builds every screen from this interface; no device
 * or host specifics.
 *
 * Data structures:
 *   gui_sample_t     — 24 bytes, one minute of sensor data
 *   gui_haus_state_t — current system snapshot
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

#define GUI_EMPTY      0x7FFF   /* Sentinel: no valid temperature */
#define GUI_MAX_SENSORS  8

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

/* ─── State snapshot ────────────────────────────────────────────────────── */

typedef struct {
    /* Time */
    struct {
        uint32_t timestamp;
        bool synced;
    } time;

    /* Temperatures (× 100, GUI_EMPTY = no sensor) */
    struct {
        int16_t temp[4];
        uint8_t sensor_count;
    } temps;

    /* PWM */
    struct {
        uint16_t duty_permille;
        uint16_t frequency_hz;
        uint8_t source;       /* GUI_PWM_SRC_CAN / EEPROM */
        bool active;
        uint16_t requested_permille;
        bool has_pending;
    } pwm;

    /* SML Power */
    struct {
        int32_t watts;
        int32_t watts_last;
        bool valid;
        uint8_t source;       /* GUI_PWR_SRC_SML / — */
    } power;

    /* Living room */
    struct {
        int16_t temperature;  /* × 100, GUI_EMPTY = no sensor */
        uint16_t humidity;    /* × 100 */
        uint32_t pressure;    /* × 100 hPa */
        bool valid;
    } living;

    /* System */
    struct {
        gui_status_t can_status;
        gui_status_t duty_status;
        gui_status_t sd_status;
        bool sd_low_space;
    } system;

    /* Chamber */
    int16_t chamber_target_c;   /* × 100 */

    /* Water model */
    bool water_modelled;

    /* Uptime */
    uint32_t uptime_seconds;

} gui_haus_state_t;

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

/* Get current system snapshot (caller must not free). */
const gui_haus_state_t *gui_get_state(void);

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
}
#endif

#endif /* HAUS_GUI_H */
