#pragma once
#ifndef ESPAUDIO_AUDIO_SYNTH_HARP8_H
#define ESPAUDIO_AUDIO_SYNTH_HARP8_H

#include <stdint.h>
#include "AudioSynthWavetable8.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Public simplified Octopus harp: 8 beams, 8 voices, 8 scales, one synth.
 *  D-BEAM in = MIDI CC1 (vibrato) only. Original MIT — not product harp.cpp. */

#define AUDIO_HARP8_VOICES 8
#define AUDIO_HARP8_SCALES 8

typedef struct {
  const char *name;
  int8_t deg[8]; /* semitone offsets from root */
} AudioHarp8Scale;

extern const AudioHarp8Scale audio_harp8_scales[AUDIO_HARP8_SCALES];

typedef struct {
  uint8_t scale;     /* 0..7 */
  uint8_t preset;    /* 0..7 */
  int8_t root_midi;  /* default 60 = C4 */
  float sample_hz;
  uint8_t gate_invert; /* 1 = active-low beam-break */
} AudioHarp8Config;

typedef struct {
  float phase[AUDIO_HARP8_VOICES];
  float env[AUDIO_HARP8_VOICES];
  float lpf[AUDIO_HARP8_VOICES];
  uint8_t gate[AUDIO_HARP8_VOICES];
  float lfo;
} AudioHarp8State;

void audio_harp8_config_default(AudioHarp8Config *c);
void audio_harp8_state_init(AudioHarp8State *s);
float audio_harp8_midi_hz(float midi);
float audio_harp8_beam_hz(uint8_t beam, const AudioHarp8Config *c);
float audio_harp8_process(AudioHarp8State *s, const AudioHarp8Config *c,
                          const uint8_t gates[AUDIO_HARP8_VOICES],
                          uint8_t cc1);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class AudioSynthHarp8 {
 public:
  AudioSynthHarp8() {
    audio_harp8_config_default(&cfg_);
    audio_harp8_state_init(&st_);
  }
  AudioHarp8Config &config() { return cfg_; }
  float process(const uint8_t gates[AUDIO_HARP8_VOICES], uint8_t cc1) {
    return audio_harp8_process(&st_, &cfg_, gates, cc1);
  }
 private:
  AudioHarp8Config cfg_;
  AudioHarp8State st_;
};
#endif

#endif
