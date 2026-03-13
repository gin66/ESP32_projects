/**
 * @file main.cpp
 * @brief Matter example using tpl_matter template
 *
 * This example creates a Matter On/Off Light endpoint that can be
 * controlled from Apple Home, Google Home, or other Matter controllers.
 */

#include "template.h"
#include <Preferences.h>

constexpr auto LED_PIN = 2;
constexpr auto BUTTON_PIN = 0;

MatterOnOffLight onOffLight;
Preferences preferences;
constexpr auto* ONOFF_PREF_KEY = "OnOff";

uint32_t button_timestamp = 0;
bool button_pressed = false;
constexpr auto DEBOUNCE_MS = 250;
constexpr auto DECOMMISSION_MS = 5000;

bool onLightChange(bool state) {
  Serial.printf("Light state: %s\n", state ? "ON" : "OFF");
  digitalWrite(LED_PIN, state ? HIGH : LOW);
  preferences.putBool(ONOFF_PREF_KEY, state);
  return true;
}

bool onIdentify(bool active) {
  for (int i = 0; i < 4; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(250);
    digitalWrite(LED_PIN, LOW);
    delay(250);
  }
  return true;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Matter Light Example ===");

  tpl_system_setup(0);

  preferences.begin("MatterPrefs", false);
  bool lastState = preferences.getBool(ONOFF_PREF_KEY, false);

  onOffLight.begin(lastState);
  onOffLight.onChange(onLightChange);
  onOffLight.onIdentify(onIdentify);

  tpl_matter_setup(TPL_MATTER_STANDALONE);
  tpl_matter_wait_commissioned();

  if (tpl_matter_is_commissioned()) {
    onOffLight.updateAccessory();
  }

  Serial.println("Matter Light ready!");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW && !button_pressed) {
    button_timestamp = millis();
    button_pressed = true;
  }

  uint32_t elapsed = millis() - button_timestamp;

  if (button_pressed && elapsed > DEBOUNCE_MS && digitalRead(BUTTON_PIN) == HIGH) {
    button_pressed = false;
    Serial.println("Button: toggle light");
    onOffLight.toggle();
  }

  if (button_pressed && elapsed > DECOMMISSION_MS) {
    Serial.println("Button: decommissioning...");
    onOffLight.setOnOff(false);
    tpl_matter_decommission();
    button_timestamp = millis();
  }

  vTaskDelay(10 / portTICK_PERIOD_MS);
}
