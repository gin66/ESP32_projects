#include "template.h"
#include <NeoPixelBus.h>
#include <FS.h>
#include <SPIFFS.h>
#include <stdint.h>
#include <esp_task_wdt.h>

#define WDT_TIMEOUT_S 10

#define PANELS 3
#define MATRIX_LED_PIN 16
#define MATRIX_WIDTH 32
#define MATRIX_HEIGHT (8*PANELS)
#define MATRIX_PIXEL_COUNT (MATRIX_WIDTH * MATRIX_HEIGHT)

#include "led_effects.h"

NeoPixelBus<NeoGrbFeature, NeoEsp32Rmt0Ws2812xMethod> matrix(MATRIX_PIXEL_COUNT, MATRIX_LED_PIN);

static volatile LedMode currentMode = ModeRainbow;
static volatile uint8_t ledBrightness = 100;
static volatile uint8_t staticR = 255;
static volatile uint8_t staticG = 255;
static volatile uint8_t staticB = 255;
static volatile BgStyle bgStyle = BgRings;
static volatile uint32_t bgSpeed = 3000;
static volatile uint16_t waveLength = 256;
static volatile bool showClock = true;
static volatile uint8_t clockBrightness = 100;
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
    case ModeWhite: return "white";
    case ModeScanner: return "scanner";
    default: return "unknown";
  }
}

const char* getBgStyleString() {
  switch (bgStyle) {
    case BgSolid: return "solid";
    case BgTrapezoid: return "trapezoid";
    case BgRings: return "rings";
    case BgWave: return "wave";
    case BgRipple: return "ripple";
    default: return "unknown";
  }
}

BgStyle parseBgStyle(const String& s) {
  if (s == "solid") return BgSolid;
  if (s == "trapezoid") return BgTrapezoid;
  if (s == "rings") return BgRings;
  if (s == "wave") return BgWave;
  if (s == "ripple") return BgRipple;
  return BgRings;
}

void processLedCommand(DynamicJsonDocument* json) {
  if (json->containsKey("command")) {
    String cmd = (*json)["command"].as<String>();
    
    if (cmd == "mode") {
      String mode = (*json)["value"].as<String>();
      if (mode == "off") currentMode = ModeOff;
      else if (mode == "rainbow") currentMode = ModeRainbow;
      else if (mode == "white") currentMode = ModeWhite;
      else if (mode == "static") currentMode = ModeStatic;
      else if (mode == "scanner") { currentMode = ModeScanner; scannerStartTime = millis(); }
    }
    else if (cmd == "brightness") {
      ledBrightness = (uint8_t)(*json)["value"];
    }
    else if (cmd == "color") {
      staticR = (*json)["r"];
      staticG = (*json)["g"];
      staticB = (*json)["b"];
      currentMode = ModeStatic;
    }
    else if (cmd == "bg_style") {
      String style = (*json)["value"].as<String>();
      bgStyle = parseBgStyle(style);
    }
    else if (cmd == "bg_speed") {
      bgSpeed = (*json)["value"];
    }
    else if (cmd == "wave_length") {
      waveLength = (*json)["value"];
    }
    else if (cmd == "clock_brightness") {
      clockBrightness = (*json)["value"];
    }
    else if (cmd == "show_clock") {
      showClock = (*json)["value"];
    }
    else if (cmd == "max_current") {
      maxCurrent = (*json)["value"];
    }
  }
}

void publishLedStatus(DynamicJsonDocument* json) {
  unsigned long elapsed = millis();
  unsigned long seconds = elapsed / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  
  char uptime[32];
  snprintf(uptime, sizeof(uptime), "%02lu:%02lu:%02lu", hours, minutes % 60, seconds % 60);
  
  (*json)["uptime"] = uptime;
  (*json)["mode"] = getModeString();
  (*json)["brightness"] = ledBrightness;
  (*json)["bg_style"] = getBgStyleString();
  (*json)["bg_speed"] = bgSpeed;
  (*json)["wave_length"] = waveLength;
  (*json)["show_clock"] = showClock;
  (*json)["clock_brightness"] = clockBrightness;
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
  
  tpl_server.on("/led/white", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    if (tpl_server.hasArg("brightness")) ledBrightness = tpl_server.arg("brightness").toInt();
    currentMode = ModeWhite;
    tpl_server.send(200, "text/html", "OK");
  });
  
  tpl_server.on("/led/scanner", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    if (tpl_server.hasArg("brightness")) ledBrightness = tpl_server.arg("brightness").toInt();
    currentMode = ModeScanner;
    scannerStartTime = millis();
    tpl_server.send(200, "text/html", "OK");
  });
  
  tpl_server.on("/led/status", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    char buf[300];
    snprintf(buf, sizeof(buf), 
             "{\"mode\":\"%s\",\"brightness\":%d,\"r\":%d,\"g\":%d,\"b\":%d,\"bg_style\":\"%s\",\"bg_speed\":%lu,\"wave_length\":%d,\"show_clock\":%s,\"clock_brightness\":%d}",
             getModeString(), ledBrightness, staticR, staticG, staticB, getBgStyleString(), bgSpeed, waveLength, showClock ? "true" : "false", clockBrightness);
    tpl_server.send(200, "application/json", buf);
  });
}

void setup() {
  tpl_system_setup(0);
  
  Serial.begin(115200);
  
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
  
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  addLedEndpoints();
  tpl_webserver_setup();
  tpl_websocket_setup(publishLedStatus, processLedCommand);
  tpl_net_watchdog_setup();
  tpl_command_setup(handleLedCommand);
  
  pinMode(tpl_ledPin, OUTPUT);
  digitalWrite(tpl_ledPin, LOW);
  
  Serial.printf("LED Matrix %dx%d initialized\n", MATRIX_WIDTH, MATRIX_HEIGHT);
  
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);
}

uint32_t last_LED = 0;
bool ledState = false;

void turnOffLeds() {
  for (uint16_t i = 0; i < MATRIX_PIXEL_COUNT; i++) {
    matrix.SetPixelColor(i, RgbColor(0, 0, 0));
  }
  matrix.Show();
}

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

void loop() {
  esp_task_wdt_reset();
  
  if (tpl_config.ota_ongoing) {
    turnOffLeds();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    return;
  }
  
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime;
  
  if ((uint32_t)(currentTime - last_LED) > 500) {
     last_LED = currentTime;
     ledState = !ledState;
     digitalWrite(tpl_ledPin, ledState ? HIGH : LOW);
  }

  uint8_t numPanels = MATRIX_HEIGHT / 8;
  uint8_t scale = 255;

  unsigned long effectTime = (currentMode == ModeScanner) ? (currentTime - scannerStartTime) : elapsed;
  // Scale brightness from 0-100 to 0-255 for LED functions
  uint8_t scaledLedBrightness = (uint16_t)ledBrightness * 255 / 100;
  uint8_t scaledClockBrightness = (uint16_t)clockBrightness * 255 / 100;
  
  calculateAllPixels(
    pixelBuffer,
    MATRIX_WIDTH,
    MATRIX_HEIGHT,
    effectTime,
    currentMode,
    scaledLedBrightness,
    staticR, staticG, staticB,
    (BgStyle)bgStyle,
    bgSpeed,
    waveLength
  );
  if (showClock && currentMode != ModeOff) {
    drawClockOverlay(pixelBuffer, MATRIX_WIDTH, MATRIX_HEIGHT, scaledClockBrightness);
  }
  scale = calculateCurrentScale();
  currentA = estimateCurrent(pixelBuffer, MATRIX_PIXEL_COUNT, numPanels, scale) / 1000000.0f;
  writePixelBufferToMatrix(scale);
  vTaskDelay(20 / portTICK_PERIOD_MS);
}
