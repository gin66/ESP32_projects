#include "esp_camera.h"
#include "string.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <base64.h>

#include "esp32-hal-psram.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fb_gfx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "template.h"

#define DEBUG_ESP

#ifdef DEBUG_ESP
#define DBG(x) Serial.println(x)
#else
#define DBG(...)
#endif

#define relayPin 4

static const char* TAG = "GARAGE";

using namespace std;

enum GarageCommand {
  GarageIdle,
  GarageOpen,
} command = GarageIdle;

uint32_t last_ms = 0;
uint32_t switch_off_delay = 0;

void process_garage_ws_cmd(DynamicJsonDocument* json) {
  tpl_process_web_socket_cam_settings(json);
  if (json->containsKey("garage")) {
    command = GarageOpen;
  }
  if (json->containsKey("image")) {
    uint8_t fail_cnt = 0;
    esp_err_t err = tpl_camera_setup(&fail_cnt, FRAMESIZE_QVGA);
    if (err == ESP_OK) {
      ESP_LOGE(TAG, "Total heap: %u", ESP.getHeapSize());
      ESP_LOGE(TAG, "Free heap: %u", ESP.getFreeHeap());
      ESP_LOGE(TAG, "Total PSRAM: %u", ESP.getPsramSize());
      ESP_LOGE(TAG, "Free PSRAM: %u", ESP.getFreePsram());

      camera_fb_t* fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Camera capture failed");
      } else {
        if (fb->format == PIXFORMAT_JPEG) {
          webSocket.broadcastBIN(fb->buf, fb->len);
        }
        esp_camera_fb_return(fb);
      }
    }
  }
}

void setup() {
  tpl_system_setup(0);

  digitalWrite(relayPin, LOW);
  pinMode(relayPin, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  ESP_LOGE(TAG, "Free heap: %u", xPortGetFreeHeapSize());

  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL, process_garage_ws_cmd);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  tpl_server.on("/", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send(200, "text/html", "Garage OK");
  });
  tpl_server.on("/image", HTTP_GET, []() {
    bool send_error = true;
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor != NULL) {
      sensor->set_pixformat(sensor, PIXFORMAT_JPEG);
      sensor->set_framesize(sensor, FRAMESIZE_UXGA);
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
    } else {
      if (fb->width > 100) {
        if (fb->format == PIXFORMAT_JPEG) {
          tpl_server.sendHeader("Connection", "close");
          tpl_server.send_P(200, "Content-Type: image/jpeg",
                            (const char*)fb->buf, fb->len);
          send_error = false;
        }
      }
      esp_camera_fb_return(fb);
    }
    if (send_error) {
      tpl_server.send(404);
    }
  });
  tpl_server.on("/garage", HTTP_GET, []() {
    command = GarageOpen;
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send(200, "text/html", "Garage opened");
  });

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
  }
  ESP_LOGE(TAG, "Free heap after setup: %u", xPortGetFreeHeapSize());
  ESP_LOGE(TAG, "Total heap: %u", ESP.getHeapSize());
  ESP_LOGE(TAG, "Free heap: %u", ESP.getFreeHeap());
  ESP_LOGE(TAG, "Total PSRAM: %u", ESP.getPsramSize());
  ESP_LOGE(TAG, "Free PSRAM: %u", ESP.getFreePsram());

  uint8_t fail_cnt = 0;
  tpl_camera_setup(&fail_cnt, FRAMESIZE_QVGA);

  Serial.println("Setup done.");
}

void loop() {
  switch (command) {
    case GarageIdle:
      break;
    case GarageOpen: {
      digitalWrite(relayPin, HIGH);
      switch_off_delay = 1000;
      last_ms = millis();
    } break;
  }
  command = GarageIdle;

  if (switch_off_delay > 0) {
    uint32_t curr = millis();
    uint32_t dt = curr - last_ms;
    if (switch_off_delay > dt) {
      switch_off_delay -= dt;
      last_ms = curr;
    } else {
      switch_off_delay = 0;
      digitalWrite(relayPin, LOW);
    }
  }

  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
