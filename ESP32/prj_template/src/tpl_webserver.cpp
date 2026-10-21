#include "tpl_webserver.h"
#include "tpl_debug_log.h"

#include <ArduinoOTA.h>
#include <WiFi.h>
#include <inttypes.h>
#include <WebServer.h>

#include "tpl_command.h"
#include "tpl_system.h"
#include "tpl_wifi.h"
#include "version.h"
#ifdef IS_ESP32CAM
#include "tpl_esp_camera.h"
#endif
#include <FS.h>
#include <SPIFFS.h>
#include <string.h>

WebServer tpl_server(80);
static volatile bool s_reinit_requested = false;

void TaskWebServerCore0(void* pvParameters) {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;

  for (;;) {
    if (s_reinit_requested) {
      s_reinit_requested = false;
      TPL_LOGI("web", "WebServer: reinitializing");
      tpl_server.stop();
      tpl_server.begin();
    }
    tpl_server.handleClient();
    vTaskDelay(xDelay);
  }
}

bool tpl_fs_delete_allowed(const char* path) {
  if (!path) return false;
  const size_t n = strlen(path);
  if (n < 2 || n > 128) return false;
  if (path[0] != '/' || path[n - 1] == '/') return false;
  for (size_t i = 0; i < n; i++) {
    const unsigned char c = (unsigned char)path[i];
    if (c < 0x20 || c == '\\') return false;
  }
  /* ".." is prefixed with the mount point and can leave the volume. */
  if (strstr(path, "..") || strstr(path, "//")) return false;
  const char* base = strrchr(path, '/');
  /* A leading dot is the sentinel (/.sd_probe) and any other hidden name. */
  if (!base || base[1] == '\0' || base[1] == '.') return false;
  if (strcmp(path, SPIFFS_CONFIG_FNAME) == 0) return false;
  if (strcmp(path, "/rollup.bin") == 0) return false;
  /* The SPIFFS ring may be open in the recorder. Refusing the name means
   * this route never removes that file out from under the writer. */
  if (strcmp(path, "/rollup_ring.bin") == 0) return false;
  /* /data_YYYYMMDD.bin — the recorder's day files. */
  if (n == 18 && strncmp(path, "/data_", 6) == 0 &&
      strcmp(path + 14, ".bin") == 0) {
    bool digits = true;
    for (int i = 6; i < 14; i++) {
      if (path[i] < '0' || path[i] > '9') digits = false;
    }
    if (digits) return false;
  }
  return true;
}

void tpl_webserver_setup() {
  tpl_server.onNotFound([]() {
    tpl_server.send(404);
    TPL_LOGI("web", "Not found: %s", tpl_server.uri().c_str());
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
  tpl_server.on("/stack", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send(200, "text/html", tpl_config.stack_info);
  });
  tpl_server.on("/spiffs/list", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    String result = "{\"files\":[";
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    bool first = true;
    while (file) {
      if (!first) result += ",";
      result += "{\"name\":\"" + String(file.name()) +
                "\",\"size\":" + String(file.size()) + "}";
      first = false;
      file = root.openNextFile();
    }
    result += "],\"total\":" + String(SPIFFS.totalBytes()) +
              ",\"used\":" + String(SPIFFS.usedBytes()) + "}";
    tpl_server.send(200, "application/json", result);
  });
  tpl_server.on("/spiffs/read", HTTP_GET, []() {
    if (!tpl_server.hasArg("file")) {
      tpl_server.sendHeader("Connection", "close");
      tpl_server.send(400, "text/plain", "Missing 'file' parameter");
      return;
    }
    String filename = tpl_server.arg("file");
    if (!SPIFFS.exists(filename)) {
      tpl_server.sendHeader("Connection", "close");
      tpl_server.send(404, "text/plain", "File not found: " + filename);
      return;
    }
    File f = SPIFFS.open(filename, "r");
    String content = f.readString();
    f.close();
    tpl_server.sendHeader("Connection", "close");
    tpl_server.send(200, "text/html", content);
  });
  auto spiffs_delete = []() {
    tpl_server.sendHeader("Connection", "close");
    if (!tpl_server.hasArg("file")) {
      tpl_server.send(400, "text/plain", "Missing 'file' parameter");
      return;
    }
    String filename = tpl_server.arg("file");
    if (!tpl_fs_delete_allowed(filename.c_str())) {
      tpl_server.send(403, "text/plain", "refused");
      return;
    }
    if (!SPIFFS.exists(filename)) {
      tpl_server.send(404, "text/plain", "File not found: " + filename);
      return;
    }
    if (!SPIFFS.remove(filename)) {
      tpl_server.send(500, "application/json",
                      "{\"ok\":false,\"error\":\"delete failed\"}");
      return;
    }
    tpl_server.send(200, "application/json", "{\"ok\":true}");
  };
  tpl_server.on("/spiffs/delete", HTTP_POST, spiffs_delete);

  tpl_server.on("/cpu", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    char buf[48];
    snprintf(buf, sizeof(buf), "CPU0: %" PRIu32 "%% CPU1: %" PRIu32 "%%", cpu_load_core0,
             cpu_load_core1);
    tpl_server.send(200, "text/html", buf);
  });
  tpl_server.on("/version", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"git_hash\":\"%s\",\"git_branch\":\"%s\",\"build_time\":\"%s\","
             "\"version\":\"%s\"}",
             GIT_HASH, GIT_BRANCH, BUILD_TIME, VERSION_STRING);
    tpl_server.send(200, "application/json", buf);
  });
  tpl_server.on("/wifi", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    uint8_t* bssid = WiFi.BSSID();
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"status\":%d,"
             "\"ssid\":\"%s\","
             "\"bssid\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
             "\"channel\":%d,"
             "\"rssi\":%d,"
             "\"ip\":\"%s\","
             "\"subnet\":\"%s\","
             "\"gateway\":\"%s\","
             "\"dns\":\"%s\","
             "\"mac\":\"%s\","
              "\"tx_power\":%d,"
              "\"sleep_mode\":%d,"
              "\"rssi_roam_count\":%d,"
              "\"rssi_threshold\":%d,"
              "\"rssi_sustained_sec\":%d}",
             WiFi.status(),
             WiFi.SSID().c_str(),
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
             (int)WiFi.channel(),
             WiFi.RSSI(),
             WiFi.localIP().toString().c_str(),
             WiFi.subnetMask().toString().c_str(),
             WiFi.gatewayIP().toString().c_str(),
             WiFi.dnsIP().toString().c_str(),
              WiFi.macAddress().c_str(),
              (int)WiFi.getTxPower(),
              (int)WiFi.getSleep(),
              tpl_wifi_get_rssi_roam_count(),
              RSSI_RECONNECT_THRESHOLD,
              RSSI_RECONNECT_SUSTAINED_SEC);
    tpl_server.send(200, "application/json", buf);
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
          TPL_LOGI("web", "Update: %s\n", upload.filename.c_str());
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
            TPL_LOGI("web", "Update Success: %u\nRebooting...\n",
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
      TPL_LOGE("web", "IMAGE ERROR: %s", tpl_server.uri().c_str());
      TPL_LOGI("web", "Camera capture failed");
      tpl_server.send(404);
    }
  });
#endif
  tpl_server.serveStatic("/", SPIFFS, "/index.html", "max-age=31536000");
  tpl_server.serveStatic("/scripts.js", SPIFFS, "/scripts.js",
                         "max-age=31536000");
  tpl_server.serveStatic("/serverIndex", SPIFFS, "/serverindex.html",
                         "max-age=31536000");
  tpl_server.begin();

  tpl_wifi_register_reconnect([](unsigned long current_ms) {
    s_reinit_requested = true;
  });

  xTaskCreatePinnedToCore(TaskWebServerCore0, "WebServer", 4096, NULL, 1,
                          &tpl_tasks.task_webserver, CORE_1);
}
