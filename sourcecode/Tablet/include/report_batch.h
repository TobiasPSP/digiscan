#pragma once
// Nonblocking Read Missing/All batch lifecycle. Plan deduplication avoids reading
// one register separately for every bit field. Cancel stops new requests; an
// in-flight operation still needs acknowledgement and capture-state restoration.
#include "report_model.h"
#include "active_plan.h"
namespace acquisition {
struct Batch {
    unsigned properties[reporting::Capacity]={};
    unsigned count=0,next=0,completed=0,failed=0,skipped=0;
    uint32_t generation=0;
    bool running=false,cancel=false,missing=false;
    void reset() {
        count=next=completed=failed=skipped=0;generation=0;
        running=cancel=missing=false;
    }
    bool build(const reporting::Model &model,const reporting::Cache &cache,bool onlyMissing,uint32_t version) {
        reset();missing=onlyMissing;generation=version;
        for (unsigned row=0;row<model.count;++row) {
            unsigned index=model.rows[row].property;
            Plan plan;if (resolve(index,plan)) { ++skipped;continue; }
            const auto &reading=cache.readings[index];
            if (onlyMissing && reading.valid && !reading.failed) continue;
            bool duplicate=false;
            for (unsigned n=0;n<count;++n) {
                const auto &p=decoding::properties[properties[n]];
                duplicate|=p.address==plan.address && p.low==plan.registers[0] && p.count==plan.count;
            }
            if (!duplicate) properties[count++]=index;
        }
        running=count!=0;return running;
    }
    bool needed(unsigned index,const reporting::Cache &cache) const {
        if (!missing) return true;
        const auto &p=decoding::properties[index];
        // A shared register read is needed if any of its fields is still missing.
        for (unsigned i=0;i<decoding::PropertyCount;++i) {
            const auto &q=decoding::properties[i];const auto &r=cache.readings[i];
            if (q.count==p.count && q.address==p.address && q.low==p.low && (!r.valid || r.failed)) return true;
        }
        return false;
    }
};
}
