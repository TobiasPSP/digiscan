#pragma once
// Change-only LVGL setters. Avoiding identical text/style writes reduces redraws
// and touch latency. These counters measure UI writes, not hardware frame rate.
// Objects still belong to LVGL; callers must clear references when dialogs die.
#include <lvgl.h>
#include <cstring>
#include <cstdarg>
#include <cstdio>

// Compare local properties, not resolved styles: theme/state inheritance must
// not prevent a deliberate local override. No cached object pointers survive deletion.
namespace ui_update {
static uint32_t textWrites=0, styleWrites=0;
inline void text(lv_obj_t *object,const char *value) {
    if (!strcmp(lv_label_get_text(object),value)) return;
    ++textWrites; lv_label_set_text(object,value);
}
inline void textFmt(lv_obj_t *object,const char *format,...) {
    char value[512]; va_list args; va_start(args,format);
    int length=vsnprintf(value,sizeof(value),format,args); va_end(args);
    if (length<0) return;
    if (size_t(length)>=sizeof(value)) {
        // Preserve LVGL formatting semantics for unusually long text.
        char *large=static_cast<char *>(lv_malloc(size_t(length)+1));
        if (!large) return;
        va_start(args,format); vsnprintf(large,size_t(length)+1,format,args); va_end(args);
        text(object,large); lv_free(large); return;
    }
    text(object,value);
}
inline void color(lv_obj_t *object,lv_style_prop_t property,lv_color_t color,lv_style_selector_t selector) {
    lv_style_value_t previous;
    if (lv_obj_get_local_style_prop(object,property,&previous,selector)==LV_STYLE_RES_FOUND &&
        lv_color_eq(previous.color,color)) return;
    lv_style_value_t value{}; value.color=color; ++styleWrites;
    lv_obj_set_local_style_prop(object,property,value,selector);
}
inline void textColor(lv_obj_t *o,lv_color_t c,lv_style_selector_t s) { color(o,LV_STYLE_TEXT_COLOR,c,s); }
inline void bgColor(lv_obj_t *o,lv_color_t c,lv_style_selector_t s) { color(o,LV_STYLE_BG_COLOR,c,s); }
inline void borderColor(lv_obj_t *o,lv_color_t c,lv_style_selector_t s) { color(o,LV_STYLE_BORDER_COLOR,c,s); }
inline void font(lv_obj_t *object,const lv_font_t *font,lv_style_selector_t selector) {
    lv_style_value_t previous;
    if (lv_obj_get_local_style_prop(object,LV_STYLE_TEXT_FONT,&previous,selector)==LV_STYLE_RES_FOUND && previous.ptr==font) return;
    lv_style_value_t value{}; value.ptr=font; ++styleWrites;
    lv_obj_set_local_style_prop(object,LV_STYLE_TEXT_FONT,value,selector);
}
}
