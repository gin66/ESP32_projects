#include <GxEPD.h>
#include <GxGDEH0213B73/GxGDEH0213B73.h>  // 2.13" b/w newer panel

#include "template.h"
// FreeFonts from Adafruit_GFX
#include <FastAccelStepper.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxIO/GxIO.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <SD.h>
#include <SPI.h>
#include <qrcode.h>

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

FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper1 = NULL;
FastAccelStepper *stepper2 = NULL;

void move_update(DynamicJsonDocument *json) {
  if (json->containsKey("moveBoth")) {
    int32_t steps = (*json)["moveBoth"];
    stepper1->move(steps);
    stepper2->move(steps);
  }
  if (json->containsKey("move1")) {
    int32_t steps = (*json)["move1"];
    stepper1->move(steps);
  }
  if (json->containsKey("move2")) {
    int32_t steps = (*json)["move2"];
    stepper2->move(steps);
  }
  if (json->containsKey("speed")) {
    uint32_t speed = (*json)["speed"];
    stepper1->setSpeedInHz(speed);
    stepper2->setSpeedInHz(speed);
  }
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
  tpl_websocket_setup(NULL, move_update);
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

  engine.init();
  stepper1 = engine.stepperConnectToPin(stepPinStepper1);
  if (stepper1) {
    stepper1->setDirectionPin(dirPinStepper1, true);
    stepper1->setEnablePin(enablePinStepper1);
    stepper1->setAutoEnable(true);

    // If auto enable/disable need delays, just add (one or both):
    // stepper1->setDelayToEnable(50);
    // stepper1->setDelayToDisable(1000);

    stepper1->setSpeedInHz(MAX_SPEED_IN_HZ);
    stepper1->setAcceleration(ACCELERATION);
  }
  stepper2 = engine.stepperConnectToPin(stepPinStepper2);
  if (stepper2) {
    stepper2->setDirectionPin(dirPinStepper2, false);
    stepper2->setEnablePin(enablePinStepper2);
    stepper2->setAutoEnable(true);

    // If auto enable/disable need delays, just add (one or both):
    // stepper2->setDelayToEnable(50);
    // stepper2->setDelayToDisable(1000);

    stepper2->setSpeedInHz(MAX_SPEED_IN_HZ);
    stepper2->setAcceleration(ACCELERATION);
  }
  if (!stepper1 || !stepper2) {
    while (0 == 0) {
      Serial.println("FAILED STEPPER INIT");
      delay(1000);
    }
  }
  // stepper1->move(1000);
  // stepper2->move(1000);
  Serial.println("Setup done.");
}

void update_display() {
  // Create the QR code 29*29
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, 0, "HELLO WORLD");

  display.fillScreen(GxEPD_WHITE);

  display.setCursor(0, 10);

  display.println(WiFi.SSID());

  display.println(WiFi.localIP());

  char strftime_buf[64];
  struct tm timeinfo;
  time_t now = time(nullptr);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%d.%m.%y, %H:%M ", &timeinfo);
  display.println(strftime_buf);

  display.setTextColor(GxEPD_BLACK);

  display.setCursor(0, display.height() - 10);

  uint8_t x_off = display.width() - qrcode.size * 2;
  uint8_t y_off = display.height() - qrcode.size * 2;
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        display.drawPixel(x_off + 2 * x, y_off + 2 * y, GxEPD_BLACK);
        display.drawPixel(x_off + 2 * x + 1, y_off + 2 * y, GxEPD_BLACK);
        display.drawPixel(x_off + 2 * x, y_off + 2 * y + 1, GxEPD_BLACK);
        display.drawPixel(x_off + 2 * x + 1, y_off + 2 * y + 1, GxEPD_BLACK);
      }
    }
  }

  if (sdOK) {
    uint32_t cardSize = SD.cardSize() / (1024 * 1024);
    display.println("SDCard:" + String(cardSize) + "MB");
  } else {
    display.println("SDCard  None");
  }
  display.update();
}

void loop() {
  Serial.println("update display");
  update_display();
  const TickType_t xDelay = 60000 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
