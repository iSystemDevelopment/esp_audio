# Native tests

`test_stream_graph.cpp` exercises `AudioStreamGraph` on the desktop — no board,
no Arduino headers, no toolchain for the ESP32 needed. The runtime core is plain
C++ on purpose, so its behaviour can be checked in seconds rather than by ear.

## You need the DSP header first

`AudioStreamGraph` renders with `isystem_dsp_kernels.h`, which is **not part of
this library**. Export it from CraftAudio:

> https://stream.isystem.app → build a patch → **Code** → **C++** tab →
> download `isystem_dsp_kernels.h`

Put it somewhere on the include path — `build/` is already gitignored and is the
obvious spot:

```
esp_audio/build/isystem_dsp_kernels.h
```

That header is generated from the same source as the browser preview, so the
board runs the math you auditioned. It is deliberately not redistributed here:
this MIT library ships the **interpreter**, you bring the **kernels**.

## Build and run

MSVC:

```
cl /nologo /std:c++17 /EHsc /I include /I build ^
   tests\test_stream_graph.cpp src\AudioStreamGraph.cpp ^
   /Fo:build\ /Fe:build\test_stream_graph.exe
build\test_stream_graph.exe
```

GCC / Clang:

```
g++ -std=c++17 -I include -I build \
    tests/test_stream_graph.cpp src/AudioStreamGraph.cpp \
    -o build/test_stream_graph
./build/test_stream_graph
```

Expected:

```
test-stream-graph:   37 passed, 0 failed
test-stream-patch:   55 passed, 0 failed
test-codec:          46 passed, 0 failed
test-stream-control: 104 passed, 0 failed
```

`scripts/native-tests.bat` builds and runs all four on Windows in one go.

Build the loader test the same way, swapping the sources — note it needs
`AudioStreamGraph.cpp` too, because the loader test renders what it parsed:

```
cl /nologo /std:c++17 /EHsc /I include /I build ^
   tests\test_stream_patch.cpp src\AudioStreamPatch.cpp src\AudioStreamGraph.cpp ^
   /Fo:build\ /Fe:build\test_stream_patch.exe
```

```
g++ -std=c++17 -I include -I build \
    tests/test_stream_patch.cpp src/AudioStreamPatch.cpp src/AudioStreamGraph.cpp \
    -o build/test_stream_patch
```

## What it covers, and what it does not

**Covered** — catalog type mapping; a source having to actually *reach* Audio Out
(an orphaned oscillator does not count); silence until a note is held; unported
modules being reported rather than approximated; a pass-through returning the
**sum** of its inputs, so an unported mixer behaves as a unity-gain summing
mixer; gain and mute; cycle safety; output bounds; null and empty input.

**Not covered — and this matters:** numeric agreement with the browser preview.
The kernels are shared, but "same kernels" is not "same output" until it is
measured. That JS↔C++ cross-check is a separate gate and no claim of parity
should be made until it has been run.

Also not covered here: I2S, USB-MIDI, and anything requiring hardware.

## A note on the negative control

A passing run means little on its own. Break something deliberately — make a
pass-through take only its first input, or let the reachability walk accept any
node — rebuild, and confirm the matching assertions fail. **Five** should.

For the loader, make `read_span()` NUL-terminate the string as it reads it. That
was the real first-cut bug: it works for exactly one field and silently blinds
every later scan, so `params` vanished after `type` and `connections` vanished
after the first module id. It turns **17** assertions red.

If a mutation does not turn the suite red, the suite is not testing what you
think it is.

## The control suite

`test_stream_control.cpp` covers the live-param layer: the SysEx frame, the
value transport, the frame reassembler, `AudioStreamGraph::setParam()`, and the
queue between the surface and the audio thread.

```
cl /nologo /std:c++17 /EHsc /I include /I build ^
   tests\test_stream_control.cpp src\AudioStreamControl.cpp ^
   src\AudioStreamPatch.cpp src\AudioStreamGraph.cpp ^
   /Fo:build\ /Fe:build\test_stream_control.exe
```

```
g++ -std=c++17 -I include -I build \
    tests/test_stream_control.cpp src/AudioStreamControl.cpp \
    src/AudioStreamPatch.cpp src/AudioStreamGraph.cpp \
    -o build/test_stream_control
```

**Covered** — the 11-byte frame and its 7-bit safety; exact float round-trip for
real catalog values (1200 Hz, 0.28 s, -13.7 cents, -0.0, one ulp below 1.0, the
largest finite float); every malformed frame refused with a distinct status and
no half-decoded value left behind; reassembly from a byte-at-a-time stream, with
note traffic interleaved, with a truncated frame in front, and after an overrun;
a live gain write measurably quartering the peak; mute winning over gain; a
write that does NOT restart the oscillator; the queue's ordering, its refusal at
capacity, and its separation of *dropped* from *rejected*; the device's own echo
being ignored; and the name↔slot table the JS side is checked against.

**Not covered** — anything needing hardware: no USB-MIDI receive path exists in
this library yet, so the transport that will actually carry these frames is
untested. `feedControlBytes()` in the example is fed from `Serial` on a bench.

### Negative controls

Break one thing, rebuild, confirm the suite goes red. Measured:

| mutation | assertions that must fail |
|----------|---------------------------|
| pack 8 bits per septet instead of 7 | **6** |
| `setParam` bounds the slot at 6 instead of the kind's own count | **4** |
| `apply_params` re-zeroes the oscillator phase | **2** |
| the queue overwrites instead of refusing when full | **3** |
| the decoder stops checking the top septet's width | **2** |

The phase mutation is worth a note. The first cut of that assertion wrote to the
**gain** node, so the oscillator's `apply_params()` never ran and the mutation
left the suite green — a vacuous assertion, not a passing one. It now writes to
the oscillator itself and measures the discontinuity directly: a 440 Hz sine at
44.1 kHz steps at most 0.0658 per sample, and a phase restart jumps 0.219. If a
mutation does not turn the suite red, the suite is not testing what you think.

## The JS↔C++ cross-check

`dump_control_vectors.cpp` prints the frames this library encodes for a fixed
vector set, plus its slot tables.
`platform/isystem-builder/smoke-stream-control.mjs` in the private monorepo runs
that **compiled binary** and diffs its bytes against the browser encoder's — not
against a JS re-implementation of it. When the binary is absent the smoke prints
a SKIP; a skipped cross-check is not a passed one.
