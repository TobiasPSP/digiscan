// Runs inside the actual LVGL preview fixture, using the firmware UART mock.
{
    uint8_t bytes[256];unsigned count=0;uint32_t period=0;
    assert(experiment::payload("AA,55 00 FF",experiment::Format::Hex,bytes,count) && count==4 && bytes[3]==255);
    assert(experiment::payload("4660",experiment::Format::U16LE,bytes,count) && count==2 && bytes[0]==0x34 && bytes[1]==0x12);
    assert(experiment::payload("4660",experiment::Format::U16BE,bytes,count) && bytes[0]==0x12 && bytes[1]==0x34);
    assert(experiment::payload("1.0",experiment::Format::FloatLE,bytes,count) && count==4 && bytes[2]==0x80 && bytes[3]==0x3f);
    assert(experiment::payload("0b10101010",experiment::Format::Binary,bytes,count) && bytes[0]==0xaa);
    assert(!experiment::payload("0b",experiment::Format::Binary,bytes,count));
    assert(!experiment::payload("256",experiment::Format::Decimal,bytes,count));
    assert(!experiment::payload("65536",experiment::Format::U16LE,bytes,count));
    assert(!experiment::payload("nan",experiment::Format::FloatBE,bytes,count));
    assert(experiment::payload("-32768",experiment::Format::I16LE,bytes,count) && count==2 && bytes[0]==0 && bytes[1]==0x80);
    assert(experiment::payload("-2147483648",experiment::Format::I32BE,bytes,count) && count==4 && bytes[0]==0x80 && bytes[3]==0);
    assert(!experiment::payload("32768",experiment::Format::I16LE,bytes,count));
    assert(!experiment::payload("-2147483649",experiment::Format::I32BE,bytes,count));
    char full[257];memset(full,'A',256);full[256]=0;
    assert(experiment::payload(full,experiment::Format::Text,bytes,count) && count==256);
    assert(experiment::refresh("0",period) && period==0);
    assert(experiment::refresh("3600.0",period) && period==3600000);
    assert(experiment::refresh("0.1",period) && period==100);
    assert(!experiment::refresh("3600.1",period) && !experiment::refresh("1.01",period));
    puts("PASS: experimental write formats, byte order, float encoding, bounds and refresh interval validation");

    if(modal)extraClose(nullptr);
    control=observer::Control{};reportBatch=acquisition::Batch{};activePending=extraPending=0;
    extraWork=Work::Idle;extraSessionBusy=false;seen=sessionReady=true;needsSync=false;
    statusAt=millis();status[0]=status[1]=status[2]=0;bringup::put32(status+132,127);rateRequest=0;
    graphGeneration=registry.generation;
    auto rawReply=[&](uint8_t code,uint8_t value=0){
        uint8_t reply[32]={};bringup::put32(reply,extraPending);reply[4]=code;reply[5]=code?0:1;reply[6]=1;
        reply[7]=extraRegister;reply[24]=value;reply[27]=3;reply[28]=extraOp==16?1:extraOp==17?2:0;reply[29]=extraAddress;
        reply[30]=uint8_t(extraWriteCount);reply[31]=uint8_t(extraWriteCount>>8);
        uint32_t bad=receiver.bad;inject(6,reply,32);pump();assert(receiver.bad==bad);
    };
    lv_textarea_set_text(writeAddress,"75");lv_dropdown_set_selected(writePrefixKind,1);
    lv_textarea_set_text(writePrefix,"03");lv_dropdown_set_selected(writeFormat,0);lv_textarea_set_text(writePayload,"AA BB 45");
    writePrepare(nullptr);assert(modal && writeCount==4 && writeBytes[0]==3 && hasLabel(modal,"CONFIRM EXPERIMENTAL WRITE"));
    uart.output.clear();writeCommit(nullptr);assert(extraPending && extraOp==16 && extraAddress==0x75 && !modal);
    rawReply(0);assert(!activeBusy());
    lv_dropdown_set_selected(writePrefixKind,2);lv_textarea_set_text(writePrefix,"1234");lv_dropdown_set_selected(writeFormat,3);lv_textarea_set_text(writePayload,full);
    writePrepare(nullptr);assert(modal && writeCount==258 && writeBytes[0]==0x34 && writeBytes[1]==0x12);
    uart.output.clear();writeCommit(nullptr);assert(extraPending && uart.output.size()>258 && uart.output.size()<320);rawReply(0);assert(!activeBusy());
    lv_dropdown_set_selected(writePrefixKind,1);lv_textarea_set_text(writePrefix,"03");lv_dropdown_set_selected(writeFormat,0);lv_textarea_set_text(writePayload,"AA BB 45");
    for(auto &binding:registry.bindings)binding.confirmed=false;++registry.evidenceGeneration;
    scanStart(nullptr);assert(extraSessionBusy && extraWork==Work::Scan);
    for(unsigned address=8;address<120;++address){
        statusAt=millis();extraTick();assert(extraPending && extraAddress==address && extraOp==17);
        rawReply(address==0x10 || address==0x75?0:address==0x20?1:2,0x42);
    }
    extraTick();refreshExtras();assert(!activeBusy() && extraWork==Work::Idle && scanFound[0x75]);
    assert(scanFound[0x10] && scanTiles[0x10] && scanTiles[0x75] && scanSkipped==1);
    for(auto &binding:registry.bindings)binding.confirmed=false;++registry.evidenceGeneration;
    bringup::put32(status+132,255);statusAt=millis();scanStart(nullptr);uart.output.clear();extraTick();
    assert(extraPending && extraOp==19);const uint32_t batchId=extraPending;
    auto scanReply=[&](unsigned address,uint8_t code,bool final){
        uint8_t reply[32]={};bringup::put32(reply,batchId);reply[4]=code;reply[5]=code?0:1;
        reply[6]=final;reply[27]=4;reply[28]=2;reply[29]=uint8_t(address);reply[30]=final;reply[31]=119;
        uint32_t bad=receiver.bad;inject(6,reply,32);pump();assert(receiver.bad==bad);
    };
    for(unsigned address=8;address<120;++address){
        assert(extraPending==batchId && scanNext==address);statusAt=millis();extraTick();
        scanReply(address,address==0x10 || address==0x75?0:2,address==119);
    }
    assert(!activeBusy() && scanNext==120 && scanFound[0x10] && scanFound[0x75] && scanSkipped==0);
    refreshExtras();
    statusAt=millis();scanStart(nullptr);extraTick();const uint32_t cancelledId=extraPending;
    cancelExtra(nullptr);assert(extraPending==cancelledId && activeBusy());
    uint8_t cancelled[32]={};bringup::put32(cancelled,cancelledId);cancelled[4]=6;cancelled[6]=1;
    cancelled[27]=4;cancelled[28]=2;cancelled[29]=8;cancelled[30]=1;cancelled[31]=uint8_t(scanEnd);inject(6,cancelled,32);pump();
    assert(!activeBusy() && scanSkipped==112 && sessionReady);
    // Retain confirmed tiles for the shared-association tests below.
    scanFound[0x10]=scanFound[0x75]=true;scanDirty=true;refreshExtras();bringup::put32(status+132,127);
    puts("PASS: new batch scan, ordered progress, full 112-address retention, legacy fallback and cancellation waits for restoration");
    lv_textarea_set_text(scanAddress,"75");identifyStart(nullptr);assert(extraWork==Work::Identify);
    for(unsigned n=0;n<identifyCount;++n){extraTick();assert(extraPending && extraOp==18);rawReply(0,extraRegister==4?4:extraRegister==0x39?1:0);}
    extraTick();assert(!activeBusy() && strstr(identifyOutput,"NOT A CHIP-ID"));
    auto setIdentWord=[&](unsigned reg,unsigned value){identifyValid[reg]=identifyValid[reg+1]=true;identifyBytes[reg]=uint8_t(value);identifyBytes[reg+1]=uint8_t(value>>8);};
    identifyValid[0x33]=identifyValid[0x76]=true;identifyBytes[0x76]=0;
    setIdentWord(0x52,20000);setIdentWord(0x70,2000);setIdentWord(0x74,4000);identifyBytes[0x33]=0x86;identifyBytes[0x39]=0;
    identificationResult();assert(strstr(identifyOutput,"16-bit / 10 mW): match") && !strstr(identifyOutput,"EXCLUDED"));
    setIdentWord(0x74,40000);identifyBytes[0x33]=0x87;identifyBytes[0x04]=100;
    identificationResult();assert(strstr(identifyOutput,"24-bit / 1 mW): match") && !strstr(identifyOutput,"POSITIVE ID"));
    memset(identifyValid,0,sizeof(identifyValid));identifyValid[4]=identifyValid[0x39]=true;identifyBytes[4]=4;identifyBytes[0x39]=1;
    identificationResult();assert(strstr(identifyOutput,"Capacity code; all seven-bit codes compatible") && !strstr(identifyOutput,"EXCLUDED") && !strstr(identifyOutput,"score"));
    using identification::Class;using identification::Outcome;
    assert(identification::evaluate(Class::B,false,0,255,1,3)==Outcome::Inconclusive);
    assert(identification::evaluate(Class::B,true,0,255,1,3)==Outcome::Contradiction);
    assert(identification::evaluate(Class::C,true,0,255,1,3)==Outcome::NoMatch);
    identification::Evidence e;e.add(Class::A,Outcome::Match);assert(!strcmp(e.result(),"POSITIVE ID"));
    e.add(Class::B,Outcome::Contradiction);assert(!strcmp(e.result(),"CONFLICTING EVIDENCE"));
    puts("PASS: original 1/0/3 evidence remains nonexclusive; A/B/C semantics and failed-read inconclusiveness");
    puts("PASS: confirmed write, all 112 bounded scan probes, NACK continuation, full result retention and advisory identification");
    statusAt=millis();lv_obj_send_event(scanTiles[0x75],LV_EVENT_CLICKED,nullptr);
    assert(modal && extraWork==Work::Identify && identifyChoice && scanList);
    buttonColor(modal,"Done",0x238557);
    for(unsigned n=0;n<identifyCount;++n){extraTick();rawReply(0,extraRegister==4?4:0);}
    extraTick();assert(extraWork==Work::Idle && strstr(lv_label_get_text(scanList),"NOT A CHIP-ID"));
    lv_dropdown_set_selected(identifyChoice,registry.find("ip2366")+2);save("tablet-v14-identification-dialog.ppm");
    identifyAccept(nullptr);refreshExtras();
    assert(!modal && !scanList && !identifyChoice && registry.bindings[0x75].profile==registry.find("ip2366"));
    assert(!strcmp(lv_label_get_text(scanNames[0x75]),"IP2366"));
    assert(registry.bindings[0x75].mode==decoding::Assignment::Manual);
    assert(assignAddressProfile(0x75,registry.find("ip2369")));refreshExtras();
    assert(!strcmp(lv_label_get_text(scanNames[0x75]),"IP2369") && registry.bindings[0x75].dirty);
    assert(assignAddressProfile(0x75,registry.find("ip2366")));refreshExtras();
    assert(!strcmp(lv_label_get_text(scanNames[0x75]),"IP2366"));
    statusAt=millis();lv_obj_send_event(scanTiles[0x75],LV_EVENT_CLICKED,nullptr);
    assert(lv_dropdown_get_selected(identifyChoice)==unsigned(registry.find("ip2366")+2));
    lv_dropdown_set_selected(identifyChoice,registry.find("ip2369")+2);
    identifyDismiss(nullptr);assert(registry.bindings[0x75].profile==registry.find("ip2366"));
    extraPending=0;extraWork=Work::Idle;extraSessionBusy=false;
    puts("PASS: shared scan/parser association, live tile refresh, persistence dirty flag and X preserves assignment");

    graphOptionsRefresh();assert(graphOptionsCount);
    lv_dropdown_set_selected(graphChoice,1);graphSelect(nullptr);assert(plotCount==2);
    pump();while(control.busy())acknowledge();extraTick();
    graphActive=true;graphDue=millis();extraTick();assert(graphRequest && activePending && graphReadAt==0);
    uint32_t firstRequest=graphRequest;
    extraTick();assert(graphRequest==firstRequest && graphReadAt==0); // Never skip a pending line.
    const auto d=activeDefinition;uint8_t reply[32]={};bringup::put32(reply,activePending);
    reply[5]=d.count;reply[6]=1;reply[7]=d.low+d.count-1;reply[24]=0x34;reply[25]=0x12;
    reply[27]=2;reply[29]=d.address;reply[30]=d.count;reply[31]=d.low;
    assert(!extraPacket({6,2,reply,32}));activePacket({6,2,reply,32});extraTick();
    assert(graphReadAt==1 && graphRequest && activePending);
    bringup::put32(reply,activePending);reply[4]=4;reply[5]=0;
    reply[29]=activeDefinition.address;reply[30]=activeDefinition.count;reply[31]=activeDefinition.low;
    assert(!extraPacket({6,3,reply,32}));activePacket({6,3,reply,32});extraTick();
    assert(!graphActive && !graphReading && !activeBusy());
    assert(graphStopped);graphToggle(nullptr);assert(!graphStopped);
    pump();while(control.busy())acknowledge();extraTick();
    // The presentation clock is frozen when capture is paused; graph events
    // must still distinguish two completed active reads with the same timestamp.
    auto &cache=reportCache.readings[plots[0].property];auto sample=cache.lastGood;
    sample.complete=sample.success=true;sample.direction='R';sample.word=14000;reportCache.accept(sample);
    previewTime_ms+=101;statusAt=millis();extraTick();uint32_t revision=plots[0].last;
    sample.word=15000;reportCache.accept(sample);previewTime_ms+=101;statusAt=millis();extraTick();assert(plots[0].last!=revision);
    revision=plots[0].last;cancelExtra(nullptr);sample.word=16000;reportCache.accept(sample);
    status[0]=1;previewTime_ms+=101;statusAt=millis();extraTick();assert(graphStopped && plots[0].last==revision);
    lv_dropdown_set_selected(graphChoice,1);graphSelect(nullptr);assert(!graphStopped);
    pump();while(control.busy())acknowledge();extraTick();
    status[0]=0;
    screenIndex=4;refreshExtras();assert(hasLabel(graphButton,"STOP"));
    assert(lv_color_eq(lv_obj_get_style_bg_color(graphButton,0),lv_color_hex(0x9c3544)));
    lv_obj_send_event(graphButton,LV_EVENT_CLICKED,nullptr);refreshExtras();assert(graphStopped && hasLabel(graphButton,"START"));
    assert(lv_color_eq(lv_obj_get_style_bg_color(graphButton,0),lv_color_hex(0x238557)));
    lv_obj_send_event(graphButton,LV_EVENT_CLICKED,nullptr);refreshExtras();assert(!graphStopped && hasLabel(graphButton,"STOP"));
    pump();while(control.busy())acknowledge();extraTick();
    assert(lv_color_eq(lv_obj_get_style_line_color(graphChart,LV_PART_MAIN),lv_color_hex(0x3c454b)));
    puts("PASS: active graph request ownership, no skipped properties, cached reads and stop on arbitration failure");

    // Render every new screen and the confirmation dialog with the real LVGL renderer.
    screenIndex=2;reportActive=false;slidePosition(nullptr,-int(pagePosition(screenIndex))*W);refreshExtras();save("tablet-v14-write.ppm");
    writePrepare(nullptr);assert(modal);save("tablet-v14-write-confirm.ppm");extraClose(nullptr);
    screenIndex=3;slidePosition(nullptr,-int(pagePosition(screenIndex))*W);refreshExtras();save("tablet-v14-identification.ppm");
    for(unsigned i=0;i<40;++i){sample.word=12000+i*100;reportCache.accept(sample);previewTime_ms+=101;statusAt=millis();extraTick();}
    screenIndex=4;slidePosition(nullptr,-int(pagePosition(screenIndex))*W);refreshExtras();save("tablet-v14-graph.ppm");
    graphManual(nullptr);assert(modal);buttonColor(modal,"Apply",0x238557);save("tablet-v14-graph-setup.ppm");extraClose(nullptr);
    buttonColor(extraScreens[0],"Review write",0x238557);buttonColor(diagPanels[5].root,"SET BAUD",0x238557);
    puts("PASS: primary action colors, IDENTIFY Done, Graph Apply, Review Write, SET BAUD and Read/Cancel states");
    screenIndex=0;reportActive=false;slidePosition(nullptr,-int(pagePosition(screenIndex))*W);
    openProfile(0x75);assert(modal);navigateReport(true);assert(!modal && screenIndex==1);
    lv_anim_refr_now();previewTime_ms+=250;lv_anim_refr_now();assert(!sliding);
    puts("PASS: graph START/STOP colors, dark guides and Monitor dialog dismissal on navigation");
    const unsigned order[]={3,0,1,4,5,6,2};
    screenIndex=3;reportActive=false;slidePosition(nullptr,0);
    for(unsigned i=0;i<7;++i){
        assert(screenIndex==order[i]);slideDone(nullptr);
        if(i<6){navigateReport(true);lv_anim_refr_now();previewTime_ms+=250;lv_anim_refr_now();}
    }
    lv_obj_update_layout(lv_screen_active());assert(lv_obj_get_x(extraScreens[0])==0);
    restoreScreen();assert(screenIndex==3);startWorkspace(true);
    puts("PASS: START/Monitor/Reports/Graph/Recorder/Read/Write order; boot always selects START instead of restoring a page");
    puts("PASS: new Write/Scan/Identification/Graph LVGL screens and dialogs rendered");
}
