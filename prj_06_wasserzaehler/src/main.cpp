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
#include "evaluate.h"
#include "read.h"

using namespace std;

// UniversalTelegramBot alarm_bot(ALARM_BOTtoken, secured_client);

struct read_s reader;
uint16_t last_seen_watchpoint = 0;

// void execute(enum Command command);

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
  if (psramFound()) {
    psram_buffer_init();
    last_seen_watchpoint = psram_buffer->last_seen_watchpoint;
  } else {
    last_seen_watchpoint = 0;
  }

  tpl_system_setup(600);  // 10 minutes deep sleep

  // turn flash light off
  digitalWrite(tpl_flashPin, LOW);
  pinMode(tpl_flashPin, OUTPUT);

  // ensure camera module not powered up
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  pinMode(PWDN_GPIO_NUM, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.print("Last watchpoint=");
  Serial.println(last_seen_watchpoint);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);
  //  tpl_command_setup(execute);

  Serial.println("Setup done.");

  uint8_t fail_cnt = 0;
  tpl_camera_setup(&fail_cnt, FRAMESIZE_VGA);
  Serial.print("camera fail count=");
  Serial.println(fail_cnt);

  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;

  // turn on flash
  digitalWrite(tpl_flashPin, HIGH);
  WATCH(100);
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

  tpl_telegram_setup(BOTtoken, CHAT_ID);
  char buf[200];
  tpl_config.bot_message = buf;
  sprintf(buf, "Camera capture V1, entries: %d BootCount: %d Watchpoint: %d",
          num_entries(), tpl_config.bootCount, last_seen_watchpoint);
  tpl_config.bot_send_message = true;
  while (tpl_config.bot_send_message) {
    vTaskDelay(xDelay);
  }

  if (tpl_config.curr_jpg != NULL) {
    Serial.println("Image captured");
    bool send_image = true;
    // process image
    uint8_t *raw_image = (uint8_t *)ps_malloc(WIDTH * HEIGHT * 3);
    uint8_t *digitized_image = (uint8_t *)ps_malloc(WIDTH * HEIGHT / 8);
    uint8_t *filtered_image = (uint8_t *)ps_malloc(WIDTH * HEIGHT / 8);
    uint8_t *temp_image = (uint8_t *)ps_malloc(WIDTH * HEIGHT / 8);

    if (raw_image && digitized_image && filtered_image && temp_image) {
      fmt2rgb888(tpl_config.curr_jpg, tpl_config.curr_jpg_len, PIXFORMAT_JPEG,
                 raw_image);
      digitize(raw_image, digitized_image, 1);
      find_pointer(digitized_image, filtered_image, temp_image, &reader);
      digitize(raw_image, digitized_image, 0);
      eval_pointer(digitized_image, &reader);
      WATCH(104);
      Serial.print("Reader candidates=");
      Serial.println(reader.candidates);
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
#ifdef ALARM
        switch (alarm) {
          case NO_ALARM:
            break;
          case ALARM_TOO_HIGH_CONSUMPTION:
            send_image = true;
            alarm_bot.sendMessage(
                CHAT_ID,
                String("Wasseralarm: Hoher Verbrauch:") + +water_steigung());
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
            alarm_bot.sendMessage(CHAT_ID,
                                  String("Wasseralarm: Leck alle Zeiger"));
            break;
        }
#endif
        WATCH(106);
        sprintf(buf,
                "Result: %d/%d/%d/%d Consumption: %d Alarm: %d "
                "Buffer_add: %d, entries: %d BootCount: %d",
                reader.pointer[0].angle, reader.pointer[1].angle,
                reader.pointer[2].angle, reader.pointer[3].angle, consumption,
                alarm, res, num_entries(), tpl_config.bootCount);
        tpl_config.bot_message = buf;
        tpl_config.bot_send_message = true;
        reader.candidates = 0;
        WATCH(107);

        while (tpl_config.bot_send_message) {
          vTaskDelay(xDelay);
        }
      }
    }
    if (send_image) {
      tpl_command = CmdSendJpg2Bot;
    }

    while (tpl_command != CmdIdle) {
      vTaskDelay(xDelay);
    }
    // enter deep sleep
    WATCH(9999);
    tpl_command = CmdDeepSleep;
  }
}

void loop() {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
