#include "esp_camera.h"
#include "string.h"
//#include "soc/soc.h"
//#include "soc/rtc_cntl_reg.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <base64.h>
#include <mem.h>

#include "esp32-hal-psram.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fb_gfx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "qrcode_recognize.h"
#include "quirc.h"
#include "template.h"

#define DEBUG_ESP

#ifdef DEBUG_ESP
#define DBG(x) Serial.println(x)
#else
#define DBG(...)
#endif

using namespace std;

WebServer server(80);
extern const uint8_t index_html_start[] asm("_binary_src_index_html_start");
extern const uint8_t server_index_html_start[] asm(
    "_binary_src_serverindex_html_start");

bool qrMode = true;
uint8_t* raw_image = NULL;
uint8_t* gray_image = NULL;

#define MAX_CODES 4
struct quirc_code qr_codes[MAX_CODES];
int qr_identify(uint16_t width, uint16_t height) {
  int id_count = 0;
  struct quirc* qr_recognizer = quirc_new();
  if (!qr_recognizer) {
    ESP_LOGE(TAG, "Can't create quirc object");
  } else {
    if (quirc_resize(qr_recognizer, width, height) < 0) {
      ESP_LOGE(TAG,
               "Resize the QR-code recognizer err (cannot allocate memory).");
    } else {
      int w, h;
      uint8_t* image = quirc_begin(qr_recognizer, &w, &h);
      if (image) {
        memcpy(image, gray_image, w * h);
        quirc_end(qr_recognizer);

        // Return the number of QR-codes identified in the last processed
        // image.
        id_count = quirc_count(qr_recognizer);
        ESP_LOGE(TAG, "qr_codes %d", id_count);
        if (id_count > MAX_CODES) {
          id_count = MAX_CODES;
        }
        for (int i = 0; i < id_count; i++) {
          quirc_extract(qr_recognizer, i, &qr_codes[i]);
          ESP_LOGE(TAG, "%d/%d %d/%d %d/%d %d/%d", qr_codes[i].corners[0].x,
                   qr_codes[i].corners[0].y, qr_codes[i].corners[1].x,
                   qr_codes[i].corners[1].y, qr_codes[i].corners[2].x,
                   qr_codes[i].corners[2].y, qr_codes[i].corners[3].x,
                   qr_codes[i].corners[3].y);
        }
      } else {
        ESP_LOGE(TAG, "no image, dimension %d*%d != %d*%d", w, h, 
                 width, height);
      }
    }
    ESP_LOGE(TAG, "Deconstruct QR-Code recognizer(quirc)");
    quirc_destroy(qr_recognizer);
  }
  return id_count;
}

WebSocketsServer webSocket = WebSocketsServer(81);

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload,
                    size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.print("websocket disconnected: ");
      Serial.println(num);
      break;
    case WStype_CONNECTED:
      Serial.print("websocket connected: ");
      Serial.println(num);
      break;
    case WStype_TEXT: {
      Serial.print("received from websocket: ");
      Serial.println((char*)payload);
      DynamicJsonDocument json(4096);
      deserializeJson(json, (char*)payload);
      process_web_socket_cam_settings(&json);
      if (json.containsKey("light")) {
      }
      if (json.containsKey("image")) {
        uint8_t fail_cnt;
        esp_err_t err =
            tpl_init_camera(&fail_cnt, PIXFORMAT_JPEG, FRAMESIZE_QVGA);
        if (err == ESP_OK) {
          camera_fb_t* fb = esp_camera_fb_get();
          if (!fb) {
            Serial.println("Camera capture failed");
          } else {
            if (fb->format == PIXFORMAT_JPEG) {
              webSocket.broadcastBIN(fb->buf, fb->len);
			  uint32_t width = fb->width;
			  uint32_t height = fb->height;
              if (raw_image == NULL) {
                raw_image = (uint8_t*)ps_malloc(width * height * 3);
              }
              if (gray_image == NULL) {
                gray_image = (uint8_t*)ps_malloc(width * height);
              }
              fmt2rgb888(fb->buf, fb->len, fb->format, raw_image);

              int code_cnt = qr_identify(width, height);
              for (int i = 0; i < code_cnt; i++) {
                uint16_t data[9];
                data[0] = 1;
                for (int j = 0; j < 4; j++) {
                  data[2 * j + 1] = qr_codes[i].corners[j].x;
                  data[2 * j + 2] = qr_codes[i].corners[j].y;
                }
                webSocket.broadcastBIN((uint8_t*)data, 18);
              }

              uint8_t head[5];
              head[0] = 0;
              head[1] = width >> 8;
              head[2] = width & 0xff;
              head[3] = height >> 8;
              head[4] = height & 0xff;
              webSocket.broadcastBIN(head, 5);
              webSocket.broadcastBIN(gray_image, width*height);
            }
            esp_camera_fb_return(fb);
          }
        }
      }
    } break;
    case WStype_BIN:
    case WStype_ERROR:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_PING:
    case WStype_PONG:
      break;
  }
}

void TaskWebSocket(void* pvParameters) {
  const TickType_t xDelay = 1 + 10 / portTICK_PERIOD_MS;
  uint32_t send_status_ms = 0;

  Serial.println("WebSocket Task started");
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  for (;;) {
    uint32_t now = millis();
    webSocket.loop();

    if (now > send_status_ms) {
      send_status_ms = now + 100;
      String data = "........................................";
      for (int i = 0; i < 40; i++) {
        char ch = 48 + digitalRead(i);
        data.setCharAt(i, ch);
      }
      DynamicJsonDocument myObject(4096);
      myObject["millis"] = millis();
      myObject["mem_free"] = (long)ESP.getFreeHeap();
      myObject["stack_free"] = (long)uxTaskGetStackHighWaterMark(NULL);
      // myObject["time"] = formattedTime;
      // myObject["b64"] = base64::encode((uint8_t*)data_buf, data_idx);
      // myObject["button_analog"] = analogRead(BUTTON_PIN);
      // myObject["button_digital"] = digitalRead(BUTTON_PIN);
      myObject["digital"] = data;
      // myObject["sample_rate"] = I2S_SAMPLE_RATE;
#define BUFLEN 4096
      char buffer[BUFLEN];
      /* size_t bx = */ serializeJson(myObject, &buffer, BUFLEN);
      String as_json = String(buffer);
      webSocket.broadcastTXT(as_json);
    }
    vTaskDelay(xDelay);
  }
}

//---------------------------------------------------
void setup() {
#ifdef DEBUG_ESP
  Serial.begin(115200);
  Serial.setDebugOutput(true);
#endif
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  ESP_LOGE(TAG, "Free heap: %u", xPortGetFreeHeapSize());

  my_wifi_setup(true);

  server.onNotFound([]() {
    server.send(404);
    Serial.print("Not found: ");
    Serial.println(server.uri());
  });
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
  server.on("/image", HTTP_GET, []() {
    bool send_error = true;
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_pixformat(sensor, PIXFORMAT_JPEG);
    sensor->set_framesize(sensor, FRAMESIZE_UXGA);
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
    } else {
      if (fb->width > 100) {
        if (fb->format != PIXFORMAT_JPEG) {
          //          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf,
          //          &_jpg_buf_len); fb = NULL; if(!jpeg_converted){
          //            Serial.println("JPEG compression failed");
          //          }
        } else {
          server.sendHeader("Connection", "close");
          server.send_P(200, "Content-Type: image/jpeg", (const char*)fb->buf,
                        fb->len);
          send_error = false;
        }
      }
      esp_camera_fb_return(fb);
    }
    if (send_error) {
      server.send(404);
      Serial.print("IMAGE ERROR: ");
      Serial.println(server.uri());
    }
  });
  server.on("/sleep", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", "SLEEP");
    esp_sleep_enable_timer_wakeup(5 * 1000000);
    esp_deep_sleep_start();
  });
  server.on("/qr", HTTP_GET, []() {
    bool send_error = true;
#ifdef NEW
    app_qr_recognize();
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_pixformat(sensor, PIXFORMAT_GRAYSCALE);
    sensor->set_framesize(sensor, FRAMESIZE_QVGA);
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
    } else {
      struct quirc* qr_recognizer = quirc_new();
      if (!qr_recognizer) {
        ESP_LOGE(TAG, "Can't create quirc object");

      } else {
        if (quirc_resize(qr_recognizer, fb->width, fb->height) < 0) {
          ESP_LOGE(
              TAG,
              "Resize the QR-code recognizer err (cannot allocate memory).");
        } else {
          int w, h;
          uint8_t* image = quirc_begin(qr_recognizer, &w, &h);
          if (image) {
            memcpy(image, fb->buf, w * h);
            quirc_end(qr_recognizer);

            // Return the number of QR-codes identified in the last processed
            // image.
            int id_count = quirc_count(qr_recognizer);

            server.sendHeader("Connection", "close");
            server.send(200, "text/plain",
                        id_count > 0 ? "QR-CODE" : "NO QR-CODE");
            send_error = false;
          } else {
            ESP_LOGE(TAG, "no image, dimension %d*%d != %d, %d*%d", w, h,
                     fb->len, fb->width, fb->height);
          }
        }
        ESP_LOGE(TAG, "Deconstruct QR-Code recognizer(quirc)");
        quirc_destroy(qr_recognizer);
      }
      esp_camera_fb_return(fb);
    }
#endif
    if (send_error) {
      server.send(404);
      Serial.print("IMAGE ERROR: ");
      Serial.println(server.uri());
    }
  });
  server.begin();

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
  }
  ESP_LOGE(TAG, "Free heap after setup: %u", xPortGetFreeHeapSize());
  ESP_LOGE(TAG, "Total heap: %u", ESP.getHeapSize());
  ESP_LOGE(TAG, "Free heap: %u", ESP.getFreeHeap());
  ESP_LOGE(TAG, "Total PSRAM: %u", ESP.getPsramSize());
  ESP_LOGE(TAG, "Free PSRAM: %u", ESP.getFreePsram());

  BaseType_t rc = xTaskCreatePinnedToCore(TaskWebSocket, "WebSocket", 81920,
                                          (void*)1, 1, NULL, 0);
  if (rc != pdPASS) {
    Serial.print("cannot start task=");
    Serial.println(rc);
  }

  Serial.println("Setup done.");
}

void loop() {
  my_wifi_loop(true);
  server.handleClient();
}
