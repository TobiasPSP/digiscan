{
    uint8_t regs[manual_read::MaxRegisters];unsigned count=0,length=0;
    assert(manual_read::registers("50, 6E, 0x70-0x72,50",regs,count) && count==5 && regs[1]==0x6e && regs[4]==0x72);
    for(const char *bad:{"","FF-00","100","00,","00;01","00-20","-1"})assert(!manual_read::registers(bad,regs,count));
    assert(manual_read::length("256",length) && length==256);
    for(const char *bad:{"0","257","-1","2.0",""})assert(!manual_read::length(bad,length));
    uint8_t sample[]={0x34,0x12};assert(manual_read::word(sample,2,false)==0x1234 && manual_read::word(sample,2,true)==0x3412);
    if(modal)extraClose(nullptr);control.abort("read test reset");control.uncertain=false;activePending=extraPending=0;reportBatch.running=false;
    recorderPhase=RecorderPhase::Idle;extraWork=Work::Idle;extraSessionBusy=false;graphActive=false;graphStopped=true;sessionReady=seen=true;needsSync=false;statusAt=millis();rateRequest=0;status[2]=0;
    lv_textarea_set_text(readAddress,"75");lv_textarea_set_text(readRegisters,"50,6E");lv_textarea_set_text(readLength,"2");lv_dropdown_set_selected(readType,1);
    lv_obj_send_event(readButton,LV_EVENT_CLICKED,nullptr);assert(extraWork==Work::Read && readPlanCount==2 && activeBusy());pump();assert(extraPending && extraOp==21 && extraRegister==0x50);
    auto reply=[&](std::initializer_list<uint8_t> bytes,uint8_t code=0,bool restored=true){
        std::vector<uint8_t> p(32+bytes.size());bringup::put32(p.data(),extraPending);p[4]=code;p[5]=code?0:1;p[6]=restored;p[7]=extraRegister;
        p[27]=5;p[28]=3;p[29]=extraAddress;p[30]=uint8_t(bytes.size());p[31]=uint8_t(bytes.size()>>8);unsigned at=32;for(uint8_t b:bytes)p[at++]=b;
        statusAt=millis();inject(8,p.data(),p.size());pump();
    };
    reply({0x34,0x12});assert(extraPending && extraRegister==0x6e && readValues[0].ok && readAt==1);
    reply({0xfe,0xff});assert(!activeBusy() && readAt==2 && readValues[1].ok);
    lv_dropdown_set_selected(readResults,0);readRender();assert(strstr(lv_label_get_text(readOutput),"Unsigned: 4660"));
    lv_dropdown_set_selected(readEndian,1);readRender();assert(strstr(lv_label_get_text(readOutput),"Unsigned: 13330"));
    lv_dropdown_set_selected(readResults,1);lv_dropdown_set_selected(readEndian,0);lv_dropdown_set_selected(readType,2);readRender();assert(strstr(lv_label_get_text(readOutput),"Signed: -2"));
    extraAddress=0x40;readRender();assert(strstr(lv_label_get_text(readOutput),"0x75 R")); // Result address does not drift.
    screenIndex=6;slidePosition(nullptr,-5*W);refreshExtras();save("tablet-v18-manual-read.ppm");
    lv_textarea_set_text(readRegisters,"50,51");lv_textarea_set_text(readLength,"1");lv_dropdown_set_selected(readType,0);readAction(nullptr);pump();
    reply({},2);assert(extraPending && extraRegister==0x51 && !readValues[0].ok);reply({0x81});assert(!activeBusy() && readValues[1].ok);
    readAction(nullptr);pump();uint32_t pending=extraPending;readAction(nullptr);assert(extraPending==pending && activeBusy() && readCancel);
    reply({},6);assert(!activeBusy() && readAt==1 && readCancel);
    lv_textarea_set_text(readRegisters,"00");lv_textarea_set_text(readLength,"256");readAction(nullptr);pump();assert(extraPending && readBytes==256);
    std::vector<uint8_t> full(288);bringup::put32(full.data(),extraPending);full[5]=full[6]=1;full[27]=5;full[28]=3;full[29]=0x75;full[31]=1;
    for(unsigned i=0;i<256;++i)full[32+i]=uint8_t(i);inject(8,full.data(),full.size());statusAt=millis();pump();assert(!activeBusy() && readValues[0].count==256 && readValues[0].bytes[255]==255);
    lv_dropdown_set_selected(readView,3);readRender();assert(strstr(lv_label_get_text(readOutput),"11111111"));
    lv_textarea_set_text(readRegisters,"FF");readAction(nullptr);assert(!activeBusy() && strstr(extraMessage,"wrap"));
    lv_textarea_set_text(readRegisters,"00");lv_textarea_set_text(readLength,"4");lv_dropdown_set_selected(readType,3);lv_dropdown_set_selected(readView,1);readAction(nullptr);pump();reply({0,0,0x80,0x3f});assert(strstr(lv_label_get_text(readOutput),"Float32: 1"));
    readAction(nullptr);pump();reply({},4,false);assert(!activeBusy() && !sessionReady && needsSync);
    sessionReady=true;needsSync=false;statusAt=millis();readAction(nullptr);pump();previewTime_ms+=15001;extraTick();assert(!activeBusy() && !sessionReady && needsSync);
    sessionReady=true;needsSync=false;statusAt=millis();readAction(nullptr);assert(extraWork==Work::Read && !extraPending);
    sessionReady=false;extraTick();assert(!activeBusy() && extraWork==Work::Idle);
    puts("PASS: manual READ real touch/UART, hex register lists/ranges, 256-byte payload, LE/BE, signed and float display, NACK continuation, cancellation ownership, boundary checks, uncertain restoration and timeout without retry");
}
