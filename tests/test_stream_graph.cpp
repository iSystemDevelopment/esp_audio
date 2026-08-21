/**
 * AudioStreamGraph — native tests. No board, no Arduino headers.
 *
 * Build (needs isystem_dsp_kernels.h exported from CraftAudio on the include
 * path — see tests/README.md):
 *
 *   cl /std:c++17 /EHsc /I include /I <kernels-dir> tests\test_stream_graph.cpp ^
 *      src\AudioStreamGraph.cpp /Fe:build\test_stream_graph.exe
 *
 *   g++ -std=c++17 -I include -I <kernels-dir> tests/test_stream_graph.cpp \
 *       src/AudioStreamGraph.cpp -o build/test_stream_graph
 *
 * These are the same behaviours the desktop runtime is held to, so the two
 * cannot drift apart quietly: reachability, pass-through summing, cycle safety,
 * bounds, and the report. Numeric agreement with the browser is a separate gate
 * and is NOT claimed here.
 */
#include "AudioStreamGraph.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static int passed = 0;
static std::vector<std::string> fails;

static void ok(const char *name, bool cond, const std::string &extra = "") {
  if (cond) { ++passed; return; }
  fails.push_back(std::string(name) + (extra.empty() ? "" : "  [" + extra + "]"));
}
static void eqi(const char *name, int got, int want) {
  ok(name, got == want, "got " + std::to_string(got) + " want " + std::to_string(want));
}

static AudioStreamNodeDesc node(const char *type) {
  AudioStreamNodeDesc d;
  std::memset(&d, 0, sizeof(d));
  d.type = type;
  d.kind = audio_stream_kind_from_type(type);
  return d;
}
static void setp(AudioStreamNodeDesc &d, int slot, float v) {
  d.p[slot] = v;
  d.p_set[slot] = true;
}
static AudioStreamEdgeDesc edge(int a, int b) {
  AudioStreamEdgeDesc e;
  e.from = (uint8_t)a;
  e.to = (uint8_t)b;
  return e;
}

struct Stats { float peak = 0.0f; bool finite = true; };

static Stats run(AudioStreamGraph &g, bool note, int blocks, int frames = 512) {
  std::vector<float> buf((size_t)frames);
  if (note) g.noteOn(69, 0.9f);
  Stats st;
  for (int b = 0; b < blocks; ++b) {
    g.render(buf.data(), frames);
    for (int i = 0; i < frames; ++i) {
      const float v = buf[(size_t)i];
      if (!std::isfinite(v)) st.finite = false;
      st.peak = std::max(st.peak, std::fabs(v));
    }
  }
  return st;
}

int main() {
  const float sr = 48000.0f;

  /* ---- type mapping ----------------------------------------------------- */
  eqi("oscillator maps", audio_stream_kind_from_type("isystem-dsp-oscillator"), AUDIO_STREAM_NODE_OSCILLATOR);
  eqi("filter maps", audio_stream_kind_from_type("isystem-dsp-filter"), AUDIO_STREAM_NODE_FILTER);
  eqi("audio-out maps", audio_stream_kind_from_type("isystem-dsp-audio-out"), AUDIO_STREAM_NODE_AUDIO_OUT);
  eqi("an unported type is pass-through", audio_stream_kind_from_type("isystem-dsp-adsr"), AUDIO_STREAM_NODE_PASSTHROUGH);
  eqi("null is pass-through", audio_stream_kind_from_type(0), AUDIO_STREAM_NODE_PASSTHROUGH);

  /* ---- a source must reach Audio Out ------------------------------------ */
  {
    AudioStreamGraph g;
    AudioStreamNodeDesc n[2] = { node("isystem-dsp-oscillator"), node("isystem-dsp-audio-out") };
    setp(n[0], AUDIO_STREAM_OSC_LEVEL, 0.8f);
    AudioStreamEdgeDesc e[1] = { edge(0, 1) };
    ok("osc -> out is playable", g.build(n, 2, e, 1, sr));
    ok("report says it reached the output", g.report().reached_output);
    eqi("one module besides Audio Out", g.report().modules, 1);
    eqi("rendered natively", g.report().rendered, 1);

    Stats st = run(g, true, 6);
    ok("a held note makes sound", st.peak > 0.05f, std::to_string(st.peak));
    ok("output is finite", st.finite);
    ok("output is bounded", st.peak <= 1.2001f, std::to_string(st.peak));
  }

  /* ---- silence when nothing is held ------------------------------------- */
  {
    AudioStreamGraph g;
    AudioStreamNodeDesc n[2] = { node("isystem-dsp-oscillator"), node("isystem-dsp-audio-out") };
    AudioStreamEdgeDesc e[1] = { edge(0, 1) };
    g.build(n, 2, e, 1, sr);
    ok("silent until a note arrives", run(g, false, 4).peak == 0.0f);
  }

  /* ---- no source: reported, not rendered as broken silence ---------------- */
  {
    AudioStreamGraph g;
    AudioStreamNodeDesc n[2] = { node("isystem-dsp-filter"), node("isystem-dsp-audio-out") };
    AudioStreamEdgeDesc e[1] = { edge(0, 1) };
    ok("filter alone is not playable", !g.build(n, 2, e, 1, sr));
    char buf[160];
    g.summary(buf, sizeof(buf));
    ok("summary names the problem", std::string(buf).find("no source") != std::string::npos, buf);
    ok("and it renders exact silence", run(g, true, 2).peak == 0.0f);
  }

  /* ---- an orphaned source does not count --------------------------------- */
  {
    AudioStreamGraph g;
    AudioStreamNodeDesc n[3] = { node("isystem-dsp-oscillator"), node("isystem-dsp-filter"),
                                 node("isystem-dsp-audio-out") };
    AudioStreamEdgeDesc e[1] = { edge(1, 2) };  /* osc wired to nothing */
    ok("an orphaned source is not playable", !g.build(n, 3, e, 1, sr));
  }

  /* ---- unported modules are reported, never approximated ----------------- */
  {
    AudioStreamGraph g;
    AudioStreamNodeDesc n[4] = { node("isystem-dsp-oscillator"), node("isystem-dsp-adsr"),
                                 node("isystem-dsp-lfo"), node("isystem-dsp-audio-out") };
    AudioStreamEdgeDesc e[3] = { edge(0, 1), edge(1, 2), edge(2, 3) };
    ok("still playable through pass-throughs", g.build(n, 4, e, 3, sr));
    eqi("three modules besides Audio Out", g.report().modules, 3);
    eqi("one rendered natively", g.report().rendered, 1);
    eqi("two passed through", g.report().passthrough, 2);
    char buf[200];
    g.summary(buf, sizeof(buf));
    const std::string s(buf);
    ok("summary counts natives", s.find("1/3") != std::string::npos, s);
    ok("summary names adsr", s.find("adsr") != std::string::npos, s);
    ok("summary names lfo", s.find("lfo") != std::string::npos, s);
  }

  /* ---- pass-through with several inputs SUMS them ------------------------ */
  {
    AudioStreamGraph one, two;
    AudioStreamNodeDesc a[3] = { node("isystem-dsp-oscillator"), node("isystem-dsp-mixer4"),
                                 node("isystem-dsp-audio-out") };
    setp(a[0], AUDIO_STREAM_OSC_LEVEL, 0.4f);
    AudioStreamEdgeDesc ae[2] = { edge(0, 1), edge(1, 2) };

    AudioStreamNodeDesc b[4] = { node("isystem-dsp-oscillator"), node("isystem-dsp-oscillator"),
                                 node("isystem-dsp-mixer4"), node("isystem-dsp-audio-out") };
    setp(b[0], AUDIO_STREAM_OSC_LEVEL, 0.4f);
    setp(b[1], AUDIO_STREAM_OSC_LEVEL, 0.4f);
    AudioStreamEdgeDesc be[3] = { edge(0, 2), edge(1, 2), edge(2, 3) };

    ok("one source through an unported module plays", one.build(a, 3, ae, 2, sr));
    ok("two sources into one unported module plays", two.build(b, 4, be, 3, sr));
    eqi("mixer4 is reported unported", one.report().passthrough, 1);

    Stats s1 = run(one, true, 6);
    Stats s2 = run(two, true, 6);
    ok("one source is audible", s1.peak > 0.05f, std::to_string(s1.peak));
    ok("two identical sources SUM rather than one winning", s2.peak > s1.peak * 1.5f,
       std::to_string(s1.peak) + " -> " + std::to_string(s2.peak));
  }

  /* ---- gain and mute ------------------------------------------------------ */
  {
    AudioStreamGraph loud, quiet, muted;
    AudioStreamNodeDesc n[3] = { node("isystem-dsp-oscillator"), node("isystem-dsp-gain"),
                                 node("isystem-dsp-audio-out") };
    AudioStreamEdgeDesc e[2] = { edge(0, 1), edge(1, 2) };
    setp(n[1], AUDIO_STREAM_GAIN_GAIN, 1.0f);
    loud.build(n, 3, e, 2, sr);
    setp(n[1], AUDIO_STREAM_GAIN_GAIN, 0.25f);
    quiet.build(n, 3, e, 2, sr);
    setp(n[1], AUDIO_STREAM_GAIN_GAIN, 1.0f);
    setp(n[1], AUDIO_STREAM_GAIN_MUTE, 1.0f);
    muted.build(n, 3, e, 2, sr);

    Stats a = run(loud, true, 6), b = run(quiet, true, 6), c = run(muted, true, 6);
    ok("gain 0.25 is audibly quieter", b.peak < a.peak * 0.5f,
       std::to_string(a.peak) + " -> " + std::to_string(b.peak));
    ok("but not silent", b.peak > 0.0f);
    ok("mute really mutes", c.peak == 0.0f, std::to_string(c.peak));
  }

  /* ---- a cycle must not hang ---------------------------------------------- */
  {
    AudioStreamGraph g;
    AudioStreamNodeDesc n[4] = { node("isystem-dsp-oscillator"), node("isystem-dsp-filter"),
                                 node("isystem-dsp-filter"), node("isystem-dsp-audio-out") };
    AudioStreamEdgeDesc e[4] = { edge(0, 1), edge(1, 2), edge(2, 1), edge(1, 3) };
    g.build(n, 4, e, 4, sr);
    ok("a cyclic graph still returns", run(g, true, 2).finite);
  }

  /* ---- junk in, no crash out ---------------------------------------------- */
  {
    AudioStreamGraph g;
    ok("null node array refused", !g.build(0, 0, 0, 0, sr));
    AudioStreamNodeDesc n[1] = { node("isystem-dsp-audio-out") };
    ok("audio-out alone is not playable", !g.build(n, 1, 0, 0, sr));
    float buf[16];
    g.render(buf, 16);
    ok("render on an unplayable graph is silent", buf[0] == 0.0f);
    ok("render tolerates a null buffer", (g.render(0, 16), true));
  }

  std::printf("test-stream-graph: %d passed, %d failed\n", passed, (int)fails.size());
  for (size_t i = 0; i < fails.size(); ++i) std::printf("  FAIL  %s\n", fails[i].c_str());
  return fails.empty() ? 0 : 1;
}
