#ifndef TABLET_PREVIEW
#define TABLET_PREVIEW
#endif
#include "../src/main.cpp"
#include <assert.h>
#undef assert
#define assert(test) do { if (!(test)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#test); exit(1); } } while (0)
static const char *outputDirectory="tmp";
static void tapRow(unsigned index) {
    lv_obj_send_event(rows[index],LV_EVENT_PRESSED,nullptr);
    lv_obj_send_event(rows[index],LV_EVENT_RELEASED,nullptr);
    lv_obj_send_event(rows[index],LV_EVENT_CLICKED,nullptr);
}
static bool hasLabel(lv_obj_t *parent,const char *expected) {
    if (lv_obj_check_type(parent,&lv_label_class) && !strcmp(lv_label_get_text(parent),expected)) return true;
    for (unsigned i=0;i<lv_obj_get_child_count(parent);++i) if (hasLabel(lv_obj_get_child(parent,i),expected)) return true;
    return false;
}
static lv_obj_t *labelButton(lv_obj_t *root,const char *caption){
    if(lv_obj_check_type(root,&lv_label_class) && !strcmp(lv_label_get_text(root),caption)){
        auto *p=lv_obj_get_parent(root);if(p && lv_obj_check_type(p,&lv_button_class))return p;
    }
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i)if(auto *found=labelButton(lv_obj_get_child(root,i),caption))return found;
    return nullptr;
}
static void buttonColor(lv_obj_t *root,const char *caption,uint32_t color){
    auto *b=labelButton(root,caption);assert(b);assert(lv_color_eq(lv_obj_get_style_bg_color(b,0),lv_color_hex(color)));
}
static void save(const char *name) {
    lv_refr_now(nullptr);
    char path[2048]; snprintf(path,sizeof(path),"%s/%s",outputDirectory,name);
    auto *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n320 480\n255\n");
    for (int i=0;i<320*480;++i) {
        uint16_t pixel=fb[i];
        uint8_t rgb[]={uint8_t(((pixel>>11)&31)*255/31),uint8_t(((pixel>>5)&63)*255/63),uint8_t((pixel&31)*255/31)};
        fwrite(rgb,1,3,f);
    }
    fclose(f);
}
static void add(uint8_t reg,uint8_t byte,uint32_t age=3000) {
    uint8_t p[]={1,0,0,0,1,1,0x75,0,2,0,2,0,1,0,0,0xea,1,0,1,2,0,0,0,0,0xeb,1,0,0};
    static uint32_t transactionId=0; bringup::put32(p,++transactionId);
    p[7]=p[17]=reg; p[26]=byte; store->add(p,sizeof(p),visualNow_ms()-age); decoder.add(store->incoming,visualNow_ms()-age);
}
static void inject(uint8_t type,const uint8_t *payload,unsigned size) {
    std::vector<uint8_t> raw(size+14), wire(bringup::MaxWire);
    raw[0]='I'; raw[1]='S'; raw[2]=1; raw[3]=type;
    uint32_t serial=sequence.have?sequence.last+1:1;
    // Include queued packets when assigning the next simulated peer sequence.
    static uint32_t next=0; if (int32_t(serial-next)>0) next=serial;
    bringup::put32(raw.data()+4,next++); raw[8]=uint8_t(size); raw[9]=uint8_t(size>>8);
    memcpy(raw.data()+10,payload,size); bringup::put32(raw.data()+10+size,bringup::crc32(raw.data(),10+size));
    size_t n=bringup::encode(raw.data(),14+size,wire.data(),wire.size()); assert(n);
    for (size_t i=0;i<n;++i) uart.input.push_back(wire[i]);
}
static void acknowledge(bool freshStatus=true) {
    assert(control.pendingId); uint8_t opcode=control.opcode;
    if (opcode==2 || opcode==9 || opcode==5 || (opcode==4 && freshStatus) || opcode==1) {
        uint8_t peer[200]; memcpy(peer,status,sizeof(peer));
        if (opcode==2 || opcode==9) peer[0]=0;
        if (opcode==9) bringup::put32(peer+4,bringup::le32(peer+4)+1);
        if (opcode==1) peer[0]=1;
        if (opcode==5) peer[3]=1;
        inject(3,peer,sizeof(peer));
    }
    uint8_t reply[6]; bringup::put32(reply,control.pendingId); reply[4]=opcode; reply[5]=0;
    inject(2,reply,sizeof(reply)); pump();
}
// Legacy fixtures change selection directly. Acknowledge the newly added
// consumer-union updates as the live RP2350 would, without masking other actions.
static void refreshFixture() {
    refreshUi();
    if (control.busy() && control.action==observer::Action::Configure) {
        pump();
        for (unsigned i=0;i<4;++i) acknowledge();
        refreshUi();
    }
}
#define refreshUi refreshFixture
int main(int argc,char **argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    if (argc>1) outputDirectory=argv[1];
    setup();
    assert(screenIndex==3 && lv_obj_get_x(extraScreens[1])==0);
    assert(!busConfirmed && startupCapturePending && lv_obj_has_flag(monitorScreen,LV_OBJ_FLAG_HIDDEN));
    startupCapturePending=false;startWorkspace(true); // Legacy regressions exercise an established session.
    assert(graphStopped && !graphActive && !graphReading);
    screenIndex=0;reportActive=false;slidePosition(nullptr,-int(pagePosition(screenIndex))*W);
    assert(!includeUnverifiedReports);
    includeUnverifiedReports=true; // Legacy fixtures deliberately assign active-only addresses.
    assert(registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual));
    defaultBaudAttempted=true; // Exercise legacy UI fixtures independently; auto negotiation tested below.
    assert(selection.display==analysis::Display::Parsed);
    assert(view==View::Unique && selection.uniqueOrder==analysis::UniqueOrder::LatestChange);
    selection.uniqueOrder=analysis::UniqueOrder::LatestTransaction; // Existing fixtures exercise transaction ordering.
    assert(selection.changeFactor==0 && decoder.changeFactor==0);
    selection.display=analysis::Display::Raw;
    uint8_t bootPeer[200]={}; bootPeer[0]=1; bringup::put32(bootPeer+132,15);
    bringup::put32(bootPeer+176,65536);
    inject(3,bootPeer,sizeof(bootPeer)); pump();
    assert(!sessionReady && control.action==observer::Action::Synchronize);
    refreshUi(); assert(lv_obj_has_state(controls[0],LV_STATE_DISABLED));
    for (uint8_t opcode:{uint8_t(2),uint8_t(8),uint8_t(5),uint8_t(4)}) {
        assert(control.opcode==opcode && control.pendingId); acknowledge();
    }
    assert(sessionReady && delivering && !status[0] && status[3]);
    assert(store->uniqueCount==0 && !control.busy());
    seen=true; statusAt=millis(); status[0]=status[3]=1;
    bringup::put32(status+60,7688); bringup::put32(status+12,6890);
    const uint8_t regs[]={0x04,0x17,0x31,0x33,0x34,0x38,0x3a,0x50,0x51,0x52,0x53,0x6e,0x6f,0x70,0x71,0x74,0x75,0x77,0x78,0x79,0x08,0x0a,0x03,0x20,0x21,0x22,0x23,0x24};
    for (unsigned i=0;i<28;++i) add(regs[i],uint8_t(i*7));
    store->unique[store->order[0]].changed_ms=visualNow_ms(); store->unique[store->order[0]].fresh=false;
    store->unique[store->order[1]].changed_ms=visualNow_ms();
    refreshUi(); save("tablet-v6-raw.ppm");
    assert(!strcmp(lv_label_get_text(speed),"7.7 kHz")); assert(visibleCount==28);
    lv_point_t size; lv_text_get_size(&size,"AA AA W AA BB+",&mono::font,0,0,1000,LV_TEXT_FLAG_NONE);
    assert(size.x<=128 && size.y<=18); assert(lv_obj_get_scroll_bottom(list)==0);
    for (unsigned i=0;i<28;++i) {
        lv_area_t row,label; lv_obj_get_coords(rows[i],&row); lv_obj_get_coords(labels[i],&label);
        assert(row.x1>=0 && row.x2<320 && row.y1>=132 && row.y2<452);
        assert(row.x1==8+int(i/16)*156 && row.y1==132+int(i%16)*20);
        assert(label.y2<=row.y2 && label.x2<=row.x2);
        assert(lv_obj_get_style_text_font(ranks[i],0)==&lv_font_montserrat_10);
    }
    add(0x6e,253,0); add(0x6f,7,0); add(0x50,0x46,0); add(0x51,0x34,0); add(0x3a,49,0);
    selection.display=analysis::Display::Both; refreshUi(); save("tablet-v6-both.ppm");
    assert(lv_obj_get_width(rows[0])==304);
    selection.display=analysis::Display::Parsed; refreshUi(); save("tablet-v6-parsed.ppm");
    bool found=false;
    for (unsigned i=0;i<visibleCount;++i) {
        const auto *v=visibleDecoded[i];
        if (!v || decoding::properties[v->property].low!=0x3a) continue;
        assert(!strcmp(lv_label_get_text(values[i]),"00110001"));
        assert(groups[i].count==2);
        assert(!strcmp(lv_label_get_text(groups[i].fields[0].name),"Thermal loop"));
        assert(lv_obj_has_flag(groups[i].fields[0].value,LV_OBJ_FLAG_HIDDEN));
        assert(lv_color_eq(lv_obj_get_style_text_color(groups[i].fields[0].name,0),lv_color_hex(Red)));
        assert(!strcmp(lv_label_get_text(groups[i].fields[1].value),"49 degC"));
        tapRow(i);
        assert(modal);
        auto *heading=lv_obj_get_child(modal,0);
        auto *body=lv_obj_get_child(modal,2);
        auto *address=lv_obj_get_child(body,0);
        auto *registers=lv_obj_get_child(body,1);
        auto *direction=lv_obj_get_child(body,2);
        assert(!strcmp(lv_label_get_text(lv_obj_get_child(address,1)),"0x75"));
        assert(!strcmp(lv_label_get_text(lv_obj_get_child(registers,1)),"0x3A"));
        assert(!strcmp(lv_label_get_text(lv_obj_get_child(registers,0)),"REGISTER"));
        assert(hasLabel(body,"Raw Value 0x31 / Dec 49"));
        assert(!strcmp(lv_label_get_text(lv_obj_get_child(direction,1)),"R / OK"));
        assert(!strstr(lv_label_get_text(heading),"DECODED PROPERTY"));
        auto *property=lv_obj_get_child(body,5);
        assert(strstr(lv_label_get_text(lv_obj_get_child(property,0)),"= 0 (R, inferred)"));
        const char *friendly=decoding::properties[visibleDecoded[i]->property].friendlydesc;
        assert(hasLabel(body,friendly));
        lv_area_t maskArea,descriptionArea;
        lv_obj_get_coords(lv_obj_get_child(property,2),&maskArea);
        lv_obj_get_coords(lv_obj_get_child(property,10),&descriptionArea);
        assert(maskArea.y2<descriptionArea.y1);
        save("tablet-v6-decoded-detail.ppm");
        lv_area_t headingArea,bodyArea;
        lv_obj_get_coords(heading,&headingArea); lv_obj_get_coords(body,&bodyArea);
        assert(headingArea.y2<bodyArea.y1 && headingArea.x2<266);
        closeDetail(nullptr);
        found=true;
    }
    assert(found);
    previewTime_ms+=3000; add(0x50,0x47,0); add(0x51,0x34,0); previewTime_ms+=5; add(0x3a,49,0);
    statusAt=millis();
    for (auto display:{analysis::Display::Raw,analysis::Display::Parsed,analysis::Display::Both}) {
        selection.display=display; selection.uniqueOrder=analysis::UniqueOrder::LatestTransaction; refreshUi();
        assert(visibleRaw[0]?visibleRaw[0]->reg==0x3a:decoding::properties[visibleDecoded[0]->property].low==0x3a);
        selection.uniqueOrder=analysis::UniqueOrder::LatestChange; groupedOnce=false; refreshUi();
        for (unsigned i=1;i<visibleCount;++i) assert(!alphabeticalBefore(i,i-1));
    }
    selection.display=analysis::Display::Parsed; selection.uniqueOrder=analysis::UniqueOrder::LatestTransaction; refreshUi();
    assert(groups[0].count==2);
    assert(lv_color_eq(lv_obj_get_style_bg_color(groups[0].fields[0].background,0),lv_color_hex(0x235742)));
    for (const char *id:{"r08_istop","r22_vbus_mode_set","battery_current_code_ma"}) {
        for (unsigned i=0;i<decoding::PropertyCount;++i) {
            if (strcmp(decoding::properties[i].id,id)) continue;
            analysis::Value value; value.property=i; value.complete=value.success=true;
            value.word=!strcmp(id,"r08_istop")?0x20:!strcmp(id,"r22_vbus_mode_set")?0xc0:0x07fd;
            decodedDetail(value); save(!strcmp(id,"r08_istop")?"tablet-v6-termination-detail.ppm":!strcmp(id,"r22_vbus_mode_set")?"tablet-v6-enum-detail.ppm":"tablet-v6-paired-detail.ppm");
            auto *body=lv_obj_get_child(modal,2);
            for (unsigned k=0;k<3;++k) {
                auto *tile=lv_obj_get_child(body,k);
                lv_area_t bounds; lv_obj_get_coords(tile,&bounds);
                for (unsigned n=0;n<2;++n) {
                    lv_area_t label; lv_obj_get_coords(lv_obj_get_child(tile,n),&label);
                    assert(label.x1>=bounds.x1 && label.x2<=bounds.x2 && label.y2<=bounds.y2);
                }
            }
            closeDetail(nullptr);
        }
    }
    for (unsigned i=0;i<visibleCount;++i) {
        for (unsigned k=0;k<groups[i].count;++k) {
            lv_area_t bg,name,value; lv_obj_get_coords(groups[i].fields[k].background,&bg);
            lv_obj_get_coords(groups[i].fields[k].name,&name); lv_obj_get_coords(groups[i].fields[k].value,&value);
            assert(name.x1>=bg.x1 && name.x2<=bg.x2 && name.y2<=bg.y2);
            assert(lv_obj_get_height(groups[i].fields[k].background)==22);
            if (!lv_obj_has_flag(groups[i].fields[k].value,LV_OBJ_FLAG_HIDDEN)) {
                assert(name.x2<value.x1 && value.x2<=bg.x2 && value.y2<=bg.y2 && name.y1==value.y1);
            }
        }
    }
    openFilter(nullptr); save("tablet-v6-filter.ppm");
    assert(lv_dropdown_get_selected(factorChoice)==0);
    lv_dropdown_set_selected(factorChoice,4); resetFilter(nullptr);
    assert(lv_dropdown_get_selected(factorChoice)==0);
    lv_dropdown_set_selected(orderChoice,1);
    lv_area_t kb; lv_obj_get_coords(keyboard,&kb); assert(kb.y1==368 && kb.y2==479);
    lv_textarea_set_text(addressInput,"");
    lv_buttonmatrix_set_selected_button(keyboard,7); lv_obj_send_event(keyboard,LV_EVENT_VALUE_CHANGED,nullptr);
    lv_buttonmatrix_set_selected_button(keyboard,5); lv_obj_send_event(keyboard,LV_EVENT_VALUE_CHANGED,nullptr);
    assert(!strcmp(lv_textarea_get_text(addressInput),"75"));
    lv_obj_send_event(registerInput,LV_EVENT_FOCUSED,nullptr);
    assert(lv_keyboard_get_textarea(keyboard)==registerInput);
    inputReplacing=true; lv_textarea_set_text(registerInput,"6E"); inputReplacing=false;
    lv_obj_send_event(registerInput,LV_EVENT_CLICKED,nullptr);
    assert(lv_textarea_text_is_selected(registerInput));
    lv_textarea_add_text(registerInput,"3A"); assert(!strcmp(lv_textarea_get_text(registerInput),"3A"));
    lv_obj_send_event(registerInput,LV_EVENT_CLICKED,nullptr); lv_textarea_set_cursor_pos(registerInput,LV_TEXTAREA_CURSOR_LAST);
    lv_textarea_add_text(registerInput,",6E"); assert(!strcmp(lv_textarea_get_text(registerInput),"3A,6E"));
    replaceInput=nullptr;
    lv_textarea_set_text(addressInput,"75"); lv_textarea_set_text(registerInput,"6E-6F,3A");
    lv_dropdown_set_selected(displayChoice,0); applyFilter(nullptr); assert(!modal);
    pump();
    for (uint8_t opcode:{uint8_t(5),uint8_t(8),uint8_t(5),uint8_t(4)}) {
        assert(control.opcode==opcode && control.pendingId); acknowledge();
    }
    refreshUi(); save("tablet-v6-filtered.ppm"); assert(visibleCount==3 && store->uniqueCount==28);
    assert(selection.uniqueOrder==analysis::UniqueOrder::LatestChange);
    openFilter(nullptr); lv_textarea_set_text(registerInput,"ZZ"); applyFilter(nullptr);
    assert(modal && selection.registers[0x6e]); closeDetail(nullptr);
    selection=analysis::Selection(); selection.display=analysis::Display::Raw;
    view=View::Diag; refreshUi(); save("tablet-v6-diag.ppm");
    assert(diagPanels[0].root && !strcmp(lv_label_get_text(diagPanels[0].values[0]),"1"));
    lv_obj_update_layout(diag);
    for (unsigned group=0;group<7;++group) {
        lv_area_t panel; lv_obj_get_coords(diagPanels[group].root,&panel);
        for (unsigned row=0;row<32 && diagPanels[group].values[row];++row) {
            lv_area_t value; lv_obj_get_coords(diagPanels[group].values[row],&value);
            assert(value.x1>=panel.x1 && value.x2<=panel.x2 && value.y2<=panel.y2);
        }
    }
    lv_obj_scroll_to_y(diag,lv_obj_get_y(diagPanels[1].root),LV_ANIM_OFF); save("tablet-v6-diag-timing.ppm");
    lv_obj_scroll_to_y(diag,lv_obj_get_y(diagPanels[5].root),LV_ANIM_OFF); save("tablet-v6-diag-transport.ppm");
    lv_obj_scroll_to_y(diag,lv_obj_get_y(diagPanels[6].root),LV_ANIM_OFF); save("tablet-v6-diag-rate.ppm");
    lv_obj_scroll_to_y(diag,0,LV_ANIM_OFF);
    for (unsigned i=0;i<decoding::PropertyCount;++i) {
        if (!decoding::properties[i].count) continue;
        analysis::Value value; value.property=i; value.word=decoding::properties[i].count==2?0xffff:0xff;
        value.complete=true; value.success=(i%2)==0;
        decodedDetail(value); lv_refr_now(nullptr);
        auto *body=lv_obj_get_child(modal,2);
        for (unsigned k=0;k<lv_obj_get_child_count(body);++k) {
            auto *tile=lv_obj_get_child(body,k);
            if (!lv_obj_check_type(tile,&lv_obj_class)) continue;
            lv_area_t bounds; lv_obj_get_coords(tile,&bounds);
            for (unsigned n=0;n<lv_obj_get_child_count(tile);++n) {
                auto *label=lv_obj_get_child(tile,n);
                if (!lv_obj_check_type(label,&lv_label_class)) continue;
                lv_area_t area; lv_obj_get_coords(label,&area);
                assert(area.x1>=bounds.x1 && area.x2<=bounds.x2 && area.y2<=bounds.y2);
            }
        }
        closeDetail(nullptr);
    }
    view=View::Unique; refreshUi(); tapRow(0); save("tablet-v6-detail.ppm");
    closeDetail(nullptr);
    for (unsigned pass=0;pass<40;++pass) {
        selection.display=analysis::Display::Parsed; add(0x3a,uint8_t(pass+20),0); refreshUi(); lv_refr_now(nullptr);
        selection.display=analysis::Display::Raw; refreshUi();
    }
    assert(!control.busy()); lv_obj_send_event(controls[1],LV_EVENT_CLICKED,nullptr);
    assert(control.action==observer::Action::Clear && control.resumeAfterClear);
    pump();
    receiver.bad=76; rxOverflow=459; sequence.gaps=3; transitionErrors=12; errorKinds[UART_FRAME_ERROR]=459;
    for (uint8_t opcode:{uint8_t(2),uint8_t(9),uint8_t(4),uint8_t(8),uint8_t(5),uint8_t(1)}) {
        assert(control.opcode==opcode && control.pendingId); acknowledge();
        if (opcode==9) assert(!receiver.bad && !rxOverflow && !sequence.gaps && !transitionErrors && !errorKinds[UART_FRAME_ERROR]);
    }
    assert(!control.busy() && !control.uncertain && status[0] && delivering);
    assert(store->uniqueCount==0 && decoder.count==0); refreshUi();
    assert(strstr(lv_label_get_text(runLabel),"PAUSE"));
    status[0]=0; refreshUi();
    assert(strstr(lv_label_get_text(runLabel),"RUN"));
    lv_obj_send_event(controls[1],LV_EVENT_CLICKED,nullptr); assert(!control.resumeAfterClear);
    pump(); for (uint8_t opcode:{uint8_t(2),uint8_t(9),uint8_t(4)}) {
        assert(control.opcode==opcode && control.pendingId); acknowledge();
    }
    assert(!control.busy() && !status[0]);
    status[0]=1; lv_obj_send_event(controls[1],LV_EVENT_CLICKED,nullptr); pump();
    acknowledge(); acknowledge(); assert(control.opcode==4);
    acknowledge(false); assert(control.uncertain && !control.busy() && !status[0]);
    control.abort("UART RX ERROR / CHECK DIAG"); refreshUi();
    assert(!strstr(lv_label_get_text(footer),"UART RX ERROR"));
    assert(lv_color_eq(lv_obj_get_style_text_color(linkIcon,0),lv_color_hex(Red)));
    assert(strstr(lv_label_get_text(linkIcon),LV_SYMBOL_WARNING));
    // A clean unplug/replug re-establishes a subscribed, paused session.
    statusAt=millis()-8001; pump(); refreshUi();
    assert(!sessionReady && needsSync && !delivering);
    assert(strstr(lv_label_get_text(footer),"Waiting for sniffer"));
    bootPeer[0]=1; bootPeer[3]=0;
    inject(3,bootPeer,sizeof(bootPeer)); pump();
    for (uint8_t opcode:{uint8_t(2),uint8_t(8),uint8_t(5),uint8_t(4)}) {
        assert(control.opcode==opcode && control.pendingId); acknowledge();
    }
    assert(sessionReady && delivering && !status[0]);
    // A failed verification must not declare readiness or silently retry.
    sessionReady=false; delivering=false;
    linkDetails(nullptr); pump();
    acknowledge(); acknowledge(); acknowledge(); assert(control.opcode==4);
    acknowledge(false); assert(!sessionReady && !delivering && control.uncertain);
    pump(); assert(!control.busy()); refreshUi();
    assert(strstr(lv_label_get_text(footer),"Tap LINK"));
    lv_obj_send_event(linkIcon,LV_EVENT_CLICKED,nullptr); pump();
    for (uint8_t opcode:{uint8_t(2),uint8_t(8),uint8_t(5),uint8_t(4)}) {
        assert(control.opcode==opcode && control.pendingId); acknowledge();
    }
    assert(sessionReady && !status[0]);
    lv_obj_send_event(controls[0],LV_EVENT_CLICKED,nullptr); pump();
    for (uint8_t opcode:{uint8_t(8),uint8_t(5),uint8_t(1)}) {
        assert(control.opcode==opcode && control.pendingId); acknowledge();
    }
    assert(status[0] && delivering && sessionReady);
    uint8_t liveRead[]={1,0,0,0,1,1,0x75,0x50,2,0,2,0,1,0,0,0xea,1,0x50,1,2,0,0,0,0,0xeb,1,0x46,0};
    uint32_t receivedBefore=store->received;
    inject(4,liveRead,sizeof(liveRead)); pump();
    assert(store->received==receivedBefore+1 && store->incoming.reg==0x50);
    // A held row is frozen, but capture/store updates continue. Click uses a snapshot.
    store->clear(); decoder.clear(); selection=analysis::Selection(); view=View::Unique;
    add(0x3a,49,0); refreshUi();
    assert(decoding::properties[visibleDecoded[0]->property].low==0x3a);
    lv_obj_send_event(rows[0],LV_EVENT_PRESSED,nullptr);
    ++previewTime_ms; add(0x17,0x80,0); refreshUi();
    assert(store->incoming.reg==0x17 && rowHeld);
    lv_obj_send_event(rows[0],LV_EVENT_RELEASED,nullptr);
    refreshUi(); // Allow reordering before CLICKED to exercise snapshot ownership.
    assert(decoding::properties[visibleDecoded[0]->property].low==0x17);
    lv_obj_send_event(rows[0],LV_EVENT_CLICKED,nullptr);
    auto *detailBody=lv_obj_get_child(modal,2);
    auto *registerTile=lv_obj_get_child(detailBody,1);
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(registerTile,1)),"0x3A"));
    closeDetail(nullptr);
    lv_obj_send_event(rows[0],LV_EVENT_PRESSED,nullptr);
    lv_obj_send_event(rows[0],LV_EVENT_PRESS_LOST,nullptr);
    lv_obj_send_event(rows[0],LV_EVENT_CLICKED,nullptr);
    assert(!modal && !rowHeld);
    refreshUi(); lv_refr_now(nullptr);
    uint32_t writes=ui_update::textWrites, styles=ui_update::styleWrites, passes=listPasses, roots=fieldRootsCreated;
    uint32_t transfers=previewTransfers;
    for (unsigned i=0;i<20;++i) { refreshUi(); lv_refr_now(nullptr); }
    assert(ui_update::textWrites==writes && ui_update::styleWrites==styles && listPasses==passes);
    assert(previewTransfers==transfers);
    openFilter(nullptr); add(0x3a,55,0); refreshUi();
    assert(listPasses==passes && fieldRootsCreated==roots);
    closeDetail(nullptr); refreshUi(); assert(listPasses==passes+1);
    // Time-only transitions still update highlights and stale indicators.
    previewTime_ms+=2200; statusAt=millis(); refreshUi();
    assert(listPasses==passes+2);
    previewTime_ms+=801; statusAt=millis(); refreshUi();
    assert(listPasses==passes+3);
    // Rebinding expanded rows reuses objects instead of delete/create churn.
    roots=fieldRootsCreated;
    for (unsigned i=0;i<20;++i) { add(i%2?0x17:0x3a,uint8_t(i),0); refreshUi(); }
    assert(fieldRootsCreated==roots);
    // A decoded snapshot also retains its value when the same key changes.
    store->clear(); decoder.clear(); add(0x3a,49,0); refreshUi();
    lv_obj_send_event(rows[0],LV_EVENT_PRESSED,nullptr);
    add(0x3a,60,0);
    assert(pressedValue.word==49);
    lv_obj_send_event(rows[0],LV_EVENT_RELEASED,nullptr);
    refreshUi(); lv_obj_send_event(rows[0],LV_EVENT_CLICKED,nullptr);
    assert(modal && pressedValue.word==49); closeDetail(nullptr);
    // Raw details must survive overwriting all history slots while held.
    selection.display=analysis::Display::Raw; view=View::Sniff;
    store->clear(); decoder.clear(); add(0x3a,49,0); refreshUi();
    lv_obj_send_event(rows[0],LV_EVENT_PRESSED,nullptr);
    for (unsigned i=0;i<70;++i) add(0x17,uint8_t(i),0);
    lv_obj_send_event(rows[0],LV_EVENT_RELEASED,nullptr);
    refreshUi(); lv_obj_send_event(rows[0],LV_EVENT_CLICKED,nullptr);
    assert(modal && store->detail.reg==0x3a && store->detail.preview[0]==49);
    closeDetail(nullptr);
    // Freeze scrolling/list layout even when the gesture starts on empty list space.
    refreshUi(); passes=listPasses; touchHeld=true; add(0x04,4,0); refreshUi();
    assert(listPasses==passes); touchHeld=false; refreshUi(); assert(listPasses==passes+1);
    // A busy opaque dialog still drains real encoded UART packets.
    openFilter(nullptr); receivedBefore=store->received;
    inject(4,liveRead,sizeof(liveRead)); pump(); refreshUi();
    assert(store->received==receivedBefore+1 && listPasses==passes+1);
    closeDetail(nullptr); refreshUi(); assert(listPasses==passes+2);
    // Complete bundles are expanded atomically, including error records.
    uint8_t batch[128]; add(0x3a,49,0);
    size_t compact=transport5::pack(store->incoming.raw,store->incoming.length,batch+2,sizeof(batch)-2);
    transport5::put16(batch,unsigned(compact)); size_t firstSize=compact+2;
    add(0x17,0x80,0); compact=transport5::pack(store->incoming.raw,store->incoming.length,batch+firstSize+2,sizeof(batch)-firstSize-2);
    transport5::put16(batch+firstSize,unsigned(compact));
    store->clear(); decoder.clear(); inject(5,batch,unsigned(firstSize+2+compact)); pump(); assert(store->received==2);
    batch[firstSize]=0xff; receivedBefore=store->received;
    inject(5,batch,unsigned(firstSize+2+compact)); pump(); assert(store->received==receivedBefore && store->malformed==1);
    analysis::Selection paired; paired.anyRegister=true; paired.registers[0x6f]=true; sourceFilter(paired);
    assert(control.filterCommand[20+0x6e/8]&(1u<<(0x6e%8)));
    assert(control.filterCommand[20+0x6f/8]&(1u<<(0x6f%8))); sourceFilter(selection);
    // New-baud confirmation waits for a CRC-valid beacon; never race UART re-init.
    lv_dropdown_set_selected(baudChoice,1); changeBaud(nullptr);
    assert(rateRequest && !rateConfirm); uint32_t request=rateRequest;
    uint8_t reply[6]; bringup::put32(reply,request); reply[4]=10; reply[5]=0;
    inject(2,reply,sizeof(reply)); pump();
    assert(rateConfirm && awaitingBaudBeacon && rateRequest==request && uart.currentBaud==1000000);
    assert(uart.rxFlushes>=2);
    uint8_t beacon[200]; memcpy(beacon,status,sizeof(beacon)); bringup::put32(beacon+136,1000000);
    inject(3,beacon,sizeof(beacon)); pump(); assert(!awaitingBaudBeacon && rateRequest!=request);
    bringup::put32(reply,rateRequest); reply[4]=11;
    inject(2,reply,sizeof(reply)); pump(); assert(!rateRequest && uartBaud==1000000);
    size_t outputBefore=uart.output.size(); previewTime_ms+=1001; statusAt=millis(); pump();
    assert(uart.output.size()>outputBefore); // Lease keepalive, independent of capture state.
    previewTime_ms+=3001; pump(); assert(uartBaud==115200 && uart.currentBaud==115200 && !sessionReady);
    // A 115200 status permits a fresh acknowledged standby synchronization.
    bringup::put32(beacon+136,115200); inject(3,beacon,sizeof(beacon)); pump();
    for (uint8_t opcode:{uint8_t(2),uint8_t(8),uint8_t(5),uint8_t(4)}) {
        assert(control.opcode==opcode && control.pendingId); acknowledge();
    }
    assert(sessionReady && !status[0]);
    // At default baud, the larger FIFO may delay a reply but not corrupt it.
    assert(control.begin(observer::Action::Pause)); pump();
    uint32_t pending=control.pendingId; assert(pending && control.timeout_ms==7000);
    previewTime_ms+=6000; statusAt=millis(); pump(); assert(control.pendingId==pending);
    acknowledge(); assert(!control.busy());
    // Failed new-speed handshakes retain stage diagnostics through recovery.
    lv_dropdown_set_selected(baudChoice,2); changeBaud(nullptr); request=rateRequest;
    bringup::put32(reply,request); reply[4]=10; reply[5]=0;
    inject(2,reply,sizeof(reply)); pump(); assert(awaitingBaudBeacon && uartBaud==2000000);
    pendingUartErrors.store(1); pendingErrorKinds[UART_FRAME_ERROR].store(1);
    pump(); assert(transitionErrors==1 && errorKinds[UART_FRAME_ERROR]==1 && rateRequest);
    previewTime_ms+=2501; pump(); assert(uartBaud==115200 && !strcmp(rateFailure,"NO NEW-RATE BEACON"));
    bringup::put32(beacon+136,115200); inject(3,beacon,sizeof(beacon)); pump();
    for (uint8_t opcode:{uint8_t(2),uint8_t(8),uint8_t(5),uint8_t(4)}) acknowledge();
    view=View::Diag; refreshUi();
    lv_obj_scroll_to_y(diag,lv_obj_get_y(diagPanels[6].root)+200,LV_ANIM_OFF); save("tablet-v6-diag-rate-failure.ppm");
    puts("PASS: stable press snapshots, canceled gestures, no idle UI writes, hidden-list deferral, timed expiry and field reuse");
    puts("PASS: compact bundle atomicity, paired source filters, baud beacon/confirmation, keepalive, fallback and resynchronization");
    puts("PASS: actual LVGL V7, expanded fields/bounds, column-major ranks, mode-switch stress, Clear touch state");
    puts("PASS: active-peer boot, reconnect standby/subscription, failed verification and touch recovery");
    // Automatic 2 Mbaud is attempted only after a verified paused session.
    defaultBaudAttempted=false; status[0]=1; statusAt=millis(); pump(); assert(!rateRequest);
    status[0]=0; pump(); assert(defaultBaudAttempted && rateRequest && rateTarget==2000000);
    request=rateRequest; bringup::put32(reply,request); reply[4]=10; reply[5]=0;
    inject(2,reply,sizeof(reply)); pump(); assert(uartBaud==2000000 && awaitingBaudBeacon);
    beacon[0]=0; bringup::put32(beacon+136,2000000); inject(3,beacon,sizeof(beacon)); pump();
    assert(rateRequest!=request && rateConfirm && !awaitingBaudBeacon);
    bringup::put32(reply,rateRequest); reply[4]=11; inject(2,reply,sizeof(reply)); pump();
    assert(!rateRequest && uartBaud==2000000 && !status[0]);
    previewTime_ms+=3001; pump(); assert(uartBaud==115200 && !sessionReady);
    bringup::put32(beacon+136,115200); inject(3,beacon,sizeof(beacon)); pump();
    for (uint8_t opcode:{uint8_t(2),uint8_t(8),uint8_t(5),uint8_t(4)}) acknowledge();
    pump(); assert(!rateRequest && sessionReady && !status[0]);
    puts("PASS: automatic 2 Mbaud after standby verification, lease fallback and no automatic retry/resume");
    // Recent membership changes only at the publication boundary; numeric
    // updates and changes within the same group cannot churn alphabetical rows.
    store->clear(); decoder.clear(); decoder.setChangeFactor(1);
    selection=analysis::Selection(); selection.uniqueOrder=analysis::UniqueOrder::LatestChange;
    view=View::Unique; groupedOnce=false; status[0]=1; statusAt=millis();
    lv_dropdown_set_selected(viewChoice,1);
    add(0x50,0x10,0); add(0x51,0x27,0); // 10000 mV.
    add(0x3a,30,0); add(0x78,0,0); add(0x79,4,0);
    refreshUi(); assert(visibleCount==3);
    assert(decoding::properties[visibleDecoded[0]->property].low==0x50);
    assert(decoding::properties[visibleDecoded[1]->property].low==0x3a);
    assert(decoding::properties[visibleDecoded[2]->property].low==0x78);
    previewTime_ms+=100; add(0x50,0x21,0); add(0x51,0x27,0); add(0x3a,31,0); refreshUi();
    assert(decoding::properties[visibleDecoded[0]->property].low==0x50);
    assert(decoding::properties[visibleDecoded[1]->property].low==0x3a);
    save("tablet-v7-stable-groups.ppm");
    previewTime_ms+=2300; statusAt=millis(); refreshUi();
    previewTime_ms+=100; add(0x78,20,0); add(0x79,4,0); refreshUi();
    assert(decoding::properties[visibleDecoded[0]->property].low==0x50);
    previewTime_ms+=1000; statusAt=millis(); touchHeld=true; refreshUi();
    assert(decoding::properties[visibleDecoded[0]->property].low==0x50);
    touchHeld=false; refreshUi();
    assert(decoding::properties[visibleDecoded[0]->property].low==0x78);
    save("tablet-v7-recent-group.ppm");
    view=View::Diag; refreshUi(); lv_obj_scroll_to_y(diag,0,LV_ANIM_OFF);
    lv_dropdown_set_selected(viewChoice,2);
    assert(strstr(lv_label_get_text(uartSummaryTitle),"ERRORS OBSERVED"));
    save("tablet-v7-diag-summary.ppm");
    puts("PASS: alphabetic group stability, live sub-step values, batched promotion and touch deferral");
    store->clear(); decoder.clear(); decoder.setChangeFactor(0);
    selection=analysis::Selection(); selection.uniqueOrder=analysis::UniqueOrder::LatestChange;
    view=View::Unique; groupedOnce=false; status[0]=1; statusAt=millis();
    lv_dropdown_set_selected(viewChoice,1);
    add(0x3a,30,0); refreshUi();
    assert(visibleCount==1 && groups[0].count==2);
    const auto started_ms=sourceChanged(*visibleDecoded[0]);
    assert(lv_color_eq(lv_obj_get_style_bg_color(rows[0],0),lv_color_hex(0x235742)));
    save("tablet-v8-fade-full.ppm");
    auto fadePasses=listPasses; auto fadeRoots=fieldRootsCreated;
    previewTime_ms+=500; refreshUi();
    assert(listPasses==fadePasses && fieldRootsCreated==fadeRoots);
    assert(lv_color_eq(lv_obj_get_style_bg_color(rows[0],0),lv_color_hex(highlight::color(Bg,0x235742,started_ms,visualNow_ms()))));
    add(0x3a,30,0); refreshUi();
    assert(sourceChanged(*visibleDecoded[0])==started_ms); // Unchanged polls do not restart fading.
    previewTime_ms+=600; refreshUi(); save("tablet-v8-fade-half.ppm");
    assert(lv_color_eq(lv_obj_get_style_bg_color(groups[0].fields[1].background,0),
        lv_color_hex(highlight::color(0x1a2023,0x235742,started_ms,visualNow_ms()))));
    add(0x3a,31,0); refreshUi();
    assert(sourceChanged(*visibleDecoded[0])==visualNow_ms());
    assert(lv_color_eq(lv_obj_get_style_bg_color(rows[0],0),lv_color_hex(0x655024)));
    previewTime_ms+=2200; refreshUi();
    assert(lv_color_eq(lv_obj_get_style_bg_color(rows[0],0),lv_color_hex(Bg)));
    save("tablet-v8-fade-expired.ppm");
    tapRow(0); save("tablet-v8-friendly-detail.ppm");
    assert(hasLabel(modal,decoding::properties[visibleDecoded[0]->property].friendlydesc));
    closeDetail(nullptr);
    puts("PASS: Parsed/Unique/Latest Change boot default; background fade, unchanged polls, refresh, expiry and no fade-only layout churn");
    statusAt=millis(); add(0x3a,32,0); refreshUi(); lv_refr_now(nullptr);
    lv_obj_send_event(controls[0],LV_EVENT_CLICKED,nullptr);
    assert(control.action==observer::Action::Pause && !visualClock.running);
    refreshUi(); lv_refr_now(nullptr);
    std::vector<uint16_t> pausedPixels(fb+132*320,fb+452*320);
    auto pausedTime=visualNow_ms(), pausedPasses=listPasses;
    previewTime_ms+=5000; statusAt=millis(); refreshUi(); lv_refr_now(nullptr);
    assert(visualNow_ms()==pausedTime && listPasses==pausedPasses);
    assert(!memcmp(pausedPixels.data(),fb+132*320,pausedPixels.size()*sizeof(uint16_t)));
    // Complete the pause handshake, then wait: transport still runs on wall time.
    pump(); acknowledge(); refreshUi(); assert(!status[0] && !visualClock.running);
    previewTime_ms+=5000; statusAt=millis(); refreshUi(); lv_refr_now(nullptr);
    assert(visualNow_ms()==pausedTime && listPasses==pausedPasses);
    assert(!memcmp(pausedPixels.data(),fb+132*320,pausedPixels.size()*sizeof(uint16_t)));
    status[0]=1; refreshUi(); assert(visualNow_ms()==pausedTime);
    previewTime_ms+=500; statusAt=millis(); refreshUi(); assert(visualNow_ms()==pausedTime+500);
    puts("PASS: Pause freezes list pixels, fades, ordering and presentation time immediately and after ACK; resume continues from the frozen age");
    store->clear(); decoder.clear(); selection=analysis::Selection();
    selection.uniqueOrder=analysis::UniqueOrder::LatestChange;
    view=View::Unique; groupedOnce=false; status[0]=1; statusAt=millis();
    add(0x22,0,0); refreshUi();
    previewTime_ms+=2300; statusAt=millis(); refreshUi();
    auto originalChange=sourceChanged(*visibleDecoded[0]);
    add(0x22,0,0); refreshUi();
    auto pollAt=visualNow_ms();
    assert(sourceChanged(*visibleDecoded[0])==originalChange);
    assert(lv_color_eq(lv_obj_get_style_bg_color(rows[0],0),lv_color_hex(0x235742)));
    auto pollPasses=listPasses;
    previewTime_ms+=225; refreshUi();
    assert(listPasses==pollPasses);
    assert(lv_color_eq(lv_obj_get_style_bg_color(rows[0],0),lv_color_hex(highlight::fade(Bg,0x235742,pollAt,visualNow_ms(),450))));
    previewTime_ms+=226; refreshUi();
    assert(lv_color_eq(lv_obj_get_style_bg_color(rows[0],0),lv_color_hex(Bg)));
    add(0x22,0,0); refreshUi(); save("tablet-v19-unique-poll.ppm");
    add(0x22,0x40,0); refreshUi();
    auto changedAt=visualNow_ms();
    previewTime_ms+=125; add(0x22,0x40,0); refreshUi();
    assert(sourceChanged(*visibleDecoded[0])==changedAt);
    assert(lv_color_eq(lv_obj_get_style_bg_color(rows[0],0),lv_color_hex(highlight::color(Bg,0x655024,changedAt,visualNow_ms()))));
    for(auto display:{analysis::Display::Raw,analysis::Display::Parsed}) {
        store->clear(); decoder.clear(); selection.display=display; view=View::Sniff;
        add(0x22,0,0); refreshUi();
        previewTime_ms+=100; add(0x22,0,0); refreshUi();
        auto firstPoll=visualNow_ms();
        assert(lv_color_eq(lv_obj_get_style_bg_color(rows[0],0),lv_color_hex(0x235742)));
        assert(lv_color_eq(lv_obj_get_style_bg_color(rows[1],0),lv_color_hex(Bg)));
        previewTime_ms+=325; add(0x22,0,0); refreshUi();
        assert(lv_color_eq(lv_obj_get_style_bg_color(rows[0],0),lv_color_hex(0x235742)));
        assert(lv_color_eq(lv_obj_get_style_bg_color(rows[1],0),lv_color_hex(highlight::fade(Bg,0x235742,firstPoll,visualNow_ms(),450))));
        save(display==analysis::Display::Raw?"tablet-v19-sniff-raw-poll.ppm":"tablet-v19-sniff-parsed-poll.ppm");
    }
    puts("PASS: Unique and Sniff unchanged polling pulses fade independently, preserve change priority and avoid fade-only layout churn");
    // Profile selection continues receiving traffic and never replaces another modal.
    decoder.clear(); registry.reset(); store->clear(); invalidateProfileView();
    status[0]=1; statusAt=millis(); seen=true; sessionReady=true; view=View::Unique;
    selection=analysis::Selection();
    registry.observe(0x75,true); registry.observe(0x40,true);
    refreshUi(); assert(modal && profileAddress==0x40);
    save("tablet-v9-profile-unknown.ppm");
    auto *original=modal; refreshUi(); assert(modal==original);
    lv_dropdown_set_selected(profileChoice,registry.find("ip2369")+2); applyProfile(nullptr);
    assert(!modal && registry.bindings[0x40].profile==registry.find("ip2369"));
    assert(registry.bindings[0x40].dirty); refreshUi();
    assert(modal && profileAddress==0x75); save("tablet-v9-profile-ambiguous.ppm");
    lv_dropdown_set_selected(profileChoice,registry.find("ip2366")+2); applyProfile(nullptr);
    assert(registry.bindings[0x75].profile==registry.find("ip2366"));
    assert(Preferences::saved.empty()); // No flash writes during capture.
    status[0]=0; refreshUi(); assert(Preferences::saved["a75"]=="ip2366" && Preferences::saved["a40"]=="ip2369");
    status[0]=1; // Fresh transactions are delivered only after capture resumes.
    add(0x3a,30,0); refreshUi(); tapRow(0); assert(modal); save("tablet-v9-profile-detail.ppm");
    assert(hasLabel(modal,"CHANGE PROFILE")); closeDetail(nullptr);
    selection.display=analysis::Display::Raw; registry.observe(0x42,false); refreshUi(); assert(!modal);
    assert(registry.nextPrompt()==0x42);
    rawDetail(store->incoming); save("tablet-v9-raw-detail.ppm"); assert(!hasLabel(modal,"ASSIGN PROFILE")); closeDetail(nullptr);
    selection.display=analysis::Display::Both;
    auto unknown=store->incoming; unknown.address=0x42;
    rawDetail(unknown); assert(hasLabel(modal,"ASSIGN PROFILE")); save("tablet-v9-unparsed-detail.ppm");
    registry.observe(0x43,true); refreshUi(); assert(modal && profileAddress==0x42);
    lv_dropdown_set_selected(profileChoice,1); applyProfile(nullptr); refreshUi();
    assert(modal && profileAddress==0x43); deferProfile(nullptr); refreshUi(); assert(!modal);
    assert(registry.bindings[0x42].mode==decoding::Assignment::Disabled);
    openProfile(0x40); lv_dropdown_set_selected(profileChoice,registry.find("ip2366")+2); applyProfile(nullptr);
    assert(registry.bindings[0x40].profile==registry.find("ip2366"));
    selection.display=analysis::Display::Raw; status[0]=0; refreshUi();
    decoder.clear(); registry.reset(); profileStorage.load(registry);
    assert(registry.bindings[0x40].profile==registry.find("ip2366"));
    puts("PASS: LVGL unknown/ambiguous chooser, modal queue, multiple addresses, paused persistence, raw-mode suppression, disabled parsing, address-wide change and reload");
    decoder.clear(); registry.reset();
    assert(registry.assign(0x75,registry.find("ip2368"),decoding::Assignment::Manual));
    selection=analysis::Selection{}; selection.display=analysis::Display::Both;
    selection.anyRegister=true; selection.registers[0x75]=true; sourceFilter(selection);
    for (unsigned r=0x74;r<=0x76;++r) assert(control.filterCommand[20+r/8]&(1u<<(r%8)));
    analysis::Value power;
    for (unsigned i=0;i<decoding::PropertyCount;++i) if (decoding::properties[i].count==3) { power.property=i; break; }
    power.word=200000; power.complete=power.success=true;
    decodedDetail(power); save("tablet-v9-ip2368-power-detail.ppm");
    assert(hasLabel(modal,"Raw Value 0x030D40 / Dec 200000\nBytes 40 0D 03 (low first)"));
    assert(hasLabel(modal,"MASK 0xFFFFFF / SHIFT 0")); closeDetail(nullptr);
    puts("PASS: IP2368 three-byte companion filter and actual LVGL 24-bit power detail rendering");
    decoder.clear(); registry.reset(); store->clear(); control=observer::Control{};
    assert(registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual));
    selection=analysis::Selection{}; status[0]=1; statusAt=millis();
    updateReportOptions(); assert(reportOptionCount==2 && reportModel.count==86);
    assert(reportColumns[0]==0 && reportColumns[19]==0 && reportColumns[20]==1 && reportColumns[39]==1 && reportColumns[40]==0);
    assert(reportPositions[0]==reportPositions[20] && reportPositions[40]==320);
    assert(lv_obj_has_state(reportReadMissing,LV_STATE_DISABLED) && lv_obj_has_state(reportRefreshAll,LV_STATE_DISABLED));
    navigateReport(true); lv_anim_refr_now(); previewTime_ms+=250;lv_anim_refr_now();
    lv_obj_update_layout(lv_screen_active());
    assert(reportActive && !sliding && lv_obj_get_x(reportScreen)==0 && lv_obj_get_x(monitorScreen)==-320);
    refreshReports();save("tablet-v10-all-unread.ppm");
    assert(!strcmp(lv_label_get_text(reportWidgets[0].line),decoding::properties[reportModel.rows[0].property].name));
    assert(lv_color_eq(lv_obj_get_style_bg_color(reportWidgets[0].root,0),lv_color_hex(0)));
    reportSelected=1;lv_dropdown_set_selected(reportChoice,1);buildReport();
    assert(reportModel.count==3);
    add(0x50,0x10,0);add(0x51,0x27,0);add(0x6e,0xd0,0);add(0x6f,7,0);add(3,4,0);
    refreshReports();save("tablet-v10-battery-fresh.ppm");
    assert(!strcmp(lv_label_get_text(reportWidgets[0].line),"10000 mV Battery voltage"));
    assert(strstr(lv_label_get_text(reportWidgets[1].line),"2000 mA ")==lv_label_get_text(reportWidgets[1].line));
    previewTime_ms+=2300;statusAt=millis();refreshReports();save("tablet-v10-battery-settled.ppm");
    assert(lv_color_eq(lv_obj_get_style_bg_color(reportWidgets[0].root,0),lv_color_hex(0xb8dfbc)));
    add(0x50,0x10,0);add(0x51,0x27,0);refreshReports();
    assert(lv_color_eq(lv_obj_get_style_bg_color(reportWidgets[0].root,0),lv_color_hex(0xb8dfbc)));
    status[0]=0;updateVisualClock();refreshReports();lv_refr_now(nullptr);
    std::vector<uint16_t> reportPixels(fb+124*320,fb+452*320);
    previewTime_ms+=5000;statusAt=millis();refreshReports();lv_refr_now(nullptr);
    assert(!memcmp(reportPixels.data(),fb+124*320,reportPixels.size()*2));
    observer::Observation failure;failure.address=0x75;failure.reg=0x51;failure.direction='R';failure.associated=true;
    reportCache.observeFailure(failure);reportDirty=true;refreshReports();save("tablet-v10-battery-failed.ppm");
    assert(strstr(lv_label_get_text(reportWidgets[0].line),"FAIL 10000 mV ")==lv_label_get_text(reportWidgets[0].line));
    assert(!lv_obj_has_flag(reportWidgets[0].bold,LV_OBJ_FLAG_HIDDEN));
    assert(lv_color_eq(lv_obj_get_style_text_color(reportWidgets[0].line,0),lv_color_hex(0xb00020)));
    assert(lv_color_eq(lv_obj_get_style_bg_color(reportWidgets[0].root,0),lv_color_hex(0xffffff)));
    lv_obj_send_event(reportWidgets[0].root,LV_EVENT_PRESSED,nullptr);
    lv_obj_send_event(reportWidgets[0].root,LV_EVENT_RELEASED,nullptr);
    lv_obj_send_event(reportWidgets[0].root,LV_EVENT_CLICKED,nullptr);assert(modal);closeDetail(nullptr);
    selection.anyAddress=selection.anyRegister=true;selection.addresses[0x40]=true;selection.registers[0x17]=true;
    selection.read=false;selection.write=true;selection.outcome=2;sourceFilter(selection);
    assert(control.filterCommand[2]==3 && control.filterCommand[3]==0);
    for (unsigned a:{0x40u,0x75u}) assert(control.filterCommand[4+a/8]&(1u<<(a%8)));
    for (unsigned r:{0x17u,0x50u,0x51u,0x6eu,0x6fu}) assert(control.filterCommand[20+r/8]&(1u<<(r%8)));
    assert(!selection.read && selection.outcome==2 && !selection.addresses[0x75]);
    reportSelected=0;buildReport();lv_refr_now(nullptr);lv_obj_scroll_to_y(reportBody,reportExtent,LV_ANIM_OFF);
    lv_refr_now(nullptr);reportDirty=true;refreshReports();save("tablet-v10-all-scrolled.ppm");
    assert(reportWidgets[0].row>=40);
    unsigned flagRow=0;
    for (;flagRow<reportModel.count;++flagRow) {
        auto &p=decoding::properties[reportModel.rows[flagRow].property];
        if (p.low==0x3a && p.mask==0x80 && p.bit0) break;
    }
    assert(flagRow<reportModel.count);
    auto flagProperty=reportModel.rows[flagRow].property;
    lv_obj_scroll_to_y(reportBody,reportPositions[flagRow],LV_ANIM_OFF);lv_obj_update_layout(reportBody);
    auto flagWidget=[&]() -> ReportRowUi & { for (auto &ui:reportWidgets) if (!lv_obj_has_flag(ui.root,LV_OBJ_FLAG_HIDDEN) && ui.row==flagRow) return ui; assert(false); return reportWidgets[0]; };
    analysis::Value flag;flag.property=flagProperty;flag.complete=flag.success=true;flag.word=0;flag.observed_ms=visualNow_ms();
    reportCache.accept(flag);reportDirty=true;refreshReports();
    assert(lv_color_eq(lv_obj_get_style_bg_color(flagWidget().root,0),lv_color_hex(0xe8b6b8)));
    flag.word=0x80;reportCache.accept(flag);reportDirty=true;refreshReports();
    assert(lv_color_eq(lv_obj_get_style_bg_color(flagWidget().root,0),lv_color_hex(0xb8dfbc)));
    auto &flagDefinition=decoding::properties[flagProperty];auto *zeroMeaning=flagDefinition.bit0;auto *oneMeaning=flagDefinition.bit1;
    flagDefinition.bit0=oneMeaning;flagDefinition.bit1=zeroMeaning;flag.word=0;
    reportCache.accept(flag);reportDirty=true;refreshReports();
    assert(lv_color_eq(lv_obj_get_style_bg_color(flagWidget().root,0),lv_color_hex(0xb8dfbc)));
    flagDefinition.bit0=zeroMeaning;flagDefinition.bit1=oneMeaning;
    save("tablet-v10-single-line-flags.ppm");
    unsigned conflictRow=0;
    for (;conflictRow<reportModel.count;++conflictRow)
        if (!strcmp(decoding::properties[reportModel.rows[conflictRow].property].name,"Set_BATlow")) break;
    assert(conflictRow<reportModel.count);
    analysis::Value conflictValue;conflictValue.property=reportModel.rows[conflictRow].property;
    conflictValue.success=conflictValue.complete=true;conflictValue.word=0x40;
    assert(!decoding::properties[conflictValue.property].conflict);
    decoding::properties[conflictValue.property].conflict=true;
    reportCache.accept(conflictValue);
    lv_obj_scroll_to_y(reportBody,reportPositions[conflictRow],LV_ANIM_OFF);lv_obj_update_layout(reportBody);
    reportDirty=true;refreshReports();
    for (auto &ui:reportWidgets) if (!lv_obj_has_flag(ui.root,LV_OBJ_FLAG_HIDDEN) && ui.row==conflictRow) {
        assert(strstr(lv_label_get_text(ui.line),"2700 mV CONFLICT"));
        assert(!strstr(lv_label_get_text(ui.line),"FAIL"));
        assert(lv_color_eq(lv_obj_get_style_bg_color(ui.root,0),lv_color_hex(0xe6ca86)));
    }
    save("tablet-v12-report-conflict.ppm");
    decodedDetail(reportCache.readings[conflictValue.property].lastGood);
    assert(!hasLabel(modal,"-- INCOMPLETE"));save("tablet-v12-conflict-details.ppm");closeDetail(nullptr);
    decoding::properties[conflictValue.property].conflict=false;
    decoder.clear();registry.reset();assert(registry.assign(0x75,registry.find("ip2369"),decoding::Assignment::Manual));
    updateReportOptions();reportSelected=0;buildReport();unsigned adjustRow=0;
    for (;adjustRow<reportModel.count;++adjustRow) if (!strcmp(decoding::properties[reportModel.rows[adjustRow].property].name,"Adjust_En")) break;
    assert(adjustRow<reportModel.count);analysis::Value adjust;adjust.property=reportModel.rows[adjustRow].property;adjust.complete=adjust.success=true;
    reportCache.accept(adjust);lv_obj_scroll_to_y(reportBody,reportPositions[adjustRow],LV_ANIM_OFF);lv_obj_update_layout(reportBody);
    reportDirty=true;refreshReports();lv_refr_now(nullptr);
    for (auto &ui:reportWidgets) if (!lv_obj_has_flag(ui.root,LV_OBJ_FLAG_HIDDEN) && ui.row==adjustRow) {
        assert(strstr(lv_label_get_text(ui.line),"Subtract correction")==lv_label_get_text(ui.line));
        assert(lv_obj_get_width(ui.line)==141);
    }
    save("tablet-v10-single-line-adjust.ppm");
    registry.reset();updateReportOptions();refreshReports();
    for (auto &widget:reportWidgets) assert(lv_obj_has_flag(widget.root,LV_OBJ_FLAG_HIDDEN));
    navigateReport(false);lv_anim_refr_now();previewTime_ms+=250;lv_anim_refr_now();lv_obj_update_layout(lv_screen_active());assert(!reportActive && !sliding);
    puts("PASS: Reports slide navigation, All/Battery runtime pages, empty/fresh/settled/failed colors, unchanged freshness, frozen pause, details, virtual scroll, removed profiles and independent source union");
    registry.reset();assert(registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual));
    control=observer::Control{};sessionReady=true;seen=true;statusAt=millis();bringup::put32(status+132,63);
    reportSettings(nullptr);assert(!hasLabel(modal,"Active reads / this session"));save("tablet-v12-active-settings.ppm");closeDetail(nullptr);
    unsigned voltage=0;while (voltage<decoding::PropertyCount && (!activeEligible(voltage) || decoding::properties[voltage].low!=0x50)) ++voltage;
    assert(voltage<decoding::PropertyCount && activeEligible(flagProperty));
    analysis::Value activeValue;activeValue.property=voltage;activeValue.direction='R';
    decodedDetail(activeValue);assert(hasLabel(modal,"Read"));buttonColor(modal,"Read",0x238557);closeDetail(nullptr);
    activeSession=0;decodedDetail(activeValue);activeAction(nullptr);
    assert(activePending && hasLabel(modal,"Cancel read"));buttonColor(modal,"Cancel read",0x9c3544);
    uint8_t result[32]={};bringup::put32(result,activePending);result[5]=2;result[6]=1;result[7]=0x51;
    result[24]=0x34;result[25]=0x12;result[27]=2;result[29]=0x75;result[30]=2;result[31]=0x50;bringup::put32(result+12,10000);
    activePacket({6,1,result,32});assert(!activePending && reportCache.readings[voltage].lastGood.word==0x1234);
    assert(hasLabel(modal,"Read"));
    activeUiTick();assert(hasLabel(modal,"Raw Value 0x1234 / Dec 4660\nBytes 34 12 (low first)"));
    lv_obj_update_layout(modal);auto *activeBody=lv_obj_get_parent(lv_obj_get_parent(activeLabel));
    lv_obj_scroll_to_y(activeBody,lv_obj_get_y(lv_obj_get_parent(activeLabel))-8,LV_ANIM_OFF);
    save("tablet-v11-active-success.ppm");
    activeAction(nullptr);assert(activePending);bringup::put32(result,activePending);result[4]=2;result[5]=1;
    activePacket({6,2,result,32});assert(reportCache.readings[voltage].failed && reportCache.readings[voltage].lastGood.word==0x1234);
    assert(strstr(activeMessage,"No complete value"));save("tablet-v11-active-failure.ppm");closeDetail(nullptr);
    assert(!activeLabel && !activeButtonCaption);
    unsigned activeCases=0;
    for (unsigned chip=0;chip<decoding::ProfileCount;++chip) {
        registry.reset();assert(registry.assign(0x42,int(chip),decoding::Assignment::Manual));
        std::vector<std::pair<unsigned,uint32_t>> acquired;
        for (unsigned index=0;index<decoding::PropertyCount;++index) {
            if (!activeEligible(index)) continue;
            const auto definition=decoding::properties[index];
            activeProperty=index;uart.output.clear();activeAction(nullptr);assert(activePending);
            assert(activeRequestedProperty==index && activeDefinition.address==0x42);
            auto captureGap=[&]() {
                uint8_t peer[200];memcpy(peer,status,sizeof(peer));
                bringup::put32(peer+4,val(4)+1);
                inject(3,peer,sizeof(peer));pump();
                for (const auto &previous:acquired) {
                    const auto &reading=reportCache.readings[previous.first];
                    assert(reading.valid && reading.lastGood.complete && reading.lastGood.word==previous.second);
                }
            };
            // Real capture restarts change epochs on subsequent active reads.
            captureGap();
            // Inspect the exact COBS request emitted by the actual tablet firmware.
            uint8_t raw[64]={};size_t at=0,used=0;
            while (at+1<uart.output.size()) {
                unsigned code=uart.output[at++];assert(code && at+code-1<=uart.output.size()-1);
                for (unsigned n=1;n<code;++n) raw[used++]=uart.output[at++];
                if (code!=255 && at<uart.output.size()-1) raw[used++]=0;
            }
            assert(raw[3]==1 && bringup::le16(raw+8)==11 && raw[10]==15 && raw[16]==0x42 && raw[17]==definition.count);
            for (unsigned n=0;n<definition.count;++n) assert(raw[18+n]==definition.low+n);
            for (unsigned n=definition.count;n<3;++n) assert(raw[18+n]==0);
            memset(result,0,sizeof(result));bringup::put32(result,activePending);
            result[5]=definition.count;result[6]=1;result[7]=definition.low+definition.count-1;
            result[24]=0x34;result[25]=definition.count>1?0x12:0;result[28]=definition.count>2?0x56:0;
            result[27]=2;result[29]=0x42;result[30]=definition.count;result[31]=definition.low;bringup::put32(result+12,10000);
            // Opening a different property during the job must not redirect its result.
            activeProperty=(index+1)%decoding::PropertyCount;
            activePacket({6,3,result,32});assert(!activePending && !reportCache.readings[index].failed);
            assert(reportCache.readings[index].lastGood.word==(definition.count==1?0x34:definition.count==2?0x1234:0x561234));
            acquired.push_back({index,reportCache.readings[index].lastGood.word});
            captureGap();
            ++activeCases;
        }
    }
    assert(activeCases==decoding::DefinitionCount && activeCases==283);
    printf("PASS: %u real UI-issued active reads across all three profiles, exact requests, relocated addresses, 8/16/24-bit results and stable request ownership\n",activeCases);
    puts("PASS: successive active reads and real UART capture-epoch changes retain all previously acquired report values");
    decoder.clear();
    for (const auto &reading:reportCache.readings) assert(!reading.valid);
    activeProperty=0;while (!activeEligible(activeProperty)) ++activeProperty;
    activeAction(nullptr);assert(activePending);const auto oldDefinition=activeDefinition;
    memset(result,0,sizeof(result));bringup::put32(result,activePending);result[27]=2;result[6]=1;
    result[29]=oldDefinition.address;result[30]=result[5]=oldDefinition.count;result[31]=oldDefinition.low;
    result[7]=oldDefinition.low+oldDefinition.count-1;result[24]=0x77;
    registry.reset();assert(registry.assign(0x42,registry.find("ip2366"),decoding::Assignment::Manual));
    reportCache.clear();activePacket({6,4,result,32});assert(!activePending && strstr(activeMessage,"profile changed"));
    for (const auto &reading:reportCache.readings) assert(!reading.attempted);
    // A mismatched result cannot be applied to the selected register.
    activeProperty=0;while (!activeEligible(activeProperty)) ++activeProperty;
    activeAction(nullptr);assert(activePending);bringup::put32(result,activePending);result[29]=0x43;
    activePacket({6,5,result,32});assert(!activePending && !sessionReady && needsSync);
    sessionReady=true;needsSync=false;
    activePending=123;activeAt_ms=millis();previewTime_ms+=15001;activeUiTick();
    assert(!activePending && !sessionReady && needsSync);
    puts("PASS: explicit Read without settings activation, capability/eligibility, complete result, partial failure retains last good, modal pointer cleanup and timeout without retry");
    // Exercise the real report buttons and serial result dispatch, not just plans.
    registry.reset();decoder.clear();
    assert(registry.assign(0x42,registry.find("ip2366"),decoding::Assignment::Manual));
    updateReportOptions();control=observer::Control{};
    seen=sessionReady=true;needsSync=false;statusAt=millis();status[0]=status[1]=status[2]=0;bringup::put32(status+132,63);
    activePending=0;reportBatchTick();
    unsigned known=0;while (known<decoding::PropertyCount && (!decoding::properties[known].count || decoding::properties[known].low!=0x50)) ++known;
    analysis::Value knownValue{};knownValue.property=known;knownValue.complete=knownValue.success=true;knownValue.direction='R';knownValue.word=12345;
    reportCache.accept(knownValue);
    acquisition::Batch expected;assert(expected.build(reportModel,reportCache,true,registry.generation));
    unsigned missingCount=expected.count;
    auto batchReply=[&](uint8_t code=0) {
        assert(activePending);const auto def=activeDefinition;
        uint8_t reply[32]={};bringup::put32(reply,activePending);
        reply[4]=code;reply[5]=code?0:def.count;reply[6]=1;reply[7]=def.low+def.count-1;
        reply[24]=0x12;reply[25]=def.count>1?0x34:0;reply[28]=def.count>2?0x56:0;
        reply[26]=status[0];reply[27]=2;reply[29]=def.address;reply[30]=def.count;reply[31]=def.low;
        inject(6,reply,sizeof(reply));pump();
        uint8_t peer[200];memcpy(peer,status,sizeof(peer));bringup::put32(peer+4,val(4)+1);
        inject(3,peer,sizeof(peer));pump();
    };
    lv_obj_send_event(reportReadMissing,LV_EVENT_CLICKED,nullptr);
    assert(reportBatch.running && activePending && hasLabel(reportReadMissing,"CANCEL") && hasLabel(reportRefreshAll,"CANCEL"));
    assert(lv_color_eq(lv_obj_get_style_bg_color(reportReadMissing,0),lv_color_hex(0x9c3544)));
    assert(lv_color_eq(lv_obj_get_style_bg_color(reportRefreshAll,0),lv_color_hex(0x9c3544)));
    unsigned reads=0;bool visited[256]={};
    while (reportBatch.running) {
        assert(activePending && activeDefinition.low!=0x50 && !visited[activeDefinition.low]);
        visited[activeDefinition.low]=true;assert(++reads<=256);batchReply();
    }
    assert(reads==missingCount && reportBatch.completed==reads && !reportBatch.failed && !activeBusy());
    assert(hasLabel(reportRefreshAll,"Read All") && lv_color_eq(lv_obj_get_style_bg_color(reportReadMissing,0),lv_color_hex(0x238557)));
    assert(reportCache.readings[known].lastGood.word==12345 && status[0]==0);
    lv_obj_send_event(reportReadMissing,LV_EVENT_CLICKED,nullptr);assert(!activeBusy());
    assert(!strcmp(reportBatchMessage,"No missing values"));
    // Read All refreshes cached values too, with capture running throughout each restore.
    status[0]=1;reportBatchTick();lv_obj_send_event(reportRefreshAll,LV_EVENT_CLICKED,nullptr);
    navigateReport(true);lv_anim_refr_now();previewTime_ms+=250;lv_anim_refr_now();
    lv_obj_scroll_to_y(reportBody,0,LV_ANIM_OFF);refreshReports();save("tablet-v13-report-reading.ppm");
    unsigned allCount=reportBatch.count;reads=0;
    while (reportBatch.running) { assert(++reads<=256);batchReply(); }
    assert(reads==allCount && allCount>missingCount && status[0]==1);
    assert(reportCache.readings[known].valid && reportCache.readings[known].lastGood.word==0x3412);
    // A NACK is local to one read; missing retries it, with no automatic repeat.
    lv_obj_send_event(reportRefreshAll,LV_EVENT_CLICKED,nullptr);unsigned failedProperty=activeRequestedProperty;
    batchReply(2);assert(reportBatch.running && reportBatch.failed==1);
    while (reportBatch.running) batchReply();
    assert(reportCache.readings[failedProperty].failed);
    lv_obj_send_event(reportReadMissing,LV_EVENT_CLICKED,nullptr);assert(reportBatch.count==1);batchReply();
    assert(!reportBatch.running && !reportCache.readings[failedProperty].failed);
    // Cancel and arbitration failure must not issue the next register request.
    lv_obj_send_event(reportRefreshAll,LV_EVENT_CLICKED,nullptr);
    lv_obj_send_event(reportReadMissing,LV_EVENT_CLICKED,nullptr);assert(reportBatch.cancel && activePending);
    batchReply(6);assert(!activeBusy() && reportBatch.completed==1);
    lv_obj_send_event(reportRefreshAll,LV_EVENT_CLICKED,nullptr);batchReply(4);
    assert(!activeBusy() && reportBatch.completed==1 && reportBatch.failed==1);
    lv_obj_send_event(reportRefreshAll,LV_EVENT_CLICKED,nullptr);assert(activePending);
    previewTime_ms+=15001;activeUiTick();
    assert(!activeBusy() && !sessionReady && needsSync);
    statusAt=millis();sessionReady=true;needsSync=false;reportBatchTick();
    lv_obj_send_event(reportRefreshAll,LV_EVENT_CLICKED,nullptr);assert(activePending);
    decoder.clearAddress(0x42); // The real profile-selection callback clears this address.
    assert(registry.assign(0x42,registry.find("ip2369"),decoding::Assignment::Manual));
    reportBatchTick();assert(reportBatch.cancel);batchReply();assert(!activeBusy());
    for (const auto &reading:reportCache.readings) assert(!reading.valid);
    save("tablet-v13-report-batch.ppm");
    puts("PASS: real Read Missing/Read All buttons, register deduplication, cached-value exclusion/refresh, epoch retention, paused/running restore, NACK recovery, cancellation, arbitration/timeout stop and profile-change cancellation");
    #include "experiment_cases.h"
    #include "graph_v22_cases.h"
    #include "address_evidence_cases.h"
    #include "discovery_cases.h"
    #include "recorder_write_cases.h"
    #include "manual_read_cases.h"
    #include "profile_upload_cases.h"
    #include "start_cases.h"
    #include "publication_cases.h"
}
