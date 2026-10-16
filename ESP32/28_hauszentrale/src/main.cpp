#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("28_hauszentrale S1.01 OK");
}

void loop() {
    delay(1000);
}
