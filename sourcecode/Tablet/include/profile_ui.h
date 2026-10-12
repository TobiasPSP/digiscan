// Included inside the tablet implementation namespace.
// Address-level profile selection and property explanation dialogs. Binding one
// address must not alter another. Raw mode ignores profiles; parsed modes offer
// assignment when evidence cannot select one unambiguously.
uint8_t profileAddress=0;
lv_obj_t *profileChoice=nullptr, *profileMessage=nullptr;
bool resetProfilesConfirmed=false;
void removeFields(unsigned);
void profileOptions(lv_obj_t *choice,uint8_t address) {
    lv_dropdown_clear_options(choice);
    lv_dropdown_add_option(choice,"Choose a profile...",LV_DROPDOWN_POS_LAST);
    char line[96]; snprintf(line,sizeof(line),"No parsing for address 0x%02X",address);
    lv_dropdown_add_option(choice,line,LV_DROPDOWN_POS_LAST);
    for (unsigned i=0;i<decoding::ProfileCount;++i) lv_dropdown_add_option(choice,decoding::profiles[i].name,LV_DROPDOWN_POS_LAST);
    const auto &b=registry.bindings[address];
    lv_dropdown_set_selected(choice,b.mode==decoding::Assignment::Disabled?1:b.profile>=0?unsigned(b.profile)+2:0);
    ui_update::bgColor(choice,lv_color_hex(0x2c363b),0); ui_update::textColor(choice,lv_color_hex(Ink),0);
    ui_update::font(choice,&lv_font_montserrat_14,0); lv_obj_set_style_radius(choice,4,0);
}
void invalidateProfileView() {
    groupedOnce=listInitialized=false;
    visibleCount=0; pressedValid=false;
    memset(decodedPublished,0,sizeof(decodedPublished)); memset(decodedRecent,0,sizeof(decodedRecent));
    for (unsigned i=0;i<128;++i) removeFields(i);
}
bool assignAddressProfile(uint8_t address,int index) {
    if(!registry.canAssign(address,index))return false;
    auto &binding=registry.bindings[address];
    auto mode=index<0?decoding::Assignment::Disabled:decoding::Assignment::Manual;
    if(binding.profile==index && binding.mode==mode){registry.saveFailed=false;return true;}
    if(binding.profile!=index){
        decoder.clearAddress(address);
        discoveryDecoder->clearAddress(address);
        registry.assign(address,index,mode);
        invalidateProfileView();
    }else{binding.mode=mode;binding.deferred=false;binding.missingSaved=false;++registry.generation;}
    binding.dirty=true;registry.saveFailed=false;
    return true;
}
void applyProfile(lv_event_t *) {
    unsigned selected=lv_dropdown_get_selected(profileChoice);
    if (!selected) { ui_update::text(profileMessage,"Select a profile or No parsing first."); return; }
    int index=selected==1?-1:int(selected)-2;
    if (!assignAddressProfile(profileAddress,index)) { ui_update::text(profileMessage,"Active profile capacity reached. Disable another address first."); return; }
    closeDetail(nullptr);
}
void deferProfile(lv_event_t *) {
    registry.bindings[profileAddress].deferred=true;
    closeDetail(nullptr);
}
void resetProfiles(lv_event_t *e) {
    if (!resetProfilesConfirmed) {
        resetProfilesConfirmed=true;
        auto *caption=lv_obj_get_child(lv_event_get_target_obj(e),0);
        ui_update::text(caption,"CONFIRM RESET ALL");
        ui_update::text(profileMessage,"Remove every address assignment? Raw observations are retained. Pause to persist.");
        return;
    }
    bool observed[128]; for (unsigned a=0;a<128;++a) observed[a]=registry.bindings[a].seen;
    decoder.clear(); discoveryDecoder->clear(); registry.reset();
    for (unsigned a=0;a<128;++a) registry.bindings[a].seen=observed[a];
    profileStorage.resetPending=true;
    invalidateProfileView(); closeDetail(nullptr);
}
void changeProfile(lv_event_t *e) {
    uint8_t address=uint8_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    closeDetail(nullptr); openProfile(address);
}
void openProfile(uint8_t address) {
    if (modal || address<8 || address>=0x78) return;
    profileAddress=address; resetProfilesConfirmed=false;
    modal=lv_obj_create(lv_screen_active()); flat(modal,Bg); lv_obj_set_pos(modal,0,0); lv_obj_set_size(modal,W,H);
    lv_obj_remove_flag(modal,LV_OBJ_FLAG_SCROLLABLE);
    char title[48]; snprintf(title,sizeof(title),"PROFILE / 0x%02X",address);
    text(modal,12,14,242,title,&lv_font_montserrat_18);
    button(modal,266,4,48,LV_SYMBOL_CLOSE,deferProfile,0);
    auto *tile=inset(modal,8,62,304,130);
    char reason[220]; int only; unsigned matches=registry.candidates(address,only);
    snprintf(reason,sizeof(reason),"%s\n%s\nAssignment applies to every register at this address.",
        registry.bindings[address].missingSaved?"Saved profile unavailable":registry.source(address),
        matches>1?"Several profiles list this address.":matches==0?"No typical-address match. Manual assignment is allowed.":"Typical-address match available.");
    detailLine(tile,12,reason,&lv_font_montserrat_14,Ink); insetEdge(tile);
    profileChoice=lv_dropdown_create(modal); lv_obj_set_pos(profileChoice,8,210); lv_obj_set_size(profileChoice,304,44);
    profileOptions(profileChoice,address);
    profileMessage=text(modal,12,270,296,"Choice takes effect immediately. Saved when capture is paused. New decoding starts with fresh traffic.",&lv_font_montserrat_12);
    ui_update::textColor(profileMessage,lv_color_hex(Muted),0);
    auto *apply=button(modal,8,340,148,"ASSIGN",applyProfile,0); ui_update::bgColor(apply,lv_color_hex(0x226b49),0);
    button(modal,164,340,148,"LATER",deferProfile,0);
    auto *reset=button(modal,8,414,304,"RESET ALL ASSIGNMENTS",resetProfiles,0); ui_update::bgColor(reset,lv_color_hex(0x85373f),0);
}
int profileDetails(lv_obj_t *body,int y,uint8_t address) {
    if ((!reportActive && selection.display==analysis::Display::Raw) || address<8 || address>=0x78) return y;
    auto *tile=inset(body,8,y,304,190); const auto &b=registry.bindings[address];
    char line[192];
    if (b.profile>=0) {
        const auto &p=decoding::profiles[b.profile];
        snprintf(line,sizeof(line),"PROFILE / %s\nID %s / revision %u\nAssignment: %s",p.name,p.id,p.revision,registry.source(address));
    } else snprintf(line,sizeof(line),"PROFILE / %s\n%s",registry.source(address),b.mode==decoding::Assignment::Disabled?"Parsing intentionally disabled.":"No profile assigned to this address.");
    int at=detailLine(tile,12,line,&lv_font_montserrat_12,Cyan);
    if (b.profile>=0) at=detailLine(tile,at,decoding::profiles[b.profile].source,&lv_font_montserrat_10,Muted);
    if (b.profile>=0) {
        auto *change=button(tile,12,at,280,"CHANGE PROFILE",changeProfile,address);
        ui_update::bgColor(change,lv_color_hex(0x245561),0); at+=52;
    } else {
        profileAddress=address;
        profileChoice=lv_dropdown_create(tile); lv_obj_set_pos(profileChoice,12,at); lv_obj_set_size(profileChoice,280,40);
        profileOptions(profileChoice,address); at+=48;
        profileMessage=text(tile,12,at,280,"",&lv_font_montserrat_10); at+=20;
        auto *apply=button(tile,12,at,280,"ASSIGN PROFILE",applyProfile,0); ui_update::bgColor(apply,lv_color_hex(0x226b49),0); at+=52;
    }
    lv_obj_set_height(tile,at); insetEdge(tile); return y+at+10;
}
