#pragma once
// One active profile binding per 7-bit I2C address. Seen and confirmed differ:
// only successful traffic/scan evidence exposes an address to tool selectors.
// Manual assignment may override typical profile addresses. Binding identity
// uses stable chip IDs, not array indices that change after installing packages.
#include "generated_profiles.h"
#include <string.h>
namespace decoding {
enum class Assignment : uint8_t { Unresolved, Automatic, Manual, Disabled };
struct Binding {
    int16_t profile=-1;
    Assignment mode=Assignment::Unresolved;
    bool seen=false, confirmed=false, deferred=false, dirty=false, missingSaved=false;
};
class Registry {
public:
    uint32_t generation=0;
    uint32_t evidenceGeneration=0;
    Binding bindings[128];
    bool saveFailed=false;
    Registry() { reset(); }
    static Property empty() {
        Property p{};
        p.id=p.name=p.unit=p.note=p.description=p.friendlydesc=p.access="";
        return p;
    }
    void reset() {
        ++generation;++evidenceGeneration;
        for (auto &p:properties) p=empty();
        for (auto &b:bindings) b=Binding{};
        saveFailed=false;
    }
    unsigned activeCount() const { unsigned n=0; for (const auto &p:properties) if (p.count) ++n; return n; }
    int find(const char *id) const { for (unsigned i=0;i<ProfileCount;++i) if (!strcmp(id,profiles[i].id)) return int(i); return -1; }
    unsigned candidates(uint8_t address,int &only) const {
        unsigned count=0; only=-1;
        for (unsigned i=0;i<ProfileCount;++i) for (unsigned a=0;a<profiles[i].addressCount;++a)
            if (profiles[i].typicalAddresses[a]==address) { ++count; only=int(i); break; }
        return count;
    }
    bool canAssign(uint8_t address,int profile) const {
        if (address<8 || address>=0x78 || profile<-1 || profile>=int(ProfileCount)) return false;
        if (profile<0) return true;
        unsigned available=0;
        for (const auto &p:properties) if (!p.count || p.address==address) ++available;
        return available>=profiles[profile].count;
    }
    // Caller invalidates this address's decoded state before replacing slots.
    bool assign(uint8_t address,int profile,Assignment mode) {
        if (!canAssign(address,profile)) return false;
        for (auto &p:properties) if (p.count && p.address==address) p=empty();
        if (profile>=0) {
            const auto &source=profiles[profile]; unsigned next=0;
            for (auto &p:properties) if (!p.count && next<source.count) {
                p=definition(source,next++); p.address=address; p.profile=uint16_t(profile);
            }
        }
        auto &b=bindings[address]; b.profile=int16_t(profile); b.mode=mode; b.deferred=false; b.missingSaved=false;
        ++generation;
        return true;
    }
    void observe(uint8_t address,bool parsing) {
        if (address<8 || address>=0x78) return;
        // Caller supplies successful, complete bus evidence, not a failed decode.
        auto &b=bindings[address]; b.seen=true;
        if(!b.confirmed){b.confirmed=true;++evidenceGeneration;}
        if (!parsing || b.mode!=Assignment::Unresolved || b.missingSaved) return;
        int only; if (candidates(address,only)==1) assign(address,only,Assignment::Automatic);
    }
    bool reportEligible(uint8_t address,bool includeUnverified=false) const {
        return address>=8 && address<0x78 && bindings[address].profile>=0 && (includeUnverified || bindings[address].confirmed);
    }
    int nextPrompt() const {
        for (unsigned a=8;a<0x78;++a) if (bindings[a].seen && bindings[a].mode==Assignment::Unresolved && !bindings[a].deferred) return int(a);
        return -1;
    }
    const char *source(uint8_t address) const {
        auto mode=bindings[address].mode;
        return mode==Assignment::Manual?"Manual":mode==Assignment::Automatic?"Automatic":mode==Assignment::Disabled?"No parsing":"Unresolved";
    }
};
}
