#pragma once
// Bounded owned recording buffers and post-playback observations. Entries carry
// complete payloads so later traffic cannot overwrite recorded writes. Identical
// writes are meaningful events. Formatting scratch is long-lived (PSRAM on the
// tablet), not a multi-kilobyte temporary on the small Arduino task stack.
#include "observations.h"
#include "recorder_wire.h"
namespace recording18 {
constexpr unsigned MaxEntries=256;
struct Bytes {uint16_t count=0;uint8_t data[256]={};bool known=false;};
struct Entry {
    observer::Observation transaction;
    recorder_wire::Timing timing;
    Bytes after;
    bool checked=true,replayed=false;
    uint32_t actualDelay_us=0;
};
// The UI runs on the 8 KB Arduino task stack. Keep row scratch space in the
// PSRAM-backed session, not in nested LVGL/formatting call frames.
struct RenderScratch {
    char value[800]={},title[1000]={},left[1000]={},right[1000]={},timing[256]={};
};
class Session {
public:
    RenderScratch render;
    uint8_t selected[128][32]={};unsigned keyCount=0;
    Entry entries[MaxEntries];unsigned count=0;
    bool recording=false,complete=true,haveTiming=false;
    uint32_t epoch=0,hz=0,revision=0,session=0;
    uint64_t lastStart=0;
    const char *message="Select ALL or registers, then START";
    int find(uint8_t a,uint8_t r)const{return a<128 && (selected[a][r/8]&(1u<<(r%8)))?0:-1;}
    bool addKey(uint8_t a,uint8_t r){
        if(a>=128)return false;if(find(a,r)<0){selected[a][r/8]|=uint8_t(1u<<(r%8));++keyCount;}return true;
    }
    void reset(){memset(selected,0,sizeof(selected));keyCount=count=0;recording=false;complete=true;haveTiming=false;++revision;++session;}
    void start(){recording=true;haveTiming=false;message="Recording WRITES";++revision;}
    void stop(const char *why){recording=false;message=why;++revision;}
    void gap(){if(recording){complete=false;stop("Capture/output gap / recording incomplete");}}
    void observe(const observer::Observation &o,const recorder_wire::Timing *t,uint32_t){
        if(!recording)return;
        // Real writes can precede a read in a compound chain. Pointer-only frames are excluded.
        size_t at=10;
        for(unsigned f=0;f<o.raw[8];++f){
            unsigned bytes=bringup::le16(o.raw+at);size_t data=at+5,next=data+2*bytes;
            if(bytes<3 || (o.raw[data]&1)){at=next;continue;}
            uint8_t address=o.raw[data]>>1,reg=o.raw[data+2];if(find(address,reg)<0){at=next;continue;}
            if(!t || !t->valid || t->count!=o.raw[8]){complete=false;stop("Missing/invalid capture timestamp");return;}
            if(haveTiming && (epoch!=t->epoch || hz!=t->hz || t->start[f]<lastStart)){gap();return;}
            haveTiming=true;epoch=t->epoch;hz=t->hz;lastStart=t->start[f];
            if(bytes>258){complete=false;stop("Unsupported write length");return;}
            if(count==MaxEntries){stop("Recorder full / 256 writes retained");return;}
            uint8_t raw[531];memcpy(raw,o.raw,10);raw[5]=1;raw[6]=address;raw[7]=reg;raw[8]=1;
            bool ok=!o.raw[9] && !o.raw[at+3] && !o.raw[at+4];for(unsigned n=0;n<bytes;++n)ok &= o.raw[data+2*n+1]!=0;
            raw[4]=ok;memcpy(raw+10,o.raw+at,5+2*bytes);auto &e=entries[count];
            if(!observer::parse(raw,15+2*bytes,e.transaction)){gap();return;}
            e.timing=recorder_wire::Timing{};e.timing.valid=true;e.timing.epoch=epoch;e.timing.hz=hz;e.timing.count=1;e.timing.start[0]=t->start[f];e.timing.end[0]=t->end[f];
            e.after.count=uint16_t(bytes-2);e.after.known=true;for(unsigned n=0;n<e.after.count;++n)e.after.data[n]=o.raw[data+4+2*n];
            e.checked=ok && o.raw[at+2]==0;e.replayed=false;e.actualDelay_us=0;++count;++revision;
            if(count==MaxEntries){stop("Recorder full / 256 writes retained");return;}at=next;
        }
    }
    uint64_t microseconds(uint64_t ticks)const{return hz?(ticks/hz)*1000000+(ticks%hz)*1000000/hz:0;}
};
// Only wire shapes reproducible by the verified active-transfer backend qualify.
inline bool replayable(const Entry &e){
    const auto &o=e.transaction;if(!o.success || !o.associated || !o.dataBytes || !e.timing.valid)return false;
    return o.direction=='W' && o.raw[8]==1 && o.dataBytes<=256 && !o.raw[9] && o.raw[12]==0;
}
constexpr unsigned PostCapacity=64;
struct PostWrite {
    uint8_t raw[531]={};uint16_t length=0;
    uint64_t start_cycles=0;uint32_t pio_Hz=0,arrival_ms=0;
};
class PostCapture {
public:
    PostWrite entries[PostCapacity];Entry preview;
    uint8_t watched[128][32]={};
    unsigned count=0,dropped=0,gaps=0;bool active=false,haveEpoch=false,cancelled=false;
    uint32_t started_ms=0,epoch=0;
    void begin(uint32_t now_ms){count=dropped=gaps=0;haveEpoch=cancelled=false;active=true;started_ms=now_ms;memset(watched,0,sizeof(watched));}
    void select(uint8_t address,uint8_t reg){if(address<128)watched[address][reg/8]|=uint8_t(1u<<(reg%8));}
    bool includes(uint8_t address,uint8_t reg)const{return address<128 && (watched[address][reg/8]&(1u<<(reg%8)));}
    void gap(){if(active)++gaps;}
    void observe(const observer::Observation &o,const recorder_wire::Timing *t,uint32_t now_ms){
        if(!active)return;size_t at=10;
        for(unsigned f=0;f<o.raw[8];++f){
            unsigned bytes=bringup::le16(o.raw+at);size_t data=at+5,next=data+2*bytes;
            if(bytes<3 || bytes>258 || (o.raw[data]&1)){at=next;continue;}
            if(!includes(o.raw[data]>>1,o.raw[data+2])){at=next;continue;}
            bool timed=t && t->valid && t->count==o.raw[8];
            if(timed){
                if(haveEpoch && epoch!=t->epoch){gap();return;}
                haveEpoch=true;epoch=t->epoch;
                // RP V12 resets cycle timestamps to zero on capture restart.
                if(t->start[f]>=uint64_t(t->hz)*5){at=next;continue;}
            }else if(uint32_t(now_ms-started_ms)>=5000){at=next;continue;}
            if(count==PostCapacity){++dropped;at=next;continue;}
            auto &e=entries[count++];e.length=uint16_t(15+2*bytes);
            memcpy(e.raw,o.raw,10);e.raw[5]=1;e.raw[6]=o.raw[data]>>1;e.raw[7]=o.raw[data+2];e.raw[8]=1;
            bool ok=!o.raw[9] && !o.raw[at+3] && !o.raw[at+4];
            for(unsigned n=0;n<bytes;++n)ok &= o.raw[data+2*n+1]!=0;
            e.raw[4]=ok;memcpy(e.raw+10,o.raw+at,5+2*bytes);
            e.start_cycles=timed?t->start[f]:0;e.pio_Hz=timed?t->hz:0;e.arrival_ms=now_ms-started_ms;
            at=next;
        }
    }
    const Entry &detail(unsigned i){
        const auto &e=entries[i];observer::parse(e.raw,e.length,preview.transaction);
        preview.after.count=uint16_t(bringup::le16(e.raw+10)-2);preview.after.known=true;
        for(unsigned n=0;n<preview.after.count;++n)preview.after.data[n]=e.raw[19+2*n];
        return preview;
    }
};
}
