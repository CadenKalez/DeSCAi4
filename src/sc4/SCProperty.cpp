#include "sc4/SCProperty.h"
#include "sc4/DBSegment.h"

#include <algorithm>

#include "rz/cRZUnknownList.h"

namespace sc4 {

bool cSCProperty::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cISCProperty) {
        *out = static_cast<cISCProperty*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cIGZVariant) {   // the embedded variant, kept alive by this property
        *out = static_cast<cIGZVariant*>(&value_);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cSCProperty::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

// operator<<(cIGZOStream&, cIGZVariant const&) / operator>>(cIGZIStream&, cIGZVariant&) (0x1000ce6eb /
// 0x1000ce660): the value is only transferred when the stream is a DB segment stream.
bool cSCProperty::Write(cIGZOStream& stream) const {
    stream.SetUint32(id_);
    void* raw = nullptr;
    if (stream.QueryInterface(kGZIID_cISC4DBSegmentOStream, &raw) && raw) {
        auto* segmentStream = static_cast<cISC4DBSegmentOStream*>(raw);
        segmentStream->WriteVariant(value_);
        segmentStream->Release();
    }
    return !stream.GetError();
}

bool cSCProperty::Read(cIGZIStream& stream) {
    stream.GetUint32(id_);
    void* raw = nullptr;
    if (stream.QueryInterface(kGZIID_cISC4DBSegmentIStream, &raw) && raw) {
        auto* segmentStream = static_cast<cISC4DBSegmentIStream*>(raw);
        segmentStream->ReadVariant(value_);
        segmentStream->Release();
    }
    return !stream.GetError();
}

// -------------------------------------------------------------------------------------------

cSCPropertyHolder::cSCPropertyHolder(const cSCPropertyHolder& other) : items_(other.items_) {}

bool cSCPropertyHolder::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cISCPropertyHolder) {
        *out = static_cast<cISCPropertyHolder*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cSCPropertyHolder::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

std::vector<cSCPropertyHolder::Entry>::const_iterator cSCPropertyHolder::Find(std::uint32_t id) const {
    auto it = std::lower_bound(items_.begin(), items_.end(), id,
                               [](const Entry& e, std::uint32_t v) { return e.id < v; });
    return (it != items_.end() && it->id == id) ? it : items_.end();
}

bool cSCPropertyHolder::HasProperty(std::uint32_t id) const { return Find(id) != items_.end(); }

bool cSCPropertyHolder::GetPropertyList(cIGZUnknownList** out) const {
    if (!out) return false;
    auto* list = new cRZUnknownList();
    list->AddRef();
    for (const Entry& e : items_) list->PushBack(e.property.get());
    *out = list;
    return true;
}

cISCProperty* cSCPropertyHolder::GetProperty(std::uint32_t id) const {
    auto it = Find(id);
    return it == items_.end() ? nullptr : it->property.get();
}

bool cSCPropertyHolder::GetProperty(std::uint32_t id, std::uint32_t& value) const {
    cISCProperty* p = GetProperty(id);
    return p && p->GetPropertyValue()->GetValUint32(value);
}

bool cSCPropertyHolder::GetProperty(std::uint32_t id, cIGZString& value) const {
    cISCProperty* p = GetProperty(id);
    return p && p->GetPropertyValue()->GetValString(value);
}

bool cSCPropertyHolder::GetProperty(std::uint32_t id, std::uint32_t iid, void** out) const {
    cISCProperty* p = GetProperty(id);
    return p && p->GetPropertyValue()->GetValUnknown(iid, out);
}

bool cSCPropertyHolder::GetProperty(std::uint32_t id, void* data, std::uint32_t& size) const {
    cISCProperty* p = GetProperty(id);
    return p && p->GetPropertyValue()->GetValVoid(data, size);
}

void cSCPropertyHolder::InsertSorted(std::uint32_t id, cISCProperty* property) {
    auto it = std::lower_bound(items_.begin(), items_.end(), id,
                               [](const Entry& e, std::uint32_t v) { return e.id < v; });
    items_.insert(it, Entry{id, cRZAutoRefCount<cISCProperty>(property)});
}

cISCProperty* cSCPropertyHolder::FindOrCreate(std::uint32_t id, bool& created) {
    if (cISCProperty* existing = GetProperty(id)) {
        created = false;
        return existing;
    }
    auto* property = new cSCProperty(id);
    InsertSorted(id, property);
    created = true;
    return property;
}

void cSCPropertyHolder::AddProperty(cISCProperty* property, bool) {
    const std::uint32_t id = property->GetPropertyID();
    auto it = std::lower_bound(items_.begin(), items_.end(), id,
                               [](const Entry& e, std::uint32_t v) { return e.id < v; });
    if (it != items_.end() && it->id == id) it->property = property;   // replaces the object
    else InsertSorted(id, property);
}

void cSCPropertyHolder::AddProperty(std::uint32_t id, const cIGZVariant* value, bool replace) {
    if (!value) return;
    auto* property = new cSCProperty(id);
    property->GetPropertyValue()->CopyFrom(*value);
    property->AddRef();
    AddProperty(property, replace);
    property->Release();
}

void cSCPropertyHolder::AddProperty(std::uint32_t id, std::uint32_t value, bool) {
    bool created;
    FindOrCreate(id, created)->GetPropertyValue()->SetValUint32(value);
}

void cSCPropertyHolder::AddProperty(std::uint32_t id, const cIGZString& value, bool) {
    bool created;
    FindOrCreate(id, created)->GetPropertyValue()->SetValString(value);
}

void cSCPropertyHolder::AddProperty(std::uint32_t id, cIGZUnknown* value, bool) {
    bool created;
    FindOrCreate(id, created)->GetPropertyValue()->SetValUnknown(value);
}

void cSCPropertyHolder::AddProperty(std::uint32_t id, const void* data, std::uint32_t size, bool) {
    bool created;
    FindOrCreate(id, created)->GetPropertyValue()->SetValVoid(data, size);
}

void cSCPropertyHolder::CopyAddProperty(cISCProperty* property, bool replace) {
    auto* copy = new cSCProperty(property->GetPropertyID());
    copy->GetPropertyValue()->CopyFrom(*property->GetPropertyValue());
    copy->AddRef();
    AddProperty(copy, replace);
    copy->Release();
}

bool cSCPropertyHolder::RemoveProperty(std::uint32_t id, bool) {
    auto it = Find(id);
    if (it == items_.end()) return false;
    items_.erase(items_.begin() + (it - items_.cbegin()));
    return true;
}

void cSCPropertyHolder::EnumProperties(void (*callback)(cISCProperty*, void*), void* context) const {
    for (const Entry& e : items_) callback(e.property.get(), context);
}

void cSCPropertyHolder::EnumProperties(bool (*filter)(cISCProperty*, void*), void (*callback)(cISCProperty*, void*),
                                       void* context) const {
    for (const Entry& e : items_)
        if (filter(e.property.get(), context)) callback(e.property.get(), context);
}

bool cSCPropertyHolder::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    stream.SetUint32(static_cast<std::uint32_t>(items_.size()));
    for (const Entry& e : items_) {
        stream.SetUint32(e.id);
        e.property->Write(stream);
    }
    return !stream.GetError();
}

bool cSCPropertyHolder::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2) return false;
    std::uint32_t count = 0;
    stream.GetUint32(count);
    items_.clear();
    while (count--) {
        std::uint32_t entryId = 0;
        stream.GetUint32(entryId);   // the entry id; the property record carries it again
        auto* property = new cSCProperty();
        property->AddRef();
        property->Read(stream);
        if (!stream.GetError()) {
            const cIGZVariant* value = property->GetPropertyValue();
            const std::uint16_t type = value->GetType();
            // (sic) an empty value is never added, an object value only when it has no object
            if (type != 0 && (type != 0x10 || value->RefIGZUnknown() == nullptr)) AddProperty(property, false);
        }
        property->Release();
    }
    return !stream.GetError();
}

}  // namespace sc4
