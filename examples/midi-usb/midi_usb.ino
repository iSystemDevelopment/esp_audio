/* DIY USB-MIDI CC packer — original MIT. Not Octopus 201-cmd SysEx.
 * You still attach a USB-MIDI stack (TinyUSB, etc.) and send the 4 bytes. */
#include "AudioMidiUsb.h"

AudioMidiUsb midi;
uint8_t pkt[4];

void setup() {
  Serial.begin(115200);
}

void loop() {
  static uint8_t v;
  midi.packCc(0, 1, v, pkt); /* ch1 CC1 */
  Serial.printf("%02X %02X %02X %02X\n", pkt[0], pkt[1], pkt[2], pkt[3]);
  v = (uint8_t)((v + 1) & 0x7f);
  delay(50);
}
