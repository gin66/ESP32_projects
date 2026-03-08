#include "template.h"
#include <NeoPixelBus.h>
#include <math.h>

#define STRIP_LED_PIN 32
#define STRIP_LED_COUNT 80

NeoPixelBus<NeoRgbwFeature, NeoEsp32Rmt0Ws2814Method> strip(STRIP_LED_COUNT, STRIP_LED_PIN);

enum LedMode {
  ModeOff,
  ModeStatic,
  ModeRainbow,
  ModeRainbowWave,
  ModeWhite
};

static volatile LedMode currentMode = ModeRainbowWave;
static volatile uint8_t ledBrightness = 128;
static volatile uint8_t staticR = 255;
static volatile uint8_t staticG = 0;
static volatile uint8_t staticB = 0;
static volatile uint8_t staticW = 0;
static volatile uint32_t rainbowSpeed = 3000;
static volatile uint32_t whiteSpeed = 8000;
static volatile uint16_t ledCount = STRIP_LED_COUNT;

static unsigned long startTime = 0;

static inline uint8_t clamp16to8(int16_t value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return (uint8_t)value;
}

RgbwColor correctColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  return RgbwColor(w, r, g, b);
}

RgbwColor hsvToRgbw(float h, float s, float v, uint8_t brightness) {
  float r, g, b;
  
  h = fmod(h, 1.0f);
  if (h < 0.0f) h = 0.0f;
  if (s < 0.0f) s = 0.0f;
  if (s > 1.0f) s = 1.0f;
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  
  int i = (int)(h * 6.0f);
  float f = h * 6.0f - (float)i;
  float p = v * (1.0f - s);
  float q = v * (1.0f - f * s);
  float t = v * (1.0f - (1.0f - f) * s);
  
  switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
    default: r = 0.0f; g = 0.0f; b = 0.0f; break;
  }
  
  float bf = (float)brightness / 255.0f * v;
  return correctColor(
    (uint8_t)(r * bf * 255.0f),
    (uint8_t)(g * bf * 255.0f),
    (uint8_t)(b * bf * 255.0f),
    0
  );
}

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
  
  startTime = millis();
  
  tpl_wifi_setup(true, true, (gpio_num_t)255);
  addLedEndpoints();
  tpl_webserver_setup();
  tpl_websocket_setup(publishLedStatus, processLedCommand);
  tpl_net_watchdog_setup();
  tpl_command_setup(handleLedCommand);
  
  strip.Begin();
  strip.Show();
  
  Serial.println("WS2814 Lichtband initialized");
}

void loop() {
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - startTime;
  
  float bf = (float)ledBrightness / 255.0f;
  uint16_t count = (ledCount < STRIP_LED_COUNT) ? ledCount : STRIP_LED_COUNT;
  
  switch (currentMode) {
    case ModeOff:
      for (int i = 0; i < count; i++) {
        strip.SetPixelColor(i, RgbwColor(0, 0, 0, 0));
      }
      break;
      
    case ModeStatic:
      for (int i = 0; i < count; i++) {
        strip.SetPixelColor(i, correctColor(
          (uint8_t)((float)staticR * bf),
          (uint8_t)((float)staticG * bf),
          (uint8_t)((float)staticB * bf),
          (uint8_t)((float)staticW * bf)
        ));
      }
      break;
      
    case ModeRainbow: {
      float period = (float)rainbowSpeed;
      float timeOffset = fmod((float)elapsed / period, 1.0f);
      for (int i = 0; i < count; i++) {
        float positionOffset = (float)i / (float)count;
        float hue = fmod(timeOffset + positionOffset, 1.0f);
        RgbwColor color = hsvToRgbw(hue, 1.0f, 1.0f, ledBrightness);
        strip.SetPixelColor(i, color);
      }
      break;
    }
    
    case ModeRainbowWave: {
      int forwardDuration = 2000;
      int backwardDuration = 2000;
      int cycleDuration = forwardDuration + backwardDuration;
      int cyclePos = elapsed % cycleDuration;
      
      float wavePos;
      if (cyclePos < forwardDuration) {
        wavePos = (float)cyclePos / (float)forwardDuration;
      } else {
        wavePos = 1.0f - (float)(cyclePos - forwardDuration) / (float)backwardDuration;
      }
      
      float hueOffset = fmod((float)elapsed / (float)rainbowSpeed, 1.0f);
      float centerPos = wavePos * (float)(count - 1);
      float waveWidth = (float)count / 5.0f;
      
      for (int i = 0; i < count; i++) {
        float dist = fabsf((float)i - centerPos);
        float intensity = 1.0f - fmin(dist / waveWidth, 1.0f);
        intensity = intensity * intensity;
        
        float hue = fmod(hueOffset + (float)i / (float)count, 1.0f);
        RgbwColor color = hsvToRgbw(hue, 1.0f, intensity, ledBrightness);
        strip.SetPixelColor(i, color);
      }
      break;
    }
    
    case ModeWhite: {
      float period = (float)whiteSpeed;
      float phase = ((float)elapsed / period) * 2.0f * PI;
      float sine = sin(phase);
      float normalized = (sine + 1.0f) / 2.0f;
      uint8_t w = (uint8_t)(normalized * (float)ledBrightness);
      
      for (int i = 0; i < count; i++) {
        strip.SetPixelColor(i, correctColor(0, 0, 0, w));
      }
      break;
    }
  }
  
  for (int i = count; i < STRIP_LED_COUNT; i++) {
    strip.SetPixelColor(i, RgbwColor(0, 0, 0, 0));
  }
  
  strip.Show();
  vTaskDelay(20 / portTICK_PERIOD_MS);
}
