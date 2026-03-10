#include "template.h"
#include <NeoPixelBus.h>
#include <FS.h>
#include <SPIFFS.h>
#include <stdint.h>
#include <esp_task_wdt.h>

#include "led_effects.h"

#define WDT_TIMEOUT_S 10

#define PANELS 3
#define MATRIX_LED_PIN 16
#define MATRIX_WIDTH 32
#define MATRIX_HEIGHT (8*PANELS)
#define MATRIX_PIXEL_COUNT (MATRIX_WIDTH * MATRIX_HEIGHT)

// 100% Brightness red  =255 for 32x8 LEDs: 3.37A
// 100% Brightness blue =255 for 32x8 LEDs: 3.34A
// 100% Brightness green=255 for 32x8 LEDs: 3.19A
// all off with 32x8:   322mA
// all off with 32x24:  580mA
// 10% Brightness, red/blue/green=255, 32x24 => 3.4A


NeoPixelBus<NeoGrbFeature, NeoEsp32Rmt0Ws2812xMethod> matrix(MATRIX_PIXEL_COUNT, MATRIX_LED_PIN);

static volatile LedMode currentMode = ModeClock;
static volatile uint8_t ledBrightness = 5;
static volatile uint8_t staticR = 255;
static volatile uint8_t staticG = 255;
static volatile uint8_t staticB = 255;
static volatile uint32_t rainbowSpeed = 3000;
static volatile unsigned long scannerStartTime = 0;
static volatile float maxCurrent = 1.0;
static volatile float currentA = 0.0;

static LedColor pixelBuffer[MATRIX_PIXEL_COUNT];
static unsigned long startTime = 0;

void writePixelBufferToMatrix(uint8_t scale) {
  for (uint16_t i = 0; i < MATRIX_PIXEL_COUNT; i++) {
    uint16_t x = i % MATRIX_WIDTH;
    uint16_t y = i / MATRIX_WIDTH;
    uint16_t target_i = xyToIndex(x, y, MATRIX_WIDTH, MATRIX_HEIGHT);
    matrix.SetPixelColor(target_i, RgbColor(
      (uint16_t)pixelBuffer[i].R * scale / 255,
      (uint16_t)pixelBuffer[i].G * scale / 255,
      (uint16_t)pixelBuffer[i].B * scale / 255
    ));
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
      else if (mode == "scanner") { currentMode = ModeScanner; scannerStartTime = millis(); }
      else if (mode == "rawscanner") { currentMode = ModeRawScanner; scannerStartTime = millis(); }
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
    else if (cmd == "max_current") {
      maxCurrent = (*json)["value"];
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
  (*json)["current"] = round(currentA * 100) / 100;
  (*json)["max_current"] = round(maxCurrent * 100) / 100;
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
    scannerStartTime = millis();
    tpl_server.send(200, "text/html", "OK");
  });
  
  tpl_server.on("/led/rawscanner", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    if (tpl_server.hasArg("brightness")) ledBrightness = tpl_server.arg("brightness").toInt();
    currentMode = ModeRawScanner;
    scannerStartTime = millis();
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
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);
  tpl_system_setup(0);
  Serial.begin(115200);
  
  startTime = millis();
  
  matrix.Begin();
  for (uint16_t i = 0; i < MATRIX_PIXEL_COUNT; i++) {
    uint16_t x = i % MATRIX_WIDTH;
    uint16_t y = i / MATRIX_WIDTH;
    pixelBuffer[i] = LedColor(0,0,0);
    if ((x < 8) && (y < 8)) {
       pixelBuffer[i] = LedColor(x, y, 0);
    }
  }
  writePixelBufferToMatrix(255);
  
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

static uint8_t calculateCurrentScale() {
  uint8_t numPanels = PANELS;
  uint32_t baseCurrentUa = 322000 + (numPanels - 1) * 129000;
  uint32_t maxCurrentUa = (uint32_t)(maxCurrent * 1000000);
  
  if (maxCurrentUa <= baseCurrentUa) return 0;
  
  uint32_t estimatedUa = estimateCurrent(pixelBuffer, MATRIX_PIXEL_COUNT, numPanels, 255);
  if (estimatedUa <= maxCurrentUa) return 255;
  
  uint32_t maxLedCurrentUa = maxCurrentUa - baseCurrentUa;
  uint32_t currentLedCurrentUa = estimatedUa - baseCurrentUa;
  
  return (uint8_t)((maxLedCurrentUa * 255) / currentLedCurrentUa);
}

void turnOffLeds() {
  for (uint16_t i = 0; i < MATRIX_PIXEL_COUNT; i++) {
    matrix.SetPixelColor(i, RgbColor(0, 0, 0));
  }
  matrix.Show();
}

void loop() {
  esp_task_wdt_reset();
  
  if (tpl_config.ota_ongoing) {
    turnOffLeds();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    return;
  }
  
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - startTime;
  
  if ((uint32_t)(currentTime - last_LED) > 500) {
     last_LED = currentTime;
     ledState = !ledState;
     digitalWrite(tpl_ledPin, ledState ? HIGH : LOW);
  }

  uint8_t numPanels = MATRIX_HEIGHT / 8;
  uint8_t scale = 255;

  if (currentMode == ModeRawScanner) {
    uint32_t msPerLed = 200;
    unsigned long scanElapsed = currentTime - scannerStartTime;
    uint16_t pos = (scanElapsed / msPerLed) % MATRIX_PIXEL_COUNT;
    for (uint16_t i = 0; i < MATRIX_PIXEL_COUNT; i++) {
      pixelBuffer[i] = (i == pos) ? LedColor(ledBrightness, ledBrightness, ledBrightness) : LedColor(0, 0, 0);
    }
    scale = calculateCurrentScale();
    currentA = estimateCurrent(pixelBuffer, MATRIX_PIXEL_COUNT, numPanels, scale) / 1000000.0f;
    for (uint16_t i = 0; i < MATRIX_PIXEL_COUNT; i++) {
      uint16_t x = i % MATRIX_WIDTH;
      uint16_t y = i / MATRIX_WIDTH;
      uint16_t target_i = xyToIndex(x, y, MATRIX_WIDTH, MATRIX_HEIGHT);
      matrix.SetPixelColor(target_i, RgbColor(
        (uint16_t)pixelBuffer[i].R * scale / 255,
        (uint16_t)pixelBuffer[i].G * scale / 255,
        (uint16_t)pixelBuffer[i].B * scale / 255
      ));
    }
    matrix.Show();
  } else {
    unsigned long effectTime = (currentMode == ModeScanner) ? (currentTime - scannerStartTime) : elapsed;
    calculateAllPixels(
      pixelBuffer,
      MATRIX_WIDTH,
      MATRIX_HEIGHT,
      effectTime,
      currentMode,
      ledBrightness,
      staticR, staticG, staticB,
      rainbowSpeed
    );
    scale = calculateCurrentScale();
    currentA = estimateCurrent(pixelBuffer, MATRIX_PIXEL_COUNT, numPanels, scale) / 1000000.0f;
    writePixelBufferToMatrix(scale);
  }
  vTaskDelay(20 / portTICK_PERIOD_MS);
}
