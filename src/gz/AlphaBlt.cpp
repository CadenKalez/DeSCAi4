#include "gz/AlphaBlt.h"

bool cGZAlphaBlt::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZAlphaBlt) {
        *out = static_cast<cIGZAlphaBlt*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cIGZSystemService) {
        *out = static_cast<cIGZSystemService*>(this);
        AddRef();
        return true;
    }
    return cRZUnknown::QueryInterface(iid, out);
}
