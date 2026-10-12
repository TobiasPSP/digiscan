// Included after the existing UI modules. Reset uses their cancellation paths.
// START is the boot/session gate. Only confirmed I2C evidence unlocks tools.
// Reset is a staged stop/acknowledge/clear/resume transaction, not an optimistic
// local flag change. Peer absence preserves an explicit uncertain/paused state.
// New Session retains settings; Reset Configuration removes assignments/settings
// but never the installed chip packages. Neither modifies the DUT registers.
enum class StartStage { Idle, Stopping, ClearPeer, Resume };
StartStage startStage=StartStage::Idle;
bool startResetConfiguration=false;
uint32_t startAt_ms=0;
const char *startMessage="";

void startWorkspace(bool unlocked){
    busConfirmed=unlocked;
    if(startHint){
        if(unlocked)lv_obj_remove_flag(startHint,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(startHint,LV_OBJ_FLAG_HIDDEN);
    }
    for(auto *o:{monitorScreen,reportScreen,extraScreens[0],extraScreens[2],recorderScreen,readScreen}){
        if(unlocked)lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
    }
}
void startFail(const char *why){
    startMessage=why;startStage=StartStage::Idle;startupCapturePending=false;sessionMutation=false;
}
void startClearLocal(){
    store->clear();decoder.clear();discoveryDecoder->clear();discover->clear();discover->enabled=false;
    recorder->reset();recorderPost->begin(millis());recorderPost->active=false;
    recorderPhase=RecorderPhase::Idle;recorderCancel=false;recorderPending=0;recorderPostDialogPending=false;
    recorderRendered=recorderRenderedSession=0xffffffffu;recorderScroll=-1;recorderMessage[0]=0;
    lv_dropdown_set_selected(recorderChoice,0);
    reportCache.clear();reportBatch.reset();reportBatchMessage[0]=0;
    readPlanCount=readAt=0;readCancel=false;for(auto &r:readValues)r=manual_read::Result{};
    readRender();graphLinkLost();graphClear();lv_dropdown_set_selected(graphChoice,0);graphConfiguredActive=false;graphMode=GraphMode::Passive;
    if(startResetConfiguration){
        graphRefresh_ms=1000;
        lv_dropdown_set_selected(graphInterval,0);
        lv_textarea_set_text(readAddress,"75");lv_textarea_set_text(readRegisters,"00");lv_textarea_set_text(readLength,"1");
        for(auto *choice:{readEndian,readView,readType,writePrefixKind,writeFormat})lv_dropdown_set_selected(choice,0);
        lv_textarea_set_text(writeAddress,"75");lv_textarea_set_text(writePrefix,"");lv_textarea_set_text(writePayload,"");
    }
    if(keyboard)lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    if(extraKeyboard)lv_obj_add_flag(extraKeyboard,LV_OBJ_FLAG_HIDDEN);
    selection=analysis::Selection{};selection.uniqueOrder=analysis::UniqueOrder::LatestChange;
    view=View::Unique;lv_dropdown_set_selected(viewChoice,unsigned(view));
    addressText[0]=registerText[0]=0;if(startResetConfiguration)includeUnverifiedReports=false;
    decoder.setChangeFactor(0);discoveryDecoder->setChangeFactor(0);
    discoveryResume=discoveryPauseOwned=discoveryPausedVerified=discoveryDeferred=clearDiscoveryRequested=false;
    clearTabletDiagnostics();sequence=analysis::Sequence{};haveCurrentTiming=false;
    for(auto &binding:registry.bindings){binding.seen=binding.confirmed=binding.deferred=false;}
    ++registry.evidenceGeneration;
    scanNext=8;scanSkipped=0;memset(scanFound,0,sizeof(scanFound));scanDirty=true;scanEvidenceGeneration=0xffffffffu;
    scanBindingGeneration=graphEvidenceGeneration=reportEvidenceGeneration=recorderFilterEvidence=0xffffffffu;
    snprintf(extraMessage,sizeof(extraMessage),"Passive discovery");activeMessage[0]=0;
    invalidateProfileView();reportDirty=true;reportPaintTop=-1;
    lv_anim_delete(monitorScreen,slidePosition);sliding=false;screenIndex=3;reportActive=false;slidePosition(nullptr,0);
    startWorkspace(false);lv_obj_scroll_to_y(scanBody,0,LV_ANIM_OFF);
    sourceFilter(selection);
}
bool startClearConfiguration(){
    // Keep binary profile files and built-in definitions; clear only owned NVS namespaces.
    if(!profileStorage.ready || !profileStorage.preferences.clear())return false;
    Preferences previousScreen;
    if(!previousScreen.begin("i2cscreen",false))return false;
    bool ok=previousScreen.clear();previousScreen.end();
    if(!ok)return false;
    registry.reset();profileStorage.resetPending=false;defaultBaudAttempted=false;return true;
}
void startResetConfirmed(lv_event_t *e){
    startResetConfiguration=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))!=0;
    closeDetail(nullptr);startupCapturePending=false;sessionMutation=true;startStage=StartStage::Stopping;startAt_ms=millis();
    startMessage="Stopping current work";
    graphLinkLost();reportBatchCancel();cancelExtra(nullptr);
    if(activePending && !reportBatch.running)activeAction(nullptr);
    if(recorderBusy())recorderAbort(nullptr);
    discoveryResume=false;recorderPostDialogPending=false;
}
void startConfirm(bool configuration){
    if(profileUploadBusy || sessionMutation || modal)return;
    modal=lv_obj_create(lv_screen_active());flat(modal,Bg);lv_obj_set_size(modal,W,H);lv_obj_remove_flag(modal,LV_OBJ_FLAG_SCROLLABLE);
    text(modal,12,18,296,configuration?"RESET CONFIGURATION":"NEW SESSION",&lv_font_montserrat_18);
    auto *tile=inset(modal,8,66,304,260);
    int at=detailLine(tile,14,"Clears observations, recordings, diagnostics and temporary filters.",&lv_font_montserrat_14,Ink);
    at=detailLine(tile,at,configuration?"Also removes saved address/profile assignments and restores settings to defaults.":"Keeps saved address/profile assignments and settings.",&lv_font_montserrat_14,Amber);
    at=detailLine(tile,at,"Uploaded and built-in chip profiles remain installed. No DUT configuration writes.",&lv_font_montserrat_12,Muted);
    lv_obj_set_height(tile,at);insetEdge(tile);
    text(modal,12,350,296,"Returns to START and resumes passive discovery.",&lv_font_montserrat_12);
    button(modal,8,428,148,"Cancel",closeDetail,0);
    auto *confirm=button(modal,164,428,148,configuration?"RESET":"NEW SESSION",startResetConfirmed,configuration?1:0);
    ui_update::bgColor(confirm,lv_color_hex(configuration?0x9c3544:0x238557),0);
}
void newSession(lv_event_t *){startConfirm(false);}
void resetConfiguration(lv_event_t *){closeDetail(nullptr);startConfirm(true);}
void startSettings(lv_event_t *){
    if(modal || sessionMutation || profileUploadBusy)return;
    modal=lv_obj_create(lv_screen_active());flat(modal,Bg);lv_obj_set_size(modal,W,H);
    text(modal,12,16,230,"INSTRUMENT SETTINGS",&lv_font_montserrat_16);button(modal,266,4,48,LV_SYMBOL_CLOSE,closeDetail,0);
    auto *tile=inset(modal,8,64,304,170);int at=detailLine(tile,12,"ACQUISITION",&lv_font_montserrat_16,Cyan);
    char info[192];snprintf(info,sizeof(info),"Startup: passive\nUART: %lu baud\nChip profiles: %u",(unsigned long)uartBaud,decoding::ProfileCount);
    at=detailLine(tile,at,info,&lv_font_montserrat_14,Ink);
    lv_obj_set_height(tile,at);insetEdge(tile);
    auto *reset=button(modal,8,268,304,"RESET CONFIGURATION",resetConfiguration,0);ui_update::bgColor(reset,lv_color_hex(0x9c3544),0);
    text(modal,12,320,296,startMessage,&lv_font_montserrat_12);
    auto *done=button(modal,164,428,148,"DONE",closeDetail,0);ui_update::bgColor(done,lv_color_hex(0x238557),0);
}
void startLink(lv_event_t *){
    if(sessionMutation || profileUploadBusy || control.busy() || rateRequest || activeBusy())return;
    if(connected() && !sessionReady){needsSync=false;delivering=false;sourceFilter(selection);control.begin(observer::Action::Synchronize);startMessage="Verifying link / stays paused";}
    else if(connected() && sessionReady){sourceFilter(selection);control.begin(status[0]?observer::Action::Pause:observer::Action::Enable);startMessage="";}
}
void startTick(){
    if(profileUploadBusy)return;
    if(!sessionMutation && !busConfirmed){
        for(unsigned a=8;a<120;++a)if(registry.bindings[a].confirmed){startWorkspace(true);break;}
    }
    if(startStage==StartStage::Stopping){
        if(uint32_t(millis()-startAt_ms)>30000){startFail("Stop timed out / state uncertain; nothing cleared");return;}
        if(activeBusy() || control.busy() || rateRequest)return;
        if(connected()){
            if(!sessionReady || control.uncertain){startFail("Link not verified / reset cancelled");return;}
            sourceFilter(selection);control.begin(observer::Action::Clear,false);startStage=StartStage::ClearPeer;startMessage="Clearing session / capture paused";return;
        }
        // No peer connected: local session reset is safe; discovery waits for a verified link.
        if(startResetConfiguration && !startClearConfiguration()){startFail("Configuration save failed / reset incomplete");return;}
        if(!startResetConfiguration){profileStorage.save(registry,true);if(registry.saveFailed){startFail("Assignment save failed / nothing cleared");return;}}
        startClearLocal();startStage=StartStage::Idle;sessionMutation=false;startupCapturePending=true;startMessage="Waiting for link";return;
    }
    if(startStage==StartStage::ClearPeer){
        if(control.busy())return;
        if(control.uncertain || !connected() || !sessionReady || status[0]){startFail("Pause/clear not verified / no automatic resume");return;}
        if(startResetConfiguration){if(!startClearConfiguration()){startFail("Configuration save failed / capture paused");return;}}
        else{profileStorage.save(registry,true);if(registry.saveFailed){startFail("Assignment save failed / capture paused");return;}}
        startClearLocal();startStage=StartStage::Idle;sessionMutation=false;startupCapturePending=true;startMessage="";
    }
    if(startStage==StartStage::Resume){
        if(control.busy())return;
        if(control.uncertain || !connected() || !sessionReady){startFail("Capture start not verified / tap LINK to recover");return;}
        if(status[0]){startStage=StartStage::Idle;startMessage="";return;}
        if(uint32_t(millis()-startAt_ms)>10000)startFail("No running status / tap LINK to recover");
        return;
    }
    if(!startupCapturePending || sessionMutation)return;
    if(control.uncertain){startFail("Initialization failed / tap LINK to recover");return;}
    if(!connected() || !sessionReady || control.busy() || rateRequest || activeBusy() || !defaultBaudAttempted)return;
    sourceFilter(selection);
    if(control.begin(observer::Action::Enable)){startupCapturePending=false;startStage=StartStage::Resume;startAt_ms=millis();startMessage="Starting passive discovery";}
}
void refreshStart(){
    bool online=connected(),verified=online && sessionReady && !control.uncertain;
    ui_update::text(startLinkIcon,verified?LV_SYMBOL_USB " LINK":LV_SYMBOL_WARNING " LINK");
    ui_update::textColor(startLinkIcon,lv_color_hex(verified?Green:online?Amber:Red),0);
    unsigned count=0;for(unsigned a=8;a<120;++a)count+=registry.bindings[a].confirmed;
    if(sessionMutation)ui_update::text(scanStatus,startMessage);
    else if(*startMessage)ui_update::text(scanStatus,startMessage);
    else if(extraWork!=Work::Scan)ui_update::textFmt(scanStatus,"%s / %u confirmed",!online?"Waiting for link":!verified?"Verifying link":status[0]?"Listening":"Paused",count);
    ui_update::text(startInfo,busConfirmed?"I2C CONFIRMED":online?"PASSIVE DISCOVERY":"DISCONNECTED");
    bool scanReady=verified && !startupCapturePending && startStage==StartStage::Idle && !control.busy() && !rateRequest && !activeBusy();
    if(scanReady || extraWork==Work::Scan)lv_obj_remove_state(scanButton,LV_STATE_DISABLED);else lv_obj_add_state(scanButton,LV_STATE_DISABLED);
    if(sessionMutation || profileUploadBusy)lv_obj_add_state(startSessionButton,LV_STATE_DISABLED);else lv_obj_remove_state(startSessionButton,LV_STATE_DISABLED);
}
