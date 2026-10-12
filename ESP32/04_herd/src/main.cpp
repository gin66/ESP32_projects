#include "string.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

#include "driver/adc.h"
#include "driver/dac.h"
#include "esp32-hal-psram.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "template.h"

static const char* TAG = "HERD";

#define DAC_PIN 25
#define CHANNEL_LEFT ADC1_CHANNEL_4
#define CHANNEL_MIDDLE ADC1_CHANNEL_7
#define CHANNEL_RIGHT ADC1_CHANNEL_5

#define PIN_LEFT 32
#define PIN_MIDDLE 35
#define PIN_RIGHT 33

struct val_s {
  int16_t val_last;
  int32_t val_avg;
  int32_t val_min;
  int32_t val_max;
  int32_t val_mean;
  int32_t summed[20];
  uint32_t sumcnt[20];
};
struct channel_s {
  adc1_channel_t channel;
  uint32_t ms;
  struct val_s curr;
  struct val_s val[2];
  bool is_rising;
} channel[3];

enum HerdCommand {
  HerdIdle,
} command = HerdIdle;

void publish_herd_ws(DynamicJsonDocument* json) {
  (*json)["ch_left"] = channel[0].curr.val_last;
  (*json)["ch_middle"] = channel[1].curr.val_last;
  (*json)["ch_right"] = channel[2].curr.val_last;
  (*json)["avg_left"] = channel[0].curr.val_avg;
  (*json)["avg_middle"] = channel[1].curr.val_avg;
  (*json)["avg_right"] = channel[2].curr.val_avg;
  (*json)["min_left"] = channel[0].curr.val_min;
  (*json)["min_middle"] = channel[1].curr.val_min;
  (*json)["min_right"] = channel[2].curr.val_min;
  (*json)["max_left"] = channel[0].curr.val_max;
  (*json)["max_middle"] = channel[1].curr.val_max;
  (*json)["max_right"] = channel[2].curr.val_max;
  (*json)["ch_left_0"] = channel[0].val[0].val_last;
  (*json)["ch_middle_0"] = channel[1].val[0].val_last;
  (*json)["ch_right_0"] = channel[2].val[0].val_last;
  (*json)["avg_left_0"] = channel[0].val[0].val_avg;
  (*json)["avg_middle_0"] = channel[1].val[0].val_avg;
  (*json)["avg_right_0"] = channel[2].val[0].val_avg;
  (*json)["min_left_0"] = channel[0].val[0].val_min;
  (*json)["min_middle_0"] = channel[1].val[0].val_min;
  (*json)["min_right_0"] = channel[2].val[0].val_min;
  (*json)["max_left_0"] = channel[0].val[0].val_max;
  (*json)["max_middle_0"] = channel[1].val[0].val_max;
  (*json)["max_right_0"] = channel[2].val[0].val_max;
  (*json)["ch_left_1"] = channel[0].val[1].val_last;
  (*json)["ch_middle_1"] = channel[1].val[1].val_last;
  (*json)["ch_right_1"] = channel[2].val[1].val_last;
  (*json)["avg_left_1"] = channel[0].val[1].val_avg;
  (*json)["avg_middle_1"] = channel[1].val[1].val_avg;
  (*json)["avg_right_1"] = channel[2].val[1].val_avg;
  (*json)["min_left_1"] = channel[0].val[1].val_min;
  (*json)["min_middle_1"] = channel[1].val[1].val_min;
  (*json)["min_right_1"] = channel[2].val[1].val_min;
  (*json)["max_left_1"] = channel[0].val[1].val_max;
  (*json)["max_middle_1"] = channel[1].val[1].val_max;
  (*json)["max_right_1"] = channel[2].val[1].val_max;
  (*json)["mean_left_0"] = channel[0].val[0].val_mean;
  (*json)["mean_middle_0"] = channel[1].val[0].val_mean;
  (*json)["mean_right_0"] = channel[2].val[0].val_mean;
  (*json)["mean_left_1"] = channel[0].val[1].val_mean;
  (*json)["mean_middle_1"] = channel[1].val[1].val_mean;
  (*json)["mean_right_1"] = channel[2].val[1].val_mean;

  const size_t CAPACITY = JSON_ARRAY_SIZE(20) * 2 + JSON_ARRAY_SIZE(2);
  StaticJsonDocument<CAPACITY> ls_summed;
  StaticJsonDocument<CAPACITY> ms_summed;
  StaticJsonDocument<CAPACITY> rs_summed;
  StaticJsonDocument<CAPACITY> sumcnt;
  for (int j = 0; j < 2; j++) {
    for (int k = 0; k < 20; k++) {
      ls_summed[j][k] = (long)channel[0].val[j].summed[k];
      ms_summed[j][k] = (long)channel[1].val[j].summed[k];
      rs_summed[j][k] = (long)channel[2].val[j].summed[k];
      sumcnt[j][k] = (long)channel[0].val[j].sumcnt[k];
    }
  }
  (*json)["left_summed_0"] = ls_summed[0];
  (*json)["left_summed_1"] = ls_summed[1];
  (*json)["middle_summed_0"] = ms_summed[0];
  (*json)["middle_summed_1"] = ms_summed[1];
  (*json)["right_summed_0"] = rs_summed[0];
  (*json)["right_summed_1"] = rs_summed[1];
  (*json)["sumcnt_0"] = sumcnt[0];
  (*json)["sumcnt_1"] = sumcnt[1];
}

void setup() {
  tpl_system_setup(0);

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  esp_log_level_set("*", ESP_LOG_NONE);
  ESP_LOGE(TAG, "Free heap: %u", xPortGetFreeHeapSize());

  tpl_wifi_setup(true, false, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(publish_herd_ws, NULL);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  ESP_LOGE(TAG, "Free heap after setup: %u", xPortGetFreeHeapSize());
  ESP_LOGE(TAG, "Total heap: %u", ESP.getHeapSize());
  ESP_LOGE(TAG, "Free heap: %u", ESP.getFreeHeap());
  ESP_LOGE(TAG, "Total PSRAM: %u", ESP.getPsramSize());
  ESP_LOGE(TAG, "Free PSRAM: %u", ESP.getFreePsram());

  pinMode(PIN_LEFT, OUTPUT);
  pinMode(PIN_MIDDLE, OUTPUT);
  pinMode(PIN_RIGHT, OUTPUT);
  digitalWrite(PIN_LEFT, LOW);
  digitalWrite(PIN_MIDDLE, LOW);
  digitalWrite(PIN_RIGHT, LOW);
  delay(100);

  pinMode(PIN_LEFT, INPUT);
  pinMode(PIN_MIDDLE, INPUT);
  pinMode(PIN_RIGHT, INPUT);

  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(CHANNEL_LEFT, ADC_ATTEN_DB_0);
  adc1_config_channel_atten(CHANNEL_MIDDLE, ADC_ATTEN_DB_0);
  adc1_config_channel_atten(CHANNEL_RIGHT, ADC_ATTEN_DB_0);
  dac_output_enable(DAC_CHANNEL_1);
#define VOUT 500
  dac_output_voltage(DAC_CHANNEL_1, VOUT * 256 / 3300);

  channel[0].channel = CHANNEL_LEFT;
  channel[1].channel = CHANNEL_MIDDLE;
  channel[2].channel = CHANNEL_RIGHT;
  for (int i = 0; i < 3; i++) {
    struct channel_s* ch = &channel[i];
    ch->ms = 0;
    ch->curr.val_last = 0;
    ch->curr.val_avg = 0;
    ch->curr.val_min = 0;
    ch->curr.val_max = 0;
    for (int j = 0; j < 2; j++) {
      ch->val[j] = ch->curr;
    }
  }

  Serial.println("Setup done.");
}

#define VOUT_0 550
#define VOUT_1 450
uint8_t curr_voltage = 255;

void loop() {
  uint8_t new_voltage;
  uint32_t now = millis();
  if (now & 32768) {
    new_voltage = 0;
    dac_output_voltage(DAC_CHANNEL_1, VOUT_0 * 256 / 3300);
  } else {
    new_voltage = 1;
    dac_output_voltage(DAC_CHANNEL_1, VOUT_1 * 256 / 3300);
  }
  if (new_voltage != curr_voltage) {
    if (curr_voltage != 255) {
      for (int i = 0; i < 3; i++) {
        struct channel_s* ch = &channel[i];
        int32_t mean = 0;
        for (int j = 0; j < 20; j++) {
          if (ch->curr.sumcnt[j] > 0) {
            mean += ch->curr.summed[j] / ch->curr.sumcnt[j];
          }
        }
        ch->curr.val_mean = mean;
        ch->val[curr_voltage] = ch->curr;
      }
    }
    curr_voltage = new_voltage;
    for (int i = 0; i < 3; i++) {
      struct channel_s* ch = &channel[i];
      ch->ms = now;
      ch->curr.val_avg = 0;
      ch->curr.val_min = 0;
      ch->curr.val_max = 0;
      ch->curr.val_mean = 0;
      for (int j = 0; j < 20; j++) {
        ch->curr.summed[j] = 0;
        ch->curr.sumcnt[j] = 0;
      }
    }
  }

  uint8_t index = now % 20;
  for (int i = 0; i < 3; i++) {
    struct channel_s* ch = &channel[i];
    int32_t curr_val = adc1_get_raw(ch->channel);
    ch->curr.val_last = curr_val;
    ch->curr.val_avg += (curr_val * 256 - ch->curr.val_avg) / 128;
    ch->curr.sumcnt[index]++;
    ch->curr.summed[index] += curr_val;

    if (now - ch->ms < 10000) {
      ch->curr.val_max = ch->curr.val_avg;
      ch->curr.val_min = ch->curr.val_avg;
    } else {
      if (ch->curr.val_max < ch->curr.val_avg) {
        ch->curr.val_max = ch->curr.val_avg;
      }
      if (ch->curr.val_min > ch->curr.val_avg) {
        ch->curr.val_min = ch->curr.val_avg;
      }
    }
  }

  switch (command) {
    case HerdIdle:
      break;
  }
  command = HerdIdle;

  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
