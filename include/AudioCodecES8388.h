#pragma once
#ifndef ESPAUDIO_AUDIO_CODEC_ES8388_H
#define ESPAUDIO_AUDIO_CODEC_ES8388_H

#include "AudioCodec.h"

/** Everest ES8388 — stereo codec, ADC + DAC, I2S data with I2C control.
 *
 *  Espressif's reference part (LyraT, ESP-ADF), which is the reason to prefer it
 *  over a one-off: there is a known-good bring-up sequence in the open rather
 *  than a datasheet reading you have to trust.
 *
 *  ── Read this before using it ───────────────────────────────────────────────
 *  This driver ships the PLUMBING and NOT the init sequence:
 *
 *    - I2C register read / write               real, and unit-tested
 *    - presence probe                          real, and unit-tested
 *    - table-driven init                       real, and unit-tested
 *    - the actual register table                >>> YOU SUPPLY THIS <<<
 *
 *  The bring-up sequence is deliberately a caller-supplied table, for two
 *  reasons. It is genuinely board-dependent — mic vs line in, headphone vs line
 *  out, MCLK ratio, whether the part is I2S master or slave — so there is no one
 *  correct sequence to bake in. And a plausible-looking sequence that is subtly
 *  wrong gives you a silent board and no clue why, which is worse than having
 *  no default at all.
 *
 *  Get a known-good table from Espressif's own driver:
 *    esp-adf/components/audio_hal/driver/es8388/es8388.c
 *  or from the ES8388 datasheet's power-up notes, and express it as
 *  AudioCodecReg pairs. Once you have one verified on YOUR board, keep it beside
 *  the sketch — it is board data, like a pin map.
 *
 *  Default I2C address is 0x10 (CE low). 0x11 when CE is pulled high.
 */

#ifdef __cplusplus

#ifndef AUDIO_CODEC_ES8388_ADDR
#define AUDIO_CODEC_ES8388_ADDR 0x10
#endif

/** Register 0 (CHIP_CONTROL1) — used by the probe. */
#ifndef AUDIO_CODEC_ES8388_REG_CONTROL1
#define AUDIO_CODEC_ES8388_REG_CONTROL1 0x00
#endif

/** DAC volume registers, from the ES8388 register map. 0x00 = 0 dB,
 *  rising value = more attenuation, 0xC0 = mute. */
#ifndef AUDIO_CODEC_ES8388_REG_DACVOL_L
#define AUDIO_CODEC_ES8388_REG_DACVOL_L 0x1A
#endif
#ifndef AUDIO_CODEC_ES8388_REG_DACVOL_R
#define AUDIO_CODEC_ES8388_REG_DACVOL_R 0x1B
#endif
/** The value that mutes a DAC volume register. */
#define AUDIO_CODEC_ES8388_VOL_MUTE 0xC0

class AudioCodecES8388 : public AudioCodec {
 public:
  AudioCodecES8388();

  /** Bus and address must be set before begin(). */
  void setBus(const AudioCodecBus &bus) { bus_ = bus; }
  void setAddress(uint8_t addr) { addr_ = addr; }

  /**
   * The bring-up table. Without one, begin() refuses rather than half-starting
   * the part — see the note at the top of this header.
   * The table is borrowed, not copied; keep it alive.
   */
  void setInitSequence(const AudioCodecReg *regs, int count) {
    init_ = regs;
    init_count_ = count;
  }

  /**
   * Probe, then play the table.
   *
   * `sample_rate_hz` is recorded for the log only: rate on this part is decided
   * by the clock registers in YOUR table, not by a value passed here. Saying so
   * is better than accepting a number and quietly ignoring it.
   */
  bool begin(uint32_t sample_rate_hz);
  void end();

  /** 0.0 .. 1.0 mapped onto both DAC volume registers. */
  bool setVolume(float v);

  bool ready() const { return ready_; }
  const char *name() const { return "ES8388"; }

  /** True when a device acknowledged at the configured address. */
  bool probe();

  bool writeReg(uint8_t reg, uint8_t val);
  bool readReg(uint8_t reg, uint8_t *out);

  /** Why begin() failed, for the boot log. Empty when it succeeded. */
  const char *lastError() const { return err_; }

  /** Volume byte for a 0..1 level. Exposed so the mapping can be tested. */
  static uint8_t volumeToReg(float v);

 private:
  AudioCodecBus bus_;
  uint8_t addr_;
  const AudioCodecReg *init_;
  int init_count_;
  bool ready_;
  uint32_t rate_;
  const char *err_;
};

#endif /* __cplusplus */
#endif /* ESPAUDIO_AUDIO_CODEC_ES8388_H */
