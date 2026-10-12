#include "bringup_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "observations.h"
#include "analysis.h"
#include "detail_text.h"
#include "highlight.h"
#include "presentation_clock.h"
#include <memory>
using namespace bringup;
static uint8_t raw[MaxRaw], wire[MaxWire];
static size_t packet(uint8_t type, size_t length) {
    memset(raw, 0, sizeof(raw));
    raw[0] = 'I'; raw[1] = 'S'; raw[2] = 1; raw[3] = type;
    put32(raw + 4, 1234);
    raw[8] = uint8_t(length); raw[9] = uint8_t(length >> 8);
    for (size_t i = 0; i < length; ++i) raw[10 + i] = uint8_t(i);
    put32(raw + 10 + length, crc32(raw, 10 + length));
    return encode(raw, length + 14, wire, sizeof(wire));
}
static bool receive(Receiver &r, Packet &p, size_t n) {
    bool accepted = false;
    for (size_t i = 0; i < n; ++i) accepted |= r.feed(wire[i], p);
    return accepted;
}
static size_t readTransaction(uint8_t *p, uint8_t reg, uint8_t data) {
    const uint8_t fixture[]={1,0,0,0,1,1,0x75,0,2,0, 2,0,1,0,0,0xea,1,0,1, 2,0,0,0,0,0xeb,1,0,0};
    memcpy(p,fixture,sizeof(fixture)); p[7]=reg; p[17]=reg; p[26]=data;
    return sizeof(fixture);
}
int main() {
    presentation::Clock clock;
    assert(clock.now(100)==100); clock.setRunning(false,150);
    assert(clock.now(10000)==150); clock.setRunning(true,10010);
    assert(clock.now(10110)==250);
    presentation::Clock wrap; assert(wrap.now(0xfffffff0u)==0xfffffff0u);
    wrap.setRunning(false,0xfffffff8u); assert(wrap.now(0x20u)==0xfffffff8u);
    wrap.setRunning(true,0x30u); assert(wrap.now(0x40u)==8u);
    decoding::Registry registry; assert(registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual));
    char detail[]="\n \r\n  Name: 100 mA  \r\n\n\n Address 0x75 \n\n\n\n Note \t\n\n";
    details::compact(detail);
    assert(!strcmp(detail,"Name: 100 mA\n\nAddress 0x75\n\nNote"));
    char empty[]=" \t\r\n\n";
    details::compact(empty); assert(!*empty);
    assert(analysis::Selection().display==analysis::Display::Parsed);
    assert(highlight::color(0x102030,0x304050,0,0)==0x304050);
    assert(highlight::color(0x102030,0x304050,0,1100)==0x203040);
    assert(highlight::color(0x102030,0x304050,0,2200)==0x102030);
    assert(highlight::color(0x102030,0x304050,0xfffffff0u,0x10u)==highlight::color(0x102030,0x304050,0,32));
    for (const auto &property:decoding::definitions) assert(property.friendlydesc && *property.friendlydesc);
    assert(analysis::Selection().uniqueOrder==analysis::UniqueOrder::LatestTransaction);
    assert(analysis::moreRecent(10,2,10,1,true));
    assert(!analysis::moreRecent(10,2,10,1,false));
    assert(analysis::moreRecent(2,0,0xfffffff0u,0,true));
    assert(analysis::moreRecent(10,0,10,0xffffffffu,true));
    for (unsigned i=0;i<decoding::PropertyCount;++i) {
        const auto &p=decoding::properties[i];
        if (!p.bit0) continue;
        for (unsigned bit=0;bit<2;++bit) {
            analysis::Value v; v.property=i; v.word=bit?p.mask:0; v.complete=v.success=true;
            char value[192]; details::value(v,value,sizeof(value));
            assert(value[0]==char('0'+bit) && value[1]==0);
            v.success=false; details::value(v,value,sizeof(value)); assert(!strncmp(value,"FAIL ",5));
        }
    }
    analysis::Selection filter;
    assert(analysis::parseList("0x75, 20-22",filter.addresses,128,filter.anyAddress));
    assert(filter.addresses[0x75] && filter.addresses[0x21] && !filter.addresses[0x23]);
    for (const char *invalid:{"80","75,","GG","30-20","123","20 21","-1"})
        assert(!analysis::parseList(invalid,filter.addresses,128,filter.anyAddress));
    assert(analysis::parseList("  ",filter.addresses,128,filter.anyAddress) && !filter.anyAddress);
    analysis::Sequence seq;
    seq.packet(10); seq.packet(15); assert(seq.gaps==0 && seq.startupGaps==4);
    seq.synced=true; seq.packet(18); assert(seq.gaps==2);
    seq.packet(1); assert(!seq.synced && seq.resets==1);
    seq.reconnect(); seq.packet(100); assert(seq.gaps==2);
    seq.synced=true; seq.last=0xffffffffu; seq.packet(0); assert(seq.gaps==2);
    analysis::Decoder decoder;
    observer::Observation obs;
    uint8_t fixture[64];
    auto add=[&](uint8_t reg,uint8_t byte,uint32_t time,bool ok=true) {
        size_t size=readTransaction(fixture,reg,byte); fixture[4]=ok;
        assert(observer::parse(fixture,size,obs)); decoder.add(obs,time);
    };
    auto property=[](const char *id) {
        for (unsigned i=0;i<decoding::PropertyCount;++i) if (!strcmp(decoding::properties[i].id,id)) return i;
        assert(false); return 0u;
    };
    char value[192];
    add(0x6e,253,100); add(0x6f,7,110);
    auto current=property("battery_current_code_ma");
    assert(decoder.values[current].word==2045 && decoder.values[current].complete);
    analysis::formatValue(decoder.values[current],value,sizeof(value)); assert(!strcmp(value,"2045 mA"));
    uint32_t changed=decoder.values[current].changed_ms;
    add(0x6e,253,200); add(0x6f,7,210);
    assert(decoder.values[current].changed_ms==changed && decoder.values[current].observed_ms==210);
    add(0x6e,0,300); add(0x31,0,310); add(0x6f,8,320);
    assert(!decoder.values[current].complete);
    add(0x6e,0,400); decoder.gap(); add(0x6f,8,410); assert(!decoder.values[current].complete);
    add(0x6e,0,500); add(0x6f,8,1601); assert(!decoder.values[current].complete);
    add(0x6e,253,1700); add(0x6f,7,1710,false);
    analysis::formatValue(decoder.values[current],value,sizeof(value)); assert(!strcmp(value,"FAIL 2045 mA"));
    add(0x3a,49,1800);
    analysis::formatValue(decoder.values[property("r3a_ic_temp")],value,sizeof(value)); assert(!strcmp(value,"49 degC"));
    analysis::formatValue(decoder.values[property("r3a_thermal_loop")],value,sizeof(value)); assert(!strcmp(value,"NO"));
    uint32_t temperatureChange=decoder.values[property("r3a_ic_temp")].fieldChanged_ms;
    add(0x3a,177,1850);
    assert(decoder.values[property("r3a_ic_temp")].fieldChanged_ms==temperatureChange);
    assert(decoder.values[property("r3a_thermal_loop")].fieldChanged_ms==1850);
    add(0x3a,178,1860);
    assert(decoder.values[property("r3a_ic_temp")].fieldChanged_ms==1860);
    assert(decoder.values[property("r3a_thermal_loop")].fieldChanged_ms==1850);
    analysis::ColumnFlow flow; int x,y;
    for (unsigned i=0;i<33;++i) { flow.place(20,x,y); assert(x==8+int((i/16)%2)*156 && y==int(i/32)*320+int(i%16)*20); }
    analysis::ColumnFlow groupsFlow; groupsFlow.place(180,x,y); assert(x==8 && y==0);
    groupsFlow.place(180,x,y); assert(x==164 && y==0);
    groupsFlow.place(400,x,y); assert(x==8 && y==320);
    groupsFlow.place(80,x,y); assert(x==164 && y==320);
    add(0x17,0x80,1900);
    analysis::formatValue(decoder.values[property("r17_ntc_en")],value,sizeof(value)); assert(!strcmp(value,"YES"));
    add(0x04,1,2000);
    analysis::formatValue(decoder.values[property("r04_bat_num")],value,sizeof(value)); assert(!strcmp(value,"1 cells RANGE"));
    add(0x74,1,2100); add(0x75,0,2110);
    analysis::formatValue(decoder.values[property("system_power_mw")],value,sizeof(value)); assert(!strcmp(value,"10 mW"));
    puts("PASS: filter lists/ranges, startup sequence sync, paired assembly/gaps, scaling, bit polarity, temperature");
    assert(crc32(reinterpret_cast<const uint8_t *>("123456789"), 9) == 0xcbf43926);
    Receiver r; Packet p;
    for (size_t n : {size_t(0), size_t(254), size_t(255), size_t(256), MaxPayload}) {
        size_t count = packet(4, n);
        assert(count && count <= MaxWire && receive(r, p, count));
        assert(p.length == n && p.sequence == 1234);
        assert(memcmp(p.payload, raw + 10, n) == 0);
    }
    size_t n = packet(3, 132); assert(receive(r, p, n));
    n = packet(3, 131); assert(!receive(r, p, n));
    n = packet(2, 6);
    put32(raw + 10, 99); raw[14] = 4; raw[15] = 0;
    put32(raw + 16, crc32(raw, 16)); n = encode(raw, 20, wire, sizeof(wire));
    assert(receive(r, p, n)); assert(matchesStatusReply(p, 99));
    assert(!matchesStatusReply(p, 1234)); assert(!matchesStatusReply(p, 0));
    wire[n - 2] ^= 1; assert(!receive(r, p, n));
    for (size_t i = 0; i < MaxWire + 10; ++i) r.feed(1, p);
    r.feed(0, p); n = packet(3, 132); assert(receive(r, p, n));
    assert(r.bad == 3);
    uint8_t touch[8] = {0,1,1,63,1,223,0,0}; TouchPoint point;
    assert(parseTouch(touch, 8, point) && point.pressed && point.x_px == 319 && point.y_px == 479);
    touch[3] = 64; assert(!parseTouch(touch, 8, point) && !point.pressed);
    touch[1] = 0; assert(parseTouch(touch, 8, point) && !point.pressed);
    touch[1] = 2; assert(parseTouch(touch, 8, point) && !point.pressed);
    memset(touch, 0xff, sizeof(touch));
    assert(parseTouch(touch, 8, point) && !point.pressed);
    touch[1] = 0; assert(parseTouch(touch, 8, point) && !point.pressed);
    touch[1] = 1; assert(!parseTouch(touch, 8, point) && !point.pressed);
    assert(!parseTouch(touch, 7, point));
    assert(statusRequest(42, wire, sizeof(wire)) == 17);
    auto model=std::unique_ptr<observer::Store>(new observer::Store());
    uint8_t transaction[bringup::MaxPayload];
    size_t tn=readTransaction(transaction,0x50,0x12);
    assert(model->add(transaction,tn,10));
    assert(model->uniqueCount==1 && model->row(true,0)->dataBytes==1);
    assert(model->row(true,0)->preview[0]==0x12 && model->row(true,0)->direction=='R');
    transaction[0]=2; assert(model->add(transaction,tn,20));
    assert(model->unique[0].changed_ms==10);
    tn=readTransaction(transaction,0x51,0x34); model->add(transaction,tn,30);
    assert(model->row(true,0)->reg==0x51);
    tn=readTransaction(transaction,0x50,0x12); model->add(transaction,tn,40);
    assert(model->row(true,0)->reg==0x51);
    assert(model->unique[0].observed_ms==40 && model->unique[0].changed_ms==10);
    put32(transaction,42); model->add(transaction,tn,41);
    assert(model->unique[0].id==42 && le32(model->unique[0].raw)==42 && model->unique[0].changed_ms==10);
    transaction[26]=0x13; model->add(transaction,tn,50);
    assert(model->row(true,0)->reg==0x50 && !model->row(true,0)->fresh);
    transaction[4]=0; model->add(transaction,tn,60);
    assert(!model->row(true,0)->success && model->failures==1);
    transaction[5]=0; model->add(transaction,tn,70);
    assert(model->uniqueCount==2 && model->unassociated==1);
    transaction[8]=9; assert(!model->add(transaction,tn,80));
    tn=readTransaction(transaction,0x50,0x12); transaction[27]=2; assert(!model->add(transaction,tn,80));
    tn=readTransaction(transaction,0x50,0x12); assert(!model->add(transaction,tn-1,80));
    model->clear();
    for (unsigned i=0;i<129;++i) { tn=readTransaction(transaction,uint8_t(i),uint8_t(i)); assert(model->add(transaction,tn,i)); }
    assert(model->uniqueCount==128 && model->capacityMiss==1 && model->historyCount==64);
    assert(model->row(true,0)->reg==127 && model->row(false,0)->reg==128);
    // Maximum payload read: selector + 256 data bytes, no inferred SMBus count stripping.
    tn=readTransaction(transaction,0x20,0);
    transaction[19]=1; transaction[20]=1;
    for (unsigned i=0;i<257;++i) { transaction[24+2*i]=i?uint8_t(i-1):0xeb; transaction[25+2*i]=(i==256)?0:1; }
    tn=24+514;
    observer::Observation observation;
    assert(observer::parse(transaction,tn,observation) && observation.dataBytes==256);
    char line[24]; observer::summary(observation,line,sizeof(line)); assert(strstr(line,"00 01 02+")!=nullptr);
    observer::Control c; assert(c.begin(observer::Action::Enable));
    for (uint8_t opcode : {uint8_t(8),uint8_t(5),uint8_t(1)}) {
        assert(c.prepare(100,wire,sizeof(wire))>0 && c.opcode==opcode);
        uint8_t response[6]; put32(response,c.pendingId); response[4]=opcode; response[5]=0;
        Packet reply={2,500,response,6}; assert(c.reply(reply));
    }
    assert(!c.busy()); assert(c.begin(observer::Action::Off));
    assert(c.prepare(100,wire,sizeof(wire))>0 && c.opcode==2);
    c.tick(2600); assert(c.uncertain && !c.busy());
    assert(c.begin(observer::Action::Off));
    for (uint8_t opcode : {uint8_t(2),uint8_t(5)}) {
        assert(c.prepare(3000,wire,sizeof(wire))>0 && c.opcode==opcode);
        uint8_t response[6]; put32(response,c.pendingId); response[4]=opcode; response[5]=0;
        Packet reply={2,123,response,6}; assert(c.reply(reply));
    }
    assert(!c.busy()); assert(c.begin(observer::Action::Pause));
    assert(c.prepare(3000,wire,sizeof(wire))>0 && c.opcode==2);
    uint8_t rejected[6]; put32(rejected,c.pendingId); rejected[4]=2; rejected[5]=2;
    Packet rejection={2,124,rejected,6}; assert(c.reply(rejection) && c.uncertain && !c.busy());
    for (bool running:{false,true}) {
        assert(c.begin(observer::Action::Clear,running));
        const uint8_t phases[]={2,9,4,8,5,1};
        for (unsigned phase=0;phase<(running?6u:3u);++phase) {
            assert(c.prepare(4000,wire,sizeof(wire))>0 && c.opcode==phases[phase]);
            uint8_t response[6]; put32(response,c.pendingId); response[4]=c.opcode; response[5]=0;
            Packet reply={2,500,response,6}; assert(c.reply(reply));
        }
        assert(!c.busy() && !c.uncertain);
    }
    assert(c.clearsAcknowledged==2);
    for (unsigned stop=0;stop<4;++stop) {
        assert(c.begin(observer::Action::Synchronize));
        const uint8_t phases[]={2,8,5,4};
        for (unsigned phase=0;phase<=stop;++phase) {
            assert(c.prepare(4500,wire,sizeof(wire)) && c.opcode==phases[phase]);
            uint8_t response[6]; put32(response,c.pendingId); response[4]=c.opcode; response[5]=phase==stop?2:0;
            Packet reply={2,501,response,6}; assert(c.reply(reply));
        }
        assert(c.uncertain && !c.busy() && !c.prepare(4501,wire,sizeof(wire)));
    }
    assert(c.begin(observer::Action::Synchronize)); assert(c.prepare(4500,wire,sizeof(wire)));
    c.tick(7000); assert(c.uncertain && !c.busy() && !c.prepare(7001,wire,sizeof(wire)));
    for (unsigned stop=0;stop<6;++stop) {
        assert(c.begin(observer::Action::Clear,true));
        for (unsigned phase=0;phase<=stop;++phase) {
            assert(c.prepare(5000,wire,sizeof(wire)));
            uint8_t response[6]; put32(response,c.pendingId); response[4]=c.opcode; response[5]=phase==stop?2:0;
            Packet reply={2,501,response,6}; assert(c.reply(reply));
        }
        assert(c.uncertain && !c.busy() && c.prepare(5001,wire,sizeof(wire))==0);
    }
    assert(c.begin(observer::Action::Clear,true)); assert(c.prepare(6000,wire,sizeof(wire)));
    c.tick(8500); assert(c.uncertain && c.prepare(8501,wire,sizeof(wire))==0);
    puts("PASS: field-specific highlights, column-major whole-group layout, Clear preserve-state/reject/timeout");
    puts("PASS: CRC, COBS/max payload, corrupt/oversized recovery, status ACK, touch bounds");
    puts("PASS: unique change ordering, failures, history/capacity, 256-byte reads, controls/timeout");
}
