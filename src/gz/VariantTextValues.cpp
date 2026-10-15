#include "gz/VariantTextValues.h"

#include <cstdlib>
#include <memory>

#include "rz/RZConvert.h"
#include "rz/RZStringUtils.h"

namespace gz {

std::uint32_t GetVariantTypeFromName(const cRZString& name) {
    cRZString s = name;
    s.LTrim();
    s.RTrim();
    s.MakeUpper();
    if (s.Strlen() == 0) return 0;
    const std::string v = s.ToChar();
    struct Entry { const char* name; std::uint32_t type; };
    static const Entry kTypes[] = {{"UINT8", 2}, {"UINT16", 4}, {"UINT32", 6}, {"SINT8", 3}, {"SINT16", 5},
                                   {"SINT32", 7}, {"SINT64", 9}, {"STRING", 0x800e}, {"FLOAT32", 10}, {"BOOL", 1}};
    for (const Entry& e : kTypes)
        if (v == e.name) return e.type;
    return 0;
}

namespace {
// Shared splitting loop: element i comes from the front of the list, the last from what is left.
template <class Convert>
void ForEachListItem(cRZString& text, std::uint32_t count, Convert&& convert) {
    std::uint32_t i = 0;
    if (count - 1u != 0) {
        for (; i < count - 1u; ++i) {
            text.LTrim();
            cRZString token = text.SplitToken(" ");
            convert(i, token.ToChar());
        }
    }
    text.LTrim();
    text.RTrim();
    convert(i, text.ToChar());
}
bool BoolFromText(const char* s) { return *s == 'T' || RZ::ConvertToUint(s) != 0; }
}  // namespace

void ConvertStringToBoolValues(cRZString text, bool* out, std::uint32_t count) {
    ForEachListItem(text, count, [&](std::uint32_t i, const char* s) { out[i] = BoolFromText(s); });
}
void ConvertStringToUnsignedValues8(cRZString text, std::uint8_t* out, std::uint32_t count) {
    ForEachListItem(text, count, [&](std::uint32_t i, const char* s) { out[i] = static_cast<std::uint8_t>(RZ::ConvertToUint(s)); });
}
void ConvertStringToUnsignedValues16(cRZString text, std::uint16_t* out, std::uint32_t count) {
    ForEachListItem(text, count, [&](std::uint32_t i, const char* s) { out[i] = static_cast<std::uint16_t>(RZ::ConvertToUint(s)); });
}
void ConvertStringToUnsignedValues32(cRZString text, std::uint32_t* out, std::uint32_t count) {
    ForEachListItem(text, count, [&](std::uint32_t i, const char* s) { out[i] = RZ::ConvertToUint(s); });
}
void ConvertStringToSignedValues8(cRZString text, std::int8_t* out, std::uint32_t count) {
    ForEachListItem(text, count, [&](std::uint32_t i, const char* s) { out[i] = static_cast<std::int8_t>(std::atoi(s)); });
}
void ConvertStringToSignedValues16(cRZString text, std::int16_t* out, std::uint32_t count) {
    ForEachListItem(text, count, [&](std::uint32_t i, const char* s) { out[i] = static_cast<std::int16_t>(std::atoi(s)); });
}
void ConvertStringToSignedValues32(cRZString text, std::int32_t* out, std::uint32_t count) {
    ForEachListItem(text, count, [&](std::uint32_t i, const char* s) { out[i] = std::atoi(s); });
}
void ConvertStringToSignedValues64(cRZString text, std::int64_t* out, std::uint32_t count) {
    // the original goes through atoi, so values beyond 32 bits are not representable
    ForEachListItem(text, count, [&](std::uint32_t i, const char* s) { out[i] = static_cast<std::int64_t>(std::atoi(s)); });
}
void ConvertStringToFloatValues(cRZString text, float* out, std::uint32_t count) {
    ForEachListItem(text, count, [&](std::uint32_t i, const char* s) { out[i] = static_cast<float>(RZAtofEnglishA(s)); });
}

bool GetDefaultVariantValue(cIGZVariant& variant, std::uint32_t type, std::int32_t count, const cRZString& text) {
    const std::uint32_t n = static_cast<std::uint32_t>(count < 0 ? -count : count);
    cRZString value = text;
    if (type == 0x800e) {
        variant.SetValString(value);
        return true;
    }
    value.LTrim();
    if (value.Strlen() == 0) return false;
    switch (type) {
        case 1: {
            value.MakeUpper();
            if (count == 1) {
                variant.SetValBool(BoolFromText(value.ToChar()));
                return true;
            }
            auto buffer = std::make_unique<bool[]>(n);
            ConvertStringToBoolValues(value, buffer.get(), n);
            variant.RefBool(buffer.get(), n);
            return true;
        }
        case 2: {
            if (count == 1) { variant.SetValUint8(static_cast<std::uint8_t>(RZ::ConvertToUint(value.ToChar()))); return true; }
            auto buffer = std::make_unique<std::uint8_t[]>(n);
            ConvertStringToUnsignedValues8(value, buffer.get(), n);
            variant.RefUint8(buffer.get(), n);
            return true;
        }
        case 3: {
            if (count == 1) { variant.SetValSint8(static_cast<std::int8_t>(std::atoi(value.ToChar()))); return true; }
            auto buffer = std::make_unique<std::int8_t[]>(n);
            ConvertStringToSignedValues8(value, buffer.get(), n);
            variant.RefSint8(buffer.get(), n);
            return true;
        }
        case 4: {
            if (count == 1) { variant.SetValUint16(static_cast<std::uint16_t>(RZ::ConvertToUint(value.ToChar()))); return true; }
            auto buffer = std::make_unique<std::uint16_t[]>(n);
            ConvertStringToUnsignedValues16(value, buffer.get(), n);
            variant.RefUint16(buffer.get(), n);
            return true;
        }
        case 5: {
            if (count == 1) { variant.SetValSint16(static_cast<std::int16_t>(std::atoi(value.ToChar()))); return true; }
            auto buffer = std::make_unique<std::int16_t[]>(n);
            ConvertStringToSignedValues16(value, buffer.get(), n);
            variant.RefSint16(buffer.get(), n);
            return true;
        }
        case 6: {
            if (count == 1) { variant.SetValUint32(RZ::ConvertToUint(value.ToChar())); return true; }
            auto buffer = std::make_unique<std::uint32_t[]>(n);
            ConvertStringToUnsignedValues32(value, buffer.get(), n);
            variant.RefUint32(buffer.get(), n);
            return true;
        }
        case 7: {
            if (count == 1) { variant.SetValSint32(std::atoi(value.ToChar())); return true; }
            auto buffer = std::make_unique<std::int32_t[]>(n);
            ConvertStringToSignedValues32(value, buffer.get(), n);
            variant.RefSint32(buffer.get(), n);
            return true;
        }
        case 9: {
            if (count == 1) { variant.SetValSint64(static_cast<std::int64_t>(std::atoi(value.ToChar()))); return true; }
            auto buffer = std::make_unique<std::int64_t[]>(n);
            ConvertStringToSignedValues64(value, buffer.get(), n);
            variant.RefSint64(buffer.get(), n);
            return true;
        }
        case 10: {
            if (count == 1) { variant.SetValFloat32(static_cast<float>(RZAtofEnglishA(value.ToChar()))); return true; }
            auto buffer = std::make_unique<float[]>(n);
            ConvertStringToFloatValues(value, buffer.get(), n);
            variant.RefFloat32(buffer.get(), n);
            return true;
        }
        default: return false;
    }
}

cRZString GetUnquotedString(cRZString text) {
    cRZString result;
    text.LTrim();
    if (text.Strlen() == 0) return result;
    if (text.ToChar()[0] == '"') {
        result = text.SplitToken("\"");          // text before the opening quote (empty)
        result = text.SplitToken("\"");          // text up to the closing quote
    } else {
        result = text.SplitToken(",");
        if (result.Strlen() == 0) result = text;
    }
    return result;
}

}  // namespace gz
