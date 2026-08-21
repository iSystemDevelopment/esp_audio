#pragma once
#ifndef ESPAUDIO_AUDIO_STREAM_GRAPH_H
#define ESPAUDIO_AUDIO_STREAM_GRAPH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** Runtime renderer for a CraftAudio stream patch.
 *
 *  A patch is DATA, not code. Build it in CraftAudio, send it to the board, and
 *  this walks the module graph once per sample. There is no per-patch compile
 *  and no class-per-node codegen — the same relationship the desktop VST3
 *  wrapper has with a .sth file.
 *
 *  ── The DSP is NOT in this library ──────────────────────────────────────────
 *  This renders using `isystem_dsp_kernels.h`, which you export from CraftAudio
 *  (Export Studio Code → C++). That header is generated from the same source as
 *  the browser preview, so the board runs the math you auditioned. It is not
 *  redistributed here: this MIT library ships the INTERPRETER, you bring the
 *  kernels. Drop the exported header beside your sketch and the include below
 *  resolves.
 *
 *  ── What it will and will not run ───────────────────────────────────────────
 *  Rendered natively: oscillator · filter · delay · distortion · gain ·
 *  audio-out.
 *
 *  Anything else is a PASS-THROUGH: it receives the SUM of its inputs and
 *  returns it unchanged, and is named in the report. Nothing is approximated
 *  with a lookalike — a patch that cannot be run fully says so rather than
 *  quietly sounding different.
 *
 *  MIDI is monophonic, last-note. The short gate ramp is a designed anti-click
 *  control, NOT an envelope; there is no ported ADSR, so an ADSR module in the
 *  patch is one of the pass-through types.
 *
 *  Not Octopus PRO ROM. No task spawning, no I2S ownership — you call render()
 *  from your own audio task and hand the buffer to AudioOutputI2S.
 */

#ifndef AUDIO_STREAM_GRAPH_MAX_NODES
#define AUDIO_STREAM_GRAPH_MAX_NODES 48
#endif
#ifndef AUDIO_STREAM_GRAPH_MAX_EDGES
#define AUDIO_STREAM_GRAPH_MAX_EDGES 96
#endif
/** Delay line length in samples. 1.5 s at 48 kHz is the kernel's own ceiling. */
#ifndef AUDIO_STREAM_GRAPH_DELAY_MAX
#define AUDIO_STREAM_GRAPH_DELAY_MAX 72000
#endif
/** Anti-click gate ramp. A designed control, not an envelope. */
#ifndef AUDIO_STREAM_GRAPH_GATE_MS
#define AUDIO_STREAM_GRAPH_GATE_MS 5.0f
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Module kinds this runtime renders natively. Everything else passes through. */
typedef enum {
  AUDIO_STREAM_NODE_PASSTHROUGH = 0,
  AUDIO_STREAM_NODE_OSCILLATOR,
  AUDIO_STREAM_NODE_FILTER,
  AUDIO_STREAM_NODE_DELAY,
  AUDIO_STREAM_NODE_DISTORTION,
  AUDIO_STREAM_NODE_GAIN,
  AUDIO_STREAM_NODE_AUDIO_OUT
} AudioStreamNodeKind;

/** Map a CraftAudio catalog type (`isystem-dsp-filter`) to a kind. */
AudioStreamNodeKind audio_stream_kind_from_type(const char *catalog_type);

/** One module, already resolved from the patch. Params are catalog units. */
typedef struct {
  AudioStreamNodeKind kind;
  /** Catalog type string, kept for the report. Not owned — must outlive the graph. */
  const char *type;
  /** Up to 6 params, meaning per kind. Unset entries take the catalog default. */
  float p[6];
  bool p_set[6];
} AudioStreamNodeDesc;

/** A wire. Indices into the node array handed to build(). */
typedef struct {
  uint8_t from;
  uint8_t to;
} AudioStreamEdgeDesc;

/** What the runtime could and could not do with the patch it was given. */
typedef struct {
  int modules;        /**< modules excluding Audio Out */
  int rendered;       /**< run with a real kernel */
  int passthrough;    /**< recognised position, no ported kernel */
  bool reached_output; /**< a source actually reaches Audio Out */
} AudioStreamGraphReport;

/* Param slots, per kind — keeps callers off magic indices. */
enum { AUDIO_STREAM_OSC_WAVE = 0, AUDIO_STREAM_OSC_LEVEL, AUDIO_STREAM_OSC_OCTAVE,
       AUDIO_STREAM_OSC_COARSE, AUDIO_STREAM_OSC_DETUNE };
enum { AUDIO_STREAM_FILTER_CUTOFF = 0, AUDIO_STREAM_FILTER_RESONANCE };
enum { AUDIO_STREAM_DELAY_TIME = 0, AUDIO_STREAM_DELAY_FEEDBACK, AUDIO_STREAM_DELAY_WET };
enum { AUDIO_STREAM_DIST_DRIVE = 0, AUDIO_STREAM_DIST_TONE, AUDIO_STREAM_DIST_WET };
enum { AUDIO_STREAM_GAIN_GAIN = 0, AUDIO_STREAM_GAIN_MUTE };

/** How many param slots a kind actually owns. A slot past this is refused by
 *  setParam() rather than written into a gap nothing reads. */
int audio_stream_slot_count(AudioStreamNodeKind kind);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

/** Opaque per-node DSP state. Sized by the header, allocated inside the graph. */
struct AudioStreamNodeState;

class AudioStreamGraph {
 public:
  AudioStreamGraph();
  ~AudioStreamGraph();

  /** Wire a patch. Returns false when no source reaches Audio Out — that is a
   *  silent patch, and it is reported rather than rendered as broken silence. */
  bool build(const AudioStreamNodeDesc *nodes, int node_count,
             const AudioStreamEdgeDesc *edges, int edge_count,
             float sample_rate);

  void prepare(float sample_rate);
  void reset();

  /** Write one param of one module, live, without rebuilding or reflashing.
   *
   *  `node` is the module's index in the array handed to build() — which is the
   *  order the modules appear in the patch document, so the desk, the panel and
   *  the board all count the same way. `slot` is the kind's own slot enum.
   *  `value` is in CATALOG UNITS (Hz, seconds, 0..1) exactly as the patch
   *  carries it: nothing here knows a param's range, so nothing here can
   *  disagree with the desk about one.
   *
   *  Returns false — and changes nothing — for an out-of-range node, a slot the
   *  kind does not own, or a NaN. A caller that wants to know a write landed can
   *  check; a caller that ignores it still cannot corrupt the graph.
   *
   *  NOT thread-safe against render(). Call it from the audio thread, or push
   *  through AudioStreamControlQueue and drain between blocks. */
  bool setParam(int node, int slot, float value);

  /** Current value of a slot. False when the patch never set it — that means
   *  "the catalog default applies", which is not the same as zero. */
  bool getParam(int node, int slot, float *out) const;

  /** Kind of a node, for a caller resolving slots. Out-of-range reads as
   *  pass-through, which owns no slots, so a bad index cannot write anything. */
  AudioStreamNodeKind nodeKind(int node) const;

  int nodeCount() const { return node_count_; }

  /** Note on / off. Monophonic, last-note priority. */
  void noteOn(int midi_note, float velocity);
  void noteOff(int midi_note);
  void allNotesOff();

  /** Render `frames` mono samples. Adds nothing when the patch is unplayable. */
  void render(float *out, int frames);

  bool playable() const { return playable_; }
  const AudioStreamGraphReport &report() const { return report_; }

  /** One line for a display or a log: "6/8 native, 2 passed through (adsr, lfo)". */
  int summary(char *buf, int buf_len) const;

 private:
  AudioStreamNodeState *state_;   /**< node_count entries */
  int node_count_;
  int order_[AUDIO_STREAM_GRAPH_MAX_NODES];
  int order_len_;
  uint8_t in_from_[AUDIO_STREAM_GRAPH_MAX_EDGES];
  uint8_t in_to_[AUDIO_STREAM_GRAPH_MAX_EDGES];
  int edge_count_;
  float out_[AUDIO_STREAM_GRAPH_MAX_NODES];
  int output_node_;

  float sr_;
  bool playable_;
  AudioStreamGraphReport report_;

  int held_count_;
  float gate_;
  float gate_step_;
  float note_hz_;

  AudioStreamGraph(const AudioStreamGraph &);
  AudioStreamGraph &operator=(const AudioStreamGraph &);
};

#endif /* __cplusplus */
#endif /* ESPAUDIO_AUDIO_STREAM_GRAPH_H */
