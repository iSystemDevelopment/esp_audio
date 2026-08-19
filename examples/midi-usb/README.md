# DIY USB MIDI (CC)

Original MIT packer (`AudioMidiUsb`). Formats USB-MIDI 4-byte CIN packets for Note / CC.

This is **not** the Octopus PRO SysEx protocol (no 201-cmd table, no `0x7C` / `0x7D` frames).

You still write the USB transport (TinyUSB or board MIDI) yourself — CraftAudio PRO emits that wiring from a visual graph.
