// GZCOM_Interface<To, From>(object, iid) — queries `object` for the interface `iid` and returns it as a
// counted reference to `To` (empty when the object does not implement it). One template; the binary
// instantiates it once per interface pair (24 of them).
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"
#include "rz/cRZAutoRefCount.h"

namespace gz {

template <class To, class From>
cRZAutoRefCount<To> GZCOM_Interface(const From* object, std::uint32_t interfaceId, To* /*typeHint*/ = nullptr) {
    void* out = nullptr;
    if (object) const_cast<From*>(object)->QueryInterface(interfaceId, &out);
    cRZAutoRefCount<To> result;
    if (out) {
        result = static_cast<To*>(out);
        static_cast<To*>(out)->Release();       // QueryInterface returned a reference; the smart pointer holds its own
    }
    return result;
}

}  // namespace gz
