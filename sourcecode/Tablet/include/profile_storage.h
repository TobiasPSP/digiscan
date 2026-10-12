#pragma once
// Persist address/profile assignments in Preferences. Defer flash writes while
// capture is active; a UI change is immediate but durable save waits for pause.
// Session clear preserves assignments; explicit configuration reset removes them.
#include "profile_registry.h"
namespace decoding {
// Preferences 2.0.17 commits putString immediately. Save only while the peer is
// confirmed paused; a failed commit remains dirty and is reported, not retried.
class ProfileStorage {
public:
    Preferences preferences;
    bool ready=false, resetPending=false;
    void load(Registry &registry) {
        ready=preferences.begin("i2cprofiles",false);
        if (!ready) { registry.saveFailed=true; return; }
        for (unsigned a=8;a<0x78;++a) {
            char key[4],id[49]={}; snprintf(key,sizeof(key),"a%02X",a);
            if (!preferences.isKey(key)) continue;
            if (!preferences.getString(key,id,sizeof(id))) { registry.bindings[a].missingSaved=true; continue; }
            int index=registry.find(id);
            if (!strcmp(id,"-")) registry.assign(a,-1,Assignment::Disabled);
            else if (index<0 || !registry.assign(a,index,Assignment::Manual)) registry.bindings[a].missingSaved=true;
        }
    }
    bool pending(const Registry &registry) const {
        if (resetPending) return true;
        for (const auto &b:registry.bindings) if (b.dirty) return true;
        return false;
    }
    void save(Registry &registry,bool safe) {
        if (!safe || registry.saveFailed || !pending(registry)) return;
        if (!ready) { registry.saveFailed=true; return; }
        if (resetPending) {
            if (!preferences.clear()) { registry.saveFailed=true; return; }
            resetPending=false;
        }
        for (unsigned a=8;a<0x78;++a) {
            auto &b=registry.bindings[a]; if (!b.dirty) continue;
            char key[4]; snprintf(key,sizeof(key),"a%02X",a);
            const char *id=b.mode==Assignment::Disabled?"-":b.profile>=0?profiles[b.profile].id:nullptr;
            if (!id || preferences.putString(key,id)!=strlen(id)) { registry.saveFailed=true; return; }
            b.dirty=false;
        }
    }
};
}
