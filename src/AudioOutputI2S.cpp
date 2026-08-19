#include "AudioOutputI2S.h"

static AudioOutputI2SConfig g_i2s_cfg;
static bool g_i2s_started = false;

#if defined(ESP_PLATFORM)
#include "driver/gpio.h"
#include "driver/i2s_std.h"
static i2s_chan_handle_t g_i2s_tx = NULL;
#endif

void audio_output_i2s_config_default(AudioOutputI2SConfig *cfg) {
  if (!cfg) return;
  cfg->bclk = AUDIO_OUTPUT_I2S_DEFAULT_BCLK;
  cfg->ws = AUDIO_OUTPUT_I2S_DEFAULT_WS;
  cfg->dout = AUDIO_OUTPUT_I2S_DEFAULT_DOUT;
  cfg->rate_hz = AUDIO_OUTPUT_I2S_DEFAULT_RATE;
  cfg->din_unused = true;
}

void audio_output_i2s_trs_labels(const char **tip, const char **ring, const char **sleeve) {
  if (tip) *tip = "Left";
  if (ring) *ring = "Right";
  if (sleeve) *sleeve = "GND";
}

bool audio_output_i2s_begin(const AudioOutputI2SConfig *cfg) {
  AudioOutputI2SConfig d;
  if (!cfg) {
    audio_output_i2s_config_default(&d);
    cfg = &d;
  }
  if (cfg->bclk < 0 || cfg->ws < 0 || cfg->dout < 0) return false;
  if (cfg->bclk == cfg->ws || cfg->bclk == cfg->dout || cfg->ws == cfg->dout) return false;
  g_i2s_cfg = *cfg;
  if (g_i2s_cfg.rate_hz == 0) g_i2s_cfg.rate_hz = AUDIO_OUTPUT_I2S_DEFAULT_RATE;
  g_i2s_cfg.din_unused = true;

#if defined(ESP_PLATFORM)
  if (g_i2s_tx) {
    audio_output_i2s_end();
  }
  i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ch.auto_clear = true;
  if (i2s_new_channel(&ch, &g_i2s_tx, NULL) != ESP_OK) {
    g_i2s_tx = NULL;
    g_i2s_started = false;
    return false;
  }
  i2s_std_config_t std = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(g_i2s_cfg.rate_hz),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)g_i2s_cfg.bclk,
      .ws = (gpio_num_t)g_i2s_cfg.ws,
      .dout = (gpio_num_t)g_i2s_cfg.dout,
      .din = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  if (i2s_channel_init_std_mode(g_i2s_tx, &std) != ESP_OK) {
    i2s_del_channel(g_i2s_tx);
    g_i2s_tx = NULL;
    g_i2s_started = false;
    return false;
  }
  if (i2s_channel_enable(g_i2s_tx) != ESP_OK) {
    i2s_del_channel(g_i2s_tx);
    g_i2s_tx = NULL;
    g_i2s_started = false;
    return false;
  }
#endif

  g_i2s_started = true;
  return true;
}

size_t audio_output_i2s_write(const int16_t *interleaved_stereo, size_t frames) {
  if (!g_i2s_started || !interleaved_stereo || frames == 0) return 0;
#if defined(ESP_PLATFORM)
  if (!g_i2s_tx) return 0;
  size_t bytes = frames * 2u * sizeof(int16_t);
  size_t written = 0;
  if (i2s_channel_write(g_i2s_tx, interleaved_stereo, bytes, &written, 100) != ESP_OK) {
    return 0;
  }
  return written / (2u * sizeof(int16_t));
#else
  (void)interleaved_stereo;
  return frames;
#endif
}

void audio_output_i2s_end(void) {
#if defined(ESP_PLATFORM)
  if (g_i2s_tx) {
    i2s_channel_disable(g_i2s_tx);
    i2s_del_channel(g_i2s_tx);
    g_i2s_tx = NULL;
  }
#endif
  g_i2s_started = false;
}

bool audio_output_i2s_started(void) { return g_i2s_started; }
