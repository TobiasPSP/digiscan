// Actual LVGL UI + actual COBS receiver, not an independent model of the UI.
{
    if(modal)closeDetail(nullptr);
    control=observer::Control{};reportBatch.reset();activePending=extraPending=0;
    burstOwner=BurstOwner::None;burstPending=0;extraSessionBusy=false;extraWork=Work::Idle;
    seen=sessionReady=delivering=true;needsSync=false;statusAt=millis();status[0]=status[1]=status[2]=0;
    startupCapturePending=false;sessionMutation=false;rateRequest=0;defaultBaudAttempted=true;
    bringup::put32(status+132,511);screenIndex=4;graphGeneration=registry.generation;
    graphClear();unsigned property=0;
    for(unsigned i=0;i<decoding::PropertyCount;++i)if(decoding::properties[i].count==2 && decoding::properties[i].low==0x50 && decoding::properties[i].address==0x75){property=i;break;}
    graphAdd(property,0,30000,Cyan);graphMode=GraphMode::Passive;graphConfiguredActive=false;graphStopped=true;
    graphToggle(nullptr);assert(!graphStopped && graphStartPending && control.action==observer::Action::Enable);
    pump();while(control.busy())acknowledge();extraTick();assert(status[0] && !graphStartPending);
    analysis::Value v;v.property=property;v.complete=v.success=true;v.direction='R';v.word=14000;v.observed_ms=visualNow_ms();
    reportCache.accept(v,true,millis());previewTime_ms+=101;statusAt=millis();extraTick();
    auto *series=plots[0].series;auto *history=lv_chart_get_y_array(graphChart,series);bool any=false;
    for(unsigned i=0;i<128;++i)any|=history[i]!=LV_CHART_POINT_NONE;assert(any);
    graphHistoryClear(nullptr);assert(plotCount==1 && plots[0].series==series && !graphStopped);
    for(unsigned i=0;i<128;++i)assert(history[i]==LV_CHART_POINT_NONE);
    graphToggle(nullptr);assert(graphStopped);v.word=15000;reportCache.accept(v,true,millis());previewTime_ms+=101;statusAt=millis();extraTick();
    for(unsigned i=0;i<128;++i)assert(history[i]==LV_CHART_POINT_NONE);
    graphAcquisitionChanged(nullptr);assert(graphMode==GraphMode::Hybrid);
    lv_dropdown_set_selected(graphInterval,29);graphIntervalChanged(nullptr);assert(graphRefresh_ms==30000);
    lv_dropdown_set_selected(graphInterval,0);graphIntervalChanged(nullptr);assert(graphRefresh_ms==1000);
    graphToggle(nullptr);pump();while(control.busy())acknowledge();extraTick();
    reportCache.accept(v,true,millis());graphDue=millis();extraTick();assert(burstOwner==BurstOwner::None);
    previewTime_ms+=1001;statusAt=millis();extraTick();assert(burstOwner==BurstOwner::Graph);burstTick();assert(burstPending);
    uint8_t result[35]={};bringup::put32(result,burstPending);result[5]=2;result[6]=1;result[7]=0x51;result[24]=0x34;result[25]=0x12;
    result[26]=1;result[27]=6;result[29]=0x75;result[30]=2;result[31]=0x50;result[34]=1;
    uint32_t bad=receiver.bad;inject(6,result,35);pump();assert(receiver.bad==bad && burstOwner==BurstOwner::None && !graphReading);
    assert(reportCache.readings[property].lastGood.word==0x1234);
    graphAcquisitionChanged(nullptr);assert(graphMode==GraphMode::Active);graphDue=millis();extraTick();assert(burstOwner==BurstOwner::Graph);
    burstTick();assert(burstPending);uint32_t request=burstPending;graphToggle(nullptr);assert(graphStopped && burstCancel);
    bringup::put32(result,request);result[4]=6;result[5]=0;inject(6,result,35);pump();assert(!activeBusy() && graphStopped);
    graphHistoryClear(nullptr);for(unsigned i=0;i<128;++i)assert(history[i]==LV_CHART_POINT_NONE);
    lv_obj_update_layout(lv_screen_active());refreshExtras();
    assert(lv_obj_get_x(graphModeButton)+lv_obj_get_width(graphModeButton)<lv_obj_get_x(graphInterval));
    assert(lv_obj_get_x(graphInterval)+lv_obj_get_width(graphInterval)<200);
    assert(lv_obj_get_x(graphButton)+lv_obj_get_width(graphButton)<=320);
    slidePosition(nullptr,-3*W);save("tablet-v22-graph-footer.ppm");
    puts("PASS: acknowledged graph START from paused capture; clear preserves configuration; STOP freezes history; hybrid freshness; active batches; cancellation; footer layout");
    graphLinkLost();graphClear();graphMode=GraphMode::Passive;graphConfiguredActive=false;graphRefresh_ms=1000;
    control=observer::Control{};status[0]=1;screenIndex=0;slidePosition(nullptr,-W);
    updateReportOptions();reportCache.clear();reportBatch.reset();
    lv_obj_send_event(reportReadMissing,LV_EVENT_CLICKED,nullptr);assert(reportBatch.running && burstOwner==BurstOwner::Reports);
    while(burstOwner==BurstOwner::Reports){
        burstTick();assert(burstPending);
        unsigned chunk=burstChunk;uint32_t id=burstPending;
        for(unsigned n=0;n<chunk;++n){const auto &d=decoding::properties[burstProperties[burstNext+n]];
            uint8_t response[35]={};bringup::put32(response,id);response[5]=d.count;response[6]=n+1==chunk;
            // Match Job::result.reg, not the optional profile high field.
            response[7]=uint8_t(d.low+d.count-1);response[24]=4;response[26]=1;response[27]=6;response[29]=d.address;
            response[30]=d.count;response[31]=d.low;response[32]=uint8_t(n);response[34]=n+1==chunk;
            inject(6,response,35);pump();
        }
    }
    assert(!reportBatch.running && reportBatch.completed==reportBatch.count && !reportBatch.failed);
    unsigned singleReads=0,multipleReads=0;
    for(unsigned n=0;n<burstCount;++n){unsigned index=burstProperties[n];const auto &d=decoding::properties[index];
        assert(reportCache.readings[index].valid && !reportCache.readings[index].failed);
        if(d.count==1){assert(d.high==0);++singleReads;}else ++multipleReads;
    }
    assert(singleReads && multipleReads);
    unsigned completed=reportBatch.completed;lv_obj_send_event(reportReadMissing,LV_EVENT_CLICKED,nullptr);
    assert(!reportBatch.running && burstOwner==BurstOwner::None && !reportBatch.count && completed);
    puts("PASS: actual Read Missing uses opcode 22, deduplicates fields, gathers every result, restores capture at batch end, and skips already valid values");
}
