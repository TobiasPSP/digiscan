#pragma once
// Resolve a runtime property into the profile's verified active-read protocol.
// Registers are byte addresses; a missing/unsupported plan fails explicitly.
// Unit conversion belongs to decoding, never to the electrical acquisition job.
#include "profile_registry.h"
namespace acquisition {
struct Plan { uint8_t address=0,count=0,registers[3]={}; };
inline const char *resolve(unsigned index,Plan &plan) {
    plan={};
    if (index>=decoding::PropertyCount) return "Property is no longer available.";
    const auto &p=decoding::properties[index];
    if (!p.count || p.profile>=decoding::ProfileCount) return "No assigned device profile.";
    if (!p.activeReadable) return "Unsupported register-read protocol or register layout.";
    if (decoding::profiles[p.profile].readProtocol!=1) return "Unsupported register-read protocol.";
    if (p.address<8 || p.address>119) return "Reserved I2C address; active access is disabled.";
    if (p.count>3 || unsigned(p.low)+p.count>256 || (p.count>1 && p.high!=p.low+p.count-1))
        return "Unsupported register layout; requires 1 to 3 consecutive registers.";
    plan.address=p.address;plan.count=p.count;
    for (unsigned i=0;i<p.count;++i) plan.registers[i]=p.low+i;
    return nullptr;
}
}
