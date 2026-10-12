#pragma once
// Owned observations and bounded history/unique stores. Successful evidence,
// malformed data, transport loss and intentional filtering are distinct facts.
// Command control progresses through acknowledgements instead of optimistically
// changing capture state when bytes are merely sent to the serial driver.
#include "bringup_protocol.h"
#include <string.h>
#include <stdio.h>

namespace observer {
constexpr unsigned UniqueCapacity = 128, HistoryCapacity = 64;
struct Observation {
    uint32_t id = 0, changed_ms = 0, observed_ms = 0;
    bool success = false, associated = false, fresh = false, unchanged = false;
    uint8_t address = 0, reg = 0;
    char direction = '?';
    uint16_t dataBytes = 0, length = 0;
    uint8_t preview[3] = {};
    uint8_t raw[bringup::MaxPayload] = {};
};
inline bool parse(const uint8_t *p, size_t n, Observation &out) {
    if (n < 10 || n > bringup::MaxPayload || p[4] > 1 || p[5] > 1 ||
        p[8] == 0 || p[8] > 8 || p[9] > 1 || (p[5] && (p[6] >= 128))) return false;
    size_t at = 10;
    int chosen = -1;
    uint16_t chosenCount = 0;
    uint8_t chosenRead = 0;
    for (unsigned frame = 0; frame < p[8]; ++frame) {
        if (n - at < 5) return false;
        uint16_t count = bringup::le16(p + at);
        if (count > 258 || p[at + 2] > 1 || p[at + 3] > 1) return false;
        at += 5;
        if (size_t(count) * 2 > n - at) return false;
        for (unsigned b = 0; b < count; ++b) if (p[at + b * 2 + 1] > 1) return false;
        if (count) {
            bool read = p[at] & 1;
            if (chosen < 0 || (p[5] && p[8] == 2 && frame == 1)) {
                chosen = int(at); chosenCount = count; chosenRead = read;
            }
        }
        at += count * 2;
    }
    if (at != n) return false;
    // Reset metadata without constructing a 4.6 KB temporary on the MCU stack.
    out.changed_ms = out.observed_ms = 0; out.fresh = out.unchanged = false;
    memset(out.preview, 0, sizeof(out.preview));
    out.id = bringup::le32(p); out.success = p[4]; out.associated = p[5];
    out.address = out.associated ? p[6] : chosen >= 0 ? p[chosen] >> 1 : 0;
    out.reg = p[7]; out.direction = chosen < 0 ? '?' : chosenRead ? 'R' : 'W';
    unsigned first = chosenRead ? 1 : out.associated ? 2 : 1;
    out.dataBytes = chosenCount > first ? chosenCount - first : 0;
    for (unsigned i = 0; i < out.dataBytes && i < 3; ++i) out.preview[i] = p[chosen + (first + i) * 2];
    out.length = uint16_t(n); memcpy(out.raw, p, n);
    return true;
}
inline bool addressEvidence(const Observation &o) {
    if(!o.success || o.length<10 || o.raw[9] || o.address<8 || o.address>=0x78)return false;
    size_t at=10;bool candidate=false;
    for(unsigned frame=0;frame<o.raw[8];++frame){
        if(at+5>o.length)return false;
        unsigned count=bringup::le16(o.raw+at);bool invalid=o.raw[at+3] || o.raw[at+4];at+=5;
        if(count*2>o.length-at)return false;
        if(invalid)return false;
        if(count>=1 && (o.raw[at]>>1)==o.address && o.raw[at+1])candidate=true;
        at+=count*2;
    }
    return candidate && at==o.length;
}
inline void summary(const Observation &o, char *buf, size_t cap) {
    char reg[3];
    if (o.associated) snprintf(reg, sizeof(reg), "%02X", o.reg); else memcpy(reg, "--", 3);
    size_t used = size_t(snprintf(buf, cap, "%02X %s %c", o.address, reg, o.direction));
    for (unsigned i = 0; i < o.dataBytes && i < 3 && used < cap; ++i)
        used += size_t(snprintf(buf + used, cap - used, " %02X", o.preview[i]));
    if (o.dataBytes > 3 && used < cap) snprintf(buf + used, cap - used, "+");
}
class Store {
public:
    Observation unique[UniqueCapacity], history[HistoryCapacity], incoming, detail;
    uint16_t order[UniqueCapacity] = {};
    unsigned uniqueCount = 0, historyCount = 0, historyHead = 0;
    uint32_t received = 0, failures = 0, unassociated = 0, capacityMiss = 0, malformed = 0;
    uint32_t revision = 0;
    void clear() {
        uniqueCount = historyCount = historyHead = 0;
        received = failures = unassociated = capacityMiss = malformed = 0;
        ++revision;
    }
    bool add(const uint8_t *p, size_t n, uint32_t now_ms, bool retain=true) {
        if (!parse(p, n, incoming)) { ++malformed; return false; }
        ++received; if (!incoming.success) ++failures;
        incoming.changed_ms = now_ms;
        incoming.observed_ms = now_ms;
        if (!incoming.associated) ++unassociated;
        if (retain) retainIncoming();
        return true;
    }
    void retainIncoming() {
        const uint8_t *p=incoming.raw;
        const size_t n=incoming.length;
        const uint32_t now_ms=incoming.observed_ms;
        unsigned slot = 0;
        if(incoming.associated)while (slot < uniqueCount && (unique[slot].address != incoming.address || unique[slot].reg != incoming.reg)) ++slot;
        bool isNew = slot == uniqueCount;
        bool identical=incoming.associated && !isNew && unique[slot].length==n && memcmp(unique[slot].raw+4,p+4,n-4)==0;
        incoming.unchanged=identical && incoming.success;
        incoming.fresh=incoming.associated && isNew;
        history[historyHead] = incoming;
        historyHead = (historyHead + 1) % HistoryCapacity;
        if (historyCount < HistoryCapacity) ++historyCount;
        ++revision;
        if (!incoming.associated) return;
        if (isNew && uniqueCount == UniqueCapacity) { ++capacityMiss; return; }
        if (identical) {
            unique[slot].observed_ms=now_ms; unique[slot].id=incoming.id;
            unique[slot].unchanged=incoming.unchanged;
            memcpy(unique[slot].raw,p,4);
            return;
        }
        incoming.fresh = isNew;
        unique[slot] = incoming;
        unsigned pos = 0;
        if (isNew) pos = uniqueCount++;
        else while (pos < uniqueCount && order[pos] != slot) ++pos;
        for (; pos > 0; --pos) order[pos] = order[pos - 1];
        order[0] = uint16_t(slot);
    }
    const Observation *row(bool uniqueMode, unsigned index) const {
        if (uniqueMode) return index < uniqueCount ? &unique[order[index]] : nullptr;
        return index < historyCount ? &history[(historyHead + HistoryCapacity - 1 - index) % HistoryCapacity] : nullptr;
    }
};
enum class Action { None, Enable, Pause, Off, Clear, Synchronize, Configure, DiscoverPause };
class Control {
public:
    Action action = Action::None;
    unsigned step = 0;
    uint32_t nextId = 0, pendingId = 0, sent_ms = 0;
    uint32_t timeout_ms=2500;
    uint8_t opcode = 0;
    const char *message = "STANDBY";
    bool uncertain = false;
    bool resumeAfterClear = false;
    uint32_t clearsAcknowledged = 0;
    uint8_t filterCommand[52]={8,8,3,0};
    bool begin(Action wanted, bool wasRunning = false) {
        if (action != Action::None || pendingId) return false;
        action = wanted; step = 0; resumeAfterClear = wanted == Action::Clear && wasRunning;
        uncertain = false; message = "COMMAND PENDING"; return true;
    }
    size_t prepare(uint32_t now_ms, uint8_t *wire, size_t cap) {
        if (action == Action::None || pendingId) return 0;
        uint8_t p[64] = {}; size_t n = 1;
        if (action == Action::Enable) {
            if (step == 0) { memcpy(p,filterCommand,52); n=52; }
            else if (step == 1) { p[0] = 5; p[1] = 1; n = 2; }
            else p[0] = 1;
        } else if (action == Action::Synchronize) {
            if (step == 0) p[0] = 2;
            else if (step == 1) { memcpy(p,filterCommand,52); n=52; }
            else if (step == 2) { p[0] = 5; p[1] = 1; n = 2; }
            else p[0] = 4;
        } else if (action == Action::Clear) {
            if (step == 0) p[0] = 2;
            else if (step == 1) p[0] = 9;
            else if (step == 2) p[0] = 4;
            else if (step == 3) { memcpy(p,filterCommand,52); n=52; }
            else if (step == 4) { p[0] = 5; p[1] = 1; n = 2; }
            else p[0] = 1;
        } else if (action==Action::Configure) {
            if (step==0) { p[0]=5; p[1]=0; n=2; }
            else if (step==1) { memcpy(p,filterCommand,52); n=52; }
            else if (step==2) { p[0]=5; p[1]=1; n=2; }
            else p[0]=4;
        } else if (action==Action::DiscoverPause && step==1) p[0]=4;
        else if (action == Action::Off && step == 1) { p[0] = 5; p[1] = 0; n = 2; }
        else p[0] = 2;
        if (++nextId == 0) ++nextId;
        size_t used = bringup::command(nextId, p, n, wire, cap);
        if (used) { pendingId = nextId; sent_ms = now_ms; opcode = p[0]; }
        return used;
    }
    bool reply(const bringup::Packet &p) {
        if (!pendingId || p.type != 2 || p.length != 6 || bringup::le32(p.payload) != pendingId || p.payload[4] != opcode) return false;
        pendingId = 0;
        if (p.payload[5] != 0) { action = Action::None; uncertain = true; message = "COMMAND REJECTED / CHECK DIAG"; return true; }
        if (action == Action::Clear && opcode == 9) ++clearsAcknowledged;
        ++step;
        if ((action==Action::DiscoverPause && step==2) || (action == Action::Synchronize && step == 4) || (action==Action::Configure && step==4) || (action == Action::Enable && step == 3) || action == Action::Pause || (action == Action::Off && step == 2) ||
            (action == Action::Clear && step == (resumeAfterClear ? 6u : 3u))) {
            message = action==Action::DiscoverPause ? "DISCOVER / PAUSED VERIFIED" : action==Action::Configure ? "SOURCE FILTER ACKNOWLEDGED" : action == Action::Synchronize ? "LINK READY / STANDBY" : action == Action::Clear ? (resumeAfterClear ? "CLEARED / RUN ACKNOWLEDGED" : "CLEARED / PAUSED") :
                action == Action::Enable ? "RUN ACKNOWLEDGED" : action == Action::Pause ? "PAUSE ACKNOWLEDGED" : "OFF ACKNOWLEDGED";
            action = Action::None;
        }
        return true;
    }
    void abort(const char *reason) { pendingId = 0; action = Action::None; uncertain = true; message = reason; }
    void tick(uint32_t now_ms) { if (pendingId && uint32_t(now_ms - sent_ms) >= timeout_ms) abort("ACK TIMEOUT / STATE UNCERTAIN"); }
    bool busy() const { return action != Action::None || pendingId; }
};
} // namespace observer
