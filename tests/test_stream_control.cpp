/**
 * AudioStreamControl — native tests. No board, no MIDI hardware.
 *
 * Build (needs isystem_dsp_kernels.h on the include path — see tests/README.md):
 *
 *   cl /std:c++17 /EHsc /I include /I build tests\test_stream_control.cpp ^
 *      src\AudioStreamControl.cpp src\AudioStreamGraph.cpp ^
 *      /Fe:build\test_stream_control.exe
 *
 *   g++ -std=c++17 -I include -I build tests/test_stream_control.cpp \
 *       src/AudioStreamControl.cpp src/AudioStreamGraph.cpp \
 *       -o build/test_stream_control
 *
 * What is asserted here is that a param written over the wire arrives as the
 * SAME number the sender had, and reaches the same code path as a param that
 * came in the patch. The claims that matter are falsifiable: an exact float
 * round-trip over a 7-bit transport, a live edit that measurably changes the
 * rendered signal, and a graph that refuses rather than half-applies.
 */
#include "AudioStreamControl.h"
#include "AudioStreamPatch.h"

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

/** Round-trip one value through encode + decode and report exact equality of
 *  the BITS, not of the floats — so a NaN compares as itself and -0.0 does not
 *  pass as 0.0. */
static bool roundtrip_exact(float v) {
  uint8_t buf[AUDIO_STREAM_CONTROL_FRAME_LEN];
  if (audio_stream_control_encode(buf, sizeof(buf), AUDIO_STREAM_SYSEX_TO_DEVICE,
                                  3, 1, v) != AUDIO_STREAM_CONTROL_FRAME_LEN) {
    return false;
  }
  AudioStreamControlMsg m = audio_stream_control_decode(buf, sizeof(buf));
  if (m.status != AUDIO_STREAM_CONTROL_OK) return false;
  uint32_t a, b;
  std::memcpy(&a, &v, 4);
  std::memcpy(&b, &m.value, 4);
  return a == b;
}

/** Peak of a rendered block, after letting the graph settle. */
static float peak_of(AudioStreamGraph &g, int frames) {
  std::vector<float> buf((size_t)frames, 0.0f);
  g.render(buf.data(), frames);
  float pk = 0.0f;
  for (int i = 0; i < frames; ++i) {
    const float a = std::fabs(buf[(size_t)i]);
    if (a > pk) pk = a;
  }
  return pk;
}

int main() {
  const float sr = 44100.0f;

  /* ---------------------------------------------------------- frame shape */
  {
    uint8_t buf[AUDIO_STREAM_CONTROL_FRAME_LEN];
    const int n = audio_stream_control_encode(
        buf, sizeof(buf), AUDIO_STREAM_SYSEX_TO_DEVICE, 5, 2, 1200.0f);
    eqi("frame is 11 bytes", n, AUDIO_STREAM_CONTROL_FRAME_LEN);
    eqi("starts F0", buf[0], 0xF0);
    eqi("carries the direction tag", buf[1], AUDIO_STREAM_SYSEX_TO_DEVICE);
    eqi("sub-byte is the stream block", buf[2], AUDIO_STREAM_SYSEX_SUB_PARAM);
    eqi("node in byte 3", buf[3], 5);
    eqi("slot in byte 4", buf[4], 2);
    eqi("ends F7", buf[10], 0xF7);

    /* The whole point of the septet packing: no payload byte may look like a
     * status byte, or the frame cannot survive a MIDI link at all. */
    bool all_7bit = true;
    for (int i = 1; i <= 9; ++i) if (buf[i] & 0x80) all_7bit = false;
    ok("every payload byte is 7-bit", all_7bit);
    ok("top septet holds only 4 bits", buf[9] <= 0x0F);

    /* Sub-byte 0x10 must sit clear of Octopus's 0x00-0x06, or the two
     * protocols collide on one cable. */
    ok("sub-byte clears the Octopus block", AUDIO_STREAM_SYSEX_SUB_PARAM > 0x06);
  }

  /* ------------------------------------------------- exact value transport */
  {
    /* Catalog units, straight off the desk. These are the values a range table
     * would have had to agree on; carrying the bits means nothing has to. */
    ok("cutoff 1200 Hz exact", roundtrip_exact(1200.0f));
    ok("cutoff 20 Hz exact", roundtrip_exact(20.0f));
    ok("cutoff 19870.5 Hz exact", roundtrip_exact(19870.5f));
    ok("resonance 4 exact", roundtrip_exact(4.0f));
    ok("delay time 0.28 s exact", roundtrip_exact(0.28f));
    ok("wet 0.4 exact", roundtrip_exact(0.4f));
    ok("drive 0.35 exact", roundtrip_exact(0.35f));
    ok("detune -13.7 cents exact", roundtrip_exact(-13.7f));
    ok("zero exact", roundtrip_exact(0.0f));
    ok("negative zero stays negative zero", roundtrip_exact(-0.0f));
    ok("smallest normal exact", roundtrip_exact(1.17549435e-38f));
    ok("largest finite exact", roundtrip_exact(3.4028235e38f));
    ok("one ulp below 1.0 exact", roundtrip_exact(0.99999994f));

    /* A 14-bit normalised value could not do this. Two cutoffs 0.1 Hz apart at
     * the top of the range would collapse onto one code; here they do not. */
    ok("19999.9 and 20000.0 stay distinct",
       roundtrip_exact(19999.9f) && roundtrip_exact(20000.0f) &&
       19999.9f != 20000.0f);
  }

  /* --------------------------------------------------- malformed is refused */
  {
    uint8_t good[AUDIO_STREAM_CONTROL_FRAME_LEN];
    audio_stream_control_encode(good, sizeof(good), AUDIO_STREAM_SYSEX_TO_DEVICE,
                                1, 0, 0.5f);

    eqi("short frame refused",
        audio_stream_control_decode(good, 7).status, AUDIO_STREAM_CONTROL_ERR_LENGTH);
    eqi("null buffer refused",
        audio_stream_control_decode(0, AUDIO_STREAM_CONTROL_FRAME_LEN).status,
        AUDIO_STREAM_CONTROL_ERR_LENGTH);

    uint8_t b[AUDIO_STREAM_CONTROL_FRAME_LEN];
    std::memcpy(b, good, sizeof(b));
    b[0] = 0xF1;
    eqi("missing F0 refused", audio_stream_control_decode(b, sizeof(b)).status,
        AUDIO_STREAM_CONTROL_ERR_FRAMING);

    std::memcpy(b, good, sizeof(b));
    b[10] = 0x00;
    eqi("missing F7 refused", audio_stream_control_decode(b, sizeof(b)).status,
        AUDIO_STREAM_CONTROL_ERR_FRAMING);

    std::memcpy(b, good, sizeof(b));
    b[1] = 0x7E;
    eqi("unknown direction id refused", audio_stream_control_decode(b, sizeof(b)).status,
        AUDIO_STREAM_CONTROL_ERR_ID);

    std::memcpy(b, good, sizeof(b));
    b[2] = 0x00; /* an Octopus command frame's sub-byte */
    eqi("Octopus sub-byte is not ours", audio_stream_control_decode(b, sizeof(b)).status,
        AUDIO_STREAM_CONTROL_ERR_SUB);

    std::memcpy(b, good, sizeof(b));
    b[6] |= 0x80;
    eqi("payload with bit 7 set refused", audio_stream_control_decode(b, sizeof(b)).status,
        AUDIO_STREAM_CONTROL_ERR_DATA);

    std::memcpy(b, good, sizeof(b));
    b[9] = 0x10; /* 5 bits in the top septet — impossible for a binary32 */
    eqi("overlong top septet refused", audio_stream_control_decode(b, sizeof(b)).status,
        AUDIO_STREAM_CONTROL_ERR_DATA);

    /* A refused frame must not leave a half-decoded value behind. */
    AudioStreamControlMsg m = audio_stream_control_decode(b, sizeof(b));
    ok("refused frame yields no value", m.value == 0.0f && m.node == 0 && m.slot == 0);

    ok("encode refuses a too-small buffer",
       audio_stream_control_encode(good, 4, AUDIO_STREAM_SYSEX_TO_DEVICE, 1, 0, 1.0f) == 0);
    ok("encode refuses a node above 7 bits",
       audio_stream_control_encode(good, sizeof(good), AUDIO_STREAM_SYSEX_TO_DEVICE,
                                   200, 0, 1.0f) == 0);
    ok("status text is not empty",
       std::strlen(audio_stream_control_status_text(AUDIO_STREAM_CONTROL_ERR_SUB)) > 0);
  }

  /* ------------------------------------------------------- stream reassembly */
  {
    uint8_t frame[AUDIO_STREAM_CONTROL_FRAME_LEN];
    audio_stream_control_encode(frame, sizeof(frame), AUDIO_STREAM_SYSEX_TO_DEVICE,
                                2, 1, 0.75f);

    AudioStreamSysexReader r;
    const uint8_t *out = 0;
    int out_len = 0;
    int completed = 0;
    /* Byte at a time is the worst case a USB-MIDI or serial link can hand you. */
    for (int i = 0; i < AUDIO_STREAM_CONTROL_FRAME_LEN; ++i)
      if (r.push(frame[i], &out, &out_len)) completed++;
    eqi("one frame out of a byte-at-a-time stream", completed, 1);
    eqi("reassembled length", out_len, AUDIO_STREAM_CONTROL_FRAME_LEN);
    ok("reassembled bytes match", out && std::memcmp(out, frame, sizeof(frame)) == 0);
    AudioStreamControlMsg m = audio_stream_control_decode(out, out_len);
    ok("reassembled frame decodes", m.status == AUDIO_STREAM_CONTROL_OK && m.value == 0.75f);

    /* Note traffic on the same cable must not disturb it. */
    r.reset();
    completed = 0;
    const uint8_t noise[] = { 0x90, 0x3C, 0x64, 0x80, 0x3C, 0x00 };
    for (size_t i = 0; i < sizeof(noise); ++i) r.push(noise[i], &out, &out_len);
    for (int i = 0; i < AUDIO_STREAM_CONTROL_FRAME_LEN; ++i)
      if (r.push(frame[i], &out, &out_len)) completed++;
    eqi("channel-voice bytes between frames are ignored", completed, 1);

    /* A truncated frame followed by a good one yields exactly one message —
     * the good one — not a splice of the two. */
    r.reset();
    completed = 0;
    for (int i = 0; i < 5; ++i) r.push(frame[i], &out, &out_len);
    for (int i = 0; i < AUDIO_STREAM_CONTROL_FRAME_LEN; ++i)
      if (r.push(frame[i], &out, &out_len)) completed++;
    eqi("a cut-off frame is abandoned, not spliced", completed, 1);
    eqi("and the survivor is the whole frame", out_len, AUDIO_STREAM_CONTROL_FRAME_LEN);

    /* An oversized frame is dropped whole and counted, and the reader recovers. */
    r.reset();
    completed = 0;
    r.push(0xF0, &out, &out_len);
    for (int i = 0; i < 200; ++i) r.push(0x01, &out, &out_len);
    ok("oversized frame counted as an overrun", r.overruns() >= 1);
    for (int i = 0; i < AUDIO_STREAM_CONTROL_FRAME_LEN; ++i)
      if (r.push(frame[i], &out, &out_len)) completed++;
    eqi("reader recovers after an overrun", completed, 1);
  }

  /* --------------------------------------------------------- live setParam */
  {
    AudioStreamNodeDesc n[3] = {
      node("isystem-dsp-oscillator"),
      node("isystem-dsp-gain"),
      node("isystem-dsp-audio-out")
    };
    setp(n[0], AUDIO_STREAM_OSC_LEVEL, 0.8f);
    setp(n[1], AUDIO_STREAM_GAIN_GAIN, 1.0f);
    AudioStreamEdgeDesc e[2] = { edge(0, 1), edge(1, 2) };

    AudioStreamGraph g;
    ok("graph builds", g.build(n, 3, e, 2, sr));
    eqi("node count reported", g.nodeCount(), 3);
    g.noteOn(69, 1.0f);

    const float loud = peak_of(g, 4096);
    ok("plays before the edit", loud > 0.3f, std::to_string(loud));

    /* A gain write must be audible, and must be the ONLY thing that changed. */
    ok("gain write accepted", g.setParam(1, AUDIO_STREAM_GAIN_GAIN, 0.25f));
    const float quiet = peak_of(g, 4096);
    ok("gain 0.25 quarters the peak",
       std::fabs(quiet / loud - 0.25f) < 0.02f,
       "ratio " + std::to_string(quiet / loud));

    /* Mute is a separate slot and must win over gain, exactly as at build. */
    ok("mute write accepted", g.setParam(1, AUDIO_STREAM_GAIN_MUTE, 1.0f));
    ok("mute silences", peak_of(g, 2048) == 0.0f);
    ok("unmute restores", g.setParam(1, AUDIO_STREAM_GAIN_MUTE, 0.0f));
    ok("unmuted peak returns to the gain value",
       std::fabs(peak_of(g, 4096) / loud - 0.25f) < 0.02f);

    /* Read-back is the value that was written, bit for bit. */
    float got = 0.0f;
    ok("read-back works", g.getParam(1, AUDIO_STREAM_GAIN_GAIN, &got));
    ok("read-back is exact", got == 0.25f, std::to_string(got));

    /* Refusals: nothing is written, and the caller is told. */
    ok("node past the end refused", !g.setParam(99, 0, 1.0f));
    ok("negative node refused", !g.setParam(-1, 0, 1.0f));
    ok("slot the kind does not own refused", !g.setParam(1, 4, 1.0f));
    ok("audio-out owns no params", !g.setParam(2, 0, 1.0f));
    ok("NaN refused", !g.setParam(1, AUDIO_STREAM_GAIN_GAIN, std::nanf("")));
    ok("a refused NaN left the value alone",
       g.getParam(1, AUDIO_STREAM_GAIN_GAIN, &got) && got == 0.25f);
    ok("null out pointer refused", !g.getParam(1, 0, 0));

    /* Slot counts are what the refusals are judged against. */
    eqi("osc owns 5 slots", audio_stream_slot_count(AUDIO_STREAM_NODE_OSCILLATOR), 5);
    eqi("filter owns 2 slots", audio_stream_slot_count(AUDIO_STREAM_NODE_FILTER), 2);
    eqi("delay owns 3 slots", audio_stream_slot_count(AUDIO_STREAM_NODE_DELAY), 3);
    eqi("distortion owns 3 slots", audio_stream_slot_count(AUDIO_STREAM_NODE_DISTORTION), 3);
    eqi("gain owns 2 slots", audio_stream_slot_count(AUDIO_STREAM_NODE_GAIN), 2);
    eqi("pass-through owns none", audio_stream_slot_count(AUDIO_STREAM_NODE_PASSTHROUGH), 0);
    eqi("nodeKind out of range reads as pass-through",
        g.nodeKind(99), AUDIO_STREAM_NODE_PASSTHROUGH);
  }

  /* ------------------------------- a live edit must not restart the sound */
  {
    /* The reason apply_params() does not touch phase. Writing a param that has
     * nothing to do with pitch, mid-note, must not re-zero the oscillator —
     * that would click on every knob move. */
    AudioStreamNodeDesc n[3] = {
      node("isystem-dsp-oscillator"),
      node("isystem-dsp-gain"),
      node("isystem-dsp-audio-out")
    };
    setp(n[0], AUDIO_STREAM_OSC_WAVE, 0.0f); /* sine, so phase is readable */
    setp(n[1], AUDIO_STREAM_GAIN_GAIN, 1.0f);
    AudioStreamEdgeDesc e[2] = { edge(0, 1), edge(1, 2) };

    /* The write has to land on the OSCILLATOR for this to mean anything: it is
     * that node's apply_params() which could re-zero the phase. An earlier cut
     * of this test wrote to the gain node instead, so the phase-reset mutation
     * left it green — a vacuous assertion, not a passing one. */
    AudioStreamGraph a, b;
    a.build(n, 3, e, 2, sr);
    b.build(n, 3, e, 2, sr);
    a.noteOn(69, 1.0f);
    b.noteOn(69, 1.0f);

    float ba[256], bb[256];
    a.render(ba, 256);
    b.render(bb, 256);

    /* Same param, same value, on the oscillator itself. Re-applying must be a
     * no-op on the waveform: b must stay sample-identical to the untouched a. */
    b.setParam(0, AUDIO_STREAM_OSC_LEVEL, 0.8f);
    a.render(ba, 256);
    b.render(bb, 256);
    bool identical = true;
    for (int i = 0; i < 256; ++i) if (ba[i] != bb[i]) identical = false;
    ok("re-applying an oscillator param does not disturb the waveform", identical);

    /* Independently of the reference: a 440 Hz sine at 44.1 kHz steps at most
     * 2*pi*440/44100 = 0.0627 per sample. A phase restart lands the next sample
     * near zero from wherever it was, which for a block boundary mid-cycle is a
     * jump far larger than that. Measure the jump rather than trusting the
     * comparison above to be looking at the right thing. */
    const float max_step = 2.0f * 3.14159265f * 440.0f / sr * 1.05f;
    b.setParam(0, AUDIO_STREAM_OSC_LEVEL, 0.8f);
    float after[8];
    b.render(after, 8);
    const float jump = std::fabs(after[0] - bb[255]);
    ok("no discontinuity across a live oscillator write",
       jump <= max_step, "jump " + std::to_string(jump) +
                             " max " + std::to_string(max_step));

    /* And the write still does something when the value actually changes. */
    b.setParam(0, AUDIO_STREAM_OSC_LEVEL, 0.4f);
    float lo[512];
    b.render(lo, 512);
    float pk_lo = 0.0f, pk_hi = 0.0f;
    for (int i = 0; i < 512; ++i) if (std::fabs(lo[i]) > pk_lo) pk_lo = std::fabs(lo[i]);
    a.render(ba, 512);
    for (int i = 0; i < 256; ++i) if (std::fabs(ba[i]) > pk_hi) pk_hi = std::fabs(ba[i]);
    ok("halving the oscillator level halves the peak",
       std::fabs(pk_lo / pk_hi - 0.5f) < 0.02f,
       "ratio " + std::to_string(pk_lo / pk_hi));
  }

  /* -------------------------------------------------------------- the queue */
  {
    AudioStreamNodeDesc n[3] = {
      node("isystem-dsp-oscillator"),
      node("isystem-dsp-gain"),
      node("isystem-dsp-audio-out")
    };
    setp(n[1], AUDIO_STREAM_GAIN_GAIN, 1.0f);
    AudioStreamEdgeDesc e[2] = { edge(0, 1), edge(1, 2) };
    AudioStreamGraph g;
    g.build(n, 3, e, 2, sr);

    AudioStreamControlQueue q;
    eqi("starts empty", q.pending(), 0);
    eqi("drains nothing when empty", q.drain(g), 0);

    ok("push accepted", q.push(1, AUDIO_STREAM_GAIN_GAIN, 0.5f));
    eqi("one pending", q.pending(), 1);
    /* Queued is not applied — the graph must be untouched until drain(). */
    float got = 0.0f;
    ok("queued write has not landed yet",
       g.getParam(1, AUDIO_STREAM_GAIN_GAIN, &got) && got == 1.0f);
    eqi("drain applies it", q.drain(g), 1);
    ok("and now it has landed",
       g.getParam(1, AUDIO_STREAM_GAIN_GAIN, &got) && got == 0.5f);
    eqi("queue empty after drain", q.pending(), 0);

    /* Order is preserved: the last write of a drag is the one that sticks. */
    for (int i = 1; i <= 10; ++i) q.push(1, AUDIO_STREAM_GAIN_GAIN, (float)i / 10.0f);
    eqi("ten queued", q.drain(g), 10);
    ok("the last write wins",
       g.getParam(1, AUDIO_STREAM_GAIN_GAIN, &got) && got == 1.0f, std::to_string(got));

    /* Overflow is refused and counted, never absorbed silently. */
    AudioStreamControlQueue q2;
    int accepted = 0;
    for (int i = 0; i < AUDIO_STREAM_CONTROL_QUEUE + 8; ++i)
      if (q2.push(1, AUDIO_STREAM_GAIN_GAIN, 0.1f)) accepted++;
    eqi("accepts exactly the capacity", accepted, AUDIO_STREAM_CONTROL_QUEUE);
    eqi("the rest are counted as dropped", (int)q2.dropped(), 8);
    eqi("drain empties a full queue", q2.drain(g), AUDIO_STREAM_CONTROL_QUEUE);
    ok("and it accepts again afterwards", q2.push(1, AUDIO_STREAM_GAIN_GAIN, 0.2f));

    /* Writes the graph refuses are separated from writes that landed. */
    AudioStreamControlQueue q3;
    q3.push(1, AUDIO_STREAM_GAIN_GAIN, 0.3f);
    q3.push(99, 0, 0.3f);
    q3.push(2, 0, 0.3f); /* audio-out owns no params */
    eqi("only the valid write is applied", q3.drain(g), 1);
    eqi("the other two are counted as rejected", (int)q3.rejected(), 2);

    /* The wire and the queue join up, and direction is honoured. */
    AudioStreamControlQueue q4;
    uint8_t f[AUDIO_STREAM_CONTROL_FRAME_LEN];
    audio_stream_control_encode(f, sizeof(f), AUDIO_STREAM_SYSEX_TO_DEVICE,
                                1, AUDIO_STREAM_GAIN_GAIN, 0.125f);
    ok("app->device frame is queued",
       q4.feed(audio_stream_control_decode(f, sizeof(f))));
    audio_stream_control_encode(f, sizeof(f), AUDIO_STREAM_SYSEX_FROM_DEVICE,
                                1, AUDIO_STREAM_GAIN_GAIN, 0.9f);
    ok("the device's own echo is not acted on",
       !q4.feed(audio_stream_control_decode(f, sizeof(f))));
    uint8_t bad[AUDIO_STREAM_CONTROL_FRAME_LEN];
    std::memcpy(bad, f, sizeof(bad));
    bad[2] = 0x00;
    ok("a non-stream frame is not queued",
       !q4.feed(audio_stream_control_decode(bad, sizeof(bad))));
    eqi("exactly one message survived", q4.pending(), 1);
    q4.drain(g);
    ok("and it carried the right value",
       g.getParam(1, AUDIO_STREAM_GAIN_GAIN, &got) && got == 0.125f);
  }

  /* ------------------------------- name <-> slot, the mapping JS must match */
  {
    eqi("cutoff resolves",
        audio_stream_param_slot(AUDIO_STREAM_NODE_FILTER, "cutoff"),
        AUDIO_STREAM_FILTER_CUTOFF);
    eqi("resonance resolves",
        audio_stream_param_slot(AUDIO_STREAM_NODE_FILTER, "resonance"),
        AUDIO_STREAM_FILTER_RESONANCE);
    eqi("detune resolves",
        audio_stream_param_slot(AUDIO_STREAM_NODE_OSCILLATOR, "detune"),
        AUDIO_STREAM_OSC_DETUNE);
    eqi("mute resolves",
        audio_stream_param_slot(AUDIO_STREAM_NODE_GAIN, "mute"),
        AUDIO_STREAM_GAIN_MUTE);
    eqi("a param the kind does not have is -1",
        audio_stream_param_slot(AUDIO_STREAM_NODE_FILTER, "wet"), -1);
    eqi("a pass-through resolves nothing",
        audio_stream_param_slot(AUDIO_STREAM_NODE_PASSTHROUGH, "cutoff"), -1);
    ok("null name is safe",
       audio_stream_param_slot(AUDIO_STREAM_NODE_FILTER, 0) == -1);
    ok("inverse agrees",
       std::strcmp(audio_stream_param_name(AUDIO_STREAM_NODE_FILTER,
                                           AUDIO_STREAM_FILTER_CUTOFF), "cutoff") == 0);
    ok("inverse of an unowned slot is null",
       audio_stream_param_name(AUDIO_STREAM_NODE_FILTER, 5) == 0);

    /* Every slot a kind claims must have a name, or the wire can address a
     * param nothing in the desk can name. */
    const AudioStreamNodeKind kinds[] = {
      AUDIO_STREAM_NODE_OSCILLATOR, AUDIO_STREAM_NODE_FILTER,
      AUDIO_STREAM_NODE_DELAY, AUDIO_STREAM_NODE_DISTORTION,
      AUDIO_STREAM_NODE_GAIN
    };
    bool complete = true;
    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); ++k) {
      for (int s = 0; s < audio_stream_slot_count(kinds[k]); ++s) {
        const char *nm = audio_stream_param_name(kinds[k], s);
        if (!nm || audio_stream_param_slot(kinds[k], nm) != s) complete = false;
      }
    }
    ok("every slot round-trips through its name", complete);
  }

  std::printf("test-stream-control: %d passed, %d failed\n", passed, (int)fails.size());
  for (size_t i = 0; i < fails.size(); ++i) std::printf("  FAIL  %s\n", fails[i].c_str());
  return fails.empty() ? 0 : 1;
}
