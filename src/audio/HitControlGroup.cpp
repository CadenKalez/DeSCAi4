#include "audio/HitControlGroup.h"

bool cHitControlGroup::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cIHitControlGroup && iid != kGZIID_cIGZUnknown) return false;
    *out = static_cast<cIHitControlGroup*>(this);
    AddRef();
    return true;
}

// Deletes when the count is below 2 (0 or 1), exactly like the original.
std::uint32_t cHitControlGroup::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}
