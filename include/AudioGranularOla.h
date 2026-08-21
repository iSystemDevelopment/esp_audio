#pragma once
#ifndef ESPAUDIO_AUDIO_GRANULAR_OLA_H
#define ESPAUDIO_AUDIO_GRANULAR_OLA_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>

#ifdef __cplusplus

/** Public MIT grain overlap-add on a caller-owned PCM buffer.
 *  Flex = keep duration, pitch from semitones (~30 ms Hann, hop = grain/4).
 *  Crystal = reverse each grain. No SD WAV decoder / no AudioPlaySdWav.
 *  You load PCM (from SD or flash), then setBuffer(). Not an Octopus kernel. */
#ifndef AUDIO_GRANULAR_OLA_MAX_GRAIN
#define AUDIO_GRANULAR_OLA_MAX_GRAIN 2048
#endif

class AudioGranularOla {
 public:
  AudioGranularOla() { reset(44100.f); }

  void reset(float sample_hz) {
    sr_ = sample_hz > 1000.f ? sample_hz : 44100.f;
    pos_ = 0.f;
    done_ = !pcm_ || n_ < 1;
    syncGrain_();
  }

  void setBuffer(const float *pcm, int n, float buf_hz) {
    pcm_ = pcm;
    n_ = n > 0 ? n : 0;
    buf_hz_ = buf_hz > 1000.f ? buf_hz : 44100.f;
    pos_ = 0.f;
    done_ = n_ < 1 || !pcm_;
  }

  void setRate(float rate) { rate_ = clampf_(rate, 0.05f, 4.f); }
  void setSemitones(float st) { semitones_ = clampf_(st, -24.f, 24.f); }
  void setFlex(bool on) { flex_ = on; }
  void setCrystal(bool on) { crystal_ = on; }
  void setLoop(bool on) { loop_ = on; }

  float process() {
    if (!pcm_ || n_ < 1 || done_) return 0.f;
    float pitch = powf(2.f, semitones_ / 12.f);
    if (!(pitch > 0.f) || pitch != pitch) pitch = 1.f;
    float srRatio = buf_hz_ / sr_;
    if (!(srRatio > 0.f) || srRatio != srRatio) srRatio = 1.f;
    float inc = flex_ ? rate_ * srRatio : rate_ * pitch * srRatio;
    float grainRatio = flex_ ? pitch : 1.f;
    float y = (flex_ || crystal_) ? olaAt_(pos_, grainRatio) : readAt_(pos_);
    pos_ += inc;
    if (loop_) {
      float span = (float)n_;
      while (pos_ >= span) pos_ -= span;
      while (pos_ < 0.f) pos_ += span;
    } else if (pos_ >= (float)n_ || pos_ < 0.f) {
      done_ = true;
      pos_ = pos_ < 0.f ? 0.f : (float)(n_ - 1);
    }
    return y;
  }

 private:
  const float *pcm_ = 0;
  int n_ = 0;
  float buf_hz_ = 44100.f;
  float sr_ = 44100.f;
  float pos_ = 0.f;
  float rate_ = 1.f;
  float semitones_ = 0.f;
  bool flex_ = false;
  bool crystal_ = false;
  bool loop_ = true;
  bool done_ = true;
  int grain_ = 1323;
  int hop_ = 330;
  int half_ = 661;

  static float clampf_(float n, float lo, float hi) {
    if (n < lo) return lo;
    if (n > hi) return hi;
    return n;
  }

  void syncGrain_() {
    int g = (int)(sr_ * 0.03f);
    if (g < 256) g = 256;
    if (g > AUDIO_GRANULAR_OLA_MAX_GRAIN) g = AUDIO_GRANULAR_OLA_MAX_GRAIN;
    grain_ = g;
    hop_ = g / 4;
    if (hop_ < 64) hop_ = 64;
    half_ = g / 2;
  }

  float readAt_(float pos) const {
    if (!pcm_ || n_ < 1) return 0.f;
    if (n_ == 1) return pcm_[0];
    float p = pos;
    if (loop_) {
      float span = (float)n_;
      while (p >= span) p -= span;
      while (p < 0.f) p += span;
    } else {
      if (p < 0.f) p = 0.f;
      if (p > (float)(n_ - 1)) p = (float)(n_ - 1);
    }
    int i0 = (int)floorf(p);
    float frac = p - (float)i0;
    int i1 = i0 + 1;
    if (loop_) {
      if (i0 < 0) i0 += n_;
      i0 %= n_;
      if (i1 < 0) i1 += n_;
      i1 %= n_;
    } else {
      if (i0 < 0) i0 = 0;
      if (i0 > n_ - 1) i0 = n_ - 1;
      if (i1 < 0) i1 = 0;
      if (i1 > n_ - 1) i1 = n_ - 1;
    }
    return pcm_[i0] * (1.f - frac) + pcm_[i1] * frac;
  }

  float olaAt_(float local, float grainRatio) const {
    int grain = grain_;
    int hop = hop_;
    int half = half_;
    float acc = 0.f;
    float wsum = 0.f;
    int iMin = (int)floorf((local - (float)half) / (float)hop) - 1;
    int iMax = (int)floorf((local + (float)half) / (float)hop) + 1;
    int i;
    for (i = iMin; i <= iMax; ++i) {
      float grainPos = (float)(i * hop);
      float g = local - grainPos + (float)half;
      if (g < 0.f || g >= (float)grain) continue;
      float srcCenter = grainPos * grainRatio;
      float gi = crystal_ ? ((float)grain - 1.f - g) : g;
      float si = srcCenter + (gi - (float)half);
      float w = 0.5f - 0.5f * cosf((3.14159265f * 2.f * g) / (float)(grain > 1 ? grain - 1 : 1));
      acc += readAt_(si) * w;
      wsum += w;
    }
    if (wsum <= 1e-6f) return 0.f;
    return acc / wsum;
  }
};

#endif /* __cplusplus */
#endif
