#pragma once
#ifndef ESPAUDIO_AUDIO_SYNTH_WAVETABLE8_H
#define ESPAUDIO_AUDIO_SYNTH_WAVETABLE8_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Eight original public wavetables + eight factory presets.
 *  Not product SOUND_BANK (128) and not commercial ROM. */

#define AUDIO_WT8_LEN 256
#define AUDIO_WT8_COUNT 8
#define AUDIO_WT8_PRESETS 8

extern const int16_t audio_wt8[AUDIO_WT8_COUNT][AUDIO_WT8_LEN];

typedef struct {
  const char *name;
  uint8_t wave;     /* 0..7 */
  float attack_s;
  float release_s;
  float cutoff01;   /* 0..1 one-pole */
  float level;
} AudioWt8Preset;

extern const AudioWt8Preset audio_wt8_presets[AUDIO_WT8_PRESETS];
extern const char *audio_wt8_wave_names[AUDIO_WT8_COUNT];

float audio_wt8_lookup(uint8_t wave, float phase);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class AudioSynthWavetable8 {
 public:
  const AudioWt8Preset &preset(uint8_t i) const {
    return audio_wt8_presets[i & 7u];
  }
  float lookup(uint8_t wave, float phase) const {
    return audio_wt8_lookup(wave, phase);
  }
};
#endif

#endif
