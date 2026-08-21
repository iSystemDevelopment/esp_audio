#include "AudioStreamGraph.h"

/* MSVC and some desktop toolchains do not define M_PI without this. The kernels
 * header is written for the ESP32 toolchain, where <math.h> supplies it. Define
 * before including so the value never depends on include order. This is pi. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "isystem_dsp_kernels.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

using namespace isystem;

namespace {

/* Catalog defaults, quoted from isystem-stream-canvas.js PARAM_DEFS. A param the
 * patch does not set falls back here — never to an invented number. */
const float kOscWave = 2.0f;      /* 0 sine 1 tri 2 saw 3 square */
const float kOscLevel = 0.8f;
const float kFilterCutoff = 1200.0f;
const float kFilterRes = 4.0f;
const float kDelayTime = 0.28f;
const float kDelayFeedback = 0.35f;
const float kDelayWet = 0.25f;
const float kDistDrive = 0.35f;
const float kDistTone = 4200.0f;
const float kDistWet = 0.4f;
const float kGainGain = 1.0f;

/* Reads a slot off anything carrying the p[]/p_set[] pair — the incoming
 * descriptor at build time, and the node's own retained copy afterwards, which
 * is what a live setParam() edits. One reader means a param written over the
 * wire lands on exactly the same code path as a param that came in the patch. */
template <typename T>
float param_or(const T &d, int slot, float dflt) {
  if (slot < 0 || slot >= 6) return dflt;
  return d.p_set[slot] ? d.p[slot] : dflt;
}

float midi_to_hz(int note) {
  return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
}

bool streq(const char *a, const char *b) {
  return a && b && strcmp(a, b) == 0;
}

/* Short name for the report: "isystem-dsp-adsr" -> "adsr". */
const char *short_type(const char *type) {
  if (!type) return "?";
  const char *p = strstr(type, "isystem-dsp-");
  return p ? p + 12 : type;
}

}  // namespace

/** Per-node DSP state. One union-ish struct keeps allocation to a single block. */
struct AudioStreamNodeState {
  AudioStreamNodeKind kind;
  const char *type;
  bool is_source;
  bool ported;

  /* Only the member matching `kind` is live. Kept as plain members rather than a
   * union because IsDelay owns a large buffer and a union would not initialise. */
  IsOsc osc;
  float osc_level;
  float osc_ratio;

  IsFilter filt;
  IsDelay<AUDIO_STREAM_GRAPH_DELAY_MAX> dly;
  IsDistortion dist;
  float gain;

  /* The patch's own param values, retained. build() used to consume the
   * descriptor and throw it away, which made a live edit impossible: changing
   * a filter's cutoff means re-calling setParams(cutoff, resonance, mode), and
   * the resonance is only knowable if it was kept. */
  float p[6];
  bool p_set[6];
};

namespace {

/* Push a node's retained params into its kernel. Called once from build() and
 * again on every live write, so there is one place where a param becomes sound
 * and no way for the two paths to drift.
 *
 * Deliberately does NOT touch oscillator phase or any delay-line contents — a
 * knob move must not restart the tone or flush the echoes. */
void apply_params(AudioStreamNodeState &s) {
  switch (s.kind) {
    case AUDIO_STREAM_NODE_OSCILLATOR: {
      s.osc.wave = (int)param_or(s, AUDIO_STREAM_OSC_WAVE, kOscWave);
      s.osc_level = param_or(s, AUDIO_STREAM_OSC_LEVEL, kOscLevel);
      /* Pitch offsets multiply the played note. `unison`, `spread` and `sub`
       * exist in the catalog with no ported kernel here, so they are ignored
       * rather than faked — the report is where that is admitted. */
      const float oct = param_or(s, AUDIO_STREAM_OSC_OCTAVE, 0.0f);
      const float coarse = param_or(s, AUDIO_STREAM_OSC_COARSE, 0.0f);
      const float detune = param_or(s, AUDIO_STREAM_OSC_DETUNE, 0.0f);
      s.osc_ratio = powf(2.0f, oct + coarse / 12.0f + detune / 1200.0f);
      break;
    }
    case AUDIO_STREAM_NODE_FILTER:
      /* The catalog filter exposes cutoff and resonance only; the kernel's
       * mode selector has no catalog param, so it stays low-pass (0). */
      s.filt.setParams(param_or(s, AUDIO_STREAM_FILTER_CUTOFF, kFilterCutoff),
                       param_or(s, AUDIO_STREAM_FILTER_RESONANCE, kFilterRes), 0);
      break;
    case AUDIO_STREAM_NODE_DELAY:
      /* The catalog also has `filter` and `mode` (digital/tape). IsDelay takes
       * neither, so they are not applied — do not pretend otherwise. */
      s.dly.setParams(param_or(s, AUDIO_STREAM_DELAY_TIME, kDelayTime),
                      param_or(s, AUDIO_STREAM_DELAY_FEEDBACK, kDelayFeedback),
                      param_or(s, AUDIO_STREAM_DELAY_WET, kDelayWet));
      break;
    case AUDIO_STREAM_NODE_DISTORTION:
      s.dist.setParams(param_or(s, AUDIO_STREAM_DIST_DRIVE, kDistDrive),
                       param_or(s, AUDIO_STREAM_DIST_TONE, kDistTone),
                       param_or(s, AUDIO_STREAM_DIST_WET, kDistWet));
      break;
    case AUDIO_STREAM_NODE_GAIN:
      s.gain = param_or(s, AUDIO_STREAM_GAIN_MUTE, 0.0f) >= 0.5f
                   ? 0.0f
                   : param_or(s, AUDIO_STREAM_GAIN_GAIN, kGainGain);
      break;
    default:
      break;
  }
}

}  // namespace

AudioStreamNodeKind audio_stream_kind_from_type(const char *t) {
  if (streq(t, "isystem-dsp-oscillator")) return AUDIO_STREAM_NODE_OSCILLATOR;
  if (streq(t, "isystem-dsp-filter")) return AUDIO_STREAM_NODE_FILTER;
  if (streq(t, "isystem-dsp-delay")) return AUDIO_STREAM_NODE_DELAY;
  if (streq(t, "isystem-dsp-distortion")) return AUDIO_STREAM_NODE_DISTORTION;
  if (streq(t, "isystem-dsp-gain")) return AUDIO_STREAM_NODE_GAIN;
  if (streq(t, "isystem-dsp-audio-out")) return AUDIO_STREAM_NODE_AUDIO_OUT;
  return AUDIO_STREAM_NODE_PASSTHROUGH;
}

AudioStreamGraph::AudioStreamGraph()
    : state_(0), node_count_(0), order_len_(0), edge_count_(0), output_node_(-1),
      sr_(44100.0f), playable_(false), held_count_(0), gate_(0.0f),
      gate_step_(1.0f), note_hz_(440.0f) {
  memset(&report_, 0, sizeof(report_));
  memset(out_, 0, sizeof(out_));
}

AudioStreamGraph::~AudioStreamGraph() {
  delete[] state_;
}

bool AudioStreamGraph::build(const AudioStreamNodeDesc *nodes, int node_count,
                             const AudioStreamEdgeDesc *edges, int edge_count,
                             float sample_rate) {
  delete[] state_;
  state_ = 0;
  node_count_ = 0;
  order_len_ = 0;
  edge_count_ = 0;
  output_node_ = -1;
  playable_ = false;
  memset(&report_, 0, sizeof(report_));
  memset(out_, 0, sizeof(out_));

  if (!nodes || node_count <= 0) return false;
  if (node_count > AUDIO_STREAM_GRAPH_MAX_NODES) node_count = AUDIO_STREAM_GRAPH_MAX_NODES;
  if (edge_count > AUDIO_STREAM_GRAPH_MAX_EDGES) edge_count = AUDIO_STREAM_GRAPH_MAX_EDGES;
  if (edge_count < 0) edge_count = 0;

  sr_ = sample_rate > 0.0f ? sample_rate : 44100.0f;
  state_ = new AudioStreamNodeState[node_count];
  node_count_ = node_count;

  for (int i = 0; i < node_count; ++i) {
    AudioStreamNodeState &s = state_[i];
    const AudioStreamNodeDesc &d = nodes[i];
    memset(&s, 0, sizeof(AudioStreamNodeState));
    s.kind = d.kind;
    s.type = d.type;
    s.is_source = (d.kind == AUDIO_STREAM_NODE_OSCILLATOR);
    s.ported = (d.kind != AUDIO_STREAM_NODE_PASSTHROUGH);

    memcpy(s.p, d.p, sizeof(s.p));
    memcpy(s.p_set, d.p_set, sizeof(s.p_set));
    if (d.kind == AUDIO_STREAM_NODE_OSCILLATOR) s.osc.phase = 0.0f;
    if (d.kind == AUDIO_STREAM_NODE_AUDIO_OUT) output_node_ = i;
    apply_params(s);

    if (d.kind == AUDIO_STREAM_NODE_AUDIO_OUT) continue;
    report_.modules++;
    if (s.ported) report_.rendered++;
    else report_.passthrough++;
  }

  /* Edges, and the in-degree Kahn needs. */
  int indeg[AUDIO_STREAM_GRAPH_MAX_NODES];
  memset(indeg, 0, sizeof(indeg));
  for (int e = 0; e < edge_count; ++e) {
    const int a = edges[e].from;
    const int b = edges[e].to;
    if (a < 0 || a >= node_count || b < 0 || b >= node_count || a == b) continue;
    in_from_[edge_count_] = (uint8_t)a;
    in_to_[edge_count_] = (uint8_t)b;
    edge_count_++;
    indeg[b]++;
  }

  /* Kahn. A cycle would spin forever in render(); nodes left with a non-zero
   * in-degree are simply dropped from the order and never run. */
  int queue[AUDIO_STREAM_GRAPH_MAX_NODES];
  int qn = 0;
  for (int i = 0; i < node_count; ++i)
    if (indeg[i] == 0) queue[qn++] = i;
  for (int q = 0; q < qn; ++q) {
    const int cur = queue[q];
    order_[order_len_++] = cur;
    for (int e = 0; e < edge_count_; ++e) {
      if (in_from_[e] != cur) continue;
      const int nx = in_to_[e];
      if (--indeg[nx] == 0) queue[qn++] = nx;
    }
  }

  /* Playable means a source actually REACHES Audio Out, not that both exist. */
  if (output_node_ >= 0) {
    bool seen[AUDIO_STREAM_GRAPH_MAX_NODES];
    memset(seen, 0, sizeof(seen));
    int stack[AUDIO_STREAM_GRAPH_MAX_NODES];
    int sn = 0;
    stack[sn++] = output_node_;
    while (sn > 0) {
      const int cur = stack[--sn];
      if (seen[cur]) continue;
      seen[cur] = true;
      if (state_[cur].is_source) {
        playable_ = true;
        break;
      }
      for (int e = 0; e < edge_count_; ++e)
        if (in_to_[e] == cur) stack[sn++] = in_from_[e];
    }
  }
  report_.reached_output = playable_;

  prepare(sr_);
  return playable_;
}

int audio_stream_slot_count(AudioStreamNodeKind kind) {
  switch (kind) {
    case AUDIO_STREAM_NODE_OSCILLATOR: return 5;
    case AUDIO_STREAM_NODE_FILTER:     return 2;
    case AUDIO_STREAM_NODE_DELAY:      return 3;
    case AUDIO_STREAM_NODE_DISTORTION: return 3;
    case AUDIO_STREAM_NODE_GAIN:       return 2;
    default: return 0; /* pass-through and audio-out own no params */
  }
}

bool AudioStreamGraph::setParam(int node, int slot, float value) {
  if (!state_ || node < 0 || node >= node_count_) return false;
  AudioStreamNodeState &s = state_[node];
  if (slot < 0 || slot >= audio_stream_slot_count(s.kind)) return false;
  /* A NaN would propagate through the kernel and poison the delay line for the
   * life of the patch, so it is refused at the door rather than clamped. */
  if (!(value == value)) return false;
  s.p[slot] = value;
  s.p_set[slot] = true;
  apply_params(s);
  return true;
}

bool AudioStreamGraph::getParam(int node, int slot, float *out) const {
  if (!out || !state_ || node < 0 || node >= node_count_) return false;
  const AudioStreamNodeState &s = state_[node];
  if (slot < 0 || slot >= audio_stream_slot_count(s.kind)) return false;
  if (!s.p_set[slot]) return false; /* unset means "the catalog default", not 0 */
  *out = s.p[slot];
  return true;
}

AudioStreamNodeKind AudioStreamGraph::nodeKind(int node) const {
  if (!state_ || node < 0 || node >= node_count_) return AUDIO_STREAM_NODE_PASSTHROUGH;
  return state_[node].kind;
}

void AudioStreamGraph::prepare(float sample_rate) {
  sr_ = sample_rate > 0.0f ? sample_rate : 44100.0f;
  gate_step_ = 1.0f / ((AUDIO_STREAM_GRAPH_GATE_MS / 1000.0f) * sr_);
  for (int i = 0; i < node_count_; ++i) {
    AudioStreamNodeState &s = state_[i];
    switch (s.kind) {
      case AUDIO_STREAM_NODE_OSCILLATOR: s.osc.phase = 0.0f; break;
      case AUDIO_STREAM_NODE_FILTER: s.filt.reset(sr_); break;
      case AUDIO_STREAM_NODE_DELAY: s.dly.reset(sr_); break;
      case AUDIO_STREAM_NODE_DISTORTION: s.dist.reset(sr_); break;
      default: break;
    }
  }
}

void AudioStreamGraph::reset() {
  prepare(sr_);
  memset(out_, 0, sizeof(out_));
  held_count_ = 0;
  gate_ = 0.0f;
}

void AudioStreamGraph::noteOn(int midi_note, float velocity) {
  (void)velocity;
  note_hz_ = midi_to_hz(midi_note);
  held_count_++;
}

void AudioStreamGraph::noteOff(int midi_note) {
  (void)midi_note;
  if (held_count_ > 0) held_count_--;
}

void AudioStreamGraph::allNotesOff() { held_count_ = 0; }

void AudioStreamGraph::render(float *out, int frames) {
  if (!out || frames <= 0) return;
  if (!playable_ || output_node_ < 0) {
    memset(out, 0, sizeof(float) * (size_t)frames);
    return;
  }
  const float target = held_count_ > 0 ? 1.0f : 0.0f;

  for (int i = 0; i < frames; ++i) {
    if (gate_ < target) {
      gate_ += gate_step_;
      if (gate_ > target) gate_ = target;
    } else if (gate_ > target) {
      gate_ -= gate_step_;
      if (gate_ < target) gate_ = target;
    }

    for (int oi = 0; oi < order_len_; ++oi) {
      const int idx = order_[oi];
      /* Every node receives the SUM of its inputs. A pass-through returns that
       * sum unchanged, so an unported mixer behaves as a unity-gain summing
       * mixer — it does not pick one input and drop the rest. */
      float in = 0.0f;
      for (int e = 0; e < edge_count_; ++e)
        if (in_to_[e] == idx) in += out_[in_from_[e]];

      AudioStreamNodeState &s = state_[idx];
      float y = in;
      switch (s.kind) {
        case AUDIO_STREAM_NODE_OSCILLATOR:
          y = s.osc.tick((note_hz_ * s.osc_ratio) / sr_) * s.osc_level * gate_;
          break;
        case AUDIO_STREAM_NODE_FILTER: y = s.filt.process(in); break;
        case AUDIO_STREAM_NODE_DELAY: y = s.dly.process(in); break;
        case AUDIO_STREAM_NODE_DISTORTION: y = s.dist.process(in); break;
        case AUDIO_STREAM_NODE_GAIN: y = in * s.gain; break;
        default: break; /* pass-through and audio-out both return the sum */
      }
      out_[idx] = y;
    }

    float v = out_[output_node_];
    if (!(v == v)) v = 0.0f; /* NaN guard without <cmath> */
    if (v > 1.2f) v = 1.2f;
    if (v < -1.2f) v = -1.2f;
    out[i] = v;
  }
}

int AudioStreamGraph::summary(char *buf, int buf_len) const {
  if (!buf || buf_len <= 0) return 0;
  if (!report_.reached_output) {
    return snprintf(buf, (size_t)buf_len, "no source reaches Audio Out (silent)");
  }
  int n = snprintf(buf, (size_t)buf_len, "%d/%d modules native",
                   report_.rendered, report_.modules);
  if (report_.passthrough > 0 && n < buf_len) {
    n += snprintf(buf + n, (size_t)(buf_len - n), ", %d passed through (",
                  report_.passthrough);
    bool first = true;
    for (int i = 0; i < node_count_ && n < buf_len; ++i) {
      if (state_[i].ported || state_[i].kind == AUDIO_STREAM_NODE_AUDIO_OUT) continue;
      n += snprintf(buf + n, (size_t)(buf_len - n), "%s%s", first ? "" : ", ",
                    short_type(state_[i].type));
      first = false;
    }
    if (n < buf_len) n += snprintf(buf + n, (size_t)(buf_len - n), ")");
  }
  return n;
}
