#include "sc4/Exemplar.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "rz/RZConvert.h"
#include "sc4/DBSegment.h"

namespace sc4 {

namespace {
ExemplarNotifyFn g_notify = nullptr;
std::unordered_map<std::uint32_t, std::string> g_propertyNames;

void Notify(std::uint32_t message, const cGZPersistResourceKey& key, cIGZSerializable* object) {
    if (g_notify) g_notify(message, key, object);
}

template <class It, class Entry>
It FindLowerBound(It first, It last, std::uint32_t id) {
    return std::lower_bound(first, last, id, [](const Entry& e, std::uint32_t v) { return e.id < v; });
}
}  // namespace

void SetExemplarNotifier(ExemplarNotifyFn fn) { g_notify = fn; }

// ---- cSCResProperty -------------------------------------------------------------------------
bool cSCResProperty::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISCResProperty || iid == kGZIID_cISCProperty || iid == kGZIID_cIGZUnknown) {
        *out = static_cast<cISCProperty*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cIGZVariant) {
        *out = static_cast<cIGZVariant*>(&value_);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cSCResProperty::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

cIGZVariant* cSCResProperty::GetPropertyValue() {
    if (value_.RefIGZUnknown() == nullptr) {
        cIGZPersistResourceManager* manager = PersistResourceManagerService();
        void* resource = nullptr;
        if (manager && manager->GetResource(key_, kGZIID_cIGZUnknown, &resource, 0, 0)) {
            auto* object = static_cast<cIGZUnknown*>(resource);
            value_.SetValUnknown(object);
            object->Release();
        }
    }
    return &value_;
}

bool cSCResProperty::SetPropertyValue(const cIGZVariant& value) {
    if (value.GetType() != 0x8010) return false;
    cIGZUnknown* object = const_cast<cIGZVariant&>(value).GetValUnknown();   // RefIGZUnknown
    if (!object) return false;
    void* raw = nullptr;
    if (!object->QueryInterface(kGZIID_cIGZPersistResource, &raw)) return false;
    auto* resource = static_cast<cIGZPersistResource*>(raw);
    const bool ok = SetPropertyValue(resource);
    resource->Release();
    return ok;
}

bool cSCResProperty::SetPropertyValue(cIGZPersistResource* resource) {
    if (!resource) return false;
    value_.RefIGZUnknown(resource);
    key_ = resource->GetKey();
    return true;
}

// ---- cSCExemplarProperties ------------------------------------------------------------------------
std::vector<cSCExemplarProperties::Entry>::iterator cSCExemplarProperties::LowerBound(std::uint32_t id) {
    return std::lower_bound(cache_.begin(), cache_.end(), id, [](const Entry& e, std::uint32_t v) { return e.id < v; });
}
std::vector<cSCExemplarProperties::Entry>::const_iterator cSCExemplarProperties::LowerBound(std::uint32_t id) const {
    return std::lower_bound(cache_.begin(), cache_.end(), id, [](const Entry& e, std::uint32_t v) { return e.id < v; });
}

bool cSCExemplarProperties::HasProperty(std::uint32_t id) const {
    auto it = LowerBound(id);
    return it != cache_.end() && it->id == id;
}

bool cSCExemplarProperties::GetPropertyList(cIGZUnknownList** out) const {
    if (!out) return false;
    auto* list = new cRZUnknownList();
    list->AddRef();
    for (const Entry& e : cache_) list->PushBack(e.property.get());
    *out = list;
    return true;
}

cISCProperty* cSCExemplarProperties::GetProperty(std::uint32_t id) const {
    auto it = LowerBound(id);
    return (it != cache_.end() && it->id == id) ? it->property.get() : nullptr;
}

void cSCExemplarProperties::AddProperty(cISCProperty* property, bool notify) {
    if (!property) return;
    cSCPropertyHolder::AddProperty(property, false);
    const std::uint32_t id = property->GetPropertyID();
    auto it = LowerBound(id);
    if (it == cache_.end() || it->id != id) cache_.insert(it, Entry{id, cRZAutoRefCount<cISCProperty>(property)});
    else it->property = property;
    if (notify) NotifyChanged();
}

bool cSCExemplarProperties::RemoveProperty(std::uint32_t id, bool notify) {
    if (!cSCPropertyHolder::RemoveProperty(id, false)) return false;
    auto it = LowerBound(id);
    if (it != cache_.end() && it->id == id) {
        cISCPropertyHolder* parent = ParentProperties();
        if (parent && parent->HasProperty(id)) it->property = parent->GetProperty(id);
        else cache_.erase(it);
    }
    if (notify) NotifyChanged();
    return true;
}

void cSCExemplarProperties::RemoveAllProperties(bool notify) {
    const bool hadLocal = Count() != 0;
    cSCPropertyHolder::RemoveAllProperties(false);
    cISCPropertyHolder* parent = ParentProperties();
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (parent && parent->HasProperty(it->id)) {
            it->property = parent->GetProperty(it->id);
            ++it;
        } else {
            it = cache_.erase(it);
        }
    }
    if (hadLocal && notify) NotifyChanged();
}

void cSCExemplarProperties::CompactProperties() {
    cSCPropertyHolder::CompactProperties();
    cache_.shrink_to_fit();
    CompactParent();
}

void cSCExemplarProperties::GenerateCachedMap() {
    cache_.clear();
    cache_.reserve(Count());
    if (cISCPropertyHolder* parent = ParentProperties()) {
        cIGZUnknownList* list = nullptr;
        if (parent->GetPropertyList(&list) && list) {
            void* raw = nullptr;
            while (list->Size() != 0) {
                raw = nullptr;
                if (!list->PopFront(kGZIID_cISCProperty, &raw)) break;
                auto* property = static_cast<cISCProperty*>(raw);
                const std::uint32_t id = property->GetPropertyID();
                auto it = LowerBound(id);
                if (it == cache_.end() || it->id != id) cache_.insert(it, Entry{id, cRZAutoRefCount<cISCProperty>(property)});
                else it->property = property;
                property->Release();
            }
            list->Release();
        }
    }
    // own properties override the inherited ones
    cIGZUnknownList* local = nullptr;
    if (cSCPropertyHolder::GetPropertyList(&local) && local) {
        for (std::uint32_t i = 0, n = local->Size(); i < n; ++i) {
            void* raw = nullptr;
            if (!local->GetElement(i, kGZIID_cISCProperty, &raw)) continue;
            auto* property = static_cast<cISCProperty*>(raw);
            const std::uint32_t id = property->GetPropertyID();
            auto it = LowerBound(id);
            if (it == cache_.end() || it->id != id) cache_.insert(it, Entry{id, cRZAutoRefCount<cISCProperty>(property)});
            else it->property = property;
            property->Release();
        }
        local->Release();
    }
}

// ---- cSCResExemplar / cohort -----------------------------------------------------------------------
bool cSCResExemplar::QueryInterface(std::uint32_t iid, void** out) {
    *out = nullptr;
    if (iid == kGZIID_cISCResExemplar) *out = static_cast<cISCResExemplar*>(this);
    else if (iid == kGZIID_cIGZPersistResource) *out = static_cast<cIGZPersistResource*>(static_cast<cGZPersistResource*>(this));
    else if (iid == kGZIID_cISCPropertyHolder) *out = static_cast<cISCPropertyHolder*>(static_cast<cSCExemplarProperties*>(this));
    else return false;
    AddRef();
    return true;
}

void cSCResExemplar::SetCohort(cISCResExemplarCohort* cohort, bool notify) {
    if (cohort_.get() == cohort) return;
    cohort_ = cohort;
    GenerateCachedMap();
    if (notify) NotifyChanged();
}

cISCPropertyHolder* cSCResExemplar::ParentProperties() const {
    return cohort_ ? cohort_->AsISCPropertyHolder() : nullptr;
}
void cSCResExemplar::CompactParent() {
    if (cohort_) cohort_->CompactProperties();
}
void cSCResExemplar::NotifyChanged() {
    Notify(kExemplarChangedMessage, cGZPersistResource::GetKey(), static_cast<cISCResExemplar*>(this));
}

void cSCResExemplar::DebugInstanceTag(cIGZString& out) const {
    out.AppendChars("Exemplar ", 9);
    if (cISCProperty* type = GetProperty(0x10)) {
        std::uint32_t value = 0;
        type->GetPropertyValue()->GetValUint32(value);
        out.AppendChars(value == 0 ? "(Other)" : "(Unknown)", value == 0 ? 7 : 9);
    }
    if (cISCProperty* name = GetProperty(0x20)) {
        cRZString text;
        name->GetPropertyValue()->GetValString(text);
        cRZString formatted;
        formatted.Sprintf(" '%s'", text.ToChar());
        out.AppendString(formatted);
    }
}

bool cSCResExemplarCohort::QueryInterface(std::uint32_t iid, void** out) {
    *out = nullptr;
    if (iid == kGZIID_cISCResExemplarCohort) *out = static_cast<cISCResExemplarCohort*>(this);
    else if (iid == kGZIID_cIGZPersistResource) *out = static_cast<cIGZPersistResource*>(static_cast<cGZPersistResource*>(this));
    else if (iid == kGZIID_cISCPropertyHolder) *out = static_cast<cISCPropertyHolder*>(static_cast<cSCExemplarProperties*>(this));
    else return false;
    AddRef();
    return true;
}

void cSCResExemplarCohort::SetParent(cISCResExemplarCohort* parent, bool notify) {
    if (parent_.get() == parent) return;
    parent_ = parent;
    GenerateCachedMap();
    if (notify) NotifyChanged();
}

cISCPropertyHolder* cSCResExemplarCohort::ParentProperties() const {
    return parent_ ? parent_->AsISCPropertyHolder() : nullptr;
}
void cSCResExemplarCohort::CompactParent() {
    if (parent_) parent_->CompactProperties();
}
void cSCResExemplarCohort::NotifyChanged() {
    Notify(kCohortChangedMessage, cGZPersistResource::GetKey(), static_cast<cISCResExemplarCohort*>(this));
}

// ---- factories --------------------------------------------------------------------------------------
void cSCResExemplarFactoryBase::SetPropertyNames(std::unordered_map<std::uint32_t, std::string> names) {
    g_propertyNames = std::move(names);
}

bool cSCResExemplarFactoryBase::GetPropertyReadableName(std::uint32_t id, std::string& out) {
    auto it = g_propertyNames.find(id);
    if (it == g_propertyNames.end()) return false;
    out = it->second;
    return true;
}

namespace {

// Text exemplar reader: the rest of the record as lines "name = value".
struct ExemplarTextReader {
    explicit ExemplarTextReader(cIGZPersistDBSerialRecord& record) {
        const std::uint32_t remaining = record.GetSize() - record.GetPosition();
        data.resize(remaining);
        record.GetFieldVoid(data.data(), remaining);
    }
    bool GetDataEntry(cRZString& name, cRZString& value) {
        while (cursor < data.size()) {
            std::size_t begin = cursor;
            const char* newline = static_cast<const char*>(std::memchr(data.data() + begin, '\n', data.size() - begin));
            std::size_t end;
            if (!newline) {
                end = data.size();
                cursor = data.size();
            } else {
                end = static_cast<std::size_t>(newline - data.data());
                cursor = end + 1;
            }
            if (end > begin && data[end - 1] == '\r') --end;
            if (end <= begin) continue;
            const char* equals = static_cast<const char*>(std::memchr(data.data() + begin, '=', end - begin));
            if (!equals) return false;
            const std::size_t eq = static_cast<std::size_t>(equals - data.data());
            name.AssignFromChars(data.data() + begin, static_cast<std::uint32_t>(eq - begin));
            name.Trim();
            value.AssignFromChars(data.data() + eq + 1, static_cast<std::uint32_t>(end - eq - 1));
            value.Trim();
            return true;
        }
        return false;
    }
    std::vector<char> data;
    std::size_t cursor = 0;
};

// Splits `text` at the first `delimiter` (token excludes it; the delimiter is consumed).
std::string TakeToken(std::string& text, char delimiter) {
    const std::size_t at = text.find(delimiter);
    std::string token = text.substr(0, at);
    text = at == std::string::npos ? std::string() : text.substr(at + 1);
    return token;
}
std::string TrimmedLeft(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

bool EqualsNoCase(const std::string& a, const char* b) {
    return a.size() == std::strlen(b) && std::equal(a.begin(), a.end(), b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

bool InterpretKey(ExemplarTextReader& reader, const char* name, cGZPersistResourceKey& key) {
    cRZString entryName, entryValue;
    if (!reader.GetDataEntry(entryName, entryValue)) return false;
    if (!EqualsNoCase(entryName.ToChar(), name)) return false;
    std::string value = entryValue.ToChar();
    if (value.rfind("Key:{", 0) != 0 || value.empty() || value.back() != '}') return false;
    std::string rest = value.substr(value.find('{') + 1);
    key.group = RZ::ConvertToUint(TakeToken(rest, ',').c_str());
    key.instance = RZ::ConvertToUint(TakeToken(rest, ',').c_str());
    key.type = RZ::ConvertToUint(rest.c_str());
    return true;
}

bool InterpretUint(ExemplarTextReader& reader, const char* name, std::uint32_t& value) {
    cRZString entryName, entryValue;
    if (!reader.GetDataEntry(entryName, entryValue)) return false;
    if (!EqualsNoCase(entryName.ToChar(), name)) return false;
    value = RZ::ConvertToUint(entryValue.ToChar());
    return true;
}

// "0xID:{"Name"}" = "Type:count:{values}"
bool InterpretProperty(ExemplarTextReader& reader, std::uint32_t& id, cRZVariant& variant) {
    cRZString entryName, entryValue;
    if (!reader.GetDataEntry(entryName, entryValue)) return false;
    id = RZ::ConvertToUint(entryName.ToChar());
    std::string text = entryValue.ToChar();
    const std::string typeName = TakeToken(text, ':');
    std::uint32_t count = RZ::ConvertToUint(text.c_str());
    {   // discard "count:"
        const std::size_t colon = text.find(':');
        text = colon == std::string::npos ? std::string() : text.substr(colon + 1);
    }
    struct { const char* name; unsigned type; } kTypes[] = {
        {"Uint8", 2}, {"Sint8", 3}, {"Uint16", 4}, {"Sint16", 5}, {"Uint32", 6}, {"Sint32", 7}, {"Uint64", 8},
        {"UintPtr", 0x11}, {"Sint64", 9}, {"Float32", 10}, {"String", 0x800e}, {"Bool", 1}};
    unsigned type = 0;
    for (auto& t : kTypes)
        if (typeName == t.name) type = t.type;
    if (type == 0) return false;
    if (type == 0x800e) count = 0;
    if (text.empty() || text[0] != '{') return false;
    text = text.substr(1);

    if (type == 0x800e) {
        std::string token = text.substr(0, text.find('}'));
        // trim spaces/tabs
        std::size_t b = 0, e = token.size();
        while (b < e && (token[b] == ' ' || token[b] == '\t')) ++b;
        while (e > b && (token[e - 1] == ' ' || token[e - 1] == '\t')) --e;
        token = token.substr(b, e - b);
        if (!token.empty() && token.front() == '"') token.erase(0, 1);
        if (!token.empty() && token.back() == '"') token.pop_back();
        cRZString value(token.c_str());
        variant.SetValString(value);
        return true;
    }
    if (count == 0) {   // scalar
        const char* s = text.c_str();
        switch (type) {
            case 1: {
                std::string lower = TrimmedLeft(text);
                for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                variant.SetValBool(lower.rfind("true", 0) == 0);
                break;
            }
            case 10: variant.SetValFloat32(static_cast<float>(RZ::ConvertEnglishToDouble(s))); break;
            case 8: variant.SetValUint64(RZ::ConvertHexToUint64(s)); break;
            case 9: variant.SetValSint64(static_cast<std::int64_t>(RZ::ConvertHexToUint64(s))); break;
            case 0x11: variant.SetValUint64(RZ::ConvertHexToUint64(s)); break;   // (sic: stored as Uint64)
            default: {
                const std::uint32_t v = RZ::ConvertToUint(s);
                switch (type) {
                    case 2: variant.SetValUint8(static_cast<std::uint8_t>(v)); break;
                    case 3: variant.SetValSint8(static_cast<std::int8_t>(v)); break;
                    case 4: variant.SetValUint16(static_cast<std::uint16_t>(v)); break;
                    case 5: variant.SetValSint16(static_cast<std::int16_t>(v)); break;
                    case 6: variant.SetValUint32(v); break;
                    case 7: variant.SetValSint32(static_cast<std::int32_t>(v)); break;
                    default: break;
                }
            }
        }
        return true;
    }
    auto nextElement = [&]() {   // up to ',' (the closing '}' ends the last one)
        text = TrimmedLeft(text);
        std::string element = text.substr(0, text.find(','));
        const std::size_t comma = text.find(',');
        text = comma == std::string::npos ? std::string() : text.substr(comma + 1);
        return element;
    };
    switch (type) {
        case 1: {
            std::vector<bool> bits;
            std::vector<unsigned char> raw(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                std::string e = nextElement();
                for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                raw[i] = e.rfind("true", 0) == 0;
            }
            variant.RefBool(reinterpret_cast<const bool*>(raw.data()), count);
            break;
        }
        case 2: case 3: {
            std::vector<std::uint8_t> v(count);
            for (std::uint32_t i = 0; i < count; ++i) v[i] = static_cast<std::uint8_t>(RZ::ConvertToUint(nextElement().c_str()));
            if (type == 2) variant.RefUint8(v.data(), count);
            else variant.RefSint8(reinterpret_cast<const std::int8_t*>(v.data()), count);
            break;
        }
        case 4: case 5: {
            std::vector<std::uint16_t> v(count);
            for (std::uint32_t i = 0; i < count; ++i) v[i] = static_cast<std::uint16_t>(RZ::ConvertToUint(nextElement().c_str()));
            if (type == 4) variant.RefUint16(v.data(), count);
            else variant.RefSint16(reinterpret_cast<const std::int16_t*>(v.data()), count);
            break;
        }
        case 6: case 7: {
            std::vector<std::uint32_t> v(count);
            for (std::uint32_t i = 0; i < count; ++i) v[i] = RZ::ConvertToUint(nextElement().c_str());
            if (type == 6) variant.RefUint32(v.data(), count);
            else variant.RefSint32(reinterpret_cast<const std::int32_t*>(v.data()), count);
            break;
        }
        case 8: case 9: case 0x11: {
            std::vector<std::uint64_t> v(count);
            for (std::uint32_t i = 0; i < count; ++i) v[i] = RZ::ConvertHexToUint64(nextElement().c_str());
            if (type == 8) variant.RefUint64(v.data(), count);
            else if (type == 9) variant.RefSint64(reinterpret_cast<const std::int64_t*>(v.data()), count);
            else variant.RefUintPtr(reinterpret_cast<const std::uintptr_t*>(v.data()), count);
            break;
        }
        case 10: {
            std::vector<float> v(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                text = TrimmedLeft(text);
                std::string e = i + 1 < count ? TakeToken(text, ',') : TakeToken(text, '}');
                v[i] = static_cast<float>(RZ::ConvertEnglishToDouble(e.c_str()));
            }
            variant.RefFloat32(v.data(), count);
            break;
        }
        default: break;
    }
    return true;
}

void WriteText(cIGZPersistDBSerialRecord& record, const std::string& text) {
    record.SetFieldVoid(text.data(), static_cast<std::uint32_t>(text.size()));
}

void WriteKeyEntry(cIGZPersistDBSerialRecord& record, const char* name, const cGZPersistResourceKey& key) {
    char line[512];
    const int n = std::snprintf(line, sizeof line, "%s=Key:{0x%08x,0x%08x,0x%08x}\r\n", name, key.group, key.instance, key.type);
    WriteText(record, std::string(line, static_cast<std::size_t>(n)));
}

void WriteUintEntry(cIGZPersistDBSerialRecord& record, const char* name, std::uint32_t value) {
    char line[512];
    const int n = std::snprintf(line, sizeof line, "%s=0x%08x\r\n", name, value);
    WriteText(record, std::string(line, static_cast<std::size_t>(n)));
}

std::string HexElement(std::uint64_t v, bool last) {
    char buffer[40];
    if (last) std::snprintf(buffer, sizeof buffer, "0x%08x%08x", static_cast<unsigned>(v >> 32), static_cast<unsigned>(v));
    else std::snprintf(buffer, sizeof buffer, "%08x%08x,", static_cast<unsigned>(v >> 32), static_cast<unsigned>(v));
    return buffer;
}

void WriteVariantEntry(cIGZPersistDBSerialRecord& record, std::uint32_t id, const std::string& name, const cIGZVariant& v) {
    char head[1024];
    int n = std::snprintf(head, sizeof head, "0x%08x:{\"%s\"}=", id, name.c_str());
    std::string line(head, static_cast<std::size_t>(n));
    const std::uint32_t count = v.GetCount();
    const std::uint16_t type = v.GetType();
    char buffer[64];
    auto append = [&](const char* fmt, auto... args) {
        const int w = std::snprintf(buffer, sizeof buffer, fmt, args...);
        line.append(buffer, static_cast<std::size_t>(w));
    };
    switch (type) {
        case 0x8001: {
            append("Bool:%d:{", count);
            const bool* data = v.RefBool();
            for (std::uint32_t i = 0; i < count; ++i) {
                line += data[i] ? "True" : "False";
                if (i + 1 < count) line += ",";
            }
            break;
        }
        case 0x8002: case 0x8003: {
            append(type == 0x8002 ? "Uint8:%d:{" : "Sint8:%d:{", count);
            for (std::uint32_t i = 0; i < count; ++i) {
                const unsigned x = type == 0x8002 ? v.RefUint8()[i] : static_cast<unsigned>(static_cast<int>(v.RefSint8()[i]));
                append(i + 1 < count ? "0x%02x," : "0x%02x", x);
            }
            break;
        }
        case 0x8004: case 0x8005: {
            append(type == 0x8004 ? "Uint16:%d:{" : "Sint16:%d:{", count);
            for (std::uint32_t i = 0; i < count; ++i) {
                const unsigned x = type == 0x8004 ? v.RefUint16()[i] : static_cast<unsigned>(static_cast<int>(v.RefSint16()[i]));
                append(i + 1 < count ? "0x%04x," : "0x%04x", x);
            }
            break;
        }
        case 0x8006: case 0x8007: {
            append(type == 0x8006 ? "Uint32:%d:{" : "Sint32:%d:{", count);
            for (std::uint32_t i = 0; i < count; ++i) {
                const unsigned x = type == 0x8006 ? v.RefUint32()[i] : static_cast<unsigned>(v.RefSint32()[i]);
                append(i + 1 < count ? "0x%08x," : "0x%08x", x);
            }
            break;
        }
        case 0x8008: case 0x8009: case 0x8011: {
            append(type == 0x8008 ? "Uint64:%d:{" : (type == 0x8009 ? "Sint64:%d:{" : "UintPtr:%d:{"), count);
            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint64_t x = type == 0x8008 ? v.RefUint64()[i]
                                  : type == 0x8009 ? static_cast<std::uint64_t>(v.RefSint64()[i])
                                                   : static_cast<std::uint64_t>(v.RefUintPtr()[i]);
                line += HexElement(x, i + 1 == count);
            }
            break;
        }
        case 0x800a: {
            append("Float32:%d:{", count);
            for (std::uint32_t i = 0; i < count; ++i) {
                char number[64];
                int w = std::snprintf(number, sizeof number, "%f", static_cast<double>(v.RefFloat32()[i]));
                RZ::RemoveUnnecessaryFloatingPointDigits(number, w);
                line += number;
                if (i + 1 < count) line += ",";
            }
            break;
        }
        case 0x800b: case 0x800c: case 0x800d: case 0x800f: case 0x8010:
            break;
        case 0x800e: {
            cRZString text;
            v.GetValString(text);
            append("String:%d:{\"", 0);
            line += text.ToChar();
            line += "\"";
            break;
        }
        default: {   // scalars: the count prints as 0 ("Type:0:{value")
            switch (type) {
                case 1: { bool b = false; v.GetValBool(b); append("Bool:%d:{%s", count, b ? "True" : "False"); break; }
                case 2: { std::uint8_t x = 0; v.GetValUint8(x); append("Uint8:%d:{0x%02x", count, x); break; }
                case 3: { std::int8_t x = 0; v.GetValSint8(x); append("Sint8:%d:{0x%02x", count, static_cast<unsigned>(static_cast<int>(x))); break; }
                case 4: { std::uint16_t x = 0; v.GetValUint16(x); append("Uint16:%d:{0x%04x", count, x); break; }
                case 5: { std::int16_t x = 0; v.GetValSint16(x); append("Sint16:%d:{0x%04x", count, static_cast<unsigned>(static_cast<int>(x))); break; }
                case 6: { std::uint32_t x = 0; v.GetValUint32(x); append("Uint32:%d:{0x%08x", count, x); break; }
                case 7: { std::int32_t x = 0; v.GetValSint32(x); append("Sint32:%d:{0x%08x", count, static_cast<unsigned>(x)); break; }
                case 8: { std::uint64_t x = 0; v.GetValUint64(x); append("Uint64:%d:{0x%08x%08x", count, static_cast<unsigned>(x >> 32), static_cast<unsigned>(x)); break; }
                case 9: { std::int64_t x = 0; v.GetValSint64(x); append("Sint64:%d:{0x%08x%08x", count, static_cast<unsigned>(static_cast<std::uint64_t>(x) >> 32), static_cast<unsigned>(x)); break; }
                case 10: {
                    float x = 0;
                    v.GetValFloat32(x);
                    char number[64];
                    int w = std::snprintf(number, sizeof number, "%f", static_cast<double>(x));
                    RZ::RemoveUnnecessaryFloatingPointDigits(number, w);
                    append("Float32:%d:{%s", count, number);
                    break;
                }
                case 0x11: { std::uintptr_t x = 0; v.GetValUintPtr(x); append("UintPtr:%d:{0x%08x%08x", count, static_cast<unsigned>(static_cast<std::uint64_t>(x) >> 32), static_cast<unsigned>(x)); break; }
                default: break;
            }
        }
    }
    line += "}\r\n";
    WriteText(record, line);
}

}  // namespace

bool cSCResExemplarFactoryBase::SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) {
    if (resource.GetType() != ResourceType()) return false;
    cIGZPersistResourceManager* manager = PersistResourceManagerService();
    char header[8] = {};
    record.GetFieldVoid(header, 8);
    const bool binary = std::memcmp(header, BinaryMagic(), 8) == 0;
    const bool text = !binary && std::memcmp(header, TextMagic(), 6) == 0 && header[6] == '#' && header[7] == '#';
    if (!binary && !(std::memcmp(header, TextMagic(), 4) == 0 && header[4] == 'T' && false) && !text) {
        // the original compares the first dword to the 'B' or 'T' variant of the magic
        std::uint32_t first = 0;
        std::memcpy(&first, header, 4);
        std::uint32_t binaryFirst = 0, textFirst = 0;
        std::memcpy(&binaryFirst, BinaryMagic(), 4);
        std::memcpy(&textFirst, TextMagic(), 4);
        if (first != binaryFirst && first != textFirst) return false;
        if (first == textFirst) {
            // fall through to the text path below
        } else {
            return false;
        }
    }
    const bool isText = !binary;
    cISCPropertyHolder* holder = PropertyHolder(resource);
    (void)holder;
    bool ok = true;
    cGZPersistResourceKey parentKey;
    auto resolveParent = [&](const cGZPersistResourceKey& key) {
        cISCResExemplarCohort* parent = nullptr;
        if (key.type != 0 || key.group != 0 || key.instance != 0) {
            void* raw = nullptr;
            if (manager && manager->GetResource(key, kGZIID_cISCResExemplarCohort, &raw, 0, 0)) parent = static_cast<cISCResExemplarCohort*>(raw);
            else ok = false;
        }
        SetParentFromKey(resource, parent);
        if (parent) parent->Release();
        ClearProperties(resource);
        return ok;
    };
    if (!isText) {
        ok = record.GetFieldResKey(parentKey);
        if (!resolveParent(parentKey)) return false;
        std::uint32_t count = 0;
        if (!record.GetFieldUint32(count)) return false;
        for (std::uint32_t i = 0; i < count && ok; ++i) {
            std::uint32_t id = 0;
            std::uint8_t kind = 0;
            ok = record.GetFieldUint32(id) && record.GetFieldUint8(kind);
            if (!ok) break;
            if (kind == 0) {
                cIGZVariant* value = nullptr;
                ok = record.GetFieldVariant(&value);
                if (ok && skipped_.find(id) == skipped_.end()) {
                    auto* property = new cSCProperty(id);
                    property->AddRef();
                    property->GetPropertyValue()->CopyFrom(*value);
                    AddLocalProperty(resource, property);
                    property->Release();
                }
                if (value) value->Release();
            } else if (kind == 1) {
                cGZPersistResourceKey key;
                ok = record.GetFieldResKey(key);
                if (ok && skipped_.find(id) == skipped_.end()) {
                    auto* property = new cSCResProperty();
                    property->AddRef();
                    property->SetPropertyID(id);
                    property->SetPropertyKey(key);
                    void* raw = nullptr;
                    ok = manager && manager->GetResource(key, kGZIID_cIGZPersistResource, &raw, 0, 0);
                    if (ok) {
                        auto* object = static_cast<cIGZPersistResource*>(raw);
                        property->SetPropertyValue(object);
                        AddLocalProperty(resource, property);
                        object->Release();
                    }
                    property->Release();
                }
            } else {
                ok = false;
            }
        }
    } else {
        char eol[2];
        record.GetFieldVoid(eol, 2);
        ExemplarTextReader reader(record);
        if (!InterpretKey(reader, "ParentCohort", parentKey)) return false;
        if (!resolveParent(parentKey)) return false;
        std::uint32_t count = 0;
        if (!InterpretUint(reader, "PropCount", count)) return false;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t id = 0;
            cRZVariant variant;
            if (!InterpretProperty(reader, id, variant)) { ok = false; break; }
            if (skipped_.find(id) != skipped_.end()) break;   // (sic) the original stops at a skipped id
            auto* property = new cSCProperty(id);
            property->AddRef();
            property->GetPropertyValue()->CopyFrom(variant);
            AddLocalProperty(resource, property);
            property->Release();
        }
    }
    if (ok) Finish(resource);
    return ok;
}

bool cSCResExemplarFactoryBase::SerialWrite(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) {
    if (resource.GetType() != ResourceType()) return false;
    cGZPersistResourceKey parentKey;
    if (cISCResExemplarCohort* parent = ParentOf(resource)) parent->GetKey(parentKey);
    const std::vector<cISCProperty*> properties = LocalProperties(resource);
    if (!textWrite_) {
        if (!record.SetFieldVoid(BinaryMagic(), 8)) return false;
        if (!record.SetFieldResKey(parentKey)) return false;
        if (!record.SetFieldUint32(static_cast<std::uint32_t>(properties.size()))) return false;
        for (cISCProperty* property : properties) {
            bool ok = record.SetFieldUint32(property->GetPropertyID());
            void* raw = nullptr;
            if (property->QueryInterface(kGZIID_cISCResProperty, &raw)) {
                auto* res = static_cast<cSCResProperty*>(static_cast<cISCProperty*>(raw));
                ok = ok && record.SetFieldUint8(1) && record.SetFieldResKey(res->GetPropertyKey());
                res->Release();
            } else {
                ok = ok && record.SetFieldUint8(0) && record.SetFieldVariant(property->GetPropertyValue());
            }
            if (!ok) return false;
        }
        return true;
    }
    const std::string magic = std::string(TextMagic(), 8) + "\r\n";
    if (!record.SetFieldVoid(magic.data(), static_cast<std::uint32_t>(magic.size()))) return false;
    WriteKeyEntry(record, "ParentCohort", parentKey);
    WriteUintEntry(record, "PropCount", static_cast<std::uint32_t>(properties.size()));
    for (cISCProperty* property : properties) {
        std::string name;
        if (!GetPropertyReadableName(property->GetPropertyID(), name)) name = "unknown";
        WriteVariantEntry(record, property->GetPropertyID(), name, *property->GetPropertyValue());
    }
    return true;
}

// -- exemplar-specific accessors -------------------------------------------------------------------
namespace {
cSCResExemplar* AsExemplar(cIGZPersistResource& r) {
    void* raw = nullptr;
    if (!r.QueryInterface(kGZIID_cISCResExemplar, &raw)) return nullptr;
    auto* exemplar = static_cast<cSCResExemplar*>(static_cast<cISCResExemplar*>(raw));
    exemplar->Release();    // (the resource keeps it alive)
    return exemplar;
}
cSCResExemplarCohort* AsCohort(cIGZPersistResource& r) {
    void* raw = nullptr;
    if (!r.QueryInterface(kGZIID_cISCResExemplarCohort, &raw)) return nullptr;
    auto* cohort = static_cast<cSCResExemplarCohort*>(static_cast<cISCResExemplarCohort*>(raw));
    cohort->Release();
    return cohort;
}
std::vector<cISCProperty*> Collect(const cSCPropertyHolder& holder) {
    std::vector<cISCProperty*> out;
    cIGZUnknownList* list = nullptr;
    if (holder.cSCPropertyHolder::GetPropertyList(&list) && list) {
        for (std::uint32_t i = 0, n = list->Size(); i < n; ++i) {
            void* raw = nullptr;
            if (list->GetElement(i, kGZIID_cISCProperty, &raw)) {
                out.push_back(static_cast<cISCProperty*>(raw));
                static_cast<cISCProperty*>(raw)->Release();   // owned by the holder
            }
        }
        list->Release();
    }
    return out;
}
}  // namespace

bool cSCResExemplarFactory::CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) {
    if (type == kResourceTypeExemplar) {
        auto* exemplar = new cSCResExemplar();
        if (exemplar->QueryInterface(iid, out)) return true;
        delete exemplar;     // QueryInterface failed: it was never referenced
    }
    *out = nullptr;
    return false;
}
cISCPropertyHolder* cSCResExemplarFactory::PropertyHolder(cIGZPersistResource& r) {
    auto* e = AsExemplar(r);
    return e ? e->AsISCPropertyHolder() : nullptr;
}
void cSCResExemplarFactory::SetParentFromKey(cIGZPersistResource& r, cISCResExemplarCohort* parent) {
    if (auto* e = AsExemplar(r)) e->SetCohort(parent, false);
}
cISCResExemplarCohort* cSCResExemplarFactory::ParentOf(cIGZPersistResource& r) {
    auto* e = AsExemplar(r);
    return e ? e->GetCohort() : nullptr;
}
void cSCResExemplarFactory::ClearProperties(cIGZPersistResource& r) {
    if (auto* e = AsExemplar(r)) e->RemoveAllProperties(false);
}
void cSCResExemplarFactory::AddLocalProperty(cIGZPersistResource& r, cISCProperty* p) {
    if (auto* e = AsExemplar(r)) e->AddProperty(p, false);
}
void cSCResExemplarFactory::Finish(cIGZPersistResource& r) {
    if (auto* e = AsExemplar(r)) {
        e->GenerateCachedMap();
        e->CompactProperties();
    }
}
std::vector<cISCProperty*> cSCResExemplarFactory::LocalProperties(cIGZPersistResource& r) {
    auto* e = AsExemplar(r);
    return e ? Collect(*e) : std::vector<cISCProperty*>{};
}

bool cSCResExemplarCohortFactory::CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) {
    if (type == kResourceTypeCohort) {
        auto* cohort = new cSCResExemplarCohort();
        if (cohort->QueryInterface(iid, out)) return true;
        delete cohort;
    }
    *out = nullptr;
    return false;
}
cISCPropertyHolder* cSCResExemplarCohortFactory::PropertyHolder(cIGZPersistResource& r) {
    auto* c = AsCohort(r);
    return c ? c->AsISCPropertyHolder() : nullptr;
}
void cSCResExemplarCohortFactory::SetParentFromKey(cIGZPersistResource& r, cISCResExemplarCohort* parent) {
    if (auto* c = AsCohort(r)) c->SetParent(parent, false);
}
cISCResExemplarCohort* cSCResExemplarCohortFactory::ParentOf(cIGZPersistResource& r) {
    auto* c = AsCohort(r);
    return c ? c->GetParent() : nullptr;
}
void cSCResExemplarCohortFactory::ClearProperties(cIGZPersistResource& r) {
    if (auto* c = AsCohort(r)) c->RemoveAllProperties(false);
}
void cSCResExemplarCohortFactory::AddLocalProperty(cIGZPersistResource& r, cISCProperty* p) {
    if (auto* c = AsCohort(r)) c->AddProperty(p, false);
}
void cSCResExemplarCohortFactory::Finish(cIGZPersistResource& r) {
    if (auto* c = AsCohort(r)) {
        c->GenerateCachedMap();
        c->CompactProperties();
    }
}
std::vector<cISCProperty*> cSCResExemplarCohortFactory::LocalProperties(cIGZPersistResource& r) {
    auto* c = AsCohort(r);
    return c ? Collect(*c) : std::vector<cISCProperty*>{};
}


// ---- cSCExemplarPropertyHolder ---------------------------------------------------------------------
bool cSCExemplarPropertyHolder::QueryInterface(std::uint32_t iid, void** out) {
    // (the original answers only 1 and 0x25216283 here, not the base class's other ids)
    if (iid == 1 || iid == kGZIID_cISCPropertyHolder) {
        *out = static_cast<cISCPropertyHolder*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cISCExemplarPropertyHolder) {
        *out = static_cast<cISCExemplarPropertyHolder*>(this);
        AddRef();
        return true;
    }
    return false;
}

bool cSCExemplarPropertyHolder::HasProperty(std::uint32_t id) const {
    if (cSCPropertyHolder::HasProperty(id)) return true;
    cISCResExemplar* exemplar = defaultExemplar_.get();
    return exemplar && exemplar->AsISCPropertyHolder()->HasProperty(id);
}

cISCProperty* cSCExemplarPropertyHolder::GetProperty(std::uint32_t id) const {
    if (cISCProperty* local = cSCPropertyHolder::GetProperty(id)) return local;
    cISCResExemplar* exemplar = defaultExemplar_.get();
    return exemplar ? exemplar->AsISCPropertyHolder()->GetProperty(id) : nullptr;
}

bool cSCExemplarPropertyHolder::GetPropertyList(cIGZUnknownList** out) const {
    cIGZUnknownList* list = nullptr;
    if (!out || !cSCPropertyHolder::GetPropertyList(&list)) return false;
    if (cISCResExemplar* exemplar = defaultExemplar_.get()) {
        cIGZUnknownList* inherited = nullptr;
        if (exemplar->AsISCPropertyHolder()->GetPropertyList(&inherited) && inherited) {
            void* raw = nullptr;
            while (inherited->PopFront(kGZIID_cISCProperty, &raw)) {
                auto* property = static_cast<cISCProperty*>(raw);
                if (!cSCPropertyHolder::HasProperty(property->GetPropertyID())) list->PushBack(property);
                property->Release();
            }
            inherited->Release();
        }
    }
    *out = list;
    return true;
}

void cSCExemplarPropertyHolder::EnumProperties(void (*callback)(cISCProperty*, void*), void* context) const {
    cSCPropertyHolder::EnumProperties(callback, context);
    cISCResExemplar* exemplar = defaultExemplar_.get();
    if (!exemplar) return;
    cIGZUnknownList* inherited = nullptr;
    if (!exemplar->AsISCPropertyHolder()->GetPropertyList(&inherited) || !inherited) return;
    void* raw = nullptr;
    while (inherited->PopFront(kGZIID_cISCProperty, &raw)) {
        auto* property = static_cast<cISCProperty*>(raw);
        if (!cSCPropertyHolder::HasProperty(property->GetPropertyID())) callback(property, context);
        property->Release();
    }
    inherited->Release();
}

void cSCExemplarPropertyHolder::EnumProperties(bool (*filter)(cISCProperty*, void*),
                                               void (*callback)(cISCProperty*, void*), void* context) const {
    cSCPropertyHolder::EnumProperties(filter, callback, context);
    cISCResExemplar* exemplar = defaultExemplar_.get();
    if (!exemplar) return;
    cIGZUnknownList* inherited = nullptr;
    if (!exemplar->AsISCPropertyHolder()->GetPropertyList(&inherited) || !inherited) return;
    void* raw = nullptr;
    while (inherited->PopFront(kGZIID_cISCProperty, &raw)) {
        auto* property = static_cast<cISCProperty*>(raw);
        if (!cSCPropertyHolder::HasProperty(property->GetPropertyID()) && filter(property, context))
            callback(property, context);
        property->Release();
    }
    inherited->Release();
}

void cSCExemplarPropertyHolder::SetDefaultExemplar(cISCResExemplar* exemplar) { defaultExemplar_ = exemplar; }

bool cSCExemplarPropertyHolder::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    if (!cSCPropertyHolder::Write(stream)) return false;
    cGZPersistResourceKey key;
    if (cISCResExemplar* exemplar = defaultExemplar_.get()) {
        exemplar->GetKey(key);
        std::uint32_t embed = 0;
        if (exemplar->AsISCPropertyHolder()->GetProperty(kPropertyEmbedExemplarInSave, embed) && embed != 0) {
            void* resourceRaw = nullptr;
            void* segmentStreamRaw = nullptr;
            if (exemplar->QueryInterface(kGZIID_cIGZPersistResource, &resourceRaw)) {
                auto* resource = static_cast<cIGZPersistResource*>(resourceRaw);
                if (stream.QueryInterface(kGZIID_cISC4DBSegmentOStream, &segmentStreamRaw) && segmentStreamRaw) {
                    auto* segmentStream = static_cast<cISC4DBSegmentOStream*>(segmentStreamRaw);
                    segmentStream->Release();    // (the stream keeps it alive)
                    if (cISC4DBSegment* segment = segmentStream->GetSegment()) {
                        if (cIGZPersistDBSegment* db = segment->AsIGZPersistDBSegment()) {
                            cIGZPersistDBRecord* record = nullptr;
                            if (db->OpenRecord(key, &record, 2)) {
                                void* serialRaw = nullptr;
                                if (record->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &serialRaw)) {
                                    auto* serial = static_cast<cIGZPersistDBSerialRecord*>(serialRaw);
                                    auto* factory = new cSCResExemplarFactory();
                                    factory->AddRef();
                                    factory->SetWriteMode(false);
                                    factory->SerialWrite(*resource, *serial);
                                    factory->Release();
                                    serial->Release();
                                }
                                db->CloseRecord(record);
                            }
                        }
                    }
                }
                resource->Release();
            }
        }
    }
    stream.SetUint32(key.group);
    stream.SetUint32(key.type);
    stream.SetUint32(key.instance);
    return !stream.GetError();
}

bool cSCExemplarPropertyHolder::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2 || !cSCPropertyHolder::Read(stream)) return false;
    cGZPersistResourceKey key;
    stream.GetUint32(key.group);
    stream.GetUint32(key.type);
    stream.GetUint32(key.instance);
    defaultExemplarKey_ = key;
    if (key.group == 0 && key.type == 0 && key.instance == 0) {
        defaultExemplar_ = nullptr;
    } else if (cIGZPersistResourceManager* manager = PersistResourceManagerService()) {
        void* raw = nullptr;
        if (manager->GetResource(key, kGZIID_cISCResExemplar, &raw, 0, 0)) {
            auto* exemplar = static_cast<cISCResExemplar*>(raw);
            defaultExemplar_ = exemplar;
            exemplar->Release();
        }
    }
    return !stream.GetError();
}

}  // namespace sc4
