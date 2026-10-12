#pragma once
// Pure color/fade calculations; no capture state or electrical interpretation.
// Time arguments use the presentation clock so paused screens remain frozen.
// Change highlights take precedence over the shorter unchanged-poll pulse.
#include <stdint.h>
namespace highlight {
constexpr uint32_t Lifetime_ms=2200;
constexpr uint32_t Refresh_ms=125;
constexpr uint32_t PollLifetime_ms=450;
inline uint32_t fade(uint32_t base,uint32_t accent,uint32_t changed_ms,uint32_t now_ms,uint32_t duration_ms) {
    const uint32_t age_ms=now_ms-changed_ms;
    if (age_ms>=duration_ms) return base;
    const uint32_t remaining_ms=duration_ms-age_ms;
    uint32_t result=0;
    for (unsigned shift=0;shift<24;shift+=8) {
        const uint32_t b=(base>>shift)&255, a=(accent>>shift)&255;
        result|=((b*age_ms+a*remaining_ms+duration_ms/2)/duration_ms)<<shift;
    }
    return result;
}
inline uint32_t color(uint32_t base,uint32_t accent,uint32_t changed_ms,uint32_t now_ms) {return fade(base,accent,changed_ms,now_ms,Lifetime_ms);}
inline uint32_t activity(uint32_t base,uint32_t accent,uint32_t changed_ms,uint32_t observed_ms,bool unchanged,bool success,bool unique,uint32_t now_ms) {
    // An unchanged poll must not conceal an outstanding value-change highlight.
    if(unique && uint32_t(now_ms-changed_ms)<Lifetime_ms)return color(base,accent,changed_ms,now_ms);
    if(unchanged && success)return fade(base,0x235742,observed_ms,now_ms,PollLifetime_ms);
    return base;
}
}
