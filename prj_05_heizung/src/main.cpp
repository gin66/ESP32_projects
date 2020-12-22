#include <string.h>

#include "template.h"
#undef ARDUINOJSON_USE_LONG_LONG
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "../../private_bot.h"
#include "../../private_sha.h"
#include "esp32-hal-psram.h"

using namespace std;

//---------------------------------------------------
void print_info() {
  Serial.print("Total heap: ");
  Serial.print(ESP.getHeapSize());
  Serial.print(" Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.print(" Total PSRAM: ");
  Serial.print(ESP.getPsramSize());
  Serial.print(" Free PSRAM: ");
  Serial.println(ESP.getFreePsram());
}
void setup() {
  tpl_system_setup();
  tpl_config.deepsleep_time = 1000000LL * 3600LL * 4LL;  // 4 h
  tpl_config.allow_deepsleep = true;
  // turn flash light off
  digitalWrite(tpl_flashPin, LOW);
  pinMode(tpl_flashPin, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
  }

  uint8_t fail_cnt = 0;
  tpl_camera_setup(&fail_cnt, FRAMESIZE_VGA);
  Serial.print("camera fail count=");
  Serial.println(fail_cnt);

  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;

  // turn on flash
  digitalWrite(tpl_flashPin, HIGH);
  // activate cam
  {
    uint32_t settle_till = millis() + 4000;
    while ((int32_t)(settle_till - millis()) > 0) {
      // let the camera adjust
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb) {
        esp_camera_fb_return(fb);
      }
      vTaskDelay(xDelay);
    }
    {
      // take picture
      camera_fb_t *fb = esp_camera_fb_get();
      // flash off
      digitalWrite(tpl_flashPin, LOW);
      if (fb) {
        tpl_config.curr_jpg_len = fb->len;
        tpl_config.curr_jpg = (uint8_t *)ps_malloc(fb->len);
        if (tpl_config.curr_jpg) {
          memcpy(tpl_config.curr_jpg, fb->buf, fb->len);
        }
        esp_camera_fb_return(fb);
      }
    }
  }
  // Free memory for bot
  esp_camera_deinit();
  if (tpl_config.curr_jpg != NULL) {
    tpl_telegram_setup(BOTtoken, CHAT_ID);
    tpl_command = CmdSendJpg2Bot;
  }
  while (tpl_command != CmdIdle) {
    vTaskDelay(xDelay);
  }

  print_info();
  Serial.println("Done.");
  // enter deep sleep
  tpl_command = CmdDeepSleep;
}

void loop() {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
