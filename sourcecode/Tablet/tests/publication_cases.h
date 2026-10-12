// Deterministic healthy examples for documentation, after fault-injection tests.
// These values are illustrative test data, not new chip calibration evidence.
{
    if(modal)closeDetail(nullptr);
    sessionMutation=false;startStage=StartStage::Idle;control=observer::Control{};
    extraWork=Work::Idle;extraPending=activePending=0;extraSessionBusy=false;
    profileUploadBusy=false;uploadRebootAt=0;startClearLocal();registry.reset();
    registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual);
    registry.observe(0x75,true);startWorkspace(true);
    seen=sessionReady=delivering=true;needsSync=false;startupCapturePending=false;
    memset(status,0,sizeof(status));status[0]=1;statusAt=millis();statusPackets=1;
    bringup::put32(status+132,255);bringup::put32(status+136,2000000);
    bringup::put32(status+176,65536);bringup::put32(status+60,7700);
    uartBaud=tabletActualBaud=2000000;rateRequest=0;defaultBaudAttempted=true;
    add(3,4,0);add(4,4,0);add(8,4,0);add(0x17,0x80,0);
    add(0x22,0x80,0);add(0x31,0,0);add(0x33,0x81,0);add(0x34,0,0);
    add(0x3a,30,0);add(0x50,0x33,0);add(0x51,0x34,0);
    add(0x6e,0xfd,0);add(0x6f,7,0);
    screenIndex=0;reportActive=false;slidePosition(nullptr,-W);
    selection.display=analysis::Display::Parsed;view=View::Unique;refreshUi();save("publication-monitor.ppm");
    for(unsigned i=0;i<decoding::PropertyCount;++i)if(decoder.used[i] && decoding::properties[i].low==0x3a){
        decodedDetail(decoder.values[i]);save("publication-details.ppm");closeDetail(nullptr);break;
    }
    screenIndex=1;reportActive=true;slidePosition(nullptr,-2*W);
    updateReportOptions();reportSelected=0;lv_dropdown_set_selected(reportChoice,0);buildReport();
    previewTime_ms+=2300;statusAt=millis();refreshReports();save("publication-reports.ppm");
    screenIndex=0;reportActive=false;slidePosition(nullptr,-W);view=View::Diag;
    lv_dropdown_set_selected(viewChoice,unsigned(view));
    refreshUi();lv_obj_scroll_to_y(diag,0,LV_ANIM_OFF);save("publication-diagnostics.ppm");
    puts("PASS: healthy publication screenshots rendered from the actual UI");
}
