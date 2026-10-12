#include "capture.h"
#include "Arduino.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/regs/dma.h"
#include "hardware/regs/pio.h"
#include "i2c_sniffer.pio.h"
#include <new>

static_assert(I2C_SNIFF_SCL_PIN == I2C_SNIFF_SDA_PIN + 1, "PIO requires adjacent SDA/SCL pins");
static_assert(I2C_SNIFF_SDA_PIN >= 0 && I2C_SNIFF_SCL_PIN < NUM_BANK0_GPIOS, "Invalid probe pins");
static_assert(I2C_SNIFF_SCL_PIN <= 31, "This capture uses the default PIO GPIO base 0");

namespace capture {
namespace {
constexpr uint32_t RING_WORDS = 8192;
#if PICO_RP2350
// RP2350 TRANS_COUNT[31:28] is MODE, not count. Mode 0 must decrement.
constexpr uint32_t DMA_TRANSFERS = DMA_CH0_TRANS_COUNT_COUNT_BITS;
#else
constexpr uint32_t DMA_TRANSFERS = 0xffffffffu;
#endif
// Timed packets contain 768 bytes of evidence in addition to their frame.
// Leave heap room for the Arduino core-1 stack and the TFT sprite.
constexpr uint32_t QUEUE_SIZE = 16;
alignas(32768) volatile uint32_t ring[RING_WORDS];
union QueueStorage {
  Packet packets[QUEUE_SIZE];
  recording::Event events[1024];
  QueueStorage() : packets{} {}
} storage;
auto &queue = storage.packets;
constexpr unsigned EXPORT_CAPACITY = 1024;
std::atomic<bool> exporting{false};
recording::Summary exportCounts;
bool exportStopping = false;
static_assert(sizeof(queue) <= 24 * 1024, "Frame queue exceeds RAM budget");
std::atomic<uint32_t> head{0}, tail{0};
std::atomic<bool> ready{false}, wanted{false}, running{false}, fault{false};
std::atomic<bool> witnessWanted{false}, privateCapture{false};
std::atomic<bool> witnessFree{false};
std::atomic<uint32_t> witnessChanged_us{0};
std::atomic<uint8_t> witnessState{0};
std::atomic<bool> witnessCaughtUp{false};
std::atomic<uint32_t> privateStarts{0},privateStops{0};
std::atomic<uint32_t> sclEdges{0}, sdaEdges{0}, starts{0}, stops{0}, bytes{0};
std::atomic<uint32_t> frames{0}, invalid{0}, overruns{0}, stalls{0}, drops{0}, epoch{0};
std::atomic<uint32_t> duplicateStates{0}, badWords{0};
uint8_t edgeHistory[EDGE_HISTORY];
uint64_t durationHistory[EDGE_HISTORY];
uint32_t edgePosition = 0;
uint32_t frameFirstEdge = 0, sampleBase = timing::SEED_CYCLES;
bool timingSeeded = false;
bool hardwareSeed = true;
uint64_t heartbeatCycles = 0;
timing::Analyzer analyzer;
std::atomic<uint32_t> timingGeneration{0}, timingValues[timing::Count];
uint32_t timing_ms = 0;
void publishTiming() {
  const auto snapshot = analyzer.snapshot();
  timingGeneration.fetch_add(1, std::memory_order_seq_cst);
  for (unsigned i = 0; i < timing::Count; ++i) timingValues[i].store(snapshot.value[i], std::memory_order_seq_cst);
  timingGeneration.fetch_add(1, std::memory_order_seq_cst);
}
bool haveLastState = false;
uint8_t lastState = 0;
PIO pio = pio1;
uint sm = 0, offset = 0;
int channel = -1;
uint32_t consumed = 0;
sniffer::Decoder decoder;
qualification::Filter qualifier;
std::atomic<bool> qualificationEnabled{true};
std::atomic<uint32_t> qualifiedClock{0}, qualifiedBoundary{0}, qualificationOverflow{0}, correctedFrames{0};
uint64_t observedCycles = 0, decodedCycles = 0, lastObservation_us = 0;
uint64_t frameStart_cycles=0;
uint8_t frameCorrections = 0;
uint8_t lastQualifiedState = 0;
bool haveQualifiedState = false;
void acceptQualified(const qualification::Sample &sample);
void publishQualification() {
  qualifiedClock.store(qualifier.clockSuppressed);
  qualifiedBoundary.store(qualifier.boundarySuppressed);
  qualificationOverflow.store(qualifier.overflows);
}
const char *errorText = "";
void publishDecoderStats() {
  if (privateCapture.load()) return;
  sclEdges.store(decoder.sclEdges, std::memory_order_relaxed);
  sdaEdges.store(decoder.sdaEdges, std::memory_order_relaxed);
  starts.store(decoder.starts, std::memory_order_relaxed);
  stops.store(decoder.stops, std::memory_order_relaxed);
  bytes.store(decoder.bytes, std::memory_order_relaxed);
}

void stopHardware() {
  pio_sm_set_enabled(pio, sm, false);
  dma_channel_abort(channel);
  pio_sm_clear_fifos(pio, sm);
  if (!exporting.load()) qualifier.flush(acceptQualified);
  qualifier.reset(); publishQualification();
  publishDecoderStats();
  decoder.reset();
  publishTiming();
  running.store(false, std::memory_order_release);
}

void startHardware() {
  pio_sm_set_enabled(pio, sm, false);
  pio_sm_clear_fifos(pio, sm);
  pio_sm_restart(pio, sm);
  pio_sm_exec(pio, sm, pio_encode_jmp(offset));
  pio->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm);
  consumed = 0;
  exportStopping = false;
  decoder.reset();
  analyzer.resetTracking();
  qualifier.configure(analyzer.pio_Hz, qualificationEnabled.load());
  observedCycles = decodedCycles = 0; frameCorrections = 0; haveQualifiedState = false;
  timingSeeded = false; sampleBase = timing::SEED_CYCLES;
  hardwareSeed = true;
  heartbeatCycles = 0;
  ++epoch;
  edgePosition = 0;
  frameFirstEdge = 0;
  haveLastState = false;
  witnessFree=false; witnessCaughtUp=false; witnessState=0; witnessChanged_us=time_us_32();
  privateStarts=privateStops=0;
  // Drop old queued frames at an explicit arm boundary.
  tail.store(head.load(std::memory_order_acquire), std::memory_order_release);
  dma_channel_config config = dma_channel_get_default_config(channel);
  channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
  channel_config_set_read_increment(&config, false);
  channel_config_set_write_increment(&config, true);
  channel_config_set_ring(&config, true, 15); // Bytes; 32 KiB, naturally aligned.
  channel_config_set_dreq(&config, pio_get_dreq(pio, sm, false));
  channel_config_set_high_priority(&config, true);
  dma_channel_configure(channel, &config, ring, &pio->rxf[sm], DMA_TRANSFERS, true);
  pio_sm_set_enabled(pio, sm, true);
  running.store(true, std::memory_order_release);
}

void exportEvent(uint32_t iterations, uint32_t state) {
  const uint64_t sequence = exportCounts.generated++;
  const uint32_t h = head.load(std::memory_order_relaxed);
  if (h - tail.load(std::memory_order_acquire) >= EXPORT_CAPACITY) {
    ++exportCounts.dropped;
    return;
  }
  new (&storage.events[h % EXPORT_CAPACITY]) recording::Event{sequence, iterations, state};
  ++exportCounts.queued;
  head.store(h + 1, std::memory_order_release);
}
void exportGap(uint32_t reason) {
  ++exportCounts.gaps;
  exportEvent(reason, 0x100); // Playback must reset tracking; never infer continuity.
}

void enqueue(const sniffer::Frame &frame) {
  const uint32_t number = ++frames;
  if (frame.invalid) ++invalid;
  const uint32_t h = head.load(std::memory_order_relaxed);
  if (h - tail.load(std::memory_order_acquire) >= QUEUE_SIZE) {
    ++drops;
    ++epoch; // Prevent register association across a lost frame.
    return;
  }
  Packet &packet = queue[h % QUEUE_SIZE];
  new (&packet) Packet;
  packet.frame = frame;
  packet.start_cycles=frameStart_cycles;
  packet.end_cycles=decodedCycles;
  packet.pio_Hz=analyzer.pio_Hz;
  packet.timingValid=timingSeeded; // Decode errors do not erase captured time evidence.
  packet.epoch = epoch.load();
  packet.number = number;
  packet.stateCount = 0;
  packet.traceTruncated = false;
  packet.corrections = frameCorrections;
  if (needsFaultTrace(frame)) {
    packet.stateCount = min<uint32_t>(edgePosition, EDGE_HISTORY);
    const uint32_t first = edgePosition - packet.stateCount;
    packet.traceTruncated = first > frameFirstEdge;
    for (unsigned i = 0; i < packet.stateCount; ++i) {
      packet.states[i] = edgeHistory[(first + i) % EDGE_HISTORY];
      packet.delta_cycles[i] = durationHistory[(first + i) % EDGE_HISTORY];
    }
  }
  head.store(h + 1, std::memory_order_release);
}
void acceptQualified(const qualification::Sample &sample) {
  if (privateCapture.load()) {
    const uint8_t previous=witnessState.load();
    witnessChanged_us.store(time_us_32());
    if ((previous&2) && (sample.state&2) && ((previous^sample.state)&1)) {
      if (sample.state&1) ++privateStops;else ++privateStarts;
      witnessFree.store(bool(sample.state&1));
    }
    witnessState.store(sample.state);
    return;
  }
  frameCorrections |= sample.corrections;
  if (haveQualifiedState && sample.state == lastQualifiedState && !sample.heartbeat) return;
  haveQualifiedState = true; lastQualifiedState = sample.state;
  const unsigned previousStarts = decoder.starts;
  const bool completed = decoder.feed(sample.state);
  analyzer.observe(sample.state, sample.cycles - decodedCycles, completed ? &decoder.completed : nullptr, millis());
  decodedCycles = sample.cycles;
  if (completed) {
    if (frameCorrections && correctedFrames.load() != UINT32_MAX) correctedFrames.fetch_add(1);
    enqueue(decoder.completed); frameFirstEdge = edgePosition ? edgePosition - 1 : 0;
    frameCorrections = 0;
  } else if (decoder.starts != previousStarts) frameCorrections = 0;
  if (decoder.starts != previousStarts) frameStart_cycles=sample.cycles;
}
} // namespace

bool init() {
  const int claimed = pio_claim_unused_sm(pio, false);
  if (claimed < 0) { errorText = "No free PIO SM"; return false; }
  sm = claimed;
  if (!pio_can_add_program(pio, &i2c_edge_capture_program)) {
    pio_sm_unclaim(pio, sm); errorText = "PIO memory full"; return false;
  }
  channel = dma_claim_unused_channel(false);
  if (channel < 0) { pio_sm_unclaim(pio, sm); errorText = "No free DMA"; return false; }
  offset = pio_add_program(pio, &i2c_edge_capture_program);
  pio_gpio_init(pio, I2C_SNIFF_SDA_PIN);
  pio_gpio_init(pio, I2C_SNIFF_SCL_PIN);
  pio_sm_config config = i2c_edge_capture_program_get_default_config(offset);
  sm_config_set_in_pins(&config, I2C_SNIFF_SDA_PIN);
  sm_config_set_in_shift(&config, false, false, 32); // Two-bit state in bits 1:0.
  sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
  sm_config_set_clkdiv(&config, 1.0f);
  if (pio_sm_init(pio, sm, offset, &config) < 0 ||
      pio_sm_set_consecutive_pindirs(pio, sm, I2C_SNIFF_SDA_PIN, 2, false) < 0) {
    pio_remove_program(pio, &i2c_edge_capture_program, offset);
    dma_channel_unclaim(channel); pio_sm_unclaim(pio, sm);
    errorText = "PIO config failed"; return false;
  }
  gpio_disable_pulls(I2C_SNIFF_SDA_PIN);
  gpio_disable_pulls(I2C_SNIFF_SCL_PIN);
  analyzer.pio_Hz = clock_get_hz(clk_sys); // Divider is exactly 1; PIO instruction cycles.
  publishTiming();
  ready.store(true, std::memory_order_release);
  return true;
}

const char *error() { return errorText; }
void request(bool enabled) { wanted.store(enabled, std::memory_order_release); }
bool active() { return running.load(std::memory_order_acquire) && !privateCapture.load(); }
void requestWitness(bool enabled) { witnessWanted.store(enabled); }
bool witnessActive() { return running.load(std::memory_order_acquire) && privateCapture.load(); }
uint32_t witnessStarts() { return privateStarts.load(); }
uint32_t witnessStops() { return privateStops.load(); }
bool witnessIdle(uint32_t guard_us) {
  return witnessActive() && witnessCaughtUp.load() && !fault.load() && witnessFree.load() && witnessState.load()==3 &&
      uint32_t(time_us_32()-witnessChanged_us.load())>=guard_us;
}

void service() {
  if (!ready.load(std::memory_order_acquire)) return;
  const bool privateWanted=witnessWanted.load(std::memory_order_acquire);
  if (running.load() && privateCapture.load()!=privateWanted) stopHardware();
  privateCapture.store(privateWanted);
  const bool enable = wanted.load(std::memory_order_acquire) || privateWanted;
  if (enable != running.load(std::memory_order_relaxed)) {
    if (!enable) {
      if (exporting.load()) {
        if (!exportStopping) { pio_sm_set_enabled(pio, sm, false); exportStopping = true; }
        // DMA drains the disabled SM's FIFO; consume its committed tail below.
      } else { stopHardware(); fault.store(false); }
    }
    else if (!fault.load()) startHardware();
  }
  if (!running.load()) return;
  const uint32_t remaining = dma_channel_hw_addr(channel)->transfer_count;
  const uint32_t produced = DMA_TRANSFERS - remaining;
  // WRITE_ADDR advances after a write access (RP2040 datasheet 2.5.1.1).
  // Reconcile its ring position with a stable transfer-count snapshot.
  const uint32_t writtenIndex = (dma_channel_hw_addr(channel)->write_addr -
      reinterpret_cast<uintptr_t>(ring)) / sizeof(uint32_t);
  if (dma_channel_hw_addr(channel)->transfer_count != remaining) return;
  const uint32_t inFlight = (produced - writtenIndex) & (RING_WORDS - 1);
  if (inFlight > 8 || produced < inFlight) return;
  const uint32_t committed = produced - inFlight;
  const uint32_t stallMask = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm);
  const bool stalled = (pio->fdebug & stallMask) != 0;
  if (consumed!=committed || !pio_sm_is_rx_fifo_empty(pio,sm)) witnessCaughtUp=false;
  if (stalled || committed - consumed >= RING_WORDS) {
    witnessFree=false;
    if (exporting.load()) {
      if (stalled) ++exportCounts.fifo;
      if (committed - consumed >= RING_WORDS) ++exportCounts.ring;
      exportGap((stalled ? recording::Fifo : 0) |
          (committed - consumed >= RING_WORDS ? recording::Ring : 0));
    }
    if (stalled) { ++stalls; pio->fdebug = stallMask; }
    if (committed - consumed >= RING_WORDS) ++overruns;
    ++epoch;
    decoder.reset();
    qualifier.reset(); frameCorrections = 0; observedCycles = decodedCycles = 0; haveQualifiedState = false;
    analyzer.gap(); timingSeeded = false; hardwareSeed = false; sampleBase = timing::CHANGE_CYCLES;
    edgePosition = 0;
    frameFirstEdge = 0; heartbeatCycles = 0;
    haveLastState = false;
    consumed = committed & ~1u; // Keep duration/state pair alignment after loss.
  }
  const uint32_t safeProduced = committed;
  if (consumed < safeProduced) {
    __dmb();
    // Bound each batch so arm/stop requests and fault checks stay responsive.
    const uint32_t limit = min<uint32_t>(safeProduced & ~1u, consumed + 256);
    while (consumed < limit) {
      const uint32_t iterations = ring[consumed++ % RING_WORDS];
      const uint32_t word = ring[consumed++ % RING_WORDS];
      if (exporting.load()) {
        if (word > 3) { ++exportCounts.bad; exportGap(recording::BadWord); }
        else exportEvent(iterations, word);
        continue;
      }
      if (word > 3) {
        witnessFree=false;
        ++badWords; ++epoch; decoder.reset();
        qualifier.reset(); frameCorrections = 0; observedCycles = decodedCycles = 0; haveQualifiedState = false;
        edgePosition = 0; haveLastState = false;
        frameFirstEdge = 0; heartbeatCycles = 0;
        analyzer.gap(); timingSeeded = false; hardwareSeed = false; sampleBase = timing::CHANGE_CYCLES;
        continue;
      }
      const uint8_t state = word;
      const uint64_t delta_cycles = timingSeeded ? uint64_t(iterations) * timing::SAMPLE_CYCLES + sampleBase : 0;
      const bool heartbeat = timingSeeded && haveLastState && state == lastState && iterations == UINT32_MAX;
      sampleBase = heartbeat ? timing::HEARTBEAT_CYCLES : hardwareSeed ? timing::SEED_CYCLES : timing::CHANGE_CYCLES;
      hardwareSeed = false;
      timingSeeded = true;
      observedCycles += delta_cycles;
      lastObservation_us = time_us_64();
      if (heartbeat) {
        qualifier.feed(state, observedCycles, acceptQualified, true);
        heartbeatCycles += delta_cycles; continue;
      }
      if (haveLastState && state == lastState) ++duplicateStates;
      haveLastState = true;
      lastState = state;
      edgeHistory[edgePosition++ % EDGE_HISTORY] = state;
      durationHistory[(edgePosition - 1) % EDGE_HISTORY] = delta_cycles + heartbeatCycles;
      heartbeatCycles = 0;
      if (!qualifier.feed(state, observedCycles, acceptQualified)) {
        ++epoch; decoder.reset(); analyzer.gap(); frameCorrections = 0;
        decodedCycles = observedCycles;
        haveQualifiedState = false;
      }
    }
    sclEdges.store(decoder.sclEdges, std::memory_order_relaxed);
    sdaEdges.store(decoder.sdaEdges, std::memory_order_relaxed);
    starts.store(decoder.starts, std::memory_order_relaxed);
    stops.store(decoder.stops, std::memory_order_relaxed);
    bytes.store(decoder.bytes, std::memory_order_relaxed);
  }
  // PIO emits only changes. Dispatch a final persistent STOP without waiting for
  // the next transaction. The quiet grace is delivery latency, not pulse width:
  // only use it with no committed backlog, empty RX FIFO and stable DMA progress.
  if (!exporting.load() && qualifier.pending() && consumed == committed &&
      pio_sm_is_rx_fifo_empty(pio, sm) && time_us_64() - lastObservation_us >= 100 &&
      dma_channel_hw_addr(channel)->transfer_count == remaining) {
    qualifier.advance(observedCycles + qualifier.threshold_cycles, acceptQualified);
  }
  publishQualification();
  witnessCaughtUp.store(consumed==committed && pio_sm_is_rx_fifo_empty(pio,sm) &&
      dma_channel_hw_addr(channel)->transfer_count==remaining);
  publishDecoderStats();
  if (millis() - timing_ms >= 250) { timing_ms = millis(); publishTiming(); }
  const uint32_t errors = DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
  if (!remaining || (dma_channel_hw_addr(channel)->ctrl_trig & errors)) {
    if (exporting.load()) { ++exportCounts.dma; exportGap(recording::Dma); }
    ++epoch;
    stopHardware();
    wanted.store(false);
    fault.store(true);
    return;
  }
  if (exportStopping && consumed == (committed & ~1u) && pio_sm_is_rx_fifo_empty(pio, sm) &&
      dma_channel_hw_addr(channel)->transfer_count == remaining) {
    exportCounts.terminalWords = committed & 1u;
    stopHardware();
  }
}

bool pop(Packet &packet) {
  const uint32_t t = tail.load(std::memory_order_relaxed);
  if (t == head.load(std::memory_order_acquire)) return false;
  packet = queue[t % QUEUE_SIZE];
  tail.store(t + 1, std::memory_order_release);
  return true;
}

Stats stats() {
  return Stats{sclEdges.load(), sdaEdges.load(), starts.load(), stops.load(), bytes.load(),
               frames.load(), invalid.load(), overruns.load(), stalls.load(), drops.load(),
               epoch.load(), active(), fault.load(), duplicateStates.load(), badWords.load()};
}

timing::Snapshot timingStats() {
  static timing::Snapshot previous;
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    const uint32_t generation = timingGeneration.load(std::memory_order_seq_cst);
    if (generation & 1) continue;
    timing::Snapshot result;
    for (unsigned i = 0; i < timing::Count; ++i) result.value[i] = timingValues[i].load(std::memory_order_seq_cst);
    if (generation == timingGeneration.load(std::memory_order_seq_cst)) { previous = result; return result; }
  }
  return previous;
}

void resetStats() {
  // Core 0 calls this only after the stop acknowledgement from core 1.
  decoder = sniffer::Decoder{};
  sclEdges = sdaEdges = starts = stops = bytes = 0;
  frames = invalid = overruns = stalls = drops = 0;
  head = tail = 0;
  fault.store(false);
  duplicateStates = badWords = 0;
  qualifier.reset(); qualifier.clearCounters(); publishQualification(); correctedFrames = 0;
  analyzer.clear();
  publishTiming();
  ++epoch;
}

bool setExportMode(bool enabled) {
  if (active() || wanted.load(std::memory_order_acquire)) return false;
  head = tail = 0;
  exportCounts = recording::Summary{};
  exporting.store(enabled, std::memory_order_release);
  return true;
}
bool popExport(recording::Event &event) {
  const uint32_t t = tail.load(std::memory_order_relaxed);
  if (t == head.load(std::memory_order_acquire)) return false;
  event = storage.events[t % EXPORT_CAPACITY];
  tail.store(t + 1, std::memory_order_release);
  return true;
}
bool exportEmpty() { return tail.load(std::memory_order_acquire) == head.load(std::memory_order_acquire); }
recording::Summary exportSummary() { return exportCounts; }
QualificationStats qualificationStats() {
  return QualificationStats{qualificationEnabled.load(),
    uint32_t((uint64_t(analyzer.pio_Hz) * 150 + 999999999) / 1000000000),
    qualifiedClock.load(), qualifiedBoundary.load(), qualificationOverflow.load(), correctedFrames.load()};
}
bool setQualification(bool enabled) {
  if (active() || wanted.load() || exporting.load()) return false;
  qualificationEnabled.store(enabled); return true;
}
} // namespace capture
