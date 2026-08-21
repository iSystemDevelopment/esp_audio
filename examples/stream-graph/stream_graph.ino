/* CraftAudio patch → ESP32, as DATA.
 *
 * Build a patch at https://stream.isystem.app, paste its JSON below, and this
 * plays it. There is no per-patch compile and no generated sketch: the runtime
 * walks the module graph, the same way the desktop VST3 wrapper loads a .sth.
 *
 * ── You need two things from CraftAudio ─────────────────────────────────────
 *   1. isystem_dsp_kernels.h   Export Studio Code → C++ tab → download.
 *                              Put it beside this sketch. It is the DSP, and it
 *                              is generated from the same source as the browser
 *                              preview, so the board runs what you auditioned.
 *   2. the patch JSON          Export Studio Code → JSON tab. Paste into
 *                              kPatch below.
 *
 * ── DAC wiring ──────────────────────────────────────────────────────────────
 * Written for a PCM5102-style I2S DAC: data only, no control bus, so nothing
 * here has to configure it. Tie its SCK to GND to use the internal PLL and no
 * MCLK is needed.
 *
 * An SGTL5000 will NOT work with this sketch as written. It is a codec, not a
 * bare DAC — it needs I2C setup (routing, volume, headphone amp) before it will
 * pass a sample, and that control layer is not in this library yet. Use a
 * PCM5102-style DAC here until it is.
 *
 * Pins below are placeholders. Set them to your board.
 */
#include "AudioOutputI2S.h"
#include "AudioStreamPatch.h"
#include "AudioStreamControl.h"

/* ── your board ───────────────────────────────────────────────────────────── */
static const int PIN_BCLK = 7;
static const int PIN_WS   = 8;
static const int PIN_DOUT = 9;
static const uint32_t SAMPLE_RATE = 44100;

/* ── your patch ───────────────────────────────────────────────────────────── */
/* Kept in a writable buffer on purpose: the loader borrows strings out of it
 * rather than copying, so it must not be a string literal in flash. */
static char kPatch[] =
  "{\"kind\":\"isystem-stream\",\"title\":\"demo\","
  "\"modules\":{"
    "\"o1\":{\"type\":\"isystem-dsp-oscillator\",\"params\":{"
        "\"wave\":{\"value\":2},\"level\":{\"value\":0.6}}},"
    "\"fl1\":{\"type\":\"isystem-dsp-filter\",\"params\":{"
        "\"cutoff\":{\"value\":1400},\"resonance\":{\"value\":6}}},"
    "\"out\":{\"type\":\"isystem-dsp-audio-out\"}"
  "},"
  "\"connections\":["
    "{\"from\":{\"module\":\"o1\"},\"to\":{\"module\":\"fl1\"}},"
    "{\"from\":{\"module\":\"fl1\"},\"to\":{\"module\":\"out\"}}"
  "]}";

/* ── runtime ──────────────────────────────────────────────────────────────── */
static AudioOutputI2S out;
static AudioStreamGraph graph;
static AudioStreamNodeDesc nodes[AUDIO_STREAM_GRAPH_MAX_NODES];
static AudioStreamEdgeDesc edges[AUDIO_STREAM_GRAPH_MAX_EDGES];

/* One block of mono float from the graph, converted to interleaved stereo. */
static const int BLOCK = 128;
static float mono[BLOCK];
static int16_t stereo[BLOCK * 2];

static bool ready = false;

/* ── live control ─────────────────────────────────────────────────────────── */
/* Turning a knob after the patch has loaded. The desk, a touch panel wired to
 * this same board, and an external controller all send the SAME 11-byte SysEx
 * frame — see AudioStreamControl.h for the format and why it carries float bits
 * rather than a normalised integer.
 *
 * feedControlBytes() is deliberately transport-agnostic: hand it bytes from
 * wherever they arrive. The intended transport is USB-MIDI SysEx, but
 * AudioMidiUsb only FORMATS outgoing packets today — it does not receive — so
 * wiring the receive path is still yours. Serial stands in below so this is
 * demonstrable on a bench; that is a convenience for this example, not a
 * statement that params belong on the serial link. */
static AudioStreamSysexReader sysexIn;
static AudioStreamControlQueue control;

void feedControlBytes(const uint8_t *bytes, int len) {
  const uint8_t *frame = 0;
  int frame_len = 0;
  for (int i = 0; i < len; ++i) {
    if (!sysexIn.push(bytes[i], &frame, &frame_len)) continue;
    AudioStreamControlMsg m = audio_stream_control_decode(frame, frame_len);
    if (m.status == AUDIO_STREAM_CONTROL_OK) {
      /* feed() also drops the board's own 0x7C echo, so a MIDI loop cannot
       * drive the device from its own output. */
      if (!control.feed(m)) Serial.println("ctl: frame ignored (wrong direction)");
    } else if (m.status != AUDIO_STREAM_CONTROL_ERR_SUB) {
      /* ERR_SUB just means it was an Octopus frame on the same cable, which is
       * normal. Anything else is a real malformed message and worth saying. */
      Serial.printf("ctl: %s\n", audio_stream_control_status_text(m.status));
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  AudioStreamPatchResult r =
      audio_stream_patch_parse(kPatch, nodes, AUDIO_STREAM_GRAPH_MAX_NODES,
                               edges, AUDIO_STREAM_GRAPH_MAX_EDGES);
  Serial.printf("patch: %s — %d modules, %d wires\n",
                audio_stream_patch_status_text(r.status), r.node_count, r.edge_count);
  /* Never let a partial load pass silently: a patch that lost modules or wires
   * will render, and it will not be the patch you built. */
  if (r.dropped_nodes || r.dropped_edges) {
    Serial.printf("patch: DROPPED %d module(s) and %d wire(s) — raise the caps "
                  "or simplify the patch\n", r.dropped_nodes, r.dropped_edges);
  }

  ready = graph.build(nodes, r.node_count, edges, r.edge_count, (float)SAMPLE_RATE);

  char what[192];
  graph.summary(what, sizeof(what));
  Serial.printf("graph: %s\n", what);
  if (!ready) {
    /* Silence with a reason beats silence that looks like a dead board. */
    Serial.println("graph: nothing to play — check that a source reaches Audio Out");
  }

  if (!out.begin(PIN_BCLK, PIN_WS, PIN_DOUT, SAMPLE_RATE)) {
    Serial.println("i2s: begin failed — check the pins");
  }

  /* Hold one note so there is something to hear. Replace with USB-MIDI:
   * AudioMidiUsb gives you note on/off, and graph.noteOn/noteOff take them. */
  graph.noteOn(57, 0.9f);  /* A3 */
}

void loop() {
  /* Collect control bytes off whatever transport you wired. */
  while (Serial.available() > 0) {
    const uint8_t b = (uint8_t)Serial.read();
    feedControlBytes(&b, 1);
  }

  if (!ready || !out.started()) {
    delay(100);
    return;
  }

  /* Apply queued writes BETWEEN blocks, never during one. setParam() rewrites
   * kernel coefficients, and doing that halfway through a render would tear.
   * Anything the graph refused is counted rather than lost quietly. */
  control.drain(graph);
  if (control.dropped() || control.rejected()) {
    static uint32_t said = 0;
    const uint32_t bad = control.dropped() + control.rejected();
    if (bad != said) {
      Serial.printf("ctl: %u dropped (queue full), %u rejected (bad node/slot)\n",
                    control.dropped(), control.rejected());
      said = bad;
    }
  }

  graph.render(mono, BLOCK);
  for (int i = 0; i < BLOCK; ++i) {
    /* Graph output is bounded to ±1.2 by the runtime; scale with headroom so a
     * loud patch clips at the DAC rather than wrapping in the conversion. */
    float v = mono[i] * (1.0f / 1.2f);
    int32_t s = (int32_t)(v * 32767.0f);
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    stereo[i * 2] = (int16_t)s;
    stereo[i * 2 + 1] = (int16_t)s;
  }
  out.write(stereo, BLOCK);
}
