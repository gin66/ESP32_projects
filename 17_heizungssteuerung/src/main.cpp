#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <DS18B20.h>
#include <OneWire.h>
#include <base64.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "template.h"

using namespace std;

// SuperMINI Esp32c3:
// https://www.nologo.tech/product/esp32/esp32C3SuperMini.html
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
const int max_duty = 1 << resolution;
//
// Measure Control Voltage and one-bit-D/A
//    GND-1kOhm-A-10kOhm-B-4.3kOhm-Power
//    B-240Ohm->Heizungkontrollspannung
//    GND-EBC-102Ohm-B
//    GND-100uF-B
//    Basis-1kOhm-GPIO10
//
// Measure Control Voltage at A
//    GND-1kOhm-A-10kOhm-Power
//
// Measure outter temperature sensor
//    GND-Rsensor1-4.3kOhm-3.3V
//    Rsensor2-10kOhm-GPIO1-100nF?-GND
//    Rsensor1-100uF-GND
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

// Uoutter to temperature
//    520 => 12°C
//    450 => 11.2°C
//    440 => 11.1°C
//
// Uoutter_avg:
//    500 => 598 Ohm => 7.0°C
// 
// U = R / (R+4.3k) * 3.3V * 4096/3.3V
// U/4096 = R / (R+4.3k)
// 4096 /U= 1+4.3k/R
// (4096-U)/U = 4.3k/R
// R = 4.3k*U/(4096-U)

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define SCREEN_ADDRESS \
  0x3C  ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

OneWire oneWire(10);
DS18B20 tempsensor(&oneWire);
#define TEMP_OFFSET 4.0

// Control voltage:
//    0-2.5V: Pump off
//    control voltage ~= 0.245*temp - 5.4
//    temp ~= 3.89 * cv + 23.87
uint32_t control_voltage_mV = 0;
#define VORLAUF_TEMP(cv_mV) (((cv_mV) + 6134) / 257)
#define CONTROL_VOLTAGE_MV(temp) (245 * (temp)-5400)

uint16_t Usupply = 0;
uint16_t Uoutter = 0;
uint32_t Uoutter_avg = 0;
uint16_t Ucontrol = 0;
uint32_t Usupply_mV = 0;
uint32_t Ucontrol_mV = 0;
float temp = 0.0;
bool temp_valid = false;
int dutycycle = max_duty;
uint8_t nvs_err = 0;

void write_control_voltage_to_nvs() {
  nvs_handle_t my_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK) {
    nvs_err = 1;
    display.println("NVS open error");
    display.display();
  } else {
    err = nvs_set_u32(my_handle, "control_voltage", control_voltage_mV);
    if (err == ESP_OK) {
      err = nvs_commit(my_handle);
      if (err == ESP_OK) {
        nvs_err = 0;
      } else {
        nvs_err = 5;
      }
    } else {
      nvs_err = 4;
    }
    nvs_close(my_handle);
  }
}

// can be used as parameter to tpl_command_setup
// void execute(enum Command command) {}

// can be used as parameter to tpl_websocket_setup
// void add_ws_info(DynamicJsonDocument* myObject) {}
void publish_func(DynamicJsonDocument *json) {
  (*json)["nvs_err"] = nvs_err;
  (*json)["Usupply"] = Usupply;
  (*json)["Uoutter"] = Uoutter;
  (*json)["Uoutter_avg"] = (Uoutter_avg+128)>>8;
  (*json)["Ucontrol"] = Ucontrol;
  (*json)["Usupply_mV"] = Usupply_mV;
  (*json)["Ucontrol_mV_ist"] = Ucontrol_mV;
  (*json)["Ucontrol_mV_soll"] = control_voltage_mV;
  (*json)["dutycycle"] = dutycycle;
  (*json)["Vorlauf_temp"] = VORLAUF_TEMP(Ucontrol_mV);
  if (temp_valid) {
    (*json)["temp"] = temp;
  }
}

void process_func(DynamicJsonDocument *json) {
  if ((*json).containsKey("control_voltage_mV")) {
    uint32_t cv = (*json)["control_voltage_mV"];
    if (cv != control_voltage_mV) {
      control_voltage_mV = cv;
      write_control_voltage_to_nvs();
    }
  }
}

//---------------------------------------------------
void setup() {
  // Wire.begin(8,9,8000000);
  Wire.begin(8, 9);
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();

  delay(5000);
  tpl_system_setup(0);  // no deep sleep

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Serial");
  display.display();

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println("HERE");

  // Initialize NVS
  display.println("NVS");
  display.display();
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    display.println("NVS erase");
    display.display();
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  nvs_handle_t my_handle;
  err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK) {
    display.println("NVS open error");
    display.display();
    nvs_err = 1;
  } else {
    err = nvs_get_u32(my_handle, "control_voltage", &control_voltage_mV);
    switch (err) {
      case ESP_OK:
        nvs_err = 0;
        break;
      case ESP_ERR_NVS_NOT_FOUND:
        nvs_err = 2;
        // printf("The value is not initialized yet!\n");
        break;
      default:
        nvs_err = 3;
        display.println("NVS read error");
        display.display();
        for (;;) {
        }
    }
  }
  nvs_close(my_handle);
  if (control_voltage_mV > 12000) {
    control_voltage_mV = 0;
  }

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

void display_temp_page(struct tm *timeinfo_p) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print(WiFi.SSID());
  display.print(" ");
  display.print(WiFi.localIP());
  display.print(" ");
  display.println(tpl_fail);

  char strftime_buf[64];
  strftime(strftime_buf, sizeof(strftime_buf), "%d.%m.%y, %H:%M:%S",
           timeinfo_p);
  display.println(strftime_buf);

  display.setTextSize(3);
  if (Ucontrol_mV > 2500) {
    display.print(VORLAUF_TEMP(Ucontrol_mV));
    display.print(
        "\xf7"
        "C");
  } else {
    display.print("AUS");
  }
  display.println();
  if (temp_valid) {
    display.print(temp);
    display.print(
        "\xf7"
        "C");
  } else {
    display.print("???");
  }
  display.display();
}

void display_debug_page(struct tm *timeinfo_p) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print(WiFi.SSID());
  display.print(" ");
  display.print(WiFi.localIP());
  display.print(" ");
  display.println(tpl_fail);

  char strftime_buf[64];
  strftime(strftime_buf, sizeof(strftime_buf), "%d.%m.%y, %H:%M:%S",
           timeinfo_p);
  display.println(strftime_buf);

  display.print("Ucontrol=");
  display.print(Ucontrol);
  display.print("=>");
  display.print(Ucontrol_mV);
  display.println("mV");
  display.print("    Duty:");
  display.print(dutycycle);
  display.print(" => ");
  if (Ucontrol_mV > 2500) {
    display.print(VORLAUF_TEMP(Ucontrol_mV));
    display.print(
        "\xf7"
        "C");
  } else {
    display.println("AUS");
  }
  display.print("Uoutter=");
  display.println((Uoutter_avg+128)>>8);
  display.print("Uin=");
  display.print(Usupply);
  display.print("=>");
  display.print(Usupply_mV);
  display.println("mV");
  if (temp_valid) {
    display.print("Temp=");
    display.println(temp);
  } else {
    display.println("Temp: not working");
  }
  display.display();
}

void loop() {
  Usupply = analogRead(0);
  Uoutter = analogRead(1);
  Ucontrol = analogRead(3);

  Usupply_mV = (Usupply + 14);
  Usupply_mV *= 1000;
  Usupply_mV /= 128;

  Ucontrol_mV = (Ucontrol + 14);
  Ucontrol_mV *= 1000;
  Ucontrol_mV /= 128;

  // Uoutter is 0..<4096
  // Uoutter_avg is 32bit
  Uoutter_avg += Uoutter - (Uoutter_avg >> 8);

  uint32_t ms_now = millis();
  uint32_t delta = ms_now - last_millis;
  if (delta < 100) {
    const TickType_t xDelay = 2 / portTICK_PERIOD_MS;
    vTaskDelay(xDelay);
    return;
  }
  last_millis = ms_now;

  if (Ucontrol_mV > control_voltage_mV + 500) {
    dutycycle = min(dutycycle + 10, max_duty);
    ledcWrite(ledChannel, dutycycle);
  } else if (Ucontrol_mV > control_voltage_mV + 50) {
    dutycycle = min(dutycycle + 1, max_duty);
    ledcWrite(ledChannel, dutycycle);
  } else if (Ucontrol_mV + 500 < control_voltage_mV) {
    dutycycle = max(dutycycle, 10) - 10;
    ledcWrite(ledChannel, dutycycle);
  } else if (Ucontrol_mV + 50 < control_voltage_mV) {
    dutycycle = max(dutycycle, 1) - 1;
    ledcWrite(ledChannel, dutycycle);
  }

  if (temp_requested) {
    temp_requested = false;
    if (tempsensor.isConversionComplete()) {
      temp = tempsensor.getTempC() - TEMP_OFFSET;
      temp_valid = true;
    }
  } else if (tempsensor.isConnected(3)) {
    temp_requested = true;
    tempsensor.requestTemperatures();
  } else {
    temp_valid = false;
  }

  struct tm timeinfo;
  time_t now = time(nullptr);
  localtime_r(&now, &timeinfo);

  if (timeinfo.tm_sec < 30) {
    display_debug_page(&timeinfo);
  } else {
    display_temp_page(&timeinfo);
  }
  display.invertDisplay(Ucontrol_mV > 2500);

  //  const TickType_t xDelay = 1000 / portTICK_PERIOD_MS;
  //  Serial.println("loop");
  esp_task_wdt_reset();
  //  taskYIELD();
  //  vTaskDelay(xDelay);
}
