#pragma once
// Present a frozen Discover candidate instead of the live latest-value cache.
// This keeps approval attached to the actual observed transaction/group even
// when fresh traffic arrives while the user examines its decoded properties.
#include "analysis.h"

namespace discovery {
// Resolve against the frozen transaction, never against live Decoder values.
inline bool propertySnapshot(const observer::Observation &o,unsigned index,analysis::Value &v) {
    if(index>=decoding::PropertyCount || !o.associated)return false;
    const auto &p=decoding::properties[index];
    if(!p.count || p.address!=o.address || o.reg<p.low || unsigned(o.reg)>=unsigned(p.low)+p.count ||
        (o.direction!='R' && o.direction!='W'))return false;
    v=analysis::Value{};v.property=index;v.direction=o.direction;v.success=o.success;
    v.transactionId=o.id;v.observed_ms=o.observed_ms;
    // Match the existing profile decoder's supported single-byte transfer shape.
    // A larger payload alone does not prove that the device auto-increments.
    v.complete=p.count==1 && o.dataBytes==1;
    if(v.complete)v.word=o.preview[0];
    return true;
}
inline bool propertySnapshot(const Candidate &c,unsigned index,analysis::Value &v) {
    if(!propertySnapshot(c.first(),index,v))return false;
    const auto &p=decoding::properties[index];
    if(c.range.parsed && p.low==c.range.low && p.count==c.range.bytes) {
        v.word=c.word();v.complete=c.complete;v.success=c.success;
    }
    return true;
}
}
