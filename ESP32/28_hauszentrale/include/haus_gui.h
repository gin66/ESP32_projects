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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "haus_rollup.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Constants ─────────────────────────────────────────────────────────── */

#define GUI_EMPTY 0x7FFF /* Sentinel: no valid sample temperature */
#define GUI_MAX_SENSORS 8

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
  GUI_STATUS_INACTIVE = 0, /* grey: not present or not used */
  GUI_STATUS_OK,           /* green: healthy / available */
  GUI_STATUS_ERROR,        /* red: fault */
} gui_status_t;

/* ─── Sample (24 bytes, static_assert on both host and device) ──────────── */

typedef struct {
  uint32_t epoch;       /* 0: minute-aligned unix time */
  int16_t temp[3];      /* 4: chamber, water_in, water_out */
  int16_t living_c100;  /* 10: living room, HAUS_CAN_TEMP_EMPTY if absent */
  uint16_t hum_c100;    /* 12: 0.01 %RH */
  uint16_t power_dw;    /* 14: decawatts, saturated at 65535 */
  uint32_t pressure_pa; /* 16: Pa */
  uint8_t flags;        /* 20: SAMPLE_FLAG_* */
  uint8_t stale_mask;   /* 21: SAMPLE_STALE_* bits, per source */
  int16_t target_c100;  /* 22: Soll / setpoint (third chart series) */
} gui_sample_t;

/* Sample flags */
#define SAMPLE_FLAG_MODELLED 0x01
#define SAMPLE_FLAG_GAP 0x02
#define SAMPLE_FLAG_STALE 0x04
#define SAMPLE_FLAG_CLIMATE_AHT20 0x08
#define SAMPLE_FLAG_CLIMATE_BMP280 0x10

/* Per-source stale bits, in RollupColumn series order (0…5), plus power. */
#define SAMPLE_STALE_CHAMBER 0x01
#define SAMPLE_STALE_WATER_IN 0x02
#define SAMPLE_STALE_WATER_OUT 0x04
#define SAMPLE_STALE_LIVING 0x08
#define SAMPLE_STALE_HUMIDITY 0x10
#define SAMPLE_STALE_PRESSURE 0x20
#define SAMPLE_STALE_POWER 0x40

#if defined(__cplusplus)
static_assert(sizeof(gui_sample_t) == 24,
              "gui_sample_t must be 24 bytes (fixed-size records)");
#else
_Static_assert(sizeof(gui_sample_t) == 24,
               "gui_sample_t must be 24 bytes (fixed-size records)");
#endif

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
#define CLIMATE_VALID_AHT20 0x01
#define CLIMATE_VALID_BMP280 0x02
/* The SHT45 supplies the same temperature + humidity pair the AHT20 was meant
 * to. It shares the bit so history records and UI badges keep their meaning. */
#define CLIMATE_VALID_SHT45 CLIMATE_VALID_AHT20

/* ─── Producer blocks (whitepaper §10.2) ─────────────────────────────────── */

/* Owner: canTask. Written completely each cycle, published as one block. */
typedef struct {
  uint32_t timestamp;
  bool synced;
  uint32_t last_update_epoch; /* last CAN frame / power update, unix s */

  /* Temperatures: HAUS_CAN_TEMP_EMPTY means "no sensor in this slot". */
  int16_t temp_c100[HAUS_CAN_MAX_SENSORS];
  uint8_t fn_of_slot[HAUS_CAN_MAX_SENSORS]; /* SensorFn, or SENSOR_FN_NONE */
  int16_t temp_by_fn[SENSOR_FN_COUNT];

  uint16_t pwm_permille; /* reported by 0x280, never what we sent */
  uint16_t frequency_hz;
  uint8_t pwm_src; /* PWM_SRC_EEPROM / _CAN / _NRF / _FAULT */
  bool active;
  uint16_t pwm_pending; /* slider position; 0xFFFF == none pending */
  uint16_t requested_permille;
  bool has_pending;
  uint32_t pwm_pending_since_ms;

  uint16_t power_dw; /* decawatts, same unit as Sample.power_dw */
  uint8_t power_src; /* NONE / SML / ADC */
  int32_t watts;
  int32_t watts_last;
  bool valid;

  gui_status_t can_status;
  gui_status_t duty_status;
  gui_status_t sd_status;
  gui_status_t wifi_status;
  bool sd_low_space;
  int16_t chamber_target_c;
  bool water_modelled;
  uint32_t uptime_seconds;

  uint32_t rx_frames;
  uint32_t tx_frames;
  uint32_t bus_errors;
  uint32_t last_frame_ms;
  bool can_enabled;  /* user intent: CAN bus on/off (Settings) */
  uint8_t can_fault; /* sticky until a frame is received again */
  uint8_t sd_fault;
  uint8_t i2c_fault;

  /* Storage status (S23). Figures are cached; refresh on demand via
   * gui_storage_refresh(), never from the render path. */
  char sd_card_type[8];
  uint64_t sd_total_bytes;
  uint64_t sd_used_bytes;
  uint64_t sd_samples_bytes;
  uint64_t sd_rollup_bytes;
  uint64_t sd_logs_bytes;
  uint32_t sd_last_rollover_epoch;
  uint32_t sd_fault_since_epoch;
  uint32_t sd_newest_file_epoch;
  bool sd_writing;
} CanState;

/* Owner: sensorTask. Written completely each cycle, published as one block. */
typedef struct {
  int16_t living_c100;        /* × 100 °C */
  uint16_t hum_c100;          /* × 100 %rh */
  uint32_t pressure_pa;       /* Pa */
  uint8_t climate_flags;      /* CLIMATE_VALID_* */
  uint32_t last_update_epoch; /* last sensor read, unix s */
} ClimateState;

/* Read-only merged view, assembled per call by gui_get_state(). */
typedef struct {
  CanState can;
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
  gui_sample_t* samples;
  uint16_t max_samples;
  uint16_t* out_count;
} gui_history_request_t;

/* ─── API ───────────────────────────────────────────────────────────────── */

/* Update state from sensors / CAN / SD. Call periodically (~1 Hz). */
void gui_update_state(void);

/* Copy the current merged snapshot into caller-owned memory.
 * Readers never retain a pointer into a producer block. */
void gui_get_state(HausState* out);

/* Get historical samples within [start_epoch, end_epoch].
 * Returns 0 on success, -1 on bad args. */
int gui_get_history(const gui_history_request_t* req);

/* Streaming chart readers. `first` is an index from start_epoch in minutes
 * (samples) or hours (rollups). Missing records are returned zeroed with their
 * expected epoch/t filled in, so the chart builder can mark them as gaps. */
#define GUI_SERIES_TARGET 6
size_t gui_read_samples_chunk(uint32_t start_epoch, size_t first, size_t count,
                              gui_sample_t* out);
size_t gui_read_rollup_chunk(uint32_t start_epoch, size_t first, size_t count,
                             RollupColumn* out);
size_t gui_read_target_samples_chunk(uint32_t start_epoch, size_t first,
                                     size_t count, int16_t* out);
size_t gui_read_target_hours_chunk(uint32_t start_epoch, size_t first,
                                   size_t count, int16_t* out);

/* Newest stored epoch (L1 or L2) and whether any data intersects the range. */
uint32_t gui_history_newest_epoch(void);
bool gui_history_has_data(uint32_t start_epoch, uint32_t end_epoch);

/* ─── Log ring (S15) ────────────────────────────────────────────────────── */
const char* gui_log_dump(int* out_len);
int gui_log_length(void);
void gui_log_clear(void);
int gui_log_save(void);
uint32_t gui_log_dropped(void);

/* Get bound sensor list. */
const gui_sensor_info_t* gui_get_sensors(uint8_t* count);

/* Water model available? */
bool gui_water_model_available(void);

/* CAN enable switch (Settings). The hardware mux is applied by the CAN
 * bring-up path; the UI only owns the persisted intent. */
bool gui_can_enabled(void);
void gui_set_can_enabled(bool enabled);

/* Storage panel (S23): re-read capacity and per-tier sizes on demand. */
void gui_storage_refresh(void);

/* Backlight (binary, §8.14). */
bool gui_backlight_on(void);
void gui_set_backlight(bool on);

/* PWM source constants */
#define GUI_PWM_SRC_CAN 1
#define GUI_PWM_SRC_EEPROM 2

/* Power source constants */
#define GUI_PWR_SRC_NONE 0
#define GUI_PWR_SRC_SML 1

#ifdef __cplusplus
} /* extern "C" */

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
