#pragma once
// In-memory property contract shared by fallback and runtime profile catalogs.
// Register ordering, masks, encoding, units, scale/offset and access are explicit.
// Descriptors borrow strings from immutable generated data or package arenas.
#include <stdint.h>
namespace decoding {
struct EnumValue { uint32_t code; const char *text; };
struct Property {
    const char *id, *name, *unit, *note;
    uint8_t address, low, high, count, shift, precision;
    uint32_t mask, minimum, maximum;
    double scale, offset;
    bool writable, ascii, conflict, unsignedCurrent;
    const char *bit0, *bit1;
    const EnumValue *enums; unsigned enumCount;
    const char *meaning0, *meaning1;
    const char *description;
    double atomic_step; // Display policy in the property's declared output unit, not ADC resolution.
    const char *friendlydesc;
    const char *access;
    uint16_t profile;
    bool activeReadable;
};
struct Profile {
    const char *id, *name;
    uint16_t revision;
    const uint8_t *typicalAddresses;
    uint8_t addressCount;
    uint16_t first, count;
    const char *source;
    uint8_t readProtocol; // 1: one-byte pointer, repeated START, one-byte read/NACK/STOP.
    const Property *table; // Runtime packages own their immutable definition table.
};
template<class T,unsigned Capacity> struct Table {
    T entries[Capacity]{}; unsigned count=0;
    T &operator[](unsigned i){return entries[i];}
    const T &operator[](unsigned i)const{return entries[i];}
    T *begin(){return entries;} T *end(){return entries+count;}
    const T *begin()const{return entries;} const T *end()const{return entries+count;}
};
}
