#include "rz/cRZVariant.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "rz/cRZString.h"

namespace {

bool IsNumeric(std::uint16_t t) { return t >= 1 && t <= 14; }

// Numeric view of a scalar for ConvertToFloat64 / ConvertToXInt64.
struct Number {
    bool isFloat = false;
    double d = 0;
    std::int64_t i = 0;
    bool isUnsigned64 = false;
};

Number ReadNumber(std::uint16_t type, const std::uint8_t* b) {
    Number n;
    auto s8 = [&] { return static_cast<std::int64_t>(static_cast<std::int8_t>(b[0])); };
    switch (type) {
        case cIGZVariant::kTypeBool:
        case cIGZVariant::kTypeUint8: n.i = b[0]; break;
        case cIGZVariant::kTypeSint8:
        case cIGZVariant::kTypeChar:
        case cIGZVariant::kTypeRZChar: n.i = s8(); break;
        case cIGZVariant::kTypeUint16:
        case cIGZVariant::kTypeRZUnicodeChar: { std::uint16_t v; std::memcpy(&v, b, 2); n.i = v; break; }
        case cIGZVariant::kTypeSint16: { std::int16_t v; std::memcpy(&v, b, 2); n.i = v; break; }
        case cIGZVariant::kTypeUint32: { std::uint32_t v; std::memcpy(&v, b, 4); n.i = v; break; }
        case cIGZVariant::kTypeSint32: { std::int32_t v; std::memcpy(&v, b, 4); n.i = v; break; }
        case cIGZVariant::kTypeUint64: { std::uint64_t v; std::memcpy(&v, b, 8); n.i = static_cast<std::int64_t>(v); n.isUnsigned64 = true; break; }
        case cIGZVariant::kTypeSint64: std::memcpy(&n.i, b, 8); break;
        case cIGZVariant::kTypeFloat32: { float v; std::memcpy(&v, b, 4); n.isFloat = true; n.d = v; break; }
        case cIGZVariant::kTypeFloat64: std::memcpy(&n.d, b, 8); n.isFloat = true; break;
        default: break;
    }
    return n;
}

}  // namespace

cRZVariant::cRZVariant(const void* data, std::uint32_t size, std::uint32_t extraFlags) {
    RefVoid(data, size);
    type_ = static_cast<std::uint16_t>(extraFlags | 0x8000);
}

bool cRZVariant::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZVariant) {
        *out = static_cast<cIGZVariant*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cRZVariant::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cRZVariant::CreateCopy(cIGZVariant** out) const {
    auto* copy = new cRZVariant();
    *copy = *this;
    copy->AddRef();
    *out = copy;
    return true;
}

void cRZVariant::Destroy() {
    if (type_ & kTypeArrayFlag) {
        delete[] static_cast<std::uint8_t*>(value_.pointer);
    } else if (type_ == kTypeUnknown) {
        if (object_) object_->Release();
    }
    std::memset(value_.bytes, 0, sizeof value_.bytes);
    object_ = nullptr;
    type_ = kTypeNone;
    count_ = 0;
}

cRZVariant& cRZVariant::operator=(const cRZVariant& other) {
    if (this == &other) return *this;
    const std::uint16_t t = other.type_;
    if (t == kTypeNone) { Destroy(); return *this; }
    if (t & kTypeArrayFlag) {
        std::size_t element = 1;
        switch (t & 0x7fff) {
            case kTypeUint16: case kTypeSint16: case kTypeRZUnicodeChar: element = 2; break;
            case kTypeUint32: case kTypeSint32: case kTypeFloat32: element = 4; break;
            case kTypeUint64: case kTypeSint64: case kTypeFloat64: case kTypeUintPtr: case kTypeVoidPtr: element = 8; break;
            default: break;
        }
        SetArray(t, other.value_.pointer, other.count_, element);
        return *this;
    }
    if (t == kTypeUnknown) { RefIGZUnknown(other.object_); return *this; }
    if (type_ != kTypeNone && type_ != t) Destroy();
    type_ = t;
    std::memcpy(value_.bytes, other.value_.bytes, sizeof value_.bytes);
    return *this;
}

cRZVariant& cRZVariant::operator=(const cIGZVariant& other) {
    if (auto* same = dynamic_cast<const cRZVariant*>(&other)) return *this = *same;
    const std::uint16_t t = other.GetType();
#define COPY(Kind, Name) case Kind: SetVal##Name(other.GetVal##Name()); break;
    switch (t) {
        case kTypeNone: Destroy(); break;
        COPY(kTypeBool, Bool) COPY(kTypeUint8, Uint8) COPY(kTypeSint8, Sint8) COPY(kTypeUint16, Uint16)
        COPY(kTypeSint16, Sint16) COPY(kTypeUint32, Uint32) COPY(kTypeSint32, Sint32) COPY(kTypeUint64, Uint64)
        COPY(kTypeSint64, Sint64) COPY(kTypeFloat32, Float32) COPY(kTypeFloat64, Float64) COPY(kTypeChar, Char)
        COPY(kTypeRZUnicodeChar, RZUnicodeChar) COPY(kTypeRZChar, RZChar) COPY(kTypeVoidPtr, VoidPtr)
        COPY(kTypeUintPtr, UintPtr)
        case kTypeUnknown: RefIGZUnknown(const_cast<cIGZVariant&>(other).GetValUnknown()); break;
        default:
            if (t & kTypeArrayFlag) {
                // arrays of any element type: copy the raw bytes the other side exposes
                std::uint32_t n = other.GetCount();
                const void* data = other.RefVoid();
                std::size_t element = 1;
                switch (t & 0x7fff) {
                    case kTypeUint16: case kTypeSint16: case kTypeRZUnicodeChar: element = 2; break;
                    case kTypeUint32: case kTypeSint32: case kTypeFloat32: element = 4; break;
                    case kTypeUint64: case kTypeSint64: case kTypeFloat64: case kTypeUintPtr: case kTypeVoidPtr: element = 8; break;
                    default: break;
                }
                SetArray(t, data, n, element);
            }
            break;
    }
#undef COPY
    return *this;
}

void cRZVariant::SetArray(std::uint16_t type, const void* data, std::uint32_t count, std::size_t elementSize) {
    const std::size_t bytes = static_cast<std::size_t>(count) * elementSize;
    if (type_ == type && count_ == count && data && value_.pointer) {   // same shape: overwrite in place
        std::memcpy(value_.pointer, data, bytes);
        return;
    }
    Destroy();
    auto* storage = new std::uint8_t[bytes ? bytes : 1];
    if (data) std::memcpy(storage, data, bytes);
    value_.pointer = storage;
    count_ = count;
    type_ = type;
}

bool cRZVariant::GetValVoidPtr(void*& value) const {
    if (type_ == kTypeVoidPtr) { value = value_.pointer; return true; }
    return false;   // pointers are never converted from other types
}

void cRZVariant::SetValVoidPtr(void* value) {
    if (type_ != kTypeNone && type_ != kTypeVoidPtr) Destroy();
    type_ = kTypeVoidPtr;
    value_.pointer = value;
}

void cRZVariant::RefIGZUnknown(cIGZUnknown* value) {
    if (type_ != kTypeNone && type_ != kTypeUnknown) Destroy();
    type_ = kTypeUnknown;
    if (object_ != value) {
        if (value) value->AddRef();
        if (object_) object_->Release();
        object_ = value;
    }
}

bool cRZVariant::RefIGZUnknown(std::uint32_t iid, void** out) {
    return type_ == kTypeUnknown && object_ && object_->QueryInterface(iid, out);
}

bool cRZVariant::GetValVoid(void* destination, std::uint32_t& size) const {
    if (type_ != kTypeArrayFlag) return false;
    if (count_ < size) size = count_;
    std::memcpy(destination, value_.pointer, size);
    return true;
}

// ---- conversions ---------------------------------------------------------------------------

bool cRZVariant::ConvertTypes(const cRZVariant& from, cRZVariant& to) {
    if (!IsNumeric(from.type_) || !IsNumeric(to.type_)) {
        return from.FromString(from.ToString(), to);
    }
    Number n = ReadNumber(from.type_, from.value_.bytes);
    to.count_ = 0;
    std::memset(to.value_.bytes, 0, sizeof to.value_.bytes);
    if (to.type_ == kTypeFloat32 || to.type_ == kTypeFloat64) {
        double d = n.isFloat ? n.d
                   : n.isUnsigned64 ? static_cast<double>(static_cast<std::uint64_t>(n.i))
                   : static_cast<double>(n.i);
        if (from.type_ == kTypeBool) d = from.value_.bytes[0] ? 1.0 : 0.0;
        if (to.type_ == kTypeFloat32) { float f = static_cast<float>(d); std::memcpy(to.value_.bytes, &f, 4); }
        else std::memcpy(to.value_.bytes, &d, 8);
        return true;
    }
    std::int64_t i = n.isFloat ? static_cast<std::int64_t>(static_cast<std::uint64_t>(n.d)) : n.i;
    switch (to.type_) {
        case kTypeBool: to.value_.bytes[0] = i != 0; break;
        case kTypeUint8: case kTypeSint8: case kTypeChar: case kTypeRZChar:
            to.value_.bytes[0] = static_cast<std::uint8_t>(i); break;
        case kTypeUint16: case kTypeSint16: case kTypeRZUnicodeChar: {
            auto v = static_cast<std::uint16_t>(i);
            std::memcpy(to.value_.bytes, &v, 2);
            break;
        }
        case kTypeUint32: case kTypeSint32: {
            auto v = static_cast<std::uint32_t>(i);
            std::memcpy(to.value_.bytes, &v, 4);
            break;
        }
        default: std::memcpy(to.value_.bytes, &i, 8); break;   // 64-bit and pointer-sized
    }
    return true;
}

std::string cRZVariant::ToString() const {
    char buf[64];
    auto sc = [&](const char* fmt, auto v) { std::snprintf(buf, sizeof buf, fmt, v); return std::string(buf); };
    switch (type_) {
        case kTypeBool: return value_.bytes[0] ? "1" : "0";
        case kTypeUint8: return sc("%u", static_cast<unsigned>(value_.bytes[0]));
        case kTypeSint8: return sc("%d", static_cast<int>(static_cast<std::int8_t>(value_.bytes[0])));
        case kTypeUint16: { std::uint16_t v; std::memcpy(&v, value_.bytes, 2); return sc("%u", static_cast<unsigned>(v)); }
        case kTypeSint16: { std::int16_t v; std::memcpy(&v, value_.bytes, 2); return sc("%d", static_cast<int>(v)); }
        case kTypeUint32: { std::uint32_t v; std::memcpy(&v, value_.bytes, 4); return sc("%u", v); }
        case kTypeSint32: { std::int32_t v; std::memcpy(&v, value_.bytes, 4); return sc("%d", v); }
        case kTypeUint64: { std::uint64_t v; std::memcpy(&v, value_.bytes, 8); return sc("%llu", static_cast<unsigned long long>(v)); }
        case kTypeSint64: { std::int64_t v; std::memcpy(&v, value_.bytes, 8); return sc("%lld", static_cast<long long>(v)); }
        case kTypeFloat32: { float v; std::memcpy(&v, value_.bytes, 4); return sc("%.9g", static_cast<double>(v)); }
        case kTypeFloat64: { double v; std::memcpy(&v, value_.bytes, 8); return sc("%.17g", v); }
        case kTypeChar: case kTypeRZChar: return std::string(1, static_cast<char>(value_.bytes[0]));
        case kTypeRZUnicodeChar: {
            std::uint16_t v; std::memcpy(&v, value_.bytes, 2);
            return v < 0x100 ? std::string(1, static_cast<char>(v)) : std::string("?");
        }
        case kTypeVoidPtr: return sc("0x%016x", static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(value_.pointer)));
        case kTypeUnknown: {
            if (object_) {
                void* raw = nullptr;
                if (object_->QueryInterface(kGZIID_cIGZString, &raw)) {
                    auto* s = static_cast<cIGZString*>(raw);
                    std::string text(s->Data(), s->Strlen());
                    s->Release();
                    return text;
                }
            }
            return sc("0x%016x", static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(object_)));
        }
        default: break;
    }
    if (type_ & kTypeArrayFlag) {
        const auto* p = static_cast<const std::uint8_t*>(value_.pointer);
        switch (type_ & 0x7fff) {
            case kTypeRZChar: return std::string(reinterpret_cast<const char*>(p), count_);
            case kTypeBool: return (count_ == 0 || p[0] == 0) ? "0" : "1";
            case kTypeUint8: return count_ ? sc("%u", static_cast<unsigned>(p[0])) : "0";
            case kTypeSint8: return count_ ? sc("%d", static_cast<int>(static_cast<std::int8_t>(p[0]))) : "0";
            case kTypeUint16: { if (!count_) return "0"; std::uint16_t v; std::memcpy(&v, p, 2); return sc("%u", static_cast<unsigned>(v)); }
            case kTypeSint16: { if (!count_) return "0"; std::int16_t v; std::memcpy(&v, p, 2); return sc("%d", static_cast<int>(v)); }
            case kTypeUint32: { if (!count_) return "0"; std::uint32_t v; std::memcpy(&v, p, 4); return sc("%u", v); }
            case kTypeSint32: { if (!count_) return "0"; std::int32_t v; std::memcpy(&v, p, 4); return sc("%d", v); }
            case kTypeFloat32: { if (!count_) return "0"; float v; std::memcpy(&v, p, 4); return sc("%.9g", static_cast<double>(v)); }
            default: break;
        }
    }
    return std::string();
}

// Text -> value: "t"/"true"/"f"/"false", 0x-prefixed hex, a number with '.' or an exponent (double)
// or an integer (negative values become signed 64-bit).
bool cRZVariant::FromString(const std::string& text, cRZVariant& to) const {
    std::int64_t i = 0;
    double d = 0;
    bool isDouble = false;
    if (text == "t" || text == "true") i = 1;
    else if (text == "f" || text == "false") i = 0;
    else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        i = static_cast<std::int64_t>(std::strtoull(text.c_str(), nullptr, 16));
    else if (text.find_first_of(".eE") != std::string::npos) { d = std::strtod(text.c_str(), nullptr); isDouble = true; }
    else i = std::strtoll(text.c_str(), nullptr, 10);

    to.count_ = 0;
    std::memset(to.value_.bytes, 0, sizeof to.value_.bytes);
    switch (to.type_) {
        case kTypeBool: to.value_.bytes[0] = isDouble ? d != 0.0 : i != 0; break;
        case kTypeFloat32: { float f = isDouble ? static_cast<float>(d) : static_cast<float>(i); std::memcpy(to.value_.bytes, &f, 4); break; }
        case kTypeFloat64: { double v = isDouble ? d : static_cast<double>(i); std::memcpy(to.value_.bytes, &v, 8); break; }
        default: {
            std::int64_t v = isDouble ? static_cast<std::int64_t>(d) : i;
            switch (to.type_) {
                case kTypeUint8: case kTypeSint8: case kTypeChar: case kTypeRZChar: to.value_.bytes[0] = static_cast<std::uint8_t>(v); break;
                case kTypeUint16: case kTypeSint16: case kTypeRZUnicodeChar: { auto x = static_cast<std::uint16_t>(v); std::memcpy(to.value_.bytes, &x, 2); break; }
                case kTypeUint32: case kTypeSint32: { auto x = static_cast<std::uint32_t>(v); std::memcpy(to.value_.bytes, &x, 4); break; }
                default: std::memcpy(to.value_.bytes, &v, 8); break;
            }
        }
    }
    return true;
}

bool cRZVariant::GetValString(cIGZString& out) const {
    if (type_ == kTypeUnknown && object_) {
        void* raw = nullptr;
        if (object_->QueryInterface(kGZIID_cIGZString, &raw)) {
            auto* s = static_cast<cIGZString*>(raw);
            out.AssignFromChars(s->Data(), s->Strlen());
            s->Release();
            return true;
        }
    } else if (type_ == (kTypeRZChar | kTypeArrayFlag)) {
        out.AssignFromChars(static_cast<const char*>(value_.pointer), count_);
        return true;
    }
    std::string text = ToString();
    out.AssignFromChars(text.data(), static_cast<std::uint32_t>(text.size()));
    return false;   // (sic) converted text is delivered but the call reports failure
}

bool cRZVariant::CreateValString(cIGZString** out) const {
    auto* s = new cRZString();
    s->AddRef();
    bool ok = GetValString(*s);
    if (!ok) {
        s->Release();
        s = nullptr;
    }
    *out = s;
    return ok;
}
