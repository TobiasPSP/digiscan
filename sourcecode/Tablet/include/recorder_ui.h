// Included in the tablet UI namespace. Bus jobs never run in touch callbacks.
// Writes-only acquisition retains order and relative bus timestamps. Playback
// excludes unchecked/non-replayable entries and omits the first idle delay.
// After replay, watch only played register pairs for five seconds. This reveals
// a host reverting settings but does not establish exclusive bus ownership.
enum class RecorderPhase {Idle,Timing,PauseArm,StartArm,Recording,Finish,PausePlay,Preflight,Playing,FaultAfter,Restore,PostStart,PostWatch,PostPause,PostRestore};
RecorderPhase recorderPhase=RecorderPhase::Idle;
uint32_t recorderPending=0,recorderSent_ms=0,recorderPlay_us=0,recorderGeneration=0;
uint64_t recorderElapsed_us=0;
uint64_t recorderOrigin_cycles=0;
uint32_t recorderWatch_ms=0;
bool recorderPostResume=false;
bool recorderPostDialogPending=false;
unsigned recorderPostPage=0;
lv_obj_t *recorderPostNext=nullptr;
void recorderPostDialog();
void recorderPostPageAction(lv_event_t *);
uint8_t recorderOpcode=0,recorderAddress=0,recorderRegister=0;
unsigned recorderAt=0,recorderBaselineAt=0,recorderPaintCount=0;
bool recorderWasRunning=false,recorderCancel=false;
uint32_t recorderRendered=0xffffffffu;
uint32_t recorderRenderedSession=0xffffffffu;
lv_obj_t *recorderArm=nullptr,*recorderPlay=nullptr,*recorderInfo=nullptr,*recorderBody=nullptr;
lv_obj_t *recorderRows[recording18::MaxEntries]={};
uint8_t recorderRowState[recording18::MaxEntries]={};
lv_obj_t *recorderPool[8]={},*recorderSpacer=nullptr;
unsigned recorderPoolIndex[8]={};
int recorderHeights[recording18::MaxEntries]={},recorderScroll=-1;
char recorderMessage[192]={};
lv_obj_t *recorderChoice=nullptr;
struct RecorderFilterOption {unsigned list;uint8_t address;};
RecorderFilterOption recorderFilterOptions[128]={};
unsigned recorderFilterOptionCount=0;
uint32_t recorderFilterGeneration=0xffffffffu,recorderFilterEvidence=0xffffffffu;
bool recorderBusy(){return recorderPhase!=RecorderPhase::Idle;}
bool recorderControl(){return recorderPhase==RecorderPhase::PauseArm || recorderPhase==RecorderPhase::StartArm || recorderPhase==RecorderPhase::Finish || recorderPhase==RecorderPhase::PausePlay || recorderPhase==RecorderPhase::Restore || recorderPhase==RecorderPhase::PostStart || recorderPhase==RecorderPhase::PostPause || recorderPhase==RecorderPhase::PostRestore;}
bool recorderAcquiring(){return recorderPhase==RecorderPhase::Timing || recorderPhase==RecorderPhase::PauseArm || recorderPhase==RecorderPhase::StartArm || recorderPhase==RecorderPhase::Recording || recorderPhase==RecorderPhase::Finish;}
bool recorderWatching(){return recorderPhase==RecorderPhase::PostStart || recorderPhase==RecorderPhase::PostWatch || recorderPhase==RecorderPhase::PostPause;}
void recorderGap(bool expectedPause){if(recorder && recorder->haveTiming && !(expectedPause && recorderPhase==RecorderPhase::Finish))recorder->gap();if(recorderPost && recorderPost->haveEpoch && !expectedPause)recorderPost->gap();}
void recorderTransportGap(){if(recorder)recorder->gap();if(recorderPost)recorderPost->gap();}
void recorderObserve(const observer::Observation &o){if(recorder)recorder->observe(o,haveCurrentTiming?&currentTiming:nullptr,millis());if(recorderWatching() && recorderPost)recorderPost->observe(o,haveCurrentTiming?&currentTiming:nullptr,millis());}
void recorderFilterRefresh(){
    unsigned old=lv_dropdown_get_selected(recorderChoice);RecorderFilterOption previous={0,0};
    bool preserve=old && old<=recorderFilterOptionCount;if(preserve)previous=recorderFilterOptions[old-1];
    lv_dropdown_set_options(recorderChoice,"Current Filter");recorderFilterOptionCount=0;
    for(unsigned a=8;a<120;++a)if(registry.reportEligible(uint8_t(a))){
        const auto &profile=decoding::profiles[registry.bindings[a].profile];
        for(unsigned i=0;i<experiment::FilterListCount && recorderFilterOptionCount<128;++i){const auto &f=experiment::filterLists[i];
            if(strcmp(profile.id,f.profile))continue;
            char label[192];snprintf(label,sizeof(label),"%s / 0x%02X / %s",f.name,a,profile.name);
            lv_dropdown_add_option(recorderChoice,label,LV_DROPDOWN_POS_LAST);
            recorderFilterOptions[recorderFilterOptionCount++]={i,uint8_t(a)};
            if(preserve && previous.list==i && previous.address==a)lv_dropdown_set_selected(recorderChoice,recorderFilterOptionCount);
        }
    }
    recorderFilterGeneration=registry.generation;recorderFilterEvidence=registry.evidenceGeneration;
}
void recorderFilterChanged(lv_event_t *){
    if(recorderBusy())return;
    recorder->message="Selection ready / START";++recorder->revision;
}
bool recorderBuildKeys(){
    recorder->reset();bool valid=true;
    unsigned preset=recorderChoice?lv_dropdown_get_selected(recorderChoice):0;
    if(preset){
        if(preset>recorderFilterOptionCount){recorder->message="Filter no longer available";return false;}
        const auto &option=recorderFilterOptions[preset-1];const auto &list=experiment::filterLists[option.list];
        if(!registry.reportEligible(option.address) || strcmp(decoding::profiles[registry.bindings[option.address].profile].id,list.profile)){
            recorder->message="Profile changed / select the filter again";return false;
        }
        for(unsigned i=0;i<list.count;++i)valid &= recorder->addKey(option.address,list.registers[i]);
        recorderGeneration=registry.generation;return valid && recorder->keyCount;
    }
    observer::Observation &probe=store->detail;probe.associated=true;probe.success=true;
    for(unsigned a=8;a<120;++a)if(selection.address(uint8_t(a)))for(unsigned r=0;r<256;++r)if(selection.reg(uint8_t(r))){
        bool selected=true;
        if(discoveryGatesMonitor()){
            probe.address=uint8_t(a);probe.reg=uint8_t(r);probe.direction='R';auto rd=discover->decision(probe);
            probe.direction='W';auto wr=discover->decision(probe);
            selected=discover->kept?(rd==discovery::Decision::Keep || wr==discovery::Decision::Keep):selected && !(rd==discovery::Decision::Ignore && wr==discovery::Decision::Ignore);
        }
        if(selected)valid &= recorder->addKey(uint8_t(a),uint8_t(r));
    }
    recorderGeneration=registry.generation;
    if(!valid)recorder->message="Invalid address/register selection";
    else if(!recorder->keyCount)recorder->message="No selected registers / use Discover or filter first";
    return valid && recorder->keyCount;
}
bool recorderSend(uint8_t op,uint8_t address=0,uint8_t reg=0,const uint8_t *data=nullptr,unsigned count=0){
    if(recorderPending || !connected() || !sessionReady || control.busy() || rateRequest || control.uncertain || uart.availableForWrite()<320)return false;
    uint8_t command[267]={op},wire[320];size_t n=1;
    if(op==20){command[1]=1;n=2;}
    else if(op==16 || op==18 || op==21){
        if(!activeSession){
#ifdef TABLET_PREVIEW
            activeSession=millis()+1;
#else
            activeSession=esp_random();if(!activeSession)activeSession=1;
#endif
        }
        command[1]=0xa5;bringup::put32(command+2,activeSession);command[6]=address;
        if(op==18){command[7]=reg;n=8;}
        else if(op==21){command[7]=reg;command[8]=uint8_t(count);command[9]=uint8_t(count>>8);n=10;}
        else{command[7]=uint8_t(count);command[8]=uint8_t(count>>8);memcpy(command+9,data,count);n=9+count;}
    }
    uint32_t id=++control.nextId;if(!id)id=++control.nextId;
    size_t bytes=bringup::command(id,command,n,wire,sizeof(wire));if(!bytes)return false;
    recorderPending=id;recorderOpcode=op;recorderAddress=address;recorderRegister=reg;recorderSent_ms=millis();
    if(uart.write(wire,bytes)!=bytes){sessionReady=false;needsSync=true;return false;}return true;
}
void recorderFinishPlayback(const char *reason){
    if(reason!=recorderMessage)snprintf(recorderMessage,sizeof(recorderMessage),"%s",reason);recorder->message=recorderMessage;
    recorderPhase=RecorderPhase::Restore;recorderPending=0;++recorder->revision;
}
bool recorderFaultPlan(uint8_t address,uint8_t &reg,uint8_t &mask){
    // Verified IP2366 reference: 0x38 bit 5 overcurrent / bit 4 short circuit.
    const auto &binding=registry.bindings[address];
    if(binding.profile<0 || strcmp(decoding::profiles[binding.profile].id,"ip2366"))return false;
    reg=0x38;mask=0x30;return true;
}
void recorderAbort(lv_event_t *){
    recorderCancel=true;
    if(recorderPending && recorderOpcode!=20){uint8_t op=14,wire[32];size_t n=bringup::command(++control.nextId,&op,1,wire,sizeof(wire));if(uart.availableForWrite()>=int(n))uart.write(wire,n);}
}
void recorderArmAction(lv_event_t *){
    if(recorderPhase==RecorderPhase::Recording){recorderPhase=RecorderPhase::Finish;control.begin(observer::Action::Pause);return;}
    if(recorderAcquiring()){recorderCancel=true;return;}
    if(recorderBusy() || activePending || reportBatch.running || extraPending || extraSessionBusy || !connected() || !sessionReady || control.busy() || rateRequest)return;
    if(!recorderBuildKeys())return;
    recorderCancel=false;recorderWasRunning=false;recorderPhase=RecorderPhase::Timing;recorder->message="Preparing timestamped capture";
    sourceFilter(selection);++recorder->revision;
}
void recorderPlayConfirmed(lv_event_t *){
    closeDetail(nullptr);recorderAt=0;recorderCancel=false;recorderWasRunning=status[0];
    recorderPhase=RecorderPhase::PausePlay;recorder->message="Pausing capture / checking faults";
    control.begin(observer::Action::Pause);
    for(unsigned i=0;i<recorder->count;++i){auto &e=recorder->entries[i];e.replayed=false;e.actualDelay_us=0;}
    recorderPost->active=false;recorderPost->count=recorderPost->dropped=recorderPost->gaps=0;
    recorderPostPage=0;
    recorderOrigin_cycles=0;for(unsigned i=0;i<recorder->count;++i)if(recorder->entries[i].checked){recorderOrigin_cycles=recorder->entries[i].timing.start[0];break;}
    ++recorder->revision;
}
void recorderPlayAction(lv_event_t *){
    if(recorderBusy()){recorderAbort(nullptr);return;}
    if(activeBusy() || control.busy() || !connected() || !sessionReady || !recorder->count)return;
    if(!recorder->complete){recorder->message="Incomplete recording / playback disabled";++recorder->revision;return;}
    unsigned checked=0;
    for(unsigned i=0;i<recorder->count;++i)if(recorder->entries[i].checked){
        uint8_t reg,mask;
        if(!recording18::replayable(recorder->entries[i]) || !recorderFaultPlan(recorder->entries[i].transaction.address,reg,mask)){
            recorder->message="Checked entry lacks replay shape or verified fault plan";++recorder->revision;return;}
        ++checked;
    }
    if(!checked){recorder->message="No checked transactions";++recorder->revision;return;}
    modal=lv_obj_create(lv_screen_active());flat(modal,Bg);lv_obj_set_size(modal,W,H);
    text(modal,12,20,296,"CONFIRM PLAYBACK",&lv_font_montserrat_18);
    text(modal,12,70,296,"Playback sends checked property writes only. Fault reads before playback and after every write are safeguards, not protection guarantees. Payloads request settings; they do not prove the DUT applied them.",&lv_font_montserrat_14);
    text(modal,12,270,296,"Other-master traffic and safety reads may delay the recorded schedule. No retries and no automatic fault clearing.",&lv_font_montserrat_12);
    button(modal,8,422,148,"Cancel",closeDetail,0);auto *b=button(modal,164,422,148,"PLAY",recorderPlayConfirmed,0);ui_update::bgColor(b,lv_color_hex(0x238557),0);
}
bool recorderPacket(const bringup::Packet &p){
    if(!recorderPending || (p.type!=2 && p.type!=6 && p.type!=8) || p.length<6 || bringup::le32(p.payload)!=recorderPending)return false;
    if(p.type==2){
        if(p.payload[4]!=recorderOpcode)return false;
        if(p.payload[5]){recorderPending=0;recorderFinishPlayback("Request rejected / RP V12 required");return true;}
        if(recorderOpcode!=20)return true;
        recorderPending=0;recorderPhase=RecorderPhase::PauseArm;control.begin(observer::Action::Pause);return true;
    }
    const auto *r=p.payload;
    if(p.length<32){recorderPending=0;sessionReady=false;needsSync=true;recorderFinishPlayback("Short result / state uncertain");return true;}
    bool valid=r[29]==recorderAddress && r[7]==recorderRegister && r[4]<=10 && r[6]<=1;
    valid &= p.type==6 && p.length==32 && r[27]==3 && r[28]==(recorderOpcode==16?1:0);
    if(recorderOpcode==16 && recorderAt<recorder->count)valid &= bringup::le16(r+30)==recorder->entries[recorderAt].after.count+1;
    if(recorderOpcode==18)valid &= !bringup::le16(r+30);
    bool ok=valid && r[4]==0 && r[5]==1 && r[6];recorderPending=0;
    if(!ok){
        if(!valid || !r[6]){sessionReady=false;needsSync=true;}
        snprintf(recorderMessage,sizeof(recorderMessage),"%s / transfer code %u / no retry",valid?"Playback/acquisition stopped":"Invalid result",r[4]);
        recorderFinishPlayback(recorderMessage);return true;
    }
    if(recorderCancel){
        recorderFinishPlayback("Cancelled / bus released");return true;
    }
    if(recorderPhase==RecorderPhase::Preflight || recorderPhase==RecorderPhase::FaultAfter){
        uint8_t reg,mask;if(!recorderFaultPlan(recorderAddress,reg,mask) || (r[24]&mask)){
            snprintf(recorderMessage,sizeof(recorderMessage),"ABORT / 0x%02X fault 0x%02X = 0x%02X",recorderAddress,reg,r[24]);recorderFinishPlayback(recorderMessage);return true;}
        if(recorderPhase==RecorderPhase::Preflight){
            ++recorderBaselineAt;
        }else{++recorderAt;recorderPhase=RecorderPhase::Playing;}
        return true;
    }
    auto &e=recorder->entries[recorderAt];e.replayed=true;
    recorderPhase=RecorderPhase::FaultAfter;
    ++recorder->revision;return true;
}
void recorderTick(){
    if(!recorder)return;
    if(recorderPhase==RecorderPhase::Idle){if(recorderPostDialogPending && !modal)recorderPostDialog();return;}
    if(recorderPending && uint32_t(millis()-recorderSent_ms)>10000){
        recorderPending=0;sessionReady=false;needsSync=true;recorder->complete=false;recorder->stop("Timeout / state uncertain / no retry");recorderPhase=RecorderPhase::Idle;return;
    }
    if(!connected() || !sessionReady || registry.generation!=recorderGeneration || control.uncertain){
        if(!recorderWatching() && recorderPhase!=RecorderPhase::PostRestore)recorder->complete=false;
        recorder->stop("Link/profile/control changed / stopped");
        if(recorderPending){if(!recorderCancel)recorderAbort(nullptr);return;}
        bool show=recorderPost->active;recorderPost->gap();recorderPost->active=false;
        recorderPhase=RecorderPhase::Idle;if(show)recorderPostDialog();return;
    }
    if(recorderPending){if(uint32_t(millis()-recorderSent_ms)>10000){recorderPending=0;sessionReady=false;needsSync=true;recorder->complete=false;recorder->stop("Timeout / state uncertain / no retry");recorderPhase=RecorderPhase::Idle;}return;}
    if(control.busy())return;
    if(recorderCancel && recorderAcquiring()){
        recorderPhase=RecorderPhase::Finish;recorder->stop("Recording cancelled");control.begin(observer::Action::Pause);recorderCancel=false;return;
    }
    if(recorderPhase==RecorderPhase::Timing){recorderSend(20);return;}
    if(recorderPhase==RecorderPhase::PauseArm){
        if(status[0])return;recorderPhase=RecorderPhase::StartArm;sourceFilter(selection);recorder->start();control.begin(observer::Action::Enable);return;
    }
    if(recorderPhase==RecorderPhase::StartArm){recorderPhase=RecorderPhase::Recording;return;}
    if(recorderPhase==RecorderPhase::Recording){
        if(!recorder->recording){recorderPhase=RecorderPhase::Finish;control.begin(observer::Action::Pause);}return;
    }
    if(recorderPhase==RecorderPhase::Finish){
        if(status[0])return;recorder->stop(recorder->complete?"Recording finished / capture paused":recorder->message);recorderPhase=RecorderPhase::Idle;sourceFilter(selection);return;
    }
    if(recorderPhase==RecorderPhase::PausePlay){
        if(status[0])return;recorderPhase=RecorderPhase::Preflight;recorderBaselineAt=0;
    }
    if(recorderPhase==RecorderPhase::Preflight){
        while(recorderBaselineAt<128){uint8_t address=uint8_t(recorderBaselineAt);
            bool used=false;for(unsigned i=0;i<recorder->count;++i)used|=recorder->entries[i].checked && recorder->entries[i].transaction.address==address;
            if(!used){++recorderBaselineAt;continue;}uint8_t reg=0,mask=0;
            if(!recorderFaultPlan(address,reg,mask)){recorderFinishPlayback("Missing verified fault check");return;}
            recorderSend(18,address,reg);return;
        }
        recorderPlay_us=micros();recorderElapsed_us=0;recorderPhase=RecorderPhase::Playing;
    }
    if(recorderPhase==RecorderPhase::Playing || recorderPhase==RecorderPhase::FaultAfter){
        uint32_t now=micros();recorderElapsed_us+=uint32_t(now-recorderPlay_us);recorderPlay_us=now;
    }
    if(recorderCancel && (recorderPhase==RecorderPhase::Playing || recorderPhase==RecorderPhase::FaultAfter)){recorderFinishPlayback("Cancelled / no more operations");}
    if(recorderPhase==RecorderPhase::Playing){
        while(recorderAt<recorder->count && !recorder->entries[recorderAt].checked)++recorderAt;
        if(recorderAt==recorder->count){
            unsigned writes=0;uint32_t late=0;
            for(unsigned i=0;i<recorder->count;++i){const auto &e=recorder->entries[i];if(e.replayed){++writes;if(e.actualDelay_us>late)late=e.actualDelay_us;}}
            snprintf(recorderMessage,sizeof(recorderMessage),"Done / %u writes / max dispatch delay %lu us",writes,(unsigned long)late);
            recorderPost->begin(millis());recorderPostResume=false;recorderPhase=RecorderPhase::PostStart;
            for(unsigned i=0;i<recorder->count;++i)if(recorder->entries[i].replayed)recorderPost->select(recorder->entries[i].transaction.address,recorder->entries[i].transaction.reg);
            recorder->message="Playback done / starting 5s write watch";++recorder->revision;
            sourceFilter(selection);control.begin(observer::Action::Enable);return;
        }else{
            auto &e=recorder->entries[recorderAt];uint64_t due=recorder->microseconds(e.timing.start[0]-recorderOrigin_cycles);
            uint64_t elapsed=recorderElapsed_us;if(elapsed<due)return;
            const auto &o=e.transaction;uint8_t bytes[257];
            bool sent=false;
            bytes[0]=o.reg;memcpy(bytes+1,e.after.data,e.after.count);sent=recorderSend(16,o.address,0,bytes,e.after.count+1);
            if(sent)e.actualDelay_us=elapsed-due>UINT32_MAX?UINT32_MAX:uint32_t(elapsed-due);return;
        }
    }
    if(recorderPhase==RecorderPhase::FaultAfter){uint8_t reg=0,mask=0;auto &e=recorder->entries[recorderAt];
        if(!recorderFaultPlan(e.transaction.address,reg,mask)){recorderFinishPlayback("Missing fault plan / abort");return;}recorderSend(18,e.transaction.address,reg);return;}
    if(recorderPhase==RecorderPhase::Restore){
        recorderPhase=RecorderPhase::Idle;sourceFilter(selection);
        if(recorderWasRunning && sessionReady && !control.uncertain)control.begin(observer::Action::Enable);
    }
    if(recorderPhase==RecorderPhase::PostStart){
        if(recorderCancel){recorderPhase=RecorderPhase::PostPause;control.begin(observer::Action::Pause);return;}
        if(!status[0])return;recorderWatch_ms=millis();recorderPhase=RecorderPhase::PostWatch;
        recorder->message="Watching writes after playback / 5 seconds";++recorder->revision;return;
    }
    if(recorderPhase==RecorderPhase::PostWatch){
        if(!recorderCancel && uint32_t(millis()-recorderWatch_ms)<5000)return;
        recorderPhase=RecorderPhase::PostPause;control.begin(observer::Action::Pause);return;
    }
    if(recorderPhase==RecorderPhase::PostPause){
        if(status[0])return;recorderPost->active=false;recorderPost->cancelled=recorderCancel;recorderPhase=RecorderPhase::PostRestore;sourceFilter(selection);
        if(recorderWasRunning){recorderPostResume=true;control.begin(observer::Action::Enable);}return;
    }
    if(recorderPhase==RecorderPhase::PostRestore){
        if(recorderPostResume && !status[0])return;
        recorderPhase=RecorderPhase::Idle;recorder->message=recorderMessage;++recorder->revision;recorderPostDialog();
    }
}
void recorderCheck(lv_event_t *e){unsigned i=*static_cast<unsigned *>(lv_event_get_user_data(e));if(i<recorder->count && !recorderBusy())recorder->entries[i].checked=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);}
void recorderBytes(const recording18::Bytes &b,char *out,size_t cap){
    if(!b.known){snprintf(out,cap,"?");return;}
    if(b.count==1){for(unsigned i=0;i<8 && i+1<cap;++i)out[i]=char('0'+((b.data[0]>>(7-i))&1));if(cap>8)out[8]=0;return;}
    unsigned at=0;for(unsigned i=0;i<b.count && at+4<cap;++i)at+=snprintf(out+at,cap-at,"%02X%s",b.data[i],i+1<b.count?" ":"");
    if(at+1<cap)out[at]=0;
}
void recorderFields(const recording18::Entry &e,bool descriptions,char *out,size_t cap){
    unsigned at=0;out[0]=0;const auto &bytes=e.after;
    for(unsigned i=0;i<decoding::PropertyCount && at+160<cap;++i){const auto &p=decoding::properties[i];
        if(!p.count || p.address!=e.transaction.address || p.low!=e.transaction.reg || p.count!=bytes.count)continue;
        analysis::Value v;v.property=i;v.complete=v.success=true;v.direction=e.transaction.direction;
        for(unsigned n=0;n<p.count;++n)v.word|=uint32_t(bytes.data[n])<<(8*n);
        char value[112];details::value(v,value,sizeof(value));
        if(descriptions)at+=snprintf(out+at,cap-at,"%s\n%s\n",p.name,p.friendlydesc?p.friendlydesc:"");
        else at+=snprintf(out+at,cap-at,"%s = %s\n",p.name,value);
    }
    if(!at)snprintf(out,cap,"%s",descriptions?"": "Unparsed write");
}
void recorderPostDialog(){
    if(modal){recorderPostDialogPending=true;return;}recorderPostDialogPending=false;
    if(!recorderPost->count && !recorderPost->gaps && !recorderPost->dropped){
        size_t n=strlen(recorderMessage);snprintf(recorderMessage+n,sizeof(recorderMessage)-n," / %s: no writes",recorderPost->cancelled?"watch cancelled":"5s watch");return;
    }
    modal=lv_obj_create(lv_screen_active());flat(modal,Bg);lv_obj_set_size(modal,W,H);
    text(modal,10,12,234,"POST-PLAYBACK WRITES",&lv_font_montserrat_14);
    auto *done=button(modal,252,4,60,"Done",closeDetail,0);ui_update::bgColor(done,lv_color_hex(0x238557),0);
    auto &scratch=recorder->render;
    snprintf(scratch.title,sizeof(scratch.title),"%u writes / dropped %u / gaps %u%s\nPlayed registers / 5s watch. Causality not proven.\nCapture resumes after final fault check; brief blind gap.",recorderPost->count,recorderPost->dropped,recorderPost->gaps,recorderPost->cancelled?" / cancelled":"");
    text(modal,10,48,300,scratch.title,&lv_font_montserrat_10);
    const unsigned pageSize=4,first=recorderPostPage*pageSize,last=first+pageSize<recorderPost->count?first+pageSize:recorderPost->count;
    bool pages=recorderPost->count>pageSize;
    auto *body=lv_obj_create(modal);flat(body,Bg);lv_obj_set_pos(body,8,100);lv_obj_set_size(body,304,pages?324:372);lv_obj_set_scroll_dir(body,LV_DIR_VER);
    recorderPostNext=nullptr;
    if(pages){
        auto *prev=button(modal,8,434,60,LV_SYMBOL_LEFT,recorderPostPageAction,0);
        recorderPostNext=button(modal,252,434,60,LV_SYMBOL_RIGHT,recorderPostPageAction,1);
        if(!first)lv_obj_add_state(prev,LV_STATE_DISABLED);if(last==recorderPost->count)lv_obj_add_state(recorderPostNext,LV_STATE_DISABLED);
        snprintf(scratch.value,sizeof(scratch.value),"%u-%u / %u",first+1,last,recorderPost->count);text(modal,88,446,152,scratch.value,&lv_font_montserrat_12);
    }
    int y=0;
    for(unsigned i=first;i<last;++i){
        const auto &e=recorderPost->detail(i);const auto &event=recorderPost->entries[i];
        recorderBytes(e.after,scratch.value,sizeof(scratch.value));
        double ms=event.pio_Hz?double(event.start_cycles)*1000.0/event.pio_Hz:double(event.arrival_ms);
        snprintf(scratch.title,sizeof(scratch.title),"+%.3f ms %02X:%02X W %s\n%s",ms,e.transaction.address,e.transaction.reg,e.transaction.success?"OK":"FAIL",scratch.value);
        recorderFields(e,false,scratch.left,sizeof(scratch.left));recorderFields(e,true,scratch.right,sizeof(scratch.right));
        lv_point_t heading,left,right;lv_text_get_size(&heading,scratch.title,&mono::font,0,0,292,LV_TEXT_FLAG_NONE);
        lv_text_get_size(&left,scratch.left,&lv_font_montserrat_10,0,0,140,LV_TEXT_FLAG_NONE);lv_text_get_size(&right,scratch.right,&lv_font_montserrat_10,0,0,140,LV_TEXT_FLAG_NONE);
        int pos=heading.y+12,height=pos+(left.y>right.y?left.y:right.y)+10;
        auto *row=inset(body,0,y,304,height);auto *title=text(row,6,4,292,scratch.title,&mono::font);
        ui_update::textColor(title,lv_color_hex(e.transaction.success?Amber:Red),0);
        auto *l=text(row,6,pos,140,scratch.left,&lv_font_montserrat_10);auto *r=text(row,158,pos,140,scratch.right,&lv_font_montserrat_10);
        ui_update::textColor(l,lv_color_hex(Muted),0);ui_update::textColor(r,lv_color_hex(Cyan),0);y+=height+6;
    }
}
void recorderPostPageAction(lv_event_t *e){
    bool next=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))!=0;
    if(next){if((recorderPostPage+1)*4>=recorderPost->count)return;++recorderPostPage;}
    else{if(!recorderPostPage)return;--recorderPostPage;}
    closeDetail(nullptr);recorderPostDialog();
}
bool recorderCanPlay(){
    if(recorderBusy() || !recorder->complete)return false;unsigned checked=0;
    for(unsigned i=0;i<recorder->count;++i)if(recorder->entries[i].checked){uint8_t reg=0,mask=0;
        if(!recording18::replayable(recorder->entries[i]) || !recorderFaultPlan(recorder->entries[i].transaction.address,reg,mask))return false;++checked;}
    return checked!=0;
}
void refreshRecorder(){
    if(!recorderInfo)return;
    if(!recorderBusy() && (recorderFilterGeneration!=registry.generation || recorderFilterEvidence!=registry.evidenceGeneration))recorderFilterRefresh();
    if(recorderBusy())lv_obj_add_state(recorderChoice,LV_STATE_DISABLED);else lv_obj_remove_state(recorderChoice,LV_STATE_DISABLED);
    ui_update::text(lv_obj_get_child(recorderArm,0),recorderAcquiring()?"DONE":"START");ui_update::bgColor(recorderArm,lv_color_hex(recorderAcquiring()?0x9c3544:0x238557),0);
    bool playing=recorderBusy() && !recorderAcquiring();bool available=recorderCanPlay() && connected() && sessionReady && !control.busy() && !rateRequest && !activeBusy();
    ui_update::text(lv_obj_get_child(recorderPlay,0),playing?"CANCEL":"PLAYBACK");ui_update::bgColor(recorderPlay,lv_color_hex(playing?0x9c3544:available?0x238557:0x303a3e),0);
    if(available || playing)lv_obj_remove_state(recorderPlay,LV_STATE_DISABLED);else lv_obj_add_state(recorderPlay,LV_STATE_DISABLED);
    if(recorderBusy() && !recorderAcquiring())lv_obj_add_state(recorderArm,LV_STATE_DISABLED);else lv_obj_remove_state(recorderArm,LV_STATE_DISABLED);
    ui_update::textFmt(recorderInfo,"%s\n%u writes / %u register keys%s",recorder->message,recorder->count,recorder->keyCount,recorder->complete?"":" / INCOMPLETE");
    int scroll=lv_obj_get_scroll_y(recorderBody);
    if(recorderRendered==recorder->revision && recorderScroll==scroll)return;recorderRendered=recorder->revision;recorderScroll=scroll;
    if(recorderRenderedSession!=recorder->session || recorderPaintCount>recorder->count || (!recorder->count && recorderPaintCount)){
        lv_obj_clean(recorderBody);memset(recorderPool,0,sizeof(recorderPool));memset(recorderHeights,0,sizeof(recorderHeights));memset(recorderRowState,0,sizeof(recorderRowState));recorderPaintCount=0;
        recorderSpacer=lv_obj_create(recorderBody);flat(recorderSpacer,Bg);lv_obj_set_size(recorderSpacer,1,1);lv_obj_remove_flag(recorderSpacer,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_scroll_to_y(recorderBody,0,LV_ANIM_OFF);scroll=0;recorderScroll=0;
        recorderRenderedSession=recorder->session;
    }
    memset(recorderRows,0,sizeof(recorderRows));
    unsigned slot=0;int y=0;
    for(unsigned i=0;i<recorder->count;++i){auto &e=recorder->entries[i];
        uint8_t rowState=1|(e.replayed?2:0)|(recorderBusy()?8:0);
        bool visible=y<=scroll+lv_obj_get_height(recorderBody) && y+recorderHeights[i]>=scroll;
        if(!visible && recorderHeights[i] && recorderRowState[i]==rowState){y+=recorderHeights[i]+6;continue;}
        auto *row=visible && slot<8?recorderPool[slot]:nullptr;
        bool created=!row;
        if(visible && slot<8){
        if(created){row=inset(recorderBody,0,y,304,64);recorderPool[slot]=row;auto *cb=lv_checkbox_create(row);lv_checkbox_set_text(cb,"");lv_obj_set_pos(cb,4,5);
            lv_obj_add_event_cb(cb,recorderCheck,LV_EVENT_VALUE_CHANGED,&recorderPoolIndex[slot]);}
        recorderPoolIndex[slot]=i;recorderRows[i]=row;++slot;lv_obj_remove_flag(row,LV_OBJ_FLAG_HIDDEN);
        auto *cb=lv_obj_get_child(row,0);if(e.checked)lv_obj_add_state(cb,LV_STATE_CHECKED);else lv_obj_remove_state(cb,LV_STATE_CHECKED);
        if(recorderBusy() || !recording18::replayable(e))lv_obj_add_state(cb,LV_STATE_DISABLED);else lv_obj_remove_state(cb,LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(cb,lv_color_hex(Green),LV_PART_INDICATOR|LV_STATE_CHECKED);
        lv_obj_set_style_text_color(cb,lv_color_hex(0x14241b),LV_PART_INDICATOR|LV_STATE_CHECKED);
        }else row=nullptr;
        recorderRowState[i]=rowState;
        auto &value=recorder->render.value;auto &title=recorder->render.title;value[0]=0;recorderBytes(e.after,value,sizeof(value));
        uint64_t delta=i?e.timing.start[0]-recorder->entries[i-1].timing.start[0]:e.timing.start[0];
        snprintf(title,sizeof(title),"+%.3f ms %02X:%02X W\n%s%s",recorder->microseconds(delta)/1000.0,e.transaction.address,e.transaction.reg,value,!e.transaction.success?" / FAIL":!recording18::replayable(e)?" / UNSUPPORTED":e.replayed?" / PLAYED":"");
        if(row){auto *heading=created?text(row,38,4,260,title,&mono::font):lv_obj_get_child(row,1);ui_update::text(heading,title);ui_update::textColor(heading,lv_color_hex(!e.transaction.success?Red:Amber),0);}
        lv_point_t size;lv_text_get_size(&size,title,&mono::font,0,0,260,LV_TEXT_FLAG_NONE);int pos=size.y+12;
        auto &left=recorder->render.left;auto &right=recorder->render.right;recorderFields(e,false,left,sizeof(left));recorderFields(e,true,right,sizeof(right));
        if(row){auto *l=created?text(row,6,pos,140,left,&lv_font_montserrat_10):lv_obj_get_child(row,2);auto *r=created?text(row,158,pos,140,right,&lv_font_montserrat_10):lv_obj_get_child(row,3);
        ui_update::text(l,left);ui_update::text(r,right);
        lv_obj_set_y(l,pos);lv_obj_set_y(r,pos);
        lv_obj_set_style_text_color(l,lv_color_hex(Muted),0);lv_obj_set_style_text_color(r,lv_color_hex(Cyan),0);
        }
        lv_point_t a,b;lv_text_get_size(&a,left,&lv_font_montserrat_10,0,0,140,LV_TEXT_FLAG_NONE);lv_text_get_size(&b,right,&lv_font_montserrat_10,0,0,140,LV_TEXT_FLAG_NONE);
        int height=pos+(a.y>b.y?a.y:b.y)+8;
        auto &timingLine=recorder->render.timing;
        snprintf(timingLine,sizeof(timingLine),"t %.3f ms / duration %.3f ms%s",recorder->microseconds(e.timing.start[0])/1000.0,recorder->microseconds(e.timing.end[e.timing.count-1]-e.timing.start[0])/1000.0,e.replayed?" / playback":"");
        if(row){auto *meta=created?text(row,6,height,292,timingLine,&lv_font_montserrat_10):lv_obj_get_child(row,4);
        ui_update::text(meta,timingLine);ui_update::textColor(meta,lv_color_hex(Muted),0);lv_obj_set_y(meta,height);
        }
        lv_point_t metaSize;lv_text_get_size(&metaSize,timingLine,&lv_font_montserrat_10,0,0,292,LV_TEXT_FLAG_NONE);height+=metaSize.y+8;
        recorderHeights[i]=height;if(row){lv_obj_set_height(row,height);lv_obj_set_y(row,y);}y+=height+6;
    }
    for(unsigned n=slot;n<8;++n)if(recorderPool[n])lv_obj_add_flag(recorderPool[n],LV_OBJ_FLAG_HIDDEN);
    if(recorderSpacer)lv_obj_set_y(recorderSpacer,y?y-1:0);
    recorderPaintCount=recorder->count;
}
void createRecorder(){
    recorderScreen=lv_obj_create(lv_screen_active());flat(recorderScreen,Bg);lv_obj_set_size(recorderScreen,W,H);lv_obj_set_pos(recorderScreen,4*W,0);lv_obj_remove_flag(recorderScreen,LV_OBJ_FLAG_SCROLLABLE);
    text(recorderScreen,8,8,304,"RECORDER",&lv_font_montserrat_18);
    recorderChoice=lv_dropdown_create(recorderScreen);lv_obj_set_pos(recorderChoice,8,36);lv_obj_set_size(recorderChoice,304,32);
    extraDropdownStyle(recorderChoice);lv_obj_set_style_text_font(recorderChoice,&lv_font_montserrat_10,0);
    lv_dropdown_set_options(recorderChoice,"Current Filter");lv_obj_add_event_cb(recorderChoice,recorderFilterChanged,LV_EVENT_VALUE_CHANGED,nullptr);
    recorderArm=button(recorderScreen,8,76,148,"START",recorderArmAction,0);
    recorderPlay=button(recorderScreen,164,76,148,"PLAYBACK",recorderPlayAction,0);
    recorderInfo=text(recorderScreen,8,126,304,"",&lv_font_montserrat_10);
    recorderBody=lv_obj_create(recorderScreen);flat(recorderBody,Bg);lv_obj_set_pos(recorderBody,8,180);lv_obj_set_size(recorderBody,304,292);lv_obj_set_scroll_dir(recorderBody,LV_DIR_VER);
    refreshRecorder();
}
