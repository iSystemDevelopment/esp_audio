#include "AudioDbeamCc1.h"

uint8_t audio_dbeam_cc1(uint16_t raw12) {
  uint16_t r = raw12 > 4095u ? 4095u : raw12;
  return (uint8_t)((r * 127u) / 4095u);
}
