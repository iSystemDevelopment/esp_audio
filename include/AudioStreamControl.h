#pragma once
#ifndef ESPAUDIO_AUDIO_STREAM_CONTROL_H
#define ESPAUDIO_AUDIO_STREAM_CONTROL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "AudioStreamGraph.h"

/** Live control of a running patch — one wire format for every surface.
 *
 *  A patch reaches the board as DATA (AudioStreamPatch). This is the other
 *  half: turning a knob afterwards, from wherever the knob happens to be. The
 *  browser desk, a touch panel wired to the same board, and an external
 *  sequencer all emit the SAME bytes and land in the same place. That is what
 *  makes the surfaces interchangeable — one wire format, not one shared struct.
 *
 *  ── Frame ───────────────────────────────────────────────────────────────────
 *
 *      F0  ID  0x10  node  slot  b0 b1 b2 b3 b4  F7        (11 bytes)
 *
 *      ID    0x7D app/panel -> device, 0x7C device -> app.
 *            Octopus's direction tag, unchanged: the firmware ignores its own
 *            0x7C, so a looped-back stream cannot re-drive the device.
 *      0x10  AUDIO_STREAM_SYSEX_SUB_PARAM.
 *      node  module index in the patch as loaded — the order the modules appear
 *            in the document, which is how the loader, the desk and the panel
 *            all count.
 *      slot  the kind's param slot (AUDIO_STREAM_FILTER_CUTOFF and friends).
 *      b0-b4 the value.
 *
 *  ── Why this extends sysex.h rather than forking it ─────────────────────────
 *  Octopus PRO XL already speaks SysEx to OctopusApp with a 201-command table,
 *  in 7-byte frames { F0, ID, sub, cmd, v14hi, v14lo, F7 }. That table is a
 *  FIXED product param list — CMD_H_VOL, CMD_TB_DRV — and it works precisely
 *  because both ends know what every command means at compile time.
 *
 *  A CraftAudio patch has no fixed param list: the modules are whatever you
 *  wired this morning. So this is a new SUB-BYTE in the same protocol, not new
 *  commands in that table. Sub-bytes 0x00-0x06 are Octopus's; the stream block
 *  starts at 0x10 so both protocols can grow without colliding. That gap is a
 *  designed margin, not a measurement.
 *
 *  ── Why the value is float bits, not a 14-bit int ───────────────────────────
 *  Octopus can send 0-16383 because both ends know each command's range. Here
 *  they cannot: cutoff is Hz, wet is 0..1, time is seconds, and the set changes
 *  with the patch. Sending a normalised integer would need a per-param range
 *  table on the board that must match the desk's PARAM_DEFS forever — a second
 *  source of truth, and exactly the kind of silent divergence this project
 *  keeps deleting.
 *
 *  So the value travels as its IEEE-754 binary32 bits, seven at a time, little
 *  end first: b0 = bits 0-6, b1 = 7-13, b2 = 14-20, b3 = 21-27, b4 = bits 28-31
 *  (so b4 is never above 0x0F, and a frame where it is gets rejected). Nothing
 *  anywhere needs to know a param's range, nothing can round it differently,
 *  and the board applies the number the desk was showing. It costs 4 bytes over
 *  an Octopus frame.
 *
 *  ── Threading ───────────────────────────────────────────────────────────────
 *  AudioStreamGraph::setParam() is not safe against a concurrent render().
 *  Push writes into AudioStreamControlQueue from the UI or MIDI task and call
 *  drain() at the top of each audio block. Single producer, single consumer.
 */

/** Direction tags, from Octopus sysex.h. Unchanged on purpose. */
#define AUDIO_STREAM_SYSEX_TO_DEVICE   0x7D
#define AUDIO_STREAM_SYSEX_FROM_DEVICE 0x7C

/** Sub-byte block owned by the stream protocol. 0x00-0x06 belong to Octopus. */
#define AUDIO_STREAM_SYSEX_SUB_PARAM 0x10

/** Bytes in a param frame, F0 and F7 included. */
#define AUDIO_STREAM_CONTROL_FRAME_LEN 11

/** Pending writes held between the producer and the audio thread. A touch UI
 *  makes at most a few dozen events a second and drain() runs every block, so
 *  this is deep headroom; overflow means something upstream is wrong, and it is
 *  counted rather than absorbed. Must stay a power of two. */
#ifndef AUDIO_STREAM_CONTROL_QUEUE
#define AUDIO_STREAM_CONTROL_QUEUE 64
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  AUDIO_STREAM_CONTROL_OK = 0,
  AUDIO_STREAM_CONTROL_ERR_LENGTH,   /**< not AUDIO_STREAM_CONTROL_FRAME_LEN bytes */
  AUDIO_STREAM_CONTROL_ERR_FRAMING,  /**< missing F0 / F7 */
  AUDIO_STREAM_CONTROL_ERR_ID,       /**< direction tag is neither 0x7C nor 0x7D */
  AUDIO_STREAM_CONTROL_ERR_SUB,      /**< sub-byte is not ours — likely an Octopus frame */
  AUDIO_STREAM_CONTROL_ERR_DATA      /**< a data byte had bit 7 set, or b4 > 0x0F */
} AudioStreamControlStatus;

typedef struct {
  AudioStreamControlStatus status;
  uint8_t id;    /**< direction tag as received */
  uint8_t node;
  uint8_t slot;
  float value;
} AudioStreamControlMsg;

/** Build a param frame into `buf`. Returns bytes written, or 0 if `cap` is too
 *  small or node/slot do not fit in 7 bits. */
int audio_stream_control_encode(uint8_t *buf, int cap, uint8_t id,
                                uint8_t node, uint8_t slot, float value);

/** Parse one complete frame. Never partially applies: a bad frame yields a
 *  status and nothing else. */
AudioStreamControlMsg audio_stream_control_decode(const uint8_t *buf, int len);

/** Human-readable status, for the boot log and the board console. */
const char *audio_stream_control_status_text(AudioStreamControlStatus s);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <atomic>

/** Reassembles SysEx frames out of an arbitrarily chopped byte stream.
 *
 *  USB-MIDI delivers SysEx in 4-byte packets, and a frame straddles them; a
 *  serial link chops wherever it likes. Bytes go in, complete frames come out.
 *  A frame longer than the cap is dropped whole and counted — never truncated
 *  and handed on as if it were valid, and never left to wedge the reader.
 */
class AudioStreamSysexReader {
 public:
  AudioStreamSysexReader() : len_(0), active_(false), overruns_(0) {}

  /** Feed one byte. Returns true when `buf` now holds a complete frame;
   *  `out_len` is its length. Bytes outside F0..F7 are ignored, so running
   *  MIDI notes on the same cable do not confuse it. */
  bool push(uint8_t byte, const uint8_t **out, int *out_len);

  /** Frames dropped for exceeding the buffer. */
  uint32_t overruns() const { return overruns_; }

  void reset() { len_ = 0; active_ = false; }

 private:
  /** A param frame is 11 bytes; the margin is for the patch-transfer frames
   *  that will share this sub-byte block later. */
  static const int kMax = 64;
  uint8_t buf_[kMax];
  int len_;
  bool active_;
  uint32_t overruns_;
};

/** Single-producer / single-consumer queue of pending param writes.
 *
 *  push() from the task that owns the surface (MIDI parse, LVGL callback, web
 *  socket handler); drain() from the audio task, between render() calls. No
 *  locks: the producer only advances the head, the consumer only the tail.
 */
class AudioStreamControlQueue {
 public:
  AudioStreamControlQueue() : head_(0), tail_(0), dropped_(0), rejected_(0) {}

  /** Queue one write. False when full — the write is NOT stored, and dropped()
   *  goes up. Dropping the newest is deliberate: a producer that re-sends its
   *  current value next frame heals itself, whereas overwriting the oldest
   *  silently loses whichever write happened to be least recent, which for a
   *  mute is not a rounding error. */
  bool push(uint8_t node, uint8_t slot, float value);

  /** Feed a decoded message. Frames that are not ours, or are addressed
   *  device->app, are ignored and reported as not-queued. */
  bool feed(const AudioStreamControlMsg &msg);

  /** Apply everything queued. Returns how many actually landed; writes the
   *  graph refused (bad node, bad slot, NaN) are counted in rejected(). */
  int drain(AudioStreamGraph &graph);

  uint32_t dropped() const { return dropped_; }
  uint32_t rejected() const { return rejected_; }
  int pending() const;
  /** Discard everything queued. Consumer side only — call it from the audio
   *  thread, next to drain(), not from the surface that is pushing. */
  void clear() {
    tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
  }

 private:
  struct Item { uint8_t node; uint8_t slot; float value; };
  Item ring_[AUDIO_STREAM_CONTROL_QUEUE];
  /* std::atomic, not volatile. `volatile` orders nothing: on a dual-core part
   * the consumer can see an advanced head before it sees the item that head
   * refers to. The release/acquire pair below is what actually publishes the
   * slot's contents along with the index. */
  std::atomic<uint32_t> head_;
  std::atomic<uint32_t> tail_;
  uint32_t dropped_;
  uint32_t rejected_;
};

#endif /* __cplusplus */
#endif /* ESPAUDIO_AUDIO_STREAM_CONTROL_H */
