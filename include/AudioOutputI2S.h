#pragma once
#ifndef ESPAUDIO_AUDIO_OUTPUT_I2S_H
#define ESPAUDIO_AUDIO_OUTPUT_I2S_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Public DIY I2S master out (original MIT).
 *  You pick BCLK / WS / DOUT. Defaults 7 / 8 / 9 are placeholders — not a
 *  product board map.
 *  Philips 16-bit stereo, 44100 Hz, DIN unused.
 *  Analog after *your* I2S DAC → 3.5 mm stereo TRS:
 *    Tip = Left · Ring = Right · Sleeve = GND
 *  Not Octopus PRO HAL (no GPIO 15/16/17 lock, no audio_synthesis_task). */

#ifndef AUDIO_OUTPUT_I2S_DEFAULT_BCLK
#define AUDIO_OUTPUT_I2S_DEFAULT_BCLK 7
#endif
#ifndef AUDIO_OUTPUT_I2S_DEFAULT_WS
#define AUDIO_OUTPUT_I2S_DEFAULT_WS 8
#endif
#ifndef AUDIO_OUTPUT_I2S_DEFAULT_DOUT
#define AUDIO_OUTPUT_I2S_DEFAULT_DOUT 9
#endif
#ifndef AUDIO_OUTPUT_I2S_DEFAULT_RATE
#define AUDIO_OUTPUT_I2S_DEFAULT_RATE 44100u
#endif

typedef struct {
  int bclk;
  int ws;
  int dout;
  uint32_t rate_hz;
  bool din_unused;
} AudioOutputI2SConfig;

void audio_output_i2s_config_default(AudioOutputI2SConfig *cfg);
void audio_output_i2s_trs_labels(const char **tip, const char **ring, const char **sleeve);
bool audio_output_i2s_begin(const AudioOutputI2SConfig *cfg);
size_t audio_output_i2s_write(const int16_t *interleaved_stereo, size_t frames);
void audio_output_i2s_end(void);
bool audio_output_i2s_started(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class AudioOutputI2S {
 public:
  AudioOutputI2S() { audio_output_i2s_config_default(&cfg_); }
  void setConfig(const AudioOutputI2SConfig &c) { cfg_ = c; }
  const AudioOutputI2SConfig &config() const { return cfg_; }
  bool begin(int bclk = AUDIO_OUTPUT_I2S_DEFAULT_BCLK,
             int ws = AUDIO_OUTPUT_I2S_DEFAULT_WS,
             int dout = AUDIO_OUTPUT_I2S_DEFAULT_DOUT,
             uint32_t rate_hz = AUDIO_OUTPUT_I2S_DEFAULT_RATE) {
    cfg_.bclk = bclk;
    cfg_.ws = ws;
    cfg_.dout = dout;
    cfg_.rate_hz = rate_hz ? rate_hz : AUDIO_OUTPUT_I2S_DEFAULT_RATE;
    cfg_.din_unused = true;
    return audio_output_i2s_begin(&cfg_);
  }
  size_t write(const int16_t *interleaved_stereo, size_t frames) {
    return audio_output_i2s_write(interleaved_stereo, frames);
  }
  void end() { audio_output_i2s_end(); }
  bool started() const { return audio_output_i2s_started(); }
  static void trsMap(const char **tip, const char **ring, const char **sleeve) {
    audio_output_i2s_trs_labels(tip, ring, sleeve);
  }
 private:
  AudioOutputI2SConfig cfg_;
};
#endif

#endif
