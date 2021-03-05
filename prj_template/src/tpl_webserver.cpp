#include "tpl_webserver.h"

#include <ArduinoOTA.h>
#include <WebServer.h>

#include "tpl_command.h"
#include "tpl_system.h"
#ifdef IS_ESP32CAM
#include "tpl_esp_camera.h"
#endif
#include <SPIFFS.h>
#include <FS.h>

WebServer tpl_server(80);

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
  tpl_server.on("/allowsleep", HTTP_GET, []() {
    tpl_config.allow_deepsleep = true;
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send(200, "text/html", "Done");
  });
  tpl_server.on("/restart", HTTP_GET, []() { ESP.restart(); });
  tpl_server.on("/deepsleep", HTTP_GET, []() {
    tpl_config.allow_deepsleep = true;
    tpl_command = CmdDeepSleep;
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send(200, "text/html", "Done");
  });
  tpl_server.on("/nosleep", HTTP_GET, []() {
    tpl_config.allow_deepsleep = false;
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send(200, "text/html", "Done");
  });
  tpl_server.on("/digital", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    String data = "................................................";
    for (int i = 0; i < 48; i++) {
      char ch = 48 + digitalRead(i);
      data.setCharAt(i, ch);
    }
    tpl_server.send(200, "text/html", data);
  });
  tpl_server.on("/digitalwrite", HTTP_GET, []() {
    // http://esp/digitalwrite?pin=1&value=0
    tpl_server.sendHeader("Connection", "close");
    long pin = tpl_server.arg("pin").toInt();
    long val = tpl_server.arg("value").toInt();
    pinMode(pin, OUTPUT);
    digitalWrite(pin, val);
    val = digitalRead(pin);
    tpl_server.send(200, "text/html", String(val));
  });
  tpl_server.on("/analog", HTTP_GET, []() {
    // http://esp/analog?pin=1
    tpl_server.sendHeader("Connection", "close");
    long pin = tpl_server.arg("pin").toInt();
    long val = analogRead(pin);
    tpl_server.send(200, "text/html", String(val));
  });
  tpl_server.on("/watch", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    long val = tpl_config.watchpoint;
    tpl_server.send(200, "text/html", String(val));
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
#ifdef IS_ESP32CAM
  tpl_server.on("/image", HTTP_GET, []() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      tpl_server.sendHeader("Connection", "close");
      tpl_server.send_P(200, "Content-Type: image/jpeg", (const char*)fb->buf,
                        fb->len);
      esp_camera_fb_return(fb);
    } else {
      Serial.print("IMAGE ERROR: ");
      Serial.println(tpl_server.uri());
      Serial.println("Camera capture failed");
      tpl_server.send(404);
    }
  });
#endif
  tpl_server.serveStatic("/", SPIFFS,"/index.html","max-age=31536000");
  tpl_server.serveStatic("/serverIndex", SPIFFS,"/serverindex.html","max-age=31536000");
  tpl_server.begin();

  xTaskCreatePinnedToCore(TaskWebServerCore0, "WebServer", 3072, NULL, 1,
                          &tpl_tasks.task_webserver, CORE_1);
}
