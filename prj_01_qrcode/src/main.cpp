#include <string.h>

#include "template.h"
#undef ARDUINOJSON_USE_LONG_LONG
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <base64.h>
#include <hwcrypto/sha.h>
#include <mem.h>
#include <time.h>

#include "../../private_sha.h"
#include "esp32-hal-psram.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fb_gfx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "qrcode_recognize.h"
#include "quirc.h"

#define DEBUG_ESP

#ifdef DEBUG_ESP
#define DBG(x) Serial.println(x)
#else
#define DBG(...)
#endif

#define OUTPUT_GRAY 0

using namespace std;

bool qrMode = true;
bool qr_code_valid = false;
bool check_qr_unlock = false;
bool allow_unlock = false;
long qr_stack_free = 0;
quirc_decode_error_t qr_decode_res = QUIRC_SUCCESS;
struct quirc_data qr_data;
volatile bool qr_task_busy = false;
camera_fb_t* fb_image = NULL;
uint8_t* raw_image = NULL;
#if OUTPUT_GRAY != 0
uint8_t* gray_image = NULL;
#endif

uint32_t* psram_buffer = NULL;
uint32_t ps_state = 0;

int id_count;
struct quirc_code qr_code;

extern const uint8_t index_html_start[] asm("_binary_src_index_html_start");

#ifdef OLD
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload,
                    size_t length) {
  switch (type) {
    case WStype_TEXT: {
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
              } else {
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
  }
}
#endif

void TaskQRreader(void* pvParameters) {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  for (;;) {
    if (qr_task_busy) {
      if (fb_image != NULL) {
        qr_stack_free = (long)uxTaskGetStackHighWaterMark(NULL);
        pinMode(tpl_ledPin, OUTPUT);
        digitalWrite(tpl_ledPin, !digitalRead(tpl_ledPin));

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

                //              uint8_t p_max = max(max(r, max(g,
                //              b)),(uint8_t)1); uint8_t p_min = min(r, min(g,
                //              b));

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
                qr_decode_res = quirc_decode(&qr_code, &qr_data);
                if (qr_decode_res == QUIRC_SUCCESS) {
                  qr_code_valid = true;
                  check_qr_unlock = true;
                } else {
                  quirc_flip(&qr_code);
                  qr_decode_res = quirc_decode(&qr_code, &qr_data);
                  if (qr_decode_res == QUIRC_SUCCESS) {
                    qr_code_valid = true;
                    check_qr_unlock = true;
                  }
                }
              } else {
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

void add_ws_info(DynamicJsonDocument* myObject) {
  String qr_string = "........................................";
  if (qr_code_valid) {
    for (int i = 0; i < 40; i++) {
      char ch = qr_data.payload[i];
      if ((ch >= 32) && (ch <= 127)) {
        qr_string.setCharAt(i, ch);
      }
    }
  }

  (*myObject)["qr_stack_free"] = qr_stack_free;
  (*myObject)["qr"] = qr_string;
  (*myObject)["qr_decode"] = String(quirc_strerror(qr_decode_res));
  (*myObject)["codes"] = id_count;
  (*myObject)["unlock"] = allow_unlock;
}

//---------------------------------------------------
void setup() {
  tpl_system_setup();

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(add_ws_info);
  // tpl_telegram_setup(BOTtoken, CHAT_ID);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  // turn flash light off
  digitalWrite(tpl_flashPin, LOW);
  pinMode(tpl_flashPin, OUTPUT);

  tpl_server.on("/image", HTTP_GET, []() {
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
          tpl_server.sendHeader("Connection", "close");
          tpl_server.send_P(200, "Content-Type: image/jpeg",
                            (const char*)fb->buf, fb->len);
          send_error = false;
        }
      }
      esp_camera_fb_return(fb);
    }
    if (send_error) {
      tpl_server.send(404);
      Serial.print("IMAGE ERROR: ");
      Serial.println(tpl_server.uri());
    }
  });

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
  }
  psram_buffer = (uint32_t*)ps_malloc(32 * 4);

  tpl_config.deepsleep_time = 1000000LL*10; // 10 s

  // have observed only 9516 Bytes free...
  xTaskCreatePinnedToCore(TaskQRreader, "QRreader", 65536, (void*)1, 0, NULL,
                          1);  // Prio 0, Core 1

  Serial.println("Setup done.");
}

void check_unlock(bool prev_minute) {
  char strftime_buf[64];
  struct tm timeinfo;
  time_t now;
  time(&now);
  if (prev_minute) {
    now -= 60;
  }
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%d.%m.%y, %H:%M ", &timeinfo);
  uint8_t sha1HashBin[20] = {0};
  // "19.12.20, 21:44 "
  String data = String(strftime_buf) + SHA_SEED;
  esp_sha(SHA1, (unsigned char*)data.c_str(), data.length(), &sha1HashBin[0]);

  char result[10];
  sprintf(result, "%02x%02x", sha1HashBin[0], sha1HashBin[1]);
  if (strncmp(result, (const char*)qr_data.payload, 4) == 0) {
    allow_unlock = true;
  }
}

void loop() {
  if (!qr_task_busy) {
    uint8_t fail_cnt = 0;
    esp_err_t err = tpl_init_camera(&fail_cnt, PIXFORMAT_JPEG, FRAMESIZE_QVGA);
    if (err == ESP_OK) {
      camera_fb_t* fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Camera capture failed");
      } else {
        if (fb->format == PIXFORMAT_JPEG) {
          fb_image = fb;
          qr_task_busy = true;
          // QR task need to return fb
        }
      }
    }
  }

  if (check_qr_unlock) {
    check_qr_unlock = false;
    check_unlock(false);
    check_unlock(true);
  }
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
