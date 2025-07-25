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
volatile uint32_t valid_xy = 0;

struct stromzaehler_packet_s stromzaehler_packet[3];
volatile uint32_t valid_strom = 0;

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
    struct XY6020Data *xyd = &xydata[valid_xy % 3];  
    (*json)["xy_cv"] = xyd->cv / 100.0; // V
    (*json)["xy_cc"] = xyd->cc / 100.0; // A
    (*json)["xy_inV"] = xyd->inV / 100.0; // V
    (*json)["xy_actV"] = xyd->actV / 100.0; // V
    (*json)["xy_actC"] = xyd->actC / 100.0; // A
    (*json)["xy_actP"] = xyd->actP / 100.0; // W
    (*json)["xy_charge"] = xyd->charge / 1000.0; // Ah
    (*json)["xy_energy"] = xyd->energy / 1000.0; // Wh
    (*json)["xy_hour"] = xyd->hour;
    (*json)["xy_min"] = xyd->min;
    (*json)["xy_sec"] = xyd->sec;
    (*json)["xy_temp"] = xyd->temp / 10.0; // °C/F
    (*json)["xy_tempExt"] = xyd->tempExt / 10.0; // °C/F
    (*json)["xy_lockOn"] = xyd->lockOn;
    (*json)["xy_protect"] = xyd->protect;
    (*json)["xy_ccActive"] = xyd->ccActive;
    (*json)["xy_cvActive"] = xyd->cvActive;
    (*json)["xy_outputOn"] = xyd->outputOn;
    (*json)["xy_model"] = xyd->model;
    (*json)["xy_version"] = xyd->version;
    (*json)["xy_slaveAdd"] = xyd->slaveAdd;
    (*json)["xy_tempOfs"] = xyd->tempOfs / 10.0; // °C/F
    (*json)["xy_tempExtOfs"] = xyd->tempExtOfs / 10.0; // °C/F
    struct stromzaehler_packet_s *packet = &stromzaehler_packet[valid_strom % 3];
    (*json)["strom_hour"] = packet->tm_hour;
    (*json)["strom_minute"] = packet->tm_min;
    (*json)["strom_second"] = packet->tm_sec;
    (*json)["strom_weekday"] = packet->tm_wday;
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
  Serial1.begin(115200, SERIAL_8N1, 16, 0); // 115200 baud, RX=GPIO16, TX=GPIO00

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

enum class XY_State {
  idle,
  request,
  ongoing
} xy_current = XY_State::idle;
uint32_t last_millis = 0;

void loop() {
  xy.task();

  uint32_t now = millis();
  switch (xy_current) {
    case XY_State::idle:
       if (now-last_millis > 500) {
         last_millis = now;
         xy_current = XY_State::request;
       }
       break;
    case XY_State::request:
       xy.ReadAllHRegs();
       xy_current = XY_State::ongoing;
       break;
    case XY_State::ongoing:
       if (xy.HRegUpdated()) {
         updateStruct(&xydata[(valid_xy+1) % 3]);
         valid_xy++;
         xy_current = XY_State::idle;
       }
  }

  const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);

  // 1-2 ms with 20ms => duty = 1*65536/20..2*65536/20 = 3276.8 .. 6553.6
  //
  // 500..~2700ms = 1638..8850
  ledcWrite(0, duty / 9);

  int packetSize = udp2.parsePacket();
  if (packetSize == sizeof(struct stromzaehler_packet_s)) {
    struct stromzaehler_packet_s *packet = &stromzaehler_packet[(valid_strom+1) % 3];
    udp2.read((uint8_t*)packet, sizeof(struct stromzaehler_packet_s));
    valid_strom++;
    // Use packet data
    Serial.printf("Time: %02d:%02d:%02d, Weekday: %d, Consumption: %.2f Wh, Production: %.2f Wh, Current: %.2f W\n",
                  packet->tm_hour, packet->tm_min, packet->tm_sec, packet->tm_wday,
                  packet->consumption_Wh, packet->production_Wh, packet->current_W);
  }
}
