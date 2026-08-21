/**
 * Emits the C++ side of the JS<->C++ control cross-check.
 *
 * Prints one line per vector: `node slot value hexbytes`. The JS encoder in
 * platform/isystem-builder/src/isystem-stream-control.js must produce the same
 * bytes for the same inputs, or the desk and the board are speaking two
 * dialects of one protocol — which is the failure this whole layer exists to
 * prevent, and the kind that only shows up on hardware.
 *
 * Build:
 *   cl /std:c++17 /EHsc /I include /I build tests\dump_control_vectors.cpp ^
 *      src\AudioStreamControl.cpp src\AudioStreamPatch.cpp src\AudioStreamGraph.cpp ^
 *      /Fe:build\dump_control_vectors.exe
 *
 * Consumed by platform/isystem-builder/smoke-stream-control.mjs.
 */
#include "AudioStreamControl.h"
#include "AudioStreamPatch.h"

#include <cstdio>

struct Vec { uint8_t node; uint8_t slot; float value; };

/* Real catalog values, plus the edges that a naive packer gets wrong. */
static const Vec kVectors[] = {
  { 0,  0, 0.0f },
  { 0,  0, -0.0f },
  { 1,  0, 1200.0f },       /* filter cutoff, catalog default */
  { 1,  1, 4.0f },          /* filter resonance */
  { 2,  0, 0.28f },         /* delay time */
  { 2,  1, 0.35f },         /* delay feedback */
  { 2,  2, 0.25f },         /* delay wet */
  { 3,  0, 0.35f },         /* distortion drive */
  { 3,  1, 4200.0f },       /* distortion tone */
  { 4,  1, 0.8f },          /* osc level */
  { 4,  4, -13.7f },        /* osc detune, negative */
  { 5,  0, 20.0f },
  { 5,  0, 19870.5f },
  { 6,  0, 1.17549435e-38f },
  { 7,  0, 3.4028235e38f },
  { 8,  0, 0.99999994f },   /* one ulp below 1.0 */
  { 127, 127, 0.5f }        /* the widest addresses 7 bits allow */
};

int main() {
  for (size_t i = 0; i < sizeof(kVectors) / sizeof(kVectors[0]); ++i) {
    const Vec &v = kVectors[i];
    uint8_t buf[AUDIO_STREAM_CONTROL_FRAME_LEN];
    const int n = audio_stream_control_encode(
        buf, sizeof(buf), AUDIO_STREAM_SYSEX_TO_DEVICE, v.node, v.slot, v.value);
    if (n != AUDIO_STREAM_CONTROL_FRAME_LEN) {
      std::printf("ENCODE_FAILED %u %u\n", v.node, v.slot);
      continue;
    }
    /* %.9g round-trips a binary32 exactly, so the JS side parses the same
     * number the C++ side encoded rather than a decimal approximation of it. */
    std::printf("%u %u %.9g ", v.node, v.slot, (double)v.value);
    for (int b = 0; b < n; ++b) std::printf("%02X", buf[b]);
    std::printf("\n");
  }

  /* The slot table, so JS cannot drift from the loader's own names. */
  /* Every kind with slots. Keep this list complete: the JS side asserts it
     saw one SLOTS line per entry in its own table, so a kind added to the
     runtime and forgotten here fails the cross-check instead of quietly
     dropping out of it. */
  const AudioStreamNodeKind kinds[] = {
    AUDIO_STREAM_NODE_OSCILLATOR, AUDIO_STREAM_NODE_FILTER,
    AUDIO_STREAM_NODE_DELAY, AUDIO_STREAM_NODE_DISTORTION,
    AUDIO_STREAM_NODE_GAIN, AUDIO_STREAM_NODE_RING_MOD,
    AUDIO_STREAM_NODE_COMPRESSOR, AUDIO_STREAM_NODE_EQ,
    AUDIO_STREAM_NODE_CRYSTALIZER, AUDIO_STREAM_NODE_CHORUS,
    AUDIO_STREAM_NODE_FLANGE, AUDIO_STREAM_NODE_MAXIMIZER,
    AUDIO_STREAM_NODE_PITCH_SHIFT
  };
  const char *type_of[] = {
    "isystem-dsp-oscillator", "isystem-dsp-filter", "isystem-dsp-delay",
    "isystem-dsp-distortion", "isystem-dsp-gain", "isystem-dsp-ring-mod",
    "isystem-dsp-compressor", "isystem-dsp-eq", "isystem-dsp-crystalizer",
    "isystem-dsp-chorus", "isystem-dsp-flange", "isystem-dsp-maximizer",
    "isystem-dsp-pitch-shift"
  };
  for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); ++k) {
    std::printf("SLOTS %s", type_of[k]);
    for (int s = 0; s < audio_stream_slot_count(kinds[k]); ++s) {
      const char *nm = audio_stream_param_name(kinds[k], s);
      std::printf(" %s", nm ? nm : "?");
    }
    std::printf("\n");
  }
  return 0;
}
