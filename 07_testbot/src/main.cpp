#include "template.h"
#undef ARDUINOJSON_USE_LONG_LONG
#include <Arduino.h>
// #include <ArduinoOTA.h>
// #include <ArduinoJson.h>  // already included in UniversalTelegramBot.h
// #include <ESP32Ping.h>
// #include <WebServer.h>
// #include <WebSockets.h>
// #include <WebSocketsClient.h>
// #include <WebSocketsServer.h>
// #include <WiFi.h>
// #include <WiFiClient.h>
// #include <base64.h>
// #include <esp_wifi.h>
// #include <mem.h>

#include "../../private_bot.h"
// #include "driver/gpio.h"
// #include "driver/rtc_io.h"
// #include "esp32-hal-psram.h"
// #include "esp_log.h"
// #include "esp_timer.h"
// #include "fb_gfx.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "img_converters.h"

#define ledPin ((gpio_num_t)2)

using namespace std;

//---------------------------------------------------
void setup() {
  tpl_system_setup();

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Wait OTA
  tpl_wifi_setup(true, true, ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL, NULL);
  tpl_telegram_setup(BOTtoken, CHAT_ID);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  Serial.println("Setup done.");
}

uint32_t next_stack_info = 0;

void loop() {
  uint32_t ms = millis();
  if ((int32_t)(ms - next_stack_info) > 0) {
    next_stack_info = ms + 500;
    tpl_update_stack_info();
    Serial.println(tpl_config.stack_info);
  }

  uint32_t period = 1000;
  if (tpl_fail >= 50) {
    period = 300;
  }
  if ((millis() / period) & 1) {
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, HIGH);
  } else {
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);
  }
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
