#pragma once
#include <stdint.h>
#include <stddef.h>

namespace recording {
constexpr uint32_t MAGIC = 0x58433249; // I2CX, little endian
constexpr unsigned MAX_EVENTS = 32;
constexpr unsigned MAX_PAYLOAD = 8 + MAX_EVENTS * 5;
constexpr unsigned MAX_PACKET = 16 + MAX_PAYLOAD + 4;
enum Type : uint8_t { Begin = 1, Data = 2, End = 3 };
enum Gap : uint32_t { Ring = 1, Fifo = 2, BadWord = 4, Dma = 8 };
struct Event { uint64_t sequence; uint32_t iterations, state; };
struct Summary {
  uint64_t generated = 0, queued = 0, dropped = 0;
  uint32_t gaps = 0, ring = 0, fifo = 0, bad = 0, dma = 0, terminalWords = 0;
};
inline void put32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) p[i] = v >> (8 * i);
}
inline void put64(uint8_t *p, uint64_t v) {
  put32(p, uint32_t(v)); put32(p + 4, uint32_t(v >> 32));
}
inline uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t crc = UINT32_MAX;
  while (n--) {
    crc ^= *p++;
    for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
  }
  return ~crc;
}
inline size_t seal(uint8_t *p, Type type, uint32_t sequence, uint32_t payloadBytes) {
  put32(p, MAGIC); p[4] = 1; p[5] = type; p[6] = p[7] = 0;
  put32(p + 8, sequence); put32(p + 12, payloadBytes);
  put32(p + 16 + payloadBytes, crc32(p, 16 + payloadBytes));
  return 20 + payloadBytes;
}
} // namespace recording
