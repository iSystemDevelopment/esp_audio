/**
 * AudioCodec / AudioCodecES8388 — native tests against a fake I2C bus.
 *
 * The point of injecting the bus is that a codec driver can be checked without
 * a board. What is verified here is everything that decides whether a real
 * bring-up succeeds or fails visibly: that it refuses to half-start, that it
 * stops at the first failed write, that the volume mapping is the right way
 * round, and that a missing part is distinguishable from a misconfigured one.
 *
 * NOT verified — and it cannot be, here: the register VALUES. This driver ships
 * no init sequence on purpose (see AudioCodecES8388.h). Nothing below asserts
 * that any particular table brings a real ES8388 up.
 *
 * Build: see tests/README.md.
 */
#include "AudioCodec.h"
#include "AudioCodecES8388.h"

#include <cstdio>
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

/** A fake I2C bus that records traffic and can be made to fail on demand. */
struct FakeBus {
  struct Write { uint8_t addr, reg, val; };
  std::vector<Write> writes;
  int reads = 0;
  bool present = true;      /* does anything acknowledge? */
  int fail_write_at = -1;   /* index of a write that should fail */
  uint8_t regs[256];

  FakeBus() { std::memset(regs, 0, sizeof(regs)); }

  static bool w(void *ctx, uint8_t addr, uint8_t reg, uint8_t val) {
    FakeBus *b = (FakeBus *)ctx;
    if (b->fail_write_at >= 0 && (int)b->writes.size() == b->fail_write_at) return false;
    Write rec = { addr, reg, val };
    b->writes.push_back(rec);
    b->regs[reg] = val;
    return true;
  }
  static bool r(void *ctx, uint8_t addr, uint8_t reg, uint8_t *out) {
    (void)addr;
    FakeBus *b = (FakeBus *)ctx;
    b->reads++;
    if (!b->present) return false;
    *out = b->regs[reg];
    return true;
  }
  AudioCodecBus bus() {
    AudioCodecBus x;
    x.write = &FakeBus::w;
    x.read = &FakeBus::r;
    x.ctx = this;
    return x;
  }
};

int main() {
  /* ---- the no-control DAC ------------------------------------------------ */
  {
    AudioCodecNone dac;
    ok("a bare DAC starts", dac.begin(44100));
    ok("and is ready", dac.ready());
    ok("it names itself for the boot log", std::string(dac.name()).find("PCM5102") != std::string::npos,
       dac.name());
    ok("volume is honestly unsupported", !dac.setVolume(0.5f),
       "a part with no control bus must say so, not pretend");
    AudioCodecNone labelled("UDA1334");
    ok("the label can be set", std::string(labelled.name()) == "UDA1334");
    AudioCodec *base = &dac;
    ok("it works through the interface", base->begin(48000) && base->ready());
  }

  /* ---- ES8388 refuses to half-start -------------------------------------- */
  {
    AudioCodecES8388 c;
    ok("no bus, no start", !c.begin(44100));
    ok("and it says why", std::string(c.lastError()).find("bus") != std::string::npos, c.lastError());

    FakeBus fb;
    c.setBus(fb.bus());
    ok("bus but no init table, no start", !c.begin(44100),
       "a probed-but-unconfigured codec passes no audio and looks like a wiring fault");
    ok("and it points at the header", std::string(c.lastError()).find("init sequence") != std::string::npos,
       c.lastError());
    eqi("nothing was written", (int)fb.writes.size(), 0);
  }

  /* ---- a missing part is distinguishable from a misconfigured one --------- */
  {
    FakeBus fb;
    fb.present = false;
    AudioCodecES8388 c;
    c.setBus(fb.bus());
    const AudioCodecReg init[] = { { 0x00, 0x80 } };
    c.setInitSequence(init, 1);
    ok("absent device does not start", !c.begin(44100));
    ok("and the error names the address", std::string(c.lastError()).find("acknowledged") != std::string::npos,
       c.lastError());
    eqi("no registers were written to a device that is not there", (int)fb.writes.size(), 0);
    ok("not ready", !c.ready());
  }

  /* ---- a table is played in order ---------------------------------------- */
  {
    FakeBus fb;
    AudioCodecES8388 c;
    c.setBus(fb.bus());
    const AudioCodecReg init[] = { { 0x08, 0x00 }, { 0x2B, 0x80 }, { 0x00, 0x05 } };
    c.setInitSequence(init, 3);
    ok("it starts", c.begin(44100));
    ok("ready", c.ready());
    eqi("every register was written", (int)fb.writes.size(), 3);
    eqi("in order: first reg", fb.writes[0].reg, 0x08);
    eqi("in order: second reg", fb.writes[1].reg, 0x2B);
    eqi("in order: third value", fb.writes[2].val, 0x05);
    eqi("to the default address", fb.writes[0].addr, AUDIO_CODEC_ES8388_ADDR);
    ok("error is clear on success", std::string(c.lastError()).empty(), c.lastError());
    ok("it probed before writing", fb.reads > 0);
  }

  /* ---- a failed write stops the sequence --------------------------------- */
  {
    FakeBus fb;
    fb.fail_write_at = 1;  /* second write fails */
    AudioCodecES8388 c;
    c.setBus(fb.bus());
    const AudioCodecReg init[] = { { 0x01, 0x11 }, { 0x02, 0x22 }, { 0x03, 0x33 } };
    c.setInitSequence(init, 3);
    ok("begin fails", !c.begin(44100));
    ok("not ready", !c.ready());
    eqi("it stopped at the failure, not carried on", (int)fb.writes.size(), 1);
    ok("and says the init failed", std::string(c.lastError()).find("init sequence") != std::string::npos,
       c.lastError());
  }

  /* ---- a non-default address is honoured ---------------------------------- */
  {
    FakeBus fb;
    AudioCodecES8388 c;
    c.setBus(fb.bus());
    c.setAddress(0x11);  /* CE pulled high */
    const AudioCodecReg init[] = { { 0x00, 0x06 } };
    c.setInitSequence(init, 1);
    ok("starts on the alternate address", c.begin(44100));
    eqi("and used it", fb.writes[0].addr, 0x11);
  }

  /* ---- volume mapping is the right way round ------------------------------ */
  {
    /* 0x00 is 0 dB and rising values ATTENUATE, so louder must mean smaller. */
    const uint8_t full = AudioCodecES8388::volumeToReg(1.0f);
    const uint8_t half = AudioCodecES8388::volumeToReg(0.5f);
    const uint8_t off  = AudioCodecES8388::volumeToReg(0.0f);
    eqi("1.0 is 0 dB", full, 0x00);
    eqi("0.0 is the mute code", off, AUDIO_CODEC_ES8388_VOL_MUTE);
    ok("louder means a SMALLER register value", full < half && half < off,
       std::to_string(full) + " < " + std::to_string(half) + " < " + std::to_string(off));
    ok("mid stays inside the usable range", half < AUDIO_CODEC_ES8388_VOL_MUTE,
       std::to_string(half));
    eqi("above 1.0 clamps", AudioCodecES8388::volumeToReg(9.0f), 0x00);
    eqi("below 0 mutes", AudioCodecES8388::volumeToReg(-1.0f), AUDIO_CODEC_ES8388_VOL_MUTE);

    FakeBus fb;
    AudioCodecES8388 c;
    c.setBus(fb.bus());
    const AudioCodecReg init[] = { { 0x00, 0x06 } };
    c.setInitSequence(init, 1);
    ok("volume before begin is refused", !c.setVolume(0.5f));
    c.begin(44100);
    const size_t before = fb.writes.size();
    ok("setVolume works once ready", c.setVolume(0.75f));
    eqi("it wrote both channels", (int)(fb.writes.size() - before), 2);
    eqi("left register", fb.writes[before].reg, AUDIO_CODEC_ES8388_REG_DACVOL_L);
    eqi("right register", fb.writes[before + 1].reg, AUDIO_CODEC_ES8388_REG_DACVOL_R);
    ok("both got the same value", fb.writes[before].val == fb.writes[before + 1].val);
  }

  /* ---- end() leaves the part muted ---------------------------------------- */
  {
    FakeBus fb;
    AudioCodecES8388 c;
    c.setBus(fb.bus());
    const AudioCodecReg init[] = { { 0x00, 0x06 } };
    c.setInitSequence(init, 1);
    c.begin(44100);
    const size_t before = fb.writes.size();
    c.end();
    ok("not ready after end", !c.ready());
    eqi("it muted both channels on the way out", (int)(fb.writes.size() - before), 2);
    eqi("left muted", fb.writes[before].val, AUDIO_CODEC_ES8388_VOL_MUTE);
    eqi("right muted", fb.writes[before + 1].val, AUDIO_CODEC_ES8388_VOL_MUTE);
  }

  std::printf("test-codec: %d passed, %d failed\n", passed, (int)fails.size());
  for (size_t i = 0; i < fails.size(); ++i) std::printf("  FAIL  %s\n", fails[i].c_str());
  return fails.empty() ? 0 : 1;
}
