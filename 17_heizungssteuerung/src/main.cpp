#include <Arduino.h>
#include <base64.h>
#include <OneWire.h>
#include <DS18B20.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_log.h>
#include "template.h"
#include <esp_task_wdt.h>

using namespace std;

// SuperMINI Esp32c3: https://www.nologo.tech/product/esp32/esp32C3SuperMini.html
//
//               USB
//    5V                 GPIO5/MISO/A5
//    GND                GPIO6/MOSI
//    3V3                GPIO7/SS
//    GPIO4/A4           GPIO8/SDA
//    GPIO3/A3           GPIO9/SCL
//    GPIO2/A2           GPIO10
//    GPIO1/A1           GPIO22/RX
//    GPIO0/A0           GPIO21/TX

// Usage:
//    GPIO4/A4        unused
//    GPIO3/A3        Measure Control voltage
//    GPIO2/A2        unused
//    GPIO1/A1        Measure outter temperature sensor
//    GPIO0/A0        Measure supply voltage
//    GPIO5/MISO/A5   unused
//    GPIO6/MOSI      unused
//    GPIO7/SS        temperature sensor DS18B20 clone
//    GPIO8/SDA       OLED
//    GPIO9/SCL       OLED
//    GPIO10          one-bit-D/A
//    GPIO22/RX       unused
//    GPIO21/TX       unused
//
// Measure Control Voltage and one-bit-D/A
//    GND-1kOhm-A-10kOhm-B-4.3kOhm-Power
//    B-240Ohm->Heizungkontrollspannung
//    GND-EBC-102Ohm-B
//    GND-100uF-B
//    Basis-1kOhm-GPIO10
//
//
// Measure Control Voltage at A
//    GND-1kOhm-A-10kOhm-Power
//
// Measure outter temperature sensor
//    GND-Rsensor1-4.3kOhm-3.3V
//    Rsensor2-10kOhm-GPIO1
//

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#define LOGO_HEIGHT   16
#define LOGO_WIDTH    16
static const unsigned char PROGMEM logo_bmp[] =
{ 0b00000000, 0b11000000,
  0b00000001, 0b11000000,
  0b00000001, 0b11000000,
  0b00000011, 0b11100000,
  0b11110011, 0b11100000,
  0b11111110, 0b11111000,
  0b01111110, 0b11111111,
  0b00110011, 0b10011111,
  0b00011111, 0b11111100,
  0b00001101, 0b01110000,
  0b00011011, 0b10100000,
  0b00111111, 0b11100000,
  0b00111111, 0b11110000,
  0b01111100, 0b11110000,
  0b01110000, 0b01110000,
  0b00000000, 0b00110000 };

OneWire oneWire(7);
DS18B20 tempsensor(&oneWire);

// can be used as parameter to tpl_command_setup
// void execute(enum Command command) {}

// can be used as parameter to tpl_websocket_setup
// void add_ws_info(DynamicJsonDocument* myObject) {}
void publish_func(DynamicJsonDocument *json) {
	Serial.println("publish_func");
//  (*json)["aport"] = analogRead(34);
//  (*json)["dport"] = digitalRead(34);
}

void process_func(DynamicJsonDocument *json) {
  if ((*json).containsKey("request_minute")) {
//    request_idx = (*json)["request_minute"];
  }
}

//---------------------------------------------------
void setup() {
  tpl_system_setup(0);  // no deep sleep
			//
  //Wire.begin(8,9,8000000);
  Wire.begin(8,9);
    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();

  delay(5000);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Serial");
  display.display();

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println("HERE");

  // Wait OTA
  display.println("WiFi");
  display.display();
  tpl_wifi_setup(true, true, (gpio_num_t)255 /*tpl_ledPin*/);

  display.println("Webserver");
  display.display();
  tpl_webserver_setup();

  display.println("Websocket");
  display.display();
  tpl_websocket_setup(publish_func, process_func);

  display.println("Time");
  display.display();
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  display.println("Tempsensor");
  display.display();
  tempsensor.begin(10);
  tempsensor.setResolution(12);

  Serial.println("Setup done.");
}

#define XPOS   0 // Indexes into the 'icons' array in function below
#define YPOS   1
#define DELTAY 2

void loop() {
  analogReadResolution(12);
  uint16_t Ucontrol = analogRead(3);
  uint16_t Uoutter = analogRead(1);
  uint16_t Usupply = analogRead(0);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print(WiFi.SSID());
  display.print(" ");
  display.println(WiFi.localIP());

  char strftime_buf[64];
  struct tm timeinfo;
  time_t now = time(nullptr);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%d.%m.%y, %H:%M ", &timeinfo);
  display.println(strftime_buf);

  display.print("Ucontrol=");
  display.println(Ucontrol);
  display.print("Uoutter=");
  display.println(Uoutter);
  display.print("Usupply=");
  display.println(Usupply);
  display.display();

  const TickType_t xDelay = 1000 / portTICK_PERIOD_MS;
  Serial.println("loop");
  esp_task_wdt_reset();
//  taskYIELD();
  vTaskDelay(xDelay);
//  tempsensor.requestTemperatures();
//  while(!tempsensor.isConversionComplete()) {
//  }
//  float temp = tempsensor.getTempC();
//  Serial.println(temp);
}
