#include "template.h"
#include <NeoPixelBus.h>
#include <FS.h>
#include <SPIFFS.h>
#include <stdint.h>

#include "led_effects.h"

#define MATRIX_LED_PIN 16
#define MATRIX_WIDTH 32
#define MATRIX_HEIGHT 32
#define MATRIX_PIXEL_COUNT (MATRIX_WIDTH * MATRIX_HEIGHT)

NeoPixelBus<NeoGrbFeature, NeoEsp32Rmt0Ws2812xMethod> matrix(MATRIX_PIXEL_COUNT, MATRIX_LED_PIN);

static volatile LedMode currentMode = ModeWhite;
static volatile uint8_t ledBrightness = 5;
static volatile uint8_t staticR = 255;
static volatile uint8_t staticG = 255;
static volatile uint8_t staticB = 255;
static volatile uint32_t rainbowSpeed = 3000;

static LedColor pixelBuffer[MATRIX_PIXEL_COUNT];
static unsigned long startTime = 0;

void writePixelBufferToMatrix() {
  for (uint16_t i = 0; i < MATRIX_PIXEL_COUNT; i++) {
    uint16_t x = i % MATRIX_WIDTH;
    uint16_t y = i / MATRIX_WIDTH;
    uint16_t target_i = xyToIndex(x, y, MATRIX_WIDTH, MATRIX_HEIGHT);
    matrix.SetPixelColor(target_i, RgbColor(pixelBuffer[i].R, pixelBuffer[i].G, pixelBuffer[i].B));
  }
  matrix.Show();
}

const char* getModeString() {
  switch (currentMode) {
    case ModeOff: return "off";
    case ModeStatic: return "static";
    case ModeRainbow: return "rainbow";
    case ModeRainbowWave: return "wave";
    case ModeWhite: return "white";
    case ModeClock: return "clock";
    case ModeScanner: return "scanner";
    case ModeRawScanner: return "rawscanner";
    default: return "unknown";
  }
}

void processLedCommand(DynamicJsonDocument* json) {
  if (json->containsKey("command")) {
    String cmd = (*json)["command"].as<String>();
    
    if (cmd == "mode") {
      String mode = (*json)["value"].as<String>();
      if (mode == "off") currentMode = ModeOff;
      else if (mode == "rainbow") currentMode = ModeRainbow;
      else if (mode == "wave") currentMode = ModeRainbowWave;
      else if (mode == "white") currentMode = ModeWhite;
      else if (mode == "static") currentMode = ModeStatic;
      else if (mode == "clock") currentMode = ModeClock;
      else if (mode == "scanner") currentMode = ModeScanner;
      else if (mode == "rawscanner") currentMode = ModeRawScanner;
    }
    else if (cmd == "brightness") {
      int val = (*json)["value"];
      ledBrightness = (uint8_t)((val * 255) / 100);
    }
    else if (cmd == "color") {
      staticR = (*json)["r"];
      staticG = (*json)["g"];
      staticB = (*json)["b"];
      currentMode = ModeStatic;
    }
    else if (cmd == "rainbow_speed") {
      rainbowSpeed = (*json)["value"];
    }
  }
}

void publishLedStatus(DynamicJsonDocument* json) {
  unsigned long elapsed = millis() - startTime;
  unsigned long seconds = elapsed / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  
  char uptime[32];
  snprintf(uptime, sizeof(uptime), "%02lu:%02lu:%02lu", hours, minutes % 60, seconds % 60);
  
  (*json)["uptime"] = uptime;
  (*json)["mode"] = getModeString();
  (*json)["brightness"] = (int)((ledBrightness * 100) / 255);
  (*json)["rainbow_speed"] = rainbowSpeed;
  (*json)["matrix_width"] = MATRIX_WIDTH;
  (*json)["matrix_height"] = MATRIX_HEIGHT;
}

void handleLedCommand(enum Command cmd) {
}

extern WebServer tpl_server;

void addLedEndpoints() {
  tpl_server.on("/led/off", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    currentMode = ModeOff;
    tpl_server.send(200, "text/html", "OK");
  });
  
  tpl_server.on("/led/static", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    if (tpl_server.hasArg("r")) staticR = tpl_server.arg("r").toInt();
    if (tpl_server.hasArg("g")) staticG = tpl_server.arg("g").toInt();
    if (tpl_server.hasArg("b")) staticB = tpl_server.arg("b").toInt();
    if (tpl_server.hasArg("brightness")) ledBrightness = tpl_server.arg("brightness").toInt();
    currentMode = ModeStatic;
    tpl_server.send(200, "text/html", "OK");
  });
  
  tpl_server.on("/led/rainbow", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    if (tpl_server.hasArg("brightness")) ledBrightness = tpl_server.arg("brightness").toInt();
    currentMode = ModeRainbow;
    tpl_server.send(200, "text/html", "OK");
  });
  
  tpl_server.on("/led/wave", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    if (tpl_server.hasArg("brightness")) ledBrightness = tpl_server.arg("brightness").toInt();
    currentMode = ModeRainbowWave;
    tpl_server.send(200, "text/html", "OK");
  });
  
  tpl_server.on("/led/white", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    if (tpl_server.hasArg("brightness")) ledBrightness = tpl_server.arg("brightness").toInt();
    currentMode = ModeWhite;
    tpl_server.send(200, "text/html", "OK");
  });
  
  tpl_server.on("/led/clock", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    if (tpl_server.hasArg("brightness")) ledBrightness = tpl_server.arg("brightness").toInt();
    currentMode = ModeClock;
    tpl_server.send(200, "text/html", "OK");
  });
  
  tpl_server.on("/led/scanner", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    if (tpl_server.hasArg("brightness")) ledBrightness = tpl_server.arg("brightness").toInt();
    currentMode = ModeScanner;
    tpl_server.send(200, "text/html", "OK");
  });
  
  tpl_server.on("/led/rawscanner", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    if (tpl_server.hasArg("brightness")) ledBrightness = tpl_server.arg("brightness").toInt();
    currentMode = ModeRawScanner;
    tpl_server.send(200, "text/html", "OK");
  });
  
  tpl_server.on("/led/status", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    char buf[128];
    snprintf(buf, sizeof(buf), 
             "{\"mode\":\"%s\",\"brightness\":%d,\"r\":%d,\"g\":%d,\"b\":%d}",
             getModeString(), ledBrightness, staticR, staticG, staticB);
    tpl_server.send(200, "application/json", buf);
  });
}

void setup() {
  tpl_system_setup(0);
  Serial.begin(115200);
  
  startTime = millis();
  
  matrix.Begin();
  for (uint16_t i = 0; i < MATRIX_PIXEL_COUNT; i++) {
    uint16_t x = i % MATRIX_WIDTH;
    uint16_t y = i / MATRIX_WIDTH;
    pixelBuffer[i] = LedColor(x / 8, y / 8, ((x + y) / 8) % 8);
  }
  writePixelBufferToMatrix();
  
  tpl_wifi_setup(true, true, (gpio_num_t)255);
  addLedEndpoints();
  tpl_webserver_setup();
  tpl_websocket_setup(publishLedStatus, processLedCommand);
  tpl_net_watchdog_setup();
  tpl_command_setup(handleLedCommand);
  
  pinMode(tpl_ledPin, OUTPUT);
  digitalWrite(tpl_ledPin, LOW);
  
  Serial.printf("LED Matrix %dx%d initialized\n", MATRIX_WIDTH, MATRIX_HEIGHT);
}

uint32_t last_LED = 0;
bool ledState = false;

void loop() {
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - startTime;
  
  if ((uint32_t)(currentTime - last_LED) > 500) {
     last_LED = currentTime;
     ledState = !ledState;
     digitalWrite(tpl_ledPin, ledState ? HIGH : LOW);
  }

  if (currentMode == ModeRawScanner) {
    uint32_t msPerLed = 200;
    uint16_t pos = (elapsed / msPerLed) % MATRIX_PIXEL_COUNT;
    for (uint16_t i = 0; i < MATRIX_PIXEL_COUNT; i++) {
      matrix.SetPixelColor(i, (i == pos) ? RgbColor(ledBrightness, ledBrightness, ledBrightness) : RgbColor(0, 0, 0));
    }
    matrix.Show();
  } else {
    calculateAllPixels(
      pixelBuffer,
      MATRIX_WIDTH,
      MATRIX_HEIGHT,
      elapsed,
      currentMode,
      ledBrightness,
      staticR, staticG, staticB,
      rainbowSpeed
    );
    writePixelBufferToMatrix();
  }
  vTaskDelay(20 / portTICK_PERIOD_MS);
}
