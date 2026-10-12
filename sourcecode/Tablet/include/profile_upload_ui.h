// Included inside the UI namespace; USB management never shares the RP UART.
// PRF1 requests are token-scoped and uploads have explicit begin/chunk/commit
// acknowledgements. A timed-out commit is uncertain, not safe to blindly retry.
// Capture/session mutations are serialized; successful install reboots to expose
// the new immutable package without invalidating live UI pointers.
portable::Disk profileDisk;
uint8_t *uploadBytes=nullptr;
unsigned uploadSize=0,uploadOffset=0;
uint32_t uploadCrc=0,uploadAt=0,uploadRebootAt=0;
char uploadId[49]={},uploadToken[9]={},uploadLine[1200]={};
unsigned uploadLineSize=0;bool uploadLineDiscard=false,uploadWaiting=false;
lv_obj_t *uploadOverlay=nullptr,*uploadLabel=nullptr,*uploadBar=nullptr;
void profileReply(const char *token,const char *format,...){
    char text[256];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    Serial.printf("PRF1 %s %s\n",token,text);
}
void uploadClose(){
    free(uploadBytes);uploadBytes=nullptr;uploadSize=uploadOffset=0;uploadWaiting=false;profileUploadBusy=false;
    if(uploadOverlay)lv_obj_delete(uploadOverlay);uploadOverlay=uploadLabel=uploadBar=nullptr;
}
void uploadError(const char *why){profileReply(uploadToken,"ERROR %s",why);uploadClose();}
void uploadProgress(const char *text){
    if(!uploadOverlay){uploadOverlay=lv_obj_create(lv_layer_top());flat(uploadOverlay,0x141719);lv_obj_set_size(uploadOverlay,304,116);lv_obj_center(uploadOverlay);
        uploadLabel=lv_label_create(uploadOverlay);lv_obj_set_pos(uploadLabel,12,12);lv_obj_set_width(uploadLabel,280);
        lv_obj_set_style_text_color(uploadLabel,lv_color_hex(Cyan),0);
        uploadBar=lv_bar_create(uploadOverlay);lv_obj_set_pos(uploadBar,12,88);lv_obj_set_size(uploadBar,280,8);}
    lv_label_set_text(uploadLabel,text);lv_bar_set_value(uploadBar,uploadSize?100ULL*uploadOffset/uploadSize:0,LV_ANIM_OFF);
}
void profileCommand(char *line){
    char token[9],command[16];int offset=0;
    if(sscanf(line,"PRF1 %8s %15s %n",token,command,&offset)!=2||strlen(token)!=8)return;
    for(char *p=token;*p;++p)if(!isxdigit(*p))return;
    if(!strcmp(command,"HELLO")){
        profileReply(token,"TABLET V22 IPRF1 %s",profileDisk.ready?"storage_ready":"storage_unavailable");
        for(unsigned i=0;i<decoding::ProfileCount;++i){auto *p=portable::installed[i];const auto &v=decoding::profiles[i];
            profileReply(token,"CHIP %s %u %08lX %s %s",v.id,v.revision,(unsigned long)(p?p->crc:0),p?"uploaded":"builtin",v.name);}
        profileReply(token,"END");return;
    }
    if(!strcmp(command,"BEGIN")){
        char id[49],crcText[9],tail;unsigned long size;unsigned replace;
        if(sscanf(line+offset,"%48s %lu %8s %u %c",id,&size,crcText,&replace,&tail)!=4||!portable::validId(id)||strlen(crcText)!=8||replace>1||size<24||size>portable::MaxBytes){profileReply(token,"ERROR begin_arguments");return;}
        char *end;uint32_t crc=strtoul(crcText,&end,16);if(*end){profileReply(token,"ERROR crc_argument");return;}
        if(sessionMutation||profileUploadBusy||activeBusy()||control.busy()||rateRequest||graphActive||graphReading||extraWork!=Work::Idle||control.uncertain||recorder->recording){profileReply(token,"ERROR workflow_busy_stop_active_work_first");return;}
        if(!profileDisk.ready){profileReply(token,"ERROR %s",profileDisk.error?profileDisk.error:"storage_unavailable");return;}
        if(portable::find(id)>=0&&!replace){profileReply(token,"ERROR replace_confirmation_required");return;}
        if(connected()&&!sessionReady){profileReply(token,"ERROR link_not_synchronized");return;}
        uploadBytes=static_cast<uint8_t *>(portable::allocate(size));if(!uploadBytes){profileReply(token,"ERROR memory");return;}
        snprintf(uploadId,sizeof(uploadId),"%s",id);snprintf(uploadToken,sizeof(uploadToken),"%s",token);
        uploadSize=size;uploadOffset=0;uploadCrc=crc;uploadAt=millis();profileUploadBusy=true;
        uploadWaiting=connected()&&status[0];
        if(uploadWaiting&&!control.begin(observer::Action::Pause)){uploadError("pause_start");return;}
        uploadProgress(uploadWaiting?"PROFILE UPLOAD\nPausing capture":"PROFILE UPLOAD\nCapture paused / receiving");
        if(!uploadWaiting)profileReply(token,"READY");return;
    }
    if(!profileUploadBusy||strcmp(uploadToken,token)){profileReply(token,"ERROR no_owned_upload");return;}
    uploadAt=millis();
    if(!strcmp(command,"ABORT")){profileReply(token,"ABORTED");uploadClose();return;}
    if(uploadWaiting){profileReply(token,"ERROR pause_pending");return;}
    if(!strcmp(command,"DATA")){
        char *end;unsigned long at=strtoul(line+offset,&end,10);
        if(end==line+offset||*end!=' '||at!=uploadOffset){uploadError("chunk_offset");return;}
        const char *hex=end+1;unsigned n=strlen(hex);if(!n||n%2||n>1024||n/2>uploadSize-uploadOffset){uploadError("chunk_size");return;}
        auto digit=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;};
        for(unsigned i=0;i<n;i+=2){int a=digit(hex[i]),b=digit(hex[i+1]);if(a<0||b<0){uploadError("chunk_hex");return;}uploadBytes[uploadOffset+i/2]=uint8_t(a*16+b);}
        uploadOffset+=n/2;profileReply(token,"ACK %u",uploadOffset);
        static uint32_t shown=0;if(uint32_t(millis()-shown)>=100){shown=millis();char msg[100];snprintf(msg,sizeof(msg),"PROFILE UPLOAD\n%s / %u of %u bytes",uploadId,uploadOffset,uploadSize);uploadProgress(msg);}return;
    }
    if(!strcmp(command,"COMMIT")){
        if(uploadOffset!=uploadSize){uploadError("incomplete_upload");return;}
        if(connected()&&(status[0]||control.busy()||control.uncertain)){uploadError("capture_not_paused");return;}
        auto *p=new(std::nothrow)portable::Package();if(!p){uploadError("memory");return;}
        uint8_t *owned=uploadBytes;uploadBytes=nullptr;
        if(!p->parse(owned,uploadSize)){const char *why=p->error;profileReply(token,"ERROR %s",why);delete p;uploadClose();return;}
        const char *why=portable::capacity(*p);
        if(strcmp(p->profile.id,uploadId)||p->crc!=uploadCrc||why){profileReply(token,"ERROR %s",why?why:"identity_or_crc");delete p;uploadClose();return;}
        // Persist any already-made address choices before reboot, but do not
        // publish the new table over live cached values/LVGL string pointers.
        profileStorage.save(registry,!connected()||!status[0]);
        uploadProgress("PROFILE UPLOAD\nValidating flash / restarting");
        if(!profileDisk.commit(*p)){profileReply(token,"ERROR %s",profileDisk.error);delete p;uploadClose();return;}
        profileReply(token,"COMMITTED %s %u %08lX",p->profile.id,p->profile.revision,(unsigned long)p->crc);
        delete p;uploadRebootAt=millis()+500;return;
    }
    profileReply(token,"ERROR unknown_command");
}
void profileUploadPump(){
    if(uploadRebootAt){if(int32_t(millis()-uploadRebootAt)>=0)ESP.restart();return;}
    for(unsigned n=0;n<2048&&Serial.available();++n){int c=Serial.read();if(c=='\r')continue;
        if(c=='\n'){if(!uploadLineDiscard){uploadLine[uploadLineSize]=0;profileCommand(uploadLine);}uploadLineSize=0;uploadLineDiscard=false;}
        else if(!uploadLineDiscard){if(uploadLineSize+1>=sizeof(uploadLine)){uploadLineDiscard=true;uploadLineSize=0;}else uploadLine[uploadLineSize++]=char(c);}}
    if(profileUploadBusy&&uint32_t(millis()-uploadAt)>20000){uploadError("upload_timeout_previous_profile_retained");return;}
    if(uploadWaiting&&!control.busy()){
        if(control.uncertain||!connected()||!sessionReady||status[0]){uploadError("pause_not_verified");return;}
        uploadWaiting=false;uploadProgress("PROFILE UPLOAD\nCapture paused / receiving");profileReply(uploadToken,"READY");
    }
}
