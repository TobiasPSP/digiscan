#pragma once
#ifndef I2C_TRANSPORT_V5_H
#define I2C_TRANSPORT_V5_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace transport5 {
constexpr size_t MaxRecord=2380, MaxBatch=4608;
inline bool rateAllowed(uint32_t baud) { return baud==115200 || baud==1000000 || baud==2000000; }
inline uint16_t u16(const uint8_t *p) { return p[0] | (uint16_t(p[1])<<8); }
inline uint32_t u32(const uint8_t *p) { return u16(p) | (uint32_t(u16(p+2))<<16); }
inline void put16(uint8_t *p,unsigned v) { p[0]=uint8_t(v); p[1]=uint8_t(v>>8); }
// Convert canonical V1 transaction payload to compact form. Nothing is inferred:
// all captured bytes, ACKs, boundaries and error flags remain representable.
inline size_t pack(const uint8_t *in,size_t n,uint8_t *out,size_t cap) {
    if (n<10 || in[4]>1 || in[5]>1 || in[9]>1 || !in[8] || in[8]>8 || cap<8) return 0;
    memcpy(out,in,4); out[4]=in[4]|(in[5]<<1)|(in[9]<<2);
    out[5]=in[6]; out[6]=in[7]; out[7]=in[8];
    size_t at=10, used=8;
    for (unsigned f=0;f<in[8];++f) {
        if (n-at<5) return 0;
        unsigned count=u16(in+at); size_t ackBytes=(count+7)/8;
        if (count>258 || in[at+2]>1 || in[at+3]>1 || count*2>n-at-5 || 4+count+ackBytes>cap-used) return 0;
        put16(out+used,count); out[used+2]=in[at+2]|(in[at+3]<<1); out[used+3]=in[at+4];
        at+=5; used+=4; memset(out+used+count,0,ackBytes);
        for (unsigned i=0;i<count;++i) {
            if (in[at+2*i+1]>1) return 0;
            out[used+i]=in[at+2*i];
            out[used+count+i/8]|=uint8_t(in[at+2*i+1]<<(i%8));
        }
        at+=count*2; used+=count+ackBytes;
    }
    return at==n?used:0;
}
inline size_t unpack(const uint8_t *in,size_t n,uint8_t *out,size_t cap) {
    if (n<8 || in[4]&~7u || !in[7] || in[7]>8 || cap<10 || ((in[4]&2) && in[5]>=128)) return 0;
    memcpy(out,in,4); out[4]=in[4]&1; out[5]=(in[4]>>1)&1;
    out[6]=in[5]; out[7]=in[6]; out[8]=in[7]; out[9]=(in[4]>>2)&1;
    size_t at=8, used=10;
    for (unsigned f=0;f<in[7];++f) {
        if (n-at<4) return 0;
        unsigned count=u16(in+at); size_t ackBytes=(count+7)/8;
        if (count>258 || in[at+2]&~3u || count+ackBytes>n-at-4 || 5+2*count>cap-used) return 0;
        put16(out+used,count); out[used+2]=in[at+2]&1; out[used+3]=(in[at+2]>>1)&1; out[used+4]=in[at+3];
        at+=4; used+=5;
        if (count%8 && (in[at+count+ackBytes-1]>>(count%8))) return 0;
        for (unsigned i=0;i<count;++i) { out[used+2*i]=in[at+i]; out[used+2*i+1]=(in[at+count+i/8]>>(i%8))&1; }
        at+=count+ackBytes; used+=2*count;
    }
    return at==n?used:0;
}
// Compact filter: opcode, flags, R/W bits, outcome, 128 address bits, 256 register bits.
struct Filter {
    bool enabled=false;
    uint8_t flags=0, directions=3, outcome=0, addresses[16]={}, registers[32]={};
    uint32_t generation=0;
    bool configure(const uint8_t *p,size_t n) {
        if (n!=52 || p[0]!=8 || p[1]&~15u || !p[2] || p[2]>3 || p[3]>2) return false;
        flags=p[1]; directions=p[2]; outcome=p[3];
        memcpy(addresses,p+4,16); memcpy(registers,p+20,32); enabled=true; ++generation; return true;
    }
    bool accepts(bool success,bool associated,uint8_t address,uint8_t reg,char direction) const {
        if (!enabled) return true;
        if ((outcome==1 && !success) || (outcome==2 && success)) return false;
        if (direction=='R' ? !(directions&1) : direction=='W' ? !(directions&2) : directions!=3) return false;
        if ((flags&1) && (address>=128 || !(addresses[address/8]&(1u<<(address%8))))) return false;
        if (!(flags&2)) return true;
        return associated ? bool(registers[reg/8]&(1u<<(reg%8))) : (!success && (flags&4));
    }
};
enum class Phase { Idle, Drain, Quiet, Confirm, Leased };
enum class RateFailure : uint32_t { None, DrainTimeout, ConfirmTimeout, LeaseTimeout, BaudMismatch };
struct Rate {
    Phase phase=Phase::Idle;
    uint32_t target=115200, token=0, since_ms=0, fallbackCount=0;
    uint32_t attempts=0, lastTarget=115200, lastActual=115200;
    RateFailure failure=RateFailure::None;
    bool wasSubscribed=false;
    bool request(uint32_t id,uint32_t baud,uint32_t now_ms,bool subscribed) {
        if (!id || !rateAllowed(baud) || changing()) return false;
        if (attempts!=UINT32_MAX) ++attempts;
        failure=RateFailure::None; lastTarget=baud;
        target=baud; token=id; since_ms=now_ms; wasSubscribed=subscribed; phase=Phase::Drain; return true;
    }
    void switched(uint32_t now_ms) { since_ms=now_ms; phase=Phase::Quiet; }
    bool ready(uint32_t now_ms) {
        if (phase!=Phase::Quiet || uint32_t(now_ms-since_ms)<100) return false;
        since_ms=now_ms; phase=Phase::Confirm; return true;
    }
    bool confirm(uint32_t id,uint32_t now_ms) {
        if (phase!=Phase::Confirm || id!=token) return false;
        phase=target==115200?Phase::Idle:Phase::Leased; since_ms=now_ms; return true;
    }
    void keepalive(uint32_t now_ms) { if (phase==Phase::Leased) since_ms=now_ms; }
    bool expired(uint32_t now_ms) const {
        const uint32_t timeout_ms=phase==Phase::Drain?7000u:phase==Phase::Confirm?1500u:3500u;
        return phase!=Phase::Idle && uint32_t(now_ms-since_ms)>=timeout_ms;
    }
    void fallback(RateFailure why=RateFailure::None) {
        if (fallbackCount!=UINT32_MAX) ++fallbackCount;
        failure=why; target=115200; phase=Phase::Idle;
    }
    void clearDiagnostics() { fallbackCount=attempts=0; failure=RateFailure::None; lastTarget=target; }
    bool changing() const { return phase==Phase::Drain || phase==Phase::Quiet || phase==Phase::Confirm; }
};
}
#endif
