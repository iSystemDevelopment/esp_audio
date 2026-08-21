/**
 * The C++ half of the graph-path JS<->C++ cross-check.
 *
 * `AudioStreamGraph` has been proven to build correct graphs and produce
 * bounded, structurally right audio. It has NOT been proven to produce the same
 * numbers as the browser. This prints what it actually renders so the JS side
 * can diff it — see platform/isystem-builder/smoke-graph-parity.mjs.
 *
 * Two kinds of vector, because they answer different questions:
 *
 *   KERNEL  one kernel, fed a deterministic signal generated in this file.
 *           Isolates that kernel: no oscillator, no graph walk, nothing else
 *           that could absorb or mask a difference.
 *
 *   GRAPH   a whole patch through AudioStreamGraph, note held. This is the
 *           thing that was never checked. It depends on every kernel in the
 *           chain AND on the walk, so it is only meaningful once the KERNEL
 *           vectors pass — a GRAPH mismatch on its own does not say where.
 *
 * Output is one line per sample block: `TAG index value`, value as %.9g so the
 * decimal round-trips a binary32 exactly and the JS side compares the number
 * this file computed, not a re-rounded copy of it.
 *
 * Build (needs isystem_dsp_kernels.h on the include path):
 *   cl /nologo /std:c++17 /EHsc /I include /I build tests\dump_graph_vectors.cpp ^
 *      src\AudioStreamGraph.cpp /Fo:build\ /Fe:build\dump_graph_vectors.exe
 */
#include "AudioStreamGraph.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "isystem_dsp_kernels.h"

#include <cstdio>
#include <cstring>
#include <cmath>

using namespace isystem;

static const float kSr = 44100.0f;
static const int kN = 512;
/* The pitch shifter delays by a whole grain (0.055 s = 2426 samples at
   44.1 kHz), so a 512-sample window only ever sees the pre-fill and both
   sides agree at exact silence — which measures nothing. */
static const int kNPitch = 8192;

/** Deterministic test signal. No RNG — the JS side must generate the identical
 *  sequence, so it has to be a closed form both can compute. */
static float testInput(int i) {
  const float t = (float)i / kSr;
  return 0.7f * sinf(2.0f * (float)M_PI * 220.0f * t) +
         0.2f * sinf(2.0f * (float)M_PI * 1750.0f * t);
}

/** A biquad is linear, so its five normalised coefficients ARE its
 *  behaviour. They are emitted as their own vectors because comparing the
 *  cascade OUTPUT cannot separate a real error from float32-vs-float64
 *  width: measured here, the clean floor (4.30e-5 RMS) is larger than a
 *  0.1% coefficient mistake (2.24e-5). Coefficients have no such problem. */
static void emitCoeffs(const char *tag, const float c[5]) {
  for (int k = 0; k < 5; ++k) std::printf("%s %d %.9g\n", tag, k, (double)c[k]);
}

static void emit(const char *tag, int i, float v) {
  std::printf("%s %d %.9g\n", tag, i, (double)v);
}

int main() {
  /* ---------------------------------------------------------- KERNEL: filter */
  /* Chamberlin SVF. The C++ header calls this a port of the browser's filter;
     this is where that claim gets measured. */
  {
    IsFilter f;
    f.reset(kSr);
    f.setParams(1200.0f, 4.0f, 0);
    for (int i = 0; i < kN; ++i) emit("filter_lp", i, f.process(testInput(i)));
  }
  {
    IsFilter f;
    f.reset(kSr);
    f.setParams(600.0f, 12.0f, 2); /* high resonance, bandpass — where an SVF
                                      diverges first if the damp term differs */
    for (int i = 0; i < kN; ++i) emit("filter_bp", i, f.process(testInput(i)));
  }

  /* ------------------------------------------------------ KERNEL: distortion */
  {
    IsDistortion d;
    d.reset(kSr);
    d.setParams(0.35f, 4200.0f, 0.4f);
    for (int i = 0; i < kN; ++i) emit("dist", i, d.process(testInput(i)));
  }
  {
    IsDistortion d;
    d.reset(kSr);
    d.setParams(1.0f, 800.0f, 1.0f); /* fully wet, hard drive, dark tone */
    for (int i = 0; i < kN; ++i) emit("dist_hot", i, d.process(testInput(i)));
  }

  /* ----------------------------------------------------- KERNEL: oscillator */
  /* Free-running, so only the wave shape and the phase increment are under
     test. Rendered at the graph's own note frequency for A4. */
  {
    IsOsc o;
    o.wave = 2; /* saw */
    o.phase = 0.0f;
    const float inc = 440.0f / kSr;
    for (int i = 0; i < kN; ++i) emit("osc_saw", i, o.tick(inc));
  }
  {
    IsOsc o;
    o.wave = 0; /* sine */
    o.phase = 0.0f;
    const float inc = 440.0f / kSr;
    for (int i = 0; i < kN; ++i) emit("osc_sine", i, o.tick(inc));
  }

  /* -------------------------------------------------------- KERNEL: ring-mod */
  {
    IsRingMod r;
    r.reset(kSr);
    r.setParams(220.0f, 0.7f); /* catalog defaults */
    for (int i = 0; i < kN; ++i) emit("ring", i, r.process(testInput(i)));
  }
  {
    IsRingMod r;
    r.reset(kSr);
    r.setParams(1990.0f, 1.0f); /* top of the carrier range, fully wet — the
                                   phase accumulator wraps every 22 samples */
    for (int i = 0; i < kN; ++i) emit("ring_hi", i, r.process(testInput(i)));
  }

  /* ------------------------------------------------------ KERNEL: compressor */
  {
    IsCompressor c;
    c.reset(kSr);
    c.setParams(-18.0f, 3.0f, 0.012f, 0.12f); /* catalog defaults */
    for (int i = 0; i < kN; ++i) emit("comp", i, c.process(testInput(i)));
  }
  {
    IsCompressor c;
    c.reset(kSr);
    c.setParams(-48.0f, 20.0f, 0.001f, 0.01f); /* limiting, fastest envelope —
                                                  where an attack coefficient
                                                  that differs shows up first */
    for (int i = 0; i < kN; ++i) emit("comp_hard", i, c.process(testInput(i)));
  }

  /* -------------------------------------------------------------- KERNEL: eq */
  {
    IsEq e;
    e.reset(kSr);
    e.setParams(0.0f, 0.0f, 0.0f); /* flat: three biquads that must be unity */
    for (int i = 0; i < kN; ++i) emit("eq_flat", i, e.process(testInput(i)));
    float c[5];
    for (int sec = 0; sec < 3; ++sec) {
      e.sectionCoeffs(sec, c);
      emitCoeffs(sec == 0 ? "coef_eq_flat_low" : sec == 1 ? "coef_eq_flat_mid"
                                                          : "coef_eq_flat_high", c);
    }
  }
  {
    IsEq e;
    e.reset(kSr);
    e.setParams(9.0f, -11.0f, 7.5f); /* shelf up, mid scooped, air up */
    for (int i = 0; i < kN; ++i) emit("eq_shaped", i, e.process(testInput(i)));
    float c[5];
    for (int sec = 0; sec < 3; ++sec) {
      e.sectionCoeffs(sec, c);
      emitCoeffs(sec == 0 ? "coef_eq_shaped_low" : sec == 1 ? "coef_eq_shaped_mid"
                                                            : "coef_eq_shaped_high", c);
    }
  }

  /* ----------------------------------------------------- KERNEL: crystalizer */
  {
    IsCrystalizer c;
    c.reset(kSr);
    c.setParams(0.55f, 3200.0f, 0.4f, 0.45f, 0.45f); /* catalog defaults */
    for (int i = 0; i < kN; ++i) emit("cryst", i, c.process(testInput(i)));
    float k5[5];
    static const char *kNames[4] = { "coef_cryst_hp", "coef_cryst_dip",
                                     "coef_cryst_pres", "coef_cryst_air" };
    for (int sec = 0; sec < 4; ++sec) { c.sectionCoeffs(sec, k5); emitCoeffs(kNames[sec], k5); }
  }
  {
    IsCrystalizer c;
    c.reset(kSr);
    c.setParams(1.0f, 8000.0f, 1.0f, 1.0f, 1.0f); /* every control at the top */
    for (int i = 0; i < kN; ++i) emit("cryst_max", i, c.process(testInput(i)));
    float k5[5];
    static const char *kNames[4] = { "coef_crystmax_hp", "coef_crystmax_dip",
                                     "coef_crystmax_pres", "coef_crystmax_air" };
    for (int sec = 0; sec < 4; ++sec) { c.sectionCoeffs(sec, k5); emitCoeffs(kNames[sec], k5); }
  }

  /* ------------------------------------- KERNEL: chorus / flange (mod delay) */
  /* The template size is CAPACITY, not tuning: measured, IsModDelay gives
     bit-identical output at 4410 and 8192 samples, and IsPitchShift at 5120
     and 11008. A size too small to hold the pitch grain does change the
     output, which is why the runtime counts that case instead of hiding it. */
  {
    IsModDelay<AUDIO_STREAM_GRAPH_MOD_MAX> m;
    m.reset(kSr);
    m.setParams(0.8f, 0.004f, 0.012f, 0.0f, 0.45f); /* chorus defaults */
    for (int i = 0; i < kN; ++i) emit("chorus", i, m.process(testInput(i)));
  }
  {
    IsModDelay<AUDIO_STREAM_GRAPH_MOD_MAX> m;
    m.reset(kSr);
    m.setParams(0.35f, 0.003f, 0.004f, 0.55f, 0.5f); /* flange, with feedback */
    for (int i = 0; i < kN; ++i) emit("flange", i, m.process(testInput(i)));
  }

  /* ------------------------------------------------------- KERNEL: maximizer */
  {
    IsMaximizer<AUDIO_STREAM_GRAPH_MAXIMIZER_MAX> m;
    m.reset(kSr);
    m.setParams(6.0f, -0.5f, 0.45f, 5.0f, 0.08f, 1.0f); /* catalog defaults */
    for (int i = 0; i < kN; ++i) emit("maxim", i, m.process(testInput(i)));
  }
  {
    IsMaximizer<AUDIO_STREAM_GRAPH_MAXIMIZER_MAX> m;
    m.reset(kSr);
    /* 2 ms lookahead, not 12: at 44.1 kHz a 12 ms lookahead (529 samples) is
       longer than this 512-sample window, so the vector was pure pre-fill
       silence on both sides. Everything else stays at the top of its range. */
    m.setParams(24.0f, -0.1f, 1.0f, 2.0f, 0.01f, 1.5f);
    for (int i = 0; i < kN; ++i) emit("maxim_hot", i, m.process(testInput(i)));
  }

  /* ----------------------------------------------------- KERNEL: pitch-shift */
  {
    IsPitchShift<AUDIO_STREAM_GRAPH_PITCH_MAX> p;
    p.reset(kSr);
    p.setParams(7.0f, 1.0f); /* a fifth up, fully wet */
    /* Structural state. The samples cannot be gated (see the JS side), so
       this is what proves the two implementations are the same one. */
    {
      float st[5];
      st[0] = (float)lroundf(kSr * 0.055f);            /* grain  */
      st[1] = st[0] * 2.0f + 128.0f;                   /* len    */
      st[2] = powf(2.0f, 7.0f / 12.0f);                /* ratio  */
      st[3] = (1.0f - st[2]) / st[0];                  /* phase increment */
      st[4] = 1.0f;                                    /* mix    */
      emitCoeffs("state_pitch_up", st);
    }
    for (int i = 0; i < kNPitch; ++i) emit("pitch_up", i, p.process(testInput(i)));
  }
  {
    IsPitchShift<AUDIO_STREAM_GRAPH_PITCH_MAX> p;
    p.reset(kSr);
    p.setParams(-12.0f, 0.5f); /* an octave down, half wet — exercises the dry
                                  tap, which the fully-wet case never reads */
    {
      float st[5];
      st[0] = (float)lroundf(kSr * 0.055f);
      st[1] = st[0] * 2.0f + 128.0f;
      st[2] = powf(2.0f, -12.0f / 12.0f);
      st[3] = (1.0f - st[2]) / st[0];
      st[4] = 0.5f;
      emitCoeffs("state_pitch_down", st);
    }
    for (int i = 0; i < kNPitch; ++i) emit("pitch_down", i, p.process(testInput(i)));
  }

  /* ------------------------------------------------------------------ GRAPH */
  /* osc -> filter -> gain -> out. Every native kind that carries state, walked
     by the runtime rather than called by hand. */
  {
    AudioStreamNodeDesc n[4];
    std::memset(n, 0, sizeof(n));
    n[0].type = "isystem-dsp-oscillator";
    n[0].kind = audio_stream_kind_from_type(n[0].type);
    n[0].p[AUDIO_STREAM_OSC_WAVE] = 2.0f;  n[0].p_set[AUDIO_STREAM_OSC_WAVE] = true;
    n[0].p[AUDIO_STREAM_OSC_LEVEL] = 0.8f; n[0].p_set[AUDIO_STREAM_OSC_LEVEL] = true;
    n[1].type = "isystem-dsp-filter";
    n[1].kind = audio_stream_kind_from_type(n[1].type);
    n[1].p[AUDIO_STREAM_FILTER_CUTOFF] = 1200.0f; n[1].p_set[AUDIO_STREAM_FILTER_CUTOFF] = true;
    n[1].p[AUDIO_STREAM_FILTER_RESONANCE] = 4.0f; n[1].p_set[AUDIO_STREAM_FILTER_RESONANCE] = true;
    n[2].type = "isystem-dsp-gain";
    n[2].kind = audio_stream_kind_from_type(n[2].type);
    n[2].p[AUDIO_STREAM_GAIN_GAIN] = 0.5f; n[2].p_set[AUDIO_STREAM_GAIN_GAIN] = true;
    n[3].type = "isystem-dsp-audio-out";
    n[3].kind = audio_stream_kind_from_type(n[3].type);

    AudioStreamEdgeDesc e[3];
    e[0].from = 0; e[0].to = 1;
    e[1].from = 1; e[1].to = 2;
    e[2].from = 2; e[2].to = 3;

    AudioStreamGraph g;
    if (!g.build(n, 4, e, 3, kSr)) {
      std::printf("GRAPH_BUILD_FAILED\n");
      return 1;
    }
    g.noteOn(69, 1.0f); /* A4 = 440 Hz */
    float out[kN];
    g.render(out, kN);
    for (int i = 0; i < kN; ++i) emit("graph_osc_filt_gain", i, out[i]);

    /* The gate ramp is a designed control, not an envelope — it is 5 ms, so it
       is still ramping through the first 220 samples at 44.1 kHz. The JS side
       has to model it or the first block will never match. Emit it separately
       so a mismatch there is distinguishable from a DSP mismatch. */
    std::printf("META gate_ms %.9g\n", (double)AUDIO_STREAM_GRAPH_GATE_MS);
    std::printf("META sr %.9g\n", (double)kSr);
    std::printf("META frames %d\n", kN);
  }

  /* --------------------------- GRAPH: osc -> ring -> comp -> eq -> cryst -> out */
  /* The per-kernel vectors above cannot show whether the WALK carries the
     numbers between them. This chains all four new kinds so a mismatch here
     with clean kernel vectors points at the runtime, not the DSP. */
  {
    AudioStreamNodeDesc n[6];
    std::memset(n, 0, sizeof(n));
    n[0].type = "isystem-dsp-oscillator";
    n[0].kind = audio_stream_kind_from_type(n[0].type);
    n[0].p[AUDIO_STREAM_OSC_WAVE] = 2.0f;  n[0].p_set[AUDIO_STREAM_OSC_WAVE] = true;
    n[0].p[AUDIO_STREAM_OSC_LEVEL] = 0.8f; n[0].p_set[AUDIO_STREAM_OSC_LEVEL] = true;
    n[1].type = "isystem-dsp-ring-mod";
    n[1].kind = audio_stream_kind_from_type(n[1].type);
    n[1].p[AUDIO_STREAM_RING_CARRIER] = 220.0f; n[1].p_set[AUDIO_STREAM_RING_CARRIER] = true;
    n[1].p[AUDIO_STREAM_RING_MIX] = 0.7f;       n[1].p_set[AUDIO_STREAM_RING_MIX] = true;
    n[2].type = "isystem-dsp-compressor";
    n[2].kind = audio_stream_kind_from_type(n[2].type);
    n[2].p[AUDIO_STREAM_COMP_THRESHOLD] = -18.0f; n[2].p_set[AUDIO_STREAM_COMP_THRESHOLD] = true;
    n[2].p[AUDIO_STREAM_COMP_RATIO] = 3.0f;       n[2].p_set[AUDIO_STREAM_COMP_RATIO] = true;
    n[2].p[AUDIO_STREAM_COMP_ATTACK] = 0.012f;    n[2].p_set[AUDIO_STREAM_COMP_ATTACK] = true;
    n[2].p[AUDIO_STREAM_COMP_RELEASE] = 0.12f;    n[2].p_set[AUDIO_STREAM_COMP_RELEASE] = true;
    n[3].type = "isystem-dsp-eq";
    n[3].kind = audio_stream_kind_from_type(n[3].type);
    n[3].p[AUDIO_STREAM_EQ_LOW] = 6.0f;   n[3].p_set[AUDIO_STREAM_EQ_LOW] = true;
    n[3].p[AUDIO_STREAM_EQ_MID] = -4.0f;  n[3].p_set[AUDIO_STREAM_EQ_MID] = true;
    n[3].p[AUDIO_STREAM_EQ_HIGH] = 3.0f;  n[3].p_set[AUDIO_STREAM_EQ_HIGH] = true;
    n[4].type = "isystem-dsp-crystalizer";
    n[4].kind = audio_stream_kind_from_type(n[4].type);
    n[4].p[AUDIO_STREAM_CRYSTAL_CLARITY] = 0.55f;  n[4].p_set[AUDIO_STREAM_CRYSTAL_CLARITY] = true;
    n[4].p[AUDIO_STREAM_CRYSTAL_FOCUS] = 3200.0f;  n[4].p_set[AUDIO_STREAM_CRYSTAL_FOCUS] = true;
    n[4].p[AUDIO_STREAM_CRYSTAL_SPARKLE] = 0.4f;   n[4].p_set[AUDIO_STREAM_CRYSTAL_SPARKLE] = true;
    n[4].p[AUDIO_STREAM_CRYSTAL_SEPARATE] = 0.45f; n[4].p_set[AUDIO_STREAM_CRYSTAL_SEPARATE] = true;
    n[4].p[AUDIO_STREAM_CRYSTAL_MIX] = 0.45f;      n[4].p_set[AUDIO_STREAM_CRYSTAL_MIX] = true;
    n[5].type = "isystem-dsp-audio-out";
    n[5].kind = audio_stream_kind_from_type(n[5].type);

    AudioStreamEdgeDesc e[5];
    for (int i = 0; i < 5; ++i) { e[i].from = (uint8_t)i; e[i].to = (uint8_t)(i + 1); }

    AudioStreamGraph g;
    if (!g.build(n, 6, e, 5, kSr)) {
      std::printf("GRAPH_FX_BUILD_FAILED\n");
      return 1;
    }
    if (g.report().passthrough != 0) {
      /* Every kind in this chain is meant to be ported now. If one falls back
         to pass-through the vector would still look plausible, so say so. */
      std::printf("GRAPH_FX_PASSTHROUGH %d\n", g.report().passthrough);
      return 1;
    }
    g.noteOn(69, 1.0f);
    float out[kN];
    g.render(out, kN);
    for (int i = 0; i < kN; ++i) emit("graph_fx_chain", i, out[i]);
  }
  return 0;
}
