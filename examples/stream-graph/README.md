# stream-graph — play a CraftAudio patch on the board

A patch is **data**. Build it in the browser, paste the JSON, and this renders it
— no per-patch compile, no generated sketch to port by hand.

Same relationship the desktop VST3 wrapper has with a `.sth` file: one runtime,
many patches.

## What you need

| | where from |
|---|---|
| `isystem_dsp_kernels.h` | https://stream.isystem.app → **Code** → **C++** tab → download. Put it beside the sketch. |
| the patch JSON | Same dialog, **JSON** tab. Paste into `kPatch[]`. |

The kernels header is the DSP and it is **not shipped with this library** — it is
generated from the same source as the browser preview, so the board runs the math
you auditioned. This library ships the interpreter; you bring the kernels.

## DAC choice matters here

**PCM5102-style DAC — works as written.** Data only, no control bus, nothing for
the sketch to configure. Tie `SCK` to GND so it uses its internal PLL and no MCLK
is needed.

**SGTL5000 — will not work yet.** It is a codec, not a bare DAC: it needs I2C
setup (routing, volume, headphone amp) before it passes a single sample, and that
control layer is not in this library. Use a PCM5102-style DAC until it is.

Set `PIN_BCLK` / `PIN_WS` / `PIN_DOUT` to your board. The defaults are
placeholders, not a product pinout.

## What it will and will not run

Rendered natively: **oscillator · filter · delay · distortion · gain ·
audio-out**.

Anything else is a **pass-through** — it receives the sum of its inputs and
returns it unchanged, and is named in the line the sketch prints at boot:

```
patch: ok — 4 modules, 3 wires
graph: 2/3 modules native, 1 passed through (mixer4)
```

Nothing is approximated with a lookalike. If a patch cannot be run fully, the
board says which parts it skipped rather than quietly sounding different.

Two other lines worth watching for:

- `patch: DROPPED n module(s)` — the patch is bigger than the compiled caps.
  Raise `AUDIO_STREAM_GRAPH_MAX_NODES` / `_MAX_EDGES` or simplify.
- `graph: nothing to play` — no source reaches Audio Out. Silence with a reason,
  not a dead board.

## Turning a knob afterwards

Loading the patch is half of it. `AudioStreamControl.h` is the other half: one
param of one module, changed on a running graph, with no rebuild and no reflash.

The frame is 11 bytes of SysEx — `F0 ID 0x10 node slot b0..b4 F7` — where
`node` is the module's index in the patch as loaded and the value travels as
raw float bits so neither end has to know the param's range. The web desk, a
touch panel wired to this board, and any external controller send **the same
bytes**. That, rather than a shared struct, is what makes those surfaces
interchangeable. The header explains the format and why it is shaped that way.

In this sketch:

```c
feedControlBytes(bytes, len);   // from wherever your transport delivers them
control.drain(graph);            // between blocks, never during one
```

`drain()` is called at the top of `loop()` because `setParam()` rewrites kernel
coefficients — doing that halfway through a `render()` would tear the block.

Two counters get printed when they move:

- `n dropped (queue full)` — the surface is pushing faster than the audio task
  drains. Coalesce at the source; the browser module ships a `coalescer()` that
  keeps the latest value per (module, param) so the **end** of a drag survives.
- `n rejected (bad node/slot)` — a frame addressed a module that is not there,
  or a param the module's kind does not own. Counted, never silently applied.

### The transport is still yours

`AudioMidiUsb` only **formats outgoing** packets; it does not receive. So the
USB-MIDI SysEx path these frames are meant to travel does not exist in this
library yet, and **none of this has run on hardware.** `feedControlBytes()` is
transport-agnostic on purpose — hand it bytes from anywhere. It is fed from
`Serial` above so the layer is demonstrable on a bench; that is a convenience
for this example, not a claim that params belong on the serial link.

## Notes and limits

- MIDI is monophonic, last-note. The short gate ramp is a designed anti-click
  control, **not** an envelope — there is no ported ADSR, so an ADSR module in
  your patch is one of the pass-through types.
- The sketch holds one note so there is something to hear. For real playing, feed
  `AudioMidiUsb` note on/off into `graph.noteOn()` / `graph.noteOff()`.
- `kPatch[]` is a writable array on purpose: the loader borrows strings out of it
  instead of copying, so it must not live in flash as a string literal.
- **Not verified against the browser numerically.** The kernels are shared, but
  "same kernels" is not "same output" until it has been measured. No parity claim
  until that cross-check has been run.
