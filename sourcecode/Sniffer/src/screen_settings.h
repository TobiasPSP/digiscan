#pragma once
#include <stdint.h>

namespace sniffer {
// Four-byte versioned record; invalid/erased records select the normal screen.
struct ScreenSettings {
  static constexpr unsigned SIZE = 4;
  static bool decode(const uint8_t *record, bool &unique) {
    if (record[0] != 0x53 || record[1] != 1 || record[2] > 1 ||
        record[3] != static_cast<uint8_t>(record[0] ^ record[1] ^ record[2] ^ 0xA7)) {
      unique = false; return false;
    }
    unique = record[2] != 0; return true;
  }
  static void encode(bool unique, uint8_t *record) {
    record[0] = 0x53; record[1] = 1; record[2] = unique ? 1 : 0;
    record[3] = static_cast<uint8_t>(record[0] ^ record[1] ^ record[2] ^ 0xA7);
  }
};
} // namespace sniffer
