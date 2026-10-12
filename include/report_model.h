#pragma once
// Runtime report reference expansion and persistent per-property readings.
// Cache is independent from Monitor history eviction: an active read must remain
// visible after another property is queried. A later success replaces an earlier
// failure; unseen values are empty. Initial successful acquisition fades once.
#include "analysis.h"
#include "highlight.h"
#include "report_types.h"
#include <stdlib.h>
namespace reporting {
constexpr unsigned Capacity=512;
struct Reference { bool all=false; uint8_t reg=0; uint32_t mask=0; };
inline bool readable(const decoding::Property &p) { return p.count && (!p.access || strcmp(p.access,"W")); }
inline bool parse(const char *s,Reference &r) {
    r=Reference{};
    if (!s) return false;
    if (!strcmp(s,"All")) { r.all=true; return true; }
    if (strlen(s)<7 || s[0]!='0' || s[1]!='x' || s[4]!='_') return false;
    unsigned reg=0;
    for (unsigned i=2;i<4;++i) {
        char c=s[i]; unsigned n=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:16;
        if (n>15) return false;
        reg=reg*16+n;
    }
    unsigned bits=0; uint32_t mask=0;
    for (const char *p=s+5;*p;++p) { if ((*p!='0' && *p!='1') || ++bits>24) return false; mask=(mask<<1)|(*p-'0'); }
    if ((bits!=8 && bits!=16 && bits!=24) || !mask) return false;
    r.reg=uint8_t(reg); r.mask=mask; return true;
}
struct Row { unsigned property,group; };
struct Model {
    Row rows[Capacity]; unsigned count=0; const char *error=nullptr;
    bool build(const Page &page,const decoding::Property *properties,unsigned size,uint8_t address) {
        count=0; error=nullptr;
        if (!page.groups || !page.count || page.count>16) { error="Invalid report groups"; return false; }
        for (unsigned g=0;g<page.count;++g) {
            const auto &group=page.groups[g];
            if (!group.items || !group.count || group.count>512) { error="Invalid report items"; break; }
            for (unsigned item=0;item<group.count;++item) {
                Reference ref; if (!parse(group.items[item],ref)) { error="Invalid register/mask reference"; break; }
                if (ref.all) {
                    unsigned begin=count;
                    for (unsigned p=0;p<size;++p) if (readable(properties[p]) && properties[p].address==address) {
                        if (count==Capacity) { error="Report capacity exceeded"; break; }
                        rows[count++]={p,g};
                    }
                    for (unsigned i=begin+1;i<count;++i) {
                        Row row=rows[i]; unsigned j=i;
                        const auto &p=properties[row.property];
                        while (j>begin) { const auto &q=properties[rows[j-1].property];
                            if (p.low>q.low || (p.low==q.low && p.shift<=q.shift)) break;
                            rows[j]=rows[j-1]; --j;
                        }
                        rows[j]=row;
                    }
                } else {
                    int match=-1;
                    for (unsigned p=0;p<size;++p) if (readable(properties[p]) && properties[p].address==address && properties[p].low==ref.reg && properties[p].mask==ref.mask) {
                        if (match>=0) { error="Ambiguous register/mask reference"; break; } match=int(p);
                    }
                    if (error) break;
                    if (match<0) { error="Property reference not found"; break; }
                    if (count==Capacity) { error="Report capacity exceeded"; break; }
                    rows[count++]={unsigned(match),g};
                }
                if (error) break;
            }
            if (error) break;
        }
        if (error) { count=0; return false; } return true;
    }
};
struct Reading {
    analysis::Value lastGood; uint32_t refreshed_ms=0,revision=0;
    bool valid=false, attempted=false, failed=false;
    bool passiveValid=false;
    uint32_t passiveAt_ms=0;
    uint8_t warnings=0; // 1: profile conflict, 2: range, 4: undefined enumeration.
};
struct Cache {
    Reading readings[decoding::PropertyCount];
    void observeFailure(const observer::Observation &o) {
        if (!o.associated || o.direction!='R' || o.success) return;
        for (unsigned i=0;i<decoding::PropertyCount;++i) {
            const auto &p=decoding::properties[i];
            if (p.count && p.address==o.address && o.reg>=p.low && o.reg<=p.high) {
                readings[i].attempted=true; readings[i].failed=true;++readings[i].revision;
            }
        }
    }
    void clear() { for (auto &r:readings) r=Reading{}; }
    void clearAddress(uint8_t address) {
        for (unsigned i=0;i<decoding::PropertyCount;++i) if (decoding::properties[i].count && decoding::properties[i].address==address) readings[i]=Reading{};
    }
    void accept(const analysis::Value &v,bool passive=false,uint32_t now_ms=0) {
        if (v.property>=decoding::PropertyCount || v.direction!='R') return;
        // An interrupted observation is not evidence of a failed bus read.
        if (v.observationIncomplete || (v.success && !v.complete)) return;
        auto &r=readings[v.property]; r.attempted=true;++r.revision;
        const auto &p=decoding::properties[v.property]; unsigned code=(v.word&p.mask)>>p.shift;
        bool defined=true;
        if (p.enumCount) { defined=false; for (unsigned n=0;n<p.enumCount;++n) defined|=p.enums[n].code==code; }
        r.failed=!v.success || !v.complete;
        if (!r.failed) {
            if(passive){r.passiveValid=true;r.passiveAt_ms=now_ms;}
            r.warnings=(p.conflict?1:0) | (code<p.minimum || code>p.maximum?2:0) | (!defined?4:0);
            if (!r.valid) r.refreshed_ms=v.observed_ms;
            r.lastGood=v; r.valid=true;
        }
    }
};
inline uint32_t background(const Reading &r,uint32_t now) {
    if (r.failed) return 0x8f3038;
    if (!r.valid) return 0x000000;
    if (r.warnings) return 0xe6ca86;
    return highlight::color(0xb8dfbc,0x655024,r.refreshed_ms,now);
}
// A clear horizontal gesture wins over vertical scroll; release never becomes a tap.
struct Swipe {
    bool down=false,horizontal=false,vertical=false; int x=0,y=0;
    void start(int px,int py) { down=true; horizontal=vertical=false; x=px;y=py; }
    void move(int px,int py) {
        if (!down || vertical) return;
        int dx=abs(px-x),dy=abs(py-y);
        if (dy>18 && dy>dx) vertical=true;
        else if (dx>=45 && dx>dy*2) horizontal=true;
    }
    int end(int px,int py) {
        move(px,py); down=false;
        return horizontal?(px<x?1:-1):0;
    }
};
}
