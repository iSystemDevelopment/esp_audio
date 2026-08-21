#pragma once
#ifndef ESPAUDIO_AUDIO_CODEC_H
#define ESPAUDIO_AUDIO_CODEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** Control side of an audio front end — the part that is NOT the I2S stream.
 *
 *  Two shapes exist and they are genuinely different, which is why this
 *  interface is here rather than a single class with flags:
 *
 *    - A bare DAC (PCM5102 and friends) has NO control bus. You wire it, it
 *      converts. There is nothing to configure and `AudioCodecNone` says so.
 *    - A codec (ES8388, ES8311, SGTL5000) will not pass a single sample until
 *      it has been set up over I2C: clocking, input and output routing, power
 *      rails, volumes.
 *
 *  Keeping them behind one interface means a sketch swaps front ends by
 *  changing one object, and `AudioOutputI2S` never has to know which it is.
 *
 *  This is control only. Sample transport stays with AudioOutputI2S.
 */

#ifdef __cplusplus

/** One I2C register write, as data. Init sequences are tables, not code. */
struct AudioCodecReg {
  uint8_t reg;
  uint8_t val;
};

/**
 * I2C transport, injected rather than assumed.
 *
 * On a board this is Wire; in a test it is a fake bus. That is not test
 * scaffolding for its own sake — it is the only way to check an init sequence
 * without hardware, and an unverifiable codec driver is how boards end up
 * silent for reasons nobody can see.
 *
 * Both return true on success.
 */
struct AudioCodecBus {
  bool (*write)(void *ctx, uint8_t addr, uint8_t reg, uint8_t val);
  bool (*read)(void *ctx, uint8_t addr, uint8_t reg, uint8_t *out);
  void *ctx;
};

class AudioCodec {
 public:
  virtual ~AudioCodec() {}

  /** Bring the part up. False means it is not usable — do not start I2S. */
  virtual bool begin(uint32_t sample_rate_hz) = 0;
  virtual void end() {}

  /** 0.0 .. 1.0. Parts with no control ignore it and return false. */
  virtual bool setVolume(float v) { (void)v; return false; }

  /** True when the part was found and configured. */
  virtual bool ready() const = 0;

  /** For the boot log — which front end is actually in play. */
  virtual const char *name() const = 0;
};

/**
 * A DAC with no control bus: PCM5102, PCM5100, UDA1334 and similar.
 *
 * Deliberately does nothing. It exists so a sketch can name its front end and
 * print it, and so swapping to a codec later changes one line. Tie the DAC's
 * SCK to GND to use its internal PLL and no MCLK is needed.
 */
class AudioCodecNone : public AudioCodec {
 public:
  explicit AudioCodecNone(const char *label = "no-control DAC (PCM5102-style)")
      : label_(label) {}
  bool begin(uint32_t sample_rate_hz) {
    (void)sample_rate_hz;
    return true; /* nothing to do, and nothing that can fail */
  }
  bool ready() const { return true; }
  const char *name() const { return label_; }

 private:
  const char *label_;
};

#endif /* __cplusplus */
#endif /* ESPAUDIO_AUDIO_CODEC_H */
