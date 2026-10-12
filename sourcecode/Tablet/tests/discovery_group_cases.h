// Uses the real UI/packet helpers from discovery_cases.h.
{
    discover->clear();store->clear();decoder.clear();discoveryDecoder->clear();
    control.abort("group fixture");control.uncertain=false;
    discoveryResume=discoveryPauseOwned=discoveryPausedVerified=discoveryDeferred=false;
    status[0]=1;statusAt=millis();sessionReady=delivering=true;needsSync=false;
    uint8_t selector[]={0,0,0,0,1,1,0x75,0x17,1,0,2,0,0,0,0,0xea,1,0x17,1};
    bringup::put32(selector,++discoveryId);inject(4,selector,sizeof(selector));pump();
    assert(!discover->count && !control.busy());
    // An unknown zero-payload command is still offered, not silently lost.
    selector[7]=selector[17]=0xfd;bringup::put32(selector,++discoveryId);
    inject(4,selector,sizeof(selector));pump();assert(discover->count==1);
    assert(!discover->group()->range.parsed && !discover->front()->dataBytes);
    pauseVerified();decision("Ignore");resume();
    // Failed selectors are evidence, unlike successful pointer setup.
    selector[7]=selector[17]=0x17;selector[4]=0;bringup::put32(selector,++discoveryId);
    inject(4,selector,sizeof(selector));pump();assert(discover->count==1);
    pauseVerified();assert(hasLabel(modal,"FAIL"));
    assert(hasLabel(modal,"Observed 0 payload bytes; profile expects one value byte per register."));
    decision("Ignore");resume();
    // Capture may begin at the high byte. Keep running until a fresh full pair.
    read(0x75,0x12);assert(!discover->count && !control.busy());
    read(0x74,0x34);assert(!discover->count && !control.busy());
    read(0x75,0x56);assert(discover->count==1 && discover->group()->complete);
    assert(discover->group()->range.low==0x74 && discover->group()->word()==0x5634);
    pauseVerified();decision("Ignore");resume();
    // A repeated low byte restarts the snapshot rather than prompting incomplete.
    read(0x50,0xff);assert(!discover->count && !control.busy());
    read(0x50,0x10);
    assert(!discover->count && !control.busy()); // Do not stop before the high byte.
    read(0x51,0x27);
    assert(discover->count==1 && discover->group()->complete && discover->group()->word()==10000);
    assert(discover->group()->present==3 && discover->group()->range.bytes==2);
    pauseVerified();
    assert(hasLabel(modal,"0x75  50-51  R"));
    assert(hasLabel(modal,"Battery voltage = 10000 mV (R)"));
    assert(hasLabel(modal,"Raw Value 0x2710 / Dec 10000"));
    save("tablet-v17-discover-group.ppm");
    decision("Keep");
    assert(store->uniqueCount==2 && discoveryDecoder->used[voltageProperty]);
    assert(discoveryDecoder->values[voltageProperty].word==10000);
    resume();
    read(0x50,0x21);assert(discover->decision(store->incoming)==discovery::Decision::Keep);
    read(0x51,0x27);assert(discover->decision(store->incoming)==discovery::Decision::Keep);
    assert(!discover->count && !control.busy() && discoveryDecoder->values[voltageProperty].word==10017);
    read(0x6e,0xa3);assert(!discover->count && !control.busy());
    read(0x6f,7);pauseVerified();
    assert(discover->group()->word()==1955 && discover->group()->range.low==0x6e);
    decision("Ignore");resume();
    read(0x6e,1);assert(discover->decision(store->incoming)==discovery::Decision::Ignore);
    read(0x6f,2);assert(discover->decision(store->incoming)==discovery::Decision::Ignore);
    assert(!control.busy() && !discover->count && store->uniqueCount==2);
    // Unknown registers remain first-class candidates, even in Parsed display.
    selection.display=analysis::Display::Parsed;
    read(0xfe,0x42);assert(discover->count==1 && !discover->group()->range.parsed);
    pauseVerified();assert(hasLabel(modal,"No matching property definition for this register and direction."));
    decision("Keep");resume();refreshUi();
    bool rawShown=false;for(unsigned i=0;i<visibleCount;++i)rawShown|=visibleRaw[i] && visibleRaw[i]->reg==0xfe;
    assert(rawShown);
    // Failed groups are offered, not hidden or converted to successful values.
    read(0x78,0x57);assert(!discover->count);
    uint8_t failed[]={0,0,0,0,0,1,0x75,0x79,2,0,2,0,1,0,0,0xea,1,0x79,1,2,0,0,0,0,0xeb,1,3,0};
    bringup::put32(failed,++discoveryId);inject(4,failed,sizeof(failed));pump();
    assert(discover->count==1 && !discover->group()->success);
    pauseVerified();assert(hasLabel(modal,"FAIL"));decision("Ignore");resume();
    // Missing successful companions are retried, not turned into approvals.
    read(0x70,0x76);assert(!discover->count && !control.busy());
    const auto incompleteBefore=discover->incompleteGroups;
    previewTime_ms+=1001;statusAt=millis();pump();
    assert(!discover->count && !control.busy() && discover->incompleteGroups==incompleteBefore+1);
    read(0x70,0x76);read(0x71,0x05);pauseVerified();
    assert(discover->group()->complete);decision("Ignore");resume();
    // An unrelated raw candidate pauses capture mid-pair. Repeated paused
    // status packets must not queue the orphan or manufacture an INCOMPLETE.
    discover->clear();
    read(0x74,0x11);read(0xfe,0x42);assert(discover->count==1);
    pauseVerified();assert(discover->count==1 && discover->group()->first().reg==0xfe);
    discover->gap();assert(discover->count==1);
    decision("Ignore");resume();assert(!discover->count);
    read(0x75,0x22);assert(!discover->count && !control.busy());
    read(0x74,0x33);read(0x75,0x44);pauseVerified();
    assert(discover->group()->complete && discover->group()->word()==0x4433);
    decision("Keep");resume();
    // Three-byte IP2368 definitions use the same atomic group mechanism.
    discover->clear();assert(registry.assign(0x75,registry.find("ip2368"),decoding::Assignment::Manual));
    unsigned three=decoding::PropertyCount;
    for(unsigned i=0;i<decoding::PropertyCount;++i)if(decoding::properties[i].address==0x75 && decoding::properties[i].count==3){three=i;break;}
    assert(three<decoding::PropertyCount);uint8_t low=decoding::properties[three].low;
    read(low,0x12);read(uint8_t(low+1),0x34);assert(!discover->count && !control.busy());
    read(uint8_t(low+2),0x56);assert(discover->count==1 && discover->group()->word()==0x563412);
    pauseVerified();decision("Keep");resume();
    for(unsigned i=0;i<3;++i){read(uint8_t(low+i),uint8_t(i));assert(discover->decision(store->incoming)==discovery::Decision::Keep);}
    assert(!discover->count && !control.busy());
    assert(registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual));
    discover->clear();store->clear();discoveryDecoder->clear();
    selection.display=analysis::Display::Parsed;
    read(0x04,4);pauseVerified();decision("Keep");resume();refreshUi();
    bool cellsShown=false;
    for(unsigned i=0;i<visibleCount;++i)if(visibleDecoded[i] &&
        !strcmp(decoding::properties[visibleDecoded[i]->property].id,"r04_bat_num")) {
        cellsShown=true;
        assert(!analysis::bitfields(*visibleDecoded[i]) && groups[i].count==0);
        assert(!strcmp(lv_label_get_text(labels[i]),"Series cells"));
        assert(!strcmp(lv_label_get_text(values[i]),"4 cells"));
    }
    assert(cellsShown);
    read(0x3a,30);pauseVerified();decision("Keep");resume();refreshUi();
    bool temperatureExpanded=false;
    for(unsigned i=0;i<visibleCount;++i)if(visibleDecoded[i] &&
        decoding::properties[visibleDecoded[i]->property].low==0x3a)
        temperatureExpanded|=analysis::bitfields(*visibleDecoded[i]) && groups[i].count==2;
    assert(temperatureExpanded);
    puts("PASS: parsed two/three-register discovery, wait for companions, frozen full values/raw transfers, atomic Keep/Ignore, continued parsing, unknown visibility in Parsed mode, failed groups and companion timeout");
}
