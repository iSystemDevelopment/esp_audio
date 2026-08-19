#pragma once
#ifndef ESPAUDIO_AUDIO_BUTTON_GPIO_H
#define ESPAUDIO_AUDIO_BUTTON_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Public DIY GPIO button helper (original MIT).
 *  Debounced active-low input with optional pull-up.
 *  Not a copy of any product ButtonPoll / Octopus pin map — wire your own pins. */

typedef struct {
  uint8_t pin;
  uint16_t debounce_ms;
  bool pullup;
  bool invert;
} AudioButtonGpioConfig;

void audio_button_gpio_config_default(AudioButtonGpioConfig *cfg);
void audio_button_gpio_begin(const AudioButtonGpioConfig *cfg);
bool audio_button_gpio_update(uint32_t now_ms, bool raw_high);
bool audio_button_gpio_fell(void);
bool audio_button_gpio_rose(void);
bool audio_button_gpio_held(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class AudioButtonGpio {
 public:
  void begin(uint8_t pin, bool pullup = true, uint16_t debounce_ms = 20) {
    AudioButtonGpioConfig c;
    audio_button_gpio_config_default(&c);
    c.pin = pin;
    c.pullup = pullup;
    c.debounce_ms = debounce_ms;
    audio_button_gpio_begin(&c);
  }
  bool update(uint32_t now_ms, bool raw_high) { return audio_button_gpio_update(now_ms, raw_high); }
  bool fell() const { return audio_button_gpio_fell(); }
  bool rose() const { return audio_button_gpio_rose(); }
  bool held() const { return audio_button_gpio_held(); }
};
#endif

#endif
