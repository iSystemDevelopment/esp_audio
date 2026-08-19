#include "AudioAnalyzeAdc.h"

#ifndef AUDIO_ANALYZE_ADC_CLAMP
#define AUDIO_ANALYZE_ADC_CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#endif

void audio_analyze_adc_config_default(AudioAnalyzeAdcConfig *cfg) {
  if (!cfg) return;
  cfg->gpio = 7;
  cfg->channel = 6;
  cfg->unit = 1;
  cfg->v_ref = 3.3f;
  cfg->bits = 12;
  cfg->sample_hz = 83333u;
}

uint16_t audio_analyze_adc_volts_to_raw12(float volts, float v_ref) {
  float vr = v_ref > 0.f ? v_ref : 3.3f;
  float v = AUDIO_ANALYZE_ADC_CLAMP(volts, 0.f, vr);
  return (uint16_t)((v / vr) * 4095.f + 0.5f);
}

float audio_analyze_adc_raw12_to_volts(uint16_t raw12, float v_ref) {
  float vr = v_ref > 0.f ? v_ref : 3.3f;
  uint16_t r = raw12 > 4095u ? 4095u : raw12;
  return (r / 4095.f) * vr;
}

AudioAnalyzeAdcSample audio_analyze_adc_map(float volts, const AudioAnalyzeAdcConfig *cfg, uint16_t offset12) {
  AudioAnalyzeAdcConfig d;
  if (!cfg) {
    audio_analyze_adc_config_default(&d);
    cfg = &d;
  }
  AudioAnalyzeAdcSample s;
  s.raw12 = audio_analyze_adc_volts_to_raw12(volts, cfg->v_ref);
  s.volts = audio_analyze_adc_raw12_to_volts(s.raw12, cfg->v_ref);
  uint16_t off = offset12 > 4095u ? 4095u : offset12;
  uint16_t ac = s.raw12 > off ? (uint16_t)(s.raw12 - off) : (uint16_t)(off - s.raw12);
  s.ac_mv = (ac / 4095.f) * (cfg->v_ref * 1000.f);
  return s;
}
