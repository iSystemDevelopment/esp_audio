/* Public simplified Octopus — live harp only.
 * 8 digital triggers + 1 analog D-BEAM → MIDI CC1 (modulation) only.
 * Original MIT example — not product firmware / not CraftAudio D-BEAM.
 *
 * Pins (ESP32-S3 DIY, change to match your board):
 *   GPIO 4  laser AM carrier ~7.8 kHz (lab number)
 *   GPIO 7  analog D-BEAM 0–3.3 V → CC1
 *   GPIO 18 beam 0 digital trigger (INPUT_PULLUP, LOW = break / play)
 *   GPIO 8..14 optional beams 1–7
 *   GPIO 5  PWM audio (add RC LPF to a small amp)
 *
 * Carrier / TIA / comparator: octopus-laser-harp-lab
 */
#include <Arduino.h>
#include <math.h>
#include "AudioAnalyzeAdc.h"
#include "AudioDbeamCc1.h"
#include "AudioSynthHarp8.h"
#include "AudioEffectBasic8.h"

#ifndef PIN_CARRIER
#define PIN_CARRIER 4
#endif
#ifndef PIN_DBEAM
#define PIN_DBEAM 7
#endif
#ifndef PIN_PWM_AUDIO
#define PIN_PWM_AUDIO 5
#endif
#ifndef LAB_FC_HZ
#define LAB_FC_HZ 7800
#endif

static const int kTrig[8] = {18, 8, 9, 10, 11, 12, 13, 14};

AudioAnalyzeAdc adc;
AudioDbeamCc1 dbeam;
AudioSynthHarp8 harp;
AudioEffectBasic8 fx8;
uint8_t gates[8];
uint8_t last_cc1 = 255;

void setupCarrierPwm() {
#if defined(ARDUINO_ARCH_ESP32)
  ledcAttach(PIN_CARRIER, LAB_FC_HZ, 8);
  ledcWrite(PIN_CARRIER, 128);
  ledcAttach(PIN_PWM_AUDIO, 32000, 8);
#endif
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 8; i++) pinMode(kTrig[i], INPUT_PULLUP);
  pinMode(PIN_DBEAM, INPUT);
  AudioAnalyzeAdcConfig cfg;
  audio_analyze_adc_config_default(&cfg);
  cfg.gpio = PIN_DBEAM;
  adc.setConfig(cfg);
  audio_harp8_config_default(&harp.config());
  harp.config().preset = 2;
  harp.config().scale = 2;
  setupCarrierPwm();
}

void loop() {
  for (int i = 0; i < 8; i++) gates[i] = (uint8_t)digitalRead(kTrig[i]);

  int raw = analogRead(PIN_DBEAM);
  float volts = (raw / 4095.f) * 3.3f;
  AudioAnalyzeAdcSample s = adc.mapVolts(volts, 2048);
  uint8_t cc1 = dbeam.cc1(s.raw12);
  if (cc1 != last_cc1) {
    last_cc1 = cc1;
#if defined(OCTOPUS_MIDI_SERIAL)
    uint8_t msg[3] = {0xB0, 0x01, cc1};
    OCTOPUS_MIDI_SERIAL.write(msg, 3);
#endif
  }

  const int N = 128;
  float acc = 0;
  for (int n = 0; n < N; n++) {
    float x = harp.process(gates, cc1);
    x = fx8.process(AUDIO_FX8_ROOM, x, 0.25f, harp.config().sample_hz);
    acc += x * x;
#if defined(ARDUINO_ARCH_ESP32)
    int duty = (int)((x * 0.5f + 0.5f) * 255.f);
    if (duty < 0) duty = 0;
    if (duty > 255) duty = 255;
    ledcWrite(PIN_PWM_AUDIO, duty);
#endif
  }
  static uint32_t t;
  if (millis() - t > 200) {
    t = millis();
    Serial.printf("\nV=%.3f CC1=%u rms=%.3f\n", s.volts, cc1, sqrtf(acc / N));
  }
}
