/**
 * AudioStreamPatch — native tests. No board.
 *
 * The loader is the join between the desk and the runtime, so the cases that
 * matter are the ugly ones: a param nested in a descriptor rather than a bare
 * number, a connection naming a module that is not there, a patch bigger than
 * the caps, and anything that could make it silently load half a patch.
 *
 * Build: see tests/README.md (needs isystem_dsp_kernels.h on the include path).
 */
#include "AudioStreamPatch.h"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstring>
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
static void eqf(const char *name, float got, float want) {
  ok(name, got == want, "got " + std::to_string(got) + " want " + std::to_string(want));
}

/* The loader mutates its input, so every case gets its own copy. */
struct Doc {
  std::vector<char> buf;
  explicit Doc(const char *s) : buf(s, s + strlen(s) + 1) {}
  char *get() { return buf.data(); }
};

static const char *kRealPatch =
  "{\"schemaVersion\":1,\"kind\":\"isystem-stream\",\"title\":\"Acid\","
  "\"modules\":{"
    "\"o1\":{\"type\":\"isystem-dsp-oscillator\",\"params\":{"
        "\"wave\":{\"value\":3},\"level\":{\"value\":0.74},\"detune\":{\"value\":66}}},"
    "\"fl1\":{\"type\":\"isystem-dsp-filter\",\"params\":{"
        "\"cutoff\":{\"value\":1116},\"resonance\":{\"value\":4.8}}},"
    "\"mx1\":{\"type\":\"isystem-dsp-mixer4\",\"params\":{\"g1\":{\"value\":0}}},"
    "\"out\":{\"type\":\"isystem-dsp-audio-out\",\"params\":{}}"
  "},"
  "\"connections\":["
    "{\"from\":{\"module\":\"o1\",\"port\":\"a\"},\"to\":{\"module\":\"fl1\",\"port\":\"a\"}},"
    "{\"from\":{\"module\":\"fl1\"},\"to\":{\"module\":\"mx1\"}},"
    "{\"from\":{\"module\":\"mx1\"},\"to\":{\"module\":\"out\"}}"
  "]}";

int main() {
  AudioStreamNodeDesc nodes[AUDIO_STREAM_GRAPH_MAX_NODES];
  AudioStreamEdgeDesc edges[AUDIO_STREAM_GRAPH_MAX_EDGES];

  /* ---- a real patch ------------------------------------------------------ */
  {
    Doc d(kRealPatch);
    AudioStreamPatchResult r = audio_stream_patch_parse(d.get(), nodes, 48, edges, 96);
    eqi("status ok", r.status, AUDIO_STREAM_PATCH_OK);
    eqi("four modules", r.node_count, 4);
    eqi("three connections", r.edge_count, 3);
    eqi("nothing dropped", r.dropped_nodes + r.dropped_edges, 0);

    eqi("osc kind", nodes[0].kind, AUDIO_STREAM_NODE_OSCILLATOR);
    eqi("filter kind", nodes[1].kind, AUDIO_STREAM_NODE_FILTER);
    eqi("mixer4 is pass-through", nodes[2].kind, AUDIO_STREAM_NODE_PASSTHROUGH);
    eqi("audio-out kind", nodes[3].kind, AUDIO_STREAM_NODE_AUDIO_OUT);
    ok("type string is borrowed intact", strcmp(nodes[2].type, "isystem-dsp-mixer4") == 0, nodes[2].type);

    /* Params come out of the nested {"value": N} descriptor, not a bare number. */
    ok("wave is set", nodes[0].p_set[AUDIO_STREAM_OSC_WAVE]);
    eqf("wave value", nodes[0].p[AUDIO_STREAM_OSC_WAVE], 3.0f);
    eqf("level value", nodes[0].p[AUDIO_STREAM_OSC_LEVEL], 0.74f);
    eqf("detune value", nodes[0].p[AUDIO_STREAM_OSC_DETUNE], 66.0f);
    ok("an unset param stays unset", !nodes[0].p_set[AUDIO_STREAM_OSC_OCTAVE],
       "the graph must fall back to the catalog default, not to zero");
    eqf("cutoff is the LIVE value, not the catalog default",
        nodes[1].p[AUDIO_STREAM_FILTER_CUTOFF], 1116.0f);
    eqf("resonance", nodes[1].p[AUDIO_STREAM_FILTER_RESONANCE], 4.8f);

    /* Edge indices must point at the right modules, in patch order. */
    eqi("edge 0 from o1", edges[0].from, 0);
    eqi("edge 0 to fl1", edges[0].to, 1);
    eqi("edge 2 to out", edges[2].to, 3);
  }

  /* ---- it drives the graph end to end ------------------------------------ */
  {
    Doc d(kRealPatch);
    AudioStreamPatchResult r = audio_stream_patch_parse(d.get(), nodes, 48, edges, 96);
    AudioStreamGraph g;
    ok("parsed patch is playable", g.build(nodes, r.node_count, edges, r.edge_count, 48000.0f));
    eqi("three modules besides Audio Out", g.report().modules, 3);
    eqi("two rendered natively", g.report().rendered, 2);
    eqi("mixer4 passed through", g.report().passthrough, 1);
    char buf[200];
    g.summary(buf, sizeof(buf));
    ok("summary names mixer4", std::string(buf).find("mixer4") != std::string::npos, buf);

    g.noteOn(69, 0.9f);
    std::vector<float> out(512);
    float peak = 0.0f;
    for (int b = 0; b < 6; ++b) {
      g.render(out.data(), 512);
      for (int i = 0; i < 512; ++i) peak = std::max(peak, std::fabs(out[(size_t)i]));
    }
    ok("a patch loaded from JSON makes sound", peak > 0.02f, std::to_string(peak));
    ok("and stays bounded", peak <= 1.2001f, std::to_string(peak));
  }

  /* ---- bare numeric params are accepted too ------------------------------- */
  {
    Doc d("{\"modules\":{\"g\":{\"type\":\"isystem-dsp-gain\",\"params\":{\"gain\":0.5}},"
          "\"o\":{\"type\":\"isystem-dsp-audio-out\"}},\"connections\":[]}");
    AudioStreamPatchResult r = audio_stream_patch_parse(d.get(), nodes, 48, edges, 96);
    eqi("two modules", r.node_count, 2);
    eqf("bare number read", nodes[0].p[AUDIO_STREAM_GAIN_GAIN], 0.5f);
  }

  /* ---- a module with no params at all ------------------------------------ */
  {
    Doc d("{\"modules\":{\"o\":{\"type\":\"isystem-dsp-oscillator\"}}}");
    AudioStreamPatchResult r = audio_stream_patch_parse(d.get(), nodes, 48, edges, 96);
    eqi("still parses", r.status, AUDIO_STREAM_PATCH_OK);
    eqi("one module", r.node_count, 1);
    ok("no params set", !nodes[0].p_set[AUDIO_STREAM_OSC_WAVE]);
  }

  /* ---- broken references are COUNTED, never silently swallowed ------------ */
  {
    Doc d("{\"modules\":{\"a\":{\"type\":\"isystem-dsp-oscillator\"},"
          "\"b\":{\"type\":\"isystem-dsp-audio-out\"}},"
          "\"connections\":[{\"from\":{\"module\":\"a\"},\"to\":{\"module\":\"ghost\"}},"
          "{\"from\":{\"module\":\"a\"},\"to\":{\"module\":\"a\"}},"
          "{\"from\":{\"module\":\"a\"},\"to\":{\"module\":\"b\"}}]}");
    AudioStreamPatchResult r = audio_stream_patch_parse(d.get(), nodes, 48, edges, 96);
    eqi("only the good edge survives", r.edge_count, 1);
    eqi("the ghost and the self-loop are counted", r.dropped_edges, 2);
    eqi("status still ok — edges dropping is not a load failure", r.status, AUDIO_STREAM_PATCH_OK);
  }

  /* ---- over the node cap: reports the REAL total ------------------------- */
  {
    std::string big = "{\"modules\":{";
    for (int i = 0; i < 10; ++i) {
      if (i) big += ",";
      big += "\"n" + std::to_string(i) + "\":{\"type\":\"isystem-dsp-gain\"}";
    }
    big += "}}";
    Doc d(big.c_str());
    AudioStreamPatchResult r = audio_stream_patch_parse(d.get(), nodes, 4, edges, 96);
    eqi("filled to the cap", r.node_count, 4);
    eqi("and reported every module it could not take", r.dropped_nodes, 6);
    eqi("status says so", r.status, AUDIO_STREAM_PATCH_ERR_TOO_MANY);
  }

  /* ---- junk in, no crash out --------------------------------------------- */
  {
    AudioStreamPatchResult r = audio_stream_patch_parse(0, nodes, 48, edges, 96);
    eqi("null json", r.status, AUDIO_STREAM_PATCH_ERR_EMPTY);
    Doc e("");
    eqi("empty json", audio_stream_patch_parse(e.get(), nodes, 48, edges, 96).status,
        AUDIO_STREAM_PATCH_ERR_EMPTY);
    Doc n("{\"title\":\"no modules here\"}");
    eqi("no modules key", audio_stream_patch_parse(n.get(), nodes, 48, edges, 96).status,
        AUDIO_STREAM_PATCH_ERR_NO_MODULES);
    Doc t("{\"modules\":{\"a\":{\"type\":\"isystem-dsp-gain\"");
    ok("truncated json does not crash",
       audio_stream_patch_parse(t.get(), nodes, 48, edges, 96).status != AUDIO_STREAM_PATCH_OK);
    Doc b("not json at all");
    eqi("garbage", audio_stream_patch_parse(b.get(), nodes, 48, edges, 96).status,
        AUDIO_STREAM_PATCH_ERR_NO_MODULES);
  }

  /* ---- a nested params object must not be read as the next module -------- */
  {
    Doc d("{\"modules\":{"
          "\"a\":{\"type\":\"isystem-dsp-filter\",\"params\":{\"cutoff\":{\"value\":900}}},"
          "\"b\":{\"type\":\"isystem-dsp-audio-out\"}}}");
    AudioStreamPatchResult r = audio_stream_patch_parse(d.get(), nodes, 48, edges, 96);
    eqi("exactly two modules, not three", r.node_count, 2);
    eqi("second is the audio out", nodes[1].kind, AUDIO_STREAM_NODE_AUDIO_OUT);
  }

  /* ---- the shipped example must actually work ---------------------------- */
  /* An example whose patch does not parse is worse than no example. Read the
     real .ino so this cannot rot when the sketch is edited. */
  {
    const char *candidates[] = {
      "examples/stream-graph/stream_graph.ino",
      "../examples/stream-graph/stream_graph.ino",
      "../../examples/stream-graph/stream_graph.ino"
    };
    std::string ino;
    for (int i = 0; i < 3 && ino.empty(); ++i) {
      std::FILE *f = std::fopen(candidates[i], "rb");
      if (!f) continue;
      char chunk[4096];
      size_t n;
      while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) ino.append(chunk, n);
      std::fclose(f);
    }
    ok("found the example sketch", !ino.empty(),
       "run from the repo root or build/ so the relative path resolves");
    if (!ino.empty()) {
      /* Pull the C string literal pieces out of kPatch[] and un-escape them. */
      const size_t a = ino.find("static char kPatch[] =");
      const size_t b = ino.find(";", a);
      ok("kPatch literal located", a != std::string::npos && b != std::string::npos);
      std::string lit = ino.substr(a, b - a), json;
      bool in = false;
      for (size_t i = 0; i < lit.size(); ++i) {
        if (!in) { if (lit[i] == 0x22) in = true; continue; }
        if (lit[i] == 0x5C && i + 1 < lit.size()) { json += lit[++i]; continue; }
        if (lit[i] == 0x22) { in = false; continue; }
        json += lit[i];
      }
      Doc d(json.c_str());
      AudioStreamPatchResult r = audio_stream_patch_parse(d.get(), nodes, 48, edges, 96);
      eqi("the example patch parses", r.status, AUDIO_STREAM_PATCH_OK);
      eqi("and drops nothing", r.dropped_nodes + r.dropped_edges, 0);
      AudioStreamGraph g;
      ok("the example patch is playable", g.build(nodes, r.node_count, edges, r.edge_count, 44100.0f));
      g.noteOn(57, 0.9f);
      std::vector<float> o(256);
      float peak = 0.0f;
      for (int blk = 0; blk < 8; ++blk) {
        g.render(o.data(), 256);
        for (int i = 0; i < 256; ++i) peak = std::max(peak, std::fabs(o[(size_t)i]));
      }
      ok("the example patch makes sound", peak > 0.02f, std::to_string(peak));
    }
  }

  /* ---- status text is never empty ---------------------------------------- */
  for (int s = AUDIO_STREAM_PATCH_OK; s <= AUDIO_STREAM_PATCH_ERR_MALFORMED; ++s) {
    const char *t = audio_stream_patch_status_text((AudioStreamPatchStatus)s);
    ok("status text present", t && *t, std::to_string(s));
  }

  std::printf("test-stream-patch: %d passed, %d failed\n", passed, (int)fails.size());
  for (size_t i = 0; i < fails.size(); ++i) std::printf("  FAIL  %s\n", fails[i].c_str());
  return fails.empty() ? 0 : 1;
}
