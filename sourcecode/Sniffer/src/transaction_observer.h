#pragma once
#include "transaction_model.h"

namespace sniffer {
class TransactionObserver {
 public:
  template <typename Sink> void feed(const Frame &frame, Sink sink) {
    const bool read = frame.size && (frame.bytes[0] & 1);
    const uint8_t address = frame.size ? frame.bytes[0] >> 1 : 0;
    const bool ordinaryAddress = address > 7 && address < 0x78;
    const bool paired = pending_ && read && address == pointer_.address;
    if (pending_ && !paired) sink(pointer_);
    if (ordinaryAddress && !read && frame.size == 2 && frame.end == Boundary::Restart && acceptedCommand(frame)) {
      pointer_ = Transaction{};
      pointer_.address = address; pointer_.reg = frame.bytes[1];
      pointer_.hasAddress = pointer_.hasRegister = true;
      pointer_.operation = Operation::Write;
      pending_ = true;
      return;
    }
    Transaction value;
    value.address = address;
    value.hasAddress = frame.size != 0;
    value.operation = read ? Operation::Read : frame.size > 1 ? Operation::Write : Operation::Probe;
    if (paired) { value.reg = pointer_.reg; value.hasRegister = true; }
    else if (ordinaryAddress && !read && frame.size >= 2 && !frame.invalid) {
      value.reg = frame.bytes[1]; value.hasRegister = true;
    }
    if (frame.invalid || !frame.size) value.result |= DecodeFault;
    value.decodeErrors = frame.errors;
    if (frame.size && !frame.ack[0]) value.result |= AddressNack;
    const unsigned first = read ? 1 : 2;
    for (unsigned i = first; i < frame.size; ++i) {
      if (value.length < MAX_PAYLOAD_BYTES) value.payload[value.length++] = frame.bytes[i];
      else value.result |= DecodeFault;
    }
    for (unsigned i = 1; i < frame.size; ++i) {
      if (!read && !frame.ack[i]) value.result |= DataNack;
      if (read && i + 1 < frame.size && !frame.ack[i]) value.result |= ReadAckFault;
    }
    if (read && frame.size > 1 && frame.ack[frame.size - 1]) value.result |= ReadAckFault;
    pending_ = false;
    sink(value);
  }
  template <typename Sink> void interrupt(Sink sink) {
    if (pending_) { pointer_.result |= Incomplete; sink(pointer_); pending_ = false; }
  }
  void reset() { pending_ = false; }
 private:
  Transaction pointer_;
  bool pending_ = false;
};
} // namespace sniffer
