#pragma once
#include "i2c_decoder.h"

namespace decoder_tests {
struct Fixture {
  sniffer::Decoder decoder{};
  sniffer::Frame frames[8]{};
  unsigned count = 0;
  constexpr void state(bool scl, bool sda) {
    if (decoder.feed(static_cast<uint8_t>((scl ? 2 : 0) | sda))) {
      if (count < 8) frames[count] = decoder.completed;
      ++count;
    }
  }
  constexpr void start() { state(false, true); state(true, true); state(true, false); state(false, false); }
  constexpr void stop() { state(false, false); state(true, false); state(true, true); }
  constexpr void byte(uint8_t value, bool ack = true) {
    for (int i = 7; i >= 0; --i) {
      const bool data = (value >> i) & 1;
      state(false, data); state(true, data); state(false, data);
    }
    state(false, !ack); state(true, !ack); state(false, !ack);
  }
};

constexpr bool generatorSuite() {
  Fixture f;
  f.state(true, true);
  f.start(); f.byte(0xEA); f.byte(0x10); f.byte(0); f.byte(255); f.byte(0xAA); f.byte(0x55); f.byte(0x81); f.stop();
  f.start(); f.byte(0xEA); f.byte(0x50); f.start(); f.byte(0xEB); f.byte(0x12); f.byte(0x34); f.byte(0xAA); f.byte(0x55, false); f.stop();
  f.start(); f.byte(0xEA); f.byte(0x20); f.start(); f.byte(0xEB); f.byte(5); f.byte(0); f.byte(255); f.byte(0xAA); f.byte(0x55); f.byte(0x81, false); f.stop();
  f.start(); f.byte(0xEC, false); f.stop();
  const uint8_t sizes[] = {7, 2, 5, 2, 7, 1};
  const uint8_t expected[] = {0xEA,0x10,0,255,0xAA,0x55,0x81,0xEA,0x50,
      0xEB,0x12,0x34,0xAA,0x55,0xEA,0x20,0xEB,5,0,255,0xAA,0x55,0x81,0xEC};
  if (f.count != 6 || f.decoder.starts != 6 || f.decoder.stops != 4 || f.decoder.bytes != 24) return false;
  for (unsigned i = 0; i < 6; ++i) if (f.frames[i].invalid || f.frames[i].size != sizes[i]) return false;
  unsigned offset = 0;
  for (unsigned i = 0; i < 6; ++i) {
    for (unsigned j = 0; j < f.frames[i].size; ++j, ++offset) {
      const bool nack = offset == 13 || offset == 22 || offset == 23;
      if (f.frames[i].bytes[j] != expected[offset] || f.frames[i].ack[j] == nack) return false;
    }
  }
  return f.frames[0].bytes[1] == 0x10 && f.frames[0].bytes[6] == 0x81 &&
         sniffer::acceptedCommand(f.frames[0]) && f.frames[1].end == sniffer::Boundary::Restart &&
         f.frames[2].bytes[0] == 0xEB && !f.frames[2].ack[4] &&
         sniffer::blockCandidateLength(f.frames[4]) == 5 &&
         !f.frames[5].ack[0] && !sniffer::acceptedCommand(f.frames[5]);
}

constexpr bool rejectedCommand() {
  Fixture f; f.state(true, true); f.start(); f.byte(0xEA); f.byte(0x10, false); f.stop();
  return f.count == 1 && !f.frames[0].invalid && !sniffer::acceptedCommand(f.frames[0]);
}
constexpr bool truncatedAndRecovery() {
  Fixture f; f.state(true, true); f.start(); f.byte(0xEA);
  for (unsigned i = 0; i < 4; ++i) { f.state(false, false); f.state(true, false); }
  f.stop(); f.start(); f.byte(0xEA); f.byte(0x50); f.stop();
  return f.count == 2 && (f.frames[0].errors & sniffer::PartialByte) && sniffer::acceptedCommand(f.frames[1]);
}
constexpr bool overflowAndRecovery() {
  Fixture f; f.state(true, true); f.start(); f.byte(0xEA);
  for (unsigned i = 0; i < sniffer::MAX_BYTES + 5; ++i) f.byte(0xAA);
  f.stop(); f.start(); f.byte(0xEA); f.byte(0x20); f.stop();
  return f.count == 2 && (f.frames[0].errors & sniffer::FrameOverflow) && sniffer::acceptedCommand(f.frames[1]);
}
constexpr bool armMidFrame() {
  Fixture f; f.state(false, false); f.byte(0xEA); f.byte(0x20); f.stop();
  f.start(); f.byte(0xEA); f.byte(0x50); f.stop();
  return f.count == 1 && sniffer::acceptedCommand(f.frames[0]);
}
constexpr bool nackThenData() {
  Fixture f; f.state(true, true); f.start(); f.byte(0xEC, false); f.byte(0x20); f.stop();
  return f.count == 1 && (f.frames[0].errors & sniffer::DataAfterNack);
}
constexpr bool resetAfterLoss() {
  Fixture f; f.state(true, true); f.start(); f.byte(0xEA);
  f.decoder.reset(); f.byte(0x10); f.stop(); f.start(); f.byte(0xEA); f.byte(0x50); f.stop();
  return f.count == 1 && sniffer::acceptedCommand(f.frames[0]);
}
constexpr bool ambiguousSetup() {
  Fixture f; f.state(true, true); f.start();
  f.state(false, false); f.state(true, true); f.state(false, true);
  for (unsigned i = 0; i < 7; ++i) { f.state(true, true); f.state(false, true); }
  f.state(false, false); f.state(true, false); f.state(false, false); f.stop();
  return f.count == 1 && (f.frames[0].errors & sniffer::AmbiguousSetup);
}
constexpr bool maximumBlock() {
  Fixture f; f.state(true, true); f.start(); f.byte(0xEB); f.byte(32);
  for (unsigned i = 0; i < 32; ++i) f.byte(static_cast<uint8_t>(i), i != 31);
  f.stop();
  return f.count == 1 && !f.frames[0].invalid && sniffer::blockCandidateLength(f.frames[0]) == 32;
}
constexpr bool nonBlockRead() {
  Fixture f; f.state(true, true); f.start(); f.byte(0xEB); f.byte(5); f.byte(0x55, false); f.stop();
  return f.count == 1 && !f.frames[0].invalid && sniffer::blockCandidateLength(f.frames[0]) == 0;
}
static_assert(generatorSuite(), "Generator suite must decode exactly");
static_assert(rejectedCommand(), "NACKed command must not be discovered");
static_assert(truncatedAndRecovery(), "Truncated frame recovery");
static_assert(overflowAndRecovery(), "Bounded frame overflow recovery");
static_assert(armMidFrame(), "Mid-frame arming must await START");
static_assert(nackThenData(), "Data after NACK is invalid");
static_assert(resetAfterLoss(), "Capture loss must discard partial frame");
static_assert(ambiguousSetup(), "Ambiguous data setup must be flagged");
static_assert(maximumBlock(), "32-byte block candidate");
static_assert(nonBlockRead(), "Length mismatch must not be a block candidate");
constexpr bool longTransfer(bool read, unsigned payloadBytes) {
  Fixture f; f.state(true, true); f.start(); f.byte(read ? 0xEB : 0xEA);
  if (!read) f.byte(0x50);
  for (unsigned i = 0; i < payloadBytes; ++i) f.byte(static_cast<uint8_t>(i), !read || i + 1 < payloadBytes);
  f.stop();
  if (f.count != 1) return false;
  const auto &frame = f.frames[0];
  if (payloadBytes > sniffer::MAX_PAYLOAD_BYTES) return (frame.errors & sniffer::FrameOverflow) != 0;
  if (frame.invalid || frame.size != payloadBytes + (read ? 1 : 2)) return false;
  for (unsigned i = 0; i < payloadBytes; ++i)
    if (frame.bytes[i + (read ? 1 : 2)] != static_cast<uint8_t>(i)) return false;
  return true;
}
static_assert(longTransfer(true, 255), "255-byte read");
static_assert(longTransfer(true, 256), "256-byte read must not wrap length");
static_assert(longTransfer(false, 256), "256-byte write must not wrap length");
static_assert(longTransfer(true, 257), "Read payload overflow");
static_assert(longTransfer(false, 257), "Write payload overflow");
} // namespace decoder_tests
