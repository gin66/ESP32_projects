#include "esp_camera.h"
#include "string.h"
//#include "soc/soc.h"
//#include "soc/rtc_cntl_reg.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP32Ping.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>
#include <mem.h>

#include "esp32-hal-psram.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "template.h"

#define ledPin 33

using namespace std;

WiFiServer telnet_server;
#define BUFFER_SIZE 1024
uint8_t buff[BUFFER_SIZE];

//---------------------------------------------------
void setup() {
  tpl_system_setup(0);  // no deep sleep

  Serial.begin(115200);
  Serial.setDebugOutput(false);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL,NULL);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  telnet_server = WiFiServer(1234);
  telnet_server.begin();
}

WiFiClient telnet_client = 0;

void loop() {
  if (telnet_client) {
    size_t size;
    if (telnet_client.connected()) {
      if ((size = telnet_client.available()) != 0) {
        size = size >= BUFFER_SIZE ? BUFFER_SIZE : size;
        telnet_client.read(buff, size);
        Serial.write(buff, size);
        Serial.flush();
      }
      if ((size = Serial.available()) != 0) {
        size = size >= BUFFER_SIZE ? BUFFER_SIZE : size;
        Serial.readBytes(buff, size);
        telnet_client.write(buff, size);
        telnet_client.flush();
      }
    } else {
      telnet_client.stop();
    }
  } else {
    telnet_client = telnet_server.available();
  }
}
