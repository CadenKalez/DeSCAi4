#include "sc4/SCPropertyHelp.h"

#include <algorithm>

namespace sc4 {
namespace {

// The variant of the holder's property `id`, or null.
cIGZVariant* ValueOf(const cISCPropertyHolder* holder, std::uint32_t id) {
    cISCProperty* p = holder->GetProperty(id);
    return p ? p->GetPropertyValue() : nullptr;
}

// Property `id`'s variant, creating the property (empty value) when missing.
cIGZVariant* ValueOfOrCreate(cISCPropertyHolder* holder, std::uint32_t id) {
    cISCProperty* p = holder->GetProperty(id);
    if (!p) {
        auto* created = new cSCProperty(id);
        holder->AddProperty(created, false);   // the holder takes the only reference
        p = created;
    }
    return p->GetPropertyValue();
}

template <class T, class Ref>
bool GetArray(const cISCPropertyHolder* holder, std::uint32_t id, std::vector<T>& out, Ref ref) {
    cIGZVariant* v = ValueOf(holder, id);
    if (!v) return false;
    const T* data = ref(v);
    const std::uint32_t n = v->GetCount();
    out.resize(n);
    if (n) std::copy(data, data + n, out.begin());
    out.shrink_to_fit();
    return true;
}

}  // namespace

#define SC4_SCALAR_GETTER(T, Call)                                                                     \
    bool cSCPropertyHelp::GetPropertyValue(const cISCPropertyHolder* h, std::uint32_t id, T& out) {   \
        cIGZVariant* v = ValueOf(h, id);                                                              \
        return v && v->Call(out);                                                                     \
    }
SC4_SCALAR_GETTER(bool, GetValBool)
SC4_SCALAR_GETTER(std::uint8_t, GetValUint8)
SC4_SCALAR_GETTER(std::int16_t, GetValSint16)
SC4_SCALAR_GETTER(std::uint16_t, GetValUint16)
SC4_SCALAR_GETTER(std::int32_t, GetValSint32)
SC4_SCALAR_GETTER(std::uint32_t, GetValUint32)
SC4_SCALAR_GETTER(std::int64_t, GetValSint64)
SC4_SCALAR_GETTER(float, GetValFloat32)
SC4_SCALAR_GETTER(double, GetValFloat64)
SC4_SCALAR_GETTER(cIGZString, GetValString)
#undef SC4_SCALAR_GETTER

bool cSCPropertyHelp::GetPropertyValue(const cISCPropertyHolder* h, std::uint32_t id, cGZPersistResourceKey& out) {
    cIGZVariant* v = ValueOf(h, id);
    if (!v) return false;
    const std::uint32_t* words = v->RefUint32();   // three dwords, unchecked
    out.type = words[0];
    out.group = words[1];
    out.instance = words[2];
    return true;
}

bool cSCPropertyHelp::GetPropertyValue(const cISCPropertyHolder* h, std::uint32_t id, SC4Percentage& out) {
    cIGZVariant* v = ValueOf(h, id);
    if (!v) return false;
    float f;
    switch (v->GetType()) {
        case cIGZVariant::kTypeUint8:   { std::uint8_t x;  if (!v->GetValUint8(x))   return false; f = float(int(x)); break; }
        case cIGZVariant::kTypeSint8:   { std::int8_t x;   if (!v->GetValSint8(x))   return false; f = float(int(x)); break; }
        case cIGZVariant::kTypeUint16:  { std::uint16_t x; if (!v->GetValUint16(x))  return false; f = float(int(x)); break; }
        case cIGZVariant::kTypeSint16:  { std::int16_t x;  if (!v->GetValSint16(x))  return false; f = float(int(x)); break; }
        case cIGZVariant::kTypeUint32:  { std::uint32_t x; if (!v->GetValUint32(x))  return false; f = float(x); break; }
        case cIGZVariant::kTypeSint32:  { std::int32_t x;  if (!v->GetValSint32(x))  return false; f = float(x); break; }
        case cIGZVariant::kTypeSint64:  { std::int64_t x;  if (!v->GetValSint64(x))  return false; f = float(x); break; }
        case cIGZVariant::kTypeFloat32: { float x;         if (!v->GetValFloat32(x)) return false; f = x; break; }
        case cIGZVariant::kTypeFloat64: { double x;        if (!v->GetValFloat64(x)) return false; f = float(x); break; }
        default: return false;
    }
    out.value = f * 0.01f;
    return true;
}

bool cSCPropertyHelp::GetPropertyValue(const cISCPropertyHolder* h, std::uint32_t id, std::vector<std::uint8_t>& out) {
    return GetArray(h, id, out, [](cIGZVariant* v) { return v->RefUint8(); });
}
bool cSCPropertyHelp::GetPropertyValue(const cISCPropertyHolder* h, std::uint32_t id, std::vector<std::uint32_t>& out) {
    return GetArray(h, id, out, [](cIGZVariant* v) { return v->RefUint32(); });
}
bool cSCPropertyHelp::GetPropertyValue(const cISCPropertyHolder* h, std::uint32_t id, std::vector<std::int32_t>& out) {
    return GetArray(h, id, out, [](cIGZVariant* v) { return v->RefSint32(); });
}
bool cSCPropertyHelp::GetPropertyValue(const cISCPropertyHolder* h, std::uint32_t id, std::vector<float>& out) {
    return GetArray(h, id, out, [](cIGZVariant* v) { return v->RefFloat32(); });
}
bool cSCPropertyHelp::GetPropertyValue(const cISCPropertyHolder* h, std::uint32_t id, std::vector<std::int64_t>& out) {
    out.clear();   // the 64-bit variant empties the destination first
    return GetArray(h, id, out, [](cIGZVariant* v) { return v->RefSint64(); });
}

bool cSCPropertyHelp::GetPropertyValue(const cISCPropertyHolder* h, std::uint32_t id, std::vector<bool>& out) {
    cIGZVariant* v = ValueOf(h, id);
    if (!v) return false;
    const bool* data = v->RefBool();
    const std::uint32_t n = v->GetCount();
    out.assign(n, false);
    for (std::uint32_t i = 0; i < n; ++i) out[i] = data[i] != 0;
    out.shrink_to_fit();
    return true;
}

void cSCPropertyHelp::SetPropertyValue(cISCPropertyHolder* h, std::uint32_t id, bool value) {
    ValueOfOrCreate(h, id)->SetValBool(value);
}
void cSCPropertyHelp::SetPropertyValue(cISCPropertyHolder* h, std::uint32_t id, std::uint8_t value) {
    ValueOfOrCreate(h, id)->SetValUint8(value);
}
void cSCPropertyHelp::SetPropertyValue(cISCPropertyHolder* h, std::uint32_t id, std::int32_t value) {
    ValueOfOrCreate(h, id)->SetValSint32(value);
}
void cSCPropertyHelp::SetPropertyValue(cISCPropertyHolder* h, std::uint32_t id, std::uint32_t value) {
    ValueOfOrCreate(h, id)->SetValUint32(value);
}
void cSCPropertyHelp::SetPropertyValue(cISCPropertyHolder* h, std::uint32_t id, std::int64_t value) {
    ValueOfOrCreate(h, id)->SetValSint64(value);
}
void cSCPropertyHelp::SetPropertyValue(cISCPropertyHolder* h, std::uint32_t id, float value) {
    ValueOfOrCreate(h, id)->SetValFloat32(value);
}
void cSCPropertyHelp::SetPropertyValue(cISCPropertyHolder* h, std::uint32_t id, const cIGZString& value) {
    ValueOfOrCreate(h, id)->SetValString(value);
}
void cSCPropertyHelp::SetPropertyValue(cISCPropertyHolder* h, std::uint32_t id, const std::vector<std::uint8_t>& v) {
    ValueOfOrCreate(h, id)->RefUint8(v.data(), static_cast<std::uint32_t>(v.size()));
}
void cSCPropertyHelp::SetPropertyValue(cISCPropertyHolder* h, std::uint32_t id, const std::vector<std::uint32_t>& v) {
    ValueOfOrCreate(h, id)->RefUint32(v.data(), static_cast<std::uint32_t>(v.size()));
}
void cSCPropertyHelp::SetPropertyValue(cISCPropertyHolder* h, std::uint32_t id, const std::vector<float>& v) {
    ValueOfOrCreate(h, id)->RefFloat32(v.data(), static_cast<std::uint32_t>(v.size()));
}
void cSCPropertyHelp::SetPropertyValue(cISCPropertyHolder* h, std::uint32_t id, const std::vector<std::int64_t>& v) {
    ValueOfOrCreate(h, id)->RefSint64(v.data(), static_cast<std::uint32_t>(v.size()));
}

}  // namespace sc4
