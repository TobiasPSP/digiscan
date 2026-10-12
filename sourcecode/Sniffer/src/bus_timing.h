#pragma once
#include <stdint.h>
#include <algorithm>
#include "i2c_decoder.h"

namespace timing {
constexpr unsigned WINDOW = 128;
constexpr uint32_t SAMPLE_CYCLES = 8, CHANGE_CYCLES = 15, SEED_CYCLES = 11, HEARTBEAT_CYCLES = 18;
enum Field { PioHz, ResolutionNs, BlindNs, ClockHz, Samples, LastSampleMs,
  HighMinNs, HighMedianNs, HighMaxNs, LowMinNs, LowMedianNs, LowMaxUs,
  PeriodMinNs, PeriodMedianNs, PeriodMaxUs, ShortHigh, ShortLow, LongLow,
  InvalidFrames, Heartbeats, Gaps, Count };
struct Snapshot { uint32_t value[Count] = {}; };

struct Window {
  uint64_t values[WINDOW] = {};
  unsigned count = 0, position = 0;
  void clear() { count = position = 0; }
  void add(uint64_t value) { values[position++ % WINDOW] = value; if (count < WINDOW) ++count; }
  void summarize(uint64_t &minimum, uint64_t &median, uint64_t &maximum) const {
    minimum = median = maximum = 0;
    if (!count) return;
    uint64_t sorted[WINDOW];
    std::copy(values, values + count, sorted);
    std::sort(sorted, sorted + count);
    minimum = sorted[0]; maximum = sorted[count - 1];
    median = count & 1 ? sorted[count / 2] : sorted[count / 2 - 1] + (sorted[count / 2] - sorted[count / 2 - 1]) / 2;
  }
};

class Analyzer {
 public:
  uint32_t pio_Hz = 0;
  Window high, low, periods;
  uint32_t shortHigh = 0, shortLow = 0, longLow = 0, invalidFrames = 0, heartbeats = 0, gaps = 0;
  uint32_t lastSample_ms = 0;
  uint64_t lowBaseline = 0;
  // Old array contents are unreachable after their counts reset. Avoid a
  // multi-kilobyte temporary on the MCU stack when clearing statistics.
  void clear() {
    high.clear(); low.clear(); periods.clear();
    shortHigh = shortLow = longLow = invalidFrames = heartbeats = gaps = 0;
    lastSample_ms = 0; lowBaseline = 0;
    previous = 3; lastClock = lastRise = 0;
    resetTracking();
  }
  void resetTracking() { seeded = active = haveClock = haveRise = false; staged = stagePosition = rises = 0; now = 0; }
  void gap() { if (gaps != UINT32_MAX) ++gaps; resetTracking(); }
  void observe(uint8_t state, uint64_t delta_cycles, const sniffer::Frame *completed, uint32_t now_ms) {
    if (!seeded) { previous = state; seeded = true; return; }
    now += delta_cycles;
    const uint8_t changed = previous ^ state;
    const bool oldScl = previous & 2, scl = state & 2;
    if (!changed) { if (heartbeats != UINT32_MAX) ++heartbeats; return; }
    if (oldScl && scl && (changed & 1)) {
      if (active && completed) {
        if (completed->invalid) { if (invalidFrames != UINT32_MAX) ++invalidFrames; }
        else {
          bool added = false;
          for (unsigned i = 0; i < staged; ++i) {
            const auto &sample = candidates[i];
            // The SCL rise used to prepare STOP/repeated START is not a bit clock.
            if (sample.index <= completed->size * 9u) { periods.add(sample.cycles); added = true; }
          }
          if (added) lastSample_ms = now_ms;
        }
      }
      active = !(state & 1); staged = stagePosition = rises = 0; haveClock = haveRise = false;
    } else if ((changed & 2) && active) {
      if (haveClock) {
        const uint64_t duration = now - lastClock;
        if (oldScl) { high.add(duration); if (pio_Hz && duration <= (pio_Hz - 1) / 4000000u && shortHigh != UINT32_MAX) ++shortHigh; }
        else {
          low.add(duration);
          if (pio_Hz && duration <= (pio_Hz - 1) / 4000000u && shortLow != UINT32_MAX) ++shortLow;
          if (lowBaseline && duration > lowBaseline * 5 && longLow != UINT32_MAX) ++longLow;
        }
      }
      lastClock = now; haveClock = true;
      if (scl) {
        ++rises;
        if (haveRise) {
          candidates[stagePosition++ % WINDOW] = Candidate{now - lastRise, rises};
          if (staged < WINDOW) ++staged;
        }
        lastRise = now; haveRise = true;
      }
    }
    previous = state;
  }
  Snapshot snapshot() {
    Snapshot s;
    s.value[PioHz] = pio_Hz;
    s.value[ResolutionNs] = ns(SAMPLE_CYCLES);
    s.value[BlindNs] = ns(HEARTBEAT_CYCLES);
    uint64_t minimum, median, maximum;
    periods.summarize(minimum, median, maximum);
    s.value[PeriodMinNs] = ns(minimum); s.value[PeriodMedianNs] = ns(median); s.value[PeriodMaxUs] = us(maximum);
    s.value[Samples] = periods.count;
    if (periods.count >= 32 && median) s.value[ClockHz] = static_cast<uint32_t>(pio_Hz / median);
    s.value[LastSampleMs] = lastSample_ms;
    high.summarize(minimum, median, maximum);
    s.value[HighMinNs] = ns(minimum); s.value[HighMedianNs] = ns(median); s.value[HighMaxNs] = ns(maximum);
    low.summarize(minimum, median, maximum);
    if (low.count >= 16) lowBaseline = median;
    s.value[LowMinNs] = ns(minimum); s.value[LowMedianNs] = ns(median); s.value[LowMaxUs] = us(maximum);
    s.value[ShortHigh] = shortHigh; s.value[ShortLow] = shortLow; s.value[LongLow] = longLow;
    s.value[InvalidFrames] = invalidFrames; s.value[Heartbeats] = heartbeats; s.value[Gaps] = gaps;
    return s;
  }
 private:
  struct Candidate { uint64_t cycles; unsigned index; } candidates[WINDOW];
  unsigned staged = 0, stagePosition = 0, rises = 0;
  uint8_t previous = 3;
  bool seeded = false, active = false, haveClock = false, haveRise = false;
  uint64_t now = 0, lastClock = 0, lastRise = 0;
  uint32_t scaled(uint64_t cycles, uint64_t factor) const {
    if (!pio_Hz) return 0;
    const uint64_t whole = cycles / pio_Hz;
    if (whole > UINT32_MAX / factor) return UINT32_MAX;
    const uint64_t result = whole * factor + (cycles % pio_Hz) * factor / pio_Hz;
    return result > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(result);
  }
  uint32_t ns(uint64_t cycles) const { return scaled(cycles, 1000000000ull); }
  uint32_t us(uint64_t cycles) const { return scaled(cycles, 1000000ull); }
};
} // namespace timing
