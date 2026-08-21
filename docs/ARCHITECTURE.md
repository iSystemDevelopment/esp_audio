# EspAudio — architecture notes

Public MIT library + simplified **Octopus** live-harp example. Never author these files under `private-isystem-codebase/`.

## Split

| Layer | Where |
|-------|--------|
| Detector puzzles (optics / analog) | [octopus-laser-harp-lab](https://github.com/iSystemDevelopment/octopus-laser-harp-lab) |
| Public DSP + Octopus app (8/8/8/8/8) | **this repo** |
| Full synth / 128 banks / groovebox | [CraftAudio](https://stream.isystem.app/) (credits / subscription) |

## Dual sensor

After TIA → servo → Bessel BPF → amp/mean:

- **Analog** 0–3.3 V → ADC = **D-BEAM → MIDI CC1** (modulation / vibrato only)
- **Digital** comparator → GPIO = **Trigger**

Kalman, curves, cutoff, and volume routes stay in the CraftAudio harp module.

Lab carrier **~7.8 kHz**. Do not copy product firmware.

## Audio thread (device)

1. Fixed block at 44.1 kHz (board-configurable).
2. No heap in the render callback.
3. UI / Wi-Fi on the second core.

## What is original MIT vs forbidden

**Here:** generated 8-cycle wavetables, 8 presets, 8 scales, simple harp, 8 insert FX, grain OLA on caller PCM (`AudioGranularOla`), ADC → CC1, DIY GPIO button, USB-MIDI CC packer, DIY I2S master out (you pick pins). No SD WAV decoder.

**Not here:** product SOUND_BANK 128, commercial ROM, `laser.cpp`, photodiode latch, `harp.cpp`, `dbeam.cpp`, AB 9-09, sequencer, extended mixers, PRO D-BEAM expression, Octopus 201-cmd SysEx (`0x7C`/`0x7D`), Octopus I2S pin map / `audio_synthesis_task`.
