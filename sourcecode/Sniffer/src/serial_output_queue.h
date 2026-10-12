#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace sniffer {
// Core-0-only queue: admit entire lines, transmit incrementally without interleaving.
template <size_t Capacity> class SerialOutputQueue {
 public:
  bool enqueue(const char *line) {
    const size_t length = strlen(line);
    if (!length || line[length - 1] != '\n' || length > Capacity - used_) return false;
    const size_t first = length < Capacity - head_ ? length : Capacity - head_;
    memcpy(data_ + head_, line, first);
    memcpy(data_, line + first, length - first);
    head_ = (head_ + length) % Capacity;
    used_ += length;
    for (size_t i = 0; i < length; ++i) if (line[i] == '\n') ++lines_;
    return true;
  }
  const uint8_t *front() const { return data_ + tail_; }
  size_t contiguous() const { return used_ < Capacity - tail_ ? used_ : Capacity - tail_; }
  size_t bytes() const { return used_; }
  size_t lines() const { return lines_; }
  bool consume(size_t count) {
    if (count > contiguous()) return false;
    for (size_t i = 0; i < count; ++i) if (data_[tail_ + i] == '\n') --lines_;
    tail_ = (tail_ + count) % Capacity;
    used_ -= count;
    return true;
  }
  size_t clear() {
    const size_t discarded = lines_;
    head_ = tail_ = used_ = lines_ = 0;
    return discarded;
  }
 private:
  uint8_t data_[Capacity] = {};
  size_t head_ = 0, tail_ = 0, used_ = 0, lines_ = 0;
};
} // namespace sniffer
