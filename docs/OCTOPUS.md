# Public Octopus (EspAudio example)

Live **harp** only. Not a groovebox. No sequencer. No extended mixer.

| Limit | Public example | CraftAudio (credits / subscription) |
|-------|----------------|--------------------------------------|
| Beams / voices | **8 / 8** | 8 harp + 8 seq + 8 drums (product) |
| Scales | **8** | 16 product scales |
| Sound bank | **8** presets | **128** factory ROM + user slots |
| Effects | **8** basic insert FX | Multi-FX + Shared Aux |
| D-BEAM | Analog → **CC1 Modulation** only | Full harp D-BEAM (Kalman, curves, cut / vol / mod) |
| Instrument | One harp synth | Full Octopus PRO XL / Stream lab |
| Groovebox | *Later* public 4 synth + 4 drum PWA | Full 8+8 groovebox |

Name of this app: **Octopus**.

## Dual sensor (from the laser-harp lab)

After TIA → servo → Bessel BPF → amp/mean:

- **Analog 0–3.3 V → ADC = D-BEAM → MIDI CC1** (modulation / vibrato only)
- **Digital comparator → GPIO = Trigger** (beam-break gate)

No cutoff, volume, or Kalman on this public D-BEAM. That module stays in CraftAudio.

Lab carrier target **~7.8 kHz**. Commercial product may use a different carrier — do not copy product firmware.

Course: https://github.com/iSystemDevelopment/octopus-laser-harp-lab

## Run the PWA

```bash
cd examples/octopus-pwa
python -m http.server 8765
```

Open http://127.0.0.1:8765/ — click **Start audio**, then strings or keys 1–8. D-BEAM slider is CC1.

## Firmware sketch

`examples/octopus/octopus.ino` — 8 pull-up triggers + analog D-BEAM → CC1 + PWM audio.

## Honesty

This DSP is **original MIT**, generated 8-cycle tables + simple voices/FX. It is **not** a dump of Octopus `.sources` (`harp.cpp`, SOUND_BANK, `laser.cpp`, `dbeam.cpp`).
