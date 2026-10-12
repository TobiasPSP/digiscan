#pragma once
#include <atomic>
#include "i2c_decoder.h"
#include "bus_timing.h"
#include "capture_export.h"
#include "timed_qualification.h"

namespace capture {
constexpr unsigned EDGE_HISTORY = 96;
inline bool needsFaultTrace(const sniffer::Frame &frame) {
  if (frame.invalid) return true;
  if (!frame.size) return false;
  if (!frame.ack[0]) return true;
  const bool read = frame.bytes[0] & 1;
  for (unsigned i = 1; i < frame.size; ++i) {
    const bool expectedAck = !read || i + 1 < frame.size;
    if (frame.ack[i] != expectedAck) return true;
  }
  return false;
}
struct Packet {
  sniffer::Frame frame;
  uint64_t start_cycles=0,end_cycles=0;
  uint32_t pio_Hz=0;
  bool timingValid=false;
  uint32_t epoch;
  uint32_t number;
  uint8_t states[EDGE_HISTORY];
  uint8_t stateCount;
  uint64_t delta_cycles[EDGE_HISTORY];
  bool traceTruncated;
  uint8_t corrections;
};
struct Stats {
  uint32_t sclEdges, sdaEdges, starts, stops, bytes, frames, invalid;
  uint32_t ringOverruns, fifoStalls, queueDrops, epoch;
  bool active, fault;
  uint32_t duplicateStates, badWords;
};
bool init();
const char *error();
void request(bool enabled);
bool active();
void requestWitness(bool enabled);
bool witnessActive();
bool witnessIdle(uint32_t guard_us);
uint32_t witnessStarts();
uint32_t witnessStops();
void service(); // Called only on core 1.
bool pop(Packet &packet); // Called only on core 0.
Stats stats();
timing::Snapshot timingStats();
void resetStats(); // Only while stopped.
bool setExportMode(bool enabled); // Only while stopped; mutually exclusive with decoding.
bool popExport(recording::Event &event);
bool exportEmpty();
recording::Summary exportSummary(); // Only read after stop acknowledgement.
struct QualificationStats {
  bool enabled;
  uint32_t threshold_cycles, clockSuppressed, boundarySuppressed, overflows, correctedFrames;
};
QualificationStats qualificationStats();
bool setQualification(bool enabled); // Paused only; takes effect on next arm.
} // namespace capture
