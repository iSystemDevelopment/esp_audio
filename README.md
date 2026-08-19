# EspAudio

Teensy-Audio-style **DSP helpers** for **ESP32-S3** / **ESP32-P4**, plus a **simplified Octopus** live-harp example.

**Hub:** [isystem.app](https://isystem.app/#music) · **This repo:** [github.com/iSystemDevelopment/esp_audio](https://github.com/iSystemDevelopment/esp_audio)

| You get here (MIT) | You do **not** get here |
|--------------------|-------------------------|
| Library: ADC map, CC1 D-BEAM, 8-voice harp, 8 presets, 8 FX, DIY GPIO button, USB-MIDI CC packer, DIY I2S out | Product 128 SOUND_BANK, commercial wavetables, laser/MCPWM kernels, 201-cmd SysEx |
| App **Octopus** — live harp, 8 scales / 8 beams / 8 voices | Sequencer, groovebox, extended mixers |
| D-BEAM → **CC1 Modulation** only | Kalman / curves / cut / vol D-BEAM (CraftAudio harp) |
| Detector course link (analog lab) | Factory calibration, OctopusApp, AB 9-09 drums |

The **serious** instrument (16 scales, 128 banks, 8+8+8 voices, Multi-FX) lives in **[CraftAudio](https://stream.isystem.app/)** after credits / subscription. Product: [octopus-info.isystem.app](https://octopus-info.isystem.app/).

---

## Public Octopus (this example)

Live performance harp only:

- **8 scales** · **8 beams** · **8 voices** · **8 basic effects** · **8 sound presets**
- **One** harp synth
- **Analog = D-BEAM → MIDI CC1 Modulation** only (vibrato)
- **Digital = Trigger** (comparator → GPIO → gate)

PWA: [`examples/octopus-pwa`](examples/octopus-pwa/)  
Firmware sketch: [`examples/octopus`](examples/octopus/)  
Spec: [`docs/OCTOPUS.md`](docs/OCTOPUS.md) · Tutorials: [`docs/TUTORIALS.md`](docs/TUTORIALS.md)

```bash
cd examples/octopus-pwa
python -m http.server 8765
# open http://127.0.0.1:8765/  → Start audio → keys 1–8
```

**Later:** a separate public Example Groovebox PWA (4 synth + 4 drum voices) is in [`examples/groovebox-pwa`](examples/groovebox-pwa/). Not AB 9-09.

---

## Sensor course (separate repo)

Optics → TIA → DC servo → AM carrier → Bessel BPF → hysteresis compare:

**[octopus-laser-harp-lab](https://github.com/iSystemDevelopment/octopus-laser-harp-lab)** — staged math, pytest, Instructables draft. After the BPF the sheet splits:

```
amp / mean
  ├─ Analog  → ADC     = D-BEAM → MIDI CC1
  └─ Digital → GPIO    = Trigger
```

Lab carrier **~7.8 kHz**. Commercial hardware may differ — design to the lab numbers.

---

## Library status

| Piece | State |
|-------|--------|
| `AudioAnalyzeAdc` | 12-bit 0…vRef map (example GPIO 7 / ADC1 CH6 / 3.3 V) |
| `AudioDbeamCc1` | Analog → MIDI **CC1** 0–127 only |
| `AudioSynthWavetable8` | **8 original** tables + **8 presets** (not product ROM) |
| `AudioSynthHarp8` | 8 voices · 8 scales · AR env · CC1 vibrato |
| `AudioEffectBasic8` | Bypass, LPF, Delay, Chorus, Distort, Tremolo, Crush, Room |
| `AudioButtonGpio` | DIY debounced GPIO button — you pick the pin |
| `AudioMidiUsb` | DIY USB-MIDI **CC / Note** 4-byte packer (you wire TinyUSB) |
| `AudioOutputI2S` | DIY I2S master out — **you pick** BCLK/WS/DOUT (defaults 7/8/9). Philips 16 stereo. TRS Tip L / Ring R / Sleeve GND after *your* DAC |
| I2S graph runner | Partial — `AudioOutputI2S` shipped; Teensy-shaped `AudioConnection` for every catalog node later |

Arduino: add this folder as a library, then `examples/octopus`.

PlatformIO (repo root):

```bash
pio run -e esp32-s3
pio run -e esp32-s3 -t upload
```

**iSystem flash tool** (WebSerial, local `.bin` only): [`tools/isystem-flash/`](tools/isystem-flash/)

Browser PWA is **not** 83 kHz ADC DMA.

**Example Groovebox** (4 synth + 4 drum, not AB 9-09): [`examples/groovebox-pwa`](examples/groovebox-pwa/)

**Home:** this clone is the public MIT SSOT. The old private `products/espaudio` tree is retired. Do not merge proprietary Octopus kernels (`o1`–`o8`, product ROM, laser/MCPWM, full D-BEAM, AB909) into this repo.

## Related

| Project | Role |
|---------|------|
| [CraftAudio](https://stream.isystem.app/) | Full synth — credits / subscription |
| [Octopus PRO XL](https://octopus-info.isystem.app/) | Commercial product |
| [octopus-laser-harp-lab](https://github.com/iSystemDevelopment/octopus-laser-harp-lab) | MIT detector puzzles + Instructables |
| [diodac_audio](https://github.com/iSystemDevelopment/diodac_audio) | Browser ↔ native MIDI bridge |

---

## License

MIT — [LICENSE](LICENSE) · [NOTICE.md](NOTICE.md)

diodac.electronics@gmail.com · https://isystem.app
