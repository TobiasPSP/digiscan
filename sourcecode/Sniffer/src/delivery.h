#pragma once
#include <stdint.h>
#include <string.h>
#include "i2c_decoder.h"

namespace sniffer {
enum class Scope : uint8_t { All, Selected };
enum class Outcome : uint8_t { Both, Success, Failure };
struct DeliveryFilter {
  Scope scope = Scope::All;
  Outcome outcome = Outcome::Both;
  uint8_t registers[128][32] = {};
  bool accepts(bool success, bool associated, uint8_t address, uint8_t reg) const {
    if (outcome == Outcome::Success && !success) return false;
    if (outcome == Outcome::Failure && success) return false;
    return scope == Scope::All || (associated && address < 128 &&
        (registers[address][reg / 8] & (1u << (reg % 8))));
  }
};

struct Assessment {
  bool success = true, associated = false, read = false;
  uint8_t address = 0, reg = 0, errors = 0;
  bool addressNack = false, dataNack = false, readAckFault = false;
};
inline bool validPointer(const Frame &f) {
  return !f.invalid && !f.errors && f.size == 2 && !(f.bytes[0] & 1) &&
      f.ack[0] && f.ack[1] && f.end == Boundary::Restart;
}
inline void enforcePayloadLimit(Frame &f) {
  if (f.size && (f.bytes[0] & 1) && f.size > MAX_PAYLOAD_BYTES + 1) {
    f.invalid = true; f.errors |= FrameOverflow;
  }
}
inline unsigned decimalDigits(unsigned value) {
  return value < 10 ? 1 : value < 100 ? 2 : value < 1000 ? 3 : value < 10000 ? 4 : 5;
}
// The readable formatter's body contract, without building a string. This keeps
// output-truncated chains out of success-only delivery and success denominators.
inline unsigned formattedBodyLength(const Frame *frames, unsigned count) {
  unsigned length = 0, pointerAddress = 0;
  bool pointer = false;
  for (unsigned s = 0; s < count; ++s) {
    const Frame &f = frames[s];
    const bool read = f.size && (f.bytes[0] & 1);
    const unsigned address = f.size ? f.bytes[0] >> 1 : 0;
    const bool paired = pointer && read && address == pointerAddress;
    if (pointer && !paired) length += 19;
    pointer = false;
    if (!paired && length) length += 3;
    if (!f.size) { length += 11; continue; }
    if (validPointer(f)) { pointer = true; pointerAddress = address; continue; }
    length += paired ? 18 : !read && f.size > 1 ? 19 : read ? 9 : 10;
    const unsigned first = read ? 1 : 2;
    if (f.size > first) length += 3 * (f.size - first);
    if (read && f.size == 2) length += 5 + decimalDigits(f.bytes[1]);
    else if (read && f.size == 3) length += 12 + decimalDigits(f.bytes[1] | (f.bytes[2] << 8)) + decimalDigits((f.bytes[1] << 8) | f.bytes[2]);
    else if (read && f.size > 3) {
      length += 6 + f.size - 2;
      for (unsigned i = 1; i < f.size; ++i) length += decimalDigits(f.bytes[i]);
    }
  }
  if (pointer) length += 19;
  return length;
}
inline Assessment assess(const Frame *frames, unsigned count, bool interrupted = false) {
  Assessment a;
  a.success = !interrupted && count != 0 && formattedBodyLength(frames, count) <= 1920;
  for (unsigned s = 0; s < count; ++s) {
    const Frame &f = frames[s];
    a.errors |= f.errors;
    if (f.invalid || f.errors || !f.size) a.success = false;
    if (!f.size) continue;
    const bool read = f.bytes[0] & 1;
    if (!f.ack[0]) a.addressNack = true;
    for (unsigned i = 1; i < f.size; ++i) {
      if (!read && !f.ack[i]) a.dataNack = true;
      if (read && ((i + 1 < f.size && !f.ack[i]) || (i + 1 == f.size && f.ack[i]))) a.readAckFault = true;
    }
  }
  a.success &= !a.addressNack && !a.dataNack && !a.readAckFault;
  // Only unambiguous one-register writes and adjacent selector/read pairs
  // provide filter keys. Corrupted selectors and other chains remain unassociated.
  if (count == 1 && frames[0].size > 1 && !(frames[0].bytes[0] & 1) &&
      !frames[0].invalid && !frames[0].errors && frames[0].ack[0] && frames[0].ack[1]) {
    a.associated = true; a.address = frames[0].bytes[0] >> 1; a.reg = frames[0].bytes[1];
  } else if (count == 2 && validPointer(frames[0]) && frames[1].size &&
      (frames[1].bytes[0] & 1) && (frames[1].bytes[0] >> 1) == (frames[0].bytes[0] >> 1)) {
    a.associated = true; a.read = true; a.address = frames[0].bytes[0] >> 1; a.reg = frames[0].bytes[1];
  }
  if (a.address <= 7 || a.address >= 0x78) a.associated = false;
  return a;
}

inline void increment(uint32_t &value) { if (value != UINT32_MAX) ++value; }
struct RegisterCounts {
  uint8_t address = 0, reg = 0, decodeFlags = 0, ackFlags = 0;
  uint32_t readOk = 0, readFail = 0, writeOk = 0, writeFail = 0;
  uint32_t currentFail = 0, maxFail = 0, recent = 0, reads = 0;
};
class DeliveryCounters {
 public:
  static constexpr unsigned CAPACITY = 512;
  RegisterCounts rows[CAPACITY] = {};
  unsigned size = 0;
  uint32_t epoch = 0, total = 0, failures = 0, unassociated = 0, unassociatedFail = 0;
  uint32_t suppressed = 0, capacityDropped = 0, chainOverflow = 0;
  uint32_t outputRejected = 0;
  uint32_t errors[5] = {};
  void clear() {
    memset(rows, 0, sizeof(rows));
    memset(errors, 0, sizeof(errors));
    size = 0; ++epoch;
    total = failures = unassociated = unassociatedFail = suppressed = capacityDropped = chainOverflow = outputRejected = 0;
  }
  void add(const Assessment &a) {
    increment(total);
    if (!a.success) increment(failures);
    for (unsigned i = 0; i < 5; ++i) if (a.errors & (1u << i)) increment(errors[i]);
    if (!a.associated) {
      increment(unassociated); if (!a.success) increment(unassociatedFail); return;
    }
    unsigned index = 0;
    while (index < size && (rows[index].address != a.address || rows[index].reg != a.reg)) ++index;
    if (index == size) {
      if (size == CAPACITY) { increment(capacityDropped); return; }
      rows[index].address = a.address; rows[index].reg = a.reg; ++size;
    }
    RegisterCounts &r = rows[index];
    r.decodeFlags |= a.errors;
    r.ackFlags |= a.addressNack | (a.dataNack << 1) | (a.readAckFault << 2);
    if (a.read) {
      increment(a.success ? r.readOk : r.readFail);
      if (a.success) r.currentFail = 0;
      else { increment(r.currentFail); if (r.currentFail > r.maxFail) r.maxFail = r.currentFail; }
      r.recent = (r.recent << 1) | !a.success;
      increment(r.reads);
    } else increment(a.success ? r.writeOk : r.writeFail);
  }
  void gap() { for (unsigned i = 0; i < size; ++i) rows[i].currentFail = 0; }
};

inline const char *errorName(unsigned bit) {
  static const char *const names[] = {"partial_byte", "empty_frame", "ambiguous_setup", "data_after_nack", "frame_overflow"};
  return names[bit];
}
} // namespace sniffer
