#include "AudioSynthWavetable8.h"

const char *audio_wt8_wave_names[AUDIO_WT8_COUNT] = {
    "Sine", "Triangle", "Saw", "Square", "Pulse", "Organ", "Bell", "Noise"};

const AudioWt8Preset audio_wt8_presets[AUDIO_WT8_PRESETS] = {
    {"Sine", 0, 0.040f, 0.220f, 0.85f, 0.80f},
    {"Triangle", 1, 0.008f, 0.160f, 0.80f, 0.82f},
    {"Saw", 2, 0.006f, 0.140f, 0.55f, 0.70f},
    {"Square", 3, 0.006f, 0.180f, 0.45f, 0.68f},
    {"Pulse", 4, 0.004f, 0.120f, 0.50f, 0.66f},
    {"Organ", 5, 0.020f, 0.280f, 0.70f, 0.75f},
    {"Bell", 6, 0.002f, 0.650f, 0.90f, 0.72f},
    {"Noise", 7, 0.001f, 0.080f, 0.40f, 0.55f},
};

float audio_wt8_lookup(uint8_t wave, float phase) {
  uint8_t w = wave & 7u;
  float p = phase;
  if (p < 0.f) p = 0.f;
  while (p >= (float)AUDIO_WT8_LEN) p -= (float)AUDIO_WT8_LEN;
  int i0 = (int)p;
  int i1 = i0 + 1;
  if (i1 >= AUDIO_WT8_LEN) i1 = 0;
  float f = p - (float)i0;
  float a = (float)audio_wt8[w][i0];
  float b = (float)audio_wt8[w][i1];
  return ((a + (b - a) * f) / 32768.f);
}
