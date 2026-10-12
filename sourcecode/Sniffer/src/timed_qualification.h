#pragma once
#include <stdint.h>

namespace qualification {
enum Correction : uint8_t { Clock = 1, Boundary = 2 };
struct Sample {
  uint64_t cycles = 0;
  uint8_t state = 0, corrections = 0;
  bool heartbeat = false;
};
// Fixed memory, no allocation. Timing is in PIO cycles, never wall-clock ticks.
class Filter {
 public:
  static constexpr unsigned CAPACITY = 8;
  uint32_t clockSuppressed = 0, boundarySuppressed = 0, overflows = 0;
  uint32_t threshold_cycles = 0;
  bool enabled = true, includeBoundary = true;
  void configure(uint32_t pio_Hz, bool enable, uint32_t threshold_ns = 150) {
    enabled = enable;
    threshold_cycles = uint32_t((uint64_t(pio_Hz) * threshold_ns + 999999999) / 1000000000);
    reset();
  }
  void reset() { count_ = 0; seeded_ = clockCandidate_ = dataCandidate_ = false; }
  void clearCounters() { clockSuppressed = boundarySuppressed = overflows = 0; }
  template <typename Sink> bool feed(uint8_t state, uint64_t cycles, Sink sink, bool heartbeat = false) {
    if (!enabled) { sink(Sample{cycles, state, 0, heartbeat}); return true; }
    advance(cycles, sink); // Mature older samples before adding a new candidate.
    if (count_ == CAPACITY) { increment(overflows); reset(); return false; }
    pending_[count_++] = Sample{cycles, state, 0, heartbeat};
    if (seeded_) {
      const uint8_t changed = state ^ previous_;
      if (changed & 2) {
        dataCandidate_ = false;
        if (state & 2) { clockCandidate_ = true; clockTime_ = cycles; }
        else {
          if (clockCandidate_ && cycles - clockTime_ < threshold_cycles) {
            cancel(clockTime_, 2, Clock); increment(clockSuppressed);
          }
          clockCandidate_ = false;
        }
      }
      if (includeBoundary && (changed & 1) && (state & previous_ & 2)) {
        if (state & 1) { dataCandidate_ = true; dataTime_ = cycles; }
        else {
          if (dataCandidate_ && cycles - dataTime_ < threshold_cycles) {
            cancel(dataTime_, 1, Boundary); increment(boundarySuppressed);
          }
          dataCandidate_ = false;
        }
      }
    }
    previous_ = state; seeded_ = true;
    return true;
  }
  template <typename Sink> void advance(uint64_t cycles, Sink sink) {
    while (count_ && cycles >= pending_[0].cycles && cycles - pending_[0].cycles >= threshold_cycles) emit(sink);
    if (clockCandidate_ && cycles >= clockTime_ && cycles - clockTime_ >= threshold_cycles) clockCandidate_ = false;
    if (dataCandidate_ && cycles >= dataTime_ && cycles - dataTime_ >= threshold_cycles) dataCandidate_ = false;
  }
  template <typename Sink> void flush(Sink sink) { while (count_) emit(sink); }
  bool pending() const { return count_ != 0; }
 private:
  Sample pending_[CAPACITY];
  unsigned count_ = 0;
  uint8_t previous_ = 0;
  bool seeded_ = false, clockCandidate_ = false, dataCandidate_ = false;
  uint64_t clockTime_ = 0, dataTime_ = 0;
  static void increment(uint32_t &n) { if (n != UINT32_MAX) ++n; }
  void cancel(uint64_t from, uint8_t bit, uint8_t reason) {
    for (unsigned i = 0; i < count_; ++i) if (pending_[i].cycles >= from && (pending_[i].state & bit)) {
      pending_[i].state &= ~bit; pending_[i].corrections |= reason;
    }
  }
  template <typename Sink> void emit(Sink sink) {
    const Sample sample = pending_[0];
    for (unsigned i = 1; i < count_; ++i) pending_[i - 1] = pending_[i];
    --count_; sink(sample);
  }
};
} // namespace qualification
