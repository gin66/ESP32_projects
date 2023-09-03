#include <GxEPD.h>
#include <GxGDEH0213B73/GxGDEH0213B73.h>  // 2.13" b/w newer panel

#include "template.h"
// FreeFonts from Adafruit_GFX
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxIO/GxIO.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <SD.h>
#include <SPI.h>

#define SPI_MOSI 23
#define SPI_MISO -1
#define SPI_CLK 18

#define ELINK_SS 5
#define ELINK_BUSY 4
#define ELINK_RESET 16
#define ELINK_DC 17

#define SDCARD_SS 13
#define SDCARD_CLK 14
#define SDCARD_MOSI 15
#define SDCARD_MISO 2

using namespace std;

#define LED_PIN 19
#define BUTTON_PIN 39 /* analog read 4095 unpressed, 0 pressed */

#define dirPinStepper1 32
#define stepPinStepper1 33
#define enablePinStepper1 25
#define dirPinStepper2 27
#define stepPinStepper2 26
#define enablePinStepper2 12

GxIO_Class io(SPI, /*CS=5*/ ELINK_SS, /*DC=*/ELINK_DC, /*RST=*/ELINK_RESET);
GxEPD_Class display(io, /*RST=*/ELINK_RESET, /*BUSY=*/ELINK_BUSY);

SPIClass sdSPI(VSPI);

bool sdOK = false;

// Gewindestange hat 2.54mm pro 2 Umdrehungen.
// 3200 steps/Umdrehung
//
// 0.397 um/step
// 5mm/s =  ~12600 steps/s
// 8mm/s =  ~20150 steps/s => 2m in 4min
#define MAX_SPEED_IN_HZ 20150
#define ACCELERATION 3200

void json_update(DynamicJsonDocument *json) {
  //  if (json->containsKey("moveBoth")) {
  //    int32_t steps = (*json)["moveBoth"];
  //    stepper1->move(steps);
  //    stepper2->move(steps);
  //  }
}

//---------------------------------------------------
void setup() {
  tpl_system_setup(0);  // no deep sleep

  Serial.begin(115200);
  Serial.setDebugOutput(false);

  // Wait OTA
  //  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_wifi_setup(true, true, (gpio_num_t)LED_PIN);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL, json_update);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

#ifdef IS_ESP32CAM
  uint8_t fail_cnt = 0;
#ifdef BOTtoken
  tpl_camera_setup(&fail_cnt, FRAMESIZE_QVGA);
#else
  tpl_camera_setup(&fail_cnt, FRAMESIZE_VGA);
#endif
#endif

#ifdef BOTtoken
  tpl_telegram_setup(BOTtoken, CHAT_ID);
#endif

  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, ELINK_SS);
  display.init();  // enable diagnostic output on Serial

  display.setRotation(1);
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(0, 0);

  sdSPI.begin(SDCARD_CLK, SDCARD_MISO, SDCARD_MOSI, SDCARD_SS);

  if (!SD.begin(SDCARD_SS, sdSPI)) {
    sdOK = false;
  } else {
    sdOK = true;
  }

  Serial.println("Setup done.");
}

void update_display() {
  char line[50] = "";

  display.fillScreen(GxEPD_WHITE);

  display.setCursor(0, 10);

  sprintf(line, "%s %s", WiFi.SSID(), WiFi.localIP());
  display.println(line);

  char strftime_buf[64];
  struct tm timeinfo;
  time_t now = time(nullptr);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%d.%m.%y, %H:%M ", &timeinfo);
  display.println(strftime_buf);

  display.setTextColor(GxEPD_BLACK);

  display.setCursor(0, display.height() - 10);

  char *x = line;
  x += sprintf(x, "SDCard:");
  if (sdOK) {
    uint32_t cardSize = SD.cardSize() / (1024 * 1024);
    uint8_t cardType = SD.cardType();

    if (cardType == CARD_MMC) {
      x += sprintf(x, "MMC ");
    } else if (cardType == CARD_SD) {
      x += sprintf(x, "SDSC ");
    } else if (cardType == CARD_SDHC) {
      x += sprintf(x, "SDHC ");
    } else {
      x += sprintf(x, "UNKNOWN ");
    }
    x += sprintf(x, "%dMB", cardSize);
  } else {
    x += sprintf(x, "None");
  }
  display.println(line);

  display.update();
}

void loop() {
  Serial.println("update display");
  update_display();
  const TickType_t xDelay = 60000 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
