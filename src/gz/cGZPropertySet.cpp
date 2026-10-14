#include "gz/cGZPropertySet.h"

#include <cstring>
#include <new>

void cGZPropertySet::Destroy(Slot& slot) {
    if (!slot.bytes.empty()) Object(slot)->~cIGZAny();  // the original calls the stored object's destructor
    slot.bytes.clear();
}

cGZPropertySet::cGZPropertySet(const cGZPropertySet& other) { *this = other; }

cGZPropertySet& cGZPropertySet::operator=(const cGZPropertySet& other) {
    refCount_ = other.refCount_;  // the original copies the count too
    if (this == &other) return *this;
    ClearAll();
    for (const auto& [key, slot] : other.values_) {
        Slot copy;
        copy.bytes.resize(slot.bytes.size());
        Object(slot)->CopyTo(copy.bytes.data());
        values_.emplace(key, std::move(copy));
    }
    cursor_ = values_.end();
    return *this;
}

cGZPropertySet::~cGZPropertySet() { ClearAll(); }

bool cGZPropertySet::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cIGZUnknown && iid != kGZIID_cIGZPropertySet) return false;
    *out = static_cast<cIGZPropertySet*>(this);
    AddRef();
    return true;
}

std::uint32_t cGZPropertySet::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

// SetValue: an existing entry is destroyed, resized to the new object's DataSize() and rebuilt
// (the stored type may change); a new entry is created the same way.
bool cGZPropertySet::SetValue(const std::uint32_t& key, const cIGZAny& value) {
    Slot& slot = values_[key];
    Destroy(slot);
    slot.bytes.resize(value.DataSize());
    value.CopyTo(slot.bytes.data());
    return true;
}

bool cGZPropertySet::ClearValue(const std::uint32_t& key) {
    const auto it = values_.find(key);
    if (it == values_.end()) return false;
    Destroy(it->second);
    if (cursor_ == it) cursor_ = values_.erase(it); else values_.erase(it);
    return true;
}

void cGZPropertySet::ClearAll() {
    for (auto& entry : values_) Destroy(entry.second);
    values_.clear();
    cursor_ = values_.end();
}

bool cGZPropertySet::FirstKeyAndValue(std::uint32_t* key, cIGZAny** value) {
    if (key == nullptr || value == nullptr) return false;
    cursor_ = values_.begin();
    if (cursor_ == values_.end()) return false;
    *key = cursor_->first;
    *value = Object(cursor_->second);
    return true;
}

// Like the original, advancing past the last element leaves the cursor at end() and keeps
// returning false; calling it before FirstKeyAndValue on an empty set is a no-op.
bool cGZPropertySet::NextKeyAndValue(std::uint32_t* key, cIGZAny** value) {
    if (cursor_ == values_.end()) return false;
    ++cursor_;
    if (key == nullptr || value == nullptr || cursor_ == values_.end()) return false;
    *key = cursor_->first;
    *value = Object(cursor_->second);
    return true;
}

bool cGZPropertySet::Value(const std::uint32_t& key, cIGZAny* out) const {
    const auto it = values_.find(key);
    return it != values_.end() && out->CopyFromObject(Object(it->second));
}

bool cGZPropertySet::HasValue(const std::uint32_t& key) const { return values_.count(key) != 0; }
