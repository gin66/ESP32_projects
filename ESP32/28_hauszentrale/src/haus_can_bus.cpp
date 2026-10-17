/**
 * @file haus_can_bus.cpp
 * @brief TWAI driver, RX/TX task and decoders (S17).
 *
 * RX task: 4096 B stack, priority 3, 100 ms alert timeout, re-arms
 * RX_DATA | ERR_PASS | BUS_ERROR | RX_QUEUE_FULL | BUS_RECOVERED.
 *
 * TX: exactly one 0x555 wall-clock frame per second. Nothing else.
 */

#include "haus_can_bus.h"

#include <Arduino.h>
#include <driver/twai.h>
#include <freertos/FreeRTOS.h>
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

void haus_can_bus_get(haus_can_snapshot_t *out) {
  if (!out)
    return;
  snapshot_lock();
  *out = s_snap;
  snapshot_unlock();
}

bool haus_can_bus_running(void) { return s_running; }

static void decode_frame(const twai_message_t *msg) {
  const uint32_t now_ms = (uint32_t)millis();
  const time_t now = time(nullptr);

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

static void can_task(void *arg) {
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
      if (!s_running)
        continue;
      if (alerts & TWAI_ALERT_RX_DATA) {
        twai_message_t msg;
        int drained = 0;
        while (drained++ < 64 && twai_receive(&msg, 0) == ESP_OK)
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
      if (alerts & TWAI_ALERT_BUS_RECOVERED)
        TPL_LOGI("can", "bus recovered");
    }

    /* A lone or failed node goes BUS_OFF after 32 failed transmissions. TWAI
     * does not leave that state on its own, so kick off recovery explicitly;
     * the controller then waits for 128 × 11 recessive bits before RUNNING. */
    twai_status_info_t status;
    if (twai_get_status_info(&status) == ESP_OK) {
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

    const uint32_t now_ms = (uint32_t)millis();
    if (now_ms - s_last_tx_ms >= 1000u) {
      s_last_tx_ms = now_ms;
      tx_timestamp();
    }
  }
}

bool haus_can_bus_start(void) {
  if (s_running)
    return true;

  /* Route GPIO 19/20 to the transceiver, then let the mux settle (S24.03). */
  if (tpl_ws7_set_usb_mode(false) != ESP_OK) {
    TPL_LOGE("can", "USB_SEL switch failed");
    return false;
  }
  /* Read back the cached CH422G shadow: 1 means IO5 is high and GPIO 19/20
   * are on the transceiver. */
  TPL_LOGI("can", "USB_SEL -> CAN, can_available=%d",
           tpl_ws7_can_available() ? 1 : 0);
  vTaskDelay(pdMS_TO_TICKS(200));

  twai_general_config_t g =
      TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)HAUS_CAN_TX,
                                  (gpio_num_t)HAUS_CAN_RX, TWAI_MODE_NORMAL);
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

  if (!s_task)
    xTaskCreate(can_task, "canTask", 4096, NULL, 3, &s_task);

  TPL_LOGI("can", "TWAI running at 125 kbps");
  return true;
}

void haus_can_bus_stop(void) {
  if (!s_running)
    return;
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
