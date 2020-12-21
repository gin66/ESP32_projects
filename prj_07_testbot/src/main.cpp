#include "string.h"
//#include "soc/soc.h"
//#include "soc/rtc_cntl_reg.h"

#include "template.h"
#undef ARDUINOJSON_USE_LONG_LONG
#include <Arduino.h>
#include <ArduinoOTA.h>
//#include <ArduinoJson.h>  // already included in UniversalTelegramBot.h
#include <ESP32Ping.h>
#include <WebServer.h>
#include <WebSockets.h>
#include <WebSocketsClient.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <base64.h>
#include <esp_wifi.h>
#include <mem.h>

#include "../../private_bot.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp32-hal-psram.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fb_gfx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"

#define DEBUG_ESP

#ifdef DEBUG_ESP
#define DBG(x) Serial.println(x)
#else
#define DBG(...)
#endif

#define ledPin ((gpio_num_t)2)
//#define ledPin ((gpio_num_t)33)
//#define flashPin ((gpio_num_t)4)

using namespace std;

RTC_DATA_ATTR uint16_t bootCount = 0;

//---------------------------------------------------
void setup() {
  bootCount++;

#ifdef DEBUG_ESP
  Serial.begin(115200);
  Serial.setDebugOutput(true);
#endif
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  ESP_LOGE(TAG, "Free heap: %u", xPortGetFreeHeapSize());

  // Wait OTA
  tpl_wifi_setup(true, true, ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL);
  tpl_telegram_setup(Heizung_BOTtoken, CHAT_ID);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  Serial.println("Setup done.");
}

void loop() {
  Serial.print("Stackfree: loop=");
  Serial.print(uxTaskGetStackHighWaterMark(NULL));
  Serial.print(" net_watchdog=");
  Serial.print(uxTaskGetStackHighWaterMark(tpl_tasks.task_net_watchdog));
  Serial.print(" websocket=");
  Serial.print(uxTaskGetStackHighWaterMark(tpl_tasks.task_websocket));
  Serial.print(" command=");
  Serial.print(uxTaskGetStackHighWaterMark(tpl_tasks.task_command));
  Serial.print(" wifi=");
  Serial.print(uxTaskGetStackHighWaterMark(tpl_tasks.task_wifi_manager));
  Serial.print(" http=");
  Serial.print(uxTaskGetStackHighWaterMark(tpl_tasks.task_webserver));
  Serial.print(" bot=");
  Serial.println(uxTaskGetStackHighWaterMark(tpl_tasks.task_telegram));

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
