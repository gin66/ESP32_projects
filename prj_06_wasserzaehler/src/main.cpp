#include <string.h>

#include "template.h"
#undef ARDUINOJSON_USE_LONG_LONG
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "../../private_bot.h"
#include "../../private_sha.h"
#include "esp32-hal-psram.h"

#include "read.h"
#include "evaluate.h"

using namespace std;

//UniversalTelegramBot alarm_bot(ALARM_BOTtoken, secured_client);

uint8_t* jpg_image = NULL;
uint32_t jpg_len = 0;
uint8_t* raw_image = NULL;
uint8_t *digitized_image;
uint8_t *filtered_image;
uint8_t *temp_image;
struct read_s reader;
uint16_t last_seen_watchpoint = 0;

//void execute(enum Command command);

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
  psram_buffer_init();
  last_seen_watchpoint = psram_buffer->last_seen_watchpoint;

  tpl_system_setup();
  tpl_config.deepsleep_time = 1000000LL * 600LL;  // 10 minutes
  tpl_config.allow_deepsleep = true;
  // turn flash light off
  digitalWrite(tpl_flashPin, LOW);
  pinMode(tpl_flashPin, OUTPUT);

  // ensure camera module not powered up
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  pinMode(PWDN_GPIO_NUM, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);
//  tpl_command_setup(execute);

  Serial.println("Setup done.");

  digitized_image = (uint8_t *)ps_malloc(WIDTH*HEIGHT/8);
  filtered_image = (uint8_t *)ps_malloc(WIDTH*HEIGHT/8);
  temp_image = (uint8_t *)ps_malloc(WIDTH*HEIGHT/8);

  uint8_t fail_cnt = 0;
  tpl_camera_setup(&fail_cnt, FRAMESIZE_VGA);
  Serial.print("camera fail count=");
  Serial.println(fail_cnt);

  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;

  // turn on flash
  digitalWrite(tpl_flashPin, HIGH);
  // activate cam
  {
    uint32_t settle_till = millis() + 10000;
    while ((int32_t)(settle_till - millis()) > 0) {
      // let the camera adjust
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb) {
        esp_camera_fb_return(fb);
      }
      vTaskDelay(xDelay);
    }
    {
      // take picture
      camera_fb_t *fb = esp_camera_fb_get();
      // flash off
      digitalWrite(tpl_flashPin, LOW);
      if (fb) {
        tpl_config.curr_jpg_len = fb->len;
        tpl_config.curr_jpg = (uint8_t *)ps_malloc(fb->len);
        if (tpl_config.curr_jpg) {
          memcpy(tpl_config.curr_jpg, fb->buf, fb->len);
        }
        esp_camera_fb_return(fb);
      }
    }
  }
  // Free memory for bot
  esp_camera_deinit();
  print_info();

  if (tpl_config.curr_jpg != NULL) {
    tpl_telegram_setup(BOTtoken, CHAT_ID);
    tpl_command = CmdSendJpg2Bot;
  }
  while (tpl_command != CmdIdle) {
    vTaskDelay(xDelay);
  }
  // enter deep sleep
  tpl_command = CmdDeepSleep;
}

void loop() {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}

#ifdef OLD
void execute(enum Command command {
    switch (command) {
      case CmdMeasure:
        WATCH(100);
        status = bot.sendMessage(
            CHAT_ID, "Camera capture V3: " + String(" entries: ") +
                         num_entries() + String(" BootCount: ") + bootCount +
                         String(" Watchpoint: ") + last_seen_watchpoint);
        if (init_camera()) {
          WATCH(101);
          digitalWrite(flashPin, HIGH);
          uint32_t start_ms = millis();
          while ((uint32_t)(millis() - start_ms) < 3000) {
            // let the camera adjust
            camera_fb_t* fb = esp_camera_fb_get();
            if (fb != NULL) {
              esp_camera_fb_return(fb);
            }
          }
          WATCH(102);
          bool send_image = true;
          for (uint8_t i = 0; i < 10; i++) {
            camera_fb_t* fb = esp_camera_fb_get();
            if (fb == NULL) {
              continue;
            }
            jpg_len = fb->len;
            if (jpg_len > 50000) {
              esp_camera_fb_return(fb);
              continue;
            }
            if (jpg_image == NULL) {
              jpg_image = (uint8_t*)ps_malloc(50000);  // should be enough
            }
            if (raw_image == NULL) {
              raw_image = (uint8_t*)ps_malloc(WIDTH * HEIGHT * 3);
            }
            memcpy(jpg_image, fb->buf, jpg_len);
            esp_camera_fb_return(fb);
            WATCH(103);

            fmt2rgb888(jpg_image, jpg_len, PIXFORMAT_JPEG, raw_image);
            digitize(raw_image, digitized_image, 1);
            find_pointer(digitized_image, filtered_image, temp_image, &reader);
            digitize(raw_image, digitized_image, 0);
            eval_pointer(digitized_image, &reader);
            WATCH(104);
            if (reader.candidates == 4) {
              send_image = false;
              struct timeval tv;
              gettimeofday(&tv, NULL);
              int8_t res = psram_buffer_add(
                  tv.tv_sec, reader.pointer[0].angle, reader.pointer[1].angle,
                  reader.pointer[2].angle, reader.pointer[3].angle);
              if (res < 0) {
                send_image = true;
              }
              WATCH(105);
              uint16_t consumption = water_consumption();
              uint8_t alarm = have_alarm();
              switch (alarm) {
                case NO_ALARM:
                  break;
                case ALARM_TOO_HIGH_CONSUMPTION:
                  send_image = true;
                  alarm_bot.sendMessage(
                      CHAT_ID, String("Wasseralarm: Hoher Verbrauch:") +
                                   +water_steigung());
                  break;
                case ALARM_CUMULATED_CONSUMPTION_TOO_HIGH:
                  send_image = true;
                  alarm_bot.sendMessage(
                      CHAT_ID,
                      String("Wasseralarm: Kumulierter Verbrauch zu hoch: ") +
                          cumulated_consumption());
                  break;
                case ALARM_LEAKAGE:
                  send_image = true;
                  alarm_bot.sendMessage(CHAT_ID, String("Wasseralarm: Leck"));
                  break;
                case ALARM_LEAKAGE_FINE:
                  send_image = true;
                  alarm_bot.sendMessage(
                      CHAT_ID, String("Wasseralarm: Leck alle Zeiger"));
                  break;
              }
              WATCH(106);
              char buf[200];
              sprintf(buf,
                      "Result: %d/%d/%d/%d Consumption: %d Alarm: %d "
                      "Buffer_add: %d, entries: %d BootCount: %d",
                      reader.pointer[0].angle, reader.pointer[1].angle,
                      reader.pointer[2].angle, reader.pointer[3].angle,
                      consumption, alarm, res, num_entries(), bootCount);
              bot.sendMessage(CHAT_ID, String(buf));
              reader.candidates = 0;
              WATCH(107);
              break;
            }
          }
          WATCH(110);
          digitalWrite(flashPin, LOW);
          if (send_image && (jpg_image != NULL)) {
            WATCH(111);
            dataBytesSent = 0;
            status = bot.sendPhotoByBinary(CHAT_ID, "image/jpeg", jpg_len,
                                           isMoreDataAvailable, nullptr,
                                           getNextBuffer, getNextBufferLen);
          }
          WATCH(112);
          bot.sendMessage(
              CHAT_ID, WiFi.SSID() + String(": ") + WiFi.localIP().toString());
        }
        WATCH(121);
        command = CmdDeepSleep;
        WATCH(1);
        break;
      case CmdSendImage:
      case CmdSendRawImage:
        if (init_camera()) {
          bool raw = (command == CmdSendRawImage);
          camera_fb_t* fb = esp_camera_fb_get();
          if (!fb) {
            Serial.println("Camera capture failed");
            delay(1000);
            ESP.restart();
          } else {
            status = true;
            if ((fb->format == PIXFORMAT_JPEG) && raw) {
              webSocket.broadcastBIN(fb->buf, fb->len);
              esp_camera_fb_return(fb);
            } else if ((fb->format == PIXFORMAT_JPEG) && !raw) {
              if (raw_image == NULL) {
                raw_image = (uint8_t*)ps_malloc(WIDTH * HEIGHT * 3);
              }
              fmt2rgb888(fb->buf, fb->len, fb->format, raw_image);
              uint8_t head[5];
              head[0] = 3;
              head[1] = fb->width >> 8;
              head[2] = fb->width & 0xff;
              head[3] = fb->height >> 8;
              head[4] = fb->height & 0xff;
              esp_camera_fb_return(fb);
              digitize(raw_image, digitized_image, 1);
              find_pointer(digitized_image, filtered_image, temp_image,
                           &reader);
              digitize(raw_image, digitized_image, 0);
              eval_pointer(digitized_image, &reader);
              webSocket.broadcastBIN(head, 5);
              webSocket.broadcastBIN(filtered_image, WIDTH * HEIGHT / 8);
            } else {
              esp_camera_fb_return(fb);
            }
          }
        }
    }
}
#endif
