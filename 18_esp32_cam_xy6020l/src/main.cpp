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
#include <xy6020l.h>

#include "../../../.private/private_bot.h"
#include "../../../.private/private_sha.h"
#include "esp32-hal-psram.h"

using namespace std;

uint32_t *p = NULL;

WiFiUDP udp2;
IPAddress broadcastIP(255,255,255,255);
xy6020l xy(Serial1, 0x01, 50, XY6020_OPT_SKIP_SAME_HREG_VALUE | XY6020_OPT_NO_HREG_UPDATE);

struct XY6020Data {
    uint16_t cv;       // constant voltage setpoint, LSB: 0.01 V
    uint16_t cc;       // constant current setpoint, LSB: 0.01 A
    uint16_t inV;      // actual input voltage, LSB: 0.01 V
    uint16_t actV;     // actual output voltage, LSB: 0.01 V
    uint16_t actC;     // actual output current, LSB: 0.01 A
    uint16_t actP;     // actual output power, LSB: 0.01 W
    uint16_t charge;   // actual charge, LSB: 0.001 Ah
    uint16_t energy;   // actual energy, LSB: 0.001 Wh
    uint16_t hour;     // output time hours, LSB: 1 h
    uint16_t min;      // output time minutes, LSB: 1 min
    uint16_t sec;      // output time seconds, LSB: 1 sec
    uint16_t temp;     // dcdc temperature, LSB: 0.1°C/F
    uint16_t tempExt;  // external temperature, LSB: 0.1°C/F
    bool lockOn;       // lock switch, true = on
    uint16_t protect;  // protect state
    bool ccActive;     // CC active, true = on
    bool cvActive;     // CV active, true = on
    bool outputOn;     // output switch, true = on
    uint16_t model;    // product number
    uint16_t version;  // version number
    uint16_t slaveAdd; // slave address
    uint16_t tempOfs;  // internal temperature offset
    uint16_t tempExtOfs; // external temperature offset
} xydata[3];
volatile uint8_t valid_xy = 0;

struct stromzaehler_packet_s stromzaehler_packet[3];
volatile uint8_t valid_strom = 0;

void updateStruct(XY6020Data *xyd) {
    xyd->cv = xy.getCV();
    xyd->cc = xy.getCC();
    xyd->inV = xy.getInV();
    xyd->actV = xy.getActV();
    xyd->actC = xy.getActC();
    xyd->actP = xy.getActP();
    xyd->charge = xy.getCharge();
    xyd->energy = xy.getEnergy();
    xyd->hour = xy.getHour();
    xyd->min = xy.getMin();
    xyd->sec = xy.getSec();
    xyd->temp = xy.getTemp();
    xyd->tempExt = xy.getTempExt();
    xyd->lockOn = xy.getLockOn();
    xyd->protect = xy.getProtect();
    xyd->ccActive = xy.isCC();
    xyd->cvActive = xy.isCV();
    xyd->outputOn = xy.getOutputOn();
    xyd->model = xy.getModel();
    xyd->version = xy.getVersion();
    xyd->slaveAdd = xy.getSlaveAdd();
    xyd->tempOfs = xy.getTempOfs();
    xyd->tempExtOfs = xy.getTempExtOfs();
}

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

uint16_t duty = 0;

void publish(DynamicJsonDocument *json) {
    struct XY6020Data *xyd = &xydata[valid_xy];  
    (*json)["cv"] = xyd->cv / 100.0; // V
    (*json)["cc"] = xyd->cc / 100.0; // A
    (*json)["inV"] = xyd->inV / 100.0; // V
    (*json)["actV"] = xyd->actV / 100.0; // V
    (*json)["actC"] = xyd->actC / 100.0; // A
    (*json)["actP"] = xyd->actP / 100.0; // W
    (*json)["charge"] = xyd->charge / 1000.0; // Ah
    (*json)["energy"] = xyd->energy / 1000.0; // Wh
    (*json)["hour"] = xyd->hour;
    (*json)["min"] = xyd->min;
    (*json)["sec"] = xyd->sec;
    (*json)["temp"] = xyd->temp / 10.0; // °C/F
    (*json)["tempExt"] = xyd->tempExt / 10.0; // °C/F
    (*json)["lockOn"] = xyd->lockOn;
    (*json)["protect"] = xyd->protect;
    (*json)["ccActive"] = xyd->ccActive;
    (*json)["cvActive"] = xyd->cvActive;
    (*json)["outputOn"] = xyd->outputOn;
    (*json)["model"] = xyd->model;
    (*json)["version"] = xyd->version;
    (*json)["slaveAdd"] = xyd->slaveAdd;
    (*json)["tempOfs"] = xyd->tempOfs / 10.0; // °C/F
    (*json)["tempExtOfs"] = xyd->tempExtOfs / 10.0; // °C/F
    struct stromzaehler_packet_s *packet = &stromzaehler_packet[valid_strom];
    (*json)["time"]["hour"] = packet->tm_hour;
    (*json)["time"]["minute"] = packet->tm_min;
    (*json)["time"]["second"] = packet->tm_sec;
    (*json)["weekday"] = packet->tm_wday;
    (*json)["consumption_Wh"] = packet->consumption_Wh;
    (*json)["production_Wh"] = packet->production_Wh;
    (*json)["current_W"] = packet->current_W;
}

void servo_update(DynamicJsonDocument *json) {
  if (json->containsKey("servo")) {
    duty = (*json)["servo"];
  }
}

void setup() {
  tpl_system_setup(10);  // 10secs deep sleep time

  // turn flash light off
  digitalWrite(tpl_flashPin, LOW);
  pinMode(tpl_flashPin, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial1.begin(115200, SERIAL_8N1, 16, 0); // 9600 baud, RX=GPIO16, TX=GPIO00

  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(publish, servo_update);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

  if (psramFound()) {
    Serial.println("PSRAM found and loaded");
  }

  udp2.begin(STROMZAEHLER_PORT);

  ledcSetup(0, 50, 16);
  ledcAttachPin(tpl_flashPin, 0);

  Serial.println("Done.");
}

bool requested = false;

void loop() {
  xy.task();

  if (!requested) {
    xy.ReadAllHRegs();
  }
  else if (xy.HRegUpdated()) {
    uint8_t newvalid_xy = (valid_xy >= 2) ? 0 : valid_xy+1;
    updateStruct(&xydata[newvalid_xy]);
    valid_xy = newvalid_xy;
  }

  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);

  // 1-2 ms with 20ms => duty = 1*65536/20..2*65536/20 = 3276.8 .. 6553.6
  //
  // 500..~2700ms = 1638..8850
  ledcWrite(0, duty / 9);

  int packetSize = udp2.parsePacket();
  if (packetSize == sizeof(struct stromzaehler_packet_s)) {
    uint8_t newvalid = (valid_strom >= 2) ? 0 : valid_strom+1;
    struct stromzaehler_packet_s *packet = &stromzaehler_packet[newvalid];
    udp2.read((uint8_t*)packet, sizeof(struct stromzaehler_packet_s));
    valid_strom = newvalid;
    // Use packet data
    Serial.printf("Time: %02d:%02d:%02d, Weekday: %d, Consumption: %.2f Wh, Production: %.2f Wh, Current: %.2f W\n",
                  packet->tm_hour, packet->tm_min, packet->tm_sec, packet->tm_wday,
                  packet->consumption_Wh, packet->production_Wh, packet->current_W);
  }
}
