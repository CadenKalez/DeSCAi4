#include "gz/cGZPersistResourceKeyList.h"

#include <algorithm>

bool cGZPersistResourceKeyList::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZPersistResourceKeyList) {
        *out = static_cast<cIGZPersistResourceKeyList*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZPersistResourceKeyList::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cGZPersistResourceKeyList::Insert(const cGZPersistResourceKey& key) {
    keys_.push_back(key);
    return true;
}

bool cGZPersistResourceKeyList::Insert(const cIGZPersistResourceKeyList& other) {
    other.EnumKeys(InsertKeyCallback, this);
    return true;
}

void cGZPersistResourceKeyList::InsertKeyCallback(const cGZPersistResourceKey& key, void* list) {
    static_cast<cGZPersistResourceKeyList*>(list)->keys_.push_back(key);
}

bool cGZPersistResourceKeyList::Erase(const cGZPersistResourceKey& key) {
    auto it = std::find(keys_.begin(), keys_.end(), key);
    if (it != keys_.end()) keys_.erase(it);
    return true;  // the original reports success whether or not a key was removed
}

bool cGZPersistResourceKeyList::EraseAll() {
    keys_.clear();
    return true;
}

void cGZPersistResourceKeyList::EnumKeys(ResourceKeyEnumFn callback, void* context) const {
    for (const cGZPersistResourceKey& key : keys_) callback(key, context);
}

bool cGZPersistResourceKeyList::IsPresent(const cGZPersistResourceKey& key) {
    return std::find(keys_.begin(), keys_.end(), key) != keys_.end();
}

bool cGZPersistResourceKeyFilter::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZPersistResourceKeyFilter) {
        *out = static_cast<cIGZPersistResourceKeyFilter*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZPersistResourceKeyFilter::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}
