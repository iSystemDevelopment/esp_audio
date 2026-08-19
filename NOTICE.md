# Notices — EspAudio (public MIT)

https://github.com/iSystemDevelopment/esp_audio  
Local checkout: `workspace-diodac-electronics/public-repositories/esp_audio` (never nest this tree under private firmware).

## Allowed

- `AudioAnalyzeAdc` · `AudioDbeamCc1` (analog → MIDI CC1 only)
- **Original** 8 wavetables / 8 presets / 8 scales / 8-voice harp / 8 basic FX
- DIY `AudioButtonGpio` (you pick the pin) · DIY `AudioMidiUsb` (CC / Note packer) · DIY `AudioOutputI2S` (you pick BCLK/WS/DOUT)
- Simplified **Octopus** PWA + firmware example (live harp only)
- Links to the detector lab

## Forbidden

- Product SOUND_BANK (128) · commercial wavetable ROM · Plasma Pad tables
- `laser.cpp` / photodiode latch / harp kernels / `dbeam.cpp` copies / AB 9-09
- D-BEAM Kalman / curves / cutoff / volume routes (CraftAudio harp module)
- Sequencer, extended mixers, full groovebox (those stay in CraftAudio)
- Octopus 201-cmd SysEx (`0x7C`/`0x7D`), encoder/OLED HAL, AB 9-09, pin maps
- Anything from `private-isystem-codebase/products/octopus/.sources/` or `products/espaudio/src/o1_*`…`o12_*` (including `init_i2s_hardware` / `audio_synthesis_task`)

CraftAudio (full instrument + PRO D-BEAM): https://stream.isystem.app/  
Detector lab: https://github.com/iSystemDevelopment/octopus-laser-harp-lab

Contact: diodac.electronics@gmail.com · https://isystem.app
