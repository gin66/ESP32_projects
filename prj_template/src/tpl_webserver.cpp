#include "tpl_webserver.h"

#include <ArduinoOTA.h>
#include <WebServer.h>

#include "tpl_system.h"

WebServer tpl_server(80);

extern const uint8_t index_html_start[] asm("_binary_src_index_html_start");
extern const uint8_t server_index_html_start[] asm(
    "_binary_src_serverindex_html_start");

void TaskWebServerCore0(void* pvParameters) {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;

  for (;;) {
    tpl_server.handleClient();
    vTaskDelay(xDelay);
  }
}

void tpl_webserver_setup() {
  tpl_server.onNotFound([]() {
    tpl_server.send(404);
    Serial.print("Not found: ");
    Serial.println(tpl_server.uri());
  });
  /*handling uploading firmware file */
  tpl_server.on("/", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send_P(200, "text/html", (const char*)index_html_start);
  });
  tpl_server.on("/deepsleep", HTTP_GET, []() {
    tpl_config.allow_deepsleep = true;
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send_P(200, "text/html", (const char*)index_html_start);
  });
  tpl_server.on("/nosleep", HTTP_GET, []() {
    tpl_config.allow_deepsleep = false;
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send_P(200, "text/html", (const char*)index_html_start);
  });
  tpl_server.on("/serverIndex", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send_P(200, "text/html", (const char*)server_index_html_start);
  });
  /*handling uploading firmware file */
  tpl_server.on(
      "/update", HTTP_POST,
      []() {
        tpl_server.sendHeader("Connection", "close");
        tpl_server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        ESP.restart();
      },
      []() {
        HTTPUpload& upload = tpl_server.upload();
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
  tpl_server.begin();

  xTaskCreatePinnedToCore(TaskWebServerCore0, "WebServer", 3072, NULL, 1,
                          &tpl_tasks.task_webserver, CORE_1);
}