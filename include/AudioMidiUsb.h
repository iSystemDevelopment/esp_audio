#pragma once
#ifndef ESPAUDIO_AUDIO_MIDI_USB_H
#define ESPAUDIO_AUDIO_MIDI_USB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Public DIY USB-MIDI helpers (original MIT).
 *  Packs standard USB-MIDI 4-byte CIN packets for Note / CC.
 *  Not Octopus SysEx (no 201-cmd table, no 0x7C/0x7D frames).
 *  You still wire TinyUSB / USB-MIDI yourself — this only formats bytes. */

enum {
  AUDIO_MIDI_CIN_NOTE_OFF = 0x8,
  AUDIO_MIDI_CIN_NOTE_ON = 0x9,
  AUDIO_MIDI_CIN_CC = 0xB
};

void audio_midi_usb_pack(uint8_t cin, uint8_t status, uint8_t d1, uint8_t d2, uint8_t out4[4]);
void audio_midi_usb_pack_cc(uint8_t channel, uint8_t cc, uint8_t value, uint8_t out4[4]);
void audio_midi_usb_pack_note_on(uint8_t channel, uint8_t note, uint8_t vel, uint8_t out4[4]);
void audio_midi_usb_pack_note_off(uint8_t channel, uint8_t note, uint8_t vel, uint8_t out4[4]);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class AudioMidiUsb {
 public:
  void packCc(uint8_t channel, uint8_t cc, uint8_t value, uint8_t out4[4]) const {
    audio_midi_usb_pack_cc(channel, cc, value, out4);
  }
  void packNoteOn(uint8_t channel, uint8_t note, uint8_t vel, uint8_t out4[4]) const {
    audio_midi_usb_pack_note_on(channel, note, vel, out4);
  }
  void packNoteOff(uint8_t channel, uint8_t note, uint8_t vel, uint8_t out4[4]) const {
    audio_midi_usb_pack_note_off(channel, note, vel, out4);
  }
};
#endif

#endif
