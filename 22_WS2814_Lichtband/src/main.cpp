#include "template.h"
#include <NeoPixelBus.h>
#include <FS.h>
#include <SPIFFS.h>
#include <stdint.h>

#include "led_effects.h"

#define STRIP_LED_PIN 32
#define STRIP_LED_COUNT 80
#define WAVE_CONFIG_FNAME "/wave_cfg.bin"

NeoPixelBus<NeoRgbwFeature, NeoEsp32Rmt0Ws2814Method> strip(STRIP_LED_COUNT, STRIP_LED_PIN);

static volatile LedMode currentMode = ModeRainbowWave;
static volatile uint8_t ledBrightness = 128;
static volatile uint8_t staticR = 255;
static volatile uint8_t staticG = 0;
static volatile uint8_t staticB = 0;
static volatile uint8_t staticW = 0;
static volatile uint32_t rainbowSpeed = 3000;
static volatile uint32_t whiteSpeed = 8000;
static volatile uint16_t ledCount = STRIP_LED_COUNT;

static WaveConfig waveConfig;
static LedState ledState;
static unsigned long startTime = 0;

enum StartupPhase {
  PhaseRed,
  PhaseBlue,
  PhaseWhite,
  PhaseNormal
};
static volatile StartupPhase startupPhase = PhaseRed;
static TaskHandle_t ledTaskHandle = NULL;

const char* getModeString() {
  switch (currentMode) {
    case ModeOff: return "off";
    case ModeStatic: return "custom";
    case ModeRainbow: return "rainbow";
    case ModeRainbowWave: return "wave";
    case ModeWhite: return "white";
    default: return "unknown";
  }
}

bool loadWaveConfig() {
  if (SPIFFS.exists(WAVE_CONFIG_FNAME)) {
    File f = SPIFFS.open(WAVE_CONFIG_FNAME, "rb");
    if (f) {
      WaveConfig temp;
      size_t readSize = f.readBytes((char*)&temp, sizeof(WaveConfig));
      f.close();
      if (readSize == sizeof(WaveConfig) && temp.version == WAVE_CONFIG_VERSION) {
        waveConfig = temp;
        return true;
      }
    }
  }
  waveConfig.init();
  return false;
}

bool saveWaveConfig() {
  File f = SPIFFS.open(WAVE_CONFIG_FNAME, "wb");
  if (!f) return false;
  waveConfig.need_store = false;
  size_t writeSize = f.write((uint8_t*)&waveConfig, sizeof(WaveConfig));
  f.close();
  return writeSize == sizeof(WaveConfig);
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
      else if (mode == "custom") currentMode = ModeStatic;
    }
    else if (cmd == "brightness") {
      int val = (*json)["value"];
      ledBrightness = (uint8_t)((val * 255) / 100);
    }
    else if (cmd == "color") {
      staticR = (*json)["r"];
      staticG = (*json)["g"];
      staticB = (*json)["b"];
      staticW = (*json)["w"];
      currentMode = ModeStatic;
    }
    else if (cmd == "rainbow_speed") {
      rainbowSpeed = (*json)["value"];
    }
    else if (cmd == "white_speed") {
      whiteSpeed = (*json)["value"];
    }
    else if (cmd == "led_count") {
      ledCount = (*json)["value"];
    }
    else if (cmd == "wave_width") {
      waveConfig.waveWidth = (*json)["value"];
      waveConfig.need_store = true;
    }
    else if (cmd == "wave_speed") {
      waveConfig.waveSpeed = (*json)["value"];
      waveConfig.need_store = true;
    }
    else if (cmd == "wave_bidirectional") {
      waveConfig.bidirectional = (*json)["value"];
      waveConfig.need_store = true;
    }
    else if (cmd == "wave_acceleration") {
      waveConfig.acceleration = (*json)["value"];
      waveConfig.need_store = true;
    }
    else if (cmd == "static_segment_count") {
      waveConfig.staticSegmentCount = (*json)["value"];
      waveConfig.need_store = true;
    }
    else if (cmd == "static_segment_offset") {
      waveConfig.staticSegmentOffset = (*json)["value"];
      waveConfig.need_store = true;
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
  (*json)["white_speed"] = whiteSpeed;
  (*json)["led_count"] = ledCount;
  (*json)["wave_width"] = waveConfig.waveWidth;
  (*json)["wave_speed"] = waveConfig.waveSpeed;
  (*json)["wave_bidirectional"] = waveConfig.bidirectional;
  (*json)["wave_acceleration"] = waveConfig.acceleration;
  (*json)["static_segment_count"] = waveConfig.staticSegmentCount;
  (*json)["static_segment_offset"] = waveConfig.staticSegmentOffset;
}

void handleLedCommand(enum Command cmd) {
}

extern WebServer tpl_server;

static void showComet(uint16_t pos, uint8_t r, uint8_t g, uint8_t b) {
  strip.ClearTo(RgbwColor(0, 0, 0, 0));
  uint8_t tail[] = {255, 180, 100, 40};
  for (int t = 0; t < 4; t++) {
    int16_t p = (int16_t)pos - t;
    if (p >= 0 && p < STRIP_LED_COUNT) {
      strip.SetPixelColor(p, RgbwColor(0,
        (uint16_t)r * tail[t] / 255,
        (uint16_t)g * tail[t] / 255,
        (uint16_t)b * tail[t] / 255));
    }
  }
  strip.Show();
}

void ledTask(void* arg) {
  strip.Begin();
  strip.ClearTo(RgbwColor(0, 0, 0, 0));
  strip.Show();

  uint16_t cometPos = 0;
  unsigned long taskStart = millis();

  while (true) {
    if (startupPhase != PhaseNormal) {
      uint8_t r = 0, g = 0, b = 0;
      if (startupPhase == PhaseRed)   { r = 255; }
      if (startupPhase == PhaseBlue)  { b = 255; }
      if (startupPhase == PhaseWhite) { r = 255; g = 255; b = 255; }

      showComet(cometPos, r, g, b);
      cometPos++;
      if (cometPos >= STRIP_LED_COUNT) cometPos = 0;

      if (startupPhase == PhaseRed && WiFi.status() == WL_CONNECTED) {
        startupPhase = PhaseBlue;
        cometPos = 0;
      }

      vTaskDelay(20 / portTICK_PERIOD_MS);
    } else {
      unsigned long elapsed = millis() - taskStart;
      uint16_t count = (ledCount < STRIP_LED_COUNT) ? ledCount : STRIP_LED_COUNT;

      LedColor leds[STRIP_LED_COUNT];
      calculateAllLeds(
        leds, count, elapsed,
        currentMode, ledBrightness,
        waveConfig, ledState,
        staticR, staticG, staticB, staticW,
        rainbowSpeed, whiteSpeed
      );

      for (uint16_t i = 0; i < count; i++) {
        strip.SetPixelColor(i, RgbwColor(leds[i].W, leds[i].R, leds[i].G, leds[i].B));
      }
      for (uint16_t i = count; i < STRIP_LED_COUNT; i++) {
        strip.SetPixelColor(i, RgbwColor(0, 0, 0, 0));
      }
      strip.Show();
      vTaskDelay(20 / portTICK_PERIOD_MS);
    }
  }
}

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
    if (tpl_server.hasArg("w")) staticW = tpl_server.arg("w").toInt();
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
  
  tpl_server.on("/led/brightness", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    if (tpl_server.hasArg("value")) ledBrightness = tpl_server.arg("value").toInt();
    tpl_server.send(200, "text/html", String(ledBrightness));
  });
  
  tpl_server.on("/led/status", HTTP_GET, []() {
    tpl_server.sendHeader("Connection", "close");
    char buf[128];
    snprintf(buf, sizeof(buf), 
             "{\"mode\":\"%s\",\"brightness\":%d,\"r\":%d,\"g\":%d,\"b\":%d,\"w\":%d}",
             getModeString(), ledBrightness, staticR, staticG, staticB, staticW);
    tpl_server.send(200, "application/json", buf);
  });
}

void setup() {
  tpl_system_setup(0);
  Serial.begin(115200);

  xTaskCreatePinnedToCore(ledTask, "led", 4096, NULL, 2, &ledTaskHandle, CORE_1);

  startTime = millis();

  tpl_wifi_setup(true, true, (gpio_num_t)255);

  startupPhase = PhaseWhite;
  delay(1000);
  startupPhase = PhaseNormal;

  loadWaveConfig();
  addLedEndpoints();
  tpl_webserver_setup();
  tpl_websocket_setup(publishLedStatus, processLedCommand);
  tpl_net_watchdog_setup();
  tpl_command_setup(handleLedCommand);

  Serial.println("WS2814 Lichtband initialized");
}

void loop() {
  if (waveConfig.need_store) {
    saveWaveConfig();
  }
  vTaskDelay(100 / portTICK_PERIOD_MS);
}
