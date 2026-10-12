#pragma once
// Profile-defined experiment descriptors and bounded active-operation helpers.
// Identification A/B/C evidence is categorical, not a comparable numeric score.
// Graph bounds remain in each property's declared physical units. Definition
// references are resolved at runtime so uploaded profiles follow the same path.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdarg.h>
#include "identification.h"
namespace experiment {
enum class Format { Hex, Decimal, Binary, Text, U16LE, U16BE, U32LE, U32BE, FloatLE, FloatBE, I16LE, I16BE, I32LE, I32BE };
inline bool payload(const char *text,Format format,uint8_t *out,unsigned &count) {
    count=0;if (!text || !*text) return false;
    if (format==Format::Text) { count=strlen(text);if(count>256)return false;memcpy(out,text,count);return true; }
    while (*text) {
        while (*text==' ' || *text=='\t' || *text==',' || *text=='\n' || *text=='\r') ++text;
        if (!*text) break;
        const char *end=text;while(*end && *end!=' ' && *end!='\t' && *end!=',' && *end!='\n' && *end!='\r') ++end;
        char token[64];unsigned len=unsigned(end-text);if(!len || len>=sizeof(token))return false;
        memcpy(token,text,len);token[len]=0;char *tail=nullptr;uint32_t word=0;
        bool floating=format==Format::FloatLE || format==Format::FloatBE;
        bool signedInteger=format>=Format::I16LE;
        unsigned bytes=format<=Format::Binary?1:(format<=Format::U16BE || format==Format::I16LE || format==Format::I16BE)?2:4;
        if(floating) { double n=strtod(token,&tail);float v=float(n);if(!isfinite(n)||!isfinite(v))return false;memcpy(&word,&v,4); }
        else if(signedInteger){errno=0;long long n=strtoll(token,&tail,10);long long limit=bytes==2?32767:2147483647LL;
            if(errno==ERANGE || n < -limit-1 || n>limit)return false;word=uint32_t(n);}
        else { if(token[0]=='-')return false;int base=format==Format::Hex?16:format==Format::Binary?2:10;
            const char *start=token;if(base==2 && start[0]=='0' && start[1]=='b')start+=2;
            unsigned long long n=strtoull(start,&tail,base);uint64_t limit=bytes==4?0xffffffffULL:(1ULL<<(bytes*8))-1;
            if(tail==start || n>limit)return false;word=uint32_t(n);
        }
        if(!tail || *tail || tail==token || count+bytes>256)return false;
        bool big=format==Format::U16BE || format==Format::U32BE || format==Format::FloatBE || format==Format::I16BE || format==Format::I32BE;
        for(unsigned i=0;i<bytes;++i)out[count++]=uint8_t(word>>(8*(big?bytes-1-i:i)));
        text=end;
    }
    return count!=0;
}
inline bool refresh(const char *s,uint32_t &ms) {
    if(!s || !*s)return false;unsigned whole=0,digits=0;
    while(*s>='0' && *s<='9') {whole=whole*10+unsigned(*s++-'0');if(++digits>4 || whole>3600)return false;}
    if(!digits)return false;unsigned tenth=0;
    if(*s=='.') {++s;if(*s<'0'||*s>'9')return false;tenth=unsigned(*s++-'0');}
    if(*s || (whole==3600 && tenth))return false;ms=whole*1000+tenth*100;return true;
}
struct Rule { const char *profile,*label;uint8_t reg;uint32_t mask,min,max;identification::Class kind; };
struct GraphLine { const char *reference;double min,max;uint32_t color;bool area; };
struct Graph { const char *profile,*title;const GraphLine *lines;unsigned count; };
struct FilterList { const char *profile,*name;const uint8_t *registers;unsigned count; };
}
