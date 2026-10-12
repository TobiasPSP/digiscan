// UI helpers included inside the tablet's implementation namespace.
// Reusable detail tiles, bitmask visualization and diagnostic sections.
// Visual bit emphasis is derived from the same profile mask as decoding; the
// friendly summary precedes programmer-level mask/shift/formula information.
lv_obj_t *inset(lv_obj_t *parent,int x,int y,int w,int h) {
    auto *o=lv_obj_create(parent); flat(o,0x1a2023);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h); lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(o,4,0); lv_obj_set_style_border_width(o,1,0);
    lv_obj_set_style_border_side(o,lv_border_side_t(LV_BORDER_SIDE_BOTTOM|LV_BORDER_SIDE_RIGHT),0);
    lv_obj_set_style_border_color(o,lv_color_hex(0x4b575b),0); return o;
}
void insetEdge(lv_obj_t *tile) {
    auto *edge=lv_obj_create(tile); flat(edge,0); lv_obj_set_style_bg_opa(edge,LV_OPA_TRANSP,0);
    lv_obj_set_pos(edge,0,0); lv_obj_set_size(edge,lv_obj_get_width(tile),lv_obj_get_height(tile));
    lv_obj_set_style_radius(edge,4,0); lv_obj_set_style_border_width(edge,2,0);
    lv_obj_set_style_border_side(edge,lv_border_side_t(LV_BORDER_SIDE_TOP|LV_BORDER_SIDE_LEFT),0);
    lv_obj_set_style_border_color(edge,lv_color_hex(0x080c0e),0);
    lv_obj_remove_flag(edge,lv_obj_flag_t(LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
}
int detailLine(lv_obj_t *parent,int y,const char *s,const lv_font_t *font,uint32_t color) {
    if (!s || !*s) return y;
    auto *label=text(parent,12,y,280,s,font); lv_obj_set_style_text_color(label,lv_color_hex(color),0);
    lv_point_t size; lv_text_get_size(&size,s,font,0,0,280,LV_TEXT_FLAG_NONE); return y+size.y+8;
}
void detailMetadata(lv_obj_t *body,int x,int w,const char *name,const char *value,uint32_t color) {
    auto *tile=inset(body,x,0,w,76);
    auto *label=text(tile,8,10,w-16,name,&lv_font_montserrat_10);
    lv_obj_set_style_text_color(label,lv_color_hex(Muted),0);
    label=text(tile,8,34,w-16,value,&lv_font_montserrat_16);
    lv_obj_set_style_text_color(label,lv_color_hex(color),0); insetEdge(tile);
}
int detailProperty(lv_obj_t *body,int y,const analysis::Value &v,unsigned selected) {
    const auto &p=decoding::properties[v.property]; auto *tile=inset(body,8,y,304,512);
    char value[192],line[384]; details::value(v,value,sizeof(value));
    snprintf(line,sizeof(line),"%s = %s (%s)",p.name,value,p.access); details::compact(line);
    int at=detailLine(tile,12,line,&lv_font_montserrat_16,v.success?0x83dde2:Amber);
    const unsigned bits=p.count*8;
    for (unsigned bit=0;bit<bits;++bit) {
        bool included=p.mask&(1u<<(bits-1-bit)); char digit[]={char(included?'1':'0'),0};
        auto *label=text(tile,12+int(bit)*12,at,12,digit,&mono::font);
        lv_obj_set_style_text_color(label,lv_color_hex(included?0xb8e6bb:0x607079),0);
        lv_obj_set_style_bg_color(label,lv_color_hex(0xb8e6bb),0);
        lv_obj_set_style_bg_opa(label,included?LV_OPA_10:LV_OPA_TRANSP,0);
    }
    at+=28;
    if (p.friendlydesc && *p.friendlydesc) at=detailLine(tile,at,p.friendlydesc,&lv_font_montserrat_14,0xe7d4ad);
    if (!p.bit0 && !p.enumCount && !p.ascii) {
        snprintf(line,sizeof(line),"value = code x %.6g %c %.6g%s%s",p.scale,p.offset<0?'-':'+',fabs(p.offset),*p.unit?" ":"",p.unit);
        at=detailLine(tile,at,line,&lv_font_montserrat_12,Cyan);
    }
    unsigned code=(v.word&p.mask)>>p.shift;
    const char *meaning=p.bit0 && v.complete?(code?p.meaning1:p.meaning0):"";
    snprintf(detailText,16384,"%s\n%s\n%s\n%s\n%s",p.description,meaning,
        p.unsignedCurrent?"Unsigned code; physical sign not established.":"",
        v.direction=='W'?"Observed write; application not verified.":"",p.note);
    details::compact(detailText); at=detailLine(tile,at,detailText,&lv_font_montserrat_12,Muted);
    snprintf(line,sizeof(line),"MASK 0x%0*X / SHIFT %u",p.count*2,p.mask,p.shift);
    at=detailLine(tile,at,line,&lv_font_montserrat_12,0x91a6b2);
    lv_obj_set_height(tile,at); insetEdge(tile);
    if (v.property==selected) {
        auto *accent=lv_obj_create(tile); flat(accent,Cyan); lv_obj_set_pos(accent,0,8); lv_obj_set_size(accent,3,at-16);
        lv_obj_set_style_bg_opa(accent,LV_OPA_60,0); lv_obj_remove_flag(accent,lv_obj_flag_t(LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
    }
    return y+at+10;
}
void decodedDetail(const analysis::Value &v) {
    if (v.property>=decoding::PropertyCount || !decoding::properties[v.property].count) return;
    const auto &p=decoding::properties[v.property]; modal=lv_obj_create(lv_screen_active()); flat(modal,Bg);
    lv_obj_set_pos(modal,0,0); lv_obj_set_size(modal,W,H); lv_obj_remove_flag(modal,LV_OBJ_FLAG_SCROLLABLE);
    text(modal,8,14,230,"REGISTER DETAIL",&lv_font_montserrat_14);
    auto *close=lv_button_create(modal); lv_obj_set_pos(close,266,4); lv_obj_set_size(close,48,40);
    lv_obj_add_event_cb(close,closeDetail,LV_EVENT_CLICKED,nullptr);
    auto *caption=lv_label_create(close); lv_label_set_text(caption,LV_SYMBOL_CLOSE); lv_obj_center(caption);
    auto *body=lv_obj_create(modal); flat(body,Bg); lv_obj_set_pos(body,0,52); lv_obj_set_size(body,W,H-52);
    lv_obj_set_scroll_dir(body,LV_DIR_VER);
    char address[16],registers[32],direction[24]; snprintf(address,sizeof(address),"0x%02X",p.address);
    if (p.count==3) snprintf(registers,sizeof(registers),"0x%02X\n0x%02X+0x%02X",p.low,p.low+1,p.high);
    else if (p.count==2) snprintf(registers,sizeof(registers),"0x%02X+0x%02X",p.low,p.high);
    else snprintf(registers,sizeof(registers),"0x%02X",p.low);
    snprintf(direction,sizeof(direction),"%c / %s",v.direction,v.success?"OK":"FAIL");
    detailMetadata(body,8,92,"ADDRESS",address,Cyan);
    detailMetadata(body,108,112,p.count==1?"REGISTER":"REGISTERS",registers,Ink);
    detailMetadata(body,228,84,"DIRECTION",direction,v.success?Ink:Amber);
    char raw[192];
    if (v.complete) {
        if (p.count==3) snprintf(raw,sizeof(raw),"Raw Value 0x%06X / Dec %u\nBytes %02X %02X %02X (low first)",v.word,v.word,v.word&255,(v.word>>8)&255,v.word>>16);
        else if (p.count==2) snprintf(raw,sizeof(raw),"Raw Value 0x%04X / Dec %u\nBytes %02X %02X (low first)",v.word,v.word,v.word&255,v.word>>8);
        else snprintf(raw,sizeof(raw),"Raw Value 0x%02X / Dec %u",v.word,v.word);
    } else snprintf(raw,sizeof(raw),"Raw Value -- / INCOMPLETE");
    auto *rawLabel=text(body,12,88,296,raw,&lv_font_montserrat_14); lv_obj_set_style_text_color(rawLabel,lv_color_hex(Muted),0);
    lv_point_t size; lv_text_get_size(&size,raw,&lv_font_montserrat_14,0,0,296,LV_TEXT_FLAG_NONE); int y=88+size.y+16;
    if (p.count>1) {
        const char *assembly="Low-first consecutive target reads; max tablet arrival gap 1 s.";
        auto *note=text(body,12,y,296,assembly,&lv_font_montserrat_12); lv_obj_set_style_text_color(note,lv_color_hex(Muted),0);
        lv_text_get_size(&size,assembly,&lv_font_montserrat_12,0,0,296,LV_TEXT_FLAG_NONE); y+=size.y+12;
    }
    y=activeReadDetails(body,y,v);
    for (unsigned i=0;i<decoding::PropertyCount;++i) {
        const auto &field=decoding::properties[i];
        if (field.address!=p.address || field.low!=p.low || field.count!=p.count ||
            (p.count>1 && field.high!=p.high) || (v.direction=='W' && !field.writable)) continue;
        auto candidate=v; candidate.property=i; y=detailProperty(body,y,candidate,v.property);
    }
    profileDetails(body,y,p.address);
}
void rawDetail(const observer::Observation &o) {
    modal=lv_obj_create(lv_screen_active()); flat(modal,Bg); lv_obj_set_pos(modal,0,0); lv_obj_set_size(modal,W,H);
    lv_obj_remove_flag(modal,LV_OBJ_FLAG_SCROLLABLE);
    text(modal,8,14,242,"TRANSACTION DETAIL",&lv_font_montserrat_14);
    button(modal,266,4,48,LV_SYMBOL_CLOSE,closeDetail,0);
    auto *body=lv_obj_create(modal); flat(body,Bg); lv_obj_set_pos(body,0,52); lv_obj_set_size(body,W,H-52); lv_obj_set_scroll_dir(body,LV_DIR_VER);
    char address[16],reg[16],direction[24]; snprintf(address,sizeof(address),"0x%02X",o.address);
    if (o.associated) snprintf(reg,sizeof(reg),"0x%02X",o.reg); else snprintf(reg,sizeof(reg),"--");
    snprintf(direction,sizeof(direction),"%c / %s",o.direction,o.success?"OK":"FAIL");
    detailMetadata(body,8,92,"ADDRESS",address,Cyan); detailMetadata(body,108,112,"REGISTER",reg,Ink);
    detailMetadata(body,228,84,"DIRECTION",direction,o.success?Ink:Red);
    int y=88;
    auto *summary=inset(body,8,y,304,90); char line[160];
    snprintf(line,sizeof(line),"TX %lu / %u payload bytes",(unsigned long)o.id,o.dataBytes);
    int height=detailLine(summary,12,line,&lv_font_montserrat_14,Ink);
    if (selection.display!=analysis::Display::Raw && o.address<128) {
        const auto &b=registry.bindings[o.address];
        height=detailLine(summary,height,b.profile>=0?"Not covered by this profile for this operation or payload shape.":b.mode==decoding::Assignment::Disabled?"No parsing selected.":"Awaiting profile assignment.",&lv_font_montserrat_12,Muted);
    }
    lv_obj_set_height(summary,height); insetEdge(summary); y+=height+10;
    size_t at=10;
    for (unsigned f=0;f<o.raw[8];++f) {
        unsigned n=bringup::le16(o.raw+at);
        auto *tile=inset(body,8,y,304,100);
        snprintf(line,sizeof(line),"FRAME %u / %s / %s",f+1,o.raw[at+2]?"RESTART":"STOP",o.raw[at+3]?"INVALID":"VALID");
        int pos=detailLine(tile,12,line,&lv_font_montserrat_14,o.raw[at+3]?Amber:Cyan);
        snprintf(line,sizeof(line),"Flags 0x%02X / %u wire bytes\n+ ACK / - NACK",o.raw[at+4],n); at+=5;
        pos=detailLine(tile,pos,line,&lv_font_montserrat_12,Muted);
        size_t used=0;
        for (unsigned i=0;i<n && used+5<16384;++i) {
            int wrote=snprintf(detailText+used,16384-used,"%02X%c%s",o.raw[at+2*i],o.raw[at+2*i+1]?'+':'-',i%8==7?"\n":" ");
            if (wrote<0 || size_t(wrote)>=16384-used) break; used+=size_t(wrote);
        }
        detailText[used]=0; at+=2*n; details::compact(detailText);
        pos=detailLine(tile,pos,detailText,&mono::font,Ink); lv_obj_set_height(tile,pos); insetEdge(tile); y+=pos+10;
    }
    profileDetails(body,y,o.address);
}
