#pragma once
#ifndef ESPAUDIO_AUDIO_EFFECT_BASIC8_H
#define ESPAUDIO_AUDIO_EFFECT_BASIC8_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Eight basic insert effects for the public Octopus example.
 *  Original MIT — not a copy of product Multi-FX / aux engines. */

#define AUDIO_FX8_COUNT 8
#define AUDIO_FX8_DELAY 4096

typedef enum {
  AUDIO_FX8_BYPASS = 0,
  AUDIO_FX8_LPF = 1,
  AUDIO_FX8_DELAY = 2,
  AUDIO_FX8_CHORUS = 3,
  AUDIO_FX8_DISTORT = 4,
  AUDIO_FX8_TREMOLO = 5,
  AUDIO_FX8_CRUSH = 6,
  AUDIO_FX8_ROOM = 7
} AudioFx8Kind;

typedef struct {
  float lpf;
  float delay[AUDIO_FX8_DELAY];
  int w;
  float lfo;
  float room_a;
  float room_b;
} AudioFx8State;

extern const char *audio_fx8_names[AUDIO_FX8_COUNT];

void audio_fx8_state_init(AudioFx8State *s);
float audio_fx8_process(AudioFx8State *s, AudioFx8Kind kind, float x, float amount, float sample_hz);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class AudioEffectBasic8 {
 public:
  AudioEffectBasic8() { audio_fx8_state_init(&st_); }
  float process(AudioFx8Kind kind, float x, float amount, float sample_hz = 44100.f) {
    return audio_fx8_process(&st_, kind, x, amount, sample_hz);
  }
 private:
  AudioFx8State st_;
};
#endif

#endif
