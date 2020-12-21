#include <string.h>

#include "template.h"
#undef ARDUINOJSON_USE_LONG_LONG
#include <Arduino.h>
#include <ArduinoOTA.h>
//#include <ArduinoJson.h>  // already included in UniversalTelegramBot.h
#include <ESP32Ping.h>
#include <UniversalTelegramBot.h>
#include <WebServer.h>
#include <WebSockets.h>
#include <WebSocketsClient.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <base64.h>
#include <mem.h>

#include "../../private_bot.h"
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

using namespace std;

extern const uint8_t *index_html_start;

//---------------------------------------------------
void setup() {
  tpl_system_setup();
  tpl_config.deepsleep_time = 1000000LL * 240;  // 4h

  // turn flash light off
  digitalWrite(tpl_flashPin, LOW);
  pinMode(tpl_flashPin, OUTPUT);
  // ensure camera module not powered up
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  pinMode(PWDN_GPIO_NUM, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(add_ws_info);
  tpl_telegram_setup(BOTtoken, CHAT_ID);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
  }

  uint8_t fail_cnt = 0;
  tpl_camera_setup(&fail_cnt, FRAMESIZE_QVGA);
  Serial.print("camera fail count=");
  Serial.println(fail_cnt);

  tpl_command = CmdSendJpg2Bot;

  Serial.println("Setup done.");
}

camera_fb_t* photo_fb = NULL;
uint32_t dataBytesSent;
bool isMoreDataAvailable() {
  if (photo_fb) {
    return (dataBytesSent < photo_fb->len);
  } else {
    return false;
  }
}
byte* getNextBuffer() {
  if (photo_fb) {
    byte* buf = &photo_fb->buf[dataBytesSent];
    dataBytesSent += 1024;
    return buf;
  } else {
    return nullptr;
  }
}

int getNextBufferLen() {
  if (photo_fb) {
    uint32_t rem = photo_fb->len - dataBytesSent;
    if (rem > 1024) {
      return 1024;
    } else {
      return rem;
    }
  } else {
    return 0;
  }
}

const unsigned long BOT_MTBS = 1000;  // mean time between scan messages
unsigned long bot_lasttime = 0;       // last time messages' scan has been done
void loop() {
  my_wifi_loop(true);

  uint32_t period = 1000;
  if (fail >= 50) {
    period = 300;
  }
  if ((millis() / period) & 1) {
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, HIGH);
  } else {
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);
  }
  server.handleClient();

  if (millis() >= bot_lasttime) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while (numNewMessages) {
      command = IDLE;
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

    bot_lasttime = millis() + BOT_MTBS;
  }

  switch (command) {
    case IDLE:
      break;
    case HEIZUNG:
      // bot.sendMessage(CHAT_ID, WiFi.SSID() + String(": ") +
      // WiFi.localIP().toString());
      status = bot.sendMessage(CHAT_ID, "Camera capture");
      if (init_camera()) {
        digitalWrite(flashPin, HIGH);
        for (uint8_t i = 0; i < 10; i++) {
          // let the camera adjust
          photo_fb = esp_camera_fb_get();
          esp_camera_fb_return(photo_fb);
          photo_fb = NULL;
        }
        photo_fb = esp_camera_fb_get();
        digitalWrite(flashPin, LOW);
        if (!photo_fb) {
          status = bot.sendMessage(CHAT_ID, "Camera capture failed");
        } else {
          dataBytesSent = 0;
          status = bot.sendPhotoByBinary(CHAT_ID, "image/jpeg", photo_fb->len,
                                         isMoreDataAvailable, nullptr,
                                         getNextBuffer, getNextBufferLen);
          esp_camera_fb_return(photo_fb);
          photo_fb = NULL;
          bot.sendMessage(
              CHAT_ID, WiFi.SSID() + String(": ") + WiFi.localIP().toString());
        }
      }
      command = DEEPSLEEP;
      break;
    case DEEPSLEEP:
      if (deepsleep) {
        // ensure LEDs and camera module off and pull up/downs set
        // appropriately
        tpl_camera_off();
        // wake up every four hours
        esp_sleep_enable_timer_wakeup(4LL * 3600LL * 1000000LL);
        esp_deep_sleep_start();
      }
      command = IDLE;
      break;
  }
}
