#include "AudioButtonGpio.h"

static AudioButtonGpioConfig g_btn;
static bool g_stable_high = true;
static bool g_last_high = true;
static uint32_t g_edge_ms = 0;
static bool g_fell = false;
static bool g_rose = false;

void audio_button_gpio_config_default(AudioButtonGpioConfig *cfg) {
  if (!cfg) return;
  cfg->pin = 0;
  cfg->debounce_ms = 20;
  cfg->pullup = true;
  cfg->invert = false;
}

void audio_button_gpio_begin(const AudioButtonGpioConfig *cfg) {
  if (cfg) g_btn = *cfg;
  g_stable_high = true;
  g_last_high = true;
  g_edge_ms = 0;
  g_fell = false;
  g_rose = false;
}

bool audio_button_gpio_update(uint32_t now_ms, bool raw_high) {
  g_fell = false;
  g_rose = false;
  if (g_btn.invert) raw_high = !raw_high;
  if (raw_high != g_last_high) {
    g_last_high = raw_high;
    g_edge_ms = now_ms;
    return g_stable_high;
  }
  uint16_t db = g_btn.debounce_ms ? g_btn.debounce_ms : 1;
  if ((uint32_t)(now_ms - g_edge_ms) < db) return g_stable_high;
  if (raw_high != g_stable_high) {
    if (g_stable_high && !raw_high) g_fell = true;
    if (!g_stable_high && raw_high) g_rose = true;
    g_stable_high = raw_high;
  }
  return g_stable_high;
}

bool audio_button_gpio_fell(void) { return g_fell; }
bool audio_button_gpio_rose(void) { return g_rose; }
bool audio_button_gpio_held(void) { return !g_stable_high; }
