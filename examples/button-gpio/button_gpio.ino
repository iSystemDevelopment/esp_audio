/* DIY GPIO button — original MIT. Wire your own pin; not an Octopus board map. */
#include "AudioButtonGpio.h"

#ifndef BTN_PIN
#define BTN_PIN 0
#endif

AudioButtonGpio btn;

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  btn.begin(BTN_PIN, true, 20);
}

void loop() {
  bool high = digitalRead(BTN_PIN) == HIGH;
  btn.update(millis(), high);
  if (btn.fell()) Serial.println("fell");
  if (btn.rose()) Serial.println("rose");
  delay(1);
}
