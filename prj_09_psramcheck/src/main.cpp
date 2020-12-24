#include <string.h>

#include "template.h"
#undef ARDUINOJSON_USE_LONG_LONG
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Esp.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "../../private_bot.h"
#include "../../private_sha.h"
#include "esp32-hal-psram.h"

using namespace std;

//---------------------------------------------------

#define MAGIC_IMAGE 0xdeadbeaf

struct ps_image_s {
  uint32_t buf_1111111X[50000];
  size_t img_len;
  uint32_t magic;
  uint32_t checksum;
  uint32_t pad2;
  uint32_t pad3;
  uint32_t pad4;
  uint32_t pad5;
  uint32_t overwritten;
};

#define OK_WORDS 3
void store_image(struct ps_image_s *p, uint8_t *jpeg, size_t jpeg_len) {
  uint32_t *in = (uint32_t *)jpeg;
  uint32_t *out = p->buf_1111111X;
  uint32_t transferred = 0;
  uint32_t checksum = 0;
  while (transferred < jpeg_len) {
    for (uint8_t i = 0; i < OK_WORDS; i++) {
      uint32_t x = *in++;
      *out++ = x;
	  checksum += x;
      transferred += 4;
    }
    // skip damaged line
    out++;
  }
  p->checksum = checksum;
  p->img_len = jpeg_len;
  p->magic = MAGIC_IMAGE;
  p->overwritten = MAGIC_IMAGE;
}
size_t check_image(struct ps_image_s *p) {
  if (p->magic != MAGIC_IMAGE) {
    return 0;
  }
  if (p->overwritten == MAGIC_IMAGE) {
    return 0;
  }
  return p->img_len;
}
bool read_image(struct ps_image_s *p, uint8_t *jpeg, size_t jpeg_len) {
  uint32_t *in = p->buf_1111111X;
  uint32_t *out = (uint32_t *)jpeg;
  uint32_t transferred = 0;
  uint32_t checksum = 0;
  while (transferred < jpeg_len) {
    for (uint8_t i = 0; i < OK_WORDS; i++) {
      uint32_t x = *in++;
      *out++ = x;
	  checksum += x;
      transferred += 4;
    }
    // skip damaged line
    in++;
  }
  p->img_len = 0;
  p->magic = 0;
  return (checksum == p->checksum);
}

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
  tpl_system_setup(10 * 60);  // 10mins deep sleep time

  // turn flash light off
  digitalWrite(tpl_flashPin, LOW);
  pinMode(tpl_flashPin, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
  }
  struct ps_image_s *p = (struct ps_image_s *) ps_malloc(sizeof(ps_image_s));
  if (p) {
    const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
    size_t img_len = check_image(p);
    if (img_len > 0) {
      // have image, then send
      tpl_config.curr_jpg_len = img_len;
      tpl_config.curr_jpg = (uint8_t *)malloc(img_len + 8 * 32);
      if (tpl_config.curr_jpg) {
        tpl_telegram_setup(CHAT_ID);
        if (read_image(p, tpl_config.curr_jpg, img_len)) {

        tpl_command = CmdSendJpg2Bot;
        while (tpl_command != CmdIdle) {
          vTaskDelay(xDelay);
        }
		}
		else {
			tpl_config.bot_message = "error";
			tpl_config.bot_send_message = true;
		}
        // wait lower level finish transmission !?
        const TickType_t vDelay = 10000 / portTICK_PERIOD_MS;
        vTaskDelay(vDelay);
      }
    } else {
      // take picture only every 24th boot => every 4 hours
      // if ((tpl_config.bootCount % 24) == 1) {
      {
        uint8_t fail_cnt = 0;
        tpl_camera_setup(&fail_cnt, FRAMESIZE_VGA);
        Serial.print("camera fail count=");
        Serial.println(fail_cnt);

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
          // take picture
          camera_fb_t *fb = esp_camera_fb_get();
          // flash off
          digitalWrite(tpl_flashPin, LOW);
          if (fb) {
            store_image(p, fb->buf, fb->len);
            esp_camera_fb_return(fb);
          }
        }
        // Free memory for bot
        // esp_camera_deinit();
        print_info();
        ESP.restart();
      }
    }
  }
  print_info();

  Serial.println("Done.");

  // enter deep sleep
  tpl_command = CmdDeepSleep;
}

void loop() {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}