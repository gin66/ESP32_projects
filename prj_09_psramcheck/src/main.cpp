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
  tpl_system_setup(10);  // 10secs deep sleep time

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
#define MAGIC 0xdeadbeaf
#define PROBE_SIZE (1024)
  uint32_t *p = (uint32_t *) ps_malloc(PROBE_SIZE);
  if (tpl_config.bootCount == 1) {
	  // init RAM
	for (uint32_t i = 0;i < PROBE_SIZE/4;i++) {
		*p++ = MAGIC ^ ((i << 16) + i);
	}
    // enter deep sleep for restart
    tpl_command = CmdDeepSleep;
  }
  else {
	uint32_t good = 0;
	uint32_t bad = 0;
	for (uint32_t i = 0;i < PROBE_SIZE/4;i++) {
		if (*p++ == MAGIC ^ ((i << 16) + i)) {
			good++;
		}
		else {
			bad++;
		}
	}
	char buf[100];
	sprintf(buf, "good=%d,, bad=%d", good,bad);
	tpl_config.bot_message = buf;
	tpl_config.bot_send_message = true;
  }
  print_info();

  Serial.println("Done.");
}

void loop() {
  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
