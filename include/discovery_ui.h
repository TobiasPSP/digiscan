#pragma once
// Discover prompt lifecycle: pause, validate ownership, show frozen raw/parsed
// evidence, then apply Keep/Ignore to the complete logical register group.
// Unknown registers remain candidates; an empty kept set cannot mute discovery.
void receiveTransaction(const uint8_t *payload,size_t length) {
    const uint32_t now_ms=visualNow_ms();
    if(!store->add(payload,length,now_ms,!discoveryGatesMonitor())){decoderGap();return;}
    const auto &o=store->incoming;
    recorderObserve(o);
    if(o.success)sequence.synced=true;
    if(observer::addressEvidence(o))registry.observe(o.address,selection.display!=analysis::Display::Raw);
    reportCache.observeFailure(o);
    // Report acquisition stays independent of the interactive Monitor decisions.
    decoder.add(o,now_ms);
    if(!discover->enabled)return;
    if(discoveryGatesMonitor() && discover->allows(o)) {
        store->retainIncoming();discoveryDecoder->add(o,now_ms);
    }
    if(view==View::Discover && screenIndex==0) {
        auto range=discovery::scope(o);bool registers=false;
        for(unsigned i=0;i<range.bytes;++i)registers|=selection.reg(uint8_t(range.low+i));
        bool interested=selection.address(o.address) && registers &&
            (selection.outcome!=1 || o.success) && (selection.outcome!=2 || !o.success) &&
            (o.direction=='R'?selection.read:o.direction=='W'?selection.write:selection.read && selection.write);
        discover->ingest(o,interested);
    }
}
void cancelDiscoveryPrompt() {
    discoveryDialog=false; discoveryResume=false; discoveryPauseOwned=false;
    discoveryPausedVerified=false;discoveryDeferred=true;
    discover->reviewing=false;
}
void discoveryDecide(lv_event_t *event) {
    if(!discoveryDialog || !discover->front() || !connected() || !sessionReady ||
        control.busy() || control.uncertain || status[0] || !discoveryPausedVerified)return;
    const bool keep=reinterpret_cast<uintptr_t>(lv_event_get_user_data(event))==1;
    if(!discover->hasDecisions() || (keep && !discover->kept)) {
        // Switching from unrestricted/exclusion-only traffic to explicit choices
        // must not leave previously unapproved observations in the kept cache.
        store->uniqueCount=store->historyCount=store->historyHead=0;++store->revision;
        discoveryDecoder->clear();discoveryDecoder->setChangeFactor(selection.changeFactor);
    }
    if(!keep && !discover->kept) {
        // Rebuild exclusion-only display without the key currently being ignored.
        store->uniqueCount=store->historyCount=store->historyHead=0;++store->revision;
        discoveryDecoder->clear();discoveryDecoder->setChangeFactor(selection.changeFactor);
    }
    if(keep) {
        const auto &candidate=*discover->group();
        for(unsigned i=0;i<candidate.range.bytes;++i)if(candidate.present&(1u<<i)) {
            store->incoming=candidate.transactions[i];
            store->incoming.changed_ms=store->incoming.observed_ms=visualNow_ms();
            store->retainIncoming();discoveryDecoder->add(store->incoming,visualNow_ms());
        }
    }
    discover->decide(keep?discovery::Decision::Keep:discovery::Decision::Ignore);
    discoveryDialog=false;closeDetail(nullptr);
    listInitialized=false;groupedOnce=false;
    discoveryTick();
}
void openDiscoveryPrompt() {
    const auto *o=discover->front();if(!o || modal)return;
    const auto &candidate=*discover->group();
    modal=lv_obj_create(lv_screen_active());flat(modal,Bg);lv_obj_set_size(modal,W,H);
    lv_obj_remove_flag(modal,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_pad_all(modal,0,0);
    discoveryDialog=true;
    discover->reviewing=true;
    text(modal,16,18,220,"DISCOVER",&lv_font_montserrat_24);
    auto *later=button(modal,240,12,64,"Later",closeDetail,0);lv_obj_set_height(later,36);
    auto *keep=button(modal,16,55,136,"Keep",discoveryDecide,1);
    auto *ignore=button(modal,168,55,136,"Ignore",discoveryDecide,0);
    ui_update::bgColor(keep,lv_color_hex(0x245b3a),0);ui_update::textColor(keep,lv_color_hex(Green),0);
    ui_update::bgColor(ignore,lv_color_hex(0x732d34),0);ui_update::textColor(ignore,lv_color_hex(Red),0);
    auto *paused=text(modal,16,103,288,"CAPTURE PAUSED");ui_update::textColor(paused,lv_color_hex(Amber),0);
    auto *body=lv_obj_create(modal);flat(body,Bg);lv_obj_set_pos(body,0,128);lv_obj_set_size(body,W,H-128);
    lv_obj_set_style_pad_all(body,0,0);lv_obj_set_scroll_dir(body,LV_DIR_VER);
    char header[64];snprintf(header,sizeof(header),"0x%02X   0x%02X   %c",o->address,o->reg,o->direction);
    if(candidate.range.bytes>1)snprintf(header,sizeof(header),"0x%02X  %02X-%02X  %c",o->address,candidate.range.low,candidate.range.low+candidate.range.bytes-1,o->direction);
    auto *key=text(body,16,8,288,header,&lv_font_montserrat_24);
    ui_update::textColor(key,lv_color_hex(Cyan),0);
    text(body,16,44,288,"Address       Register      Direction",&lv_font_montserrat_12);
    auto *result=text(body,16,70,288,!candidate.success?"FAIL":candidate.complete?"OK":"INCOMPLETE");
    ui_update::textColor(result,lv_color_hex(candidate.success && candidate.complete?Green:Red),0);
    // MaxPayload bounds all frames; 16 KiB can represent every byte with ACK/NACK.
    int y=100;
    for(unsigned n=0;n<candidate.range.bytes;++n)if(candidate.present&(1u<<n)) {
      const auto &rawObservation=candidate.transactions[n];size_t at=10,used=0;
      for(unsigned f=0;f<rawObservation.raw[8];++f) {
        unsigned count=bringup::le16(rawObservation.raw+at);at+=5;
        if(f)used+=snprintf(detailText+used,16384-used,"; ");
        for(unsigned b=0;b<count;++b)
            used+=snprintf(detailText+used,16384-used,"%02X%c ",rawObservation.raw[at+b*2],rawObservation.raw[at+b*2+1]?'+':'-');
        at+=count*2;
      }
      detailText[used]=0;
      auto *raw=inset(body,8,y,304,100);char heading[64];
      snprintf(heading,sizeof(heading),"RAW 0x%02X / %s / + ACK / - NACK",rawObservation.reg,rawObservation.success?"OK":"FAIL");
      int height=detailLine(raw,10,heading,&lv_font_montserrat_10,Muted);
      height=detailLine(raw,height,detailText,&mono::font,Ink);lv_obj_set_height(raw,height);insetEdge(raw);
      y+=height+10;
    }
    if(candidate.problem)y=detailLine(body,y,candidate.problem,&lv_font_montserrat_12,Amber);
    if(candidate.range.parsed && o->dataBytes!=1) {
        char payloadInfo[128];snprintf(payloadInfo,sizeof(payloadInfo),
            "Observed %u payload byte%s; profile expects one value byte per register.",o->dataBytes,o->dataBytes==1?"":"s");
        y=detailLine(body,y,payloadInfo,&lv_font_montserrat_12,Amber);
        if(o->direction=='W' && !o->dataBytes)
            y=detailLine(body,y,"Register selector only; no property value was written.",&lv_font_montserrat_12,Muted);
    }
    const auto &binding=registry.bindings[o->address];
    char profile[128];snprintf(profile,sizeof(profile),"PARSER / %s",binding.profile>=0?decoding::profiles[binding.profile].name:
        binding.mode==decoding::Assignment::Disabled?"No parsing selected":"No profile assigned");
    y=detailLine(body,y,profile,&lv_font_montserrat_12,Cyan);
    unsigned matched=0;
    for(unsigned i=0;i<decoding::PropertyCount;++i) {
        analysis::Value value;
        if(!discovery::propertySnapshot(candidate,i,value))continue;
        if(!matched && o->dataBytes==1) {
            char binary[9],line[80];for(unsigned bit=0;bit<8;++bit)binary[bit]=(o->preview[0]&(1u<<(7-bit)))?'1':'0';binary[8]=0;
            snprintf(line,sizeof(line),"Raw Value 0x%02X / Dec %u / %s",o->preview[0],o->preview[0],binary);
            if(candidate.range.bytes==1)y=detailLine(body,y,line,&lv_font_montserrat_12,Muted);
            else if(candidate.complete) {
                snprintf(line,sizeof(line),"Raw Value 0x%0*X / Dec %u",candidate.range.bytes*2,candidate.word(),candidate.word());
                y=detailLine(body,y,line,&lv_font_montserrat_12,Muted);
            }
        }
        if(!value.complete) {
            char line[160];snprintf(line,sizeof(line),"%s: needs %u register byte%s (0x%02X-0x%02X). Value not inferred from this transaction.",
                decoding::properties[i].name,decoding::properties[i].count,decoding::properties[i].count==1?"":"s",decoding::properties[i].low,unsigned(decoding::properties[i].low)+decoding::properties[i].count-1);
            y=detailLine(body,y,line,&lv_font_montserrat_12,Amber);
        }
        if(o->direction=='W' && !decoding::properties[i].writable)
            y=detailLine(body,y,"Profile marks this property read-only. This write does not establish its device state.",&lv_font_montserrat_12,Amber);
        y=detailProperty(body,y,value,decoding::PropertyCount);++matched;
    }
    if(!matched)detailLine(body,y,"No matching property definition for this register and direction.",&lv_font_montserrat_12,Muted);
}
void discoveryTick() {
    if(!discover || !discover->enabled)return;
    discover->expire(visualNow_ms());
    if(!connected() || !sessionReady || control.uncertain) {
        discoveryResume=false;discoveryPausedVerified=false;
        if(discoveryDialog)closeDetail(nullptr);
        return;
    }
    if(view!=View::Discover || screenIndex!=0 || discoveryDeferred || activeBusy() || rateRequest)return;
    if(control.busy())return;
    if(discover->count) {
        if(!discoveryPauseOwned && !discoveryPausedVerified) {
            const bool running=status[0];
            if(control.begin(observer::Action::DiscoverPause)) {
                discoveryPauseOwned=true;discoveryResume=running;
                updateVisualClock();
            }
            return;
        }
        if(discoveryPausedVerified && !status[0] && rxAt==rxCount && !uart.available() && !modal)
            openDiscoveryPrompt();
    } else if(discoveryPauseOwned && discoveryPausedVerified && !discoveryDialog) {
        const bool resume=discoveryResume && !discover->overflow;
        discoveryResume=false;discoveryPauseOwned=false;discoveryPausedVerified=false;
        if(resume)control.begin(observer::Action::Enable);
        else if(discover->overflow)control.message="DISCOVER QUEUE FULL / REVIEW THEN TAP RUN";
    }
}
