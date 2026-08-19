/* DIY I2S master out — original MIT. You pick BCLK/WS/DOUT.
 * Defaults 7/8/9 are placeholders, not a product board.
 * Analog after your DAC → 3.5 mm TRS: Tip=Left Ring=Right Sleeve=GND.
 * Not Octopus PRO HAL. */
#include "AudioOutputI2S.h"

AudioOutputI2S out;
int16_t frame[2];
uint32_t n;

void setup() {
  Serial.begin(115200);
  const char *tip = 0, *ring = 0, *sleeve = 0;
  AudioOutputI2S::trsMap(&tip, &ring, &sleeve);
  Serial.printf("TRS %s / %s / %s\n", tip, ring, sleeve);
  if (!out.begin(7, 8, 9, 44100)) {
    Serial.println("I2S begin failed (host build stores pins only)");
  }
}

void loop() {
  /* Quiet stereo frame — replace with your mixer. */
  frame[0] = 0;
  frame[1] = 0;
  out.write(frame, 1);
  n++;
  if ((n & 0x3fff) == 0) Serial.println(n);
}
