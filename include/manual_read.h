#pragma once
// Validate user-entered byte registers, read lengths and raw numeric display.
// Multi-byte signedness/endianness is an explicit user choice, never inferred
// from a register name. Results own their payload bytes up to the supported bound.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
namespace manual_read {
constexpr unsigned MaxRegisters=32;
inline bool registers(const char *s,uint8_t *out,unsigned &count){
    count=0;if(!s)return false;
    while(*s){
        while(*s==' ' || *s=='\t')++s;
        if(!*s || *s=='-')return false;
        char *end=nullptr;unsigned long low=strtoul(s,&end,16);
        if(end==s || low>255)return false;s=end;while(*s==' ' || *s=='\t')++s;
        unsigned long high=low;
        if(*s=='-'){++s;while(*s==' ' || *s=='\t')++s;if(!*s || *s=='-')return false;
            high=strtoul(s,&end,16);if(end==s || high>255 || high<low)return false;s=end;}
        for(unsigned r=unsigned(low);r<=high;++r){bool found=false;for(unsigned i=0;i<count;++i)found|=out[i]==r;
            if(!found){if(count==MaxRegisters)return false;out[count++]=uint8_t(r);}}
        while(*s==' ' || *s=='\t')++s;
        if(!*s)break;if(*s!=',')return false;++s;if(!*s)return false;
    }
    return count!=0;
}
inline bool length(const char *s,unsigned &n){
    if(!s || !*s)return false;n=0;
    for(;*s;++s){if(*s<'0' || *s>'9')return false;n=n*10+unsigned(*s-'0');if(n>256)return false;}
    return n!=0;
}
inline uint64_t word(const uint8_t *data,unsigned count,bool big){
    uint64_t value=0;for(unsigned i=0;i<count && i<8;++i)value|=uint64_t(data[i])<<(8*(big?count-1-i:i));return value;
}
struct Result {uint8_t reg=0,code=0;uint16_t count=0;bool complete=false,restored=false,ok=false;uint8_t bytes[256]={};};
}
