#pragma once
// Bounded structured UART framing shared with the RP companion protocol.
// Decode framing/length/checksum before accessing payload fields. Partial input
// survives pump iterations; malformed packets are counted, not trusted as data.
// Binary integers are explicitly encoded rather than serializing C++ structs.
#include <stdint.h>
#include <stddef.h>

namespace bringup {
constexpr size_t MaxPayload = 4608;
constexpr size_t MaxRaw = MaxPayload + 14;
constexpr size_t MaxWire = 4642;
inline uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
inline uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t crc = 0xffffffff;
    while (n--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return ~crc;
}
inline size_t encode(const uint8_t *src, size_t n, uint8_t *dst, size_t cap) {
    if (!cap) return 0;
    size_t out = 1, codeAt = 0;
    uint8_t code = 1;
    for (size_t i = 0; i < n; ++i) {
        if (!src[i]) {
            dst[codeAt] = code;
            if (out >= cap) return 0;
            codeAt = out++; code = 1;
        } else {
            if (out >= cap) return 0;
            dst[out++] = src[i];
            if (++code == 255) {
                dst[codeAt] = code;
                if (out >= cap) return 0;
                codeAt = out++; code = 1;
            }
        }
    }
    dst[codeAt] = code;
    if (out >= cap) return 0;
    dst[out++] = 0;
    return out;
}
inline size_t statusRequest(uint32_t id, uint8_t *wire, size_t cap) {
    uint8_t raw[15] = {'I', 'S', 1, 1};
    put32(raw + 4, id); raw[8] = 1; raw[10] = 4;
    put32(raw + 11, crc32(raw, 11));
    return encode(raw, sizeof(raw), wire, cap);
}
inline size_t command(uint32_t id, const uint8_t *payload, size_t n, uint8_t *wire, size_t cap) {
    if (!payload || !n || n > 267) return 0;
    uint8_t raw[281] = {'I', 'S', 1, 1};
    put32(raw + 4, id); raw[8] = uint8_t(n); raw[9]=uint8_t(n>>8);
    for (size_t i = 0; i < n; ++i) raw[10 + i] = payload[i];
    put32(raw + 10 + n, crc32(raw, 10 + n));
    return encode(raw, 14 + n, wire, cap);
}
struct Packet { uint8_t type; uint32_t sequence; const uint8_t *payload; uint16_t length; };
inline bool matchesStatusReply(const Packet &p, uint32_t requestId) {
    // Response headers carry the output sequence, not the command request ID.
    return requestId && p.type == 2 && p.length == 6 &&
        le32(p.payload) == requestId && p.payload[4] == 4;
}
class Receiver {
    uint8_t wire_[MaxWire], raw_[MaxRaw];
    size_t used_ = 0;
    bool discard_ = false;
public:
    uint32_t bytes = 0, valid = 0, bad = 0;
    void clearDiagnostics() { bytes=valid=bad=0; }
    void inputGap() { used_ = 0; discard_ = true; ++bad; }
    void reconnect() { used_ = 0; discard_ = false; }
    bool feed(uint8_t b, Packet &packet) {
        ++bytes;
        if (b) {
            if (!discard_) {
                if (used_ < sizeof(wire_)) wire_[used_++] = b;
                else { discard_ = true; ++bad; }
            }
            return false;
        }
        if (discard_) { discard_ = false; used_ = 0; return false; }
        if (!used_) return false;
        size_t end = used_, in = 0, out = 0;
        used_ = 0;
        while (in < end) {
            uint8_t code = wire_[in++];
            size_t count = code - 1;
            if (!code || count > end - in || count > sizeof(raw_) - out) { ++bad; return false; }
            while (count--) raw_[out++] = wire_[in++];
            if (code != 255 && in < end) {
                if (out == sizeof(raw_)) { ++bad; return false; }
                raw_[out++] = 0;
            }
        }
        if (out < 14 || raw_[0] != 'I' || raw_[1] != 'S' || raw_[2] != 1 ||
            raw_[3] < 2 || raw_[3] > 8) { ++bad; return false; }
        uint16_t length = le16(raw_ + 8);
        if (length > MaxPayload || out != size_t(length) + 14 ||
            le32(raw_ + out - 4) != crc32(raw_, out - 4)) { ++bad; return false; }
        if(raw_[3]==6 && length==35){
            const auto *p=raw_+10;
            if(p[27]!=6 || p[4]>10 || p[5]>3 || p[6]>1 || p[26]>1 ||
                p[29]<8 || p[29]>119 || !p[30] || p[30]>3 || p[5]>p[30] ||
                unsigned(p[31])+p[30]>256 || le16(p+32)>=128 || p[34]>1 ||
                (!p[34] && (p[6] || (p[4]!=0 && p[4]!=2 && p[4]!=3)))){++bad;return false;}
            packet={raw_[3],le32(raw_+4),p,length};++valid;return true;
        }
        if ((raw_[3] == 3 && length != 132 && length != 172 && length != 200) || (raw_[3] == 2 && length != 6) ||
            (raw_[3]==6 && (length!=32 || (raw_[10+27]<1 || raw_[10+27]>4) || raw_[10+4]>10 ||
                raw_[10+5]>(raw_[10+27]==1?2:raw_[10+27]==2?3:1) || raw_[10+6]>1 || raw_[10+26]>1 ||
                (raw_[10+27]==2 && (raw_[10+29]<8 || raw_[10+29]>119 || raw_[10+30]<1 || raw_[10+30]>3 ||
                    raw_[10+5]>raw_[10+30] || unsigned(raw_[10+31])+raw_[10+30]>256)) ||
                (raw_[10+27]==3 && (raw_[10+29]<8 || raw_[10+29]>119 || raw_[10+28]>2 ||
                    (raw_[10+28]==1?(le16(raw_+10+30)<1 || le16(raw_+10+30)>258):le16(raw_+10+30)!=0))) ||
                (raw_[10+27]==4 && (raw_[10+28]!=2 || raw_[10+29]<8 || raw_[10+29]>119 ||
                    raw_[10+30]>1 || raw_[10+31]<raw_[10+29] || raw_[10+31]>119 ||
                    (!raw_[10+30] && (raw_[10+6] || (raw_[10+4]!=0 && raw_[10+4]!=2)))))))) {
            ++bad; return false;
        }
        packet = {raw_[3], le32(raw_ + 4), raw_ + 10, length};
        ++valid;
        return true;
    }
};
struct TouchPoint { bool pressed = false; uint16_t x_px = 0, y_px = 0; };
inline bool parseTouch(const uint8_t *data, size_t n, TouchPoint &point) {
    point = {};
    if (n != 8) return false;
    // Match Espressif's one-contact driver: other counts provide no point,
    // not an I2C failure. Do not interpret coordinate bytes in those reports.
    if (data[1] != 1) return true;
    uint16_t x = uint16_t(data[2] & 15) << 8 | data[3];
    uint16_t y = uint16_t(data[4] & 15) << 8 | data[5];
    if (x >= 320 || y >= 480) return false;
    point.pressed = true; point.x_px = x; point.y_px = y;
    return true;
}
} // namespace bringup
