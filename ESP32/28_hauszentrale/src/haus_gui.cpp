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

#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

#include <atomic>
#include <cstring>

#include "haus_can.h"
#include "haus_can_bus.h"
#include "haus_history.h"
#include "haus_rollup.h"
#include "haus_sd.h"
#include "haus_sensor_roles.h"
#include "haus_sht45.h"
#include "haus_triple_buffer.h"
#include "tpl_debug_log.h"
#include "tpl_ws7.h"

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
static bool g_can_enabled = false;

/* ─── CAN bus readers (called by gui_update_state) ──────────────────────────
 */

static void read_temperatures(haus_can_temperature_t* out) {
  haus_can_snapshot_t snap;
  haus_can_bus_get(&snap);
  for (int i = 0; i < HAUS_CAN_MAX_SENSORS; i++)
    out->temperature[i] = snap.temp_c100[i];
}

static void read_pwm(haus_can_pwm_t* out) {
  haus_can_snapshot_t snap;
  haus_can_bus_get(&snap);
  out->duty_permille = snap.pwm_permille;
  out->frequency_hz = snap.frequency_hz;
  out->flags = snap.pwm_flags;
}

static void read_power(int32_t* watts, uint8_t* source) {
  /* TODO: Read from SML decoder (0x600) or ADC (0x2C0).
   * For now, return no data. */
  *watts = 0;
  *source = GUI_PWR_SRC_NONE;
}

static void read_living(int16_t* temp, uint16_t* hum, uint32_t* pres,
                        uint8_t* flags) {
  /* SHT45 on the shared I2C bus at 0x44 (does not collide with CH422G or
   * GT911). Polling is non-blocking: the previous conversion is read and the
   * next one is started, so the UI loop never waits for the ~8.3 ms
   * measurement. Pressure stays unavailable (no BMP sensor fitted). */
  int16_t t = 0;
  uint16_t h = 0;
  const bool ok = haus_sht45_poll(&t, &h);
  static bool logged_first = false;
  if (ok && !logged_first) {
    logged_first = true;
    TPL_LOGI("climate", "SHT45 first reading: %d.%02d C, %u.%02u %%rh", t / 100,
             (t % 100 + 100) % 100, h / 100, h % 100);
  }
  *temp = ok ? t : GUI_EMPTY;
  *hum = ok ? h : 0;
  *pres = 0;
  *flags = ok ? CLIMATE_VALID_SHT45 : 0;
}

/* ─── SD history reader (called by gui_get_history) ─────────────────────────
 */

static int read_day_file(const char* filename, gui_sample_t* samples,
                         uint16_t max_samples, uint16_t* out_count) {
  /* TODO: Open /data_YYYYMMDD.bin from SD, read samples into buffer.
   * For now, return 0 samples. */
  (void)filename;
  (void)max_samples;
  if (out_count) *out_count = 0;
  return 0;
}

static int read_rollup_file(gui_sample_t* samples, uint16_t max_samples,
                            uint16_t* out_count) {
  /* TODO: Open /rollup.bin from SD, read hourly aggregates.
   * For now, return 0 samples. */
  (void)max_samples;
  if (out_count) *out_count = 0;
  return 0;
}

/* ─── Public API ────────────────────────────────────────────────────────────
 */

void gui_get_state(HausState* out) {
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

int gui_get_history(const gui_history_request_t* req) {
  if (!req || !req->samples || !req->out_count) return -1;
  *req->out_count = 0;
  if (req->end_epoch <= req->start_epoch) return 0;

  const uint32_t first = req->start_epoch - (req->start_epoch % 60u);
  const uint32_t last = req->end_epoch - (req->end_epoch % 60u);
  const size_t total = (last - first) / 60u + 1u;
  const size_t step = total > req->max_samples
                          ? (total + req->max_samples - 1) / req->max_samples
                          : 1u;
  size_t written = 0;
  for (size_t i = 0; i < total && written < req->max_samples; i += step) {
    gui_sample_t s;
    if (gui_read_samples_chunk(first, i, 1, &s) != 1) break;
    if (s.epoch != 0) req->samples[written++] = s;
  }
  *req->out_count = (uint16_t)written;
  return 0;
}

size_t gui_read_samples_chunk(uint32_t start_epoch, size_t first, size_t count,
                              gui_sample_t* out) {
  gui_sample_t* l1 = haus_history_l1();
  if (!l1 || !out || count == 0) return 0;

  const uint32_t base = start_epoch - (start_epoch % 60u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t epoch = base + (uint32_t)(first + i) * 60u;
    const size_t mod = (size_t)((epoch / 60u) % HAUS_L1_SAMPLES);
    if (l1[mod].epoch == epoch) {
      out[i] = l1[mod];
    } else {
      memset(&out[i], 0, sizeof(out[i]));
      out[i].epoch = epoch;
      out[i].temp[0] = out[i].temp[1] = out[i].temp[2] = HAUS_CAN_TEMP_EMPTY;
      out[i].living_c100 = HAUS_CAN_TEMP_EMPTY;
      out[i].target_c100 = HAUS_CAN_TEMP_EMPTY;
    }
  }
  return count;
}

size_t gui_read_rollup_chunk(uint32_t start_epoch, size_t first, size_t count,
                             RollupColumn* out) {
  RollupColumn* l2 = haus_history_l2();
  if (!l2 || !out || count == 0) return 0;

  const uint32_t base = start_epoch - (start_epoch % 3600u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t epoch = base + (uint32_t)(first + i) * 3600u;
    const size_t slot = (size_t)((epoch / 3600u) % HAUS_L2_COLUMNS);
    if (l2[slot].t == epoch / 60u) {
      out[i] = l2[slot];
    } else {
      memset(&out[i], 0, sizeof(out[i]));
      out[i].t = epoch / 60u;
    }
  }
  return count;
}

size_t gui_read_target_samples_chunk(uint32_t start_epoch, size_t first,
                                     size_t count, int16_t* out) {
  gui_sample_t* l1 = haus_history_l1();
  if (!l1 || !out || count == 0) return 0;

  const uint32_t base = start_epoch - (start_epoch % 60u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t epoch = base + (uint32_t)(first + i) * 60u;
    const size_t mod = (size_t)((epoch / 60u) % HAUS_L1_SAMPLES);
    out[i] =
        (l1[mod].epoch == epoch) ? l1[mod].target_c100 : HAUS_CAN_TEMP_EMPTY;
  }
  return count;
}

size_t gui_read_target_hours_chunk(uint32_t start_epoch, size_t first,
                                   size_t count, int16_t* out) {
  RollupColumn* l2 = haus_history_l2();
  if (!l2 || !out || count == 0) return 0;

  const uint32_t base = start_epoch - (start_epoch % 3600u);
  for (size_t i = 0; i < count; i++) {
    const uint32_t epoch = base + (uint32_t)(first + i) * 3600u;
    const size_t slot = (size_t)((epoch / 3600u) % HAUS_L2_COLUMNS);
    out[i] = (l2[slot].t == epoch / 60u) ? l2[slot].target_c100
                                         : HAUS_CAN_TEMP_EMPTY;
  }
  return count;
}

uint32_t gui_history_newest_epoch(void) {
  uint32_t newest = 0;
  gui_sample_t* l1 = haus_history_l1();
  if (l1) {
    for (size_t i = 0; i < HAUS_L1_SAMPLES; i++) {
      if (l1[i].epoch > newest) newest = l1[i].epoch;
    }
  }
  RollupColumn* l2 = haus_history_l2();
  if (l2) {
    for (size_t i = 0; i < HAUS_L2_COLUMNS; i++) {
      const uint32_t t = l2[i].t * 60u;
      if (t > newest) newest = t;
    }
  }
  return newest;
}

bool gui_history_has_data(uint32_t start_epoch, uint32_t end_epoch) {
  gui_sample_t* l1 = haus_history_l1();
  if (l1) {
    for (size_t i = 0; i < HAUS_L1_SAMPLES; i++) {
      if (l1[i].epoch >= start_epoch && l1[i].epoch <= end_epoch) return true;
    }
  }
  RollupColumn* l2 = haus_history_l2();
  if (l2) {
    for (size_t i = 0; i < HAUS_L2_COLUMNS; i++) {
      const uint32_t t = l2[i].t * 60u;
      if (t >= start_epoch && t <= end_epoch) return true;
    }
  }
  return false;
}

const gui_sensor_info_t* gui_get_sensors(uint8_t* count) {
  /* TODO: Load sensor_roles[] from SPIFFS, resolve rank → ROM → label.
   * For now, return empty. */
  (void)count;
  return NULL;
}

const char* gui_log_dump(int* out_len) { return tpl_debug_ring_dump(out_len); }

int gui_log_length(void) { return tpl_debug_ring_length(); }

void gui_log_clear(void) { tpl_debug_ring_clear(); }

uint32_t gui_log_dropped(void) {
  return 0; /* the ring API does not expose its overwrite counter */
}

int gui_log_save(void) {
  if (!haus_sd_is_mounted()) return 0;
  int len = 0;
  const char* dump = tpl_debug_ring_dump(&len);
  if (!dump || len <= 0) return 0;
  char path[40];
  snprintf(path, sizeof(path), "/log_%lu.txt", (unsigned long)time(nullptr));
  if (!haus_sd_lock(2000)) return 0;
  haus_history_set_writing(true);
  File f = SD.open(path, FILE_WRITE);
  const bool ok =
      f && f.write((const uint8_t*)dump, (size_t)len) == (size_t)len;
  if (f) f.close();
  haus_history_set_writing(false);
  haus_sd_unlock();
  if (ok) haus_history_storage_refresh();
  return ok ? 1 : 0;
}

bool gui_can_enabled(void) { return g_can_enabled; }

/* Applying the CAN state switches a USB mux, delays, and installs a driver.
 * Do it in a worker task: if it ever wedges, the UI and web server keep
 * running instead of the whole loop task blocking. */
static volatile bool g_can_target = false;
static TaskHandle_t s_can_apply_task = NULL;

static void can_apply_task(void* arg) {
  (void)arg;
  for (int attempt = 0; attempt < 5; attempt++) {
    vTaskDelay(pdMS_TO_TICKS(500));
    /* Apply at least once even when the reported state already matches:
     * a failed start can leave the USB mux on the transceiver. */
    if (attempt > 0 && haus_can_bus_running() == g_can_target) break;
    TPL_LOGI("settings", "CAN %s (attempt %d)", g_can_target ? "on" : "off",
             attempt + 1);
    haus_can_bus_set_enabled(g_can_target);
  }
  s_can_apply_task = NULL;
  vTaskDelete(NULL);
}

const char* gui_wifi_ip(void) {
  static char addr[16] = "--";
  if (WiFi.status() != WL_CONNECTED) return "--";
  const IPAddress ip = WiFi.localIP();
  snprintf(addr, sizeof(addr), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return addr;
}

static volatile bool g_redraw_requested = false;

void gui_request_redraw(void) { g_redraw_requested = true; }

bool gui_take_redraw(void) {
  if (!g_redraw_requested) return false;
  g_redraw_requested = false;
  return true;
}

void gui_set_can_enabled(bool enabled) {
  g_can_enabled = enabled;
  g_can_target = enabled;
  if (!s_can_apply_task)
    xTaskCreate(can_apply_task, "canApply", 4096, NULL, 2, &s_can_apply_task);
}

void gui_storage_refresh(void) {
  haus_sd_space_refresh();
  haus_history_storage_refresh();
}

static bool g_backlight_on = true;

bool gui_backlight_on(void) { return g_backlight_on; }

void gui_set_backlight(bool on) {
  g_backlight_on = on;
  tpl_ws7_backlight(on);
  TPL_LOGI("settings", "backlight %s", on ? "on" : "off");
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
  CanState& can = g_can.write();
  ClimateState& climate = g_climate.write();

  /* System clock. tpl_wifi_setup() starts SNTP, so time() becomes valid
   * once the first sync has landed. Before that the UI must show --:--. */
  const time_t now = time(nullptr);
  can.timestamp = (now > 0) ? (uint32_t)now : 0;
  can.synced = (now > 1700000000); /* after 2023-11-14 */

  haus_can_snapshot_t snap;
  haus_can_bus_get(&snap);
  can.last_update_epoch = snap.last_update_epoch;

  /* Temperatures from 0x200. The slot's number is only a rank; the function
   * travels with the DS18B20 ROM announced on 0x180+r (§7.7, S9):
   *   slot → rank → ROM (expires after 90 s) → local sensor_roles[] → function
   * An expired or unbound ROM leaves the slot at SENSOR_FN_NONE, so the zone
   * renders "--" instead of a plausible number under the wrong label. */
  haus_can_temperature_t temps;
  read_temperatures(&temps);
  for (int i = 0; i < HAUS_CAN_MAX_SENSORS; i++) {
    can.temp_c100[i] = temps.temperature[i];
    can.fn_of_slot[i] = SENSOR_FN_NONE;
  }
  for (int i = 0; i < SENSOR_FN_COUNT; i++)
    can.temp_by_fn[i] = HAUS_CAN_TEMP_EMPTY;

  /* Feed the pure resolver with the learned identities and this frame's
   * temperatures. It applies the 90 s expiry, the local ROM → function table
   * and resolves each function at the newest rank only. */
  haus_sensor_rank_t ranks[HAUS_CAN_MAX_SENSORS];
  for (int r = 0; r < HAUS_CAN_MAX_SENSORS; r++) {
    ranks[r].seen = (snap.id_seen_ms[r] != 0);
    ranks[r].seen_ms = snap.id_seen_ms[r];
    memcpy(ranks[r].rom, snap.id_rom[r], 8);
    ranks[r].temp_c100 = can.temp_c100[r];
  }

  haus_sensor_role_t roles[HAUS_SENSOR_MAX_ROLES];
  uint8_t role_count = haus_sensor_roles_count();
  if (role_count > HAUS_SENSOR_MAX_ROLES) role_count = HAUS_SENSOR_MAX_ROLES;
  for (uint8_t i = 0; i < role_count; i++) {
    const haus_sensor_role_t* entry = haus_sensor_roles_at(i);
    if (entry)
      roles[i] = *entry;
    else
      roles[i].function = SENSOR_FN_NONE;
  }

  static haus_sensor_map_state_t s_map_state;
  static bool s_map_reset_done = false;
  if (!s_map_reset_done) {
    haus_sensor_map_reset(&s_map_state);
    s_map_reset_done = true;
  }

  haus_sensor_map_output_t mapped;
  haus_sensor_map_resolve(ranks, (uint32_t)millis(), roles, role_count,
                          &s_map_state, &mapped);
  for (int r = 0; r < HAUS_CAN_MAX_SENSORS; r++)
    can.fn_of_slot[r] = mapped.fn_of_slot[r];
  for (int f = 0; f < SENSOR_FN_COUNT; f++)
    can.temp_by_fn[f] = mapped.temp_by_fn[f];
  static bool s_was_bound[SENSOR_FN_COUNT] = {false, false, false, false,
                                              false};
  bool now_bound[SENSOR_FN_COUNT] = {false, false, false, false, false};
  for (int r = 0; r < HAUS_CAN_MAX_SENSORS; r++) {
    const uint8_t fn = mapped.fn_of_slot[r];
    if (fn < SENSOR_FN_COUNT && fn != SENSOR_FN_NONE) now_bound[fn] = true;
  }
  for (int f = 0; f < SENSOR_FN_COUNT; f++) {
    if (now_bound[f] && !s_was_bound[f])
      TPL_LOGI("can", "%s resolved", haus_sensor_fn_name((uint8_t)f));
    s_was_bound[f] = now_bound[f];
  }
  for (int f = 0; f < SENSOR_FN_COUNT; f++) {
    if (mapped.shifted[f]) {
      TPL_LOGW("can", "! rank shift: %s moved %u -> %u",
               haus_sensor_fn_name((uint8_t)f), (unsigned)mapped.from_rank[f],
               (unsigned)mapped.to_rank[f]);
    }
    if (mapped.expired[f]) {
      TPL_LOGW("can", "%s identity expired; zone shows --",
               haus_sensor_fn_name((uint8_t)f));
    }
  }

  /* PWM — reported-by-0x280 value; nothing received yet. */
  haus_can_pwm_t pwm;
  read_pwm(&pwm);
  can.pwm_permille = pwm.duty_permille;
  can.frequency_hz = pwm.frequency_hz;
  can.pwm_src = (uint8_t)haus_can_pwm_source(pwm.flags);
  can.active = (can.pwm_permille > 0 && can.pwm_permille < 1000);
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

  /* Living room — SHT45 (temperature + humidity); no pressure sensor. */
  int16_t living_temp = GUI_EMPTY;
  uint16_t living_hum = 0;
  uint32_t living_pres = 0;
  uint8_t climate_flags = 0;
  read_living(&living_temp, &living_hum, &living_pres, &climate_flags);
  climate.living_c100 = living_temp;
  climate.hum_c100 = living_hum;
  climate.pressure_pa = living_pres;
  climate.climate_flags = climate_flags;
  climate.last_update_epoch =
      (climate_flags & CLIMATE_VALID_SHT45) ? can.timestamp : 0;

  can.can_status = snap.running
                       ? (snap.bus_fault ? GUI_STATUS_ERROR : GUI_STATUS_OK)
                       : GUI_STATUS_INACTIVE;
  can.duty_status =
      (can.pwm_src == PWM_SRC_CAN) ? GUI_STATUS_OK : GUI_STATUS_INACTIVE;
  /* SD is green when present and readable, grey when absent. Red is reserved
   * for an in-progress write (SD status semantics, §8.15.1). */
  uint64_t total_bytes = 0;
  uint64_t used_bytes = 0;
  can.sd_low_space = false;
  if (haus_sd_space_get(&total_bytes, &used_bytes)) {
    const uint64_t free_bytes =
        total_bytes > used_bytes ? total_bytes - used_bytes : 0;
    can.sd_low_space = free_bytes < (50ULL * 1024ULL * 1024ULL);
  }

  /* Storage panel figures are cached; gui_storage_refresh() fills them on
   * demand (panel open, rollover). Never scan the FAT from the 1 Hz path. */
  snprintf(can.sd_card_type, sizeof(can.sd_card_type), "%s",
           haus_sd_card_type());
  can.sd_total_bytes = total_bytes;
  can.sd_used_bytes = used_bytes;
  haus_history_storage_get(&can.sd_samples_bytes, &can.sd_rollup_bytes,
                           &can.sd_logs_bytes);
  can.sd_last_rollover_epoch = haus_history_last_rollover_epoch();
  can.sd_fault_since_epoch = haus_history_fault_since_epoch();
  can.sd_newest_file_epoch = haus_history_newest_file_epoch();
  can.sd_writing = haus_history_writing();
  can.sd_status = !haus_sd_is_mounted()
                      ? GUI_STATUS_INACTIVE
                      : (can.sd_writing ? GUI_STATUS_ERROR : GUI_STATUS_OK);

  /* WiFi is optional after boot: the symbol is green only while associated
   * and holding a DHCP lease, grey otherwise. */
  const IPAddress ip = WiFi.localIP();
  const bool ip_valid = (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0);
  can.wifi_status = (WiFi.status() == WL_CONNECTED && ip_valid)
                        ? GUI_STATUS_OK
                        : GUI_STATUS_INACTIVE;

  can.chamber_target_c = 6000; /* 60 °C default */
  can.water_modelled = false;
  can.uptime_seconds = millis() / 1000;

  /* CAN counters and fault flags come from the RX task snapshot. */
  can.rx_frames = snap.rx_frames;
  can.tx_frames = snap.tx_frames;
  can.bus_errors = snap.bus_errors;
  can.last_frame_ms = snap.last_frame_ms;
  can.can_enabled = g_can_enabled;
  can.can_fault = snap.bus_fault ? 1 : 0;
  can.sd_fault = haus_history_sd_fault() ? 1 : 0;
  can.i2c_fault = 0;

  g_can.publish();
  g_climate.publish();
  g_published.store(true, std::memory_order_release);
}
