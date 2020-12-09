#include "esp_camera.h"
#include "string.h"
//#include "soc/soc.h"
//#include "soc/rtc_cntl_reg.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Arduino_JSON.h>
#include <ESP32Ping.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <base64.h>
#include <mem.h>

#include "driver/adc.h"
#include "driver/dac.h"
#include "esp32-hal-psram.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "template.h"

using namespace std;

#define DAC_PIN 25 /* DAC_CHANNEL_1 */
#define CHANNEL_LEFT ADC1_CHANNEL_4
#define CHANNEL_MIDDLE ADC1_CHANNEL_7
#define CHANNEL_RIGHT ADC1_CHANNEL_5

#define PIN_LEFT 32  // without pullup
#define PIN_MIDDLE 35
#define PIN_RIGHT 33  // without pullup

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

WebServer server(80);
extern const uint8_t index_html_start[] asm("_binary_src_index_html_start");
extern const uint8_t server_index_html_start[] asm(
    "_binary_src_serverindex_html_start");

bool qrMode = true;

enum Command {
  IDLE,
} command = IDLE;

WebSocketsServer webSocket = WebSocketsServer(81);

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload,
                    size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.print("websocket disconnected: ");
      Serial.println(num);
      break;
    case WStype_CONNECTED:
      Serial.print("websocket connected: ");
      Serial.println(num);
      break;
    case WStype_TEXT: {
      Serial.print("received from websocket: ");
      Serial.println((char*)payload);
      JSONVar json = JSONVar::parse((char*)payload);
      if (json.hasOwnProperty("garage")) {
      }
    } break;
    case WStype_BIN:
    case WStype_ERROR:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_PING:
    case WStype_PONG:
      break;
  }
}
void TaskWebSocket(void* pvParameters) {
  const TickType_t xDelay = 1 + 10 / portTICK_PERIOD_MS;
  uint32_t send_status_ms = 0;

  Serial.println("WebSocket Task started");
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  for (;;) {
    uint32_t now = millis();
    webSocket.loop();

    if (now > send_status_ms) {
      send_status_ms = now + 100;
      String data = "........................................";
      for (int i = 0; i < 40; i++) {
        // char ch = 48 + digitalRead(i);
        // data.setCharAt(i, ch);
      }
      JSONVar myObject;
      myObject["millis"] = millis();
      myObject["mem_free"] = (long)ESP.getFreeHeap();
      myObject["stack_free"] = (long)uxTaskGetStackHighWaterMark(NULL);
      // myObject["time"] = formattedTime;
      // myObject["b64"] = base64::encode((uint8_t*)data_buf, data_idx);
      myObject["ch_left"] = channel[0].curr.val_last;
      myObject["ch_middle"] = channel[1].curr.val_last;
      myObject["ch_right"] = channel[2].curr.val_last;
      myObject["avg_left"] = channel[0].curr.val_avg;
      myObject["avg_middle"] = channel[1].curr.val_avg;
      myObject["avg_right"] = channel[2].curr.val_avg;
      myObject["min_left"] = channel[0].curr.val_min;
      myObject["min_middle"] = channel[1].curr.val_min;
      myObject["min_right"] = channel[2].curr.val_min;
      myObject["max_left"] = channel[0].curr.val_max;
      myObject["max_middle"] = channel[1].curr.val_max;
      myObject["max_right"] = channel[2].curr.val_max;
      myObject["ch_left_0"] = channel[0].val[0].val_last;
      myObject["ch_middle_0"] = channel[1].val[0].val_last;
      myObject["ch_right_0"] = channel[2].val[0].val_last;
      myObject["avg_left_0"] = channel[0].val[0].val_avg;
      myObject["avg_middle_0"] = channel[1].val[0].val_avg;
      myObject["avg_right_0"] = channel[2].val[0].val_avg;
      myObject["min_left_0"] = channel[0].val[0].val_min;
      myObject["min_middle_0"] = channel[1].val[0].val_min;
      myObject["min_right_0"] = channel[2].val[0].val_min;
      myObject["max_left_0"] = channel[0].val[0].val_max;
      myObject["max_middle_0"] = channel[1].val[0].val_max;
      myObject["max_right_0"] = channel[2].val[0].val_max;
      myObject["ch_left_1"] = channel[0].val[1].val_last;
      myObject["ch_middle_1"] = channel[1].val[1].val_last;
      myObject["ch_right_1"] = channel[2].val[1].val_last;
      myObject["avg_left_1"] = channel[0].val[1].val_avg;
      myObject["avg_middle_1"] = channel[1].val[1].val_avg;
      myObject["avg_right_1"] = channel[2].val[1].val_avg;
      myObject["min_left_1"] = channel[0].val[1].val_min;
      myObject["min_middle_1"] = channel[1].val[1].val_min;
      myObject["min_right_1"] = channel[2].val[1].val_min;
      myObject["max_left_1"] = channel[0].val[1].val_max;
      myObject["max_middle_1"] = channel[1].val[1].val_max;
      myObject["max_right_1"] = channel[2].val[1].val_max;

      myObject["mean_left_0"] = channel[0].val[0].val_mean;
      myObject["mean_middle_0"] = channel[1].val[0].val_mean;
      myObject["mean_right_0"] = channel[2].val[0].val_mean;
      myObject["mean_left_1"] = channel[0].val[1].val_mean;
      myObject["mean_middle_1"] = channel[1].val[1].val_mean;
      myObject["mean_right_1"] = channel[2].val[1].val_mean;

      JSONVar ls_summed[2];
      JSONVar ms_summed[2];
      JSONVar rs_summed[2];
      JSONVar sumcnt[2];
      for (int j = 0; j < 2; j++) {
        for (int k = 0; k < 20; k++) {
          ls_summed[j][k] = (long)channel[0].val[j].summed[k];
          ms_summed[j][k] = (long)channel[1].val[j].summed[k];
          rs_summed[j][k] = (long)channel[2].val[j].summed[k];
          sumcnt[j][k] =
              (long)channel[0].val[j].sumcnt[k];  // any channel is ok
        }
      }
      myObject["left_summed_0"] = ls_summed[0];
      myObject["left_summed_1"] = ls_summed[1];
      myObject["middle_summed_0"] = ms_summed[0];
      myObject["middle_summed_1"] = ms_summed[1];
      myObject["right_summed_0"] = rs_summed[0];
      myObject["right_summed_1"] = rs_summed[1];
      myObject["sumcnt_0"] = sumcnt[0];
      myObject["sumcnt_1"] = sumcnt[1];
      // myObject["button_digital"] = digitalRead(BUTTON_PIN);
      myObject["digital"] = data;
      // myObject["sample_rate"] = I2S_SAMPLE_RATE;
      myObject["wifi_dBm"] = WiFi.RSSI();
      myObject["SSID"] = WiFi.SSID();

      String as_json = JSONVar::stringify(myObject);
      webSocket.broadcastTXT(as_json);
    }
    vTaskDelay(xDelay);
  }
}

//---------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  esp_log_level_set("*", ESP_LOG_NONE);
  ESP_LOGE(TAG, "Free heap: %u", xPortGetFreeHeapSize());

  my_wifi_setup(false);

  server.onNotFound([]() { server.send(404); });
  /*handling uploading firmware file */
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send_P(200, "text/html", (const char*)index_html_start);
  });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send_P(200, "text/html", (const char*)server_index_html_start);
  });
  /*handling uploading firmware file */
  server.on(
      "/update", HTTP_POST,
      []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        ESP.restart();
      },
      []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          Serial.printf("Update: %s\n", upload.filename.c_str());
          if (!Update.begin(
                  UPDATE_SIZE_UNKNOWN)) {  // start with max available size
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          /* flashing firmware to ESP*/
          if (Update.write(upload.buf, upload.currentSize) !=
              upload.currentSize) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (Update.end(
                  true)) {  // true to set the size to the current progress
            Serial.printf("Update Success: %u\nRebooting...\n",
                          upload.totalSize);
          } else {
            Update.printError(Serial);
          }
        }
      });
  server.begin();

  ESP_LOGE(TAG, "Free heap after setup: %u", xPortGetFreeHeapSize());
  ESP_LOGE(TAG, "Total heap: %u", ESP.getHeapSize());
  ESP_LOGE(TAG, "Free heap: %u", ESP.getFreeHeap());
  ESP_LOGE(TAG, "Total PSRAM: %u", ESP.getPsramSize());
  ESP_LOGE(TAG, "Free PSRAM: %u", ESP.getFreePsram());

  BaseType_t rc;

  rc = xTaskCreatePinnedToCore(TaskWebSocket, "WebSocket", 81920, (void*)1, 1,
                               NULL, 0);
  if (rc != pdPASS) {
    Serial.print("cannot start websocket task=");
    Serial.println(rc);
  }

  startNetWatchDog();

  if (MDNS.begin(HOSTNAME)) {
    Serial.println("MDNS responder started");
  }
  MDNS.addService("http", "tcp", 80);

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
#define VOUT 500 /* mV */
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
}

#define VOUT_0 550 /* mV */
#define VOUT_1 450 /* mV */
uint8_t curr_voltage = 255;

void loop() {
  my_wifi_loop(false);
  //  uint32_t period = 1000;
  //  if (fail >= 50) {
  //    period = 300;
  //  }
  server.handleClient();

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

  uint8_t index = now % 20;  // 20 ms for 50 Hz
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
    case IDLE:
      break;
  }
  command = IDLE;
}
