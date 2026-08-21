#include "AudioStreamControl.h"

#include <string.h>

namespace {

/* Reinterpret, never convert. A cast would round; this moves the bits. */
uint32_t float_bits(float f) {
  uint32_t u;
  memcpy(&u, &f, sizeof(u));
  return u;
}

float bits_float(uint32_t u) {
  float f;
  memcpy(&f, &u, sizeof(f));
  return f;
}

}  // namespace

int audio_stream_control_encode(uint8_t *buf, int cap, uint8_t id,
                                uint8_t node, uint8_t slot, float value) {
  if (!buf || cap < AUDIO_STREAM_CONTROL_FRAME_LEN) return 0;
  /* MIDI data bytes are 7-bit. A node index or slot that does not fit is a
   * caller bug, and silently masking it would address the wrong module. */
  if (node > 0x7F || slot > 0x7F) return 0;

  const uint32_t u = float_bits(value);
  buf[0] = 0xF0;
  buf[1] = id;
  buf[2] = AUDIO_STREAM_SYSEX_SUB_PARAM;
  buf[3] = node;
  buf[4] = slot;
  buf[5] = (uint8_t)(u & 0x7F);
  buf[6] = (uint8_t)((u >> 7) & 0x7F);
  buf[7] = (uint8_t)((u >> 14) & 0x7F);
  buf[8] = (uint8_t)((u >> 21) & 0x7F);
  buf[9] = (uint8_t)((u >> 28) & 0x0F); /* only 4 bits left in a 32-bit float */
  buf[10] = 0xF7;
  return AUDIO_STREAM_CONTROL_FRAME_LEN;
}

AudioStreamControlMsg audio_stream_control_decode(const uint8_t *buf, int len) {
  AudioStreamControlMsg m;
  memset(&m, 0, sizeof(m));

  if (!buf || len != AUDIO_STREAM_CONTROL_FRAME_LEN) {
    m.status = AUDIO_STREAM_CONTROL_ERR_LENGTH;
    return m;
  }
  if (buf[0] != 0xF0 || buf[AUDIO_STREAM_CONTROL_FRAME_LEN - 1] != 0xF7) {
    m.status = AUDIO_STREAM_CONTROL_ERR_FRAMING;
    return m;
  }
  if (buf[1] != AUDIO_STREAM_SYSEX_TO_DEVICE &&
      buf[1] != AUDIO_STREAM_SYSEX_FROM_DEVICE) {
    m.status = AUDIO_STREAM_CONTROL_ERR_ID;
    return m;
  }
  if (buf[2] != AUDIO_STREAM_SYSEX_SUB_PARAM) {
    /* Almost certainly a well-formed Octopus frame on the same cable. Not an
     * error in the stream, just not ours — the caller passes it along. */
    m.status = AUDIO_STREAM_CONTROL_ERR_SUB;
    return m;
  }
  /* Every payload byte must be 7-bit, and the top septet only carries 4 bits.
   * Checking that rejects a corrupted frame instead of decoding a wild float
   * and writing it into a delay line. */
  for (int i = 3; i <= 9; ++i) {
    if (buf[i] & 0x80) {
      m.status = AUDIO_STREAM_CONTROL_ERR_DATA;
      return m;
    }
  }
  if (buf[9] > 0x0F) {
    m.status = AUDIO_STREAM_CONTROL_ERR_DATA;
    return m;
  }

  const uint32_t u = (uint32_t)buf[5] | ((uint32_t)buf[6] << 7) |
                     ((uint32_t)buf[7] << 14) | ((uint32_t)buf[8] << 21) |
                     ((uint32_t)buf[9] << 28);

  m.status = AUDIO_STREAM_CONTROL_OK;
  m.id = buf[1];
  m.node = buf[3];
  m.slot = buf[4];
  m.value = bits_float(u);
  return m;
}

const char *audio_stream_control_status_text(AudioStreamControlStatus s) {
  switch (s) {
    case AUDIO_STREAM_CONTROL_OK:          return "ok";
    case AUDIO_STREAM_CONTROL_ERR_LENGTH:  return "wrong frame length";
    case AUDIO_STREAM_CONTROL_ERR_FRAMING: return "missing F0/F7";
    case AUDIO_STREAM_CONTROL_ERR_ID:      return "unknown direction id";
    case AUDIO_STREAM_CONTROL_ERR_SUB:     return "not a stream frame";
    case AUDIO_STREAM_CONTROL_ERR_DATA:    return "malformed payload";
  }
  return "unknown";
}

/* ----------------------------------------------------------- frame reader */

bool AudioStreamSysexReader::push(uint8_t byte, const uint8_t **out, int *out_len) {
  if (byte == 0xF0) {
    /* A second F0 before F7 means the first frame was cut off. Start over on
     * the new one rather than splicing two half-frames into one bad message. */
    active_ = true;
    len_ = 0;
    buf_[len_++] = byte;
    return false;
  }
  if (!active_) return false;

  if (len_ >= kMax) {
    /* Too long to be one of ours. Drop the whole frame and wait for the next
     * F0 — truncating would hand on a frame that passes the length check by
     * accident. */
    active_ = false;
    len_ = 0;
    overruns_++;
    return false;
  }
  buf_[len_++] = byte;

  if (byte == 0xF7) {
    active_ = false;
    if (out) *out = buf_;
    if (out_len) *out_len = len_;
    return true;
  }
  return false;
}

/* ------------------------------------------------------------------ queue */

bool AudioStreamControlQueue::push(uint8_t node, uint8_t slot, float value) {
  const uint32_t head = head_.load(std::memory_order_relaxed);
  const uint32_t tail = tail_.load(std::memory_order_acquire);
  if (head - tail >= (uint32_t)AUDIO_STREAM_CONTROL_QUEUE) {
    dropped_++;
    return false;
  }
  Item &it = ring_[head & (AUDIO_STREAM_CONTROL_QUEUE - 1)];
  it.node = node;
  it.slot = slot;
  it.value = value;
  /* Release: the item above is visible to the consumer before the new head is. */
  head_.store(head + 1, std::memory_order_release);
  return true;
}

bool AudioStreamControlQueue::feed(const AudioStreamControlMsg &msg) {
  if (msg.status != AUDIO_STREAM_CONTROL_OK) return false;
  /* 0x7C is the device talking. Acting on it would let the board's own echo
   * drive it back — the loop immunity Octopus's direction tag exists for. */
  if (msg.id != AUDIO_STREAM_SYSEX_TO_DEVICE) return false;
  return push(msg.node, msg.slot, msg.value);
}

int AudioStreamControlQueue::drain(AudioStreamGraph &graph) {
  int applied = 0;
  uint32_t tail = tail_.load(std::memory_order_relaxed);
  const uint32_t head = head_.load(std::memory_order_acquire);
  while (tail != head) {
    const Item &it = ring_[tail & (AUDIO_STREAM_CONTROL_QUEUE - 1)];
    if (graph.setParam(it.node, it.slot, it.value)) applied++;
    else rejected_++;
    tail++;
  }
  tail_.store(tail, std::memory_order_release);
  return applied;
}

int AudioStreamControlQueue::pending() const {
  return (int)(head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_acquire));
}
