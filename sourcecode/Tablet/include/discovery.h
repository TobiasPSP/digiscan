#pragma once
// Session-only discovery decisions and multi-register candidate assembly.
// Known property groups wait for their required companion bytes. Capture gaps
// invalidate partial assembly instead of mixing values across discontinuities.
// Keep/Ignore applies to grouped registers, not individual bytes of a word.
#include "observations.h"
#include "analysis.h"

namespace discovery {
enum class Decision : uint8_t { Unknown, Keep, Ignore };
constexpr unsigned PendingCapacity=128;
struct Scope {
    uint8_t low=0, bytes=1;
    bool parsed=false;
    Scope(uint8_t start=0,uint8_t count=1,bool known=false):low(start),bytes(count),parsed(known) {}
};
inline Scope scope(const observer::Observation &o) {
    Scope result;result.low=o.reg;
    bool found=false;
    for(const auto &p:decoding::properties) {
        if(!p.count || p.count>3 || p.address!=o.address || o.reg<p.low || unsigned(o.reg)>=unsigned(p.low)+p.count)continue;
        if(found && (result.low!=p.low || result.bytes!=p.count))return Scope{o.reg,1,false};
        result=Scope{p.low,p.count,true};found=true;
    }
    return result;
}
struct Candidate {
    observer::Observation transactions[3];
    Scope range;
    uint8_t present=0;
    bool complete=false, success=false;
    bool awaitingStart=false;
    uint32_t started_ms=0;
    const char *problem=nullptr;
    const observer::Observation &first() const {
        for(unsigned i=0;i<3;++i)if(present&(1u<<i))return transactions[i];
        return transactions[0];
    }
    void start(const observer::Observation &o,Scope s) {
        range=s;present=0;complete=success=false;awaitingStart=false;started_ms=o.observed_ms;problem=nullptr;add(o);
    }
    void add(const observer::Observation &o) {
        const unsigned i=o.reg-range.low;
        if(i>=range.bytes || i>=3)return;
        transactions[i]=o;present|=uint8_t(1u<<i);
        complete=present==((1u<<range.bytes)-1);success=true;
        for(unsigned n=0;n<range.bytes;++n)if(present&(1u<<n)) {
            success&=transactions[n].success;
            if(range.parsed && transactions[n].dataBytes!=1)complete=false;
        }
    }
    uint32_t word() const {
        uint32_t value=0;for(unsigned i=0;i<range.bytes;++i)if(present&(1u<<i))value|=uint32_t(transactions[i].preview[0])<<(8*i);
        return value;
    }
};
// Two bits per address/register/direction. Decisions never evict each other.
class Session {
    uint8_t decisions[16384]={};
public:
    Candidate pending[PendingCapacity], assembling[8], scratch;
    bool assemblyUsed[8]={};
    unsigned head=0, count=0, kept=0, ignored=0;
    uint32_t overflow=0, unkeyed=0, incompleteGroups=0, revision=0;
    bool enabled=false;
    bool reviewing=false;
    static bool keyed(const observer::Observation &o) {
        return o.associated && o.address<128 && (o.direction=='R' || o.direction=='W');
    }
    static unsigned key(const observer::Observation &o) {
        return (unsigned(o.address)*256+o.reg)*2+(o.direction=='W');
    }
    Decision decision(const observer::Observation &o) const {
        if (!keyed(o)) return Decision::Unknown;
        const auto s=scope(o);Decision result=Decision::Unknown;
        for(unsigned i=0;i<s.bytes;++i) {
            unsigned k=(unsigned(o.address)*256+s.low+i)*2+(o.direction=='W');
            auto d=Decision((decisions[k/4]>>((k%4)*2))&3);
            if(!i)result=d;else if(d!=result)return Decision::Unknown;
        }
        return result;
    }
    bool hasDecisions() const { return kept || ignored; }
    bool allows(const observer::Observation &o) const {
        if(!enabled || !hasDecisions())return true;
        return kept?decision(o)==Decision::Keep:decision(o)!=Decision::Ignore;
    }
    bool enqueue(const observer::Observation &o) {
        if (!keyed(o)) { ++unkeyed;return false; }
        if (decision(o)!=Decision::Unknown) return false;
        scratch.start(o,Scope{o.reg,1,false});return enqueueGroup(scratch);
    }
    bool enqueueGroup(const Candidate &c) {
        for(unsigned i=0;i<count;++i) {
            auto &old=pending[(head+i)%PendingCapacity];
            if(old.first().address==c.first().address && old.first().direction==c.first().direction &&
                old.range.low==c.range.low && old.range.bytes==c.range.bytes) {
                if(!old.complete && c.complete && !(reviewing && i==0)){old=c;++revision;}
                return false;
            }
        }
        if(count==PendingCapacity){++overflow;return false;}
        pending[(head+count)%PendingCapacity]=c;++count;++revision;return true;
    }
    void finish(unsigned i,const char *reason) {
        assembling[i].problem=reason;enqueueGroup(assembling[i]);assemblyUsed[i]=false;
    }
    void expire(uint32_t now_ms) {
        for(unsigned i=0;i<8;++i)if(assemblyUsed[i] && uint32_t(now_ms-assembling[i].started_ms)>1000) {
            assemblyUsed[i]=false;++incompleteGroups;
        }
    }
    void gap() {
        // Pausing for another candidate can cut a valid pair in half. A partial
        // sample is not a new transaction to approve; retry on the next full pair.
        for(unsigned i=0;i<8;++i)if(assemblyUsed[i]){assemblyUsed[i]=false;++incompleteGroups;}
    }
    void ingest(const observer::Observation &o,bool interested) {
        // All traffic participates in continuity, even a filtered-out register.
        expire(o.observed_ms);
        const auto s=scope(o);
        for(unsigned i=0;i<8;++i)if(assemblyUsed[i] && assembling[i].first().address==o.address) {
            auto &a=assembling[i];
            const bool same=interested && o.direction==a.first().direction && s.low==a.range.low && s.bytes==a.range.bytes;
            if(same && o.reg==s.low && o.success && o.dataBytes==1) {
                a.start(o,s);return;
            }
            unsigned next=0;while(next<a.range.bytes && (a.present&(1u<<next)))++next;
            if(!a.awaitingStart && same &&
                o.reg==unsigned(a.range.low)+next && o.dataBytes==1) {
                a.add(o);
                if(!o.success || a.complete)finish(i,o.success?nullptr:"Companion read failed");
                return;
            }
            // Wait for a fresh low byte; never stop capture on a mid-pair orphan.
            a.awaitingStart=true;a.problem="Register sequence interrupted";
            if(same)return;
        }
        if(!interested)return;
        if(!keyed(o)){++unkeyed;return;}
        if(decision(o)!=Decision::Unknown)return;
        // A successful selector has no property value. Unknown zero-payload
        // commands still need raw review, and failed selectors remain visible.
        if(s.parsed && o.success && o.direction=='W' && !o.dataBytes)return;
        if(!s.parsed || s.bytes==1 || !o.success || o.dataBytes!=1) {
            scratch.start(o,s);
            if(!scratch.complete)scratch.problem=o.dataBytes!=1?"Payload not decoded by this profile":"Missing preceding register read";
            enqueueGroup(scratch);return;
        }
        for(unsigned i=0;i<8;++i)if(!assemblyUsed[i]){
            assembling[i].start(o,s);assembling[i].awaitingStart=o.reg!=s.low;
            if(assembling[i].awaitingStart)assembling[i].problem="Waiting for preceding register read";
            assemblyUsed[i]=true;return;
        }
        ++overflow;scratch.start(o,s);scratch.problem="Assembly capacity exhausted";enqueueGroup(scratch);
    }
    const Candidate *group() const { return count?&pending[head]:nullptr; }
    const observer::Observation *front() const { return count?&pending[head].first():nullptr; }
    bool decide(Decision value) {
        if(!count || value==Decision::Unknown)return false;
        const auto &c=pending[head];
        for(unsigned i=0;i<c.range.bytes;++i) {
            unsigned k=(unsigned(c.first().address)*256+c.range.low+i)*2+(c.first().direction=='W'),shift=(k%4)*2;
            decisions[k/4]=uint8_t((decisions[k/4]&~(3u<<shift))|(unsigned(value)<<shift));
        }
        if(value==Decision::Keep)++kept;else ++ignored;
        head=(head+1)%PendingCapacity;--count;++revision;reviewing=false;return true;
    }
    void clear() {
        memset(decisions,0,sizeof(decisions));memset(assemblyUsed,0,sizeof(assemblyUsed));head=count=kept=ignored=0;overflow=unkeyed=incompleteGroups=0;reviewing=false;++revision;
    }
};
}
