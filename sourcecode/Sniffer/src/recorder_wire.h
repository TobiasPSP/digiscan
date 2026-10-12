#pragma once
#include <stdint.h>
#include <stddef.h>
namespace recorder_wire {
constexpr unsigned MaxFrames=8, Header=12;
inline uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(8*i);return v;}
inline void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;++i)p[i]=uint8_t(v>>(8*i));}
struct Timing {
    uint32_t epoch=0,hz=0;unsigned count=0;
    uint64_t start[MaxFrames]={},end[MaxFrames]={};
    bool valid=false;
};
inline bool read(const uint8_t *p,size_t n,Timing &t){
    t=Timing{};
    if(n<Header || p[0]!=1 || p[1]>1 || !p[2] || p[2]>MaxFrames || p[3])return false;
    t.count=p[2];t.epoch=uint32_t(get64(p+4)&0xffffffffu);
    t.hz=uint32_t(p[8])|(uint32_t(p[9])<<8)|(uint32_t(p[10])<<16)|(uint32_t(p[11])<<24);
    if(!t.hz || n<Header+16*t.count)return false;
    for(unsigned i=0;i<t.count;++i){
        t.start[i]=get64(p+Header+16*i);t.end[i]=get64(p+Header+16*i+8);
        if(t.end[i]<t.start[i] || (i && t.start[i]<t.end[i-1]))return false;
    }
    t.valid=p[1];return true;
}
}
