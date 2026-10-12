#pragma once
#include <stdint.h>
#include <string.h>
#include "i2c_decoder.h"

namespace sniffer {
enum class Operation : uint8_t { Write, Read, Probe };
enum Result : uint8_t { Success = 0, AddressNack = 1, DataNack = 2,
                       DecodeFault = 4, Incomplete = 8, ReadAckFault = 16 };
struct Transaction {
  uint8_t address = 0, reg = 0, result = Success, decodeErrors = 0;
  bool hasRegister = false, hasAddress = false;
  Operation operation = Operation::Probe;
  uint16_t length = 0;
  uint8_t payload[MAX_PAYLOAD_BYTES] = {};
};

inline bool sameKey(const Transaction &a, const Transaction &b) {
  return a.address == b.address && a.hasRegister == b.hasRegister &&
      (!a.hasRegister || a.reg == b.reg);
}
inline bool sameValue(const Transaction &a, const Transaction &b) {
  return a.operation == b.operation && a.result == b.result && a.decodeErrors == b.decodeErrors && a.length == b.length &&
      !memcmp(a.payload, b.payload, a.length);
}
inline unsigned sortKey(const Transaction &t) {
  return static_cast<unsigned>(t.address) * 257 + (t.hasRegister ? t.reg : 256);
}
enum class Change : uint8_t { None, Added, Changed, Full };
struct Entry {
  Transaction value;
  uint32_t revision = 0, changed_ms = 0, flash_ms = 0;
  Change change = Change::None;
  bool unseen = true;
};
constexpr uint32_t HIGHLIGHT_MS = 2500, FLASH_MS = 350, FLASH_COOLDOWN_MS = 600;
inline unsigned highlightPhase(const Entry *entry, bool unique, uint32_t now_ms) {
  if (!unique || !entry || now_ms - entry->changed_ms >= HIGHLIGHT_MS) return 0;
  if (now_ms - entry->flash_ms < FLASH_MS) return 15;
  return 12 - (now_ms - entry->changed_ms) * 12 / HIGHLIGHT_MS;
}

template <unsigned Capacity> class UniqueTransactions {
 public:
  unsigned size() const { return size_; }
  const Entry &at(unsigned index) const { return entries_[index]; }
  uint32_t rejected() const { return rejected_; }
  Change update(const Transaction &value, uint32_t now_ms) {
    unsigned position = 0;
    while (position < size_ && sortKey(entries_[position].value) < sortKey(value)) ++position;
    if (position < size_ && sameKey(entries_[position].value, value)) {
      Entry &entry = entries_[position];
      if (sameValue(entry.value, value)) return Change::None;
      entry.value = value;
      entry.revision = ++revision_;
      entry.changed_ms = now_ms;
      entry.change = Change::Changed;
      entry.unseen = true;
      return Change::Changed;
    }
    if (size_ == Capacity) { ++rejected_; return Change::Full; }
    memmove(entries_ + position + 1, entries_ + position, (size_ - position) * sizeof(Entry));
    entries_[position] = Entry{value, ++revision_, now_ms, now_ms, Change::Added};
    ++size_;
    return Change::Added;
  }
  void clear() { size_ = 0; rejected_ = 0; ++revision_; }
  void acknowledge(unsigned index, uint32_t now_ms) {
    Entry &entry = entries_[index];
    if (entry.unseen) {
      // Start the visual lifetime when the row is actually visible, including
      // changes received off-page. Throttle flashes on rapidly changing values.
      entry.changed_ms = now_ms;
      if (entry.change == Change::Added || now_ms - entry.flash_ms >= FLASH_COOLDOWN_MS)
        entry.flash_ms = now_ms;
    }
    entry.unseen = false;
  }
 private:
  Entry entries_[Capacity] = {};
  unsigned size_ = 0;
  uint32_t revision_ = 0, rejected_ = 0;
};

template <unsigned Capacity> class RecentTransactions {
 public:
  unsigned size() const { return size_; }
  const Entry &at(unsigned newestFirstIndex) const {
    return entries_[(next_ + Capacity - 1 - newestFirstIndex) % Capacity];
  }
  void add(const Transaction &value, uint32_t now_ms) {
    entries_[next_] = Entry{value, ++revision_, now_ms, now_ms, Change::Added};
    next_ = (next_ + 1) % Capacity;
    if (size_ < Capacity) ++size_;
  }
  void clear() { size_ = next_ = 0; ++revision_; }
 private:
  Entry entries_[Capacity] = {};
  unsigned size_ = 0, next_ = 0;
  uint32_t revision_ = 0;
};
} // namespace sniffer
