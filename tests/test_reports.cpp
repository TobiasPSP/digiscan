#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "generated_reports.h"
#include "presentation_clock.h"
#include "active_plan.h"
#include "report_batch.h"
static reporting::Cache cache;
static void observe(const analysis::Value &v) { cache.accept(v); }
int main() {
    reporting::Reference ref;
    assert(reporting::parse("All",ref) && ref.all);
    assert(reporting::parse("0x50_1111111111111111",ref) && ref.mask==65535 && ref.reg==0x50);
    for (auto s:{"", "ALL", "0x50_00000000", "0xGG_11111111", "0x50_111", "0x50_1111111Z"}) assert(!reporting::parse(s,ref));
    decoding::Registry registry;
    reporting::Model model;
    for (unsigned chip=0;chip<decoding::ProfileCount;++chip) {
        registry.reset(); assert(registry.assign(0x40,int(chip),decoding::Assignment::Manual));
        unsigned readable=0;
        for (unsigned i=0;i<decoding::PropertyCount;++i) if (decoding::properties[i].count && decoding::properties[i].activeReadable) {
            acquisition::Plan plan;assert(!acquisition::resolve(i,plan));++readable;
            const auto &p=decoding::properties[i];assert(plan.address==0x40 && plan.count==p.count && plan.registers[0]==p.low);
            assert(plan.registers[p.count-1]==p.low+p.count-1);
        }
        assert(readable>50);
        assert(readable==decoding::profiles[chip].count);
        unsigned found=0;
        for (const auto &page:reporting::pages) if (!strcmp(page.profileId,decoding::profiles[chip].id)) {
            assert(model.build(page,decoding::properties,decoding::PropertyCount,0x40)); ++found;
            if (!strcmp(page.header,"Battery")) assert(model.count==3);
            else {
                assert(model.count==decoding::profiles[chip].count);
                for (unsigned i=1;i<model.count;++i) {
                    const auto &a=decoding::properties[model.rows[i-1].property],&b=decoding::properties[model.rows[i].property];
                    assert(a.low<b.low || (a.low==b.low && a.shift>=b.shift));
                }
            }
            for (unsigned i=0;i<model.count;++i) assert(decoding::properties[model.rows[i].property].address==0x40);
            cache.clear();acquisition::Batch batch;
            assert(batch.build(model,cache,true,registry.generation));
            assert(batch.count<=model.count && batch.generation==registry.generation);
            for (unsigned a=0;a<batch.count;++a) {
                const auto &p=decoding::properties[batch.properties[a]];
                for (unsigned b=a+1;b<batch.count;++b) {
                    const auto &q=decoding::properties[batch.properties[b]];
                    assert(p.address!=q.address || p.low!=q.low || p.count!=q.count);
                }
            }
            for (unsigned row=0;row<model.count;++row) {
                analysis::Value value{};value.property=model.rows[row].property;value.direction='R';value.complete=value.success=true;cache.accept(value);
            }
            assert(!batch.build(model,cache,true,registry.generation));
            assert(batch.build(model,cache,false,registry.generation));
            unsigned index=model.rows[0].property;cache.readings[index].failed=true;
            assert(batch.build(model,cache,true,registry.generation) && batch.count==1 && batch.needed(batch.properties[0],cache));
        }
        assert(found==2);
    }
    registry.reset(); assert(registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual));
    unsigned pdoSelect=0;while (pdoSelect<decoding::PropertyCount && strcmp(decoding::properties[pdoSelect].id?decoding::properties[pdoSelect].id:"","r0d_pdo_select")) ++pdoSelect;
    assert(pdoSelect<decoding::PropertyCount);
    acquisition::Plan pdoPlan;assert(!acquisition::resolve(pdoSelect,pdoPlan));
    assert(pdoPlan.address==0x75 && pdoPlan.count==1 && pdoPlan.registers[0]==0x0d);
    auto accessLabel=decoding::properties[pdoSelect].access;
    decoding::properties[pdoSelect].access="Uncertain write access";
    assert(!acquisition::resolve(pdoSelect,pdoPlan)); // Display/write caveats never gate a read.
    decoding::properties[pdoSelect].access=accessLabel;
    const char *items[]={"0x50_1111111111111111"}; reporting::Group group={nullptr,items,1};
    reporting::Page page={"ip2366","Test",&group,1};
    assert(model.build(page,decoding::properties,decoding::PropertyCount,0x75)); unsigned battery=model.rows[0].property;
    auto saved=decoding::properties[battery]; decoding::properties[battery].low=0x49;
    assert(!model.build(page,decoding::properties,decoding::PropertyCount,0x75)); // Runtime, not generated indexes.
    decoding::properties[battery]=saved;
    auto duplicate=decoding::properties[511]; decoding::properties[511]=saved;
    assert(!model.build(page,decoding::properties,decoding::PropertyCount,0x75) && model.count==0);
    decoding::properties[511]=duplicate;
    analysis::Decoder decoder; decoder.valueObserver=observe;
    observer::Observation o; o.address=0x75;o.reg=0x50;o.associated=o.success=true;o.direction='R';o.dataBytes=1;o.preview[0]=0x10;
    decoder.add(o,100); assert(!cache.readings[battery].valid);
    o.reg=0x51;o.preview[0]=0x27;decoder.add(o,110);
    auto &r=cache.readings[battery];assert(r.valid && !r.failed && r.lastGood.word==10000);
    assert(reporting::background(r,110)==0x655024 && reporting::background(r,2310)==0xb8dfbc);
    o.reg=0x50;o.preview[0]=0x10;decoder.add(o,500);o.reg=0x51;o.preview[0]=0x27;decoder.add(o,510);
    assert(r.refreshed_ms==110 && decoder.values[battery].fieldChanged_ms==110);
    o.reg=0x50;o.preview[0]=0x11;decoder.add(o,520);o.reg=0x51;o.preview[0]=0x27;decoder.add(o,530);
    assert(r.lastGood.word==10001 && r.refreshed_ms==110);
    o.reg=0x50;o.preview[0]=0x10;decoder.add(o,540);o.reg=0x51;o.preview[0]=0x27;decoder.add(o,550);
    o.success=false;cache.observeFailure(o);assert(r.failed && r.lastGood.word==10000 && reporting::background(r,99999)==0x8f3038);
    o.success=true;o.reg=0x50;o.preview[0]=0x10;decoder.add(o,600);o.reg=0x51;o.preview[0]=0x27;decoder.add(o,610);assert(!r.failed && r.refreshed_ms==110);
    o.reg=0x50;o.preview[0]=0x11;decoder.add(o,700);decoder.gap();assert(!r.failed && r.lastGood.word==10000);
    o.reg=0x51;decoder.add(o,710);assert(!r.failed && r.lastGood.word==10000);
    unsigned conflict=0;
    while (conflict<decoding::PropertyCount && (!decoding::properties[conflict].count || strcmp(decoding::properties[conflict].name,"Set_BATlow"))) ++conflict;
    assert(conflict<decoding::PropertyCount);
    analysis::Value cv;cv.property=conflict;cv.success=cv.complete=true;cv.word=0x40;
    assert(!decoding::properties[conflict].conflict);
    cache.accept(cv);assert(cache.readings[conflict].warnings==0);
    decoding::properties[conflict].conflict=true; // Exercise a hypothetical unresolved external profile.
    cache.accept(cv);assert(cache.readings[conflict].valid && !cache.readings[conflict].failed && cache.readings[conflict].warnings==1);
    char formatted[192];analysis::formatValue(cache.readings[conflict].lastGood,formatted,sizeof(formatted));
    assert(strstr(formatted,"2700 mV") && strstr(formatted,"CONFLICT") && !strstr(formatted,"INCOMPLETE"));
    cv.success=false;cache.accept(cv);assert(cache.readings[conflict].failed && cache.readings[conflict].lastGood.word==0x40);
    cv.success=true;cache.accept(cv);assert(!cache.readings[conflict].failed && cache.readings[conflict].warnings==1);
    auto &cp=decoding::properties[conflict];auto previous=cp;cp.conflict=false;cp.maximum=1;
    cache.accept(cv);assert(!cache.readings[conflict].failed && cache.readings[conflict].warnings==2);
    decoding::EnumValue choices[]={{0,"zero"}};cp.maximum=7;cp.enums=choices;cp.enumCount=1;
    cache.accept(cv);assert(!cache.readings[conflict].failed && cache.readings[conflict].warnings==4);cp=previous;cp.conflict=false;
    cache.clear(); assert(!r.valid && !r.failed && reporting::background(r,0)==0);
    analysis::Value write;write.property=battery;write.direction='W';write.complete=write.success=true;cache.accept(write);assert(!r.attempted);
    presentation::Clock clock;clock.now(100);clock.setRunning(false,200);assert(clock.now(9000)==200);
    reporting::Swipe swipe;swipe.start(280,250);swipe.move(200,254);assert(swipe.end(120,256)==1);
    swipe.start(20,250);assert(swipe.end(120,260)==-1);
    swipe.start(120,180);swipe.move(125,240);assert(swipe.end(40,300)==0);
    puts("PASS: runtime report resolution, three profiles, relocated address, ordering, invalid/ambiguous references, unchanged refresh, retained failures, interrupted pairs and gestures");
}
