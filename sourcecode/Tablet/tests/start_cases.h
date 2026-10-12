// Runs the real START lifecycle and UART control paths in the LVGL fixture.
{
    if(modal)closeDetail(nullptr);
    uploadRebootAt=0;profileUploadBusy=false;sessionMutation=false;startStage=StartStage::Idle;
    control=observer::Control{};rateRequest=0;defaultBaudAttempted=true;
    extraPending=activePending=0;extraWork=Work::Idle;extraSessionBusy=false;
    reportBatch=acquisition::Batch{};recorderPhase=RecorderPhase::Idle;
    registry.reset();startClearLocal();startWorkspace(false);startupCapturePending=true;
    assert(hasLabel(extraScreens[1],"V22"));
    assert(lv_obj_has_flag(startHint,LV_OBJ_FLAG_HIDDEN));
    seen=sessionReady=delivering=false;needsSync=true;statusAt=0;memset(status,0,sizeof(status));
    selection.anyAddress=selection.anyRegister=true;selection.addresses[0x40]=true;selection.registers[0x22]=true;
    sourceFilter(selection);assert(control.filterCommand[1]==12 && control.filterCommand[2]==3 && control.filterCommand[3]==0);
    navigateReport(true);assert(screenIndex==3 && !busConfirmed);
    startTick();assert(startupCapturePending && !control.busy());
    uint8_t peer[200]={};peer[0]=1;bringup::put32(peer+132,255);bringup::put32(peer+176,65536);
    inject(3,peer,sizeof(peer));pump();assert(control.action==observer::Action::Synchronize);
    for(unsigned i=0;i<4;++i)acknowledge();
    assert(sessionReady && control.action==observer::Action::Enable && !startupCapturePending);
    pump();for(unsigned i=0;i<3;++i)acknowledge();
    assert(startStage==StartStage::Idle && status[0] && !busConfirmed);
    assert(lv_obj_has_flag(monitorScreen,LV_OBJ_FLAG_HIDDEN));
    uint8_t transaction[]={1,0,0,0,0,1,0x75,0x3a,2,0,2,0,1,0,0,0xea,1,0x3a,1,2,0,0,0,0,0xeb,1,49,0};
    receiveTransaction(transaction,sizeof(transaction));startTick();refreshUi();
    assert(!busConfirmed && !scanTiles[0x75]);
    transaction[4]=1;bringup::put32(transaction,2);receiveTransaction(transaction,sizeof(transaction));startTick();refreshUi();
    assert(busConfirmed && registry.bindings[0x75].confirmed && scanTiles[0x75]);
    assert(!lv_obj_has_flag(startHint,LV_OBJ_FLAG_HIDDEN));
    assert(hasLabel(extraScreens[1],"Swipe left for I2C Tools"));
    assert(!lv_obj_has_flag(monitorScreen,LV_OBJ_FLAG_HIDDEN) && screenIndex==3);
    save("tablet-v21-start-confirmed.ppm");
    navigateReport(true);previewTime_ms+=250;lv_anim_refr_now();assert(screenIndex==0);
    sourceFilter(selection);assert(control.filterCommand[1]&1); // Monitor selection still exists.
    closeDetail(nullptr);screenIndex=3;slidePosition(nullptr,0);sourceFilter(selection);
    assert(control.filterCommand[1]==12);

    // Confirmed ranges are never actively probed, including new scan evidence.
    control=observer::Control{};statusAt=millis();status[0]=1;needsSync=false;
    for(auto &binding:registry.bindings)binding.confirmed=false;
    registry.bindings[0x08].confirmed=registry.bindings[0x0a].confirmed=registry.bindings[0x75].confirmed=true;
    ++registry.evidenceGeneration;scanStart(nullptr);extraTick();
    assert(extraPending && extraAddress==9 && scanNext==9 && scanEnd==9 && extraOp==19);
    auto rangeReply=[&](unsigned address,uint8_t code,bool final){
        uint8_t reply[32]={};bringup::put32(reply,extraPending);reply[4]=code;reply[5]=code?0:1;reply[6]=final;
        reply[27]=4;reply[28]=2;reply[29]=uint8_t(address);reply[30]=final;reply[31]=uint8_t(scanEnd);
        inject(6,reply,32);pump();
    };
    rangeReply(9,2,true);assert(extraAddress==11 && scanEnd==0x74 && extraPending);
    for(unsigned a=11;a<=0x74;++a)rangeReply(a,a==0x20?0:2,a==0x74);
    assert(extraAddress==0x76 && scanEnd==0x77 && extraPending);
    rangeReply(0x76,2,false);rangeReply(0x77,2,true);
    assert(extraWork==Work::Idle && !extraPending && scanNext==120 && registry.bindings[0x20].confirmed);
    for(unsigned a=8;a<120;++a)registry.bindings[a].confirmed=true;
    scanStart(nullptr);uart.output.clear();extraTick();assert(!extraPending && extraWork==Work::Idle && uart.output.empty());

    // Canceling confirmation is non-destructive; New Session preserves assignments.
    assert(assignAddressProfile(0x75,registry.find("ip2366")));profileStorage.save(registry,true);
    int assigned=registry.bindings[0x75].profile;unsigned installedCount=decoding::ProfileCount;
    discover->enabled=true;recorder->count=1;reportCache.readings[0].valid=true;
    includeUnverifiedReports=true;
    newSession(nullptr);assert(modal && hasLabel(modal,"NEW SESSION"));save("tablet-v21-new-session-confirm.ppm");
    closeDetail(nullptr);assert(recorder->count==1 && registry.bindings[0x75].confirmed);
    newSession(nullptr);lv_obj_send_event(labelButton(modal,"NEW SESSION"),LV_EVENT_CLICKED,nullptr);
    assert(sessionMutation && startStage==StartStage::Stopping);
    pump();assert(control.action==observer::Action::Clear);pump();
    for(unsigned i=0;i<3;++i)acknowledge();
    assert(!sessionMutation && screenIndex==3 && !busConfirmed && registry.bindings[0x75].profile==assigned);
    assert(!registry.bindings[0x75].confirmed && !registry.bindings[0x75].seen && !recorder->count && !discover->hasDecisions());
    assert(!selection.anyAddress && !selection.anyRegister && !reportCache.readings[0].valid);
    assert(includeUnverifiedReports); // New Session retains device/report settings.
    pump();for(unsigned i=0;i<3;++i)acknowledge();assert(status[0] && startStage==StartStage::Idle);

    // Settings reset drops bindings/NVS choices but never installed binary packages.
    startSettings(nullptr);assert(modal && hasLabel(modal,"RESET CONFIGURATION"));save("tablet-v21-settings.ppm");
    lv_obj_send_event(labelButton(modal,"RESET CONFIGURATION"),LV_EVENT_CLICKED,nullptr);
    assert(modal && hasLabel(modal,"RESET"));lv_obj_send_event(labelButton(modal,"RESET"),LV_EVENT_CLICKED,nullptr);
    pump();pump();for(unsigned i=0;i<3;++i)acknowledge();
    assert(!sessionMutation && registry.bindings[0x75].profile<0 && Preferences::saved.empty());
    assert(!includeUnverifiedReports);
    assert(decoding::ProfileCount==installedCount && portable::installed[portable::find("ip2366")]);
    assert(!defaultBaudAttempted);defaultBaudAttempted=true; // Baud negotiation is covered separately.
    pump();pump();for(unsigned i=0;i<3;++i)acknowledge();assert(status[0]);
    // Reconnect is synchronized into standby without rearming or discarding data.
    bringup::put32(transaction,3);receiveTransaction(transaction,sizeof(transaction));startTick();
    unsigned retained=store->received;assert(retained && busConfirmed);
    assert(!startupCapturePending);sessionReady=delivering=false;needsSync=true;
    statusAt=millis();memcpy(peer,status,sizeof(peer));inject(3,peer,sizeof(peer));pump();
    assert(control.action==observer::Action::Synchronize);
    for(unsigned i=0;i<4;++i)acknowledge();
    assert(sessionReady && !status[0] && !control.busy() && !startupCapturePending);
    assert(store->received==retained && registry.bindings[0x75].confirmed && busConfirmed);
    refreshUi();save("tablet-v21-start-paused.ppm");

    // New Session waits for an in-flight scan's cancellation/restoration result.
    scanStart(nullptr);extraTick();assert(extraPending);unsigned cancelAddress=scanNext,cancelEnd=scanEnd;uint32_t cancelId=extraPending;
    newSession(nullptr);lv_obj_send_event(labelButton(modal,"NEW SESSION"),LV_EVENT_CLICKED,nullptr);
    startTick();assert(sessionMutation && !control.busy() && extraPending==cancelId);
    uint8_t cancelled[32]={};bringup::put32(cancelled,cancelId);cancelled[4]=6;cancelled[6]=1;
    cancelled[27]=4;cancelled[28]=2;cancelled[29]=uint8_t(cancelAddress);cancelled[30]=1;cancelled[31]=uint8_t(cancelEnd);
    inject(6,cancelled,32);pump();assert(control.action==observer::Action::Clear);pump();
    for(unsigned i=0;i<3;++i)acknowledge();pump();for(unsigned i=0;i<3;++i)acknowledge();
    assert(!sessionMutation && !activeBusy() && status[0]);

    // Offline reset clears only local state; failed configuration storage does not resume.
    assert(assignAddressProfile(0x75,registry.find("ip2366")));profileStorage.save(registry,true);
    assigned=registry.bindings[0x75].profile;seen=false;sessionReady=false;needsSync=true;uart.output.clear();
    newSession(nullptr);lv_obj_send_event(labelButton(modal,"NEW SESSION"),LV_EVENT_CLICKED,nullptr);startTick();
    assert(!sessionMutation && startupCapturePending && !busConfirmed && registry.bindings[0x75].profile==assigned && uart.output.empty());
    Preferences::fail=true;startSettings(nullptr);resetConfiguration(nullptr);
    lv_obj_send_event(labelButton(modal,"RESET"),LV_EVENT_CLICKED,nullptr);startTick();Preferences::fail=false;
    assert(!sessionMutation && !startupCapturePending && registry.bindings[0x75].profile==assigned);
    assert(strstr(startMessage,"failed") && decoding::ProfileCount==installedCount);

    // An unanswered pause is not followed by destructive local reset or automatic resume.
    seen=sessionReady=true;statusAt=millis();status[0]=1;control=observer::Control{};needsSync=false;
    receiveTransaction(transaction,sizeof(transaction));retained=store->received;
    newSession(nullptr);lv_obj_send_event(labelButton(modal,"NEW SESSION"),LV_EVENT_CLICKED,nullptr);pump();pump();
    assert(control.pendingId && control.opcode==2);previewTime_ms+=8001;pump();pump();
    assert(!sessionMutation && !startupCapturePending && store->received==retained && registry.bindings[0x75].profile==assigned);
    puts("PASS: START-only boot, running-peer synchronization/automatic passive capture, valid-only address unlock, independent source filtering, confirmed-address scan exclusion, confirmations, session reset/assignment preservation, configuration reset/profile preservation and paused reconnect");
    puts("PASS: New Session waits for scan cancellation; offline reset, storage failure and lost pause acknowledgement preserve definitions and prevent automatic resume");
}
