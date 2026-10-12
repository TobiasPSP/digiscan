#define TABLET_PREVIEW
#include "preview_platform.h"
#include <new>
#include "profile_disk.h"
#include "profile_storage.h"
#include "active_plan.h"
#include <assert.h>
static std::vector<uint8_t> readFile(const char *root,const char *name){
    char path[2048];snprintf(path,sizeof(path),"%s/%s.i2cp",root,name);auto *f=fopen(path,"rb");assert(f);
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<uint8_t>b(n);assert(fread(b.data(),1,n,f)==unsigned(n));fclose(f);return b;
}
static portable::Package *parse(const std::vector<uint8_t> &b){auto *p=new portable::Package;auto *raw=static_cast<uint8_t *>(portable::allocate(b.size()));assert(raw);memcpy(raw,b.data(),b.size());p->parse(raw,b.size());return p;}
static void checksum(std::vector<uint8_t> &b){auto c=portable::crc32(b.data()+24,b.size()-24);for(unsigned i=0;i<4;++i)b[12+i]=uint8_t(c>>(i*8));}
int main(int argc,char **argv){assert(argc==2);portable::Disk disk;disk.begin();assert(disk.ready);
    std::vector<uint8_t> sample;
    for(const char *id:{"ip2366","ip2368","ip2369"}){
        auto bytes=readFile(argv[1],id);auto *p=parse(bytes);if(p->error)fprintf(stderr,"%s: %s\n",id,p->error);assert(!p->error);
        int slot=portable::find(id);assert(slot>=0);const auto &old=decoding::profiles[slot];assert(p->profile.count==old.count&&p->profile.revision==old.revision);
        for(unsigned i=0;i<old.count;++i){const auto &a=decoding::definition(old,i),&b=p->profile.table[i];
            assert(!strcmp(a.id,b.id)&&!strcmp(a.name,b.name)&&!strcmp(a.unit,b.unit)&&!strcmp(a.access,b.access));
            assert(a.low==b.low&&a.high==b.high&&a.count==b.count&&a.mask==b.mask&&a.shift==b.shift&&a.minimum==b.minimum&&a.maximum==b.maximum);
            assert(a.scale==b.scale&&a.offset==b.offset&&a.atomic_step==b.atomic_step&&a.precision==b.precision);
            assert(a.writable==b.writable&&a.activeReadable==b.activeReadable&&a.ascii==b.ascii&&a.conflict==b.conflict&&a.unsignedCurrent==b.unsignedCurrent);
            assert(a.enumCount==b.enumCount&&bool(a.bit0)==bool(b.bit0));
            if(a.bit0)assert(!strcmp(a.bit0,b.bit0)&&!strcmp(a.bit1,b.bit1));
            for(unsigned e=0;e<a.enumCount;++e)assert(a.enums[e].code==b.enums[e].code&&!strcmp(a.enums[e].text,b.enums[e].text));
        }
        assert(p->pageCount==2&&p->graphCount==1&&p->filterCount==1);
        assert(disk.commit(*p));assert(portable::install(p));if(!strcmp(id,"ip2366"))sample=bytes;
    }
    decoding::Registry registry;registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual);
    analysis::Decoder decoder;observer::Observation o;o.address=0x75;o.reg=0x3a;o.direction='R';o.associated=o.success=true;o.dataBytes=1;o.preview[0]=49;
    decoder.add(o,100);bool temperature=false;for(unsigned i=0;i<decoding::PropertyCount;++i)if(decoder.used[i]&&!strcmp(decoding::properties[i].unit,"degC")){assert(decoder.values[i].word==49);temperature=true;}assert(temperature);
    reporting::Model report;for(const auto &page:reporting::pages)if(!strcmp(page.profileId,"ip2366"))assert(report.build(page,decoding::properties,decoding::PropertyCount,0x75));
    decoding::ProfileStorage saved;registry.bindings[0x75].dirty=true;saved.load(registry);saved.save(registry,true);
    // Same chip/revision replacement: runtime strings/scales come from the package.
    auto updated=sample;unsigned versionAt=24+2+strlen("ip2366")+1+2+strlen("IP2366 v1.0")+1;updated[versionAt]=3;checksum(updated);
    auto *next=parse(updated);assert(!next->error&&next->profile.revision==3);assert(disk.commit(*next));
    auto *a=disk.read("/ip2366.a"),*b=disk.read("/ip2366.b");assert(a&&b&&a->generation!=b->generation);delete a;delete b;
    // An interrupted inactive-slot write preserves the previous valid version.
    LittleFS.writeBudget=100;assert(!disk.commit(*next));LittleFS.writeBudget=-1;
    a=disk.read("/ip2366.a");b=disk.read("/ip2366.b");auto *best=a&&(!b||a->generation>b->generation)?a:b;assert(best&&best->profile.revision==3);delete a;delete b;delete next;
    // Load the stored replacements through the real boot publication path.
    for(auto *&p:portable::installed)p=nullptr;disk.load();assert(decoding::profiles[registry.find("ip2366")].revision==3);
    decoding::Registry restored;saved.load(restored);assert(restored.bindings[0x75].profile==restored.find("ip2366"));
    // A genuinely new fourth profile requires no compile-time descriptors.
    auto fourth=sample;memcpy(fourth.data()+26,"test66",6);checksum(fourth);auto *extra=parse(fourth);assert(!extra->error&&portable::install(extra));
    assert(decoding::ProfileCount==4&&restored.assign(0x40,restored.find("test66"),decoding::Assignment::Manual));
    bool relocated=false;for(auto &p:decoding::properties)if(p.count&&p.address==0x40){assert(p.profile==unsigned(restored.find("test66")));relocated=true;}assert(relocated);
    for(unsigned cut=0;cut<sample.size();cut+=127){std::vector<uint8_t> shortFile(sample.begin(),sample.begin()+cut);auto *bad=parse(shortFile);assert(bad->error);delete bad;}
    uint32_t random=12345;unsigned rejected=0;
    for(unsigned trial=0;trial<2000;++trial){auto corrupt=sample;random=random*1664525u+1013904223u;unsigned at=24+random%(corrupt.size()-24);corrupt[at]^=uint8_t(1u<<(trial%8));
        if(trial%2)checksum(corrupt);auto *p=parse(corrupt);if(p->error)++rejected;delete p;}
    assert(rejected>=1000);
    puts("PASS: all 283 properties roundtrip; units/masks/booleans/active plans; runtime reports/graphs/presets; replacement and fourth profile; persisted ID association; inactive-slot failure recovery; truncations and 2000 corruptions");
}
