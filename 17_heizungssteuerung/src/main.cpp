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
//    GPIO7/SS        one-bit-D/A
//    GPIO8/SDA       OLED
//    GPIO9/SCL       OLED
//    GPIO10          temperature sensor DS18B20 clone
//    GPIO22/RX       unused
//    GPIO21/TX       unused
#define CONTROL_PIN 7
const int ledChannel = 0;
const int resolution = 10;
const int freq = 1000;
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
// Supply Voltage: 
//    7V  => 888
//    8V  => 1016
//    9V  => 1135
//    10V => 1260
//    11V => 1388
//    12V => 1515
//    13V => 1643
//    14V => 1771
//    15V => 1901
//    16V => 2030
//    17V => 2159
//    18V => 2286
//    19V => 2414
//    20V => 2542
//    21V => 2670
//    22V => 2800
//    23V => 2926
//    24V => 3055
//  Supply voltage = 0.007825*adc + 0.1113 ~= (adc+14)/128
//
//  Ucontrol Voltage:
//    1823 => 14.378V
//    1918 => 15.096V
//    2010 => 15.814V
//    2106 => 16.532V
//    2159 => 16.962V
//    893  =>  7.046V
//
//  Similar relation as supply voltage 

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

OneWire oneWire(10);
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

  digitalWrite(CONTROL_PIN, HIGH);
  pinMode(CONTROL_PIN, OUTPUT);
  analogReadResolution(12);

  // configure LED PWM functionalitites
  ledcSetup(ledChannel, freq, resolution);

    // attach the channel to the GPIO to be controlled
  ledcAttachPin(CONTROL_PIN, ledChannel);

    Serial.println("Setup done.");
}

bool temp_requested = false;
uint32_t last_millis = 0;
uint16_t dutycycle = 1<<resolution;

void loop() {
  uint16_t Usupply = analogRead(0);
  uint16_t Uoutter = analogRead(1);
  uint16_t Ucontrol = analogRead(3);

  uint32_t Usupply_mV = (Usupply + 14);
  Usupply_mV *= 1000;
  Usupply_mV /= 128;

  uint32_t Ucontrol_mV = (Ucontrol + 14);
  Ucontrol_mV *= 1000;
  Ucontrol_mV /= 128;

  uint32_t ms_now = millis();
  uint32_t delta = ms_now - last_millis;
  if (delta < 100) {
    const TickType_t xDelay = 2 / portTICK_PERIOD_MS;
    vTaskDelay(xDelay);
    return;
  }
  last_millis = ms_now;

  if (Ucontrol_mV > 6000+500) {
    dutycycle+=10;
	 ledcWrite(ledChannel, dutycycle);
  }
  else if (Ucontrol_mV > 6000+50) {
    dutycycle++;
	 ledcWrite(ledChannel, dutycycle);
  }
  else if (Ucontrol_mV < 6000-500) {
	dutycycle-=10;
	 ledcWrite(ledChannel, dutycycle);
  }
  else if (Ucontrol_mV < 6000-50) {
	dutycycle--;
	 ledcWrite(ledChannel, dutycycle);
  }

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
  display.print("=>");
  display.print(Ucontrol_mV);
  display.print("mV: ");
  display.println(dutycycle);
  display.print("Uoutter=");
  display.println(Uoutter);
  display.print("Usupply=");
  display.println(Usupply);
  display.print("=>");
  display.print(Usupply_mV);
  display.println("mV");

  if (temp_requested) {
	  temp_requested = false;
    if(tempsensor.isConversionComplete()) {
	    float temp = tempsensor.getTempC();
	    display.print("Temp=");
	    display.println(temp);
	    Serial.println(temp);
    }
  }
  else if (tempsensor.isConnected(3)) {
    temp_requested = true;
    tempsensor.requestTemperatures();
  }
  else {
	 display.println("Temp: not working");
  }

  display.display();

//  const TickType_t xDelay = 1000 / portTICK_PERIOD_MS;
//  Serial.println("loop");
  esp_task_wdt_reset();
//  taskYIELD();
//  vTaskDelay(xDelay);
}
