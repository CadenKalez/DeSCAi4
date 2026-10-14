#include "rz/cRZMessage2Standard.h"

#include "gz/cIGZSerializable.h"
#include "gz/cIGZString.h"

namespace {
template <class T>
void AssignReferenced(T*& slot, T* value) {
    T* old = slot;
    if (old == value) return;
    if (value) value->AddRef();
    slot = value;
    if (old) old->Release();
}
}  // namespace

cRZMessage2Standard::~cRZMessage2Standard() {
    if (extra_) extra_->Release();
    if (unknown_) unknown_->Release();
    if (string_) string_->Release();
}

bool cRZMessage2Standard::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 0xa52294b4 || iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZMessage2Standard) {
        *out = static_cast<cIGZMessage2Standard*>(this);
        AddRef();
        return true;
    }
    return false;
}

// cRZMessage2Standard::Release() @ 0x1000414a0: destroys when the count is 1 or less.
std::uint32_t cRZMessage2Standard::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

void cRZMessage2Standard::SetType(std::uint32_t type) {
    type_ = type;
    if (type == 0) flags_ &= 0xfe; else flags_ |= 0x01;
}

// @ 0x1000417d4: a fresh message (empty payload) of the same type, as interface `iid`.
bool cRZMessage2Standard::Create(std::uint32_t iid, void** out) const {
    auto* message = new cRZMessage2Standard();
    if (!message->QueryInterface(iid, out)) {
        delete message;
        return false;
    }
    message->SetType(type_);
    return true;
}

bool cRZMessage2Standard::GetString(std::uint32_t iid, void** out) {
    return GetHasString() && string_ ? string_->QueryInterface(iid, out) : false;
}

void cRZMessage2Standard::SetString(cIGZString* value) {
    if (value == nullptr) flags_ &= 0xbf; else flags_ |= 0x40;
    AssignReferenced(string_, value);
}

void cRZMessage2Standard::SetIGZUnknown(cIGZUnknown* value) {
    if (value == nullptr) flags_ &= 0x7f; else flags_ |= 0x80;
    AssignReferenced(unknown_, value);
}

bool cRZMessage2Standard::GetExtra(std::uint32_t iid, void** out) {
    return GetHasExtra() && extra_ ? extra_->QueryInterface(iid, out) : false;
}

void cRZMessage2Standard::SetExtra(cIGZSerializable* value) {
    if (value == nullptr) flags_ &= 0xdf; else flags_ |= 0x20;
    AssignReferenced(extra_, value);
}
