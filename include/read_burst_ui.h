// Included inside UI. Both Reports and Graph use the same bounded wire batch.
// A retained witness is not a restored capture: only the terminal reply confirms
// restoration. Late or out-of-order replies cannot mutate a different request.
void burstFinish(bool safe){
    auto owner=burstOwner;burstOwner=BurstOwner::None;burstPending=0;
    if(owner==BurstOwner::Reports){reportBatch.running=false;
        snprintf(reportBatchMessage,sizeof(reportBatchMessage),"%s / %u reads / %u failed",burstCancel?"Cancelled":safe?"Done":"Stopped",reportBatch.completed,reportBatch.failed);}
    if(owner==BurstOwner::Graph)graphBurstDone(safe);
}
bool burstStart(BurstOwner owner,const unsigned *properties,unsigned count){
    if(burstOwner!=BurstOwner::None || activePending || extraPending || control.busy() || rateRequest || !connected() || !sessionReady || control.uncertain || !(val(132)&256))return false;
    burstCount=0;
    for(unsigned i=0;i<count && i<512;++i){acquisition::Plan plan;if(acquisition::resolve(properties[i],plan))continue;
        bool duplicate=false;
        for(unsigned n=0;n<burstCount;++n){const auto &p=decoding::properties[burstProperties[n]];
            duplicate|=p.address==plan.address && p.low==plan.registers[0] && p.count==plan.count;}
        if(!duplicate)burstProperties[burstCount++]=properties[i];
    }
    if(!burstCount)return false;
    burstOwner=owner;burstNext=0;burstCancel=burstCancelSent=false;burstGeneration=registry.generation;return true;
}
void burstStop(){
    if(burstOwner==BurstOwner::None)return;burstCancel=true;
    if(!burstPending){burstFinish(true);return;}
    uint8_t op=14,wire[32];size_t n=bringup::command(++control.nextId,&op,1,wire,sizeof(wire));
    if(n && uart.availableForWrite()>=int(n))burstCancelSent=uart.write(wire,n)==n;
}
void burstTick(){
    if(burstOwner==BurstOwner::None)return;
    if(burstGeneration!=registry.generation && !burstCancel)burstStop();
    if(burstCancel && !burstCancelSent && burstPending)burstStop();
    if(!connected() || !sessionReady || control.uncertain || (burstPending && uint32_t(millis()-burstAt)>35000)){
        sessionReady=false;needsSync=true;burstFinish(false);return;
    }
    if(burstPending || control.busy() || rateRequest)return;
    if(burstCancel || burstNext==burstCount){burstFinish(true);return;}
    uint8_t command[199]={22,0xa5},wire[224];
    if(!activeSession){activeSession=millis()+1;if(!activeSession)activeSession=1;}
    bringup::put32(command+2,activeSession);
    burstChunk=burstCount-burstNext;if(burstChunk>64)burstChunk=64;command[6]=uint8_t(burstChunk);
    for(unsigned i=0;i<burstChunk;++i){acquisition::Plan plan;
        if(acquisition::resolve(burstProperties[burstNext+i],plan)){burstFinish(false);return;}
        command[7+3*i]=plan.address;command[8+3*i]=plan.registers[0];command[9+3*i]=plan.count;}
    uint32_t id=control.nextId+1;if(!id)++id;
    size_t n=bringup::command(id,command,7+3*burstChunk,wire,sizeof(wire));
    if(!n || uart.availableForWrite()<int(n))return;
    control.nextId=id;burstPending=id;burstReceived=0;burstAt=millis();decoderGap();
    if(uart.write(wire,n)!=n){sessionReady=false;needsSync=true;burstFinish(false);}
}
bool burstPacket(const bringup::Packet &packet){
    if(!burstPending || packet.length<6 || bringup::le32(packet.payload)!=burstPending)return false;
    const auto *p=packet.payload;
    if(packet.type==2 && packet.length==6 && p[4]==22){if(p[5])burstFinish(false);return true;}
    if(packet.type!=6)return false;
    if(packet.length!=35 || p[27]!=6 || p[34]>1){sessionReady=false;needsSync=true;burstFinish(false);return true;}
    unsigned index=p[32]|unsigned(p[33])<<8;
    bool terminal=p[34]!=0;
    if(index>=burstChunk || (index!=burstReceived && !(burstCancel && terminal && index+1==burstReceived))){sessionReady=false;needsSync=true;burstFinish(false);return true;}
    const auto &definition=decoding::properties[burstProperties[burstNext+index]];
    if(p[29]!=definition.address || p[30]!=definition.count || p[31]!=definition.low || p[5]>definition.count){sessionReady=false;needsSync=true;burstFinish(false);return true;}
    // high is unused (zero) for single-register definitions. The wire result
    // reports the last byte address in the requested acquisition plan.
    uint8_t lastRegister=uint8_t(unsigned(definition.low)+definition.count-1);
    bool ok=p[4]==0 && p[5]==definition.count && p[7]==lastRegister;
    bool safe=(p[4]==0 || p[4]==2 || p[4]==3 || (burstCancel && p[4]==6)) && (!terminal || p[6]);
    if(index==burstReceived){
        ++burstReceived;
        if(!burstCancel && burstGeneration==registry.generation){
            uint32_t word=p[24]|uint32_t(p[25])<<8|uint32_t(p[28])<<16;
            for(unsigned i=0;i<decoding::PropertyCount;++i){const auto &d=decoding::properties[i];
                if(d.count && d.address==definition.address && d.low==definition.low && d.count==definition.count){
                    analysis::Value v;v.property=i;v.direction='R';v.word=word;v.complete=ok;v.success=ok;v.observed_ms=visualNow_ms();reportCache.accept(v);}}
            invalidateReports();
            if(burstOwner==BurstOwner::Reports){++reportBatch.completed;if(!ok)++reportBatch.failed;}
        }
    }
    if(terminal){
        if(!p[6]){sessionReady=false;needsSync=true;}
        decoderGap();burstPending=0;burstNext+=burstReceived;
        if(!safe || burstCancel || burstNext==burstCount)burstFinish(safe);
    }
    return true;
}
