#include "gz/COMLibrary.h"

#include <cstring>


bool cGZCOMLibrary::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cIGZCOMLibrary) return false;
    *out = this;
    AddRef();
    return true;
}

bool cGZCOMLibrary::Load() {
    if (loaded_) return true;
    // (the original converts the path to the system code page first and then ignores the result)
    loaded_ = true;
    director_ = nullptr;
    handle_ = nullptr;
    loaded_ = false;
    return false;
}

bool cGZCOMLibrary::operator<(const cGZCOMLibrary& other) const {
    cRZString theirs;
    other.GetPath(theirs);
    const std::size_t mine = path_.Strlen(), their = theirs.Strlen();
    const std::size_t common = mine < their ? mine : their;
    if (common != 0) {
        const int order = std::memcmp(path_.Data(), theirs.Data(), common);
        if (order != 0) return order < 0;
    }
    return their > mine;
}
