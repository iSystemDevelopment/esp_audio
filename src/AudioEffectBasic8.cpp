#include "AudioEffectBasic8.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const char *audio_fx8_names[AUDIO_FX8_COUNT] = {
    "Bypass", "Low-pass", "Delay", "Chorus", "Distort", "Tremolo", "Crush", "Room"};

void audio_fx8_state_init(AudioFx8State *s) {
  if (!s) return;
  memset(s, 0, sizeof(*s));
}

static float clampf(float n, float lo, float hi) {
  if (n < lo) return lo;
  if (n > hi) return hi;
  return n;
}

float audio_fx8_process(AudioFx8State *s, AudioFx8Kind kind, float x, float amount, float sample_hz) {
  AudioFx8State z;
  if (!s) {
    audio_fx8_state_init(&z);
    s = &z;
  }
  float a = clampf(amount, 0.f, 1.f);
  float fs = sample_hz > 1000.f ? sample_hz : 44100.f;
  float y = x;
  switch (kind) {
    case AUDIO_FX8_LPF: {
      float c = 0.04f + a * 0.9f;
      s->lpf += c * (x - s->lpf);
      y = s->lpf;
      break;
    }
    case AUDIO_FX8_DELAY: {
      int d = 800 + (int)(a * 2800.f);
      if (d >= AUDIO_FX8_DELAY) d = AUDIO_FX8_DELAY - 1;
      int r = s->w - d;
      if (r < 0) r += AUDIO_FX8_DELAY;
      float tap = s->delay[r];
      y = x + tap * (0.15f + a * 0.45f);
      s->delay[s->w] = x + tap * 0.25f;
      s->w++;
      if (s->w >= AUDIO_FX8_DELAY) s->w = 0;
      break;
    }
    case AUDIO_FX8_CHORUS: {
      s->lfo += 0.8f / fs;
      if (s->lfo > 1.f) s->lfo -= 1.f;
      float mod = 0.5f + 0.5f * sinf(s->lfo * 2.f * (float)M_PI);
      int d = 90 + (int)(mod * (40.f + a * 80.f));
      int r = s->w - d;
      if (r < 0) r += AUDIO_FX8_DELAY;
      y = x + s->delay[r] * (0.3f + a * 0.4f);
      s->delay[s->w] = x;
      s->w++;
      if (s->w >= AUDIO_FX8_DELAY) s->w = 0;
      break;
    }
    case AUDIO_FX8_DISTORT: {
      float g = 1.f + a * 8.f;
      float t = tanhf(x * g);
      y = x * (1.f - a) + t * a;
      break;
    }
    case AUDIO_FX8_TREMOLO: {
      s->lfo += (2.f + a * 8.f) / fs;
      if (s->lfo > 1.f) s->lfo -= 1.f;
      float m = 0.5f + 0.5f * sinf(s->lfo * 2.f * (float)M_PI);
      y = x * (1.f - a + a * m);
      break;
    }
    case AUDIO_FX8_CRUSH: {
      float steps = 4.f + (1.f - a) * 28.f;
      y = floorf(x * steps + 0.5f) / steps;
      break;
    }
    case AUDIO_FX8_ROOM: {
      s->room_a = s->room_a * (0.82f - a * 0.1f) + x * 0.25f;
      s->room_b = s->room_b * (0.88f - a * 0.08f) + s->room_a * 0.35f;
      y = x + (s->room_a + s->room_b) * (0.2f + a * 0.45f);
      break;
    }
    default:
      y = x;
      break;
  }
  return clampf(y, -1.f, 1.f);
}
