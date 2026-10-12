#include "../../rp2350-sniffer-v7/src/client_protocol.h"
#include "observations.h"
#include "transport_v5.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
#include <random>

static uint8_t canonical[4608], packed[4608], restored[4608];
static sniffer::Frame frames[8];
static size_t make(unsigned count,unsigned bytes,bool read=false) {
    memset(canonical,0,sizeof(canonical)); canonical[0]=42; canonical[4]=canonical[5]=1;
    canonical[6]=0x75; canonical[7]=0x50; canonical[8]=uint8_t(count);
    size_t at=10;
    for (unsigned f=0;f<count;++f) {
        link::put16(canonical+at,uint16_t(bytes)); canonical[at+2]=f+1<count; canonical[at+3]=f&1; canonical[at+4]=uint8_t(f);
        at+=5;
        for (unsigned i=0;i<bytes;++i) { canonical[at++]=i?uint8_t(i+f):read?0xeb:0xea; canonical[at++]=(i+f)&1; }
    }
    return at;
}
template<size_t N> static std::vector<uint8_t> drain(link::ClientT<N> &c) {
    std::vector<uint8_t> data;
    while (c.bytes()) { size_t n=c.contiguous(); data.insert(data.end(),c.frontData(),c.frontData()+n); c.consume(n); }
    return data;
}
int main() {
    for (unsigned count=1;count<=8;++count) for (unsigned bytes=0;bytes<=258;++bytes) {
        size_t n=make(count,bytes); size_t compact=transport5::pack(canonical,n,packed,sizeof(packed));
        assert(compact && compact<=transport5::MaxRecord);
        size_t decoded=transport5::unpack(packed,compact,restored,sizeof(restored));
        assert(decoded==n && !memcmp(canonical,restored,n));
        assert(!transport5::unpack(packed,compact-1,restored,sizeof(restored)));
    }
    std::mt19937 random(2366); unsigned accepted=0;
    for (unsigned i=0;i<10000;++i) {
        size_t n=make(1+random()%8,random()%259); size_t size=transport5::pack(canonical,n,packed,sizeof(packed));
        packed[random()%size]^=uint8_t(1u<<(random()%8));
        size_t decoded=transport5::unpack(packed,size,restored,sizeof(restored));
        if (decoded) { ++accepted; assert(decoded<=sizeof(restored)); }
    }
    // Every byte/ACK of a 256-byte read is preserved.
    size_t n=make(1,257,true); size_t size=transport5::pack(canonical,n,packed,sizeof(packed));
    assert(transport5::unpack(packed,size,restored,sizeof(restored))==n);
    observer::Observation o; assert(observer::parse(restored,n,o) && o.dataBytes==256);
    uint8_t filter[52]={8,15,1,0}; filter[4+0x75/8]|=1u<<(0x75%8); filter[20+0x50/8]|=1u<<(0x50%8);
    transport5::Filter f; assert(f.configure(filter,sizeof(filter)));
    assert(f.accepts(true,true,0x75,0x50,'R'));
    assert(!f.accepts(true,true,0x75,0x51,'R') && !f.accepts(true,true,0x75,0x50,'W'));
    assert(f.accepts(false,false,0x75,0,'R') && !f.accepts(true,false,0x75,0,'R'));
    assert(!f.accepts(false,false,0x74,0,'R'));
    filter[1]=11; assert(f.configure(filter,sizeof(filter))); assert(!f.accepts(false,false,0x75,0,'R'));
    filter[3]=1; assert(f.configure(filter,sizeof(filter))); assert(!f.accepts(false,true,0x75,0x50,'R'));
    filter[3]=2; assert(f.configure(filter,sizeof(filter))); assert(!f.accepts(true,true,0x75,0x50,'R'));
    auto old=f; filter[2]=0; assert(!f.configure(filter,sizeof(filter)) && f.generation==old.generation);
    auto uart=std::unique_ptr<link::UartClient>(new link::UartClient);
    auto usb=std::unique_ptr<link::Client>(new link::Client);
    static_assert(link::Client::capacity()==16384 && link::UartClient::capacity()==65536,"UART-only queue enlargement");
    uart->subscribed=usb->subscribed=true; uart->compact=true;
    filter[2]=1; filter[3]=0; assert(uart->sourceFilter.configure(filter,sizeof(filter)));
    frames[0].size=2; frames[0].bytes[0]=0xea; frames[0].bytes[1]=0x50; frames[0].ack[0]=frames[0].ack[1]=true;
    frames[0].end=sniffer::Boundary::Restart;
    frames[1].size=2; frames[1].bytes[0]=0xeb; frames[1].bytes[1]=0x46; frames[1].ack[0]=true; frames[1].ack[1]=false;
    auto assessment=sniffer::assess(frames,2); assert(assessment.success);
    auto other=assessment; other.reg=0x51;
    uart->transaction(1,other,frames,2,false,0); usb->transaction(1,other,frames,2,false,0);
    assert(uart->suppressed==1 && !uart->bytes() && !uart->batchBytes && usb->bytes());
    drain(*usb);
    for (unsigned i=0;i<8;++i) uart->transaction(i+2,assessment,frames,2,false,100);
    assert(!uart->bytes() && uart->batchCount==8); uart->tick(101); assert(!uart->bytes());
    uart->tick(102); assert(uart->bytes());
    auto bytes=drain(*uart); bringup::Receiver receiver; bringup::Packet packet{};
    unsigned records=0;
    for (uint8_t byte:bytes) if (receiver.feed(byte,packet)) {
        assert(packet.type==5); size_t at=0;
        while (at<packet.length) {
            size_t length=bringup::le16(packet.payload+at); at+=2;
            size_t decoded=transport5::unpack(packet.payload+at,length,restored,sizeof(restored)); at+=length;
            assert(decoded && observer::parse(restored,decoded,o) && o.reg==0x50 && o.preview[0]==0x46); ++records;
        }
    }
    assert(records==8 && !receiver.bad && bytes.size()==208); // V1: 8 * 44 = 352.
    // Receive buffer ownership: a reply inside a handler cannot overwrite its request.
    uint8_t command[52]={8,8,3,0}, raw[78]={'I','S',1,1}, wire[100];
    link::put32(raw+4,10); raw[8]=52; memcpy(raw+10,command,52); link::put32(raw+62,link::crc(raw,62));
    size_t wn=link::encode(raw,66,wire); bool called=false;
    for (size_t i=0;i<wn;++i) uart->receive(wire[i],[&](uint32_t id,const uint8_t *p,size_t len) {
        uart->response(id,8,0); assert(len==52 && !memcmp(p,command,52)); called=true;
    }); assert(called); drain(*uart);
    uart->response(20,4,0); size_t queued=uart->bytes(); unsigned fifo=4;
    uart->drainFifo([&] { return fifo!=0; },[&](uint8_t) { --fifo; });
    assert(fifo==0 && uart->bytes()==queued-4); // Stop immediately at FIFO backpressure.
    size_t before=uart->bytes(); uart->drainFifo([] { return true; },[](uint8_t) {},3);
    assert(uart->bytes()==before-3); drain(*uart);
    for (unsigned i=0;i<4000;++i) uart->transaction(i,assessment,frames,2,false,200);
    uart->flushBatch(); assert(uart->dropped && uart->transactionDropped && uart->highWater>16384 && uart->highWater<=65536);
    const auto sequenceBefore=uart->sequence; const auto queuedBefore=uart->bytes();
    uart->clearDiagnostics(); assert(!uart->dropped && !uart->transactionDropped && uart->sequence==sequenceBefore && uart->bytes()==queuedBefore);
    uart->discardOutput(); assert(uart->discardedBytes && !uart->bytes());
    transport5::Rate rate;
    assert(!rate.request(1,921600,0,true)); assert(rate.request(1,1000000,0,true));
    assert(!rate.request(2,2000000,1,true)); rate.switched(20);
    assert(!rate.ready(119) && !rate.confirm(1,30) && !rate.request(3,115200,30,true));
    assert(rate.ready(120));
    assert(!rate.confirm(2,130)); assert(rate.confirm(1,130));
    rate.keepalive(3000); assert(!rate.expired(6499) && rate.expired(6500)); rate.fallback();
    assert(rate.target==115200 && rate.fallbackCount==1);
    assert(rate.request(2,2000000,0xfffffff0u,true)); rate.switched(0xfffffff0u);
    assert(!rate.ready(0x30u) && rate.ready(0x54u));
    assert(!rate.expired(0x100u) && rate.expired(0x700u)); rate.fallback();
    rate.clearDiagnostics(); assert(!rate.attempts && !rate.fallbackCount && rate.phase==transport5::Phase::Idle);
    assert(rate.request(3,2000000,10,true)); assert(!rate.expired(7009) && rate.expired(7010));
    rate.fallback(transport5::RateFailure::DrainTimeout); assert(rate.failure==transport5::RateFailure::DrainTimeout);
    assert(rate.request(4,2000000,10,true)); rate.switched(10); assert(rate.ready(110));
    assert(!rate.expired(1609) && rate.expired(1610));
    rate.fallback(transport5::RateFailure::ConfirmTimeout);
    assert(rate.failure==transport5::RateFailure::ConfirmTimeout && rate.lastTarget==2000000);
    printf("PASS: 2072 exhaustive compact roundtrips; 10000 mutations (%u structurally accepted); filters, queue/backpressure, byte ownership, baud leases/wrap\n",accepted);
    puts("CAPACITY: eight short reads = 208 bytes (V1 352), 40.9% fewer; no transaction coalescing");
}
