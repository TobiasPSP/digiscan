#pragma once
// Evaluate profile evidence without overclaiming chip identity. A is chip-specific
// positive evidence, B is exclusion evidence, C is compatible shared behavior.
// Multiple compatible profiles need explicit selection; more C rules must not
// manufacture a unique match. Transport failures are not dependable chip IDs.
#include <stdint.h>
namespace identification {
enum class Class : uint8_t { A, B, C };
enum class Outcome : uint8_t { Match, Contradiction, NoMatch, Inconclusive };
inline Outcome evaluate(Class kind,bool valid,uint32_t value,uint32_t mask,uint32_t min,uint32_t max) {
    if(!valid)return Outcome::Inconclusive;
    value&=mask;
    if(value>=min && value<=max)return Outcome::Match;
    return kind==Class::B?Outcome::Contradiction:Outcome::NoMatch;
}
struct Evidence {
    unsigned exclusive=0,mandatory=0,shared=0,unmatched=0,missing=0;
    bool excluded=false;
    void add(Class kind,Outcome outcome) {
        if(outcome==Outcome::Inconclusive){++missing;return;}
        if(outcome==Outcome::Contradiction){excluded=true;return;}
        if(outcome==Outcome::NoMatch){++unmatched;return;}
        if(kind==Class::A)++exclusive;
        else if(kind==Class::B)++mandatory;
        else ++shared;
    }
    const char *result() const {
        if(excluded)return exclusive?"CONFLICTING EVIDENCE":"EXCLUDED";
        if(exclusive)return "POSITIVE ID";
        return shared || mandatory?"COMPATIBLE / NOT IDENTIFIED":"INCONCLUSIVE";
    }
};
inline const char *name(Outcome outcome) {
    switch(outcome){case Outcome::Match:return "match";case Outcome::Contradiction:return "CONTRADICTION";
        case Outcome::NoMatch:return "no match (not exclusion)";default:return "unread/failed";}
}
}
