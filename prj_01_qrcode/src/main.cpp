#include <string.h>
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
#include <time.h>

#include "esp32-hal-psram.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rom/rtc.h"
#include "fb_gfx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "qrcode_recognize.h"
#include <hwcrypto/sha.h>
#include "quirc.h"
#include "template.h"
#include "../../private_sha.h"

#define DEBUG_ESP

#ifdef DEBUG_ESP
#define DBG(x) Serial.println(x)
#else
#define DBG(...)
#endif

#define OUTPUT_GRAY 0

using namespace std;

WebServer server(80);
extern const uint8_t index_html_start[] asm("_binary_src_index_html_start");
extern const uint8_t server_index_html_start[] asm(
    "_binary_src_serverindex_html_start");

#define flashPin 4
#define ledPin 33

bool qrMode = true;
bool qr_code_valid = false;
long qr_stack_free = 0;
struct quirc_data qr_data;
volatile bool qr_task_busy = false;
camera_fb_t* fb_image = NULL;
uint8_t* raw_image = NULL;
#if OUTPUT_GRAY != 0
uint8_t* gray_image = NULL;
#endif

String sha = String("");

int id_count;
struct quirc_code qr_code;

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
              if (!qr_task_busy) {
				  fb_image = fb;
                  qr_task_busy = true;
				  // QR task need to return fb
                }
				else {
					esp_camera_fb_return(fb);
				}
#if OUTPUT_GRAY != 0
				  if (id_count > 0) {
					uint16_t data[9];
					data[0] = 1;
					for (int j = 0; j < 4; j++) {
					  data[2 * j + 1] = qr_code.corners[j].x;
					  data[2 * j + 2] = qr_code.corners[j].y;
					}
					webSocket.broadcastBIN((uint8_t*)data, 18);
				  }
				  if (gray_image != NULL) {
					  uint8_t head[5];
					  head[0] = 0;
					  head[1] = fb->width >> 8;
					  head[2] = fb->width & 0xff;
					  head[3] = fb->height >> 8;
					  head[4] = fb->height & 0xff;
					  webSocket.broadcastBIN(head, 5);
					  webSocket.broadcastBIN(gray_image, fb->width * fb->height);
				  }
#endif
            }
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

void TaskQRreader(void* pvParameters) {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  for (;;) {
    if (qr_task_busy) {
	if (fb_image != NULL) {
      qr_stack_free = (long)uxTaskGetStackHighWaterMark(NULL);
      pinMode(ledPin, OUTPUT);
      digitalWrite(ledPin, !digitalRead(ledPin));

      uint32_t width = fb_image->width;
      uint32_t height = fb_image->height;
      if (raw_image == NULL) {
        raw_image = (uint8_t*)ps_malloc(width * height * 3);
      }
      fmt2rgb888(fb_image->buf, fb_image->len, PIXFORMAT_JPEG, raw_image);
      esp_camera_fb_return(fb_image);
	  fb_image = NULL;
	  
      struct quirc* qr_recognizer = quirc_new();
      if (qr_recognizer) {
        if (quirc_resize(qr_recognizer, width, height) >= 0) {
          int w = width, h = height;
          uint8_t* image = quirc_begin(qr_recognizer, &w, &h);
          if (image) {
            uint8_t* rgb = raw_image;
            uint8_t* gray = image;
#if OUTPUT_GRAY != 0
			  if (gray_image == NULL) {
				gray_image = (uint6_t*)ps_malloc(width * height);
			  }
            uint8_t* gray2 = gray_image;
#endif
            for (uint32_t i = width * height; i > 0; i--) {
              uint8_t r = *rgb++;
              uint8_t g = *rgb++;
              uint8_t b = *rgb++;

			  uint8_t out = (g >> 1) + (b >> 1);
			   out = (out >> 1) + (r >> 1);

//              uint16_t out = 0;

 //             uint16_t avg = 0;
 //             avg += r;
 //             avg += g;
 //             avg += b;
 //             avg /= 3;

//              uint8_t p_max = max(max(r, max(g, b)),(uint8_t)1);
//              uint8_t p_min = min(r, min(g, b));

//              out = r;
//			  out *= g;
//			  out /= p_max;
//			  out *= b;
//			  out /= p_max;

              *gray++ = out;
#if OUTPUT_GRAY != 0
              *gray2++ = out;
#endif
            }
            quirc_end(qr_recognizer);
              // Return the number of QR-codes identified in the last processed
              // image.
              id_count = quirc_count(qr_recognizer);
              if (id_count > 0) {
                quirc_extract(qr_recognizer, 0, &qr_code);
                quirc_decode_error_t res = quirc_decode(&qr_code, &qr_data);
                if (res == QUIRC_SUCCESS) {
                  qr_code_valid = true;
                }
              }
			  else {
				memset(&qr_data, 0, sizeof(qr_data));
			  }
          }
        }
        quirc_destroy(qr_recognizer);
      }
	}
      qr_task_busy = false;
    }
    vTaskDelay(xDelay);
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
      myObject["reset_reason"] = (rtc_get_reset_reason(0) << 4) | rtc_get_reset_reason(1);
      myObject["millis"] = millis();
      myObject["mem_free"] = (long)ESP.getFreeHeap();
      myObject["stack_free"] = (long)uxTaskGetStackHighWaterMark(NULL);
      myObject["qr_stack_free"] = qr_stack_free;
	  myObject["sha"] = sha;
      // myObject["time"] = formattedTime;
      // myObject["b64"] = base64::encode((uint8_t*)data_buf, data_idx);
      // myObject["button_analog"] = analogRead(BUTTON_PIN);
      // myObject["button_digital"] = digitalRead(BUTTON_PIN);
      myObject["digital"] = data;
      String qr_string = "........................................";
      if (qr_code_valid) {
        for (int i = 0; i < 40; i++) {
          char ch = qr_data.payload[i];
          if ((ch >= 32) && (ch <= 127)) {
            qr_string.setCharAt(i, ch);
          }
        }
      }
      myObject["qr"] = qr_string;
	  myObject["codes"] = id_count;
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

  // turn flash light off
  digitalWrite(flashPin, LOW);
  pinMode(flashPin, OUTPUT);

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
  server.begin();

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
  }
  ESP_LOGE(TAG, "Free heap after setup: %u", xPortGetFreeHeapSize());
  ESP_LOGE(TAG, "Total heap: %u", ESP.getHeapSize());
  ESP_LOGE(TAG, "Free heap: %u", ESP.getFreeHeap());
  ESP_LOGE(TAG, "Total PSRAM: %u", ESP.getPsramSize());
  ESP_LOGE(TAG, "Free PSRAM: %u", ESP.getFreePsram());

  BaseType_t rc = xTaskCreatePinnedToCore(TaskWebSocket, "WebSocket", 16384,
                                          (void*)1, 1, NULL, 0);
  if (rc != pdPASS) {
    Serial.print("cannot start task=");
    Serial.println(rc);
  }

  // have observed only 9516 Bytes free...
  rc = xTaskCreatePinnedToCore(TaskQRreader, "QRreader", 65536, (void*)1, 0, NULL, 1);  // Prio 0, Core 1
  if (rc != pdPASS) {
    Serial.print("cannot start task=");
    Serial.println(rc);
  }

  Serial.print("Retrieving time: ");
  configTime(0, 0, "pool.ntp.org");  // get UTC time via NTP
  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }

  Serial.println("Setup done.");
}

void calc_sha() {
  char strftime_buf[64];
  struct tm timeinfo;
  time_t now;
  time(&now);
  setenv("TZ", "CET-1CEST,M3.5.0/2:00,M10.5.0/3:00", 1);
  tzset();
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%d.%m.%y, %H:%M ", &timeinfo);
    uint8_t sha1HashBin[20] = { 0 };
	// "19.12.20, 21:44 "
    String data = String(strftime_buf) + SHA_SEED;
    esp_sha(SHA1, (unsigned char *)data.c_str(), data.length(), &sha1HashBin[0]);

	char result[10];
	sprintf(result,"%02x%02x",sha1HashBin[0], sha1HashBin[1]);
	sha = String(result) + String(" ") + data;
}

void loop() {
  my_wifi_loop(true);
  server.handleClient();
  calc_sha();
}
