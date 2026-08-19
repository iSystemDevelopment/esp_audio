#pragma once
#ifndef ESPAUDIO_AUDIO_DBEAM_CC1_H
#define ESPAUDIO_AUDIO_DBEAM_CC1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Public DIY D-BEAM: analog 12-bit → MIDI CC1 (modulation) only.
 *  No Kalman, curves, envelopes, or cut/vol routes — those stay in CraftAudio. */

uint8_t audio_dbeam_cc1(uint16_t raw12);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class AudioDbeamCc1 {
 public:
  uint8_t cc1(uint16_t raw12) const { return audio_dbeam_cc1(raw12); }
};
#endif

#endif
