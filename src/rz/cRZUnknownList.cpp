#include "rz/cRZUnknownList.h"

#include <iterator>

bool cRZUnknownList::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZUnknownList) {
        *out = static_cast<cIGZUnknownList*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cRZUnknownList::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cRZUnknownList::GetElement(std::uint32_t index, std::uint32_t iid, void** out) const {
    if (index >= items_.size()) return false;
    auto it = items_.begin();
    std::advance(it, index);
    return it->get()->QueryInterface(iid, out);
}

bool cRZUnknownList::EnumElements(cIGZUnknownEnumerator& e, cIGZUnknown* context, std::uint32_t flags) {
    for (auto& item : items_) e.Enumerate(item.get(), context, flags);
    return true;
}

bool cRZUnknownList::ConstEnumElements(cIGZUnknownEnumerator& e, cIGZUnknown* context, std::uint32_t flags) const {
    for (auto& item : items_) e.Enumerate(item.get(), context, flags);
    return true;
}

bool cRZUnknownList::PushFront(cIGZUnknown* element) {
    items_.emplace_front(element);
    return true;
}

bool cRZUnknownList::PushBack(cIGZUnknown* element) {
    items_.emplace_back(element);
    return true;
}

// Gathers the other list's elements (those that answer cIGZUnknown) in order.
static std::list<cRZAutoRefCount<cIGZUnknown>> Gather(const cIGZUnknownList& other) {
    std::list<cRZAutoRefCount<cIGZUnknown>> gathered;
    for (std::uint32_t i = 0, n = other.Size(); i < n; ++i) {
        void* raw = nullptr;
        if (other.GetElement(i, kGZIID_cIGZUnknown, &raw)) {
            auto* u = static_cast<cIGZUnknown*>(raw);
            gathered.emplace_back(u);   // takes its own reference
            u->Release();               // drop the one GetElement handed out
        }
    }
    return gathered;
}

bool cRZUnknownList::PushFront(const cIGZUnknownList& other) {
    auto gathered = Gather(other);
    items_.splice(items_.begin(), gathered);
    return true;
}

bool cRZUnknownList::PushBack(const cIGZUnknownList& other) {
    auto gathered = Gather(other);
    items_.splice(items_.end(), gathered);
    return true;
}

bool cRZUnknownList::PeekFront(std::uint32_t iid, void** out) const {
    return !items_.empty() && items_.front()->QueryInterface(iid, out);
}

bool cRZUnknownList::PeekBack(std::uint32_t iid, void** out) const {
    return !items_.empty() && items_.back()->QueryInterface(iid, out);
}

bool cRZUnknownList::PopFront(std::uint32_t iid, void** out) {
    if (items_.empty() || !items_.front()->QueryInterface(iid, out)) return false;
    items_.pop_front();
    return true;
}

bool cRZUnknownList::PopBack(std::uint32_t iid, void** out) {
    if (items_.empty() || !items_.back()->QueryInterface(iid, out)) return false;
    items_.pop_back();
    return true;
}

void cRZUnknownList::CreateClone(cIGZUnknownList** out) const {
    auto* clone = new cRZUnknownList();
    clone->refCount_ = 1;
    clone->PushFront(*this);
    *out = clone;
}
