#include <Arduino.h>
#if !SNIFFER_HEADLESS
#include <Adafruit_NeoPixel.h>
#include <Bootsel.h>
#include <TFT_eSPI.h>
#include <EEPROM.h>
#endif
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "capture.h"
#include "decoder_tests.h"
#include "transaction_output.h"
#include "serial_output_queue.h"
#include "transaction_observer.h"
#include "button_gestures.h"
#include "screen_settings.h"
#include "delivery.h"
#include "recorder_wire.h"
#if SNIFFER_HEADLESS
#include "client_protocol.h"
#endif
#if SNIFFER_HEADLESS && PICO_RP2350 && I2C_SNIFF_SDA_PIN == 30 && I2C_SNIFF_SCL_PIN == 31
#define ACTIVE_READ_AVAILABLE 1
#include "active_hardware.h"
#include "scan_batch.h"
#include "read_batch.h"
#include "experimental_protocol.h"
#else
#define ACTIVE_READ_AVAILABLE 0
#endif

#if !SNIFFER_HEADLESS
#if defined(RP2040_PIO_INTERFACE) || TFT_SPI_PORT != 1
#error "Timing capture requires TFT hardware SPI1; PIO TFT exhausts instruction memory"
#endif
static_assert(TFT_SCLK == 10 && TFT_MOSI == 11 && TFT_MISO == -1,
    "Verified display wiring: SPI1 SCK GP10, TX GP11, no RX pin");
static_assert(TFT_SPI_MODE == 0, "Match the previously working PIO display clock mode");
#endif

bool core1_separate_stack = true;

namespace {
constexpr unsigned HISTORY_SIZE = 96, DISPLAY_ROWS = 6;
struct Discovery { uint8_t address, command, kind, length; bool hasCommand; };
struct Pending { bool valid = false; uint8_t command = 0; };
uint8_t seen[128][3][32] = {};
bool seenUnassociatedRead[128] = {};
Pending pending[128];
Discovery history[HISTORY_SIZE];
unsigned historyCount = 0, scrollTop = 0;
uint32_t lastEpoch = 0, traceDrops = 0, outputDrops = 0;
uint32_t disconnectedDrops = 0;
uint32_t rejectedLines = 0;
constexpr unsigned USB_CAPACITY = 32768;
sniffer::SerialOutputQueue<USB_CAPACITY> usbOutput;
// Only core 0 consumes packets. Keep the large working packet off its stack.
capture::Packet workingPacket;
bool captureReady = false, armed = false, verbose = true, rawOutput = false, dirty = true;
bool exportSession = false, exportEnding = false;
uint8_t exportBuffer[recording::MAX_PACKET];
size_t exportSize = 0, exportSent = 0;
uint32_t exportSequence = 0, exportDisconnected_ms = 0;
recording::Event exportPending;
bool exportHavePending = false;
sniffer::TransactionOutput transactions;
sniffer::DeliveryFilter deliveryFilter, stagedFilter;
sniffer::DeliveryCounters deliveryCounters;
constexpr unsigned CHAIN_CAPACITY = 8;
sniffer::Frame chain[CHAIN_CAPACITY];
recorder_wire::Timing chainTiming;
uint32_t chainNumbers[CHAIN_CAPACITY] = {};
uint8_t chainStates[CHAIN_CAPACITY][capture::EDGE_HISTORY] = {};
uint8_t chainStateCounts[CHAIN_CAPACITY] = {};
uint64_t chainDurations[CHAIN_CAPACITY][capture::EDGE_HISTORY] = {};
bool chainTraceTruncated[CHAIN_CAPACITY] = {};
uint8_t chainCorrections[CHAIN_CAPACITY] = {};
unsigned chainCount = 0;
bool chainOverflow = false, staging = false, commitPending = false, snapshotPending = false;
uint32_t stageId = 0, filterGeneration = 0, snapshotId = 0, transactionNumber = 0;
uint32_t stage_ms = 0;
unsigned snapshotCursor = 0;
bool snapshotStarted = false;
#if !SNIFFER_HEADLESS
sniffer::TransactionObserver observer;
sniffer::RecentTransactions<32> recentTransactions;
sniffer::UniqueTransactions<64> uniqueTransactions;
bool uniqueMode = false, storedUniqueMode = false, settingsValid = false;
bool settingsPending = false, settingsFailed = false;
unsigned screenPage = 0;
uint32_t page_ms = 0;
TFT_eSPI tft(TFT_WIDTH, TFT_HEIGHT);
// One reusable strip avoids a full-screen framebuffer and visible erase/redraw.
TFT_eSprite displayStrip(&tft);
bool displayBufferReady = false;
Adafruit_NeoPixel statusPixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
#endif
bool solidLedShown = false;
bool forceScreen = true;
#if SNIFFER_HEADLESS
link::Client usbClient;
link::UartClient uartClient;
bool structuredUsb = false, switchUsbPending = false, uartReady = false;
transport5::Rate uartRate;
uint32_t uartRxOverflows = 0;
uint32_t diagnosticEpoch=0;
#if ACTIVE_READ_AVAILABLE
active_read::Hardware activeHardware;
active_read::Job<active_read::Hardware> activeJob(activeHardware);
active_read::ScanBatch<active_read::Hardware> scanBatch(activeJob);
active_read::ReadBatch<active_read::Hardware> readBatch(activeJob);
uint8_t activeReply[35]={};
uint8_t replayReadReply[288]={};
size_t replayReadLength=0;
bool activeReplyPending=false;
uint8_t activeFormat=1;
bool activeGapMade=false;
active_read::RequestGate activeGate;
void serviceActive() {
  if(readBatch.running)readBatch.tick(time_us_32());else scanBatch.tick(time_us_32());
  // An unsent progress snapshot must not hide cancellation's terminal restore result.
  if (scanBatch.running && activeReplyPending && activeFormat==4 &&
      !activeReply[30] && scanBatch.terminal()) activeReplyPending=false;
  if(readBatch.running && activeReplyPending && activeFormat==6 && !activeReply[34] && readBatch.terminal())activeReplyPending=false;
  if (activeJob.phase==active_read::Phase::Done && !activeReplyPending) {
    const auto &r=activeJob.result;
    memset(activeReply,0,sizeof(activeReply));link::put32(activeReply,r.request);
    activeReply[4]=uint8_t(r.code);activeReply[5]=r.count;activeReply[6]=r.restored;
    activeReply[7]=r.reg;link::put32(activeReply+8,r.abort);link::put32(activeReply+12,r.clock_Hz);
    link::put32(activeReply+16,r.idle_us);link::put32(activeReply+20,r.duration_us);
    activeReply[24]=r.bytes[0];activeReply[25]=r.bytes[1];activeReply[26]=r.wasArmed;activeReply[27]=activeFormat;
    if (activeFormat==2) {
      activeReply[28]=r.bytes[2];activeReply[29]=activeHardware.address;
      activeReply[30]=activeJob.requested;activeReply[31]=activeJob.registers[0];
    }
    if (activeFormat==3) {
      activeReply[28]=activeHardware.rawMode;activeReply[29]=activeHardware.address;
      activeReply[30]=uint8_t(activeHardware.writeSize);activeReply[31]=uint8_t(activeHardware.writeSize>>8);
    }
    if (activeFormat==4) {
      activeReply[28]=2;activeReply[29]=scanBatch.address;
      activeReply[30]=scanBatch.terminal();activeReply[31]=scanBatch.last;
    }
    if(activeFormat==5){
      memcpy(replayReadReply,activeReply,32);replayReadReply[27]=5;
      replayReadReply[28]=3;replayReadReply[29]=activeHardware.address;
      link::put16(replayReadReply+30,activeHardware.readReceived);
      memcpy(replayReadReply+32,activeHardware.readData,activeHardware.readReceived);
      replayReadLength=32+activeHardware.readReceived;
    }
    if(activeFormat==6){
      activeReply[28]=r.bytes[2];activeReply[29]=activeHardware.address;
      activeReply[30]=activeJob.requested;activeReply[31]=activeJob.registers[0];
      link::put16(activeReply+32,readBatch.index);activeReply[34]=readBatch.terminal();
    }
    activeReplyPending=true;
    if (!scanBatch.running && !readBatch.running) activeJob.phase=active_read::Phase::Idle;
    activeGate.cached=readBatch.running?readBatch.terminal():!scanBatch.running || scanBatch.terminal();
    armed=capture::active();solidLedShown=false;dirty=true;
  }
  if (activeReplyPending && (activeFormat==5?uartClient.send(link::ReplayReadResult,replayReadReply,replayReadLength):uartClient.send(link::ActiveResult,activeReply,activeFormat==6?35:32))) {
    activeReplyPending=false;scanBatch.delivered(time_us_32());readBatch.delivered(time_us_32());
  }
}
#endif
void serviceClients();
void notifyClients();
#endif

const char *kindName(uint8_t kind) { return kind == 0 ? "W" : kind == 1 ? "R" : "BC"; }
void led();
void recordTransaction(const sniffer::Transaction &value);

bool serialLine(const char *line) {
#if SNIFFER_HEADLESS
  if (structuredUsb) return true; // Legacy text never enters binary framing.
#endif
  if (usbOutput.enqueue(line)) return true;
  ++rejectedLines;
  return false;
}

void serviceUsb() {
#if SNIFFER_HEADLESS
  if (structuredUsb) return;
#endif
  if (exportSession) return; // Binary and human-readable output never interleave.
  static bool connected = false;
  static bool waitingForConnection = false;
  static uint32_t disconnected_ms = 0;
  static uint32_t reportedDrops = 0, report_ms = 0;
  if (!Serial) {
    // operator bool can also return false on USB mutex contention. Do not
    // discard buffered lines for a single transient failure.
    if (!waitingForConnection) { waitingForConnection = true; disconnected_ms = millis(); }
    if (millis() - disconnected_ms >= 500) {
      disconnectedDrops += usbOutput.clear();
      connected = false;
    }
    return;
  }
  waitingForConnection = false;
  if (!connected) {
    // A reconnect may follow a partially transmitted line; start a fresh line.
    usbOutput.enqueue("\n");
    usbOutput.enqueue("RP2040 sniffer timing: 256-byte capture / protocol 1\n");
    usbOutput.enqueue("Decoder compile-time tests: PASS (15)\n");
    usbOutput.enqueue("USB: v=readable, r=raw, q=discoveries, a=arm, i=idle, c=clear+idle, d=diagnostics, t=timing\n");
    connected = true;
  }
  // SerialUSB.cpp can wait on a full FIFO. Never request more than the current
  // available space, and bound each pump to four 64-byte writes. Core 0 is the
  // only Serial writer; all messages use this queue.
  for (unsigned i = 0; i < 4 && usbOutput.bytes(); ++i) {
    const int available = Serial.availableForWrite();
    if (available <= 0) break;
    const size_t count = min<size_t>(64, min<size_t>(available, usbOutput.contiguous()));
    const size_t written = Serial.write(usbOutput.front(), count);
    usbOutput.consume(written);
    if (written < count) break;
  }
  const uint32_t dropped = rejectedLines + disconnectedDrops;
  if (dropped < reportedDrops) reportedDrops = dropped; // Explicit user clear.
  const uint32_t now_ms = millis();
  if (dropped != reportedDrops && now_ms - report_ms >= 1000) {
    char line[96];
    snprintf(line, sizeof(line), "OUTPUT_LOSS dropped_lines=%lu queued_bytes=%u (use d for diagnostics)\n",
        static_cast<unsigned long>(dropped), static_cast<unsigned>(usbOutput.bytes()));
    if (usbOutput.enqueue(line)) { reportedDrops = dropped; report_ms = now_ms; }
  }
}

void beginExport() {
  if (!captureReady || capture::active() || staging || commitPending || snapshotPending) {
    serialLine("ERROR,export_requires_idle\n"); return;
  }
  if (!capture::setExportMode(true)) { serialLine("ERROR,export_busy\n"); return; }
  capture::request(true);
  const uint32_t began_ms = millis();
  while (!capture::active() && millis() - began_ms < 100) delay(1);
  if (!capture::active()) {
    capture::request(false);
    serialLine("ERROR,export_start_failed\n"); return;
  }
  usbOutput.clear();
  exportSession = true; exportEnding = false; exportSequence = 0;
  exportSent = 0; exportDisconnected_ms = 0; armed = true;
  exportHavePending = false;
  uint8_t *p = exportBuffer + 16;
  recording::put32(p, capture::timingStats().value[timing::PioHz]);
  recording::put32(p + 4, I2C_SNIFF_SDA_PIN);
  recording::put32(p + 8, I2C_SNIFF_SCL_PIN);
  recording::put32(p + 12, timing::SAMPLE_CYCLES);
  recording::put32(p + 16, timing::CHANGE_CYCLES);
  recording::put32(p + 20, timing::SEED_CYCLES);
  recording::put32(p + 24, timing::HEARTBEAT_CYCLES);
  recording::put32(p + 28, 1024);
  exportSize = recording::seal(exportBuffer, recording::Begin, exportSequence++, 32);
}

void serviceExport() {
  // Only 'x' is accepted during binary acquisition. No menus or text responses.
  while (Serial.available()) if (Serial.read() == 'x') capture::request(false);
  if (!Serial.dtr()) {
    if (!exportDisconnected_ms) exportDisconnected_ms = millis();
    if (millis() - exportDisconnected_ms >= 500) capture::request(false);
  } else exportDisconnected_ms = 0;
  if (exportDisconnected_ms && millis() - exportDisconnected_ms >= 1500 && !capture::active()) {
    // A disconnected host cannot receive END. Leave its file explicitly incomplete,
    // but restore ordinary standby so a subsequent connection can start afresh.
    exportSession = false; exportSize = exportSent = 0; armed = false;
    capture::setExportMode(false); dirty = forceScreen = true;
    return;
  }
  if (exportSent < exportSize) {
    for (unsigned i = 0; i < 16 && exportSent < exportSize; ++i) {
      const int available = Serial.availableForWrite();
      if (available <= 0) return;
      const size_t n = min<size_t>(64, min<size_t>(available, exportSize - exportSent));
      const size_t written = Serial.write(exportBuffer + exportSent, n);
      exportSent += written;
      if (written < n) return;
    }
    if (exportSent < exportSize) return;
  }
  if (exportEnding) {
    exportSession = false; armed = false; solidLedShown = false;
    capture::setExportMode(false); dirty = forceScreen = true;
#if !SNIFFER_HEADLESS
    observer.interrupt(recordTransaction);
#endif
    return;
  }
  exportSent = 0;
  unsigned count = 0;
  recording::Event event;
  uint64_t next = 0;
  while (count < recording::MAX_EVENTS) {
    if (exportHavePending) { event = exportPending; exportHavePending = false; }
    else if (!capture::popExport(event)) break;
    if (count && event.sequence != next) {
      exportPending = event; exportHavePending = true; break;
    }
    if (!count) recording::put64(exportBuffer + 16, event.sequence);
    uint8_t *p = exportBuffer + 24 + count++ * 5;
    recording::put32(p, event.iterations); p[4] = event.state == 0x100 ? 0x80 : event.state;
    next = event.sequence + 1;
  }
  if (count) exportSize = recording::seal(exportBuffer, recording::Data, exportSequence++, 8 + count * 5);
  else if (!capture::active() && capture::exportEmpty()) {
    const auto s = capture::exportSummary();
    uint8_t *p = exportBuffer + 16;
    recording::put64(p, s.generated); recording::put64(p + 8, s.queued);
    recording::put64(p + 16, s.dropped);
    const uint32_t fields[] = {s.gaps, s.ring, s.fifo, s.bad, s.dma, s.terminalWords};
    for (unsigned i = 0; i < 6; ++i) recording::put32(p + 24 + i * 4, fields[i]);
    exportSize = recording::seal(exportBuffer, recording::End, exportSequence++, 48);
    exportEnding = true;
  } else exportSize = 0;
  led();
}

void recordTransaction(const sniffer::Transaction &value) {
#if !SNIFFER_HEADLESS
  const uint32_t now_ms = millis();
  recentTransactions.add(value, now_ms);
  if (value.hasRegister) uniqueTransactions.update(value, now_ms);
  dirty = true;
#else
  (void)value;
#endif
}

void timingTrace(uint32_t txn, uint32_t frame, const uint64_t *durations, unsigned count, bool truncated) {
  char line[768];
  for (unsigned first = 0; first < count; first += 24) {
    const unsigned end = min(first + 24, count);
    size_t used = snprintf(line, sizeof(line), "FAULT_TIME,txn=%lu,frame=%lu,offset=%u,total=%u,truncated=%u,pio_hz=%lu,unit=cycles,deltas=",
        static_cast<unsigned long>(txn), static_cast<unsigned long>(frame), first, count, truncated,
        static_cast<unsigned long>(capture::timingStats().value[timing::PioHz]));
    for (unsigned i = first; i < end; ++i)
      used += snprintf(line + used, sizeof(line) - used, "%s%llu", i == first ? "" : ":", static_cast<unsigned long long>(durations[i]));
    line[used++] = '\n'; line[used] = 0;
    if (!serialLine(line)) ++traceDrops;
  }
}

void trace(const capture::Packet &packet) {
  if (!verbose || !rawOutput) return;
  const sniffer::Frame &f = packet.frame;
  char line[sniffer::MAX_BYTES * 4 + 64];
  size_t used = snprintf(line, sizeof(line), "RAW,%lu,%s,%s,",
      static_cast<unsigned long>(packet.number),
      f.end == sniffer::Boundary::Restart ? "Sr" : "P", f.invalid ? "BAD" : "OK");
  for (unsigned i = 0; i < f.size && used + 5 < sizeof(line); ++i)
    used += snprintf(line + used, sizeof(line) - used, "%02X%c%s", f.bytes[i], f.ack[i] ? '+' : '-', i + 1 == f.size ? "" : " ");
  snprintf(line + used, sizeof(line) - used, "\n");
  if (!serialLine(line)) ++traceDrops;
  if (packet.stateCount) {
    used = snprintf(line, sizeof(line), "EDGE,%lu,reason=%02X,states=",
        static_cast<unsigned long>(packet.number), f.errors);
    for (unsigned i = 0; i < packet.stateCount && used + 2 < sizeof(line); ++i)
      line[used++] = '0' + packet.states[i];
    line[used++] = '\n';
    line[used] = '\0';
    if (!serialLine(line)) ++traceDrops;
    timingTrace(0, packet.number, packet.delta_cycles, packet.stateCount, packet.traceTruncated);
  }
}

void addDiscovery(Discovery d) {
  if (d.hasCommand) {
    uint8_t &bits = seen[d.address][d.kind][d.command / 8];
    const uint8_t mask = 1u << (d.command % 8);
    if (bits & mask) return;
    bits |= mask;
  } else {
    if (seenUnassociatedRead[d.address]) return;
    seenUnassociatedRead[d.address] = true;
  }
  if (historyCount == HISTORY_SIZE) {
    memmove(history, history + 1, (HISTORY_SIZE - 1) * sizeof(Discovery));
    --historyCount;
  }
  history[historyCount++] = d;
  scrollTop = historyCount > DISPLAY_ROWS ? historyCount - DISPLAY_ROWS : 0;
  dirty = true;
  if (verbose || deliveryFilter.scope != sniffer::Scope::All || deliveryFilter.outcome != sniffer::Outcome::Both) return;
  char command[3] = "--", line[96];
  if (d.hasCommand) snprintf(command, sizeof(command), "%02X", d.command);
  snprintf(line, sizeof(line), "NEW,addr=%02X,op=%s,cmd=%s,len=%u\n", d.address, kindName(d.kind), command, d.length);
  if (!serialLine(line)) ++outputDrops;
}

void finishChain(const char *reason = nullptr) {
  if (!chainCount && !chainOverflow) return;
  const auto assessment = sniffer::assess(chain, chainCount, reason || chainOverflow);
  deliveryCounters.add(assessment);
  ++transactionNumber;
#if SNIFFER_HEADLESS
  chainTiming.count=chainCount;
  chainTiming.valid=chainTiming.valid && !reason && !chainOverflow;
  uartClient.transaction(transactionNumber, assessment, chain, chainCount, reason || chainOverflow,millis(),&chainTiming);
  if (structuredUsb) usbClient.transaction(transactionNumber, assessment, chain, chainCount, reason || chainOverflow,millis());
#endif
  if (chainOverflow) sniffer::increment(deliveryCounters.chainOverflow);
  // Formatting is deliberately downstream of both independent filters.
  if (!deliveryFilter.accepts(assessment.success, assessment.associated,
      assessment.address, assessment.reg)) sniffer::increment(deliveryCounters.suppressed);
  else if (verbose && !rawOutput) {
    // Admit the transaction and its evidence together. Dropping just FAULT could
    // otherwise leave the PC believing a damaged register selector was explicit.
    const unsigned reserve = sniffer::TransactionOutput::LINE_CAPACITY + 192 +
        (assessment.success ? 0 : chainCount * 3600);
    if (usbOutput.bytes() + reserve > USB_CAPACITY) {
      unsigned lines = 1;
      if (!assessment.success) {
        ++lines;
        for (unsigned i = 0; i < chainCount; ++i) lines += 1 + (chainStateCounts[i] ? 1 + (chainStateCounts[i] + 23) / 24 : 0);
      }
      rejectedLines += lines; traceDrops += lines;
      sniffer::increment(deliveryCounters.outputRejected);
      chainCount = 0; chainOverflow = false; return;
    }
    const auto emit = [](const char *line) { if (!serialLine(line)) ++traceDrops; };
    uint8_t corrections = 0;
    for (unsigned i = 0; i < chainCount; ++i) corrections |= chainCorrections[i];
    if (corrections) {
      char line[96];
      snprintf(line, sizeof(line), "QUALIFIED,txn=%lu,clock=%u,boundary=%u\n",
          static_cast<unsigned long>(transactionNumber), bool(corrections & qualification::Clock),
          bool(corrections & qualification::Boundary));
      serialLine(line);
    }
    if (!assessment.success) {
      char line[192];
      snprintf(line, sizeof(line), "FAULT,txn=%lu,association=%s,interrupted=%u,chain_overflow=%u,segments=%u\n",
          static_cast<unsigned long>(transactionNumber), assessment.associated ? "explicit" : "unassociated",
          reason != nullptr, chainOverflow, chainCount);
      serialLine(line);
    }
    transactions = sniffer::TransactionOutput{};
    for (unsigned i = 0; i < chainCount; ++i) transactions.feed(chain[i], emit);
    if (reason || chainOverflow) transactions.interrupt(chainOverflow ? "chain overflow" : reason, emit);
    if (!assessment.success) {
      char line[384];
      for (unsigned s = 0; s < chainCount; ++s) {
        const auto &f = chain[s];
        size_t used = snprintf(line, sizeof(line), "FAULT_SEG,txn=%lu,segment=%u,frame=%lu,boundary=%s,bytes=%u,errors=%02X,flags=",
            static_cast<unsigned long>(transactionNumber), s, static_cast<unsigned long>(chainNumbers[s]),
            f.end == sniffer::Boundary::Stop ? "P" : "Sr", f.size, f.errors);
        for (unsigned bit = 0; bit < 5; ++bit) if (f.errors & (1u << bit))
          used += snprintf(line + used, sizeof(line) - used, "%s|", sniffer::errorName(bit));
        const auto segment = sniffer::assess(&f, 1);
        bool earlyReadNack = false;
        if (f.size && (f.bytes[0] & 1))
          for (unsigned j = 1; j + 1 < f.size; ++j) if (!f.ack[j]) earlyReadNack = true;
        snprintf(line + used, sizeof(line) - used, "addr_nack=%u|data_nack=%u|read_ack_fault=%u|read_early_nack=%u|missing_final_nack=%u\n",
            segment.addressNack, segment.dataNack, segment.readAckFault, earlyReadNack,
            f.size > 1 && (f.bytes[0] & 1) && f.ack[f.size - 1]);
        serialLine(line);
        if (chainStateCounts[s]) {
          used = snprintf(line, sizeof(line), "FAULT_EDGE,txn=%lu,frame=%lu,states=",
              static_cast<unsigned long>(transactionNumber), static_cast<unsigned long>(chainNumbers[s]));
          for (unsigned j = 0; j < chainStateCounts[s]; ++j) line[used++] = '0' + chainStates[s][j];
          line[used++] = '\n'; line[used] = 0; serialLine(line);
          timingTrace(transactionNumber, chainNumbers[s], chainDurations[s], chainStateCounts[s], chainTraceTruncated[s]);
        }
      }
    }
  }
  chainCount = 0; chainOverflow = false;
}

void process(const capture::Packet &packet) {
  if (packet.epoch != lastEpoch) {
    finishChain("capture gap");
    deliveryCounters.gap();
#if !SNIFFER_HEADLESS
    observer.interrupt(recordTransaction);
#endif
    memset(pending, 0, sizeof(pending));
    lastEpoch = packet.epoch;
  }
  sniffer::Frame f = packet.frame;
  sniffer::enforcePayloadLimit(f);
  if (rawOutput) {
    if (f.errors != packet.frame.errors) { capture::Packet corrected = packet; corrected.frame = f; trace(corrected); }
    else trace(packet);
  } // Raw mode is explicitly unfiltered.
  if (chainCount < CHAIN_CAPACITY && !chainOverflow) {
    if(!chainCount){chainTiming=recorder_wire::Timing{};chainTiming.valid=true;chainTiming.epoch=packet.epoch;chainTiming.hz=packet.pio_Hz;}
    chainTiming.start[chainCount]=packet.start_cycles;chainTiming.end[chainCount]=packet.end_cycles;
    chainTiming.valid &= packet.timingValid && packet.epoch==chainTiming.epoch && packet.pio_Hz==chainTiming.hz;
    chain[chainCount] = f; chainNumbers[chainCount] = packet.number;
    chainStateCounts[chainCount] = packet.stateCount;
    memcpy(chainStates[chainCount], packet.states, packet.stateCount);
    memcpy(chainDurations[chainCount], packet.delta_cycles, packet.stateCount * sizeof(uint64_t));
    chainTraceTruncated[chainCount] = packet.traceTruncated;
    chainCorrections[chainCount] = packet.corrections;
    ++chainCount;
  } else chainOverflow = true;
  if (f.end == sniffer::Boundary::Stop) finishChain();
#if !SNIFFER_HEADLESS
  observer.feed(f, recordTransaction);
#endif
  if (f.invalid) { memset(pending, 0, sizeof(pending)); return; }
  if (!f.size) return;
  const uint8_t address = f.bytes[0] >> 1;
  if (address <= 7 || address >= 0x78) return; // Raw trace still retains reserved/10-bit headers.
  if (!f.ack[0]) { pending[address].valid = false; return; }
  if (!(f.bytes[0] & 1)) {
    pending[address].valid = false;
    if (sniffer::acceptedCommand(f)) {
      pending[address] = Pending{true, f.bytes[1]};
      addDiscovery(Discovery{address, f.bytes[1], 0, 0, true});
    }
  } else if (f.size > 1) {
    const bool associated = pending[address].valid;
    addDiscovery(Discovery{address, pending[address].command, 1, 0, associated});
    const uint8_t length = sniffer::blockCandidateLength(f);
    if (associated && length) addDiscovery(Discovery{address, pending[address].command, 2, length, true});
  }
}

void printTiming(bool details = false) {
  const auto q = capture::qualificationStats();
  char qline[192];
  snprintf(qline, sizeof(qline), "QUAL,enabled=%u,threshold_ns=150,threshold_cycles=%lu,scl_suppressed=%lu,sda_boundary_suppressed=%lu,overflow=%lu,corrected_frames=%lu\n",
      q.enabled, static_cast<unsigned long>(q.threshold_cycles), static_cast<unsigned long>(q.clockSuppressed),
      static_cast<unsigned long>(q.boundarySuppressed), static_cast<unsigned long>(q.overflows),
      static_cast<unsigned long>(q.correctedFrames));
  serialLine(qline);
  const auto s = capture::timingStats();
  const auto *v = s.value;
  char line[512], clock[32], age[24];
  if (v[timing::ClockHz]) {
    const uint32_t tenths_kHz = (v[timing::ClockHz] + 50) / 100;
    snprintf(clock, sizeof(clock), "%lu.%lu", static_cast<unsigned long>(tenths_kHz / 10), static_cast<unsigned long>(tenths_kHz % 10));
  } else strcpy(clock, "unknown");
  if (v[timing::Samples]) snprintf(age, sizeof(age), "%lu", static_cast<unsigned long>(millis() - v[timing::LastSampleMs]));
  else strcpy(age, "unknown");
  snprintf(line, sizeof(line), "TIMING,clock_kHz=%s,samples=%lu,age_ms=%s,active=%u,pio_hz=%lu,resolution_ns=%lu,max_blind_ns=%lu,method=valid_frame_median\n",
      clock, static_cast<unsigned long>(v[timing::Samples]), age, capture::active(),
      static_cast<unsigned long>(v[timing::PioHz]), static_cast<unsigned long>(v[timing::ResolutionNs]), static_cast<unsigned long>(v[timing::BlindNs]));
  serialLine(line);
  if (!details) return;
  snprintf(line, sizeof(line), "TIMING_RANGE,high_min_ns=%lu,high_median_ns=%lu,high_max_ns=%lu,low_min_ns=%lu,low_median_ns=%lu,low_max_us=%lu,period_min_ns=%lu,period_median_ns=%lu,period_max_us=%lu\n",
      static_cast<unsigned long>(v[timing::HighMinNs]), static_cast<unsigned long>(v[timing::HighMedianNs]), static_cast<unsigned long>(v[timing::HighMaxNs]),
      static_cast<unsigned long>(v[timing::LowMinNs]), static_cast<unsigned long>(v[timing::LowMedianNs]), static_cast<unsigned long>(v[timing::LowMaxUs]),
      static_cast<unsigned long>(v[timing::PeriodMinNs]), static_cast<unsigned long>(v[timing::PeriodMedianNs]), static_cast<unsigned long>(v[timing::PeriodMaxUs]));
  serialLine(line);
  snprintf(line, sizeof(line), "TIMING_FLAGS,short_high_lt250ns=%lu,short_low_lt250ns=%lu,long_low_gt5x_baseline=%lu,invalid_frames=%lu,heartbeats=%lu,gaps=%lu,window=128,filter=%s\n",
      static_cast<unsigned long>(v[timing::ShortHigh]), static_cast<unsigned long>(v[timing::ShortLow]), static_cast<unsigned long>(v[timing::LongLow]),
      static_cast<unsigned long>(v[timing::InvalidFrames]), static_cast<unsigned long>(v[timing::Heartbeats]), static_cast<unsigned long>(v[timing::Gaps]), q.enabled ? "150ns" : "none");
  serialLine(line);
}

void printDiagnostics() {
  const auto s = capture::stats();
  char line[512];
  snprintf(line, sizeof(line),
      "DIAG,armed=%u,scl_edges=%lu,sda_edges=%lu,starts=%lu,stops=%lu,bytes=%lu,frames=%lu,bad=%lu,ring=%lu,fifo=%lu,queue=%lu,trace_drop=%lu,new_drop=%lu,fault=%u\n",
      s.active, static_cast<unsigned long>(s.sclEdges), static_cast<unsigned long>(s.sdaEdges),
      static_cast<unsigned long>(s.starts), static_cast<unsigned long>(s.stops), static_cast<unsigned long>(s.bytes),
      static_cast<unsigned long>(s.frames), static_cast<unsigned long>(s.invalid),
      static_cast<unsigned long>(s.ringOverruns), static_cast<unsigned long>(s.fifoStalls),
      static_cast<unsigned long>(s.queueDrops), static_cast<unsigned long>(traceDrops),
      static_cast<unsigned long>(outputDrops), s.fault);
  serialLine(line);
  snprintf(line, sizeof(line), "USB,queued_bytes=%u,queued_lines=%u,rejected_lines=%lu,disconnect_drop=%lu\n",
      static_cast<unsigned>(usbOutput.bytes()), static_cast<unsigned>(usbOutput.lines()),
      static_cast<unsigned long>(rejectedLines),
      static_cast<unsigned long>(disconnectedDrops));
  serialLine(line);
  snprintf(line, sizeof(line), "CAPTURE,duplicate_states=%lu,bad_words=%lu\n",
      static_cast<unsigned long>(s.duplicateStates), static_cast<unsigned long>(s.badWords));
  serialLine(line);
#if !SNIFFER_HEADLESS
  snprintf(line, sizeof(line), "UI,unique=%u,entries=%u,unique_full=%lu,save_pending=%u,save_failed=%u\n",
      uniqueMode, uniqueTransactions.size(), static_cast<unsigned long>(uniqueTransactions.rejected()),
      settingsPending, settingsFailed);
  serialLine(line);
  snprintf(line, sizeof(line), "UI,display_buffer=%u,heap_free_bytes=%d,core0_stack_free_bytes=%d\n",
      displayBufferReady, rp2040.getFreeHeap(), rp2040.getFreeStack());
  serialLine(line);
  snprintf(line, sizeof(line), "UI,tft_spi_port=%u,tft_spi_mode=%u,tft_sck=%u,tft_mosi=%u,tft_pinmux_ok=%u\n",
      TFT_SPI_PORT, TFT_SPI_MODE, TFT_SCLK, TFT_MOSI,
      gpio_get_function(TFT_SCLK) == GPIO_FUNC_SPI && gpio_get_function(TFT_MOSI) == GPIO_FUNC_SPI);
  serialLine(line);
#else
  snprintf(line, sizeof(line), "SYSTEM,headless=1,sda_gpio=%u,scl_gpio=%u,heap_free_bytes=%d,core0_stack_free_bytes=%d\n",
      I2C_SNIFF_SDA_PIN, I2C_SNIFF_SCL_PIN, rp2040.getFreeHeap(), rp2040.getFreeStack());
  serialLine(line);
  snprintf(line, sizeof(line), "SYSTEM,uart_ready=%u,uart_queued=%u,uart_drop=%lu,uart_rx_bad=%lu,uart_suppressed=%lu,uart_rx_overflow=%lu\n",
      uartReady, unsigned(uartClient.bytes()), static_cast<unsigned long>(uartClient.dropped),
      static_cast<unsigned long>(uartClient.malformed), static_cast<unsigned long>(uartClient.suppressed),
      static_cast<unsigned long>(uartRxOverflows));
  serialLine(line);
  snprintf(line,sizeof(line),"UART_V6,diag_epoch=%lu,capacity=%u,queue_peak=%lu,sent=%lu,fallbacks=%lu,phase=%u,failure=%u,target_baud=%lu,attempt_baud=%lu\n",
      (unsigned long)diagnosticEpoch,unsigned(uartClient.capacity()),(unsigned long)uartClient.highWater,
      (unsigned long)uartClient.sentBytes,(unsigned long)uartRate.fallbackCount,unsigned(uartRate.phase),unsigned(uartRate.failure),
      (unsigned long)uartRate.lastTarget,(unsigned long)uartRate.lastActual);
  serialLine(line);
  snprintf(line, sizeof(line), "UART_RX,bytes=%lu,reject=%s,wire_bytes=%u,decoded_bytes=%u,baud=%lu,tx_pinmux=%u,rx_pinmux=%u\n",
      static_cast<unsigned long>(uartClient.receivedBytes), uartClient.rejectedReason,
      unsigned(uartClient.rejectedWireBytes), unsigned(uartClient.rejectedDecodedBytes),
      static_cast<unsigned long>(Serial1.getActualBaud()),
      gpio_get_function(0) == GPIO_FUNC_UART, gpio_get_function(1) == GPIO_FUNC_UART);
  serialLine(line);
  size_t hexUsed = snprintf(line, sizeof(line), "UART_RX_REJECT_HEX,");
  for (size_t i = 0; i < uartClient.rejectedPrefixBytes; ++i) {
    hexUsed += snprintf(line + hexUsed, sizeof(line) - hexUsed, "%02X%s", uartClient.rejectedPrefix[i],
        i + 1 == uartClient.rejectedPrefixBytes ? "" : " ");
  }
  line[hexUsed++] = '\n'; line[hexUsed] = 0;
  serialLine(line);
#endif
  printTiming(true);
}

void requestedReport(bool diagnostics) {
  // Core 0 is the only USB producer. Reserve the complete bounded response so
  // markers cannot surround a truncated report or interleaved transactions.
  if (USB_CAPACITY - usbOutput.bytes() < 4096) {
    serialLine("ERROR,report_queue_full\n"); return;
  }
  serialLine(diagnostics ? "REPORT_BEGIN,kind=D\n" : "REPORT_BEGIN,kind=T\n");
  if (diagnostics) printDiagnostics(); else printTiming(true);
  serialLine(diagnostics ? "REPORT_END,kind=D\n" : "REPORT_END,kind=T\n");
}

void saveScreenMode() {
#if !SNIFFER_HEADLESS
  if (!settingsPending || settingsFailed || capture::active()) return;
  uint8_t record[sniffer::ScreenSettings::SIZE];
  sniffer::ScreenSettings::encode(uniqueMode, record);
  for (unsigned i = 0; i < sizeof(record); ++i) EEPROM.write(i, record[i]);
  if (EEPROM.commit()) {
    settingsPending = false;
    settingsValid = true;
    storedUniqueMode = uniqueMode;
    serialLine("UI,mode_saved\n");
  } else { settingsFailed = true; serialLine("ERROR,screen_mode_save_failed\n"); }
  dirty = true;
#endif
}

void toggleScreenMode() {
#if !SNIFFER_HEADLESS
  uniqueMode = !uniqueMode;
  settingsPending = !settingsValid || uniqueMode != storedUniqueMode;
  settingsFailed = false;
  screenPage = 0; page_ms = millis(); forceScreen = dirty = true;
  serialLine(uniqueMode ? "UI,unique\n" : "UI,recent\n");
  saveScreenMode();
#else
  serialLine("ERROR,no_local_display\n");
#endif
}

void setArmed(bool enable, bool publishStatus=true) {
#if ACTIVE_READ_AVAILABLE
  if (activeJob.busy() || scanBatch.running || readBatch.running) { serialLine("ERROR,active_read_busy\n");return; }
#endif
  if (enable && (snapshotPending || commitPending)) { serialLine("ERROR,control_busy\n"); return; }
  if (!captureReady) return;
  capture::request(enable);
  const uint32_t began_ms = millis();
  while (capture::active() != enable && millis() - began_ms < 100) delay(1);
  armed = capture::active();
  if (armed != enable) serialLine("ERROR,capture_request_failed\n");
  solidLedShown = false;
  dirty = true;
  serialLine(armed ? "ARMED\n" : "IDLE\n");
  if (!armed) saveScreenMode();
#if SNIFFER_HEADLESS
  if (publishStatus) notifyClients();
#endif
}

void clearAndStop(bool publishStatus=true) {
  if (snapshotPending || commitPending) { serialLine("ERROR,control_busy\n"); return; }
  setArmed(false,publishStatus);
  if (capture::active()) return;
  capture::resetStats();
  memset(seen, 0, sizeof(seen));
  memset(seenUnassociatedRead, 0, sizeof(seenUnassociatedRead));
  memset(pending, 0, sizeof(pending));
  finishChain("capture stopped");
  deliveryCounters.clear();
  transactions = sniffer::TransactionOutput{};
#if !SNIFFER_HEADLESS
  observer.reset();
  recentTransactions.clear(); uniqueTransactions.clear();
  screenPage = 0; page_ms = millis(); forceScreen = true;
#endif
  disconnectedDrops = 0;
  rejectedLines = 0;
  historyCount = scrollTop = traceDrops = outputDrops = 0;
  dirty = true;
  serialLine("CLEAR\n");
#if SNIFFER_HEADLESS
  if (publishStatus) notifyClients();
#endif
}

#if SNIFFER_HEADLESS
template<size_t N> void clientStatus(link::ClientT<N> &client) {
  const uint32_t pendingBatch=uint32_t(client.batchBytes);
  client.flushBatch();
  const auto s = capture::stats();
  const auto t = capture::timingStats();
  uint8_t p[200] = {};
  p[0] = capture::active(); p[1] = s.fault; p[2] = exportSession;
  p[3] = client.subscribed;
  const uint32_t values[] = {s.epoch, transactionNumber, s.frames, s.invalid,
      s.ringOverruns, s.fifoStalls, s.queueDrops, client.dropped,
      client.malformed, client.suppressed, uint32_t(client.bytes())};
  for (unsigned i = 0; i < 11; ++i) link::put32(p + 4 + 4 * i, values[i]);
  for (unsigned i = 0; i < timing::Count; ++i) link::put32(p + 48 + i * 4, t.value[i]);
  size_t length=48+timing::Count*4;
  if (static_cast<const void *>(&client)==static_cast<const void *>(&uartClient)) {
    const uint32_t transport[]={15u|(ACTIVE_READ_AVAILABLE?496u:0u),static_cast<uint32_t>(Serial1.getActualBaud()),client.highWater,client.sentBytes,
        uartRate.fallbackCount,pendingBatch,client.sourceFilter.generation,client.transportOmitted,client.transactionDropped,client.discardedBytes};
    for (unsigned i=0;i<10;++i) link::put32(p+132+i*4,transport[i]);
    const uint32_t details[]={diagnosticEpoch,uint32_t(client.capacity()),uint32_t(uartRate.phase),
        uint32_t(uartRate.failure),uartRate.attempts,uartRate.lastTarget,uartRate.lastActual};
    for (unsigned i=0;i<7;++i) link::put32(p+172+i*4,details[i]);
    length=200;
  }
  client.send(link::Status, p, length);
}
void notifyClients() {
  if (uartReady && uartRate.phase!=transport5::Phase::Drain && uartRate.phase!=transport5::Phase::Quiet) clientStatus(uartClient);
  if (structuredUsb) clientStatus(usbClient);
}
template<size_t N> void clientCommand(link::ClientT<N> &client, uint32_t request, const uint8_t *p, size_t n) {
  client.flushBatch();
  const uint8_t op = n ? p[0] : 0;
  uint8_t result = 0;
  const bool isUart=static_cast<const void *>(&client)==static_cast<const void *>(&uartClient);
  if (!n) result = 1;
#if ACTIVE_READ_AVAILABLE
  else if(op==22 && n>=10 && p[1]==0xa5 && link::get32(p+2) && isUart){
    if(!active_read::ReadBatch<active_read::Hardware>::valid(p,n)){client.response(request,op,1);return;}
    const auto admission=activeGate.admit(link::get32(p+2),request,activeJob.busy() || scanBatch.running || readBatch.running,activeReplyPending,
      captureReady && !exportSession && !staging && !snapshotPending && !commitPending && !uartRate.changing());
    if(admission==active_read::Admission::Replay)activeReplyPending=true;
    else if(admission==active_read::Admission::Reject)result=2;
    else{activeFormat=6;activeGapMade=false;readBatch.start(request,time_us_32(),capture::active(),p,n);}
  }
  else if (op==19 && n==8 && p[1]==0xa5 && link::get32(p+2) && p[6]>=8 && p[7]<=119 && p[6]<=p[7] && isUart) {
    const auto admission=activeGate.admit(link::get32(p+2),request,activeJob.busy() || scanBatch.running || readBatch.running,activeReplyPending,
      captureReady && !exportSession && !staging && !snapshotPending && !commitPending && !uartRate.changing());
    if (admission==active_read::Admission::Replay) activeReplyPending=true;
    else if (admission==active_read::Admission::Reject) result=2;
    else { activeFormat=4;activeGapMade=false;scanBatch.start(request,time_us_32(),capture::active(),p[6],p[7]); }
  }
  else if ((op==16 || op==17 || op==18 || op==21) && n>=7 && p[1]==0xa5 && link::get32(p+2) && isUart) {
    uint16_t bytes=0;const bool valid=active_read::experimentalRequest(p,n,bytes);
    if (!valid) { client.response(request,op,1);return; }
    const auto admission=activeGate.admit(link::get32(p+2),request,activeJob.busy() || scanBatch.running || readBatch.running,activeReplyPending,
      captureReady && !exportSession && !staging && !snapshotPending && !commitPending && !uartRate.changing());
    if (admission==active_read::Admission::Replay) activeReplyPending=true;
    else if (admission==active_read::Admission::Reject) result=2;
    else {
      activeFormat=op==21?5:3;activeGapMade=false;activeHardware.address=p[6];
      activeHardware.rawMode=op==16?1:op==17?2:op==21?3:0;activeHardware.writeSize=op==16?bytes:0;
      activeHardware.readSize=op==21?bytes:0;
      if (op==16) memcpy(activeHardware.writeData,p+9,bytes);
      activeJob.transferTimeout_us=500000;
      const uint8_t placeholder=(op==18 || op==21)?p[7]:0;activeJob.startPlan(request,time_us_32(),capture::active(),&placeholder,1);
    }
  }
  else if (((op==13 && n==6) || (op==15 && n==11)) && p[1]==0xa5 && link::get32(p+2) && isUart) {
    bool valid=op==13 || (p[6]>=8 && p[6]<=119 && p[7]>=1 && p[7]<=3);
    if (valid && op==15) for (unsigned i=1;i<p[7];++i) valid&=unsigned(p[8+i-1])+1==p[8+i];
    if (valid && op==15) for (unsigned i=p[7];i<3;++i) valid&=p[8+i]==0;
    if (!valid) { client.response(request,op,1); return; }
    const auto admission=activeGate.admit(link::get32(p+2),request,activeJob.busy() || scanBatch.running || readBatch.running,activeReplyPending,
        captureReady && !exportSession && !staging && !snapshotPending && !commitPending && !uartRate.changing());
    if (admission==active_read::Admission::Replay) activeReplyPending=true;
    else if (admission==active_read::Admission::Reject) result=2;
    else {
      activeReply[27]=0;
      activeGapMade=false;
      activeFormat=op==15?2:1;
      activeHardware.rawMode=0;activeHardware.writeSize=0;activeJob.transferTimeout_us=50000;
      activeHardware.address=op==15?p[6]:0x75;
      const uint8_t legacy[]={0x50,0x51};
      activeJob.startPlan(request,time_us_32(),capture::active(),op==15?p+8:legacy,op==15?p[7]:2);
    }
  }
  else if (op==14 && n==1 && isUart) { if(readBatch.running)readBatch.cancel(time_us_32());else if(scanBatch.running)scanBatch.cancel(time_us_32());else activeJob.cancel=true; }
  else if ((activeJob.busy() || scanBatch.running || readBatch.running) && op!=4 && op!=12) result=2;
#endif
  else if (op == 4 && n == 1) { clientStatus(client); }
  else if (op == 5 && n == 2 && p[1] <= 1) client.subscribed = p[1];
  else if(op==20 && n==2 && p[1]<=1){client.flushBatch();client.timed=bool(p[1]);}
  else if (op==9 && n==1 && isUart) {
    if (exportSession || staging || snapshotPending || commitPending || uartRate.changing()) result=2;
    else {
      clearAndStop(false);
      if (capture::active()) result=3;
      else {
        uartClient.clearDiagnostics(); usbClient.clearDiagnostics(); uartRate.clearDiagnostics();
        uartRate.lastActual=Serial1.getActualBaud();
        uartRxOverflows=0; transactionNumber=0; ++diagnosticEpoch;
        clientStatus(client);
        if (structuredUsb) clientStatus(usbClient);
      }
    }
  }
  else if (op==8 && isUart) {
    if (!client.sourceFilter.configure(p,n)) result=1;
    else client.compact=bool(p[1]&8);
  } else if (op==10 && n==5 && isUart) {
    if (!uartRate.request(request,link::get32(p+1),millis(),client.subscribed)) result=2;
    else { client.transportPaused=true; client.subscribed=false; }
  } else if (op==11 && n==5 && isUart) {
    if (!uartRate.confirm(link::get32(p+1),millis())) result=2;
    else { client.transportPaused=false; client.subscribed=uartRate.wasSubscribed; }
  } else if (op==12 && n==1 && isUart) uartRate.keepalive(millis());
  else if (op == 6 && n >= 3 && p[1] <= 1 && p[2] <= 2 &&
      n == (p[1] ? 4099u : 3u)) {
    client.sourceFilter.enabled=false;
    client.filter.scope = p[1] ? sniffer::Scope::Selected : sniffer::Scope::All;
    client.filter.outcome = sniffer::Outcome(p[2]);
    if (p[1]) memcpy(client.filter.registers, p + 3, 4096);
  } else if (op == 7 && n == 2 && p[1] <= 1) {
    if (capture::active() || exportSession) result = 2;
    else if (!capture::setQualification(p[1])) result = 3;
    else notifyClients();
  } else if (op >= 1 && op <= 3 && n == 1) {
    if (exportSession || staging || snapshotPending || commitPending) result = 2;
    else {
      if (op == 3) clearAndStop(); else setArmed(op == 1);
      if (capture::active() != (op == 1)) result = 3;
    }
  } else result = 1; // Includes unsupported raw export and unknown commands.
  client.response(request, op, result);
}
void serviceClients() {
  uartClient.tick(millis());
  if (uartRate.expired(millis())) {
#if ACTIVE_READ_AVAILABLE
    activeJob.cancel=true;
    scanBatch.cancel(time_us_32());
    readBatch.cancel(time_us_32());
#endif
    const auto why=uartRate.phase==transport5::Phase::Drain?transport5::RateFailure::DrainTimeout:
        uartRate.phase==transport5::Phase::Leased?transport5::RateFailure::LeaseTimeout:transport5::RateFailure::ConfirmTimeout;
    uartClient.discardOutput(); uartClient.subscribed=false; uartClient.transportPaused=false;
    Serial1.end(); Serial1.begin(115200); uartRate.fallback(why);
    clientStatus(uartClient);
  }
  if (switchUsbPending && !usbOutput.bytes()) {
    structuredUsb = true; switchUsbPending = false; clientStatus(usbClient);
  }
  if (uartReady) {
    static uint32_t beacon_ms=0;
    if (uartRate.ready(millis())) { beacon_ms=millis(); clientStatus(uartClient); }
    if (uartRate.phase==transport5::Phase::Confirm && uint32_t(millis()-beacon_ms)>=100) {
      beacon_ms=millis(); clientStatus(uartClient);
    }
    if (Serial1.overflow()) { sniffer::increment(uartRxOverflows); uartClient.inputGap(); }
    for (unsigned i = 0; i < 128 && Serial1.available(); ++i)
      uartClient.receive(uint8_t(Serial1.read()), [](uint32_t id, const uint8_t *p, size_t n) { clientCommand(uartClient, id, p, n); });
    // GP0/GP1 and successful Serial1.setTX(0)/setRX(1) verify UART0 binding.
    // This core reports availableForWrite as 0/1, NOT free FIFO bytes. Single
    // core-0 TX ownership permits the SDK's checked DR write with no wait loop.
    uartClient.drainFifo([] { return uart_is_writable(uart0); },
        [](uint8_t byte) { uart_get_hw(uart0)->dr=byte; });
    if (uartRate.phase==transport5::Phase::Drain && !uartClient.bytes()) {
      Serial1.flush(); // At most the remaining hardware FIFO; no transaction-sized blocking write.
      Serial1.end(); Serial1.begin(uartRate.target);
      const uint32_t actual=Serial1.getActualBaud();
      uartRate.lastActual=actual;
      const uint32_t error=actual>uartRate.target?actual-uartRate.target:uartRate.target-actual;
      if (uint64_t(error)*100>uartRate.target) {
        Serial1.end(); Serial1.begin(115200); uartRate.fallback(transport5::RateFailure::BaudMismatch); uartClient.transportPaused=false;
      } else { uartClient.resetInput(); uartRate.switched(millis()); }
    }
  }
  if (structuredUsb) {
    for (unsigned i = 0; i < 128 && Serial.available(); ++i)
      usbClient.receive(uint8_t(Serial.read()), [](uint32_t id, const uint8_t *p, size_t n) { clientCommand(usbClient, id, p, n); });
    size_t amount=usbClient.contiguous(); const int available=Serial.availableForWrite();
    if (Serial && available>0 && amount) {
      if (amount>size_t(available)) amount=size_t(available);
      if (amount>256) amount=256;
      usbClient.consume(Serial.write(usbClient.frontData(),amount));
    }
  }
  static uint32_t report_ms = 0;
  static bool lastActive = false;
  const bool active = capture::active();
  if (active != lastActive || millis() - report_ms >= 1000) {
    lastActive = active; report_ms = millis(); notifyClients();
  }
}
#endif

void capabilities() {
#if SNIFFER_HEADLESS
  serialLine("CAPS,protocol=1,filter=1,stats=1,errors=1,raw_filter=0,timing=1,report_markers=1,qualification=1,headless=1,structured=1,uart=1\n");
#else
  serialLine("CAPS,protocol=1,filter=1,stats=1,errors=1,raw_filter=0,timing=1,report_markers=1,qualification=1\n");
#endif
}

void command(const char *input) {
  char name[16], arg[80], extra;
  unsigned long id = 0;
  char reply[192];
  const auto reject = [&](const char *reason) {
    if (!commitPending) staging = false;
    snprintf(reply, sizeof(reply), "NAK,id=%lu,reason=%s,generation=%lu\n", id, reason,
        static_cast<unsigned long>(filterGeneration)); serialLine(reply);
  };
  if (sscanf(input, "%15[^,],%lu", name, &id) != 2 || id > UINT32_MAX) { reject("syntax"); return; }
  if (capture::active() || commitPending || snapshotPending) { reject("pause_or_busy"); return; }
  if (!strcmp(name, "QUALIFY")) {
    if (sscanf(input, "QUALIFY,%lu,%79[^,]%c", &id, arg, &extra) != 2 ||
        (strcmp(arg, "ON") && strcmp(arg, "OFF"))) { reject("qualification_value"); return; }
    if (!capture::setQualification(!strcmp(arg, "ON"))) { reject("qualification_busy"); return; }
    snprintf(reply, sizeof(reply), "ACK,id=%lu,command=QUALIFY,enabled=%u\n", id, !strcmp(arg, "ON"));
    serialLine(reply); return;
  } else if (!strcmp(name, "BEGIN")) {
    char scope[16], outcome[16];
    if (sscanf(input, "BEGIN,%lu,%15[^,],%15[^,]%c", &id, scope, outcome, &extra) != 3) { reject("syntax"); return; }
    if ((strcmp(scope, "All") && strcmp(scope, "Selected")) ||
        (strcmp(outcome, "Both") && strcmp(outcome, "Success") && strcmp(outcome, "Failure"))) { reject("filter_value"); return; }
    if (rawOutput && (strcmp(scope, "All") || strcmp(outcome, "Both"))) { reject("raw_unfiltered_only"); return; }
    memset(stagedFilter.registers, 0, sizeof(stagedFilter.registers));
    stagedFilter.scope = !strcmp(scope, "All") ? sniffer::Scope::All : sniffer::Scope::Selected;
    stagedFilter.outcome = !strcmp(outcome, "Both") ? sniffer::Outcome::Both : !strcmp(outcome, "Success") ? sniffer::Outcome::Success : sniffer::Outcome::Failure;
    stageId = id; stage_ms = millis(); staging = true;
  } else if (!strcmp(name, "MAP")) {
    unsigned address;
    if (!staging || id != stageId) { reject("no_stage"); return; }
    if (sscanf(input, "MAP,%lu,%x,%79[^,]%c", &id, &address, arg, &extra) != 3 || address > 127 || strlen(arg) != 64) { staging = false; reject("map_syntax"); return; }
    uint8_t bytes[32];
    for (unsigned i = 0; i < 64; ++i) if (!isxdigit(static_cast<unsigned char>(arg[i]))) { staging = false; reject("map_hex"); return; }
    for (unsigned i = 0; i < 32; ++i) { char byte[3] = {arg[i*2], arg[i*2+1], 0}; bytes[i] = strtoul(byte, nullptr, 16); }
    memcpy(stagedFilter.registers[address], bytes, 32);
    stage_ms = millis();
  } else if (!strcmp(name, "COMMIT")) {
    if (sscanf(input, "COMMIT,%lu%c", &id, &extra) != 1 || !staging || id != stageId) { reject("no_stage"); return; }
    commitPending = true; return; // Acknowledge only after the old capture queue drains.
  } else if (!strcmp(name, "STATS")) {
    if (sscanf(input, "STATS,%lu%c", &id, &extra) != 1) { reject("syntax"); return; }
    snapshotId = id; snapshotPending = true; snapshotStarted = false; snapshotCursor = 0; return;
  } else if (!strcmp(name, "GET")) {
    if (sscanf(input, "GET,%lu%c", &id, &extra) != 1) { reject("syntax"); return; }
    snprintf(reply, sizeof(reply), "ACK,id=%lu,command=GET,generation=%lu,scope=%s,outcome=%s\n",
        id, static_cast<unsigned long>(filterGeneration), deliveryFilter.scope == sniffer::Scope::All ? "All" : "Selected",
        deliveryFilter.outcome == sniffer::Outcome::Both ? "Both" : deliveryFilter.outcome == sniffer::Outcome::Success ? "Success" : "Failure");
    serialLine(reply); return;
  } else { reject("unknown_command"); return; }
  snprintf(reply, sizeof(reply), "ACK,id=%lu,command=%s\n", id, name); serialLine(reply);
}

void serviceControl() {
  char line[384];
  if (commitPending) {
    deliveryFilter = stagedFilter; staging = commitPending = false; ++filterGeneration;
    snprintf(line, sizeof(line), "ACK,id=%lu,command=COMMIT,generation=%lu,scope=%s,outcome=%s\n",
        static_cast<unsigned long>(stageId), static_cast<unsigned long>(filterGeneration),
        deliveryFilter.scope == sniffer::Scope::All ? "All" : "Selected",
        deliveryFilter.outcome == sniffer::Outcome::Both ? "Both" : deliveryFilter.outcome == sniffer::Outcome::Success ? "Success" : "Failure");
    serialLine(line);
  }
  if (!snapshotPending || usbOutput.bytes() > 12000) return;
  if (!snapshotStarted) {
    snprintf(line, sizeof(line), "STATS_BEGIN,id=%lu,epoch=%lu,rows=%u,total=%lu,failures=%lu,suppressed=%lu,unassociated=%lu,unassociated_fail=%lu,capacity_drop=%lu,chain_overflow=%lu,output_rejected=%lu\n",
        static_cast<unsigned long>(snapshotId), static_cast<unsigned long>(deliveryCounters.epoch), deliveryCounters.size,
        static_cast<unsigned long>(deliveryCounters.total), static_cast<unsigned long>(deliveryCounters.failures),
        static_cast<unsigned long>(deliveryCounters.suppressed), static_cast<unsigned long>(deliveryCounters.unassociated),
        static_cast<unsigned long>(deliveryCounters.unassociatedFail), static_cast<unsigned long>(deliveryCounters.capacityDropped),
        static_cast<unsigned long>(deliveryCounters.chainOverflow), static_cast<unsigned long>(deliveryCounters.outputRejected));
    serialLine(line); snapshotStarted = true; return;
  }
  if (snapshotCursor < deliveryCounters.size) {
    const auto &r = deliveryCounters.rows[snapshotCursor++];
    snprintf(line, sizeof(line), "STATS_ROW,id=%lu,address=%02X,reg=%02X,r_ok=%lu,r_fail=%lu,w_ok=%lu,w_fail=%lu,now=%lu,max=%lu,recent=%08lX,reads=%lu,decode_flags=%02X,ack_flags=%02X\n",
        static_cast<unsigned long>(snapshotId), r.address, r.reg, static_cast<unsigned long>(r.readOk),
        static_cast<unsigned long>(r.readFail), static_cast<unsigned long>(r.writeOk), static_cast<unsigned long>(r.writeFail),
        static_cast<unsigned long>(r.currentFail), static_cast<unsigned long>(r.maxFail),
        static_cast<unsigned long>(r.recent), static_cast<unsigned long>(r.reads), r.decodeFlags, r.ackFlags);
    serialLine(line); return;
  }
  snprintf(line, sizeof(line), "STATS_END,id=%lu,rows=%u,partial_byte=%lu,empty_frame=%lu,ambiguous_setup=%lu,data_after_nack=%lu,frame_overflow=%lu\n",
      static_cast<unsigned long>(snapshotId), deliveryCounters.size,
      static_cast<unsigned long>(deliveryCounters.errors[0]), static_cast<unsigned long>(deliveryCounters.errors[1]),
      static_cast<unsigned long>(deliveryCounters.errors[2]), static_cast<unsigned long>(deliveryCounters.errors[3]),
      static_cast<unsigned long>(deliveryCounters.errors[4]));
  serialLine(line); snapshotPending = false;
}

void receiveCommand(int character) {
  static char line[192];
  static unsigned used = 0;
  static bool inCommand = false, overflow = false;
  if (!inCommand && character == '!') { inCommand = true; used = 0; overflow = false; return; }
  if (inCommand) {
    if (character == '\r') return;
    if (character == '\n') {
      line[used] = 0;
      if (overflow) { staging = false; serialLine("NAK,id=0,reason=line_overflow\n"); }
      else command(line);
      inCommand = false; return;
    }
    if (character < 32 || character > 126 || used + 1 >= sizeof(line)) overflow = true;
    else if (!overflow) line[used++] = character;
    return;
  }
  switch (character) {
#if SNIFFER_HEADLESS
    case 'z':
      if (capture::active() || exportSession || staging || snapshotPending || commitPending)
        serialLine("ERROR,structured_requires_idle\n");
      else { serialLine("PROTO,structured=1,version=1\n"); switchUsbPending = true; }
      break;
#endif
    case '[':
    case ']':
      if (!capture::setQualification(character == ']')) serialLine("ERROR,qualification_requires_idle\n");
      else printTiming(true);
      break;
    case 'b': beginExport(); break;
    case 'h': capabilities(); break;
    case 'v': verbose = true; rawOutput = false; break;
    case 'r':
      if (deliveryFilter.scope != sniffer::Scope::All || deliveryFilter.outcome != sniffer::Outcome::Both) serialLine("ERROR,raw_requires_All_Both\n");
      else { verbose = rawOutput = true; } break;
    case 'q': verbose = false; break;
    case 'a': setArmed(true); break;
    case 'i': setArmed(false); break;
    case 'c': if (!snapshotPending && !commitPending) clearAndStop(); else serialLine("ERROR,control_busy\n"); break;
    case 'd': requestedReport(true); break;
    case 't': requestedReport(false); break;
  }
}

void drawDisplay() {
#if !SNIFFER_HEADLESS
  if (!displayBufferReady) return;
  const auto s = capture::stats();
  const bool captureFault = !captureReady || s.fault;
  constexpr unsigned ROWS = 8, ROW_Y = 62, ROW_H = 28;
  static uint32_t shownRevision[ROWS] = {};
  static unsigned shownKey[ROWS] = {}, shownPhase[ROWS] = {};
  static char shownHeader[64] = {}, shownFooter[64] = {};
  static char shownTiming[32] = {};
  static uint32_t timingDisplay_ms = 0;
  const uint32_t now_ms = millis();
  const unsigned count = uniqueMode ? uniqueTransactions.size() : recentTransactions.size();
  const unsigned pages = max(1u, (count + ROWS - 1) / ROWS);
  if (screenPage >= pages) { screenPage = 0; forceScreen = true; }
  if (now_ms - page_ms >= 5000) {
    page_ms = now_ms;
    if (pages > 1) { screenPage = (screenPage + 1) % pages; forceScreen = true; }
  }
  char line[64], header[64];
  const char sdaLevel = gpio_get(I2C_SNIFF_SDA_PIN) ? 'H' : 'L';
  const char sclLevel = gpio_get(I2C_SNIFF_SCL_PIN) ? 'H' : 'L';
  displayStrip.setTextWrap(false);
  snprintf(header, sizeof(header), "%u %u %u %u %u %c %c %u %u",
      uniqueMode, s.active, captureFault, settingsPending, settingsFailed,
      sdaLevel, sclLevel,
      count, uniqueTransactions.rejected() != 0);
  if (forceScreen || strcmp(header, shownHeader)) {
    strcpy(shownHeader, header);
    displayStrip.fillSprite(TFT_BLACK);
    displayStrip.setTextSize(2); displayStrip.setCursor(4, 3);
    displayStrip.setTextColor(uniqueMode ? TFT_CYAN : TFT_WHITE, TFT_BLACK);
    displayStrip.print(uniqueMode ? "UNIQUE" : "RECENT");
    displayStrip.setTextSize(1); displayStrip.setCursor(122, 7);
    displayStrip.setTextColor(captureFault ? TFT_RED : s.active ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    displayStrip.print(captureFault ? "FAULT" : s.active ? "LIVE" : "PAUSED");
    displayStrip.setCursor(4, 25); displayStrip.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    snprintf(line, sizeof(line), "SDA:%c SCL:%c %u/%u", sdaLevel,
        sclLevel, count, uniqueMode ? 64 : 32);
    displayStrip.print(line);
    if (settingsPending) { displayStrip.setCursor(91, 7); displayStrip.setTextColor(TFT_YELLOW, TFT_BLACK); displayStrip.print(settingsFailed ? "ERR" : "*"); }
    if (uniqueMode && uniqueTransactions.rejected()) { displayStrip.setCursor(146, 25); displayStrip.setTextColor(TFT_RED, TFT_BLACK); displayStrip.print("FULL"); }
    displayStrip.drawFastHLine(0, 40, tft.width(), TFT_DARKGREY);
    displayStrip.pushSprite(0, 0, 0, 0, tft.width(), 44);
  }
  if (forceScreen || millis() - timingDisplay_ms >= 500) {
    timingDisplay_ms = millis();
    const auto ts = capture::timingStats();
    const uint32_t frequency_Hz = ts.value[timing::ClockHz];
    const bool fresh = s.active && ts.value[timing::Samples] && millis() - ts.value[timing::LastSampleMs] < 2000;
    char label[24], key[32];
    if (frequency_Hz) {
      const uint32_t tenths_kHz = (frequency_Hz + 50) / 100;
      snprintf(label, sizeof(label), "CLK:%lu.%lukHz", static_cast<unsigned long>(tenths_kHz / 10), static_cast<unsigned long>(tenths_kHz % 10));
    } else strcpy(label, "CLK:--");
    snprintf(key, sizeof(key), "%s:%u", label, fresh);
    if (forceScreen || strcmp(key, shownTiming)) {
      strcpy(shownTiming, key);
      displayStrip.fillSprite(TFT_BLACK);
      displayStrip.setTextSize(2); displayStrip.setCursor(4, 0);
      displayStrip.setTextColor(fresh ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
      displayStrip.print(label);
      displayStrip.drawFastHLine(0, 17, tft.width(), TFT_DARKGREY);
      displayStrip.pushSprite(0, 44, 0, 0, tft.width(), 18);
    }
  }
  for (unsigned row = 0; row < ROWS; ++row) {
    const unsigned index = screenPage * ROWS + row;
    if (uniqueMode && index < count) uniqueTransactions.acknowledge(index, now_ms);
    const sniffer::Entry *entry = index < count ? &(uniqueMode ? uniqueTransactions.at(index) : recentTransactions.at(index)) : nullptr;
    const unsigned phase = sniffer::highlightPhase(entry, uniqueMode, now_ms);
    const bool flash = phase == 15;
    const unsigned key = entry ? sniffer::sortKey(entry->value) : 0xFFFFFFFFu;
    const uint32_t revision = entry ? entry->revision : 0;
    if (!forceScreen && shownRevision[row] == revision && shownKey[row] == key && shownPhase[row] == phase) continue;
    shownRevision[row] = revision; shownKey[row] = key; shownPhase[row] = phase;
    const int y = ROW_Y + row * ROW_H;
    const uint16_t accent = entry && entry->change == sniffer::Change::Changed ? TFT_CYAN : TFT_GREEN;
    const uint16_t background = flash ? accent : phase ?
        tft.color565(0, 24 + phase * 3, entry && entry->change == sniffer::Change::Changed ? 24 + phase * 3 : 0) : TFT_BLACK;
    displayStrip.fillSprite(background);
    if (!entry) {
      displayStrip.drawFastHLine(4, ROW_H - 1, tft.width() - 8, TFT_DARKGREY);
      displayStrip.pushSprite(0, y, 0, 0, tft.width(), ROW_H);
      serviceUsb();
      continue;
    }
    if (phase && !flash) {
      const unsigned strength = phase * 255 / 12;
      const uint16_t fade = tft.color565(0, strength, entry->change == sniffer::Change::Changed ? strength : 0);
      displayStrip.fillRect(0, 0, 3, ROW_H - 1, fade);
      displayStrip.drawRect(0, 0, tft.width(), ROW_H - 1, fade);
    }
    const auto &value = entry->value;
    char address[3] = "--", reg[3] = "--";
    if (value.hasAddress) snprintf(address, sizeof(address), "%02X", value.address);
    if (value.hasRegister) snprintf(reg, sizeof(reg), "%02X", value.reg);
    snprintf(line, sizeof(line), "%s %s %c", address, reg,
        value.operation == sniffer::Operation::Read ? 'R' : value.operation == sniffer::Operation::Write ? 'W' : 'P');
    displayStrip.setTextSize(2); displayStrip.setCursor(6, 1);
    displayStrip.setTextColor(flash ? TFT_BLACK : TFT_WHITE, background); displayStrip.print(line);
    displayStrip.setTextSize(1); displayStrip.setCursor(137, 5);
    displayStrip.setTextColor(flash ? TFT_BLACK : value.result ? TFT_RED : TFT_GREEN, background);
    displayStrip.print(value.result ? "FAIL" : "OK");
    size_t used = 0;
    const unsigned preview = min<unsigned>(6, value.length);
    for (unsigned i = 0; i < preview; ++i)
      used += snprintf(line + used, sizeof(line) - used, "%s%02X", i ? " " : "", value.payload[i]);
    if (!preview) used = snprintf(line, sizeof(line), "--");
    if (value.length > preview) snprintf(line + used, sizeof(line) - used, " +%u", value.length - preview);
    displayStrip.setCursor(6, 18); displayStrip.setTextColor(flash ? TFT_BLACK : TFT_LIGHTGREY, background); displayStrip.print(line);
    displayStrip.pushSprite(0, y, 0, 0, tft.width(), ROW_H);
    serviceUsb();
  }
  const uint32_t faults = s.invalid + s.ringOverruns + s.fifoStalls + s.queueDrops;
  const uint32_t outputLoss = rejectedLines + disconnectedDrops;
  snprintf(line, sizeof(line), "%u/%u   E:%lu%s U:%lu%s", screenPage + 1, pages,
      static_cast<unsigned long>(min<uint32_t>(9999, faults)), faults > 9999 ? "+" : "",
      static_cast<unsigned long>(min<uint32_t>(9999, outputLoss)), outputLoss > 9999 ? "+" : "");
  if (!captureReady) snprintf(line, sizeof(line), "%s", capture::error());
  if (forceScreen || strcmp(line, shownFooter)) {
    strcpy(shownFooter, line); displayStrip.fillSprite(TFT_BLACK);
    displayStrip.setTextSize(1); displayStrip.setTextColor(TFT_LIGHTGREY, TFT_BLACK); displayStrip.setCursor(4, 6); displayStrip.print(line);
    displayStrip.pushSprite(0, 300, 0, 0, tft.width(), 20);
  }
  forceScreen = false;
#endif
}

void buttons() {
  static sniffer::ButtonGestures gestures;
  static uint32_t poll_ms = 0;
  const uint32_t now_ms = millis();
  if (now_ms - poll_ms < 20) return;
  poll_ms = now_ms;
#if SNIFFER_HEADLESS
  const bool pressed = digitalRead(SNIFFER_BUTTON_PIN) == LOW;
#else
  const bool pressed = BOOTSEL;
#endif
  switch (gestures.update(pressed, now_ms)) {
    case sniffer::Gesture::Short: setArmed(!capture::active()); break;
    case sniffer::Gesture::Double:
#if !SNIFFER_HEADLESS
      toggleScreenMode();
#endif
      break;
    case sniffer::Gesture::Long: clearAndStop(); break;
    case sniffer::Gesture::None: break;
  }
}

void led() {
#if SNIFFER_HEADLESS
  static uint32_t lastToggle_ms = 0;
  static bool initialized = false, lastArmed = false, illuminated = false;
  const uint32_t now_ms = millis();
  const uint32_t halfPeriod_ms = armed ? 125 : 1000;
  if (!initialized || armed != lastArmed) {
    initialized = true;
    lastArmed = armed;
    illuminated = true;
  } else if (now_ms - lastToggle_ms >= halfPeriod_ms) {
    illuminated = !illuminated;
  } else {
    return;
  }
  lastToggle_ms = now_ms;
  digitalWrite(SNIFFER_LED_PIN,
      (illuminated != bool(SNIFFER_LED_ACTIVE_LOW)) ? HIGH : LOW);
#else
  static uint32_t last_ms = 0;
  if (armed) {
    if (!solidLedShown) { statusPixel.setPixelColor(0, statusPixel.Color(0, 32, 0)); statusPixel.show(); solidLedShown = true; }
    return;
  }
  const uint32_t now_ms = millis();
  if (now_ms - last_ms < 20) return;
  last_ms = now_ms;
  const uint32_t phase_ms = now_ms % 3600;
  const uint32_t ramp = phase_ms < 1800 ? phase_ms : 3600 - phase_ms;
  statusPixel.setPixelColor(0, statusPixel.Color(1 + ramp * 127 / 1800, 1 + ramp * 71 / 1800, 1 + ramp * 23 / 1800));
  statusPixel.show();
  solidLedShown = false;
#endif
}
} // namespace

void setup1() {}
void loop1() { capture::service(); }

void setup() {
  Serial.begin(115200);
#if SNIFFER_HEADLESS
  uartReady = Serial1.setTX(0) && Serial1.setRX(1) && Serial1.setFIFOSize(1024);
  if (uartReady) Serial1.begin(115200);
#endif
  delay(1500);
  pinMode(I2C_SNIFF_SDA_PIN, INPUT);
  pinMode(I2C_SNIFF_SCL_PIN, INPUT);
  gpio_disable_pulls(I2C_SNIFF_SDA_PIN);
  gpio_disable_pulls(I2C_SNIFF_SCL_PIN);
  // Capture owns PIO1 (29 instructions), RGB uses PIO0 (4 instructions).
  // TFT uses hardware SPI1, not a third PIO program.
  captureReady = capture::init();
#if SNIFFER_HEADLESS
  pinMode(SNIFFER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(SNIFFER_LED_PIN, OUTPUT);
  digitalWrite(SNIFFER_LED_PIN, SNIFFER_LED_ACTIVE_LOW ? HIGH : LOW);
  serialLine("SYSTEM,headless=1,board=weact_rp2350b_core\n");
#else
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);
  if (!statusPixel.begin()) serialLine("ERROR,status_led_init_failed\n");
  statusPixel.setBrightness(255);
  // Visible even before display initialization completes; loop() takes over
  // with the usual idle breathing or armed color.
  statusPixel.setPixelColor(0, statusPixel.Color(64, 24, 12));
  statusPixel.show();
  // Configure the instance TFT_eSPI actually uses, not Arduino's global SPI.
  // This also keeps non-default pins explicit if the library constructor changes.
  // In TFT_eSPI 2.5.43's Arduino-Pico backend this base reference is backed
  // by its SPIClassRP2040 object (Processors/TFT_eSPI_RP2040.c).
  auto &displaySpi = static_cast<SPIClassRP2040 &>(tft.getSPIinstance());
  displaySpi.setSCK(TFT_SCLK);
  displaySpi.setTX(TFT_MOSI);
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  displayStrip.setColorDepth(16);
  displayBufferReady = displayStrip.createSprite(tft.width(), 44) != nullptr;
  if (!displayBufferReady) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(4, 4);
    tft.print("Display buffer failed");
    serialLine("ERROR,display_buffer_allocation_failed\n");
  }
  EEPROM.begin(256);
  uint8_t settings[sniffer::ScreenSettings::SIZE];
  for (unsigned i = 0; i < sizeof(settings); ++i) settings[i] = EEPROM.read(i);
  settingsValid = sniffer::ScreenSettings::decode(settings, uniqueMode);
  storedUniqueMode = uniqueMode;
  char memoryLine[128];
  snprintf(memoryLine, sizeof(memoryLine), "UI,display_buffer=%u,heap_free_bytes=%d,core0_stack_free_bytes=%d\n",
      displayBufferReady, rp2040.getFreeHeap(), rp2040.getFreeStack());
  serialLine(memoryLine);
#endif
  if (!captureReady) {
    char line[96]; snprintf(line, sizeof(line), "ERROR,capture_init,%s\n", capture::error()); serialLine(line);
  }
  drawDisplay();
}

void loop() {
#if SNIFFER_HEADLESS
  serviceClients();
#if ACTIVE_READ_AVAILABLE
  serviceActive();
#endif
#endif
  if (exportSession) { serviceExport(); return; }
  serviceUsb();
  if (staging && !commitPending && millis() - stage_ms > 10000) staging = false;
  for (unsigned i = 0; i < 32 && Serial.available(); ++i) {
#if SNIFFER_HEADLESS
    if (structuredUsb || switchUsbPending) break;
#endif
    const int commandByte=Serial.read();
#if ACTIVE_READ_AVAILABLE
    if (!activeJob.busy() && !scanBatch.running && !readBatch.running)
#endif
      receiveCommand(commandByte);
    if (exportSession) return;
  }
  unsigned processed = 0;
  for (; processed < 32 && capture::pop(workingPacket); ++processed) {
    process(workingPacket);
    serviceUsb(); // Drain between large frames, not just after a 32-frame burst.
#if SNIFFER_HEADLESS
    serviceClients();
#endif
  }
  if (processed < 32 && !capture::active()) {
#if ACTIVE_READ_AVAILABLE
    if ((activeJob.busy() || scanBatch.running || readBatch.running) && !activeGapMade) {
      // Intentional acquisition boundary, not evidence of a DUT decode fault.
      chainCount=0;chainOverflow=false;memset(pending,0,sizeof(pending));
      deliveryCounters.gap();activeGapMade=true;
    }
#endif
    finishChain("capture stopped");
#if !SNIFFER_HEADLESS
    observer.interrupt(recordTransaction);
#endif
    serviceControl();
  }
#if ACTIVE_READ_AVAILABLE
  if (!activeJob.busy() && !scanBatch.running && !readBatch.running)
#endif
    buttons();
  if (armed && !capture::active()) { armed = false; solidLedShown = false; dirty = true; }
  led();
  saveScreenMode();
  static uint32_t display_ms = 0, diag_ms = 0;
  const uint32_t now_ms = millis();
  if (now_ms - diag_ms >= 1000) { if (armed && verbose) printDiagnostics(); diag_ms = now_ms; dirty = true; }
  if (now_ms - display_ms >= 50) { drawDisplay(); dirty = false; display_ms = now_ms; }
  serviceUsb();
}
