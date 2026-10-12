#pragma once
#include <stdint.h>

namespace sniffer {
constexpr unsigned MAX_PAYLOAD_BYTES = 256;
constexpr unsigned MAX_BYTES = MAX_PAYLOAD_BYTES + 2; // Address + one-byte register + payload.
enum class Boundary : uint8_t { Stop, Restart };
enum Error : uint8_t { PartialByte = 1, EmptyFrame = 2, AmbiguousSetup = 4,
                       DataAfterNack = 8, FrameOverflow = 16 };
struct Frame {
  uint8_t bytes[MAX_BYTES] = {};
  bool ack[MAX_BYTES] = {};
  uint16_t size = 0;
  bool invalid = false;
  uint8_t errors = 0;
  Boundary end = Boundary::Stop;
};

// No platform code here: decoding consumes ordered two-bit wire states.
class Decoder {
 public:
  Frame frame{};
  Frame completed{};
  unsigned starts = 0, stops = 0, bytes = 0, sclEdges = 0, sdaEdges = 0;

  constexpr void reset() {
    seeded_ = active_ = endedByNack_ = false;
    bitCount_ = value_ = 0;
    frame = Frame{};
  }

  constexpr bool feed(uint8_t state) {
    state &= 3;
    if (!seeded_) {
      previous_ = state;
      seeded_ = true;
      return false;
    }
    const uint8_t changed = previous_ ^ state;
    if (changed & 1) ++sdaEdges;
    if (changed & 2) ++sclEdges;
    const bool scl = state & 2;
    const bool oldScl = previous_ & 2;
    const bool sda = state & 1;
    previous_ = state;
    if (!changed) return false;

    if (scl && oldScl && (changed & 1)) {
      const bool emitted = active_;
      if (active_) {
        // A boundary's preparatory SCL rise tentatively sampled one bit.
        const bool boundaryBit = bitCount_ == 1 && value_ == (sda ? 0 : 1);
        if (bitCount_ != 0 && !boundaryBit) frame.errors |= PartialByte;
        if (frame.size == 0) frame.errors |= EmptyFrame;
        frame.invalid = frame.errors != 0;
        frame.end = sda ? Boundary::Stop : Boundary::Restart;
        completed = frame;
      }
      frame = Frame{};
      bitCount_ = value_ = 0;
      endedByNack_ = false;
      active_ = !sda;
      if (sda) ++stops; else ++starts;
      return emitted;
    }

    if (!oldScl && scl && active_) {
      // A simultaneous observed data/clock rise has ambiguous bit setup.
      if (changed & 1) { frame.invalid = true; frame.errors |= AmbiguousSetup; }
      if (bitCount_ < 8) {
        value_ = static_cast<uint8_t>((value_ << 1) | sda);
        ++bitCount_;
      } else {
        ++bytes;
        if (endedByNack_) { frame.invalid = true; frame.errors |= DataAfterNack; }
        const unsigned capacity = frame.size && (frame.bytes[0] & 1) ? MAX_PAYLOAD_BYTES + 1 : MAX_BYTES;
        if (frame.size < capacity) {
          frame.bytes[frame.size] = value_;
          frame.ack[frame.size] = !sda;
          ++frame.size;
        } else {
          frame.invalid = true;
          frame.errors |= FrameOverflow;
        }
        endedByNack_ = sda;
        bitCount_ = value_ = 0;
      }
    }
    return false;
  }

 private:
  uint8_t previous_ = 3, bitCount_ = 0, value_ = 0;
  bool seeded_ = false, active_ = false, endedByNack_ = false;
};

constexpr bool acceptedCommand(const Frame &f) {
  return !f.invalid && f.size >= 2 && !(f.bytes[0] & 1) && f.ack[0] && f.ack[1];
}

// The wire alone cannot distinguish this shape from coincidental ordinary data.
constexpr uint8_t blockCandidateLength(const Frame &f) {
  if (f.invalid || f.size < 3 || !(f.bytes[0] & 1) || !f.ack[0]) return 0;
  const uint8_t length = f.bytes[1];
  if (length < 1 || length > 32 || f.size != length + 2) return 0;
  for (unsigned i = 1; i + 1 < f.size; ++i) if (!f.ack[i]) return 0;
  return f.ack[f.size - 1] ? 0 : length;
}
} // namespace sniffer
