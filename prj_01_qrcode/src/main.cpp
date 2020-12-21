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
#include "../../private_bot.h"
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
#endif

void TaskQRreader(void* pvParameters) {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  for (;;) {
    if (qr_task_busy) {
      if (fb_image != NULL) {
        Serial.println("analyze");
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
                Serial.println("codes found");
                quirc_extract(qr_recognizer, 0, &qr_code);
                qr_decode_res = quirc_decode(&qr_code, &qr_data);
                if (qr_decode_res == QUIRC_SUCCESS) {
                  Serial.println("valid code");
                  qr_code_valid = true;
                  check_qr_unlock = true;
                } else {
                  quirc_flip(&qr_code);
                  qr_decode_res = quirc_decode(&qr_code, &qr_data);
                  if (qr_decode_res == QUIRC_SUCCESS) {
                    Serial.println("valid code after flip");
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
  tpl_system_setup();
  tpl_config.deepsleep_time = 1000000LL * 10;  // 10 s
  // turn flash light off
  digitalWrite(tpl_flashPin, LOW);
  pinMode(tpl_flashPin, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(add_ws_info);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
  }
  psram_buffer = (uint32_t*)ps_malloc(32 * 4);

  // better to start task before camera setup
#ifdef ENABLE_QRreader
  tpl_tasks.app_name1 = "QRreader";
  if (pdPASS != xTaskCreatePinnedToCore(TaskQRreader, "QRreader", 40000, NULL,
                                        1, &tpl_tasks.task_app1,
                                        CORE_1)) {  // Prio 1, Core 1
    Serial.println("Failed to start task.");
  }
#endif

  uint8_t fail_cnt = 0;
  tpl_camera_setup(&fail_cnt, FRAMESIZE_QVGA);
  Serial.print("camera fail count=");
  Serial.println(fail_cnt);

  tpl_telegram_setup(BOTtoken, CHAT_ID);

  print_info();

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

uint32_t next_stack_info = 0;

void loop() {
  uint32_t ms = millis();
  if ((int32_t)(ms - next_stack_info) > 0) {
    next_stack_info = ms + 2000;
    tpl_update_stack_info();
    Serial.println(tpl_config.stack_info);
  }
  if (!qr_task_busy) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
    } else {
      fb_image = fb;
      qr_task_busy = true;
      // QR task need to return fb
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
