#include "AudioCodecES8388.h"

#include <string.h>

AudioCodecES8388::AudioCodecES8388()
    : addr_(AUDIO_CODEC_ES8388_ADDR), init_(0), init_count_(0), ready_(false),
      rate_(0), err_("") {
  memset(&bus_, 0, sizeof(bus_));
}

bool AudioCodecES8388::writeReg(uint8_t reg, uint8_t val) {
  if (!bus_.write) return false;
  return bus_.write(bus_.ctx, addr_, reg, val);
}

bool AudioCodecES8388::readReg(uint8_t reg, uint8_t *out) {
  if (!bus_.read || !out) return false;
  return bus_.read(bus_.ctx, addr_, reg, out);
}

bool AudioCodecES8388::probe() {
  uint8_t v = 0;
  /* Any acknowledged read is enough. The ES8388 has no fixed ID register whose
   * value can be asserted, so this checks that SOMETHING is at the address —
   * which distinguishes "not wired" from "wired and misconfigured", the two
   * failures that otherwise look identical from the outside. */
  return readReg(AUDIO_CODEC_ES8388_REG_CONTROL1, &v);
}

uint8_t AudioCodecES8388::volumeToReg(float v) {
  /* ES8388 DAC volume: 0x00 is 0 dB and larger values attenuate, so the mapping
   * is inverted against the 0..1 level. 0xC0 is the mute code, and the usable
   * range stops just below it. */
  if (!(v > 0.0f)) return AUDIO_CODEC_ES8388_VOL_MUTE;
  if (v > 1.0f) v = 1.0f;
  const float span = (float)(AUDIO_CODEC_ES8388_VOL_MUTE - 1);
  float atten = (1.0f - v) * span;
  if (atten < 0.0f) atten = 0.0f;
  if (atten > span) atten = span;
  return (uint8_t)(atten + 0.5f);
}

bool AudioCodecES8388::setVolume(float v) {
  if (!ready_) return false;
  const uint8_t r = volumeToReg(v);
  const bool a = writeReg(AUDIO_CODEC_ES8388_REG_DACVOL_L, r);
  const bool b = writeReg(AUDIO_CODEC_ES8388_REG_DACVOL_R, r);
  return a && b;
}

bool AudioCodecES8388::begin(uint32_t sample_rate_hz) {
  ready_ = false;
  rate_ = sample_rate_hz;
  err_ = "";

  if (!bus_.write || !bus_.read) {
    err_ = "no I2C bus set — call setBus() first";
    return false;
  }
  if (!init_ || init_count_ <= 0) {
    /* Refusing beats half-starting. An ES8388 that has been probed but not
     * configured will sit there acknowledging I2C and passing no audio, which
     * looks exactly like a wiring fault. See the header for where to get a
     * known-good table. */
    err_ = "no init sequence set — this driver does not ship one, see AudioCodecES8388.h";
    return false;
  }
  if (!probe()) {
    err_ = "no device acknowledged at the I2C address — check wiring and CE";
    return false;
  }

  for (int i = 0; i < init_count_; ++i) {
    if (!writeReg(init_[i].reg, init_[i].val)) {
      /* Stop at the first failure. Continuing would leave the part in a state
       * no table describes, and the log would blame the wrong register. */
      err_ = "I2C write failed during the init sequence";
      return false;
    }
  }

  ready_ = true;
  return true;
}

void AudioCodecES8388::end() {
  if (ready_) {
    writeReg(AUDIO_CODEC_ES8388_REG_DACVOL_L, AUDIO_CODEC_ES8388_VOL_MUTE);
    writeReg(AUDIO_CODEC_ES8388_REG_DACVOL_R, AUDIO_CODEC_ES8388_VOL_MUTE);
  }
  ready_ = false;
}
