# iSystem flash tool

WebSerial flasher for **this** MIT library’s firmware `.bin` (Octopus example or your own build).

**No secrets.** The page never phones home. You pick a local `.bin` you built.

## Build a binary first

Arduino IDE: File → Open `examples/octopus/octopus.ino` (this folder as a library) → Sketch → Export compiled Binary.

PlatformIO (from the repo root):

```bash
pio run -e esp32-s3
# firmware: .pio/build/esp32-s3/firmware.bin
```

Then open `index.html` (Chrome / Edge, HTTPS or localhost) → Connect → choose the `.bin` → Flash.

CLI alternative (Python, if `esptool` is installed):

```bash
python flash.py COM3 path/to/firmware.bin
```

Hold **BOOT**, tap **RST**, then release BOOT if the board does not enter download mode on its own.
