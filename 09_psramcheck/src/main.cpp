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
void setup() {
#define MAGIC 0xdeadbeaf
#define PROBE_SIZE (1024 * 1024)
  p = (uint32_t *)ps_malloc(PROBE_SIZE);

  tpl_system_setup(10);  // 10secs deep sleep time

  // turn flash light off
  digitalWrite(tpl_flashPin, LOW);
  pinMode(tpl_flashPin, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL, NULL);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
  }

  tpl_server.on("/init", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send_P(200, "text/html", "OK");
    for (uint32_t i = 0; i < PROBE_SIZE / 4; i++) {
      p[i] = MAGIC ^ ((i << 16) + i);
    }
  });
  tpl_server.on("/dump", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send_P(200, "Content-Type: application/octet-stream",
                      (const char *)p, PROBE_SIZE);
  });

  Serial.println("Done.");
}

void loop() {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
