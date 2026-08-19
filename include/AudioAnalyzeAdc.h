#pragma once
#ifndef ESPAUDIO_AUDIO_ANALYZE_ADC_H
#define ESPAUDIO_AUDIO_ANALYZE_ADC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Generic 12-bit ADC helper (0 … vRef). Example map: GPIO 7 / ADC1 CH6 / 3.3 V.
 *  Sanitized public HAL — not a copy of any product firmware TU. */
typedef struct {
  int gpio;
  int channel;   /* ADC1 channel index */
  int unit;      /* 1 = ADC_UNIT_1 */
  float v_ref;   /* volts */
  int bits;      /* 12 */
  uint32_t sample_hz;
} AudioAnalyzeAdcConfig;

typedef struct {
  uint16_t raw12;
  float volts;
  float ac_mv;   /* |raw - offset| in millivolts */
} AudioAnalyzeAdcSample;

void audio_analyze_adc_config_default(AudioAnalyzeAdcConfig *cfg);
uint16_t audio_analyze_adc_volts_to_raw12(float volts, float v_ref);
float audio_analyze_adc_raw12_to_volts(uint16_t raw12, float v_ref);
AudioAnalyzeAdcSample audio_analyze_adc_map(float volts, const AudioAnalyzeAdcConfig *cfg, uint16_t offset12);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class AudioAnalyzeAdc {
 public:
  AudioAnalyzeAdc() { audio_analyze_adc_config_default(&cfg_); }
  void setConfig(const AudioAnalyzeAdcConfig &c) { cfg_ = c; }
  const AudioAnalyzeAdcConfig &config() const { return cfg_; }
  AudioAnalyzeAdcSample mapVolts(float volts, uint16_t offset12 = 2048) const {
    return audio_analyze_adc_map(volts, &cfg_, offset12);
  }
 private:
  AudioAnalyzeAdcConfig cfg_;
};
#endif

#endif
