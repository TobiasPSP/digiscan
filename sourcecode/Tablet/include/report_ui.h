// Included inside the tablet UI namespace. Report definitions resolve at runtime.
// Compact two-column report rendering reuses rows instead of rebuilding the page.
// Successful values remain gathered even when their registers are not repeatedly
// polled. Swiping changes presentation, not ownership of acquisition requirements.
lv_obj_t *reportBody=nullptr,*reportSpacer=nullptr,*reportInfo=nullptr;
lv_obj_t *reportReadMissing=nullptr,*reportRefreshAll=nullptr;
lv_obj_t *reportProgress=nullptr;
char reportBatchMessage[128]="";
struct ReportOption { uint8_t address; unsigned page; };
ReportOption reportOptions[512]; unsigned reportOptionCount=0,reportSelected=0;
reporting::Model reportModel;
uint32_t reportGeneration=0xffffffffu,reportPaintAt=0,reportPressGeneration=0;
uint32_t reportEvidenceGeneration=0xffffffffu;
bool includeUnverifiedReports=false;
bool reportDirty=true; int reportPaintTop=-1;
void invalidateReports() { reportDirty=true; }
struct ReportRowUi { lv_obj_t *root,*line,*bold; unsigned row=0; };
constexpr unsigned ReportWidgetCount=48, ReportRowsPerColumn=20;
ReportRowUi reportWidgets[ReportWidgetCount];
lv_obj_t *reportGroupLabels[16]={};
int reportPositions[512]={},reportColumns[512]={},reportExtent=0;
bool reportPressed=false; analysis::Value reportPressedValue;
constexpr int ReportHeight=16;
void reportClicked(lv_event_t *e) {
    auto code=lv_event_get_code(e);
    if (code==LV_EVENT_PRESS_LOST) { reportPressed=false; rowHeld=false; return; }
    if (code==LV_EVENT_RELEASED) { rowHeld=false; return; }
    if (code==LV_EVENT_PRESSED) {
        reportPressed=false; rowHeld=true;
        unsigned widget=unsigned(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
        if (widget>=ReportWidgetCount || reportWidgets[widget].row>=reportModel.count || modal) return;
        unsigned index=reportModel.rows[reportWidgets[widget].row].property;
        const auto &reading=reportCache.readings[index];
        reportPressedValue=reading.valid?reading.lastGood:analysis::Value{};
        reportPressedValue.property=index; reportPressedValue.direction='R';
        if (!reading.valid) { reportPressedValue.success=!reading.failed;reportPressedValue.complete=false; }
        reportPressGeneration=registry.generation; reportPressed=true;
    } else if (code==LV_EVENT_CLICKED && reportPressed && reportPressGeneration==registry.generation && !swipe.horizontal && !sliding && !modal) {
        reportPressed=false; rowHeld=false; decodedDetail(reportPressedValue);
    }
}
void buildReport() {
    reportModel.count=0; reportExtent=0; reportPressed=false;
    reportDirty=true; reportPaintTop=-1;
    for (auto &widget:reportWidgets) lv_obj_add_flag(widget.root,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(reportSpacer,0,0);
    lv_obj_scroll_to_y(reportBody,0,LV_ANIM_OFF);
    for (auto *label:reportGroupLabels) if (label) lv_obj_add_flag(label,LV_OBJ_FLAG_HIDDEN);
    if (!reportOptionCount) { ui_update::text(reportInfo,"No assigned profile with reports"); return; }
    auto option=reportOptions[reportSelected];const auto &page=reporting::pages[option.page];
    if (!reportModel.build(page,decoding::properties,decoding::PropertyCount,option.address)) {
        ui_update::textFmt(reportInfo,"Report unavailable: %s",reportModel.error); return;
    }
    ui_update::text(reportInfo,"");
    for (unsigned i=0;i<reportModel.count;) {
        unsigned group=reportModel.rows[i].group;
        const char *title=page.groups[group].header;
        if (title && *title) {
            lv_obj_remove_flag(reportGroupLabels[group],LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(reportGroupLabels[group],8,reportExtent+3);
            ui_update::text(reportGroupLabels[group],title); reportExtent+=20;
        }
        unsigned end=i; while (end<reportModel.count && reportModel.rows[end].group==group) ++end;
        // Column-major blocks keep the same reading order as Monitor.
        constexpr unsigned block=2*ReportRowsPerColumn;
        for (unsigned n=0;n<end-i;++n) {
            reportColumns[i+n]=int((n%block)/ReportRowsPerColumn);
            reportPositions[i+n]=reportExtent+int((n/block)*ReportRowsPerColumn+n%ReportRowsPerColumn)*ReportHeight;
        }
        unsigned count=end-i, last=(count-1)%block+1;
        reportExtent+=int(((count-1)/block)*ReportRowsPerColumn+(last>ReportRowsPerColumn?ReportRowsPerColumn:last))*ReportHeight;
        i=end;
    }
    lv_obj_set_pos(reportSpacer,0,reportExtent); lv_obj_set_size(reportSpacer,1,1);
    lv_obj_scroll_to_y(reportBody,0,LV_ANIM_OFF);reportPaintAt=0;reportDirty=true;
}
void reportSelect(lv_event_t *) {
    if (reportBatch.running) return;
    reportSelected=lv_dropdown_get_selected(reportChoice); if (reportSelected>=reportOptionCount) reportSelected=0;
    buildReport();
}
void updateReportOptions() {
    uint8_t previousAddress=0; const char *previousProfile="",*previousTitle="";
    if (reportOptionCount && reportSelected<reportOptionCount) {
        const auto &option=reportOptions[reportSelected];const auto &page=reporting::pages[option.page];
        previousAddress=option.address;previousProfile=page.profileId;previousTitle=page.header;
    }
    reportOptionCount=0;reportSelected=0;lv_dropdown_clear_options(reportChoice);
    for (unsigned a=8;a<0x78;++a) if (registry.reportEligible(uint8_t(a),includeUnverifiedReports)) {
        const auto &profile=decoding::profiles[registry.bindings[a].profile];
        for (unsigned p=0;p<reporting::PageCount && reportOptionCount<512;++p) if (!strcmp(reporting::pages[p].profileId,profile.id)) {
            const auto &page=reporting::pages[p]; char title[192];
            snprintf(title,sizeof(title),"0x%02X / %s / %s",a,profile.name,page.header);
            lv_dropdown_add_option(reportChoice,title,LV_DROPDOWN_POS_LAST);
            reportOptions[reportOptionCount]={uint8_t(a),p};
            if (a==previousAddress && !strcmp(page.profileId,previousProfile) && !strcmp(page.header,previousTitle)) reportSelected=reportOptionCount;
            ++reportOptionCount;
        }
    }
    if (!reportOptionCount) { lv_dropdown_add_option(reportChoice,"No confirmed addresses",LV_DROPDOWN_POS_LAST);lv_obj_add_state(reportChoice,LV_STATE_DISABLED); }
    else lv_obj_remove_state(reportChoice,LV_STATE_DISABLED);
    lv_dropdown_set_selected(reportChoice,reportSelected); buildReport();reportGeneration=registry.generation;reportEvidenceGeneration=registry.evidenceGeneration;
}
void reportBatchResult(bool ok,bool safe) {
    if (!reportBatch.running) return;
    ++reportBatch.completed;if (!ok) ++reportBatch.failed;
    if (!safe || reportBatch.cancel) {
        reportBatch.running=false;
        snprintf(reportBatchMessage,sizeof(reportBatchMessage),"%s / %u reads / %u failed",reportBatch.cancel?"Cancelled":"Stopped",reportBatch.completed,reportBatch.failed);
    }
}
void reportBatchCancel() {
    if (!reportBatch.running || reportBatch.cancel) return;
    reportBatch.cancel=true;
    if(burstOwner==BurstOwner::Reports){burstStop();return;}
    if (activePending) activeAction(nullptr);
}
void reportBatchStart(lv_event_t *event) {
    if (reportBatch.running) { reportBatchCancel();return; }
    if (!connected() || !sessionReady || status[1] || status[2] || !(val(132)&32) || activeBusy() || control.busy() || rateRequest || control.uncertain) return;
    bool missing=lv_event_get_target(event)==reportReadMissing;
    reportBatch.build(reportModel,reportCache,missing,registry.generation);
    snprintf(reportBatchMessage,sizeof(reportBatchMessage),"%s",reportBatch.running?"Starting":missing?"No missing values":"No readable properties");
    reportBatchTick();
}
void reportBatchTick() {
    if (!reportProgress) return;
    if (reportBatch.running) {
        if (reportBatch.generation!=registry.generation) reportBatchCancel();
        if((val(132)&256) && burstOwner==BurstOwner::None && !reportBatch.cancel && reportBatch.next==0){
            if(burstStart(BurstOwner::Reports,reportBatch.properties,reportBatch.count))reportBatch.next=reportBatch.count;
        }
        if (!activePending && burstOwner!=BurstOwner::Reports) {
            if (reportBatch.cancel || !connected() || !sessionReady || control.uncertain) {
                reportBatch.running=false;
                snprintf(reportBatchMessage,sizeof(reportBatchMessage),"%s / %u reads / %u failed",reportBatch.cancel?"Cancelled":"Link uncertain",reportBatch.completed,reportBatch.failed);
            } else if (!control.busy() && !rateRequest) {
                while (reportBatch.next<reportBatch.count && !reportBatch.needed(reportBatch.properties[reportBatch.next],reportCache)) {
                    ++reportBatch.next;++reportBatch.skipped;
                }
                if (reportBatch.next==reportBatch.count) {
                    reportBatch.running=false;
                    snprintf(reportBatchMessage,sizeof(reportBatchMessage),"Done / %u reads / %u failed / %u skipped",reportBatch.completed,reportBatch.failed,reportBatch.skipped);
                } else {
                    unsigned displayedProperty=activeProperty;
                    activeProperty=reportBatch.properties[reportBatch.next];
                    reportBatchDispatch=true;activeAction(nullptr);reportBatchDispatch=false;
                    activeProperty=displayedProperty;
                    if (activePending) ++reportBatch.next;
                }
            }
        }
        if (reportBatch.running) snprintf(reportBatchMessage,sizeof(reportBatchMessage),"%s %u/%u / %u failed",reportBatch.cancel?"Cancelling":"Reading",reportBatch.completed,reportBatch.count,reportBatch.failed);
    }
    ui_update::text(reportProgress,reportBatchMessage);
    ui_update::text(lv_obj_get_child(reportReadMissing,0),reportBatch.running?"CANCEL":"Read Missing");
    ui_update::text(lv_obj_get_child(reportRefreshAll,0),reportBatch.running?"CANCEL":"Read All");
    uint32_t actionColor=reportBatch.running?0x9c3544:0x238557;
    ui_update::bgColor(reportReadMissing,lv_color_hex(actionColor),0);
    ui_update::bgColor(reportRefreshAll,lv_color_hex(actionColor),0);
    bool ready=connected() && sessionReady && !status[1] && !status[2] && (val(132)&32) && !activeBusy() && !control.busy() && !rateRequest && !control.uncertain && reportModel.count;
    if (reportBatch.running && !reportBatch.cancel) lv_obj_remove_state(reportReadMissing,LV_STATE_DISABLED);
    else if (ready) lv_obj_remove_state(reportReadMissing,LV_STATE_DISABLED);
    else lv_obj_add_state(reportReadMissing,LV_STATE_DISABLED);
    if ((reportBatch.running && !reportBatch.cancel) || ready) lv_obj_remove_state(reportRefreshAll,LV_STATE_DISABLED);else lv_obj_add_state(reportRefreshAll,LV_STATE_DISABLED);
    if (reportBatch.running || !reportOptionCount) lv_obj_add_state(reportChoice,LV_STATE_DISABLED);else lv_obj_remove_state(reportChoice,LV_STATE_DISABLED);
}
void reportPolicy() {
    if (!reportScreen) return;
    if ((reportGeneration!=registry.generation || reportEvidenceGeneration!=registry.evidenceGeneration) && !touchHeld && !rowHeld && !modal && !sliding) updateReportOptions();
    if (sessionMutation || control.busy() || rateRequest || activeBusy() || control.uncertain) return;
    uint8_t old[52]; memcpy(old,control.filterCommand,52); sourceFilter(selection);
    if (memcmp(old,control.filterCommand,52) && connected() && sessionReady) {
        decoderGap();control.begin(observer::Action::Configure);
    }
}
void refreshReports() {
    if (!reportBody || sliding || touchHeld || rowHeld) return;
    uint32_t now=visualNow_ms();
    int top=lv_obj_get_scroll_y(reportBody),bottom=top+lv_obj_get_height(reportBody);
    if (!reportDirty && top==reportPaintTop && (!visualClock.running || uint32_t(now-reportPaintAt)<125)) return;
    reportPaintAt=now; reportPaintTop=top;reportDirty=false;
    unsigned widget=0;
    for (unsigned row=0;row<reportModel.count && widget<ReportWidgetCount;++row) {
        int y=reportPositions[row]; if (y+ReportHeight<top || y>bottom) continue;
        auto &ui=reportWidgets[widget++];ui.row=row; const auto &p=decoding::properties[reportModel.rows[row].property];
        const auto &reading=reportCache.readings[reportModel.rows[row].property];
        lv_obj_remove_flag(ui.root,LV_OBJ_FLAG_HIDDEN);lv_obj_set_pos(ui.root,8+156*reportColumns[row],y+1);
        char value[192]="",line[384];
        if (reading.valid && !p.bit0) analysis::formatValue(reading.lastGood,value,sizeof(value));
        snprintf(line,sizeof(line),"%s%s%s%s",reading.failed?"FAIL ":"",value,*value?" ":"",p.name);
        uint32_t bg=reporting::background(reading,now);
        if (reading.valid && p.bit0 && !reading.failed && !reading.warnings) {
            unsigned code=(reading.lastGood.word&p.mask)>>p.shift;
            // YAML explicitly defines YES/NO polarity; a set bit is not always TRUE.
            bg=!strcmp(code?p.bit1:p.bit0,"YES")?0xb8dfbc:0xe8b6b8;
        }
        if (reading.failed) bg=0xffffff;
        unsigned brightness=(((bg>>16)&255)*3+((bg>>8)&255)*6+(bg&255))/10;
        uint32_t color=reading.failed?0xb00020:brightness>145?0x182320:Ink;
        ui_update::bgColor(ui.root,lv_color_hex(bg),0);
        ui_update::text(ui.line,line);ui_update::textColor(ui.line,lv_color_hex(color),0);
        // One-pixel overprint gives failure text a bold weight at the same small size.
        if (reading.failed) {
            ui_update::text(ui.bold,line);ui_update::textColor(ui.bold,lv_color_hex(color),0);
            lv_obj_remove_flag(ui.bold,LV_OBJ_FLAG_HIDDEN);
        } else lv_obj_add_flag(ui.bold,LV_OBJ_FLAG_HIDDEN);
    }
    for (;widget<ReportWidgetCount;++widget) lv_obj_add_flag(reportWidgets[widget].root,LV_OBJ_FLAG_HIDDEN);
}
void reportUnverifiedChanged(lv_event_t *e){
    includeUnverifiedReports=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);updateReportOptions();
}
void reportSettings(lv_event_t *) {
    if (modal || swipe.horizontal || sliding) return;
    modal=lv_obj_create(lv_screen_active()); flat(modal,Bg);lv_obj_set_size(modal,W,H);
    text(modal,12,14,230,"INSTRUMENT SETTINGS",&lv_font_montserrat_18);
    button(modal,266,4,48,LV_SYMBOL_CLOSE,closeDetail,0);
    auto *unverified=lv_checkbox_create(modal);lv_obj_set_pos(unverified,12,44);lv_checkbox_set_text(unverified,"Include unverified addresses");
    lv_obj_set_style_text_font(unverified,&lv_font_montserrat_12,0);lv_obj_set_style_text_color(unverified,lv_color_hex(Ink),0);
    if(includeUnverifiedReports)lv_obj_add_state(unverified,LV_STATE_CHECKED);
    lv_obj_add_event_cb(unverified,reportUnverifiedChanged,LV_EVENT_VALUE_CHANGED,nullptr);
    auto *tile=inset(modal,8,68,304,150);
    detailLine(tile,12,"Acquisition: explicit Read",&lv_font_montserrat_16,Cyan);
    detailLine(tile,44,"Reports collect independently of the monitor filter.",&lv_font_montserrat_14,Ink);
    detailLine(tile,94,"Selectable pull-ups remain disabled.",&lv_font_montserrat_12,Muted);insetEdge(tile);
    text(modal,12,244,296,"Capture Run/Pause is instrument-wide.",&lv_font_montserrat_14);
    text(modal,12,284,296,"Read Missing fills absent or failed values. Read All refreshes this report. Shared fields use one read; Cancel stops the batch.",&lv_font_montserrat_14);
    text(modal,12,360,296,"Experimental shared-bus access. Existing controller arbitration is not guaranteed. No pull-ups, retries or configuration writes.",&lv_font_montserrat_12);
}
void slidePosition(void *,int32_t x) {
    lv_obj_set_x(monitorScreen,x+W);lv_obj_set_x(reportScreen,x+2*W);
    const unsigned pages[]={6,0,3};
    for(unsigned i=0;i<3;++i)if(extraScreens[i])lv_obj_set_x(extraScreens[i],x+int(pages[i])*W);
    if(recorderScreen)lv_obj_set_x(recorderScreen,x+4*W);
    if(readScreen)lv_obj_set_x(readScreen,x+5*W);
}
unsigned pagePosition(unsigned screen) { const unsigned positions[]={1,2,6,0,3,4,5};return positions[screen]; }
void restoreScreen() {
    graphLinkLost();
    screenIndex=3;
    reportActive=screenIndex==1;slidePosition(nullptr,-int(pagePosition(screenIndex))*W);
    for(auto *o:{monitorScreen,reportScreen,extraScreens[0],extraScreens[2],recorderScreen,readScreen})lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
}
void slideDone(lv_anim_t *) {
    sliding=false;reportPaintAt=0;reportDirty=true;
}
void navigateReport(bool next) {
    if (!busConfirmed || sessionMutation || sliding || (modal && screenIndex!=0)) return;
    const unsigned order[]={3,0,1,4,5,6,2};unsigned position=pagePosition(screenIndex);
    unsigned target=order[next?(position<6?position+1:6):(position?position-1:0)];
    if(target==screenIndex)return;
    discoveryResume=false;
    if(modal)closeDetail(nullptr);
    screenIndex=target;reportActive=target==1;pressedValid=false;reportPressed=false;rowHeld=false;sliding=true;
    lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,monitorScreen);
    lv_anim_set_values(&anim,lv_obj_get_x(monitorScreen)-W,-int(pagePosition(screenIndex))*W);lv_anim_set_duration(&anim,180);
    lv_anim_set_exec_cb(&anim,slidePosition);lv_anim_set_completed_cb(&anim,slideDone);lv_anim_start(&anim);
}
void createReports() {
    reportScreen=lv_obj_create(lv_screen_active());flat(reportScreen,Bg);lv_obj_set_size(reportScreen,W,H);lv_obj_set_pos(reportScreen,W,0);
    lv_obj_remove_flag(reportScreen,LV_OBJ_FLAG_SCROLLABLE);
    text(reportScreen,8,7,236,"I2C REPORTS",&lv_font_montserrat_18);
    button(reportScreen,272,0,40,LV_SYMBOL_SETTINGS,reportSettings,0);
    reportChoice=lv_dropdown_create(reportScreen);lv_obj_set_pos(reportChoice,8,36);lv_obj_set_size(reportChoice,304,36);
    ui_update::font(reportChoice,&lv_font_montserrat_12,0);ui_update::bgColor(reportChoice,lv_color_hex(0x2c363b),0);ui_update::textColor(reportChoice,lv_color_hex(Ink),0);
    lv_obj_add_event_cb(reportChoice,reportSelect,LV_EVENT_VALUE_CHANGED,nullptr);
    reportReadMissing=button(reportScreen,8,78,148,"Read Missing",reportBatchStart,0);
    reportRefreshAll=button(reportScreen,164,78,148,"Read All",reportBatchStart,0);
    ui_update::bgColor(reportReadMissing,lv_color_hex(0x238557),0);ui_update::bgColor(reportRefreshAll,lv_color_hex(0x238557),0);
    lv_obj_add_state(reportReadMissing,LV_STATE_DISABLED);lv_obj_add_state(reportRefreshAll,LV_STATE_DISABLED);
    reportProgress=text(reportScreen,8,120,304,"",&lv_font_montserrat_10);ui_update::textColor(reportProgress,lv_color_hex(Muted),0);
    reportBody=lv_obj_create(reportScreen);flat(reportBody,Bg);lv_obj_set_pos(reportBody,0,136);lv_obj_set_size(reportBody,W,316);lv_obj_set_scroll_dir(reportBody,LV_DIR_VER);
    reportSpacer=lv_obj_create(reportBody);flat(reportSpacer,Bg);lv_obj_set_size(reportSpacer,1,1);lv_obj_remove_flag(reportSpacer,LV_OBJ_FLAG_CLICKABLE);
    reportInfo=text(reportBody,12,12,296,"");
    for (auto &label:reportGroupLabels) {label=text(reportBody,8,0,304,"",&lv_font_montserrat_12);lv_obj_add_flag(label,LV_OBJ_FLAG_HIDDEN);}
    for (unsigned i=0;i<ReportWidgetCount;++i) {
        auto &ui=reportWidgets[i];ui.root=lv_obj_create(reportBody);flat(ui.root,0);lv_obj_set_size(ui.root,148,15);lv_obj_remove_flag(ui.root,LV_OBJ_FLAG_SCROLLABLE);
        ui.line=text(ui.root,3,2,141,"",&lv_font_montserrat_10);lv_label_set_long_mode(ui.line,LV_LABEL_LONG_DOT);
        ui.bold=text(ui.root,4,2,141,"",&lv_font_montserrat_10);lv_label_set_long_mode(ui.bold,LV_LABEL_LONG_DOT);lv_obj_add_flag(ui.bold,LV_OBJ_FLAG_HIDDEN);
        for (auto code:{LV_EVENT_PRESSED,LV_EVENT_RELEASED,LV_EVENT_PRESS_LOST,LV_EVENT_CLICKED})lv_obj_add_event_cb(ui.root,reportClicked,code,reinterpret_cast<void *>(uintptr_t(i)));
        for (auto *label:{ui.line,ui.bold}) {lv_obj_remove_flag(label,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(label,LV_OBJ_FLAG_EVENT_BUBBLE);}
        lv_obj_add_flag(ui.root,LV_OBJ_FLAG_HIDDEN);
    }
    auto *indicator=text(reportScreen,8,462,304,"MONITOR  /  REPORTS",&lv_font_montserrat_10);lv_obj_set_style_text_align(indicator,LV_TEXT_ALIGN_CENTER,0);ui_update::textColor(indicator,lv_color_hex(Muted),0);
    updateReportOptions();
}
