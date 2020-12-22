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
  tpl_camera_setup(&fail_cnt, FRAMESIZE_QVGA);
  Serial.print("camera fail count=");
  Serial.println(fail_cnt);

  tpl_telegram_setup(BOTtoken, CHAT_ID);

  print_info();

  tpl_config.allow_deepsleep = true;

  Serial.println("Setup done.");
}

uint32_t next_stack_info = 0;
uint32_t next_command = 0;
uint8_t cmd_i = 0;
enum Command commands[] = {
	CmdFlash,
	CmdSendJpg2Bot,
	CmdDeepSleep,
};


void loop() {
  uint32_t ms = millis();
  if ((int32_t)(ms - next_stack_info) > 0) {
    next_stack_info = ms + 2000;
    tpl_update_stack_info();
    Serial.println(tpl_config.stack_info);
  }
  if ((int32_t)(ms - next_command) > 0) {
    next_command = ms + 1000;
	if (tpl_command == CmdIdle) {
		tpl_command = commands[cmd_i++];
	}
  }
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
