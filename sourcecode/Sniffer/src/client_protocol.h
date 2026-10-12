#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "delivery.h"
#include "transport_v5.h"
#include "recorder_wire.h"

namespace link {
constexpr size_t MAX_PAYLOAD = 4608, MAX_RAW = MAX_PAYLOAD + 14;
constexpr size_t MAX_WIRE = MAX_RAW + MAX_RAW / 254 + 2;
enum Type : uint8_t { Command = 1, Response = 2, Status = 3, Transaction = 4, Batch = 5, ActiveResult = 6, TimedTransaction=7, ReplayReadResult=8 };
inline void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
inline void put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = v >> (8 * i); }
inline uint16_t get16(const uint8_t *p) { return p[0] | (uint16_t(p[1]) << 8); }
inline uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t(get16(p + 2)) << 16); }
inline uint32_t crc(const uint8_t *p, size_t n) {
  uint32_t v = 0xffffffff;
  while (n--) { v ^= *p++; for (unsigned i = 0; i < 8; ++i) v = (v >> 1) ^ ((v & 1) ? 0xedb88320 : 0); }
  return ~v;
}
inline size_t encode(const uint8_t *p, size_t n, uint8_t *out) {
  size_t pos = 1, codePos = 0; uint8_t code = 1;
  for (size_t i = 0; i < n; ++i) {
    if (!p[i]) { out[codePos] = code; codePos = pos++; code = 1; }
    else { out[pos++] = p[i]; if (++code == 255) { out[codePos] = code; codePos = pos++; code = 1; } }
  }
  out[codePos] = code; out[pos++] = 0; return pos;
}
inline size_t decode(const uint8_t *p, size_t n, uint8_t *out) {
  size_t i = 0, used = 0;
  while (i < n) {
    const unsigned code = p[i++];
    if (!code || code - 1 > n - i || used + code - 1 + (code != 255 && i + code - 1 < n) > MAX_RAW) return 0;
    for (unsigned j = 1; j < code; ++j) out[used++] = p[i++];
    if (code != 255 && i < n) out[used++] = 0;
  }
  return used;
}
template<size_t CAPACITY = 16384> class ClientT {
 public:
  sniffer::DeliveryFilter filter;
  uint32_t dropped = 0, malformed = 0, suppressed = 0, sequence = 0;
  uint32_t receivedBytes = 0;
  uint8_t rejectedPrefix[64] = {};
  size_t rejectedWireBytes = 0, rejectedDecodedBytes = 0, rejectedPrefixBytes = 0;
  const char *rejectedReason = "none";
  bool subscribed = false;
  bool compact=false, transportPaused=false;
  bool timed=false;
  transport5::Filter sourceFilter;
  uint32_t highWater=0, sentBytes=0, transportOmitted=0, transactionDropped=0, discardedBytes=0;
  uint8_t compactRecord[transport5::MaxRecord]={}, batch[transport5::MaxBatch]={};
  size_t batchBytes=0;
  unsigned batchCount=0;
  uint32_t batchAt_ms=0;
  uint8_t payload[MAX_PAYLOAD] = {};
  static constexpr size_t capacity() { return CAPACITY; }
  void clearDiagnostics() {
    dropped=malformed=suppressed=receivedBytes=sentBytes=transportOmitted=transactionDropped=discardedBytes=0;
    highWater=uint32_t(queued); sourceFilter.generation=0;
    rejectedReason="none"; rejectedWireBytes=rejectedDecodedBytes=rejectedPrefixBytes=0;
    memset(rejectedPrefix,0,sizeof(rejectedPrefix));
    // Keep sequence, RX framing, queue and subscription: Clear is not a reconnect.
  }
  void inputGap() { rxUsed = 0; overflow = true; }
  void resetInput() { rxUsed=0; overflow=false; }
  template<class Handler> void receive(uint8_t byte, Handler handler) {
    uint8_t *raw=received; // Outbound replies/batch flushing must not overwrite this command.
    sniffer::increment(receivedBytes);
    if (byte) {
      if (rxUsed < sizeof(rx)) rx[rxUsed++] = byte; else overflow = true;
      return;
    }
    if (!rxUsed && !overflow) return;
    const size_t n = overflow ? 0 : decode(rx, rxUsed, raw);
    const char *reason = nullptr;
    if (overflow) reason = "input_gap_or_overflow";
    else if (!n) reason = "cobs";
    else if (n < 14) reason = "short_header";
    else if (raw[0] != 'I' || raw[1] != 'S') reason = "magic";
    else if (raw[2] != 1) reason = "version";
    else if (raw[3] != Command) reason = "packet_type";
    else if (get16(raw + 8) != n - 14) reason = "length";
    else if (crc(raw, n - 4) != get32(raw + n - 4)) reason = "crc";
    if (reason) {
      rejectedReason = reason;
      rejectedWireBytes = rxUsed;
      rejectedDecodedBytes = n;
      rejectedPrefixBytes = rxUsed < sizeof(rejectedPrefix) ? rxUsed : sizeof(rejectedPrefix);
      memcpy(rejectedPrefix, rx, rejectedPrefixBytes);
    }
    rxUsed = 0; overflow = false;
    if (reason) { sniffer::increment(malformed); return; }
    handler(get32(raw + 4), raw + 10, n - 14);
  }
  bool send(Type type, const uint8_t *p, size_t n) {
    const uint32_t seq = sequence++;
    const size_t needed = n + 14 + (n + 14) / 254 + 2;
    if (n > MAX_PAYLOAD || free() < needed || ((type == Transaction || type==Batch || type==TimedTransaction) && free() < MAX_WIRE + 512)) {
      sniffer::increment(dropped); return false;
    }
    raw[0] = 'I'; raw[1] = 'S'; raw[2] = 1; raw[3] = type;
    put32(raw + 4, seq); put16(raw + 8, uint16_t(n));
    memcpy(raw + 10, p, n); put32(raw + 10 + n, crc(raw, 10 + n));
    const size_t used = encode(raw, n + 14, wire);
    for (size_t i = 0; i < used; ++i) { queue[head] = wire[i]; head = (head + 1) % CAPACITY; }
    queued += used; if (queued>highWater) highWater=uint32_t(queued); return true;
  }
  void flushBatch() {
    if (!batchBytes) return;
    if (!send(Batch,batch,batchBytes)) for (unsigned i=0;i<batchCount;++i) sniffer::increment(transactionDropped);
    batchBytes=0; batchCount=0;
  }
  void tick(uint32_t now_ms) { if (batchBytes && uint32_t(now_ms-batchAt_ms)>=2) flushBatch(); }
  void response(uint32_t request, uint8_t op, uint8_t result) {
    uint8_t p[6]; put32(p, request); p[4] = op; p[5] = result; send(Response, p, sizeof(p));
  }
  void transaction(uint32_t id, const sniffer::Assessment &a, const sniffer::Frame *f, unsigned count, bool interrupted,uint32_t now_ms=0,const recorder_wire::Timing *timing=nullptr) {
    if (transportPaused) { sniffer::increment(transportOmitted); return; }
    if (!subscribed) return;
    uint8_t address=a.address; char direction=a.read?'R':'W';
    if (!a.associated) {
      direction='?';
      for (unsigned i=0;i<count;++i) if (f[i].size) { address=f[i].bytes[0]>>1; direction=f[i].bytes[0]&1?'R':'W'; break; }
    }
    if (sourceFilter.enabled ? !sourceFilter.accepts(a.success,a.associated,address,a.reg,direction) :
        !filter.accepts(a.success,a.associated,a.address,a.reg)) { sniffer::increment(suppressed); return; }
    put32(payload, id); payload[4] = a.success; payload[5] = a.associated;
    payload[6] = a.address; payload[7] = a.reg; payload[8] = count; payload[9] = interrupted;
    size_t n = 10;
    for (unsigned s = 0; s < count; ++s) {
      if (f[s].size > sniffer::MAX_BYTES || n + 5 + 2 * f[s].size > MAX_PAYLOAD) { sniffer::increment(dropped); ++sequence; return; }
      put16(payload + n, f[s].size); n += 2;
      payload[n++] = uint8_t(f[s].end); payload[n++] = f[s].invalid; payload[n++] = f[s].errors;
      for (unsigned i = 0; i < f[s].size; ++i) { payload[n++] = f[s].bytes[i]; payload[n++] = f[s].ack[i]; }
    }
    if (timed && timing && count==timing->count) {
      flushBatch();
      const size_t header=recorder_wire::Header+16*count;
      const size_t packed=transport5::pack(payload,n,compactRecord,sizeof(compactRecord));
      if(!packed || header+packed>MAX_PAYLOAD){sniffer::increment(transactionDropped);++sequence;return;}
      payload[0]=1;payload[1]=timing->valid;payload[2]=count;payload[3]=0;
      put32(payload+4,timing->epoch);put32(payload+8,timing->hz);
      for(unsigned i=0;i<count;++i){recorder_wire::put64(payload+12+16*i,timing->start[i]);recorder_wire::put64(payload+20+16*i,timing->end[i]);}
      memcpy(payload+header,compactRecord,packed);
      if(!send(TimedTransaction,payload,header+packed))sniffer::increment(transactionDropped);
      return;
    }
    if (!compact) { if (!send(Transaction,payload,n)) sniffer::increment(transactionDropped); return; }
    const size_t packed=transport5::pack(payload,n,compactRecord,sizeof(compactRecord));
    if (!packed) { sniffer::increment(dropped); sniffer::increment(transactionDropped); ++sequence; return; }
    if (batchBytes && (batchBytes+2+packed>1024 || uint32_t(now_ms-batchAt_ms)>=2)) flushBatch();
    if (!batchBytes) batchAt_ms=now_ms;
    put16(batch+batchBytes,uint16_t(packed)); memcpy(batch+batchBytes+2,compactRecord,packed);
    batchBytes+=2+packed; ++batchCount;
    if (batchBytes>=1024) flushBatch();
  }
  size_t bytes() const { return queued; }
  uint8_t front() const { return queue[tail]; }
  void consume() { if (queued) { tail = (tail + 1) % CAPACITY; --queued; } }
  const uint8_t *frontData() const { return queue+tail; }
  size_t contiguous() const { return queued<CAPACITY-tail?queued:CAPACITY-tail; }
  void consume(size_t n) { while (n-- && queued) { consume(); sniffer::increment(sentBytes); } }
  template<class Ready,class Put> void drainFifo(Ready ready,Put put,size_t budget=128) {
    while (budget && queued) {
      const size_t n=contiguous()<budget?contiguous():budget;
      size_t sent=0; const uint8_t *p=frontData();
      while (sent<n && ready()) put(p[sent++]);
      consume(sent); budget-=sent;
      if (sent<n) break;
    }
  }
  void discardOutput() {
    uint64_t lost=uint64_t(discardedBytes)+queued+batchBytes;
    discardedBytes=lost>UINT32_MAX?UINT32_MAX:uint32_t(lost);
    head=tail=queued=0; batchBytes=0; batchCount=0; inputGap();
  }
 private:
  size_t free() const { return CAPACITY - queued; }
  uint8_t raw[MAX_RAW] = {}, received[MAX_RAW]={}, wire[MAX_WIRE] = {}, rx[MAX_WIRE] = {}, queue[CAPACITY] = {};
  size_t rxUsed = 0, head = 0, tail = 0, queued = 0;
  bool overflow = false;
};
using Client = ClientT<>;
using UartClient = ClientT<65536>;
} // namespace link
