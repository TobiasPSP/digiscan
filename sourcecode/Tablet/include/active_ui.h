// Included inside the UI namespace after detail helpers.
// Touch only requests work; the main scheduler owns bus command execution.
// Completion distinguishes read success from restoration of previous capture
// state. A timeout must not imply that the peer resumed or that a read failed
// electrically: it can also mean the acknowledgement was not delivered.
void activeCaption(bool pending){
    if(!activeButtonCaption)return;
    ui_update::text(activeButtonCaption,pending?"Cancel read":"Read");
    ui_update::bgColor(lv_obj_get_parent(activeButtonCaption),lv_color_hex(pending?0x9c3544:0x238557),0);
}
bool activeEligible(unsigned index) {
    acquisition::Plan plan;return !acquisition::resolve(index,plan);
}
void activeAction(lv_event_t *) {
    if(burstOwner!=BurstOwner::None)return;
    uint8_t wire[48],command[11]={15,0xa5};
    if (activePending) {
        if (reportBatch.running) reportBatch.cancel=true;
        command[0]=14;size_t n=bringup::command(++control.nextId,command,1,wire,sizeof(wire));
        if (uart.availableForWrite()>=int(n)) uart.write(wire,n);
        return;
    }
    if (reportBatch.running && !reportBatchDispatch) return;
    if (extraPending || (extraSessionBusy && !graphDispatch)) return;
    acquisition::Plan plan;
    if (!connected() || !sessionReady || !(val(132)&32) ||
        control.busy() || rateRequest || acquisition::resolve(activeProperty,plan)) return;
    if (!activeSession) {
#ifdef TABLET_PREVIEW
        activeSession=millis();
#else
        // Deduplication epoch, not an authentication token; avoid repeatable boot timestamps.
        activeSession=esp_random();
#endif
        if (!activeSession) activeSession=1;
    }
    bringup::put32(command+2,activeSession);
    command[6]=plan.address;command[7]=plan.count;memcpy(command+8,plan.registers,3);
    uint32_t id=++control.nextId;if (!id) id=++control.nextId;
    size_t n=bringup::command(id,command,sizeof(command),wire,sizeof(wire));
    if (!n || uart.availableForWrite()<int(n)) return;
    activePending=id;activeAt_ms=millis();activeGeneration=registry.generation;
    activeRefreshDetail=false;
    activeRequestedProperty=activeProperty;activeDefinition=decoding::properties[activeProperty];
    decoderGap();snprintf(activeMessage,sizeof(activeMessage),"ACTIVE / waiting for STOP + idle");
    if (uart.write(wire,n)!=n) { sessionReady=false;needsSync=true;snprintf(activeMessage,sizeof(activeMessage),"SHORT WRITE / state uncertain; no retry"); }
    if (activeLabel) ui_update::text(activeLabel,activeMessage);
    activeCaption(true);
}
int activeReadDetails(lv_obj_t *body,int y,const analysis::Value &value) {
    activeProperty=value.property;
    auto *tile=inset(body,8,y,304,152);
    detailLine(tile,8,"ACTIVE READ / shared bus",&lv_font_montserrat_12,Cyan);
    auto *action=button(tile,8,32,280,activePending?"Cancel read":"Read",activeAction,0);
    activeButtonCaption=lv_obj_get_child(action,0);
    activeCaption(activePending!=0);
    acquisition::Plan plan;const char *reason=acquisition::resolve(value.property,plan);
    if (!reason && (!connected() || !sessionReady)) reason="Connect the sniffer and wait for link synchronization.";
    if (!reason && !(val(132)&32)) reason="Update the RP2350 to V9 for property reads.";
    if (!reason && (control.busy() || rateRequest)) reason="Another instrument operation is pending.";
    if (!reason && reportBatch.running && !activePending) reason="Report batch is in progress.";
    if (!reason && extraSessionBusy) reason="An experimental operation is in progress.";
    if (!activePending && reason)
        lv_obj_add_state(action,LV_STATE_DISABLED);
    activeLabel=text(tile,8,82,288,reason?reason:activeMessage,&lv_font_montserrat_12);
    insetEdge(tile);return y+162;
}
void activePacket(const bringup::Packet &packet) {
    if (!activePending || packet.length<6 || bringup::le32(packet.payload)!=activePending) return;
    if (packet.type==2) {
        if (!packet.payload[5]) return;
        snprintf(activeMessage,sizeof(activeMessage),"ACTIVE request rejected (%u); no read retry",packet.payload[5]);
        activePending=0;
        reportBatchResult(false,false);
    } else {
        const auto *p=packet.payload;
        static const char *names[]={"OK","BUS BUSY","ADDRESS NACK","POINTER NACK","ARBITRATION LOST",
            "TIMEOUT","CANCELLED","RESTORE FAILED","HARDWARE ABORT","CAPTURE FAULT","INTERFERENCE"};
        const auto &definition=activeDefinition;
        if (packet.length!=32 || p[27]!=2 || p[4]>10 || p[29]!=definition.address || p[30]!=definition.count ||
            p[31]!=definition.low || p[5]>definition.count) {
            activePending=0;sessionReady=false;needsSync=true;
            reportBatchResult(false,false);
            snprintf(activeMessage,sizeof(activeMessage),"ACTIVE result mismatch / state uncertain. No retry.");
            if (activeLabel) ui_update::text(activeLabel,activeMessage);
            activeCaption(false);
            return;
        }
        bool ok=p[4]==0 && p[5]==definition.count && p[6] && p[7]==definition.low+definition.count-1;
        analysis::Value value{};value.property=activeRequestedProperty;value.direction='R';value.success=value.complete=ok;
        value.word=uint32_t(p[24])|(uint32_t(p[25])<<8)|(uint32_t(p[28])<<16);value.observed_ms=visualNow_ms();
        if (activeGeneration==registry.generation) {
            // Reading a register supplies all its fields, not just the selected mask.
            for (unsigned i=0;i<decoding::PropertyCount;++i) {
                const auto &field=decoding::properties[i];
                if (field.count==definition.count && field.address==definition.address && field.low==definition.low && field.high==definition.high) {
                    value.property=i;reportCache.accept(value);
                }
            }
            invalidateReports();
            if (ok && modal && activeLabel && activeProperty<decoding::PropertyCount) {
                const auto &shown=decoding::properties[activeProperty];
                activeRefreshDetail=shown.address==definition.address && shown.low==definition.low && shown.count==definition.count;
            }
        }
        if (ok && activeGeneration==registry.generation) {
            char formatted[192];value.property=activeRequestedProperty;analysis::formatValue(value,formatted,sizeof(formatted));
            snprintf(activeMessage,sizeof(activeMessage),"ACTIVE OK / %.100s\n%lu Hz / capture restored",formatted,(unsigned long)bringup::le32(p+12));
        } else if (ok) snprintf(activeMessage,sizeof(activeMessage),"ACTIVE OK / profile changed\nResult not applied; capture restored");
        else snprintf(activeMessage,sizeof(activeMessage),"ACTIVE %s / %u of %u bytes\nNo complete value\nAbort 0x%08lX / restored %s",
            names[p[4]],p[5],definition.count,(unsigned long)bringup::le32(p+8),p[6]?"YES":"NO");
        if (!p[6]) { sessionReady=false;needsSync=true; }
        activePending=0;decoderGap();
        // NACKs are per-register failures. Other failures stop the batch rather
        // than repeatedly probing a busy, lost-arbitration or uncertain bus.
        reportBatchResult(ok,p[6] && (ok || p[4]==2 || p[4]==3));
    }
    if (activeLabel) ui_update::text(activeLabel,activeMessage);
    activeCaption(false);
}
void activeUiTick() {
    burstTick();
    // Refresh outside packet handling, preserving scroll position and modal lifetime.
    if (activeRefreshDetail) {
        activeRefreshDetail=false;
        if (modal && activeLabel && activeProperty<decoding::PropertyCount && reportCache.readings[activeProperty].valid) {
            auto value=reportCache.readings[activeProperty].lastGood;
            auto *body=lv_obj_get_parent(lv_obj_get_parent(activeLabel));int scroll=lv_obj_get_scroll_y(body);
            closeDetail(nullptr);decodedDetail(value);
            lv_obj_update_layout(modal);body=lv_obj_get_parent(lv_obj_get_parent(activeLabel));
            lv_obj_scroll_to_y(body,scroll,LV_ANIM_OFF);
        }
    }
    // Includes completion delivery behind a full 64 KiB UART queue at fallback baud.
    if (activePending && uint32_t(millis()-activeAt_ms)>15000) {
        activePending=0;sessionReady=false;needsSync=true;
        reportBatchResult(false,false);
        snprintf(activeMessage,sizeof(activeMessage),"ACTIVE result timeout / state uncertain\nNo automatic read retry. Reconnect to sync.");
        if (activeLabel) ui_update::text(activeLabel,activeMessage);
        activeCaption(false);
    }
    reportBatchTick();
}
