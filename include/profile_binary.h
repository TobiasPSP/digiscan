#pragma once
// Untrusted IPRF v1 input boundary. Check header, length, CRC, counts and references
// before installing a package. Explicit little-endian reads avoid ABI/padding
// assumptions. Packages own byte storage and arenas referenced by runtime catalogs;
// installed packages must remain alive until reboot. Unknown features are rejected.
#include "generated_profiles.h"
#include "generated_reports.h"
#include "generated_experiments.h"
#include <math.h>
#include <stdlib.h>
namespace portable {
constexpr unsigned MaxBytes=384*1024, ArenaBytes=512*1024;
inline void *allocate(size_t size){
#ifdef TABLET_PREVIEW
    return calloc(1,size);
#else
    return heap_caps_calloc(1,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#endif
}
inline uint32_t u32(const uint8_t *p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline uint32_t crc32(const uint8_t *p,unsigned n){uint32_t crc=~0u;while(n--){crc^=*p++;for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));}return ~crc;}
struct Reader {
    // Text borrows the original encoded byte buffer; descriptor arrays are
    // aligned within the separate arena. Both buffers belong to Package.
    // The first error is sticky so a later failed read cannot hide its cause.
    const uint8_t *data; unsigned size,pos=24; uint8_t *arena; unsigned used=0,capacity; const char *error=nullptr;
    Reader(const uint8_t *p,unsigned n,uint8_t *a,unsigned cap):data(p),size(n),arena(a),capacity(cap){}
    void fail(const char *why){if(!error)error=why;}
    // Subtraction after checking pos prevents unsigned addition overflow from
    // turning an oversized/truncated field into an apparently valid pointer.
    const uint8_t *take(unsigned n){if(error || pos>size || n>size-pos){fail("truncated");return nullptr;}auto *p=data+pos;pos+=n;return p;}
    unsigned byte(){auto *p=take(1);return p?*p:0;}
    unsigned word(){auto *p=take(2);return p?unsigned(p[0])|(unsigned(p[1])<<8):0;}
    uint32_t dword(){auto *p=take(4);return p?u32(p):0;}
    double number(){double v=0;auto *p=take(8);if(p)memcpy(&v,p,8);if(!isfinite(v))fail("nonfinite_number");return v;}
    unsigned count(unsigned max){unsigned n=word();if(n>max)fail("capacity");return error?0:n;}
    template<class T>T *array(unsigned n){
        unsigned aligned=(used+alignof(T)-1)&~unsigned(alignof(T)-1);
        if(error || aligned>capacity || n>(capacity-aligned)/sizeof(T)){fail("arena_capacity");return nullptr;}
        auto *p=reinterpret_cast<T *>(arena+aligned);used=aligned+n*sizeof(T);return p;
    }
    const char *text(unsigned max=4096){
        unsigned n=word();if(!n || n>max+1){fail("text_length");return "";}
        auto *p=take(n);if(!p)return "";
        if(p[n-1] || memchr(p,0,n-1)){fail("text_termination");return "";}
        // Reject malformed UTF-8 before passing profile strings to LVGL.
        for(unsigned i=0;i<n-1;){uint8_t c=p[i++];if(c<128)continue;unsigned tail=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:0;
            if(!tail || i+tail>n-1){fail("utf8");break;}uint8_t first=p[i];
            if((c==0xe0&&first<0xa0)||(c==0xed&&first>=0xa0)||(c==0xf0&&first<0x90)||(c==0xf4&&first>=0x90)){fail("utf8");break;}
            while(tail--)if((p[i++]&0xc0)!=0x80){fail("utf8");break;}
        }
        return reinterpret_cast<const char *>(p);
    }
};
inline bool validId(const char *s){unsigned n=strlen(s);if(!n||n>48||s[0]<'a'||s[0]>'z')return false;for(;*s;++s)if(!(*s>='a'&&*s<='z')&&!(*s>='0'&&*s<='9')&&*s!='_'&&*s!='.'&&*s!='-')return false;return true;}
struct Package {
    uint8_t *bytes=nullptr,*arena=nullptr; unsigned size=0;
    decoding::Profile profile{}; reporting::Page *pages=nullptr;unsigned pageCount=0;
    experiment::Rule *rules=nullptr;unsigned ruleCount=0;
    experiment::Graph *graphs=nullptr;unsigned graphCount=0;
    experiment::FilterList *filters=nullptr;unsigned filterCount=0;
    const char *metadata=nullptr,*error=nullptr; uint32_t crc=0,generation=0;
    ~Package(){free(bytes);free(arena);}
    bool resolves(const char *s,bool allowAll)const{
        reporting::Reference ref;if(!reporting::parse(s,ref))return false;if(ref.all)return allowAll;
        unsigned matches=0;for(unsigned i=0;i<profile.count;++i){auto &p=profile.table[i];
            if(p.low==ref.reg&&p.mask==ref.mask&&p.count*8==strlen(s)-5)++matches;}
        return matches==1;
    }
    bool parse(uint8_t *owned,unsigned n){
        // Ownership transfers even on rejection: the caller must not free owned
        // after parse(), and deleting the Package releases both backing buffers.
        // Validate framing and checksum before constructing any descriptors.
        bytes=owned;size=n;
        if(n<24||n>MaxBytes||memcmp(bytes,"IPRF",4)||bytes[4]!=1||bytes[5]||bytes[6]!=24||bytes[7]||u32(bytes+8)!=n||u32(bytes+16)||u32(bytes+20)){error="header_or_version";return false;}
        crc=u32(bytes+12);if(crc32(bytes+24,n-24)!=crc){error="crc";return false;}
        unsigned arenaSize=n*2+16384;if(arenaSize>ArenaBytes)arenaSize=ArenaBytes;
        arena=static_cast<uint8_t *>(allocate(arenaSize));if(!arena){error="memory";return false;}
        Reader r(bytes,n,arena,arenaSize);profile.id=r.text(48);profile.name=r.text(80);profile.revision=r.word();
        if(!validId(profile.id)||!*profile.name||strchr(profile.name,'\n')||strchr(profile.name,'\r')||!profile.revision)r.fail("device_metadata");
        profile.addressCount=r.byte();if(!profile.addressCount||profile.addressCount>112)r.fail("addresses");
        auto *addresses=r.take(profile.addressCount);profile.typicalAddresses=addresses;
        if(addresses)for(unsigned i=0;i<profile.addressCount;++i){if(addresses[i]<8||addresses[i]>=120)r.fail("address");for(unsigned j=0;j<i;++j)if(addresses[j]==addresses[i])r.fail("duplicate_address");}
        profile.readProtocol=r.byte();if(profile.readProtocol>1)r.fail("read_protocol");profile.source=r.text();
        profile.count=r.count(512);if(!profile.count)r.fail("empty_properties");auto *properties=r.array<decoding::Property>(profile.count);profile.table=properties;
        for(unsigned i=0;i<profile.count&&!r.error;++i){auto &p=properties[i];
            p.id=r.text(96);p.name=r.text(192);p.unit=r.text(64);p.note=r.text();p.bit0=r.text(8);p.bit1=r.text(8);
            p.meaning0=r.text();p.meaning1=r.text();p.description=r.text();p.friendlydesc=r.text(2048);p.access=r.text(64);
            p.address=r.byte();p.low=r.byte();p.high=r.byte();p.count=r.byte();p.shift=r.byte();p.precision=r.byte();
            p.mask=r.dword();p.minimum=r.dword();p.maximum=r.dword();p.scale=r.number();p.offset=r.number();p.atomic_step=r.number();
            unsigned flags=r.byte();p.writable=flags&1;p.ascii=flags&2;p.conflict=flags&4;p.unsignedCurrent=flags&8;p.activeReadable=flags&16;
            bool bits=flags&32;if(!bits){p.bit0=p.bit1=p.meaning0=p.meaning1=nullptr;}
            p.enumCount=r.count(256);auto *enums=r.array<decoding::EnumValue>(p.enumCount);p.enums=enums;
            for(unsigned j=0;j<p.enumCount&&!r.error;++j){enums[j].code=r.dword();enums[j].text=r.text(192);for(unsigned k=0;k<j;++k)if(enums[k].code==enums[j].code)r.fail("duplicate_enum");}
            if(!validId(p.id)||!*p.name||p.address<8||p.address>=120||p.count<1||p.count>3||p.low+unsigned(p.count)>256||
                (p.count==1?p.high!=0:p.high!=p.low+p.count-1)||p.shift>=p.count*8||p.precision>6||flags>63){r.fail("property_layout");break;}
            uint32_t full=(1u<<(8*p.count))-1,field=p.mask>>p.shift;
            if(!p.mask||(p.mask&~full)||(p.mask&((1u<<p.shift)-1))||(field&(field+1))||p.minimum>p.maximum||p.maximum>field||p.scale<=0||p.atomic_step<0||!isfinite(p.maximum*p.scale+p.offset)||
               (p.activeReadable&&!profile.readProtocol)||(p.ascii&&p.count!=1)||(p.writable&&p.count!=1))r.fail("property_conversion");
            if(bits && (field!=1||p.count!=1||p.enumCount||!p.bit0||!p.bit1||strcmp(p.bit0,p.bit1)==0||
                (strcmp(p.bit0,"YES")&&strcmp(p.bit0,"NO"))||(strcmp(p.bit1,"YES")&&strcmp(p.bit1,"NO"))))r.fail("boolean");
            if(p.atomic_step>0&&(bits||p.enumCount||p.ascii||p.conflict))r.fail("atomic_step");
            for(unsigned j=0;j<p.enumCount;++j)if(enums[j].code>field)r.fail("enum_range");
            for(unsigned j=0;j<i;++j){auto &q=properties[j];if(!strcmp(p.id,q.id))r.fail("duplicate_property");
                if(p.low<=q.low+q.count-1&&q.low<=p.low+p.count-1&&(p.count>1||q.count>1||(p.mask&q.mask)))r.fail("overlapping_properties");}
        }
        pageCount=r.count(8);pages=r.array<reporting::Page>(pageCount);
        for(unsigned i=0;i<pageCount&&!r.error;++i){auto &p=pages[i];p.profileId=profile.id;p.header=r.text(192);p.count=r.count(16);auto *groups=r.array<reporting::Group>(p.count);p.groups=groups;
            if(!*p.header||!p.count)r.fail("report");for(unsigned j=0;j<p.count&&!r.error;++j){auto &g=groups[j];g.header=r.text(192);g.count=r.count(512);auto *items=r.array<const char *>(g.count);g.items=items;
                if(!g.count)r.fail("report_items");for(unsigned k=0;k<g.count&&!r.error;++k){items[k]=r.text(40);if(!r.error&&!resolves(items[k],true))r.fail("report_reference");}
            }
        }
        ruleCount=r.count(32);rules=r.array<experiment::Rule>(ruleCount);
        for(unsigned i=0;i<ruleCount&&!r.error;++i){auto &v=rules[i];v.profile=profile.id;v.label=r.text(192);v.reg=r.byte();v.mask=r.byte();v.min=r.byte();v.max=r.byte();unsigned kind=r.byte();
            if(!*v.label||!v.mask||v.min>v.max||kind>2)r.fail("identification");v.kind=static_cast<identification::Class>(kind);}
        graphCount=r.count(8);graphs=r.array<experiment::Graph>(graphCount);
        for(unsigned i=0;i<graphCount&&!r.error;++i){auto &g=graphs[i];g.profile=profile.id;g.title=r.text(192);g.count=r.byte();if(!*g.title||!g.count||g.count>8){r.fail("graph");break;}
            auto *lines=r.array<experiment::GraphLine>(g.count);g.lines=lines;for(unsigned j=0;j<g.count&&!r.error;++j){auto &l=lines[j];l.reference=r.text(40);l.min=r.number();l.max=r.number();l.color=r.dword();unsigned area=r.byte();l.area=area;
                if(!r.error&&(!resolves(l.reference,false)||l.min>=l.max||l.color>0xffffff||area>1))r.fail("graph_line");}}
        filterCount=r.count(8);filters=r.array<experiment::FilterList>(filterCount);
        for(unsigned i=0;i<filterCount&&!r.error;++i){auto &f=filters[i];f.profile=profile.id;f.name=r.text(192);f.count=r.count(128);f.registers=r.take(f.count);if(!*f.name||!f.count)r.fail("filter");
            if(f.registers)for(unsigned j=0;j<f.count;++j)for(unsigned k=0;k<j;++k)if(f.registers[k]==f.registers[j])r.fail("duplicate_filter_register");}
        metadata=r.text(16384);if(r.pos!=n)r.fail("trailing_bytes");error=r.error;return !error;
    }
};
static Package *installed[decoding::MaxProfiles]{};
template<class T,unsigned N>inline unsigned excluding(const decoding::Table<T,N> &t,const char *id){unsigned n=0;for(const auto &v:t)if(strcmp(v.profile,id))++n;return n;}
template<class T,unsigned N>inline void erase(decoding::Table<T,N> &t,const char *id){unsigned n=0;for(auto &v:t)if(strcmp(v.profile,id))t.entries[n++]=v;t.count=n;}
inline int find(const char *id){for(unsigned i=0;i<decoding::ProfileCount;++i)if(!strcmp(decoding::profiles[i].id,id))return int(i);return -1;}
inline const char *capacity(const Package &p){int slot=find(p.profile.id);if(slot<0&&decoding::ProfileCount>=decoding::MaxProfiles)return "profile_capacity";
    unsigned pages=0;for(auto &v:reporting::pages)if(strcmp(v.profileId,p.profile.id))++pages;
    if(pages+p.pageCount>64||excluding(experiment::rules,p.profile.id)+p.ruleCount>256||excluding(experiment::graphs,p.profile.id)+p.graphCount>64||excluding(experiment::filterLists,p.profile.id)+p.filterCount>64)return "catalog_capacity";return nullptr;}
// Boot-only publication: live decoded values and LVGL objects must never outlive replaced strings.
inline bool install(Package *p){if(p->error||capacity(*p))return false;int slot=find(p->profile.id);if(slot<0)slot=decoding::profiles.count++;
    unsigned n=0;for(auto &v:reporting::pages)if(strcmp(v.profileId,p->profile.id))reporting::pages.entries[n++]=v;reporting::pages.count=n;
    erase(experiment::rules,p->profile.id);erase(experiment::graphs,p->profile.id);erase(experiment::filterLists,p->profile.id);
    for(unsigned i=0;i<p->pageCount;++i)reporting::pages.entries[reporting::pages.count++]=p->pages[i];
    for(unsigned i=0;i<p->ruleCount;++i)experiment::rules.entries[experiment::rules.count++]=p->rules[i];
    for(unsigned i=0;i<p->graphCount;++i)experiment::graphs.entries[experiment::graphs.count++]=p->graphs[i];
    for(unsigned i=0;i<p->filterCount;++i)experiment::filterLists.entries[experiment::filterLists.count++]=p->filters[i];
    decoding::DefinitionCount-=decoding::profiles[slot].count;decoding::DefinitionCount+=p->profile.count;
    decoding::profiles[slot]=p->profile;installed[slot]=p;return true;}
}
