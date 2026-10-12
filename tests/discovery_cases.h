// Runs in the real LVGL/UART preview, after the existing regression cases.
{
    closeDetail(nullptr); control.abort("fixture");control.uncertain=false;
    screenIndex=0;reportActive=false;sliding=false;swipe.horizontal=false;
    slidePosition(nullptr,-int(pagePosition(screenIndex))*W);
    activePending=extraPending=rateRequest=0;extraSessionBusy=false;reportBatch.running=false;
    uartBaud=115200;statusAt=millis();status[0]=1;status[2]=0;status[3]=1;
    seen=sessionReady=delivering=true;needsSync=false;linkWasOnline=true;
    assert(registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual));
    selection=analysis::Selection{};selection.display=analysis::Display::Raw;
    selection.uniqueOrder=analysis::UniqueOrder::LatestTransaction;
    sourceFilter(selection);store->clear();decoder.clear();discoveryDecoder->clear();discover->clear();
    discover->enabled=false;discoveryDeferred=discoveryDialog=discoveryPauseOwned=discoveryPausedVerified=discoveryResume=false;
    lv_dropdown_set_selected(viewChoice,3);lv_obj_send_event(viewChoice,LV_EVENT_VALUE_CHANGED,nullptr);
    assert(view==View::Discover && discover->enabled && !control.busy());
    uint32_t discoveryId=100000;
    auto read=[&](uint8_t reg,uint8_t byte) {
        uint8_t p[]={0,0,0,0,1,1,0x75,0,2,0,2,0,1,0,0,0xea,1,0,1,2,0,0,0,0,0xeb,1,0,0};
        bringup::put32(p,++discoveryId);p[7]=p[17]=reg;p[26]=byte;inject(4,p,sizeof(p));pump();
    };
    auto write=[&](uint8_t reg,uint8_t byte) {
        uint8_t p[]={0,0,0,0,1,1,0x75,0,1,0,3,0,0,0,0,0xea,1,0,1,0,1};
        bringup::put32(p,++discoveryId);p[7]=p[17]=reg;p[19]=byte;inject(4,p,sizeof(p));pump();
    };
    auto pauseVerified=[&]() {
        assert(control.action==observer::Action::DiscoverPause && control.opcode==2);
        acknowledge();assert(control.opcode==4 && !modal);
        acknowledge();assert(!control.busy() && !status[0] && discoveryDialog && modal);
    };
    auto decision=[&](const char *name) {
        auto *b=labelButton(modal,name);assert(b);lv_obj_send_event(b,LV_EVENT_CLICKED,nullptr);
    };
    auto resume=[&]() {
        assert(control.action==observer::Action::Enable);pump();
        acknowledge();acknowledge();acknowledge();assert(!control.busy() && status[0]);
    };
    read(0x3a,30);assert(discover->count==1 && store->uniqueCount==1 && !modal);
    read(0x3a,99);assert(discover->count==1 && discover->front()->preview[0]==30);
    // A second rare key can already be queued when the pause takes effect.
    write(0x3a,0);assert(discover->count==2);pauseVerified();
    buttonColor(modal,"Keep",0x245b3a);buttonColor(modal,"Ignore",0x732d34);
    assert(hasLabel(modal,"0x75   0x3A   R"));
    lv_obj_update_layout(modal);
    assert(lv_obj_get_y(labelButton(modal,"Keep"))==55 && lv_obj_get_y(labelButton(modal,"Ignore"))==55);
    assert(hasLabel(modal,"Internal die temperature = 30 degC (R, inferred)"));
    assert(hasLabel(modal,"Thermal_loop = 0 (R, inferred)"));
    assert(hasLabel(modal,"Raw Value 0x1E / Dec 30 / 00011110"));
    save("tablet-v17-discover.ppm");
    unsigned tempProperty=decoding::PropertyCount,voltageProperty=decoding::PropertyCount;
    for(unsigned i=0;i<decoding::PropertyCount;++i) {
        const auto &p=decoding::properties[i];
        if(p.address==0x75 && !strcmp(p.id,"r3a_ic_temp"))tempProperty=i;
        if(p.address==0x75 && !strcmp(p.id,"battery_voltage_mv"))voltageProperty=i;
    }
    assert(tempProperty<decoding::PropertyCount && voltageProperty<decoding::PropertyCount);
    assert(hasLabel(modal,decoding::properties[tempProperty].friendlydesc));
    analysis::Value snapshot;
    assert(discovery::propertySnapshot(*discover->front(),tempProperty,snapshot) && snapshot.word==30 && snapshot.complete);
    auto &probe=store->incoming;probe=*discover->front();probe.reg=0x50;
    assert(discovery::propertySnapshot(probe,voltageProperty,snapshot) && !snapshot.complete);
    probe.reg=0x51;assert(discovery::propertySnapshot(probe,voltageProperty,snapshot) && !snapshot.complete);
    probe.associated=false;assert(!discovery::propertySnapshot(probe,voltageProperty,snapshot));
    decision("Keep");assert(discover->kept==1 && store->uniqueCount==1 && discoveryDialog && !control.busy());
    assert(hasLabel(modal,"0x75   0x3A   W"));decision("Ignore");resume();
    assert(discover->ignored==1 && !discover->count);
    read(0x3a,31);write(0x3a,77);
    assert(!control.busy() && !modal && !discover->count && store->unique[0].preview[0]==31 && store->unique[0].direction=='R');
    // Unknown keys are hidden in other Monitor modes; decisions survive mode changes.
    lv_dropdown_set_selected(viewChoice,1);lv_obj_send_event(viewChoice,LV_EVENT_VALUE_CHANGED,nullptr);
    read(0x17,0x80);assert(!discover->count && store->uniqueCount==1);
    bool reportReceived=false;
    for(unsigned i=0;i<decoding::PropertyCount;++i)if(decoding::properties[i].address==0x75 && decoding::properties[i].low==0x17)
        reportReceived|=reportCache.readings[i].valid;
    assert(reportReceived); // Hidden Monitor keys still feed the independent Reports cache.
    lv_obj_send_event(controls[1],LV_EVENT_CLICKED,nullptr);assert(control.action==observer::Action::Clear);
    pump();for(unsigned i=0;i<6;++i)acknowledge();
    assert(discover->kept==1 && discover->ignored==1 && !store->uniqueCount);
    read(0x3a,32);assert(store->uniqueCount==1 && !modal);
    // Returning to Discover prompts for the next genuinely undecided key.
    lv_dropdown_set_selected(viewChoice,3);lv_obj_send_event(viewChoice,LV_EVENT_VALUE_CHANGED,nullptr);
    read(0x17,0x80);pauseVerified();decision("Ignore");resume();
    read(0x17,0);assert(!modal && !control.busy() && discover->ignored==2);
    // Later keeps the candidate and stays paused; explicit Run reopens it safely.
    read(0x35,0);pauseVerified();decision("Later");
    assert(discover->count==1 && !control.busy() && !status[0] && discoveryDeferred);
    pump();assert(!modal);
    lv_obj_send_event(controls[0],LV_EVENT_CLICKED,nullptr);pump();
    acknowledge();acknowledge();acknowledge();pauseVerified();decision("Keep");resume();
    // Clear in Discover alone resets both decisions and pending keys.
    lv_obj_send_event(controls[1],LV_EVENT_CLICKED,nullptr);pump();
    for(unsigned i=0;i<6;++i)acknowledge();
    assert(discover->enabled && !discover->kept && !discover->ignored && !discover->count);
    lv_dropdown_set_selected(viewChoice,1);lv_obj_send_event(viewChoice,LV_EVENT_VALUE_CHANGED,nullptr);
    read(0x3a,31);refreshUi();
    assert(!discoveryGatesMonitor() && store->uniqueCount==1 && visibleCount && !modal);
    assert(&monitorDecoder()==&decoder);
    lv_dropdown_set_selected(viewChoice,0);lv_obj_send_event(viewChoice,LV_EVENT_VALUE_CHANGED,nullptr);
    read(0x04,4);refreshUi();assert(!discoveryGatesMonitor() && visibleCount && !modal);
    lv_dropdown_set_selected(viewChoice,3);lv_obj_send_event(viewChoice,LV_EVENT_VALUE_CHANGED,nullptr);
    assert(!discoveryGatesMonitor() && !store->uniqueCount);
    // Compact bundles preserve all 256 payload bytes; a broken trailing record
    // must reject the whole bundle before prompting for its otherwise valid head.
    std::vector<uint8_t> longRead(538,0),bundle(transport5::MaxRecord+2);
    bringup::put32(longRead.data(),++discoveryId);
    longRead[4]=longRead[5]=1;longRead[6]=0x75;longRead[7]=0x40;longRead[8]=2;
    longRead[10]=2;longRead[12]=1;longRead[15]=0xea;longRead[16]=1;longRead[17]=0x40;longRead[18]=1;
    longRead[19]=longRead[20]=1;longRead[24]=0xeb;longRead[25]=1;
    for(unsigned i=0;i<256;++i){longRead[26+2*i]=uint8_t(i);longRead[27+2*i]=i<255;}
    size_t packed=transport5::pack(longRead.data(),longRead.size(),bundle.data()+2,bundle.size()-2);assert(packed);
    transport5::put16(bundle.data(),uint16_t(packed));bundle.resize(packed+2);
    auto corrupt=bundle;corrupt.push_back(0xff);
    inject(5,corrupt.data(),unsigned(corrupt.size()));pump();assert(!discover->count && !control.busy());
    inject(5,bundle.data(),unsigned(bundle.size()));pump();
    assert(discover->front()->dataBytes==256 && discover->front()->length==538);
    assert(discover->front()->raw[536]==255 && !discover->front()->raw[537]);
    pauseVerified();decision("Ignore");resume();
    read(0x3a,33);pauseVerified();
    // Link loss cancels automatic resume without silently choosing Ignore.
    statusAt=millis()-8001;pump();assert(!modal && discover->count==1 && !discoveryResume);
    statusAt=millis();sessionReady=true;delivering=true;needsSync=false;control.uncertain=false;
    discoveryDeferred=false;discoveryPauseOwned=false;discoveryPausedVerified=false;
    pump();pauseVerified();decision("Keep");assert(!control.busy() && !status[0]);
    // A stale status acknowledgement must not unlock Keep/Ignore or resume.
    status[0]=1;read(0x04,4);acknowledge();assert(control.opcode==4);
    acknowledge(false);assert(control.uncertain && !modal && !discoveryResume);
    // Bounded pending capacity is explicit; the decision map itself never evicts.
    #include "discovery_group_cases.h"
    discover->clear();control.abort("fixture");control.uncertain=false;
    observer::Observation &candidate=store->incoming;candidate.associated=true;candidate.address=0x75;candidate.direction='R';
    for(unsigned i=0;i<discovery::PendingCapacity;++i){candidate.reg=uint8_t(i);assert(discover->enqueue(candidate));}
    candidate.reg=200;assert(!discover->enqueue(candidate) && discover->overflow==1);
    while(discover->count)assert(discover->decide(discovery::Decision::Keep));
    candidate.reg=0;assert(discover->allows(candidate));candidate.direction='W';assert(!discover->allows(candidate));
    assert(discover->kept==discovery::PendingCapacity);
    puts("PASS: Discover verified pause/drain, queued candidates, immutable Keep/Ignore preview, R/W keys, updates, independent Reports, retained filter across views, mode-specific Clear, Later/retry, link loss, stale ACK and bounded overflow");
}
