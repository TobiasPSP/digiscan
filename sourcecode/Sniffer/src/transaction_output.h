#pragma once
#include <stdio.h>
#include <stdarg.h>
#include "i2c_decoder.h"

namespace sniffer {
// Assemble only adjacent repeated-START segments, never stale register pointers.
class TransactionOutput {
 public:
  static constexpr unsigned LINE_CAPACITY = 2048;
  static constexpr unsigned DETAIL_CAPACITY = 1920;
  template <typename Sink> void feed(const Frame &f, Sink sink) {
    const bool read = f.size && (f.bytes[0] & 1);
    const unsigned address = f.size ? f.bytes[0] >> 1 : 0;
    const bool paired = pointer_ && read && address == address_;
    if (pointer_) {
      if (!paired) append("0x%02X Reg 0x%02X Write", address_, reg_);
      pointer_ = false;
    }
    if (!paired && used_) append(" ; ");
    if (f.invalid || !f.size) failed_ = true;
    if (f.size) {
      if (!f.ack[0]) { failed_ = true; addressNack_ = true; }
      for (unsigned i = 1; i < f.size; ++i) {
        if (!read && !f.ack[i]) { failed_ = true; dataNack_ = true; }
        if (read && i + 1 < f.size && !f.ack[i]) failed_ = true;
      }
      if (read && f.size > 1 && f.ack[f.size - 1]) {
        failed_ = true; missingFinalNack_ = true;
      }
      if (!read && f.size == 2 && f.end == Boundary::Restart && !f.invalid && f.ack[0] && f.ack[1]) {
        pointer_ = true; address_ = address; reg_ = f.bytes[1];
      } else {
        if (paired) append("0x%02X Reg 0x%02X Read", address, reg_);
        else if (!read && f.size > 1) append("0x%02X Reg 0x%02X Write", address, f.bytes[1]);
        else append("0x%02X %s", address, read ? "Read" : "Probe");
        for (unsigned i = read ? 1 : 2; i < f.size; ++i) append(" %02X", f.bytes[i]);
        if (read && f.size == 2) append(" Dec %u", f.bytes[1]);
        else if (read && f.size == 3) append(" Dec LE=%u BE=%u",
            static_cast<unsigned>(f.bytes[1]) | (static_cast<unsigned>(f.bytes[2]) << 8),
            (static_cast<unsigned>(f.bytes[1]) << 8) | f.bytes[2]);
        else if (read && f.size > 3) {
          append(" Dec[");
          for (unsigned i = 1; i < f.size; ++i) append("%s%u", i == 1 ? "" : " ", f.bytes[i]);
          append("]");
        }
      }
    } else append("Undecodable");
    errors_ |= f.errors;
    if (f.end == Boundary::Stop) finish(sink);
  }

  template <typename Sink> void interrupt(const char *reason, Sink sink) {
    if (!used_ && !pointer_) return;
    failed_ = true;
    if (pointer_) { append("0x%02X Reg 0x%02X Write", address_, reg_); pointer_ = false; }
    append(" [%s]", reason);
    finish(sink);
  }

 private:
  char line_[LINE_CAPACITY] = {};
  unsigned used_ = 0, address_ = 0, reg_ = 0, errors_ = 0;
  bool pointer_ = false, failed_ = false, truncated_ = false;
  bool addressNack_ = false, dataNack_ = false, missingFinalNack_ = false;

  void append(const char *format, ...) {
    // Reserve space for the result and error annotations, even on oversized chains.
    constexpr unsigned limit = DETAIL_CAPACITY;
    if (used_ >= limit) { truncated_ = true; return; }
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(line_ + used_, limit - used_ + 1, format, args);
    va_end(args);
    if (n < 0) { failed_ = true; return; }
    if (static_cast<unsigned>(n) > limit - used_) {
      used_ = limit; truncated_ = true;
    } else used_ += n;
  }

  template <typename Sink> void finish(Sink sink) {
    if (pointer_) { append("0x%02X Reg 0x%02X Write", address_, reg_); pointer_ = false; }
    snprintf(line_ + used_, sizeof(line_) - used_, "%s%s%s%s%s%s\n",
        failed_ ? " FAIL" : truncated_ ? " UNKNOWN" : " OK", addressNack_ ? " (address NACK)" : "",
        dataNack_ ? " (data NACK)" : "", missingFinalNack_ ? " (missing final NACK)" : "",
        errors_ ? " (decode error)" : "", truncated_ ? " [OUTPUT_TRUNCATED]" : "");
    sink(line_);
    used_ = address_ = reg_ = errors_ = 0;
    pointer_ = failed_ = truncated_ = false;
    addressNack_ = dataNack_ = missingFinalNack_ = false;
    line_[0] = '\0';
  }
};
} // namespace sniffer
