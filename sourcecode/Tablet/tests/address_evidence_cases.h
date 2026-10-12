{
    if(modal)extraClose(nullptr);
    cancelExtra(nullptr);graphClear();control=observer::Control{};reportBatch=acquisition::Batch{};
    activePending=extraPending=0;registry.reset();decoder.clear();
    assert(registry.assign(0x3a,registry.find("ip2366"),decoding::Assignment::Manual));
    assert(registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual));
    includeUnverifiedReports=false;updateReportOptions();assert(reportOptionCount==0);
    graphOptionsRefresh();assert(graphOptionsCount==0);
    seen=sessionReady=delivering=true;needsSync=false;status[0]=status[1]=status[2]=0;status[3]=1;statusAt=millis();
    uint8_t raw[]={1,0,0,0,1,1,0x75,0x50,2,0,2,0,1,0,0,0xea,1,0x50,1,2,0,0,0,0,0xeb,1,0x34,0};
    observer::Observation observed;assert(observer::parse(raw,sizeof(raw),observed) && observer::addressEvidence(observed));
    uint8_t probe[]={2,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0xea,1};
    assert(observer::parse(probe,sizeof(probe),observed) && !observed.dataBytes && observer::addressEvidence(observed));
    probe[16]=0;assert(observer::parse(probe,sizeof(probe),observed) && !observer::addressEvidence(observed));
    auto fail=std::vector<uint8_t>(raw,raw+sizeof(raw));fail[4]=0;fail[6]=0x3a;fail[15]=0x74;fail[24]=0x75;
    inject(4,fail.data(),unsigned(fail.size()));pump();
    assert(!registry.bindings[0x3a].confirmed && !registry.bindings[0x3a].seen);
    // A structurally valid compact bundle must use the same eligibility gate.
    uint8_t packed[transport5::MaxRecord],bundle[transport5::MaxRecord+2];
    size_t n=transport5::pack(fail.data(),fail.size(),packed,sizeof(packed));assert(n);
    bundle[0]=uint8_t(n);bundle[1]=uint8_t(n>>8);memcpy(bundle+2,packed,n);
    inject(5,bundle,unsigned(n+2));pump();assert(!registry.bindings[0x3a].confirmed);
    fail[4]=1;fail[9]=1;assert(observer::parse(fail.data(),fail.size(),observed) && !observer::addressEvidence(observed));
    fail[9]=0;fail[16]=fail[25]=0;assert(observer::parse(fail.data(),fail.size(),observed) && !observer::addressEvidence(observed));
    inject(4,raw,sizeof(raw));pump();assert(registry.bindings[0x75].confirmed);
    updateReportOptions();assert(reportOptionCount==2);
    graphOptionsRefresh();assert(graphOptionsCount==1 && graphOptions[0].address==0x75);
    for(unsigned i=0;i<reportOptionCount;++i)assert(reportOptions[i].address==0x75);
    unsigned chosen=reportSelected;inject(4,fail.data(),unsigned(fail.size()));pump();updateReportOptions();
    assert(reportSelected==chosen && reportOptionCount==2 && !registry.bindings[0x3a].confirmed);
    includeUnverifiedReports=true;updateReportOptions();assert(reportOptionCount==4);
    includeUnverifiedReports=false;updateReportOptions();assert(reportOptionCount==2 && reportOptions[reportSelected].address==0x75);
    graphActive=graphReading=true;graphStopped=false;graphLinkLost();assert(graphStopped && !graphActive && !graphReading);
    puts("PASS: failed/interrupted/address-NACK observations do not discover devices; legacy and compact paths confirm valid payloads; Reports hides unverified assignments and preserves the valid selection");
}
