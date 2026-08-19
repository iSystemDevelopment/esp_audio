#include "AudioMidiUsb.h"

void audio_midi_usb_pack(uint8_t cin, uint8_t status, uint8_t d1, uint8_t d2, uint8_t out4[4]) {
  if (!out4) return;
  out4[0] = (uint8_t)((cin & 0x0f));
  out4[1] = status;
  out4[2] = (uint8_t)(d1 & 0x7f);
  out4[3] = (uint8_t)(d2 & 0x7f);
}

void audio_midi_usb_pack_cc(uint8_t channel, uint8_t cc, uint8_t value, uint8_t out4[4]) {
  uint8_t ch = (uint8_t)(channel & 0x0f);
  audio_midi_usb_pack(AUDIO_MIDI_CIN_CC, (uint8_t)(0xb0 | ch), cc, value, out4);
}

void audio_midi_usb_pack_note_on(uint8_t channel, uint8_t note, uint8_t vel, uint8_t out4[4]) {
  uint8_t ch = (uint8_t)(channel & 0x0f);
  audio_midi_usb_pack(AUDIO_MIDI_CIN_NOTE_ON, (uint8_t)(0x90 | ch), note, vel, out4);
}

void audio_midi_usb_pack_note_off(uint8_t channel, uint8_t note, uint8_t vel, uint8_t out4[4]) {
  uint8_t ch = (uint8_t)(channel & 0x0f);
  audio_midi_usb_pack(AUDIO_MIDI_CIN_NOTE_OFF, (uint8_t)(0x80 | ch), note, vel, out4);
}
