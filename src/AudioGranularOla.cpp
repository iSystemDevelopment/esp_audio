#include "AudioGranularOla.h"

#ifdef __cplusplus
void audio_granular_ola_touch(AudioGranularOla *g) {
  if (g) (void)g->process();
}
#endif
