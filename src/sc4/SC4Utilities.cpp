#include "sc4/SC4Utilities.h"

#include <string>
#include <vector>

#include "rz/CharLength.h"
#include "rz/PathUtils.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {

namespace {
ILanguageFormatter* g_formatter = nullptr;
}

ILanguageFormatter* GetLanguageFormatter() { return g_formatter; }
void SetLanguageFormatter(ILanguageFormatter* formatter) { g_formatter = formatter; }

std::uint32_t GetTripCapacity(cISCPropertyHolder* holder, std::uint32_t index) {
    cISCProperty* property = holder->GetProperty(kPropertyTripCapacity);
    if (!property) return 0;
    cIGZVariant* value = property->GetPropertyValue();
    if (value->GetType() == (cIGZVariant::kTypeUint16 | cIGZVariant::kTypeArrayFlag)) return value->RefUint16()[index];
    return value->RefUint32()[index];
}

// A missing property is created as twelve zero uint16 values; a value that does not fit a uint16 (>= 0xffff)
// widens the whole table to uint32 first.
void SetTripCapacity(cISCPropertyHolder* holder, std::uint32_t index, std::uint32_t value) {
    cISCProperty* property = holder->GetProperty(kPropertyTripCapacity);
    if (!property) {
        auto* created = new cSCProperty(kPropertyTripCapacity);
        created->AddRef();
        const std::uint16_t zeros[kTripCapacityCount] = {};
        created->GetPropertyValue()->RefUint16(zeros, kTripCapacityCount);
        holder->AddProperty(created, false);
        created->Release();
        property = holder->GetProperty(kPropertyTripCapacity);
        if (!property) return;
    }
    cIGZVariant* variant = property->GetPropertyValue();
    if (variant->GetType() == (cIGZVariant::kTypeUint16 | cIGZVariant::kTypeArrayFlag)) {
        std::uint16_t* narrow = const_cast<std::uint16_t*>(variant->RefUint16());
        if (value < 0xffff) {
            narrow[index] = static_cast<std::uint16_t>(value);
            return;
        }
        std::uint32_t widened[kTripCapacityCount];
        for (std::uint32_t i = 0; i < kTripCapacityCount; ++i) widened[i] = narrow[i];
        variant->RefUint32(widened, kTripCapacityCount);
    }
    const_cast<std::uint32_t*>(variant->RefUint32())[index] = value;
}

bool GetPropertyAsSint32(cISCPropertyHolder* holder, std::uint32_t id, std::int32_t& out) {
    cISCProperty* property = holder->GetProperty(id);
    if (!property) return false;
    cIGZVariant* value = property->GetPropertyValue();
    const std::uint16_t type = value->GetType();
    if ((type & cIGZVariant::kTypeArrayFlag) == 0) return value->GetValSint32(out);
    switch (type & 0x7fff) {
        case cIGZVariant::kTypeBool: out = *value->RefBool() ? 1 : 0; return true;
        case cIGZVariant::kTypeUint8: out = *value->RefUint8(); return true;
        case cIGZVariant::kTypeSint8: out = *value->RefSint8(); return true;
        case cIGZVariant::kTypeUint16: out = *value->RefUint16(); return true;
        case cIGZVariant::kTypeSint16: out = *value->RefSint16(); return true;
        case cIGZVariant::kTypeUint32: out = static_cast<std::int32_t>(*value->RefUint32()); return true;
        case cIGZVariant::kTypeSint32: out = *value->RefSint32(); return true;
        case cIGZVariant::kTypeSint64: out = static_cast<std::int32_t>(*value->RefSint64()); return true;
        case cIGZVariant::kTypeFloat32: out = static_cast<std::int32_t>(*value->RefFloat32()); return true;
        case cIGZVariant::kTypeFloat64: out = static_cast<std::int32_t>(*value->RefFloat64()); return true;
        case cIGZVariant::kTypeUintPtr: out = static_cast<std::int32_t>(*value->RefUintPtr()); return true;
        default: return false;                                          // (uint64 arrays included)
    }
}

bool GetFloatProperty(cISCPropertyHolder* holder, std::uint32_t id, float& out) {
    cISCProperty* property = holder->GetProperty(id);
    if (!property) return false;
    cIGZVariant* value = property->GetPropertyValue();
    const std::uint16_t type = value->GetType();
    const bool isArray = (type & cIGZVariant::kTypeArrayFlag) != 0;
    switch (type & 0x7fff) {
        case cIGZVariant::kTypeFloat64:
            if (!isArray) { out = static_cast<float>(value->GetValFloat64()); return true; }
            if (value->GetCount() == 0) return false;
            out = static_cast<float>(*value->RefFloat64());
            return true;
        case cIGZVariant::kTypeFloat32:
            if (!isArray) { out = value->GetValFloat32(); return true; }
            if (value->GetCount() == 0) return false;
            out = *value->RefFloat32();
            return true;
        default: return false;
    }
}

bool IsStaticBuilding(cISCPropertyHolder* holder, bool isBuildingOccupant) {
    if (!holder || holder->HasProperty(0x029244db)) return false;
    static const std::uint32_t kPurposeProperties[] = {0x27812821, 0x27812820, 0x27812822, 0x27812823, 0x27812824};
    bool purposeFound = false;
    for (std::uint32_t id : kPurposeProperties)
        if (holder->HasProperty(id)) { purposeFound = true; break; }
    if (!purposeFound) return false;
    if (isBuildingOccupant) return true;
    bool flag = false;
    const bool found = cSCPropertyHelp::GetPropertyValue(holder, 0x8a5e5db8, flag);
    return found && flag;
}

bool IsStraightConfiguration(std::int32_t c) {
    return c == 0x301 || c == 0x20002 || c == 0x30100 || c == 0x1000003 || c == 0x2000200 || c == 0x3010000;
}

std::int32_t SC4ZoomAndRotationToIndex(std::int32_t zoom, std::int32_t rotation) { return rotation + zoom * 4; }

void SC4LimitStringLength(const cIGZString& in, cIGZString& out, std::uint32_t maxLength) {
    const char* data = in.ToChar();
    const std::uint32_t length = out.Strlen();
    std::uint32_t limit = length < maxLength ? length : maxLength;
    std::uint32_t kept = 0;
    if (limit != 0) {
        do {
            const std::uint32_t next = kept + GetCharLength(data + kept, length - kept, -2);
            if (next <= limit) kept = next;
            else break;
        } while (kept < limit);
    }
    if (&in != &out) {
        out.AssignFromChars(data, kept);
    } else if (kept < length) {
        out.Resize(kept);
    }
}

void SC4SanitizeFileName(const cRZString& in, cRZString& out) {
    cRZString valid;
    MakeFileNameValid(in, valid, nullptr);
    std::string s = valid.ToChar();
    for (std::size_t pos = s.find("  "); pos != std::string::npos; pos = s.find("  ")) s.erase(pos, 1);
    cRZString result(s.c_str());
    result.Trim();
    out = result;
}

void SC4MakeMoneyString(std::int64_t value, cIGZString& out) {
    cRZString symbol("\xc2\xa7");
    if (g_formatter) g_formatter->FormatMoney(value, out, symbol);
}

void SC4MakeFormattedMoneyString(double value, cIGZString& out, const cIGZString& format) {
    cRZString symbol("\xc2\xa7");
    if (g_formatter) g_formatter->FormatMoney(value, out, format, symbol);
}

void SC4InsertNumberIntoString(cRZString& text, std::int64_t value) {
    std::string s = text.ToChar();
    const std::size_t pos = s.find('#');
    if (pos == std::string::npos) return;
    cRZString number;
    if (g_formatter) g_formatter->FormatNumber(value, number);
    s.replace(pos, 1, number.ToChar());
    text = cRZString(s.c_str());
}

}  // namespace sc4
