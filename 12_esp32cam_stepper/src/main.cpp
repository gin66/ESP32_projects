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
#include <FastAccelStepper.h>

#include "../../private_bot.h"
#include "../../private_sha.h"
#include "esp32-hal-psram.h"

using namespace std;

// Gewindestange hat 2.54mm pro 2 Umdrehungen.
// 3200 steps/Umdrehung
//
// 0.397 um/step
// 5mm/s =  ~12600 steps/s
// 8mm/s =  ~20150 steps/s => 2m in 4min
//
// Max position is ~3884000
//
#define MAX_POSITION 3884000
#define MAX_SPEED_IN_HZ 20150
#define ACCELERATION 32000
#define dirPinStepper1     15
#define stepPinStepper1    14
#define enablePinStepper1  13
#define endSwitch1		    2

FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper1 = NULL;

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

volatile bool isHoming = true;
volatile bool hitSwitch = false;
volatile bool homed = false;

void move_publish(DynamicJsonDocument *json) {
	(*json)["position"] = stepper1->getCurrentPosition();
}

void move_update(DynamicJsonDocument *json) {
  if (json->containsKey("move1")) {
	int32_t steps = (*json)["move1"];
	stepper1->move(steps);
	isHoming = true;
  }
  if (json->containsKey("speed")) {
	uint32_t speed = (*json)["speed"];
	stepper1->setSpeedInHz(speed);
	stepper1->applySpeedAcceleration();
  }
}

void stopISR() {
	if (isHoming) {
		stepper1->forceStopAndNewPosition(0);
		isHoming = false;
		hitSwitch = true;
	}
}

void setup() {
  tpl_system_setup(10);  // 10secs deep sleep time

  // turn flash light off
  digitalWrite(tpl_flashPin, LOW);
  pinMode(tpl_flashPin, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(move_publish, move_update);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
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
    stepper1->setSpeedInHz(3200);
    stepper1->setAcceleration(ACCELERATION);
  }

  pinMode(endSwitch1, INPUT);
  attachInterrupt(endSwitch1, stopISR, RISING);

  Serial.println("Done.");
}

void loop() {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
  if (hitSwitch) {
      const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
      vTaskDelay(xDelay);
	  hitSwitch = false;
	  homed = true;
	  stepper1->moveTo(32000);
  }
  if (homed) {
	  int32_t pos = stepper1->getCurrentPosition();
	  if (pos > MAX_POSITION/2) {
		  pos = MAX_POSITION - pos;
	  }
	  if (pos > 1000000) {
		  stepper1->setSpeedInHz(30000);
	  }
	  else if (pos > 500000) {
		  stepper1->setSpeedInHz(12800);
	  }
	  else if (pos > 250000) {
		  stepper1->setSpeedInHz(6400);
	  }
	  else {
		  stepper1->setSpeedInHz(3200);
	  }
	  stepper1->applySpeedAcceleration();
  }
  else {
	  stepper1->setSpeedInHz(3200);
	  stepper1->applySpeedAcceleration();
  }
}
