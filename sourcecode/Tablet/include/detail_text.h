#pragma once
// Compact, bounded formatting for raw and decoded detail panes.
// Display text is not a source of profile semantics: units/access/formulas come
// from the definition. Buffer capacities include the terminating NUL byte.
#include "analysis.h"
namespace details {
inline bool whitespace(char c) { return c==' ' || c=='\t' || c=='\r'; }
// Trim lines in place, omit outer blanks, and retain at most one blank line.
inline void compact(char *text) {
    char *read=text, *write=text;
    bool haveLine=false, blank=false;
    while (*read) {
        char *begin=read;
        while (*read && *read!='\n') ++read;
        char *end=read;
        if (*read) ++read;
        while (begin<end && whitespace(*begin)) ++begin;
        while (end>begin && whitespace(end[-1])) --end;
        if (begin==end) { if (haveLine) blank=true; continue; }
        if (haveLine) { *write++='\n'; if (blank) *write++='\n'; }
        while (begin<end) *write++=*begin++;
        haveLine=true; blank=false;
    }
    *write='\0';
}
inline void value(const analysis::Value &v,char *out,size_t cap) {
    const auto &p=decoding::properties[v.property];
    if (!p.bit0 || !v.complete) { analysis::formatValue(v,out,cap); return; }
    unsigned code=(v.word&p.mask)>>p.shift;
    snprintf(out,cap,"%s%u%s%s",v.success?"":"FAIL ",code,
        code<p.minimum || code>p.maximum?" RANGE":"",p.conflict?" CONFLICT":"");
}
}
