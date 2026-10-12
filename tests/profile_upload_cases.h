{
    if(modal)closeDetail(nullptr);control.abort("upload test reset");control.uncertain=false;
    activePending=extraPending=0;reportBatch.running=extraSessionBusy=false;extraWork=Work::Idle;graphActive=graphReading=false;
    recorderPhase=RecorderPhase::Idle;recorder->reset();rateRequest=0;status[0]=0;seen=false;sessionReady=true;
    LittleFS.files.clear();profileDisk.begin();assert(profileDisk.ready);
    auto command=[](const std::string &s){Serial.output.clear();for(char c:s+"\n")Serial.input.push_back(uint8_t(c));profileUploadPump();};
    command("PRF1 aabb1122 HELLO");assert(Serial.output.find("TABLET V22 IPRF1 storage_ready")!=std::string::npos&&Serial.output.find("CHIP ip2366 2 00000000 builtin")!=std::string::npos);
    char path[2048];snprintf(path,sizeof(path),"%s/../../profiles/ip2366.i2cp",outputDirectory);auto *file=fopen(path,"rb");assert(file);
    fseek(file,0,SEEK_END);unsigned size=ftell(file);rewind(file);std::vector<uint8_t> binary(size);assert(fread(binary.data(),1,size,file)==size);fclose(file);
    uint32_t crc=portable::u32(binary.data()+12);char begin[128];snprintf(begin,sizeof(begin),"PRF1 aabb1122 BEGIN ip2366 %u %08X 0",size,crc);
    command(begin);assert(!profileUploadBusy&&Serial.output.find("replace_confirmation_required")!=std::string::npos);
    begin[strlen(begin)-1]='1';command(begin);assert(profileUploadBusy&&Serial.output.find("READY")!=std::string::npos);
    command("PRF1 99887766 DATA 0 01");assert(profileUploadBusy&&Serial.output.find("no_owned_upload")!=std::string::npos);
    command("PRF1 aabb1122 DATA 1 01");assert(!profileUploadBusy&&LittleFS.files.empty());
    command(begin);assert(profileUploadBusy);command("PRF1 aabb1122 ABORT");assert(!profileUploadBusy&&LittleFS.files.empty());
    command(begin);previewTime_ms+=20001;profileUploadPump();assert(!profileUploadBusy&&LittleFS.files.empty());
    // An active peer must acknowledge Pause before any upload data is accepted.
    seen=true;statusAt=millis();status[0]=1;sessionReady=true;delivering=true;needsSync=false;
    command(begin);assert(uploadWaiting&&profileUploadBusy&&control.action==observer::Action::Pause);
    for(unsigned n=0;n<8&&control.busy();++n){if(control.pendingId)acknowledge();else pump();}
    assert(!status[0]);profileUploadPump();assert(!uploadWaiting&&Serial.output.find("READY")!=std::string::npos);
    save("tablet-v20-profile-upload.ppm");
    for(unsigned at=0;at<size;){unsigned count=std::min(512u,size-at);std::string line="PRF1 aabb1122 DATA "+std::to_string(at)+" ";const char *hex="0123456789ABCDEF";
        for(unsigned i=0;i<count;++i){auto b=binary[at+i];line+=hex[b>>4];line+=hex[b&15];}command(line);at+=count;assert(uploadOffset==at&&Serial.output.find("ACK "+std::to_string(at))!=std::string::npos);}
    command("PRF1 aabb1122 COMMIT");assert(uploadRebootAt&&Serial.output.find("COMMITTED ip2366 2")!=std::string::npos);
    auto *stored=profileDisk.read("/ip2366.a");assert(stored&&!stored->error&&stored->crc==crc);delete stored;
    // Reboot publishes the complete package; the GUI consumes the runtime table.
    uploadClose();uploadRebootAt=0;decoder.clear();store->clear();registry.reset();profileDisk.load();
    assert(portable::installed[registry.find("ip2366")]&&registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual));
    command("PRF1 aabb1122 HELLO");assert(Serial.output.find("uploaded IP2366 v1.0")!=std::string::npos);
    seen=true;statusAt=millis();status[0]=1;view=View::Unique;selection=analysis::Selection{};groupedOnce=false;
    screenIndex=0;reportActive=false;slidePosition(nullptr,0);add(0x3a,49,0);refreshUi();
    assert(visibleCount==1&&groups[0].count==2);tapRow(0);assert(modal&&hasLabel(modal,decoding::properties[visibleDecoded[0]->property].friendlydesc));save("tablet-v20-uploaded-profile-detail.ppm");closeDetail(nullptr);
    registry.observe(0x75,true);updateReportOptions();bool pageFound=false;for(unsigned i=0;i<reportOptionCount;++i)if(reportOptions[i].address==0x75)pageFound=true;assert(pageFound);
    puts("PASS: actual USB management handler inventory, same-version overwrite guard, owned upload, bad offsets/abort/timeout, capture Pause ACK, complete chunk transfer, verified commit, boot publication and real LVGL uploaded-profile details/reports");
}
