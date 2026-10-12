#include "analysis.h"
#include "../../rp2350-sniffer-v7/src/client_protocol.h"
#include <cassert>
#include <memory>
#include <vector>
#include <cstdio>

int main() {
    decoding::Registry registry; assert(registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual));
    auto client=std::make_unique<link::Client>();
    auto receiver=std::make_unique<bringup::Receiver>();
    auto store=std::make_unique<observer::Store>();
    auto frames=std::make_unique<sniffer::Frame[]>(2);
    frames[0].size=2; frames[0].bytes[0]=0xea; frames[0].bytes[1]=0x20;
    frames[0].end=sniffer::Boundary::Restart;
    frames[0].ack[0]=frames[0].ack[1]=true;
    frames[1].size=257; frames[1].bytes[0]=0xeb; frames[1].ack[0]=true;
    for (unsigned i=1;i<257;++i) { frames[1].bytes[i]=uint8_t(i-1); frames[1].ack[i]=i<256; }
    auto assessment=sniffer::assess(frames.get(),2);
    client->subscribed=true; client->transaction(42,assessment,frames.get(),2,false);
    bringup::Packet packet{}; bool received=false;
    while (client->bytes()) {
        if (receiver->feed(client->front(),packet)) {
            received=true; assert(packet.type==4);
            assert(store->add(packet.payload,packet.length,100));
        }
        client->consume();
    }
    assert(received && store->incoming.dataBytes==256 && store->incoming.id==42);
    std::vector<uint8_t> baseline(store->incoming.raw,store->incoming.raw+store->incoming.length);
    auto oversizedRead=baseline;
    oversizedRead[19]=2; oversizedRead[20]=1;
    oversizedRead.back()=1; oversizedRead.push_back(0xff); oversizedRead.push_back(0);
    observer::Observation oversized;
    assert(observer::parse(oversizedRead.data(),oversizedRead.size(),oversized) && oversized.dataBytes==257);
    uint32_t random=0x12345678;
    auto next=[&]() { random^=random<<13; random^=random>>17; random^=random<<5; return random; };
    unsigned accepted=0;
    observer::Observation observation;
    for (unsigned test=0;test<20000;++test) {
        auto bytes=baseline;
        for (unsigned k=0;k<1+test%8;++k) bytes[next()%bytes.size()]=uint8_t(next());
        size_t length=test%3?bytes.size():next()%(bytes.size()+1);
        if (observer::parse(bytes.data(),length,observation)) {
            ++accepted;
            assert(observation.length==length && observation.dataBytes<=256 && observation.address<128);
        }
    }
    assert(accepted);
    for (unsigned i=0;i<50000;++i) receiver->feed(uint8_t(next()),packet);
    receiver->feed(0,packet); receiver->feed(0,packet);
    client->transaction(43,assessment,frames.get(),2,false); received=false;
    while (client->bytes()) {
        received|=receiver->feed(client->front(),packet); client->consume();
    }
    assert(received && packet.type==4 && bringup::le32(packet.payload)==43);
    // Review reproduction: reused transaction IDs merge distinct history events.
    analysis::Value before,after; before.property=after.property=0;
    before.transactionId=after.transactionId=7; before.observed_ms=10; after.observed_ms=20;
    assert(analysis::sameSource(before,after,false));
    printf("PASS: RP2350-to-tablet 256-byte wire compatibility; 20000 structural mutations (%u accepted); 50000 noise bytes/recovery\n",accepted);
    puts("REPRODUCED: Sniff grouping lacks a session/observation identity for reused transaction IDs.");
    puts("REPRODUCED: tablet structural parser accepts a 257-byte read; current RP2350 decoder caps reads at 256.");
}
