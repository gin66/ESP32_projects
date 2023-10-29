#include <Arduino.h>
#include <base64.h>
#include <OneWire.h>
#include <DS18B20.h>
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

  delay(5000);
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println("HERE");

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)255 /*tpl_ledPin*/);
  tpl_webserver_setup();
  tpl_websocket_setup(publish_func, process_func);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  tempsensor.begin(10);
  tempsensor.setResolution(12);

  Serial.println("Setup done.");
}


void loop() {
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
