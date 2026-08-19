#include "AudioSynthHarp8.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const AudioHarp8Scale audio_harp8_scales[AUDIO_HARP8_SCALES] = {
    {"Major", {0, 2, 4, 5, 7, 9, 11, 12}},
    {"Minor", {0, 2, 3, 5, 7, 8, 10, 12}},
    {"Pentatonic", {0, 2, 4, 7, 9, 12, 14, 16}},
    {"Dorian", {0, 2, 3, 5, 7, 9, 10, 12}},
    {"Mixolydian", {0, 2, 4, 5, 7, 9, 10, 12}},
    {"Blues", {0, 3, 5, 6, 7, 10, 12, 15}},
    {"Whole", {0, 2, 4, 6, 8, 10, 12, 14}},
    {"Chromatic", {0, 1, 2, 3, 4, 5, 6, 7}},
};

void audio_harp8_config_default(AudioHarp8Config *c) {
  if (!c) return;
  c->scale = 0;
  c->preset = 0;
  c->root_midi = 60;
  c->sample_hz = 44100.f;
  c->gate_invert = 1;
}

void audio_harp8_state_init(AudioHarp8State *s) {
  if (!s) return;
  for (int i = 0; i < AUDIO_HARP8_VOICES; i++) {
    s->phase[i] = 0;
    s->env[i] = 0;
    s->lpf[i] = 0;
    s->gate[i] = 0;
  }
  s->lfo = 0;
}

float audio_harp8_midi_hz(float midi) {
  return 440.f * powf(2.f, (midi - 69.f) / 12.f);
}

float audio_harp8_beam_hz(uint8_t beam, const AudioHarp8Config *c) {
  uint8_t b = beam & 7u;
  uint8_t sc = c ? (c->scale & 7u) : 0;
  int8_t root = c ? c->root_midi : 60;
  return audio_harp8_midi_hz((float)(root + audio_harp8_scales[sc].deg[b]));
}

static float clampf(float n, float lo, float hi) {
  if (n < lo) return lo;
  if (n > hi) return hi;
  return n;
}

float audio_harp8_process(AudioHarp8State *s, const AudioHarp8Config *c,
                          const uint8_t gates[AUDIO_HARP8_VOICES],
                          uint8_t cc1) {
  AudioHarp8Config d;
  if (!c) {
    audio_harp8_config_default(&d);
    c = &d;
  }
  AudioHarp8State z;
  if (!s) {
    audio_harp8_state_init(&z);
    s = &z;
  }
  const AudioWt8Preset *pr = &audio_wt8_presets[c->preset & 7u];
  float fs = c->sample_hz > 1000.f ? c->sample_hz : 44100.f;
  float atk = pr->attack_s > 0.0005f ? (1.f / (pr->attack_s * fs)) : 1.f;
  float rel = pr->release_s > 0.0005f ? (1.f / (pr->release_s * fs)) : 1.f;
  float cut = clampf(pr->cutoff01, 0.02f, 0.98f);
  s->lfo += 5.f / fs;
  if (s->lfo > 1.f) s->lfo -= 1.f;
  float vib = (cc1 / 127.f) * 0.03f * sinf(s->lfo * 2.f * (float)M_PI);
  float mix = 0.f;
  for (int i = 0; i < AUDIO_HARP8_VOICES; i++) {
    uint8_t g = gates ? gates[i] : 0;
    if (c->gate_invert) g = g ? 0 : 1;
    s->gate[i] = g;
    if (g) {
      s->env[i] += (1.f - s->env[i]) * atk;
      if (s->env[i] > 1.f) s->env[i] = 1.f;
    } else {
      s->env[i] -= s->env[i] * rel;
      if (s->env[i] < 1e-5f) s->env[i] = 0.f;
    }
    if (s->env[i] <= 0.f) continue;
    float hz = audio_harp8_beam_hz((uint8_t)i, c) * (1.f + vib);
    s->phase[i] += hz * ((float)AUDIO_WT8_LEN / fs);
    while (s->phase[i] >= (float)AUDIO_WT8_LEN) s->phase[i] -= (float)AUDIO_WT8_LEN;
    float x = audio_wt8_lookup(pr->wave, s->phase[i]);
    s->lpf[i] += cut * (x - s->lpf[i]);
    mix += s->lpf[i] * s->env[i];
  }
  return clampf(mix * pr->level * 0.35f, -1.f, 1.f);
}
