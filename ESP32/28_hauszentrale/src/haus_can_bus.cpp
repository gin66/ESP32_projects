/**
 * @file haus_can_bus.cpp
 * @brief TWAI driver, RX/TX task and decoders (S17).
 *
 * RX task: 6144 B stack, priority 3, 100 ms alert timeout, re-arms
 * RX_DATA | ERR_PASS | BUS_ERROR | RX_QUEUE_FULL | BUS_RECOVERED.
 *
 * TX: exactly one 0x555 wall-clock frame per second. Nothing else.
 */

#include "haus_can_bus.h"

#include <Arduino.h>
#include <driver/twai.h>
#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>
#include <freertos/task.h>

#include <cstring>
#include <ctime>

#include "tpl_debug_log.h"
#include "tpl_ws7.h"

#ifndef HAUS_CAN_TX
#define HAUS_CAN_TX 20
#endif
#ifndef HAUS_CAN_RX
#define HAUS_CAN_RX 19
#endif

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static haus_can_snapshot_t s_snap;
static volatile bool s_running = false;
static TaskHandle_t s_task = NULL;
static uint32_t s_last_tx_ms = 0;

static void snapshot_lock(void) { portENTER_CRITICAL(&s_lock); }
static void snapshot_unlock(void) { portEXIT_CRITICAL(&s_lock); }

static void snapshot_reset(void) {
  snapshot_lock();
  memset(&s_snap, 0, sizeof(s_snap));
  for (int i = 0; i < HAUS_CAN_MAX_SENSORS; i++)
    s_snap.temp_c100[i] = HAUS_CAN_TEMP_EMPTY;
  snapshot_unlock();
}

void haus_can_bus_get(haus_can_snapshot_t* out) {
  if (!out) return;
  snapshot_lock();
  *out = s_snap;
  snapshot_unlock();
}

bool haus_can_bus_running(void) { return s_running; }

static void decode_frame(const twai_message_t* msg) {
  const uint32_t now_ms = (uint32_t)millis();
  /* time() takes the newlib lock and reads the RTC; at ~1000 frames/s that
   * is pure overhead. Once per second is enough for a wall-clock update. */
  static uint32_t last_time_ms = 0;
  static time_t now = 0;
  if (now_ms - last_time_ms >= 1000u) {
    last_time_ms = now_ms;
    now = time(nullptr);
  }

  snapshot_lock();
  s_snap.rx_frames++;
  s_snap.last_frame_ms = now_ms;
  if (now > 1700000000) {
    s_snap.timestamp = (uint32_t)now;
    s_snap.last_update_epoch = (uint32_t)now;
  }

  if (msg->identifier == CAN_ID_DATA_BASE &&
      msg->data_length_code >= sizeof(haus_can_temperature_t)) {
    haus_can_temperature_t temps;
    haus_can_get_temperature(msg->data, &temps);
    memcpy(s_snap.temp_c100, temps.temperature, sizeof(temps.temperature));
    s_snap.valid = true;
  } else if (msg->identifier >= CAN_ID_ID_BASE &&
             msg->identifier < CAN_ID_ID_BASE + 4 &&
             msg->data_length_code >= 8) {
    const uint8_t rank = (uint8_t)(msg->identifier - CAN_ID_ID_BASE);
    haus_can_identity_t ident;
    haus_can_get_identity(msg->data, &ident);
    memcpy(s_snap.id_rom[rank], ident.rom, 8);
    s_snap.id_seen_ms[rank] = now_ms;
    if (rank >= s_snap.id_count) s_snap.id_count = (uint8_t)(rank + 1);
    snapshot_unlock();
    TPL_LOGI("can", "identity rank %u ROM %02X%02X%02X%02X%02X%02X%02X%02X",
             (unsigned)rank, ident.rom[0], ident.rom[1], ident.rom[2],
             ident.rom[3], ident.rom[4], ident.rom[5], ident.rom[6],
             ident.rom[7]);
    snapshot_lock();
  } else if (msg->identifier == CAN_ID_PWM_REPORT &&
             msg->data_length_code >= sizeof(haus_can_pwm_t)) {
    haus_can_pwm_t pwm;
    haus_can_get_pwm(msg->data, &pwm);
    s_snap.pwm_permille = pwm.duty_permille;
    s_snap.frequency_hz = pwm.frequency_hz;
    s_snap.pwm_flags = pwm.flags;
  }

  /* A good frame clears the sticky bus fault (S17.05). */
  s_snap.bus_fault = false;
  snapshot_unlock();
}

static void tx_timestamp(void) {
  const time_t now = time(nullptr);
  haus_can_timestamp_t ts = {0, 0, 0, 0};
  if (now >= 1700000000) {
    struct tm tm;
    if (localtime_r(&now, &tm)) {
      ts.seconds = (uint8_t)tm.tm_sec;
      ts.minutes = (uint8_t)tm.tm_min;
      ts.hours = (uint8_t)tm.tm_hour;
      ts.weekday = (uint8_t)tm.tm_wday;
    }
  }
  /* Before the first SNTP sync the frame still goes out with a zero clock:
   * the bus needs an ACK source, and a quiet controller is indistinguishable
   * from a broken one during bring-up. */

  twai_message_t msg;
  memset(&msg, 0, sizeof(msg));
  msg.identifier = CAN_ID_TIMESTAMP;
  msg.data_length_code = 8;
  msg.flags = TWAI_MSG_FLAG_NONE;
  haus_can_set_timestamp(msg.data, &ts);

  const esp_err_t err = twai_transmit(&msg, 0);
  snapshot_lock();
  if (err == ESP_OK) {
    s_snap.tx_frames++;
  } else {
    s_snap.bus_errors++;
    s_snap.bus_fault = true;
    snapshot_unlock();
    static uint32_t last_err_log_ms = 0;
    if ((uint32_t)millis() - last_err_log_ms > 5000u) {
      last_err_log_ms = (uint32_t)millis();
      TPL_LOGW("can", "0x555 TX failed: %s", esp_err_to_name(err));
    }
    return;
  }
  snapshot_unlock();
}

static void can_task(void* arg) {
  (void)arg;
  TPL_LOGI("can", "canTask started (RX %d / TX %d, 125 kbps)", HAUS_CAN_RX,
           HAUS_CAN_TX);

  for (;;) {
    if (!s_running) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, pdMS_TO_TICKS(100)) == ESP_OK) {
      if (!s_running) continue;
      if (alerts & TWAI_ALERT_RX_DATA) {
        twai_message_t msg;
        int drained = 0;
        while (drained++ < 32 && twai_receive(&msg, 0) == ESP_OK)
          decode_frame(&msg);
      }
      if (alerts & (TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR)) {
        snapshot_lock();
        s_snap.bus_errors++;
        s_snap.bus_fault = true;
        snapshot_unlock();
      }
      if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
        snapshot_lock();
        s_snap.bus_errors++;
        snapshot_unlock();
        TPL_LOGW("can", "RX queue full");
      }
      if (alerts & TWAI_ALERT_BUS_RECOVERED) TPL_LOGI("can", "bus recovered");
    }

    /* A lone or failed node goes BUS_OFF after 32 failed transmissions. TWAI
     * does not leave that state on its own, so kick off recovery explicitly;
     * the controller then waits for 128 × 11 recessive bits before RUNNING.
     * Poll at 10 Hz, not once per drained frame: under a flood the loop runs
     * ~1000 times/s and the register read competes with the RGB DMA. */
    static uint32_t last_status_ms = 0;
    const uint32_t status_now_ms = (uint32_t)millis();
    twai_status_info_t status;
    if (status_now_ms - last_status_ms >= 100u &&
        twai_get_status_info(&status) == ESP_OK) {
      last_status_ms = status_now_ms;
      snapshot_lock();
      const uint8_t prev = s_snap.twai_state;
      s_snap.twai_state = (uint8_t)status.state;
      s_snap.tx_error_counter = status.tx_error_counter;
      s_snap.rx_error_counter = status.rx_error_counter;
      snapshot_unlock();
      if (status.state != prev)
        TPL_LOGI("can", "TWAI state %u -> %u (TEC %u, REC %u)", (unsigned)prev,
                 (unsigned)status.state, (unsigned)status.tx_error_counter,
                 (unsigned)status.rx_error_counter);
      if (status.state == TWAI_STATE_BUS_OFF) {
        TPL_LOGW("can", "bus-off; initiating recovery");
        twai_initiate_recovery();
      }
    }

    const uint32_t now_ms = status_now_ms;
    if (now_ms - s_last_tx_ms >= 1000u) {
      s_last_tx_ms = now_ms;
      tx_timestamp();
    }

    /* Under a frame flood the alert is pending again immediately, so this
     * loop never blocks. Without a yield the idle task starves, the task
     * watchdog reboots the panel, and the RGB DMA underruns (white stripe
     * tearing). One tick per pass is ~1000 passes/s, far above the 960
     * frames/s the 125 kbps bus can carry. */
    vTaskDelay(1);
  }
}

void haus_can_bus_init(void) {
  /* The RX/TX task is created by haus_can_bus_start(), i.e. only when CAN is
   * actually enabled. With CAN off it used to cost 6 KB of internal RAM plus
   * its TCB on every boot (docs/memory_report.md, S28.33). */
}

static bool can_task_create(void) {
  if (s_task) return true;
  const BaseType_t created =
      xTaskCreate(can_task, "canTask", 4096, NULL, 3, &s_task);
  if (created != pdPASS) {
    s_task = NULL;
    TPL_LOGE("can", "canTask create failed (%d), heap %u B", (int)created,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return false;
  }
  TPL_LOGI("can", "canTask created, heap %u B",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  return true;
}

bool haus_can_bus_start(void) {
  if (s_running) return true;

  /* S24.03: give the mux and the USB PHY 200 ms to settle before handing
   * GPIO 19/20 to the transceiver. */
  TPL_LOGI("can", "mux settle 200 ms before CAN");
  vTaskDelay(pdMS_TO_TICKS(200));

  /* Route GPIO 19/20 to the transceiver. */
  if (tpl_ws7_set_usb_mode(false) != ESP_OK) {
    TPL_LOGE("can", "USB_SEL switch failed");
    return false;
  }
  /* Read back the cached CH422G shadow: 1 means IO5 is high and GPIO 19/20
   * are on the transceiver. */
  TPL_LOGI("can", "USB_SEL -> CAN, can_available=%d",
           tpl_ws7_can_available() ? 1 : 0);
  vTaskDelay(pdMS_TO_TICKS(200));

  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)HAUS_CAN_TX, (gpio_num_t)HAUS_CAN_RX, TWAI_MODE_NORMAL);
  twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t err = twai_driver_install(&g, &t, &f);
  if (err != ESP_OK) {
    TPL_LOGE("can", "driver install failed: %s", esp_err_to_name(err));
    tpl_ws7_set_usb_mode(true);
    return false;
  }
  err = twai_start();
  if (err != ESP_OK) {
    TPL_LOGE("can", "twai_start failed: %s", esp_err_to_name(err));
    twai_driver_uninstall();
    tpl_ws7_set_usb_mode(true);
    return false;
  }

  const uint32_t wanted = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS |
                          TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL |
                          TWAI_ALERT_BUS_RECOVERED;
  uint32_t configured = 0; /* must be non-NULL (S17.04) */
  twai_reconfigure_alerts(wanted, &configured);

  snapshot_reset();
  {
    snapshot_lock();
    s_snap.running = true;
    snapshot_unlock();
  }
  s_running = true;

  if (!can_task_create())
  if (!s_task) {
    twai_stop();
    twai_driver_uninstall();
    s_running = false;
    snapshot_lock();
    s_snap.running = false;
    snapshot_unlock();
    tpl_ws7_set_usb_mode(true);
    return false;
  }

  TPL_LOGI("can", "TWAI running at 125 kbps (task %p, heap %u B)",
           (void*)s_task,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  return true;
}

void haus_can_bus_stop(void) {
  if (!s_running) {
    /* Even if TWAI never started, a failed start attempt may have left the
     * mux on the transceiver. Off means USB gets GPIO 19/20 back. */
    tpl_ws7_set_usb_mode(true);
    return;
  }
  s_running = false;
  vTaskDelay(pdMS_TO_TICKS(150)); /* let the task leave twai_read_alerts */

  twai_stop();
  twai_driver_uninstall();

  snapshot_lock();
  s_snap.running = false;
  s_snap.bus_fault = false;
  s_snap.valid = false;
  snapshot_unlock();

  tpl_ws7_set_usb_mode(true);
  TPL_LOGI("can", "TWAI stopped; USB restored (can_available=%d)",
           tpl_ws7_can_available() ? 1 : 0);
}

void haus_can_bus_set_enabled(bool enabled) {
  if (enabled)
    haus_can_bus_start();
  else
    haus_can_bus_stop();
}
