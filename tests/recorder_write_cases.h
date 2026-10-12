{
    if(modal)closeDetail(nullptr);control.abort("recorder test reset");control.uncertain=false;
    activePending=extraPending=0;reportBatch.running=extraSessionBusy=false;extraWork=Work::Idle;graphStopped=true;graphActive=false;
    discover->clear();discover->enabled=false;selection=analysis::Selection{};selection.anyAddress=true;selection.addresses[0x75]=true;
    selection.read=true;selection.write=false; // Monitor direction does not suppress Recorder writes.
    registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual);
    recorderPhase=RecorderPhase::Idle;recorder->reset();recorderPending=0;recorderFilterRefresh();lv_dropdown_set_selected(recorderChoice,0);
    sessionReady=delivering=true;needsSync=false;status[0]=1;status[1]=status[2]=0;status[3]=1;statusAt=millis();rateRequest=0;defaultBaudAttempted=true;
    refreshRecorder();assert(lv_obj_has_state(recorderPlay,LV_STATE_DISABLED));assert(!hasLabel(recorderScreen,"Read Missing") && hasLabel(recorderScreen,"START"));
    auto observation=[](uint8_t reg,uint8_t value,bool read,uint32_t id){
        std::vector<uint8_t> p;if(read)p={1,0,0,0,1,1,0x75,reg,2,0,2,0,1,0,0,0xea,1,reg,1,2,0,0,0,0,0xeb,1,value,0};
        else p={1,0,0,0,1,1,0x75,reg,1,0,3,0,0,0,0,0xea,1,reg,1,value,1};bringup::put32(p.data(),id);return p;
    };
    lv_obj_send_event(recorderArm,LV_EVENT_CLICKED,nullptr);assert(recorderPhase==RecorderPhase::Timing && recorder->keyCount==256 && recorder->find(0x75,0xee)>=0);
    pump();assert(recorderPending && recorderOpcode==20);
    uint8_t timingAck[6];bringup::put32(timingAck,recorderPending);timingAck[4]=20;timingAck[5]=0;inject(2,timingAck,6);pump();
    for(unsigned n=0;n<12 && recorderPhase!=RecorderPhase::Recording;++n){if(control.pendingId)acknowledge();else pump();}
    assert(recorderPhase==RecorderPhase::Recording && recorder->recording && status[0]);refreshRecorder();assert(hasLabel(recorderArm,"DONE") && lv_obj_has_state(recorderPlay,LV_STATE_DISABLED));
    auto timed=[&](uint8_t reg,uint8_t value,bool read,uint32_t id,uint64_t start,uint8_t address=0x75){
        auto p=observation(reg,value,read,id);p[6]=address;p[15]=uint8_t(address<<1);if(read)p[24]=uint8_t((address<<1)|1);
        uint8_t packed[128];size_t bytes=transport5::pack(p.data(),p.size(),packed,sizeof(packed));assert(bytes);
        unsigned count=read?2:1,head=12+16*count;std::vector<uint8_t> wire(head+bytes);wire[0]=wire[1]=1;wire[2]=count;bringup::put32(wire.data()+4,val(4));bringup::put32(wire.data()+8,150000000);
        for(unsigned f=0;f<count;++f){recorder_wire::put64(wire.data()+12+16*f,start+f*15000);recorder_wire::put64(wire.data()+20+16*f,start+(f+1)*15000);}
        memcpy(wire.data()+head,packed,bytes);inject(7,wire.data(),wire.size());pump();
    };
    timed(0x33,0x81,true,100,500000);assert(!recorder->count);
    timed(0x22,0x40,false,101,1500000);timed(0x22,0x40,true,102,2000000);timed(0x22,0x40,false,103,3300000);
    timed(0x0b,0x80,false,104,5100000);timed(0x0b,0x81,false,105,6600000);
    assert(recorder->count==4 && recorder->entries[0].after.data[0]==recorder->entries[1].after.data[0]);
    previewTime_ms+=5000;statusAt=millis();pump();assert(recorderPhase==RecorderPhase::Recording); // No change-based auto-stop.
    lv_obj_send_event(recorderArm,LV_EVENT_CLICKED,nullptr);
    for(unsigned n=0;n<8 && recorderBusy();++n){if(control.pendingId)acknowledge();else pump();}
    assert(!recorderBusy() && !recorder->recording && recorder->complete && !status[0]);
    screenIndex=5;slidePosition(nullptr,-4*W);refreshRecorder();save("tablet-v19-recorder.ppm");
    assert(!lv_obj_has_state(recorderPlay,LV_STATE_DISABLED));char parsed[1000];recorderFields(recorder->entries[0],false,parsed,sizeof(parsed));assert(strstr(parsed,"Vbus_Mode_Set") && strstr(parsed,"DFP"));
    lv_obj_update_layout(recorderBody);lv_obj_scroll_to_y(recorderBody,2000,LV_ANIM_OFF);refreshRecorder();
    auto *cb=lv_obj_get_child(recorderRows[3],0);lv_obj_remove_state(cb,LV_STATE_CHECKED);lv_obj_send_event(cb,LV_EVENT_VALUE_CHANGED,nullptr);assert(!recorder->entries[3].checked);
    lv_obj_scroll_to_y(recorderBody,0,LV_ANIM_OFF);refreshRecorder();
    auto result=[&](uint8_t value=0,uint8_t code=0){
        assert(recorderPending && (recorderOpcode==16 || recorderOpcode==18));uint8_t r[32]={};bringup::put32(r,recorderPending);r[4]=code;r[5]=code?0:1;r[6]=1;r[7]=recorderRegister;r[29]=recorderAddress;r[27]=3;r[28]=recorderOpcode==16?1:0;
        if(recorderOpcode==16){unsigned count=recorder->entries[recorderAt].after.count+1;r[30]=uint8_t(count);r[31]=uint8_t(count>>8);}else r[24]=value;
        inject(6,r,32);statusAt=millis();pump();
    };
    auto startPlayback=[&](){recorderPlayAction(nullptr);assert(modal);recorderPlayConfirmed(nullptr);pump();
        for(unsigned n=0;n<8 && control.busy();++n){if(control.pendingId)acknowledge();else pump();}pump();assert(recorderPhase==RecorderPhase::Preflight && recorderOpcode==18 && recorderRegister==0x38);};
    startPlayback();result();assert(recorderPending && recorderOpcode==16 && recorderAt==0); // No initial 10 ms idle wait.
    result();assert(recorderPhase==RecorderPhase::FaultAfter && recorderRegister==0x38);result();assert(recorderAt==1 && !recorderPending);
    previewTime_ms+=12;statusAt=millis();pump();assert(recorderOpcode==16 && recorderAt==1);result();result();assert(recorderAt==2 && !recorderPending);
    previewTime_ms+=12;statusAt=millis();pump();assert(recorderOpcode==16 && recorderAt==2);result();result();
    assert(recorderPhase==RecorderPhase::PostStart && recorder->entries[2].replayed && !recorder->entries[3].replayed && strstr(recorderMessage,"3 writes"));
    for(unsigned n=0;n<12 && recorderPhase!=RecorderPhase::PostWatch;++n){if(control.pendingId)acknowledge();else pump();}
    assert(recorderPhase==RecorderPhase::PostWatch && status[0] && control.filterCommand[1]==11 && control.filterCommand[2]==3);
    assert(recorderPost->includes(0x75,0x22) && recorderPost->includes(0x75,0x0b) && !recorderPost->includes(0x42,0x0b));
    timed(0x33,0x80,true,200,75000);timed(0x22,0x00,false,201,150000);timed(0x0b,0x80,false,202,300000,0x42);
    timed(0x33,0x81,false,203,450000);timed(0x0b,0x80,false,204,600000);
    assert(recorderPost->count==2 && recorder->count==4 && recorderPost->entries[1].raw[6]==0x75);
    assert(recorderPost->detail(0).after.data[0]==0 && recorderPost->detail(0).transaction.reg==0x22);
    previewTime_ms+=4999;statusAt=millis();pump();assert(recorderPhase==RecorderPhase::PostWatch);
    previewTime_ms+=1;statusAt=millis();pump();
    for(unsigned n=0;n<12 && recorderBusy();++n){if(control.pendingId)acknowledge();else pump();}
    assert(!recorderBusy() && !status[0] && modal && hasLabel(modal,"POST-PLAYBACK WRITES"));
    assert(control.filterCommand[2]==1);save("tablet-v19-post-playback.ppm");closeDetail(nullptr);
    refreshRecorder();save("tablet-v19-playback.ppm");
    startPlayback();result();assert(recorderOpcode==16);result();result(0x20);assert(!recorderBusy() && strstr(recorder->message,"ABORT") && !recorder->entries[1].replayed);
    startPlayback();result(0x10);assert(!recorderBusy() && !recorder->entries[0].replayed);
    for(unsigned i=0;i<4;++i)recorder->entries[i].checked=i==1;
    status[0]=1;statusAt=millis();startPlayback();result();assert(recorderOpcode==16 && recorderAt==1 && recorderOrigin_cycles==3300000);
    result();result();for(unsigned n=0;n<12 && recorderPhase!=RecorderPhase::PostWatch;++n){if(control.pendingId)acknowledge();else pump();}
    assert(recorderPhase==RecorderPhase::PostWatch);recorderAbort(nullptr);pump();
    for(unsigned n=0;n<18 && recorderBusy();++n){if(control.pendingId)acknowledge();else pump();}
    assert(!recorderBusy() && status[0] && !modal && !recorderPost->active);status[0]=0;
    puts("PASS: immediate first checked write, retained later intervals, 5s played-register follow-up writes, other addresses/registers and reads excluded, response dialog, original log retained, paused/running restoration and cancellation");
    recorder->reset();assert(recorderBuildKeys() && recorder->keyCount==256);recorder->start();
    auto raw=observation(0x22,0x40,false,700);observer::parse(raw.data(),raw.size(),store->incoming);
    recorder_wire::Timing t;t.valid=true;t.count=1;t.hz=150000000;t.epoch=3;t.start[0]=150000;t.end[0]=170000;
    recorder->observe(store->incoming,&t,millis());assert(recorder->count==1);t.epoch=4;recorder->observe(store->incoming,&t,millis());assert(!recorder->complete && !recorder->recording);
    recorder->reset();recorder->addKey(0x75,0x22);recorder->start();recorder->observe(store->incoming,nullptr,millis());assert(!recorder->complete);
    recorder->reset();recorder->addKey(0x75,0x22);recorder->start();t.epoch=5;
    for(unsigned i=0;i<256;++i){t.start[0]=150000+uint64_t(i)*50000;t.end[0]=t.start[0]+10000;recorder->observe(store->incoming,&t,millis());}
    assert(recorder->count==256 && !recorder->recording && recorder->complete);refreshRecorder();lv_obj_update_layout(recorderBody);lv_obj_scroll_to_y(recorderBody,100000,LV_ANIM_OFF);refreshRecorder();assert(recorderRows[255]);
    lv_mem_monitor_t memory;lv_mem_monitor(&memory);assert(memory.free_size>32768);
    recorder->reset();refreshRecorder();assert(!recorderRows[0] && lv_obj_has_state(recorderPlay,LV_STATE_DISABLED));
    for(const char *id:{"ip2366","ip2368","ip2369"}){assert(registry.assign(0x75,registry.find(id),decoding::Assignment::Manual));recorderFilterRefresh();
        unsigned preset=0;for(unsigned i=0;i<recorderFilterOptionCount;++i)if(recorderFilterOptions[i].address==0x75 && !strcmp(experiment::filterLists[recorderFilterOptions[i].list].profile,id))preset=i+1;
        assert(preset);lv_dropdown_set_selected(recorderChoice,preset);assert(recorderBuildKeys() && recorder->keyCount==5 && recorder->find(0x75,0x0b)>=0 && recorder->find(0x75,0x22)<0);}
    lv_dropdown_set_selected(recorderChoice,0);selection.anyAddress=false;assert(recorderBuildKeys() && recorder->keyCount==112*256 && recorder->find(0x42,0xee)>=0);
    selection.anyAddress=true;selection.anyRegister=true;selection.registers[0x22]=true;assert(recorderBuildKeys() && recorder->keyCount==1 && recorder->find(0x75,0x0b)<0);
    recorder->reset();recorder->addKey(0x75,0x22);recorder->start();
    auto compound=observation(0x22,0x40,false,900);compound[8]=2;compound[12]=1;
    const uint8_t readFrame[]={2,0,0,0,0,0xeb,1,0x40,0};compound.insert(compound.end(),readFrame,readFrame+sizeof(readFrame));
    assert(observer::parse(compound.data(),compound.size(),store->incoming));t.count=2;t.start[0]=100;t.end[0]=200;t.start[1]=210;t.end[1]=310;
    recorder->observe(store->incoming,&t,millis());assert(recorder->count==1 && recorder->entries[0].after.data[0]==0x40 && !recorder->entries[0].checked && !recording18::replayable(recorder->entries[0]));
    recorder->reset();recorder->addKey(0x75,0x22);recorder->start();
    auto failed=observation(0x22,0x40,false,901);failed.back()=0;failed[4]=0;assert(observer::parse(failed.data(),failed.size(),store->incoming));t.count=1;
    recorder->observe(store->incoming,&t,millis());assert(recorder->count==1 && !recorder->entries[0].transaction.success && !recorder->entries[0].checked);
    recorder->reset();recorder->addKey(0x75,0x22);recorder->start();
    std::vector<uint8_t> maximum={1,0,0,0,1,1,0x75,0x22,1,0,2,1,0,0,0,0xea,1,0x22,1};
    for(unsigned i=0;i<256;++i){maximum.push_back(uint8_t(i));maximum.push_back(1);}
    assert(observer::parse(maximum.data(),maximum.size(),store->incoming));recorder->observe(store->incoming,&t,millis());
    assert(recorder->count==1 && recorder->entries[0].after.count==256 && recording18::replayable(recorder->entries[0]));
    for(unsigned i=0;i<256;++i)assert(recorder->entries[0].after.data[i]==uint8_t(i));
    recorder->stop("Boundary tests complete");
    recorderPost->begin(millis());recorderPost->select(0x75,0x22);t.count=1;t.hz=150000000;t.start[0]=100;t.end[0]=200;
    for(unsigned i=0;i<recording18::PostCapacity+2;++i)recorderPost->observe(store->incoming,&t,millis());
    assert(recorderPost->count==64 && recorderPost->dropped==2 && recorderPost->detail(63).after.count==256);
    for(unsigned i=0;i<256;++i)assert(recorderPost->preview.after.data[i]==uint8_t(i));
    recorderPost->active=false;recorderPostPage=0;recorderPostDialog();assert(modal);lv_mem_monitor(&memory);assert(memory.free_size>32768);
    for(unsigned i=1;i<16;++i){assert(recorderPostNext);lv_obj_send_event(recorderPostNext,LV_EVENT_CLICKED,nullptr);assert(recorderPostPage==i);lv_mem_monitor(&memory);assert(memory.free_size>32768);}
    assert(lv_obj_has_state(recorderPostNext,LV_STATE_DISABLED));closeDetail(nullptr);
    recorderPost->begin(millis());recorderPost->select(0x75,0x22);t.start[0]=uint64_t(t.hz)*5;t.end[0]=t.start[0]+100;
    recorderPost->observe(store->incoming,&t,millis());assert(!recorderPost->count);t.start[0]-=1;t.end[0]=t.start[0]+1;
    recorderPost->observe(store->incoming,&t,millis());assert(recorderPost->count==1);++t.epoch;recorderPost->observe(store->incoming,&t,millis());assert(recorderPost->gaps==1 && recorderPost->count==1);
    recorderPost->active=false;recorderPost->count=0;
    recorderPost->begin(millis());recorderPhase=RecorderPhase::PostWatch;recorder->complete=true;sessionReady=false;
    recorderTick();assert(!recorderBusy() && !recorderPost->active && recorderPost->gaps==1 && recorder->complete && recorder->count==1);
    assert(modal);closeDetail(nullptr);sessionReady=true;
    puts("PASS: post-watch exact 5s PIO cutoff, epoch gaps, 64-write capacity/drop accounting, full 256-byte payload and dialog heap budget");
    puts("PASS: compound write/read extraction, unsupported repeated START replay rejection, failed-write retention, exact 256-byte payload");
    puts("PASS: write-only START/DONE, no baselines, repeated identical writes, no polls/pointer writes, parsed requested values, selected/ALL unseen registers, original replay timing, write-only execution and preflight/post-write fault aborts");
    puts("PASS: 256-write prefix/capacity stop, virtual scroll memory, empty playback disabled, profile presets and capture timestamp gaps");
}
