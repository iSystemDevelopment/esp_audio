# DIY I2S out

Original MIT (`AudioOutputI2S`). Philips 16-bit stereo master, **you pick** BCLK / WS / DOUT.

Placeholder defaults: GPIO **7 / 8 / 9** — not a product pin map.

Analog after *your* I2S DAC → 3.5 mm stereo TRS:

| Contact | Channel |
|---------|---------|
| Tip | Left |
| Ring | Right |
| Sleeve | GND |

DIN is unused (no analog line-in on this helper).

This is **not** the Octopus PRO I2S HAL (no locked GPIO 15/16/17, no `audio_synthesis_task`).
