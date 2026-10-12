#pragma once
// Runtime semantic decoding and Monitor selection. Bytes first assemble using
// the profile's register order/endianness; mask/shift and numeric conversion then
// produce the declared unit exactly once. Boolean and enumerated fields differ.
// Change significance uses atomic_step * factor; zero means exact-value changes.
// Observed freshness is independent of whether a value materially changed.
#include "observations.h"
#include "profile_registry.h"
#include <math.h>
namespace analysis {
enum class Display { Raw, Parsed, Both };
enum class UniqueOrder { LatestTransaction, LatestChange };
inline bool moreRecent(uint32_t timeA,uint32_t idA,uint32_t timeB,uint32_t idB,bool breakTies) {
    return int32_t(timeA-timeB)>0 || (breakTies && timeA==timeB && int32_t(idA-idB)>0);
}
struct Selection {
    bool addresses[128]={}, registers[256]={};
    bool anyAddress=false, anyRegister=false, read=true, write=true;
    Display display=decoding::PropertyCount?Display::Parsed:Display::Raw;
    UniqueOrder uniqueOrder=UniqueOrder::LatestTransaction;
    double changeFactor=0;
    bool includeUnkeyed=true;
    uint8_t outcome=0;
    bool active() const { return anyAddress || anyRegister || !read || !write || outcome || !includeUnkeyed || uniqueOrder!=UniqueOrder::LatestTransaction || changeFactor!=0; }
    bool address(uint8_t a) const { return !anyAddress || (a<128 && addresses[a]); }
    bool reg(uint8_t r) const { return !anyRegister || registers[r]; }
    bool matches(const observer::Observation &o) const {
        return (outcome!=1 || o.success) && (outcome!=2 || !o.success) && address(o.address) &&
            (!anyRegister || (o.associated && reg(o.reg)) || (!o.associated && !o.success && includeUnkeyed)) &&
            (o.direction=='R'?read:o.direction=='W'?write:read && write);
    }
};
// Hexadecimal lists/ranges only; a candidate is committed only after both lists validate.
inline bool parseList(const char *s, bool *bits, unsigned size, bool &specified) {
    memset(bits,0,size*sizeof(bool)); specified=false;
    while (*s) {
        while (*s==' ' || *s=='\t') ++s;
        if (!*s) break;
        unsigned values[2]={}; unsigned count=0;
        for (;;) {
            if (s[0]=='0' && (s[1]=='x' || s[1]=='X')) s+=2;
            unsigned value=0, digits=0;
            while ((*s>='0' && *s<='9') || (*s>='a' && *s<='f') || (*s>='A' && *s<='F')) {
                unsigned digit=*s<='9'?*s-'0':*s<='F'?*s-'A'+10:*s-'a'+10;
                if (++digits>2) return false;
                value=value*16+digit; ++s;
            }
            if (!digits || value>=size) return false;
            values[count++]=value;
            while (*s==' ' || *s=='\t') ++s;
            if (count==1 && *s=='-') { ++s; while (*s==' ' || *s=='\t') ++s; continue; }
            break;
        }
        unsigned end=count==2?values[1]:values[0];
        if (end<values[0]) return false;
        for (unsigned n=values[0];n<=end;++n) bits[n]=true;
        specified=true;
        if (!*s) break;
        if (*s++!=',') return false;
        const char *next=s; while (*next==' ' || *next=='\t') ++next;
        if (!*next) return false;
    }
    return true;
}
struct Sequence {
    bool have=false, synced=false;
    uint32_t last=0, gaps=0, startupGaps=0, resets=0;
    bool packet(uint32_t seq) {
        bool discontinuity=false;
        if (have) {
            uint32_t delta=seq-last;
            if (delta>1 && delta<0x80000000u) {
                if (synced) gaps+=delta-1; else startupGaps+=delta-1;
                discontinuity=true;
            } else if (!delta || delta>=0x80000000u) {
                ++resets; synced=false; discontinuity=true;
            }
        }
        have=true; last=seq; return discontinuity;
    }
    void reconnect() { have=synced=false; }
};
struct Value {
    unsigned property=0;
    uint32_t word=0;
    uint32_t observed_ms=0, changed_ms=0;
    uint32_t transactionId=0;
    uint32_t fieldChanged_ms=0;
    char direction='R';
    bool complete=false, success=false, fresh=false, unchanged=false;
    bool observationIncomplete=false;
};
constexpr unsigned DecodedHistoryCapacity=128;
class Decoder {
public:
    void (*valueObserver)(const Value &)=nullptr;
    void (*resetObserver)(int)=nullptr;
    Value values[decoding::PropertyCount], history[DecodedHistoryCapacity];
    unsigned order[decoding::PropertyCount]={}, count=0, historyHead=0, historyCount=0;
    bool used[decoding::PropertyCount]={};
    uint32_t anchors[decoding::PropertyCount]={};
    double changeFactor=0;
    void setChangeFactor(double factor) {
        if (!isfinite(factor) || factor<0 || factor>100) return;
        changeFactor=factor;
        for (unsigned i=0;i<decoding::PropertyCount;++i) if (used[i]) anchors[i]=values[i].word;
    }
    int pending[128];
    uint32_t pendingWord[128]={};
    uint8_t nextByte[128]={};
    uint32_t pending_ms[128]={};
    Decoder() { for (auto &p:pending) p=-1; }
    void notifyIncomplete(unsigned address) {
        if (pending[address]<0 || !valueObserver) return;
        Value v; v.property=unsigned(pending[address]); v.word=pendingWord[address];
        v.observationIncomplete=true;
        v.observed_ms=pending_ms[address]; valueObserver(v);
    }
    void gap() { for (unsigned a=0;a<128;++a) { notifyIncomplete(a); pending[a]=-1; } }
    void clear() { count=historyHead=historyCount=0; memset(used,0,sizeof(used)); gap(); if (resetObserver) resetObserver(-1); }
    void clearAddress(uint8_t address) {
        if (address>=128) return;
        if (resetObserver) resetObserver(address);
        pending[address]=-1;
        unsigned kept=0;
        for (unsigned i=0;i<count;++i) {
            unsigned slot=order[i];
            if (decoding::properties[slot].address==address) { used[slot]=false; values[slot]=Value{}; anchors[slot]=0; }
            else order[kept++]=slot;
        }
        count=kept;
        Value survivors[DecodedHistoryCapacity]; kept=0;
        for (unsigned i=0;i<historyCount;++i) {
            const auto &v=history[(historyHead+DecodedHistoryCapacity-historyCount+i)%DecodedHistoryCapacity];
            if (decoding::properties[v.property].address!=address) survivors[kept++]=v;
        }
        memcpy(history,survivors,kept*sizeof(Value)); historyCount=kept; historyHead=kept%DecodedHistoryCapacity;
    }
    void emit(unsigned index,uint32_t word,const observer::Observation &o,bool complete,uint32_t now_ms) {
        Value v; v.property=index; v.word=word; v.direction=o.direction; v.complete=complete;
        v.success=o.success; v.observed_ms=now_ms; v.transactionId=o.id;
        bool changed=!used[index] || values[index].word!=word || values[index].direction!=v.direction ||
            values[index].complete!=complete || values[index].success!=v.success;
        const auto &definition=decoding::properties[index];
        bool stateChanged=!used[index] || values[index].direction!=v.direction ||
            values[index].complete!=complete || values[index].success!=v.success;
        unsigned code=(word&definition.mask)>>definition.shift;
        unsigned anchor=(anchors[index]&definition.mask)>>definition.shift;
        bool numeric=definition.atomic_step>0 && !definition.bit0 && !definition.enumCount && !definition.ascii && !definition.conflict;
        double delta_in_declared_unit=fabs((double(code)-double(anchor))*definition.scale);
        bool rangeChanged=used[index] &&
            (((values[index].word&definition.mask)>>definition.shift)<definition.minimum || ((values[index].word&definition.mask)>>definition.shift)>definition.maximum)!=
            (code<definition.minimum || code>definition.maximum);
        bool fieldChanged=stateChanged || rangeChanged || (code!=anchor &&
            (!numeric || changeFactor==0 || delta_in_declared_unit+1e-9>=definition.atomic_step*changeFactor));
        if (fieldChanged) anchors[index]=word;
        v.fieldChanged_ms=fieldChanged?now_ms:values[index].fieldChanged_ms;
        v.fresh=!used[index] || (!fieldChanged && values[index].fresh);
        v.unchanged=used[index] && complete && v.success && !stateChanged &&
            ((values[index].word&definition.mask)==(word&definition.mask));
        v.changed_ms=v.fieldChanged_ms;
        if (valueObserver) valueObserver(v);
        history[historyHead]=v; historyHead=(historyHead+1)%DecodedHistoryCapacity;
        if (historyCount<DecodedHistoryCapacity) ++historyCount;
        if (!changed) { values[index].observed_ms=now_ms; values[index].transactionId=o.id;values[index].unchanged=v.unchanged;return; }
        values[index]=v;
        unsigned pos=0;
        if (!used[index]) { used[index]=true; pos=count++; }
        else while (pos<count && order[pos]!=index) ++pos;
        for (;pos>0;--pos) order[pos]=order[pos-1];
        order[0]=index;
    }
    void add(const observer::Observation &o,uint32_t now_ms) {
        if (o.address>=128) return;
        int previous=pending[o.address];
        if (!o.success) pending[o.address]=-1;
        bool target=false;
        for (const auto &p:decoding::properties) if (p.count && p.address==o.address) { target=true; break; }
        if (!target) return;
        if (previous>=0) {
            const auto &p=decoding::properties[previous];
            bool matches=o.associated && o.address==p.address && o.reg==p.low+nextByte[o.address] && o.direction=='R' && o.dataBytes==1;
            if (!matches || uint32_t(now_ms-pending_ms[o.address])>1000) {
                pending[o.address]=previous; notifyIncomplete(o.address);
            }
            pending[o.address]=-1;
            if (matches && uint32_t(now_ms-pending_ms[o.address])<=1000) {
                uint32_t word=pendingWord[o.address] | uint32_t(o.preview[0])<<(8*nextByte[o.address]);
                if (++nextByte[o.address]==p.count) emit(unsigned(previous),word,o,true,now_ms);
                else if (o.success) { pending[o.address]=previous; pendingWord[o.address]=word; }
                else emit(unsigned(previous),0,o,false,now_ms);
                return;
            }
        }
        if (!o.associated || o.dataBytes!=1) return;
        for (unsigned i=0;i<decoding::PropertyCount;++i) {
            const auto &p=decoding::properties[i];
            if (p.address!=o.address || (o.direction!='R' && !(o.direction=='W' && p.writable))) continue;
            if (p.count==1 && p.low==o.reg) emit(i,o.preview[0],o,true,now_ms);
            else if (p.count>1 && o.reg==p.low && o.direction=='R') {
                if (o.success) { pending[o.address]=int(i); pendingWord[o.address]=o.preview[0]; nextByte[o.address]=1; pending_ms[o.address]=now_ms; }
                else emit(i,0,o,false,now_ms);
            } else if (p.count>1 && o.reg>p.low && o.reg<=p.high && o.direction=='R') emit(i,0,o,false,now_ms);
        }
    }
    const Value *row(bool unique,unsigned index) const {
        if (unique) return index<count?&values[order[index]]:nullptr;
        return index<historyCount?&history[(historyHead+DecodedHistoryCapacity-1-index)%DecodedHistoryCapacity]:nullptr;
    }
};
inline bool matches(const Selection &f,const Value &v) {
    const auto &p=decoding::properties[v.property];
    bool registerMatch=false;
    for (unsigned n=0;n<p.count;++n) registerMatch|=f.reg(p.low+n);
    return (f.outcome!=1 || v.success) && (f.outcome!=2 || !v.success) && f.address(p.address) && registerMatch &&
        (v.direction=='R'?f.read:f.write);
}
inline void formatValue(const Value &v,char *out,size_t cap) {
    const auto &p=decoding::properties[v.property];
    if (!v.complete) { snprintf(out,cap,"-- INCOMPLETE"); return; }
    unsigned code=(v.word&p.mask)>>p.shift;
    char body[160];
    if (p.bit0) snprintf(body,sizeof(body),"%s",code?p.bit1:p.bit0);
    else if (p.enumCount) {
        const char *meaning=nullptr;
        for (unsigned i=0;i<p.enumCount;++i) if (p.enums[i].code==code) meaning=p.enums[i].text;
        if (meaning) snprintf(body,sizeof(body),"%s",meaning);
        else snprintf(body,sizeof(body),"%u UNDEFINED",code);
    } else if (p.ascii) {
        if (code>=32 && code<=126) snprintf(body,sizeof(body),"'%c'",char(code));
        else snprintf(body,sizeof(body),"ASCII %02X",code);
    } else {
        // One explicit conversion from YAML field code to its declared output unit.
        double value_in_declared_unit=code*p.scale+p.offset;
        snprintf(body,sizeof(body),"%.*f%s%s",p.precision,value_in_declared_unit,*p.unit?" ":"",p.unit);
    }
    snprintf(out,cap,"%s%s%s%s",v.success?"":"FAIL ",body,
        code<p.minimum || code>p.maximum?" RANGE":"",p.conflict?" CONFLICT":"");
}
inline bool sameSource(const Value &a,const Value &b,bool unique) {
    const auto &x=decoding::properties[a.property], &y=decoding::properties[b.property];
    return x.address==y.address && x.low==y.low && x.count==y.count && x.high==y.high &&
        (unique || a.transactionId==b.transactionId);
}
inline unsigned priority(const Value &v) {
    const auto &p=decoding::properties[v.property];
    if (*p.unit && strcmp(p.unit,"code")) return 3;
    if (p.enumCount || !p.bit0) return 2;
    return 1;
}
inline bool flagsOnly(const Value &v) {
    unsigned count=0;
    const auto &p=decoding::properties[v.property];
    if (p.count!=1) return false;
    for (const auto &field:decoding::properties) {
        if (field.address!=p.address || field.low!=p.low || field.count!=1 || (v.direction=='W' && !field.writable)) continue;
        if (!field.bit0) return false;
        ++count;
    }
    return count>1;
}
inline bool bitfields(const Value &v) {
    const auto &p=decoding::properties[v.property];
    if (p.count!=1) return false;
    unsigned count=0;bool masked=false;
    for (const auto &field:decoding::properties) {
        if(field.address!=p.address || field.low!=p.low || field.count!=1 ||
            (v.direction=='W' && !field.writable))continue;
        ++count;masked|=field.mask!=255;
    }
    return count>1 && masked;
}
struct ColumnFlow {
    int column=0, y=0, page=0, extent=320;
    void place(int height,int &x_px,int &y_px) {
        if (y && y+height>320) {
            y=0;
            if (column==0) column=1;
            else { column=0; page+=extent; extent=320; }
        }
        x_px=8+column*156; y_px=page+y;
        if (y+height>extent) extent=y+height;
        y+=height;
    }
};
inline void formatOverview(const Value &v,char *out,size_t cap) {
    if (!flagsOnly(v) || !v.complete) { formatValue(v,out,cap); return; }
    char binary[9];
    for (unsigned i=0;i<8;++i) binary[i]=(v.word&(1u<<(7-i)))?'1':'0';
    binary[8]=0;
    snprintf(out,cap,"%s%s",v.success?"":"FAIL ",binary);
}
}
