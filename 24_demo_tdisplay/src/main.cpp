#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>

#define SW_NAME_REV "24_demo_tdisplay v1.0"

#include "template.h"

#include <lvgl.h>
#include <stdlib.h>

#include "ui/screens.h"
#include "ui/ui.h"
#include "ui/vars.h"
#include "ui/actions.h"

#include <TFT_eSPI.h>

#define SCREEN_WIDTH 135
#define SCREEN_HEIGHT 240

#define BTN1_PIN 0
#define BTN2_PIN 35
#define ADC_PIN 34

#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

TFT_eSPI tft = TFT_eSPI();

static uint32_t g_btn1_count = 0;
static uint32_t g_btn2_count = 0;
static bool g_btn1_last = false;
static bool g_btn2_last = false;
static char g_btn_text[32] = "";
static unsigned long g_last_btn_time = 0;

static const char* ntp_server = "pool.ntp.org";
static const long gmt_offset_sec = 3600;
static const int daylight_offset_sec = 3600;
static bool g_time_synced = false;

void log_print(lv_log_level_t level, const char* buf) {
  LV_UNUSED(level);
  Serial.println(buf);
  Serial.flush();
}

void lv_init_esp32(void) {
  lv_log_register_print_cb(log_print);

  lv_display_t* disp;
  disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf,
                            sizeof(draw_buf));

  tft.setRotation(0);
}

static void update_display() {
  char buf[32];

  if (objects.lbl_time != NULL) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      g_time_synced = true;
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
               timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
               (millis() / 3600000) % 24,
               (millis() / 60000) % 60,
               (millis() / 1000) % 60);
    }
    lv_label_set_text(objects.lbl_time, buf);
  }

  if (objects.lbl_wifi != NULL) {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s", WiFi.SSID().c_str());
    } else {
      snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " ---");
    }
    lv_label_set_text(objects.lbl_wifi, buf);
  }

  if (objects.lbl_ip != NULL) {
    if (WiFi.status() == WL_CONNECTED) {
      snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
    } else {
      snprintf(buf, sizeof(buf), "IP: ---");
    }
    lv_label_set_text(objects.lbl_ip, buf);
  }

  if (objects.lbl_voltage != NULL) {
    int raw = analogRead(ADC_PIN);
    int mv = raw * 3300 / 4095;
    snprintf(buf, sizeof(buf), "%d mV  (raw: %d)", mv, raw);
    lv_label_set_text(objects.lbl_voltage, buf);
  }

  if (objects.lbl_heap != NULL) {
    uint32_t free_heap = ESP.getFreeHeap();
    uint32_t total_heap = ESP.getHeapSize();
    uint32_t pct = total_heap > 0 ? (free_heap * 100) / total_heap : 0;
    snprintf(buf, sizeof(buf), "Heap: %uK free", free_heap / 1024);
    lv_label_set_text(objects.lbl_heap, buf);

    if (objects.bar_heap != NULL) {
      lv_bar_set_value(objects.bar_heap, pct, LV_ANIM_OFF);
    }
  }

  if (objects.lbl_uptime != NULL) {
    unsigned long sec = millis() / 1000;
    snprintf(buf, sizeof(buf), "UP %luh %lum %lus",
             sec / 3600, (sec % 3600) / 60, sec % 60);
    lv_label_set_text(objects.lbl_uptime, buf);
  }

  if (objects.lbl_btn != NULL && strlen(g_btn_text) > 0) {
    lv_label_set_text(objects.lbl_btn, g_btn_text);
  }
}

static void check_buttons() {
  bool btn1 = digitalRead(BTN1_PIN) == LOW;
  bool btn2 = digitalRead(BTN2_PIN) == LOW;

  if (btn1 && !g_btn1_last) {
    g_btn1_count++;
    snprintf(g_btn_text, sizeof(g_btn_text), "B1:%lu B2:%lu",
             g_btn1_count, g_btn2_count);
    g_last_btn_time = millis();
    Serial.printf("[BTN] BTN1 pressed (count: %lu)\n", g_btn1_count);
  }

  if (btn2 && !g_btn2_last) {
    g_btn2_count++;
    snprintf(g_btn_text, sizeof(g_btn_text), "B1:%lu B2:%lu",
             g_btn1_count, g_btn2_count);
    g_last_btn_time = millis();
    Serial.printf("[BTN] BTN2 pressed (count: %lu)\n", g_btn2_count);
  }

  g_btn1_last = btn1;
  g_btn2_last = btn2;

  if (millis() - g_last_btn_time > 10000) {
    g_btn_text[0] = '\0';
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println(SW_NAME_REV);

  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  analogReadResolution(12);

  tpl_system_setup(0);
  tpl_wifi_setup(true, true, (gpio_num_t)255);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL, NULL);
  tpl_command_setup(NULL);

  configTime(gmt_offset_sec, daylight_offset_sec, ntp_server);

  lv_init();
  lv_init_esp32();

  ui_init();
  Serial.println("[Main] UI initialized");

  Serial.println("[Main] Setup complete");
}

void loop() {
  static long last_ms = 0;
  static unsigned long last_update = 0;
  long now_ms = millis();

  lv_task_handler();
  lv_tick_inc(now_ms - last_ms);
  ui_tick();

  check_buttons();

  if (now_ms - last_update > 500) {
    last_update = now_ms;
    update_display();
  }

  last_ms = now_ms;
  delay(5);
}
