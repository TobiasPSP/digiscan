#pragma once
// Declarative report page/group/item references. References stay unresolved until
// the runtime profile is selected; All expands readable properties dynamically.
// Descriptors borrow immutable strings, not ownership of their source package.
#include "profile_types.h"
namespace reporting {
struct Group { const char *header; const char *const *items; unsigned count; };
struct Page { const char *profileId, *header; const Group *groups; unsigned count; };
}
