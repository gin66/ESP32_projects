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

WebServer server(80);
extern const uint8_t index_html_start[] asm("_binary_src_index_html_start");
extern const uint8_t server_index_html_start[] asm(
    "_binary_src_serverindex_html_start");

bool qrMode = true;

enum Command {
  IDLE,
} command = IDLE;

//---------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);

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

  if (psramFound()) {
  }
  ESP_LOGE(TAG, "Free heap after setup: %u", xPortGetFreeHeapSize());
  ESP_LOGE(TAG, "Total heap: %u", ESP.getHeapSize());
  ESP_LOGE(TAG, "Free heap: %u", ESP.getFreeHeap());
  ESP_LOGE(TAG, "Total PSRAM: %u", ESP.getPsramSize());
  ESP_LOGE(TAG, "Free PSRAM: %u", ESP.getFreePsram());

  startNetWatchDog();

  if (MDNS.begin(HOSTNAME)) {
  }
  MDNS.addService("http", "tcp", 80);

  telnet_server = WiFiServer(1234);
  telnet_server.begin();
}

WiFiClient telnet_client = 0;

void loop() {
  my_wifi_loop(false);

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

  switch (command) {
    case IDLE:
      break;
  }
  command = IDLE;
}
