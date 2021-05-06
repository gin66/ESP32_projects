#include <string.h>

#include "template.h"
#undef ARDUINOJSON_USE_LONG_LONG
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Esp.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "../../private_bot.h"
#include "../../private_sha.h"
#include "esp32-hal-psram.h"

using namespace std;

uint32_t *p = NULL;

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

uint16_t duty = 0;

void servo_update(DynamicJsonDocument *json) {
  if (json->containsKey("servo")) {
	duty = (*json)["servo"];
  }
}

void setup() {
  tpl_system_setup(10);  // 10secs deep sleep time

  // turn flash light off
  digitalWrite(tpl_flashPin, LOW);
  pinMode(tpl_flashPin, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL, servo_update);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
  }

  ledcSetup(0, 50, 16);
  ledcAttachPin(12, 0);

  Serial.println("Done.");
}

void loop() {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);

  // 1-2 ms with 20ms => duty = 1*65536/20..2*65536/20 = 3276.8 .. 6553.6
  //
  // 500..~2700ms = 1638..8850
  ledcWrite(0, duty/9 + 1638);
  duty+=20;
}
