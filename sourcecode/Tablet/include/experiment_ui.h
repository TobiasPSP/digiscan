// Included inside the UI namespace; acquisition remains in pump(), not touch callbacks.
// START scans and graph acquisition share the acknowledged active-job transport.
// Address tiles reflect confirmed evidence, not speculative malformed frames.
// Scan ranges skip already confirmed addresses and merge replies into the same
// registry used by parsing. Graph updates stop independently from capture.
// Helpers are included outside this namespace in main.cpp.
lv_obj_t *readAddress=nullptr,*readRegisters=nullptr,*readLength=nullptr,*readEndian=nullptr,*readView=nullptr,*readType=nullptr;
lv_obj_t *readButton=nullptr,*readInfo=nullptr,*readResults=nullptr,*readOutput=nullptr;
uint8_t readPlan[manual_read::MaxRegisters]={};unsigned readPlanCount=0,readAt=0,readBytes=1;
manual_read::Result readValues[manual_read::MaxRegisters];bool readCancel=false;
uint8_t readTarget=0;
void readRender();
lv_obj_t *writeAddress,*writePrefix,*writePrefixKind,*writeFormat,*writePayload,*writeStatus;
lv_obj_t *scanAddress,*scanList,*scanStatus,*graphChoice,*graphChart,*graphInfo;
lv_obj_t *graphButton=nullptr;
lv_obj_t *scanButton=nullptr,*scanBody=nullptr,*identifyChoice=nullptr;
lv_obj_t *startLinkIcon=nullptr,*startSessionButton=nullptr,*startInfo=nullptr,*startHint=nullptr;
uint32_t scanEvidenceGeneration=0xffffffffu;
lv_obj_t *scanTiles[128]={},*scanNames[128]={};
unsigned scanSkipped=0;uint32_t scanBindingGeneration=0xffffffffu;
char identifyOutput[4096]={};
lv_obj_t *extraKeyboard=nullptr;
uint8_t writeBytes[258]={};unsigned writeCount=0;
uint8_t preparedWriteAddress=0,extraRegister=0;unsigned extraWriteCount=0;
uint32_t extraAt=0;uint8_t extraOp=0,extraAddress=0;
enum class Work { Idle, Scan, Identify, Read };
Work extraWork=Work::Idle;unsigned scanNext=8,scanEnd=119,identifyAt=0;
void scanTilePressed(lv_event_t *);
void cancelExtra(lv_event_t *);
void scanStart(lv_event_t *);
void graphManual(lv_event_t *);
void scanToggle(lv_event_t *e){if(extraWork==Work::Scan || extraPending)cancelExtra(e);else scanStart(e);}
uint8_t identifyRegs[32]={},identifyBytes[256]={};bool identifyValid[256]={},scanFound[128]={};
unsigned identifyCount=0;char extraMessage[384]="Ready";
struct Plot {unsigned property=0;double min=0,max=1;uint32_t last=0,color=0;lv_chart_series_t *series=nullptr;};
Plot plots[8];unsigned plotCount=0,graphReadAt=0;bool graphActive=false,graphReading=false;
bool graphStopped=true;
bool graphConfiguredActive=false;
enum class GraphMode { Passive, Hybrid, Active };
GraphMode graphMode=GraphMode::Passive;
lv_obj_t *graphModeButton=nullptr,*graphInterval=nullptr;
bool graphStartPending=false;
const char *graphModeName(){return graphMode==GraphMode::Passive?"Passive":graphMode==GraphMode::Hybrid?"Passive/Active":"Active";}
uint32_t graphEvidenceGeneration=0xffffffffu;
void graphLinkLost(){graphStartPending=false;graphStopped=true;graphActive=graphReading=false;if(extraWork==Work::Idle)extraSessionBusy=false;}
uint32_t graphRefresh_ms=1000,graphDue=0,graphGeneration=0,graphPaint=0;
uint32_t graphRequest=0;bool graphResult=false,graphSafe=false,scanDirty=false;
lv_obj_t *graphError,*graphChecks[512]={};
struct GraphOption {unsigned graph;uint8_t address;};GraphOption graphOptions[128];unsigned graphOptionsCount=0;
bool hexNumber(const char *s,unsigned max,unsigned &number){
    if(!s || !*s || *s=='-')return false;char *end;unsigned long n=strtoul(s,&end,16);
    if(end==s || *end || n>max)return false;number=unsigned(n);return true;
}
void extraKeyboardEvent(lv_event_t *e){if(lv_event_get_code(e)==LV_EVENT_READY || lv_event_get_code(e)==LV_EVENT_CANCEL)lv_obj_add_flag(extraKeyboard,LV_OBJ_FLAG_HIDDEN);}
void extraFocus(lv_event_t *e){
    if(!extraKeyboard){extraKeyboard=lv_keyboard_create(lv_screen_active());lv_obj_set_size(extraKeyboard,W,180);lv_obj_set_pos(extraKeyboard,0,H-180);
        lv_obj_add_event_cb(extraKeyboard,extraKeyboardEvent,LV_EVENT_ALL,nullptr);}
    lv_obj_move_foreground(extraKeyboard);lv_keyboard_set_textarea(extraKeyboard,lv_event_get_target_obj(e));lv_obj_remove_flag(extraKeyboard,LV_OBJ_FLAG_HIDDEN);
}
lv_obj_t *extraInput(lv_obj_t *parent,int x,int y,int width,const char *initial){
    auto *o=lv_textarea_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,36);lv_textarea_set_one_line(o,true);lv_textarea_set_text(o,initial);
    lv_obj_set_style_text_font(o,&lv_font_montserrat_12,0);lv_obj_add_event_cb(o,extraFocus,LV_EVENT_CLICKED,nullptr);return o;
}
void extraDropdownStyle(lv_obj_t *o){lv_obj_set_style_text_font(o,&lv_font_montserrat_12,0);lv_obj_set_style_bg_color(o,lv_color_hex(0x2c363b),0);lv_obj_set_style_text_color(o,lv_color_hex(Ink),0);}
void readRender(){
    if(!readOutput)return;unsigned i=lv_dropdown_get_selected(readResults);if(i>=readPlanCount){ui_update::text(readOutput,"");return;}
    const auto &r=readValues[i];char output[4096];unsigned at=0;
    auto append=[&](const char *format,...){if(at+1>=sizeof(output))return;va_list args;va_start(args,format);int n=vsnprintf(output+at,sizeof(output)-at,format,args);va_end(args);if(n>0)at+=unsigned(n)<sizeof(output)-at?unsigned(n):sizeof(output)-at-1;};
    append("0x%02X R 0x%02X / %u/%u bytes\n",readTarget,r.reg,r.count,readBytes);
    if(!r.complete){append("Not read yet");ui_update::text(readOutput,output);return;}
    append("%s / code %u / capture %s\n",r.ok?"OK":"FAIL",r.code,r.restored?"restored":"UNCERTAIN");
    unsigned type=lv_dropdown_get_selected(readType);bool big=lv_dropdown_get_selected(readEndian)==1;
    if(r.ok && type && r.count<=8){uint64_t word=manual_read::word(r.bytes,r.count,big);
        if(type==1)append("Unsigned: %llu\n",(unsigned long long)word);
        if(type==2){if(r.count<8 && (word&(uint64_t(1)<<(r.count*8-1))))word|=UINT64_MAX<<(r.count*8);int64_t signedWord;memcpy(&signedWord,&word,8);append("Signed: %lld\n",(long long)signedWord);}
        if(type==3 && r.count==4){uint32_t bits=uint32_t(word);float value;memcpy(&value,&bits,4);append("Float32: %.9g\n",double(value));}
    }
    if(r.ok && type && (r.count>8 || (type==3 && r.count!=4)))append("%s\n",type==3?"Float32 requires 4 bytes":"Integer requires 1-8 bytes");
    unsigned view=lv_dropdown_get_selected(readView);
    for(unsigned n=0;n<r.count;++n){if(!(n%8))append("\n+%03u ",n);
        if(view==0)append("%02X/%u ",r.bytes[n],r.bytes[n]);
        else if(view==1)append("%02X ",r.bytes[n]);
        else if(view==2)append("%3u ",r.bytes[n]);
        else if(view==3){char bits[9];for(unsigned b=0;b<8;++b)bits[b]=char('0'+((r.bytes[n]>>(7-b))&1));bits[8]=0;append("%s ",bits);}
        else append("%c",r.bytes[n]>=32 && r.bytes[n]<=126?r.bytes[n]:'.');
    }
    ui_update::text(readOutput,output);ui_update::textColor(readOutput,lv_color_hex(r.ok?Ink:Red),0);
}
void readDisplayChanged(lv_event_t *){readRender();}
void readAction(lv_event_t *){
    if(extraWork==Work::Read || (extraPending && extraOp==21)){
        readCancel=true;
        if(extraPending){uint8_t op=14,wire[32];size_t n=bringup::command(++control.nextId,&op,1,wire,sizeof(wire));if(uart.availableForWrite()>=int(n))uart.write(wire,n);}
        else{extraWork=Work::Idle;extraSessionBusy=false;snprintf(extraMessage,sizeof(extraMessage),"Cancelled / no pending transfer");}return;
    }
    if(activeBusy() || control.busy() || rateRequest)return;
    if(!connected() || !sessionReady || !(val(132)&64)){snprintf(extraMessage,sizeof(extraMessage),"Connect RP V12 before reading");return;}
    unsigned address,length,count;uint8_t plan[manual_read::MaxRegisters];
    if(!hexNumber(lv_textarea_get_text(readAddress),119,address) || address<8){snprintf(extraMessage,sizeof(extraMessage),"Address: hex 08-77");return;}
    if(!manual_read::length(lv_textarea_get_text(readLength),length)){snprintf(extraMessage,sizeof(extraMessage),"Length: decimal 1-256 bytes");return;}
    if(!manual_read::registers(lv_textarea_get_text(readRegisters),plan,count)){snprintf(extraMessage,sizeof(extraMessage),"Registers: hex list/range, max 32 starts");return;}
    for(unsigned i=0;i<count;++i)if(unsigned(plan[i])+length>256){snprintf(extraMessage,sizeof(extraMessage),"Read would wrap beyond register FF");return;}
    unsigned type=lv_dropdown_get_selected(readType);
    if((type==1 || type==2) && length>8 || type==3 && length!=4){snprintf(extraMessage,sizeof(extraMessage),"Integer: 1-8 bytes / Float32: 4 bytes");return;}
    if(extraKeyboard)lv_obj_add_flag(extraKeyboard,LV_OBJ_FLAG_HIDDEN);
    memcpy(readPlan,plan,count);readPlanCount=count;readBytes=length;readAt=0;readCancel=false;extraAddress=readTarget=uint8_t(address);
    lv_dropdown_clear_options(readResults);
    for(unsigned i=0;i<count;++i){readValues[i]=manual_read::Result{};readValues[i].reg=plan[i];char label[32];snprintf(label,sizeof(label),"0x%02X / %u bytes",plan[i],length);lv_dropdown_add_option(readResults,label,LV_DROPDOWN_POS_LAST);}
    graphActive=false;graphReading=false;graphStopped=true;extraWork=Work::Read;extraSessionBusy=true;
    snprintf(extraMessage,sizeof(extraMessage),"Reading %u register starts / %u bytes each",count,length);readRender();
}
void createManualRead(){
    readScreen=lv_obj_create(lv_screen_active());flat(readScreen,Bg);lv_obj_set_size(readScreen,W,H);lv_obj_set_pos(readScreen,5*W,0);lv_obj_remove_flag(readScreen,LV_OBJ_FLAG_SCROLLABLE);
    text(readScreen,8,8,304,"I2C READ",&lv_font_montserrat_18);
    text(readScreen,8,36,144,"Address (hex)",&lv_font_montserrat_10);text(readScreen,168,36,144,"Bytes / register (dec)",&lv_font_montserrat_10);
    readAddress=extraInput(readScreen,8,50,144,"75");readLength=extraInput(readScreen,168,50,144,"1");
    text(readScreen,8,92,304,"Registers (hex list/range)",&lv_font_montserrat_10);readRegisters=extraInput(readScreen,8,106,304,"00");
    readView=lv_dropdown_create(readScreen);lv_obj_set_pos(readView,8,152);lv_obj_set_size(readView,144,36);lv_dropdown_set_options(readView,"Hex + Decimal\nHex\nDecimal\nBinary\nASCII");
    readEndian=lv_dropdown_create(readScreen);lv_obj_set_pos(readEndian,168,152);lv_obj_set_size(readEndian,144,36);lv_dropdown_set_options(readEndian,"Little endian\nBig endian");
    readType=lv_dropdown_create(readScreen);lv_obj_set_pos(readType,8,194);lv_obj_set_size(readType,304,36);lv_dropdown_set_options(readType,"Byte values\nUnsigned integer\nSigned integer\nFloat32");
    for(auto *o:{readView,readEndian,readType}){extraDropdownStyle(o);lv_obj_add_event_cb(o,readDisplayChanged,LV_EVENT_VALUE_CHANGED,nullptr);}
    readButton=button(readScreen,8,238,100,"READ",readAction,0);ui_update::bgColor(readButton,lv_color_hex(0x238557),0);
    readInfo=text(readScreen,116,238,196,"",&lv_font_montserrat_10);
    readResults=lv_dropdown_create(readScreen);lv_obj_set_pos(readResults,8,302);lv_obj_set_size(readResults,304,32);lv_dropdown_set_options(readResults,"Results");extraDropdownStyle(readResults);lv_obj_add_event_cb(readResults,readDisplayChanged,LV_EVENT_VALUE_CHANGED,nullptr);
    auto *body=lv_obj_create(readScreen);flat(body,Bg);lv_obj_set_pos(body,8,342);lv_obj_set_size(body,304,130);lv_obj_set_scroll_dir(body,LV_DIR_VER);
    readOutput=text(body,4,4,290,"",&lv_font_montserrat_10);
}
void extraClose(lv_event_t *){if(extraKeyboard)lv_obj_add_flag(extraKeyboard,LV_OBJ_FLAG_HIDDEN);closeDetail(nullptr);}
bool rawRequest(uint8_t op,uint8_t address,const uint8_t *data,unsigned count){
    if(!connected() || !sessionReady || !(val(132)&64) || activePending || reportBatch.running || extraPending || control.busy() || rateRequest || status[2] || control.uncertain){
        snprintf(extraMessage,sizeof(extraMessage),"Not ready / update RP to V10; wait for link and idle operation");return false;}
    uint8_t command[267]={op,0xa5},wire[320];
    if(!activeSession){
#ifdef TABLET_PREVIEW
        activeSession=millis()+1;
#else
        activeSession=esp_random();if(!activeSession)activeSession=1;
#endif
    }
    bringup::put32(command+2,activeSession);command[6]=address;size_t bytes=7;
    if(op==16){command[7]=uint8_t(count);command[8]=uint8_t(count>>8);memcpy(command+9,data,count);bytes=9+count;}
    if(op==18){command[7]=data[0];bytes=8;}
    if(op==19){command[7]=uint8_t(scanEnd);bytes=8;}
    if(op==21){command[7]=data[0];command[8]=uint8_t(count);command[9]=uint8_t(count>>8);bytes=10;}
    uint32_t id=++control.nextId;if(!id)id=++control.nextId;
    size_t n=bringup::command(id,command,bytes,wire,sizeof(wire));
    if(!n || uart.availableForWrite()<int(n))return false;
    extraPending=id;extraAt=millis();extraOp=op;extraAddress=address;extraWriteCount=op==16?count:0;extraRegister=op==18 || op==21?data[0]:0;decoderGap();
    if(uart.write(wire,n)!=n){sessionReady=false;needsSync=true;}
    snprintf(extraMessage,sizeof(extraMessage),"0x%02X / waiting for bus idle",address);return true;
}
void writeCommit(lv_event_t *){
    extraClose(nullptr);rawRequest(16,preparedWriteAddress,writeBytes,writeCount);
}
void writePrepare(lv_event_t *){
    unsigned address,prefix=0,count=0;uint8_t payload[256];
    unsigned kind=lv_dropdown_get_selected(writePrefixKind);
    if(!hexNumber(lv_textarea_get_text(writeAddress),119,address)||address<8 ||
        (kind && !hexNumber(lv_textarea_get_text(writePrefix),kind==1?255:65535,prefix)) ||
        !experiment::payload(lv_textarea_get_text(writePayload),experiment::Format(lv_dropdown_get_selected(writeFormat)),payload,count)){
        snprintf(extraMessage,sizeof(extraMessage),"Invalid address, prefix or payload (max 256 bytes)");return;}
    if(sessionMutation || startupCapturePending || activeBusy() || control.busy() || rateRequest || extraWork!=Work::Idle)return;
    preparedWriteAddress=uint8_t(address);writeCount=0;if(kind==1)writeBytes[writeCount++]=uint8_t(prefix);
    if(kind>=2){writeBytes[writeCount++]=uint8_t(prefix>>(kind==2?0:8));writeBytes[writeCount++]=uint8_t(prefix>>(kind==2?8:0));}
    memcpy(writeBytes+writeCount,payload,count);writeCount+=count;
    if(extraKeyboard)lv_obj_add_flag(extraKeyboard,LV_OBJ_FLAG_HIDDEN);
    modal=lv_obj_create(lv_screen_active());flat(modal,Bg);lv_obj_set_size(modal,W,H);
    text(modal,12,16,290,"CONFIRM EXPERIMENTAL WRITE",&lv_font_montserrat_16);
    char preview[1024];unsigned pos=snprintf(preview,sizeof(preview),"Address 0x%02X / %u bytes\n",address,writeCount);
    for(unsigned i=0;i<writeCount && pos+4<sizeof(preview);++i)pos+=snprintf(preview+pos,sizeof(preview)-pos,"%02X ",writeBytes[i]);
    auto *body=lv_obj_create(modal);flat(body,Bg);lv_obj_set_pos(body,8,52);lv_obj_set_size(body,304,280);lv_obj_set_scroll_dir(body,LV_DIR_VER);text(body,4,4,288,preview,&lv_font_montserrat_12);
    text(modal,12,340,296,"Writes can change charging/protection settings. Shared-bus idle checks cannot prevent every collision.",&lv_font_montserrat_12);
    button(modal,8,420,148,"Cancel",extraClose,0);auto *send=button(modal,164,420,148,"SEND WRITE",writeCommit,0);lv_obj_set_style_bg_color(send,lv_color_hex(0x8f3038),0);
}
void cancelExtra(lv_event_t *){
    graphStartPending=false;
    if(burstOwner==BurstOwner::Graph)burstStop();
    extraWork=Work::Idle;graphActive=graphReading=false;graphStopped=true;extraSessionBusy=false;
    if(extraPending){uint8_t command=14,wire[32];size_t n=bringup::command(++control.nextId,&command,1,wire,sizeof(wire));if(uart.availableForWrite()>=int(n))uart.write(wire,n);}
    else if(activePending)activeAction(nullptr);
}
void graphToggle(lv_event_t *){
    if(!graphStopped){cancelExtra(nullptr);return;}
    if(activeBusy() || control.busy() || rateRequest)return;
    if(!plotCount){graphManual(nullptr);return;}
    if(graphConfiguredActive && (!connected() || !sessionReady || status[2] || control.uncertain))return;
    if(!connected() || !sessionReady || control.uncertain)return;
    graphStopped=false;graphActive=graphMode!=GraphMode::Passive;graphDue=millis();
    // START owns an explicit acquisition request, not merely chart animation.
    // Re-enable both source delivery and capture through acknowledged controls.
    graphStartPending=graphMode!=GraphMode::Active;
    if(graphStartPending){sourceFilter(selection);control.begin(observer::Action::Enable);}
}
void graphHistoryClear(lv_event_t *){
    for(unsigned i=0;i<plotCount;++i)if(plots[i].series)lv_chart_set_all_value(graphChart,plots[i].series,LV_CHART_POINT_NONE);
    graphPaint=millis();
}
void graphAcquisitionChanged(lv_event_t *){
    if(activeBusy() || control.busy() || rateRequest)return;
    graphMode=GraphMode((unsigned(graphMode)+1)%3);graphConfiguredActive=graphMode!=GraphMode::Passive;
    graphActive=!graphStopped && graphConfiguredActive;graphDue=millis();
    if(!graphStopped && graphMode!=GraphMode::Active && connected() && sessionReady){sourceFilter(selection);graphStartPending=true;control.begin(observer::Action::Enable);}
}
void graphIntervalChanged(lv_event_t *){graphRefresh_ms=(lv_dropdown_get_selected(graphInterval)+1)*1000;graphDue=millis()+graphRefresh_ms;}
void graphBurstDone(bool safe){
    graphReading=false;extraSessionBusy=false;graphDue=millis()+graphRefresh_ms;
    if(!safe){graphActive=false;graphStopped=true;snprintf(extraMessage,sizeof(extraMessage),"Graph stopped / unsafe read or link uncertainty");}
}
void scanStart(lv_event_t *){
    if(activeBusy() || extraWork!=Work::Idle)return;
    if(!connected() || !sessionReady || !(val(132)&64)){snprintf(extraMessage,sizeof(extraMessage),"Connect RP V10 before scanning");return;}
    graphActive=false;for(unsigned a=0;a<128;++a)scanFound[a]=registry.bindings[a].confirmed;
    scanSkipped=0;scanDirty=true;scanNext=8;extraWork=Work::Scan;extraSessionBusy=true;
    snprintf(extraMessage,sizeof(extraMessage),"Scanning 0x08-0x77 / read probes");
}
void identifyStart(lv_event_t *){
    unsigned address;if(activeBusy() || extraWork!=Work::Idle || !hexNumber(lv_textarea_get_text(scanAddress),119,address)||address<8)return;
    if(!connected() || !sessionReady || !(val(132)&64)){snprintf(extraMessage,sizeof(extraMessage),"Connect RP V10 before identification");return;}
    extraAddress=uint8_t(address);identifyAt=0;identifyCount=0;memset(identifyValid,0,sizeof(identifyValid));
    auto append=[](uint8_t reg){for(unsigned i=0;i<identifyCount;++i)if(identifyRegs[i]==reg)return;if(identifyCount<32)identifyRegs[identifyCount++]=reg;};
    for(const auto &r:experiment::rules)append(r.reg);
    for(uint8_t reg:{uint8_t(0x33),uint8_t(0x52),uint8_t(0x53),uint8_t(0x70),uint8_t(0x71),uint8_t(0x74),uint8_t(0x75),uint8_t(0x76)})append(reg);
    graphActive=false;extraWork=Work::Identify;extraSessionBusy=true;
}
void identificationResult(){
    unsigned pos=0,positiveCount=0,positiveIndex=0,profileIndex=0;
    auto append=[&](const char *format,...){
        unsigned remaining=sizeof(identifyOutput)-pos;
        if(remaining<=1)return;
        va_list args;va_start(args,format);
        int count=vsnprintf(identifyOutput+pos,remaining,format,args);
        va_end(args);
        if(count>0)pos+=unsigned(count)<remaining?unsigned(count):remaining-1;
    };
    append("READ-ONLY EVIDENCE / NOT A CHIP-ID\nA exclusive / B mandatory / C shared\nFailed reads are inconclusive.\n\n");
    for(const auto &profile:decoding::profiles){
        identification::Evidence evidence;
        append("%s\n",profile.name);
        for(const auto &r:experiment::rules)if(!strcmp(r.profile,profile.id)){
            auto outcome=identification::evaluate(r.kind,identifyValid[r.reg],identifyBytes[r.reg],r.mask,r.min,r.max);
            evidence.add(r.kind,outcome);
            append("  %c %02X/%02X: %s\n    %s\n",'A'+unsigned(r.kind),r.reg,unsigned(r.mask),identification::name(outcome),r.label);
        }
        bool wide=!strcmp(profile.id,"ip2368");
        auto power=identification::Outcome::Inconclusive;
        if(identifyValid[0x52] && identifyValid[0x53] && identifyValid[0x70] && identifyValid[0x71] && identifyValid[0x74] && identifyValid[0x75] && (!wide || identifyValid[0x76])){
            double voltage_mV=identifyBytes[0x52]+256.0*identifyBytes[0x53];
            double current_mA=identifyBytes[0x70]+256.0*identifyBytes[0x71];
            double expected_mW=voltage_mV*current_mA/1000;
            double raw=identifyBytes[0x74]+256.0*identifyBytes[0x75]+(wide?65536.0*identifyBytes[0x76]:0);
            double measured_mW=raw*(wide?1:10);
            if(expected_mW>=500)power=fabs(measured_mW-expected_mW)<=expected_mW*0.3?identification::Outcome::Match:identification::Outcome::NoMatch;
        }
        evidence.add(identification::Class::C,power);
        append("  C power (%s): %s\n",wide?"24-bit / 1 mW":"16-bit / 10 mW",power==identification::Outcome::Inconclusive?"missing bytes or load below 500 mW":identification::name(power));
        auto voltage=identification::Outcome::Inconclusive;
        if(identifyValid[0x33] && (identifyBytes[0x33]&0xc0)==0x80 && identifyValid[0x52] && identifyValid[0x53]){
            unsigned code=identifyBytes[0x33]&7;
            constexpr unsigned voltage_mV[]={5000,7000,9000,12000,15000,20000,28000};
            unsigned start=wide?2:1,limit=wide || !strcmp(profile.id,"ip2366")?7:6;
            if(code>=start && code<=limit){
                double actual_mV=identifyBytes[0x52]+256.0*identifyBytes[0x53];
                unsigned expected_mV=voltage_mV[code-start];
                voltage=fabs(actual_mV-expected_mV)<=expected_mV*0.1?identification::Outcome::Match:identification::Outcome::NoMatch;
            }
        }
        evidence.add(identification::Class::C,voltage);
        append("  C powered voltage code: %s\n",voltage==identification::Outcome::Inconclusive?"prerequisites missing / undefined code":identification::name(voltage));
        append("  %s\n\n",evidence.result());
        if(evidence.exclusive && !evidence.excluded){++positiveCount;positiveIndex=profileIndex;}
        ++profileIndex;
    }
    append("Raw observations:\n");
    for(unsigned i=0;i<identifyCount;++i){unsigned reg=identifyRegs[i];if(identifyValid[reg])append("%02X: %02X\n",reg,identifyBytes[reg]);else append("%02X: unread/failed\n",reg);}
    if(scanList)lv_label_set_text(scanList,identifyOutput);
    snprintf(extraMessage,sizeof(extraMessage),"Identification complete / shared matches do not identify");
    if(identifyChoice && positiveCount==1)lv_dropdown_set_selected(identifyChoice,positiveIndex+2);
    scanDirty=false;
}
void identifyDismiss(lv_event_t *){
    if(extraWork==Work::Identify || extraPending)cancelExtra(nullptr);
    scanList=identifyChoice=nullptr;scanDirty=true;extraClose(nullptr);
}
void identifyAccept(lv_event_t *){
    unsigned selected=identifyChoice?lv_dropdown_get_selected(identifyChoice):0;
    if(selected && !assignAddressProfile(extraAddress,selected==1?-1:int(selected)-2)){
        if(scanList)lv_label_set_text(scanList,"Active profile capacity reached. Disable another address first.");
        return;
    }
    identifyDismiss(nullptr);
}
void scanTilePressed(lv_event_t *e){
    if(modal || activeBusy())return;
    unsigned address=unsigned(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    char addr[8];snprintf(addr,sizeof(addr),"%02X",address);lv_textarea_set_text(scanAddress,addr);
    identifyStart(nullptr);if(extraWork!=Work::Identify)return;
    modal=lv_obj_create(lv_screen_active());flat(modal,Bg);lv_obj_set_size(modal,W,H);
    char title[48];snprintf(title,sizeof(title),"IDENTIFY / 0x%02X",address);
    text(modal,12,12,248,title,&lv_font_montserrat_18);
    button(modal,266,4,46,LV_SYMBOL_CLOSE,identifyDismiss,0);
    auto *body=lv_obj_create(modal);flat(body,Bg);lv_obj_set_pos(body,8,56);lv_obj_set_size(body,304,308);lv_obj_set_scroll_dir(body,LV_DIR_VER);
    scanList=text(body,4,4,288,"Reading identification evidence...",&lv_font_montserrat_12);
    identifyChoice=lv_dropdown_create(modal);lv_obj_set_pos(identifyChoice,8,376);lv_obj_set_size(identifyChoice,304,40);
    profileOptions(identifyChoice,uint8_t(address));extraDropdownStyle(identifyChoice);
    auto *done=button(modal,8,428,304,"Done",identifyAccept,0);ui_update::bgColor(done,lv_color_hex(0x238557),0);
}
bool extraPacket(const bringup::Packet &p){
    if(graphRequest && (p.type==6 || p.type==2) && p.length>=6 && bringup::le32(p.payload)==graphRequest){
        if(p.type==6 || p.payload[5]){graphResult=true;graphSafe=p.type==6 && p.length==32 && p.payload[27]==2 && p.payload[6] && (p.payload[4]==0 || p.payload[4]==2 || p.payload[4]==3);}
        return false;
    }
    if(!extraPending || (p.type!=6 && p.type!=2 && p.type!=8) || p.length<6 || bringup::le32(p.payload)!=extraPending)return false;
    if(p.type==2 && p.payload[4]==extraOp && !p.payload[5])return true;
    if(extraOp==21){
        if(p.type==2){extraPending=0;extraWork=Work::Idle;extraSessionBusy=false;snprintf(extraMessage,sizeof(extraMessage),"Read rejected / RP V12 required / code %u",p.payload[5]);return true;}
        bool valid=p.type==8 && p.length>=32 && p.payload[27]==5 && p.payload[28]==3 && p.payload[29]==extraAddress && p.payload[7]==extraRegister &&
            p.payload[4]<=10 && p.payload[5]<=1 && p.payload[6]<=1 && bringup::le16(p.payload+30)<=readBytes && p.length==32+bringup::le16(p.payload+30);
        if(!valid){extraPending=0;extraWork=Work::Idle;extraSessionBusy=false;sessionReady=false;needsSync=true;snprintf(extraMessage,sizeof(extraMessage),"Invalid read result / state uncertain / no retry");return true;}
        auto &r=readValues[readAt];r.reg=extraRegister;r.count=bringup::le16(p.payload+30);r.code=p.payload[4];r.restored=p.payload[6];r.complete=true;
        r.ok=!r.code && p.payload[5]==1 && r.restored && r.count==readBytes;memcpy(r.bytes,p.payload+32,r.count);
        extraPending=0;++readAt;lv_dropdown_set_selected(readResults,readAt-1);
        if(r.ok)registry.observe(extraAddress,selection.display!=analysis::Display::Raw);
        if(!r.restored){sessionReady=false;needsSync=true;}
        bool continueBatch=r.ok || ((r.code==2 || r.code==3) && r.restored);
        if(readCancel || !continueBatch || readAt==readPlanCount){extraWork=Work::Idle;extraSessionBusy=false;}
        snprintf(extraMessage,sizeof(extraMessage),"%s %u/%u / %s / capture %s",readCancel?"Cancelled":extraWork==Work::Read?"Reading":"Done",readAt,readPlanCount,r.ok?"OK":"FAIL",r.restored?"restored":"UNCERTAIN");
        readRender();return true;
    }
    if(extraOp==19){
        bool valid=p.type==6 && p.length==32 && p.payload[27]==4 && p.payload[28]==2 &&
            p.payload[29]==scanNext && p.payload[31]==scanEnd && p.payload[30]<=1 && p.payload[4]<=10 &&
            p.payload[5]<=1 && p.payload[6]<=1;
        if(!valid){extraPending=0;extraWork=Work::Idle;extraSessionBusy=false;sessionReady=false;needsSync=true;
            snprintf(extraMessage,sizeof(extraMessage),"Rejected scan result / state uncertain");return true;}
        const bool final=p.payload[30],ok=p.payload[4]==0 && p.payload[5]==1;
        if(ok){scanFound[scanNext]=true;registry.observe(uint8_t(scanNext),selection.display!=analysis::Display::Raw);}
        scanDirty=true;extraAt=millis();
        if(!final){++scanNext;return true;}
        bool complete=p.payload[4]==0 || p.payload[4]==2;
        if(complete)++scanNext;else scanSkipped+=120-scanNext;
        extraPending=0;
        if(!complete || !p.payload[6] || extraWork!=Work::Scan){extraWork=Work::Idle;extraSessionBusy=false;}
        snprintf(extraMessage,sizeof(extraMessage),"Scan %s / capture %s",complete?"range complete":"stopped",p.payload[6]?"restored":"UNCERTAIN");
        if(!p.payload[6]){sessionReady=false;needsSync=true;}
        return true;
    }
    bool valid=p.type==6 && p.length==32 && p.payload[27]==3 && p.payload[29]==extraAddress && p.payload[4]<=10 && p.payload[5]<=1 && p.payload[6]<=1;
    valid=valid && p.payload[28]==(extraOp==16?1:extraOp==17?2:0) && (extraOp!=18 || p.payload[7]==extraRegister) && (extraOp!=16 || bringup::le16(p.payload+30)==extraWriteCount);
    bool ok=valid && p.payload[4]==0 && p.payload[5]==1 && p.payload[6];
    if(ok && extraOp==17)registry.observe(extraAddress,selection.display!=analysis::Display::Raw);
    bool nack=valid && (p.payload[4]==2 || p.payload[4]==3) && p.payload[6];
    extraPending=0;
    if(!valid){sessionReady=false;needsSync=true;extraWork=Work::Idle;extraSessionBusy=false;snprintf(extraMessage,sizeof(extraMessage),"Rejected/mismatched result / no retry");return true;}
    snprintf(extraMessage,sizeof(extraMessage),"0x%02X / %s / code %u / capture %s",extraAddress,ok?"ACK / OK":"FAIL",p.payload[4],p.payload[6]?"restored":"UNCERTAIN");
    if(extraWork==Work::Scan){
        if(ok)scanFound[extraAddress]=true;
        scanDirty=true;
        // BusBusy is a safe admission timeout, not an attempted transfer.
        if(ok || nack || (p.payload[4]==1 && p.payload[6])){if(p.payload[4]==1)++scanSkipped;++scanNext;}
        else extraWork=Work::Idle;
    }
    if(extraWork==Work::Identify){unsigned reg=identifyRegs[identifyAt];identifyValid[reg]=ok;identifyBytes[reg]=p.payload[24];if(!ok && !nack)extraWork=Work::Idle;else ++identifyAt;}
    if(extraWork==Work::Idle)extraSessionBusy=false;
    if(!p.payload[6]){sessionReady=false;needsSync=true;}
    return true;
}
void graphClear(){
    if(graphChart)for(auto &p:plots)if(p.series){lv_chart_remove_series(graphChart,p.series);p.series=nullptr;}
    plotCount=0;graphReading=false;if(extraWork==Work::Idle)extraSessionBusy=false;
}
bool graphSourceFilter(uint8_t *command){
    for(unsigned i=0;i<plotCount;++i){const auto &p=decoding::properties[plots[i].property];
        command[4+p.address/8]|=uint8_t(1u<<(p.address%8));
        for(unsigned n=0;n<p.count;++n){unsigned reg=p.low+n;command[20+reg/8]|=uint8_t(1u<<(reg%8));}}
    return plotCount!=0;
}
void graphAdd(unsigned property,double min,double max,uint32_t color){
    if(plotCount==8)return;const auto &p=decoding::properties[property];if(p.ascii)return;
    if(!isfinite(min) || !isfinite(max) || min>=max)return;
    auto &line=plots[plotCount++];line=Plot{};line.property=property;line.min=min;line.max=max;line.color=color;
    line.series=lv_chart_add_series(graphChart,lv_color_hex(color),LV_CHART_AXIS_PRIMARY_Y);
}
void graphApply(lv_event_t *){
    unsigned selected=0;for(unsigned i=0;i<decoding::PropertyCount;++i)if(graphChecks[i] && lv_obj_has_state(graphChecks[i],LV_STATE_CHECKED))++selected;
    if(selected>8){ui_update::text(graphError,"Select at most eight properties");return;}
    Plot previous[8];memcpy(previous,plots,sizeof(previous));unsigned previousCount=plotCount;
    graphClear();graphStopped=true;graphActive=false;
    constexpr uint32_t colors[]={Cyan,Amber,Green,Red,0xaa9be7,0x77b6ff,0xff93cd,0xbcd071};
    for(unsigned i=0;i<decoding::PropertyCount;++i)if(graphChecks[i] && lv_obj_has_state(graphChecks[i],LV_STATE_CHECKED)){
        const auto &p=decoding::properties[i];double min=p.minimum*p.scale+p.offset,max=p.maximum*p.scale+p.offset;uint32_t color=colors[plotCount%8];
        for(unsigned n=0;n<previousCount;++n)if(previous[n].property==i){min=previous[n].min;max=previous[n].max;color=previous[n].color;break;}
        if(min>max){double swap=min;min=max;max=swap;}if(min==max)max=min+1;
        graphAdd(i,min,max,color);}
    graphGeneration=registry.generation;graphDue=millis();extraClose(nullptr);graphToggle(nullptr);
}
void graphManual(lv_event_t *){
    if(modal || activeBusy())return;memset(graphChecks,0,sizeof(graphChecks));
    modal=lv_obj_create(lv_screen_active());flat(modal,Bg);lv_obj_set_size(modal,W,H);
    text(modal,8,10,300,"GRAPH PROPERTIES / MAX 8",&lv_font_montserrat_16);
    auto *body=lv_obj_create(modal);flat(body,Bg);lv_obj_set_pos(body,0,40);lv_obj_set_size(body,W,332);lv_obj_set_scroll_dir(body,LV_DIR_VER);
    graphError=text(modal,8,384,304,"",&lv_font_montserrat_12);lv_obj_set_style_text_color(graphError,lv_color_hex(Red),0);
    int y=4;for(unsigned i=0;i<decoding::PropertyCount;++i){const auto &p=decoding::properties[i];if(!p.count || p.ascii || !reporting::readable(p))continue;
        auto *cb=lv_checkbox_create(body);char label[160];snprintf(label,sizeof(label),"%02X:%02X %s",p.address,p.low,p.name);lv_checkbox_set_text(cb,label);lv_obj_set_pos(cb,8,y);lv_obj_set_width(cb,298);lv_obj_set_style_text_font(cb,&lv_font_montserrat_10,0);lv_obj_set_style_text_color(cb,lv_color_hex(Ink),0);y+=28;graphChecks[i]=cb;
        for(unsigned n=0;n<plotCount;++n)if(plots[n].property==i)lv_obj_add_state(cb,LV_STATE_CHECKED);
    }
    button(modal,8,422,148,"Cancel",extraClose,0);auto *apply=button(modal,164,422,148,"Apply",graphApply,0);ui_update::bgColor(apply,lv_color_hex(0x238557),0);
}
void graphSelect(lv_event_t *){
    unsigned selected=lv_dropdown_get_selected(graphChoice);if(!selected){graphManual(nullptr);return;}
    if(selected>graphOptionsCount || activeBusy())return;
    auto option=graphOptions[selected-1];const auto &g=experiment::graphs[option.graph];graphClear();graphStopped=true;graphActive=false;
    for(unsigned n=0;n<g.count;++n){reporting::Reference ref;if(!reporting::parse(g.lines[n].reference,ref))continue;
        for(unsigned p=0;p<decoding::PropertyCount;++p){const auto &d=decoding::properties[p];if(d.count && d.address==option.address && d.low==ref.reg && d.mask==ref.mask)graphAdd(p,g.lines[n].min,g.lines[n].max,g.lines[n].color);}}
    graphGeneration=registry.generation;graphDue=millis();graphToggle(nullptr);
}
void graphOptionsRefresh(){
    unsigned old=lv_dropdown_get_selected(graphChoice);GraphOption previous={0,0};
    bool preserve=old && old<=graphOptionsCount;if(preserve)previous=graphOptions[old-1];
    lv_dropdown_set_options(graphChoice,"Manual");graphOptionsCount=0;
    for(unsigned a=8;a<120;++a)if(registry.reportEligible(uint8_t(a))){const auto &p=decoding::profiles[registry.bindings[a].profile];
        for(unsigned i=0;i<experiment::GraphCount && graphOptionsCount<128;++i)if(!strcmp(p.id,experiment::graphs[i].profile)){
            char label[192];snprintf(label,sizeof(label),"%02X / %s",a,experiment::graphs[i].title);lv_dropdown_add_option(graphChoice,label,LV_DROPDOWN_POS_LAST);graphOptions[graphOptionsCount++]={i,uint8_t(a)};}}
    if(preserve)for(unsigned i=0;i<graphOptionsCount;++i)if(graphOptions[i].address==previous.address && graphOptions[i].graph==previous.graph)lv_dropdown_set_selected(graphChoice,i+1);
    graphEvidenceGeneration=registry.evidenceGeneration;
}
void extraTick(){
    if(!writeStatus)return;
    if(extraWork==Work::Read && (!connected() || !sessionReady || control.uncertain)){
        extraWork=Work::Idle;extraSessionBusy=false;snprintf(extraMessage,sizeof(extraMessage),"Read stopped / link or capture state uncertain / no retry");
    }
    if(extraPending && uint32_t(millis()-extraAt)>15000){extraPending=0;extraWork=Work::Idle;extraSessionBusy=false;sessionReady=false;needsSync=true;snprintf(extraMessage,sizeof(extraMessage),"Timeout / state uncertain / no retry");}
    if(!extraPending && extraWork!=Work::Idle && connected() && sessionReady && !activePending && !reportBatch.running && !control.busy() && !rateRequest){
        if(extraWork==Work::Scan){
            while(scanNext<=119 && registry.bindings[scanNext].confirmed)++scanNext;
            if(scanNext<=119){
                scanEnd=scanNext;
                while(scanEnd<119 && !registry.bindings[scanEnd+1].confirmed)++scanEnd;
                rawRequest(val(132)&128?19:17,uint8_t(scanNext),nullptr,0);
            }else{extraWork=Work::Idle;extraSessionBusy=false;snprintf(extraMessage,sizeof(extraMessage),"Scan complete / confirmed addresses skipped");}
        }
        else if(extraWork==Work::Read){if(!connected() || !sessionReady){extraWork=Work::Idle;extraSessionBusy=false;}else if(readAt<readPlanCount)rawRequest(21,readTarget,readPlan+readAt,readBytes);}
        else if(identifyAt<identifyCount)rawRequest(18,extraAddress,identifyRegs+identifyAt,1);else{extraWork=Work::Idle;extraSessionBusy=false;identificationResult();}
    }
    if(graphGeneration!=registry.generation){graphClear();graphOptionsRefresh();graphGeneration=registry.generation;}
    else if(graphEvidenceGeneration!=registry.evidenceGeneration)graphOptionsRefresh();
    if(graphRequest && !activePending){
        graphRequest=0;if(!graphResult || !graphSafe || !sessionReady){graphReading=graphActive=false;graphStopped=true;extraSessionBusy=false;snprintf(extraMessage,sizeof(extraMessage),"Active graph stopped after unsafe/uncertain result");}
        else ++graphReadAt;
    }
    if(graphStartPending && !control.busy()){
        graphStartPending=false;if(control.uncertain){graphStopped=true;graphActive=false;}
    }
    if(plotCount && !graphStopped && graphActive && !graphReading && !graphStartPending && extraWork==Work::Idle && !activeBusy() && !modal && connected() && sessionReady && int32_t(millis()-graphDue)>=0){
        if(val(132)&256){
            unsigned properties[8],count=0;
            for(unsigned i=0;i<plotCount;++i){const auto &r=reportCache.readings[plots[i].property];
                // Only successful passive observations count as freshness. Our
                // active replies must never suppress the next hybrid refresh.
                bool fresh=r.passiveValid && !r.failed && uint32_t(millis()-r.passiveAt_ms)<graphRefresh_ms;
                if(graphMode==GraphMode::Active || !fresh)properties[count++]=plots[i].property;
            }
            if(!count)graphDue=millis()+graphRefresh_ms;
            else if(burstStart(BurstOwner::Graph,properties,count)){graphReading=true;extraSessionBusy=true;}
        }else{graphReading=true;graphReadAt=0;extraSessionBusy=true;}
    }
    if(graphReading && burstOwner!=BurstOwner::Graph && !activePending && !extraPending && !reportBatch.running && !control.busy() && !rateRequest){
        if(graphReadAt>=plotCount){graphReading=false;extraSessionBusy=false;graphDue=millis()+(graphRefresh_ms?graphRefresh_ms:100);}
        else if(graphMode==GraphMode::Hybrid && reportCache.readings[plots[graphReadAt].property].passiveValid &&
            !reportCache.readings[plots[graphReadAt].property].failed && uint32_t(millis()-reportCache.readings[plots[graphReadAt].property].passiveAt_ms)<graphRefresh_ms)++graphReadAt;
        else{activeProperty=plots[graphReadAt].property;if(!activeEligible(activeProperty)){graphReading=graphActive=false;graphStopped=true;extraSessionBusy=false;snprintf(extraMessage,sizeof(extraMessage),"Graph property has no active-read plan");}
            else if(!modal){graphDispatch=true;activeAction(nullptr);graphDispatch=false;graphRequest=activePending;graphResult=graphSafe=false;if(!activePending){graphReading=false;extraSessionBusy=false;}}}
    }
    if(!graphStopped && !graphStartPending && uint32_t(millis()-graphPaint)>=100){graphPaint=millis();bool newValue=false;
        for(unsigned i=0;i<plotCount;++i)newValue|=reportCache.readings[plots[i].property].revision!=plots[i].last;
        // Shared 10 Hz presentation grid; held values are not additional reads.
        // Freeze passive history when capture is paused, except explicit reads.
        if(!status[0] && !graphActive && !newValue)return;
        for(unsigned i=0;i<plotCount;++i){auto &line=plots[i];const auto &r=reportCache.readings[line.property];const auto &p=decoding::properties[line.property];line.last=r.revision;
        if(!r.valid || r.failed){lv_chart_set_next_value(graphChart,line.series,LV_CHART_POINT_NONE);continue;}double value=((r.lastGood.word&p.mask)>>p.shift)*p.scale+p.offset;
            double span=line.max-line.min;if(span<=0)span=1;int normalized=int((value-line.min)*1000/span);if(normalized<0)normalized=0;if(normalized>1000)normalized=1000;
            lv_chart_set_next_value(graphChart,line.series,normalized);}}
}
void refreshExtras(){
    ui_update::text(writeStatus,extraMessage);
    bool reading=extraWork==Work::Read || (extraPending && extraOp==21);
    ui_update::text(lv_obj_get_child(readButton,0),reading?"CANCEL":"READ");ui_update::bgColor(readButton,lv_color_hex(reading?0x9c3544:0x238557),0);ui_update::text(readInfo,extraMessage);
    for(auto *o:{readAddress,readRegisters,readLength}){if(reading)lv_obj_add_state(o,LV_STATE_DISABLED);else lv_obj_remove_state(o,LV_STATE_DISABLED);}
    if(scanEvidenceGeneration!=registry.evidenceGeneration){
        scanEvidenceGeneration=registry.evidenceGeneration;scanDirty=true;
        for(unsigned a=8;a<120;++a)scanFound[a]=registry.bindings[a].confirmed;
    }
    unsigned found=0;for(unsigned a=8;a<120;++a)found+=scanFound[a];
    bool scanning=extraWork==Work::Scan;
    ui_update::text(lv_obj_get_child(scanButton,0),scanning?"CANCEL":"Scan I2C");
    lv_obj_set_style_bg_color(scanButton,lv_color_hex(scanning?0x9c3544:connected() && sessionReady?0x238557:0x174b35),0);
    lv_obj_set_style_bg_color(scanButton,lv_color_hex(0x174b35),LV_STATE_DISABLED);
    lv_obj_set_style_color_filter_opa(scanButton,LV_OPA_TRANSP,LV_STATE_DISABLED);
    char summary[96];if(scanning)snprintf(summary,sizeof(summary),"Scanning %02X / found %u",scanNext,found);
    else if(found || scanNext>119 || scanSkipped)snprintf(summary,sizeof(summary),"Found %u / untested %u",found,scanSkipped);
    else snprintf(summary,sizeof(summary),"%s",extraPending?"Cancelling":"Standby");
    ui_update::text(scanStatus,summary);
    if(scanBindingGeneration!=registry.generation){scanDirty=true;scanBindingGeneration=registry.generation;}
    if(scanDirty){scanDirty=false;unsigned index=0;
        for(unsigned a=8;a<120;++a){
            if(!scanFound[a]){if(scanTiles[a]){lv_obj_delete(scanTiles[a]);scanTiles[a]=scanNames[a]=nullptr;}continue;}
            if(!scanTiles[a]){
                auto *tile=lv_button_create(scanBody);scanTiles[a]=tile;lv_obj_set_size(tile,148,84);
                lv_obj_set_style_radius(tile,6,0);lv_obj_set_style_bg_color(tile,lv_color_hex(0x25343c),0);
                lv_obj_set_style_pad_all(tile,8,0);lv_obj_remove_flag(tile,LV_OBJ_FLAG_SCROLLABLE);
                char addr[16];snprintf(addr,sizeof(addr),"0x%02X",a);text(tile,0,0,132,addr,&lv_font_montserrat_24);
                scanNames[a]=text(tile,0,38,132,"unidentified",&lv_font_montserrat_12);
                lv_obj_set_style_text_color(scanNames[a],lv_color_hex(Muted),0);
                lv_obj_add_event_cb(tile,scanTilePressed,LV_EVENT_CLICKED,reinterpret_cast<void *>(uintptr_t(a)));
            }
            lv_obj_set_pos(scanTiles[a],8+(index%2)*156,8+(index/2)*92);++index;
            const auto &binding=registry.bindings[a];
            if(binding.profile<0)ui_update::text(scanNames[a],binding.mode==decoding::Assignment::Disabled?"No parsing":"unidentified");
            else {const char *name=decoding::profiles[binding.profile].name;ui_update::textFmt(scanNames[a],"%.*s",int(strcspn(name," ")),name);}
        }
    }
    if(screenIndex!=4)return;
    ui_update::text(lv_obj_get_child(graphModeButton,0),graphModeName());
    if(graphMode==GraphMode::Passive)lv_obj_add_state(graphInterval,LV_STATE_DISABLED);else lv_obj_remove_state(graphInterval,LV_STATE_DISABLED);
    ui_update::text(lv_obj_get_child(graphButton,0),graphStopped?"START":"STOP");
    ui_update::bgColor(graphButton,lv_color_hex(graphStopped?0x238557:0x9c3544),0);
    if(graphStopped){ui_update::textFmt(graphInfo,"PAUSED / %s",graphModeName());return;}
    char legend[1000];unsigned pos=snprintf(legend,sizeof(legend),"%s / 10 Hz history / normalized 0-100%%\n",graphModeName());
    for(unsigned i=0;i<plotCount;++i){auto &line=plots[i];const auto &r=reportCache.readings[line.property];const auto &p=decoding::properties[line.property];
        char value[192]="--";if(r.valid)analysis::formatValue(r.lastGood,value,sizeof(value));pos+=snprintf(legend+pos,sizeof(legend)-pos,"%.26s: %.20s\n",p.name,value);
    }
    ui_update::text(graphInfo,legend);
}
void createExtras(){
    createManualRead();
    const char *titles[]={"I2C WRITE","START","I2C GRAPH"};
    const unsigned pages[]={6,0,3};
    for(unsigned i=0;i<3;++i){extraScreens[i]=lv_obj_create(lv_screen_active());flat(extraScreens[i],Bg);lv_obj_set_size(extraScreens[i],W,H);lv_obj_set_pos(extraScreens[i],int(pages[i])*W,0);lv_obj_remove_flag(extraScreens[i],LV_OBJ_FLAG_SCROLLABLE);text(extraScreens[i],8,8,i==1?132:304,titles[i],&lv_font_montserrat_18);}
    auto *s=extraScreens[0];text(s,8,38,140,"Address (hex)",&lv_font_montserrat_10);writeAddress=extraInput(s,8,52,96,"75");
    writePrefixKind=lv_dropdown_create(s);lv_obj_set_pos(writePrefixKind,112,52);lv_obj_set_size(writePrefixKind,200,36);lv_dropdown_set_options(writePrefixKind,"No register prefix\n8-bit register\n16-bit register LE\n16-bit register BE");
    extraDropdownStyle(writePrefixKind);
    writePrefix=extraInput(s,8,94,96,"");writeFormat=lv_dropdown_create(s);lv_obj_set_pos(writeFormat,112,94);lv_obj_set_size(writeFormat,200,36);
    lv_dropdown_set_options(writeFormat,"Hex bytes\nDecimal bytes\nBinary bytes\nASCII / UTF-8 bytes\nUInt16 LE\nUInt16 BE\nUInt32 LE\nUInt32 BE\nFloat32 LE\nFloat32 BE\nInt16 LE\nInt16 BE\nInt32 LE\nInt32 BE");
    extraDropdownStyle(writeFormat);
    writePayload=lv_textarea_create(s);lv_obj_set_pos(writePayload,8,142);lv_obj_set_size(writePayload,304,190);lv_textarea_set_max_length(writePayload,2304);lv_obj_add_event_cb(writePayload,extraFocus,LV_EVENT_CLICKED,nullptr);
    auto *review=button(s,8,342,148,"Review write",writePrepare,0);ui_update::bgColor(review,lv_color_hex(0x238557),0);button(s,164,342,148,"Cancel job",cancelExtra,0);writeStatus=text(s,8,400,304,"",&lv_font_montserrat_12);
    s=extraScreens[1];
    // Read-only revision tile: not a button. Keep header/link/gear hit areas
    // separate so the version label cannot steal touches from either control.
    auto *version=inset(s,88,0,60,40);
    lv_obj_remove_flag(version,LV_OBJ_FLAG_CLICKABLE);
    auto *versionLabel=text(version,0,0,60,"V22",&lv_font_montserrat_12);
    lv_obj_set_style_text_align(versionLabel,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(versionLabel,lv_color_hex(Cyan),0);
    lv_obj_center(versionLabel);
    auto *link=button(s,156,0,108,LV_SYMBOL_USB " LINK",startLink,0);startLinkIcon=lv_obj_get_child(link,0);
    button(s,272,0,40,LV_SYMBOL_SETTINGS,startSettings,0);
    scanStatus=text(s,8,44,304,"Waiting for link",&lv_font_montserrat_12);lv_obj_set_height(scanStatus,28);lv_label_set_long_mode(scanStatus,LV_LABEL_LONG_DOT);
    startInfo=text(s,8,80,304,"",&lv_font_montserrat_10);ui_update::textColor(startInfo,lv_color_hex(Muted),0);
    scanBody=lv_obj_create(s);flat(scanBody,Bg);lv_obj_set_pos(scanBody,0,100);lv_obj_set_size(scanBody,W,304);lv_obj_set_scroll_dir(scanBody,LV_DIR_VER);
    // Keep the workspace invitation outside the scrolling address tiles.
    startHint=text(s,8,412,304,"Swipe left for I2C Tools",&lv_font_montserrat_12);
    ui_update::textColor(startHint,lv_color_hex(Muted),0);
    lv_obj_add_flag(startHint,LV_OBJ_FLAG_HIDDEN);
    scanButton=button(s,8,436,148,"Scan I2C",scanToggle,0);
    startSessionButton=button(s,164,436,148,"NEW SESSION",newSession,0);
    lv_obj_set_style_text_font(startSessionButton,&lv_font_montserrat_12,0);
    // Hidden target retains the shared identification dispatcher's validated address input.
    scanAddress=extraInput(s,0,0,96,"75");lv_obj_add_flag(scanAddress,LV_OBJ_FLAG_HIDDEN);
    s=extraScreens[2];graphChoice=lv_dropdown_create(s);lv_obj_set_pos(graphChoice,8,40);lv_obj_set_size(graphChoice,208,36);lv_obj_add_event_cb(graphChoice,graphSelect,LV_EVENT_VALUE_CHANGED,nullptr);button(s,224,40,88,"Setup",graphManual,0);
    extraDropdownStyle(graphChoice);
    graphChart=lv_chart_create(s);lv_obj_set_pos(graphChart,8,90);lv_obj_set_size(graphChart,304,220);lv_chart_set_type(graphChart,LV_CHART_TYPE_LINE);lv_chart_set_point_count(graphChart,128);lv_chart_set_range(graphChart,LV_CHART_AXIS_PRIMARY_Y,0,1000);lv_obj_set_style_bg_color(graphChart,lv_color_hex(0x1e2528),0);lv_obj_set_style_size(graphChart,0,0,LV_PART_INDICATOR);
    lv_obj_set_style_line_color(graphChart,lv_color_hex(0x3c454b),LV_PART_MAIN);
    lv_obj_set_style_border_color(graphChart,lv_color_hex(0x3c454b),LV_PART_MAIN);
    graphInfo=text(s,8,320,304,"",&lv_font_montserrat_10);graphOptionsRefresh();graphGeneration=registry.generation;
    graphModeButton=button(s,8,438,120,"Passive",graphAcquisitionChanged,0);lv_obj_set_style_text_font(graphModeButton,&lv_font_montserrat_10,0);
    graphInterval=lv_dropdown_create(s);lv_obj_set_pos(graphInterval,136,438);lv_obj_set_size(graphInterval,58,40);extraDropdownStyle(graphInterval);
    lv_obj_set_style_text_font(graphInterval,&lv_font_montserrat_10,0);lv_dropdown_clear_options(graphInterval);
    for(unsigned sec=1;sec<=30;++sec){char label[12];snprintf(label,sizeof(label),"%us",sec);lv_dropdown_add_option(graphInterval,label,LV_DROPDOWN_POS_LAST);}
    lv_obj_add_event_cb(graphInterval,graphIntervalChanged,LV_EVENT_VALUE_CHANGED,nullptr);
    auto *clear=button(s,200,438,36,LV_SYMBOL_TRASH,graphHistoryClear,0);
    lv_obj_set_style_pad_all(clear,0,0);
    lv_obj_set_style_bg_color(clear,lv_color_hex(0x3b4951),0);
    graphButton=button(s,242,438,70,"START",graphToggle,0);lv_obj_set_style_bg_color(graphButton,lv_color_hex(0x238557),0);
}
