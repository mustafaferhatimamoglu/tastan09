#include <Arduino.h>

constexpr uint8_t LED_PIN = LED_BUILTIN;
constexpr unsigned long BLINK_INTERVAL_MS = 500;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // NodeMCU LED is active low
}

void loop() {
  digitalWrite(LED_PIN, LOW);
  delay(BLINK_INTERVAL_MS);
  digitalWrite(LED_PIN, HIGH);
  delay(BLINK_INTERVAL_MS);
}