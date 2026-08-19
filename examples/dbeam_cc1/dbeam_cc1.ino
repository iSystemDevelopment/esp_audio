/* Analog 0–vRef → 12-bit → MIDI CC1 only. Example GPIO 7 / ADC1 CH6 / 3.3 V. */
#include "AudioAnalyzeAdc.h"
#include "AudioDbeamCc1.h"

AudioAnalyzeAdc adc;
AudioDbeamCc1 dbeam;

void setup() {
  Serial.begin(115200);
}

void loop() {
  float volts = analogRead(7) / 4095.f * 3.3f;
  AudioAnalyzeAdcSample s = adc.mapVolts(volts, 2048);
  uint8_t cc1 = dbeam.cc1(s.raw12);
  Serial.printf("V=%.3f adc=%u CC1=%u\n", s.volts, s.raw12, cc1);
  delay(16);
}
