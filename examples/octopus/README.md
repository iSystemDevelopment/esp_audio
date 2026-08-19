# Octopus firmware example

Install this `esp_audio` folder as an Arduino library (ESP32). Open **Examples → EspAudio → octopus**.

PlatformIO from the library root: `pio run -e esp32-s3 -t upload`. Flash a built `.bin` with [`tools/isystem-flash`](../../tools/isystem-flash/).

- GPIO **18** (+ optional 8–14): digital triggers, `INPUT_PULLUP`, LOW = break / play
- GPIO **7**: analog D-BEAM 0–3.3 V → **CC1** only
- GPIO **4**: ~7.8 kHz carrier (lab)
- GPIO **5**: PWM audio (RC LPF → small amp)

Detector: https://github.com/iSystemDevelopment/octopus-laser-harp-lab
