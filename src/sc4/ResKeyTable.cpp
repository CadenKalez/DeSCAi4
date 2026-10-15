#include "sc4/ResKeyTable.h"

#include "sc4/DBSegment.h"

#include <algorithm>

namespace sc4 {

bool cSCResKeyEnumerator::QueryInterface(std::uint32_t iid, void** out) {
    *out = nullptr;
    if (iid == 1 || iid == kGZIID_cISCResKeyEnumerator) {
        *out = static_cast<cISCResKeyEnumerator*>(this);
        AddRef();
        return true;
    }
    return false;
}

bool cSCResKeyEnumerator::Next(std::uint32_t count, cGZPersistResourceKey** keys, std::uint32_t& got) {
    std::uint32_t n = 0;
    while (n < count && cursor_ != keys_.size()) keys[n++] = &keys_[cursor_++];
    got = n;
    return n == count;
}

bool cSCResKeyEnumerator::Skip(std::uint32_t count) {
    const std::size_t target = cursor_ + count;
    if (target < keys_.size()) {
        cursor_ = target;
        return true;
    }
    return false;
}

bool cSCResKeyEnumerator::Clone(cISCResKeyEnumerator** out) {
    auto* copy = new cSCResKeyEnumerator(keys_);
    copy->cRZRefCount::AddRef();
    *out = copy;
    return true;
}

bool cSCResKeyTable::QueryInterface(std::uint32_t iid, void** out) {
    *out = nullptr;
    if (iid == kGZIID_cIGZSerializable || iid == kGZIID_cISCResKeyTable || iid == 1) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

void cSCResKeyTable::SetEntry(std::uint32_t index, const cGZPersistResourceKey& key) {
    if (Size() <= index) Resize(index + 1);
    keys_[index] = key;
}

void cSCResKeyTable::AddEntries(const cISCProperty* property) {
    const cIGZVariant* value = property->GetPropertyValue();
    const std::uint32_t* data = value->RefUint32();
    const std::uint32_t count = value->GetCount();
    for (std::uint32_t i = 3; i <= count; i += 3) {
        cGZPersistResourceKey key;
        key.type = data[i - 2];
        key.group = data[i - 3];
        key.instance = data[i - 1];
        AddEntry(key);
    }
}

void cSCResKeyTable::RemoveEntry(std::uint32_t index) {
    if (index < Size()) keys_.erase(keys_.begin() + index);
}

void cSCResKeyTable::RemoveEntry(const cGZPersistResourceKey& key) {
    auto it = std::find(keys_.begin(), keys_.end(), key);
    if (it != keys_.end()) keys_.erase(it);
}

void cSCResKeyTable::EnumEntries(void (*fn)(const cGZPersistResourceKey&, void*), void* context) const {
    for (const cGZPersistResourceKey& key : keys_) fn(key, context);
}

bool cSCResKeyTable::CreateEnumerator(cISCResKeyEnumerator** out) const {
    auto* enumerator = new cSCResKeyEnumerator(keys_);
    enumerator->cRZRefCount::AddRef();
    *out = enumerator;
    return true;
}

bool cSCResKeyTable::Write(cIGZOStream& stream) const {
    stream.SetUint32(Size());
    for (const cGZPersistResourceKey& key : keys_) {
        stream.SetUint32(key.type);
        stream.SetUint32(key.group);
        stream.SetUint32(key.instance);
    }
    return !stream.GetError();
}

bool cSCResKeyTable::Read(cIGZIStream& stream) {
    std::uint32_t count = 0;
    stream.GetUint32(count);
    keys_.resize(count);
    for (cGZPersistResourceKey& key : keys_) {
        stream.GetUint32(key.type);
        stream.GetUint32(key.group);
        stream.GetUint32(key.instance);
    }
    return !stream.GetError();
}

}  // namespace sc4
