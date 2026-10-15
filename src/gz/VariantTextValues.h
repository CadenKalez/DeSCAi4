// Text -> variant helpers of the property-text reader (0x1002695a6..0x10026b8d4): the variant type from its
// name, value lists separated by spaces converted into typed arrays, and the "unquoted" token reader.
#pragma once
#include <cstdint>

#include "gz/cIGZVariant.h"
#include "rz/cRZString.h"

namespace gz {

// getVariantType: trims, upper-cases and matches UINT8/UINT16/UINT32/SINT8/SINT16/SINT32/SINT64/STRING/FLOAT32/
// BOOL; 0 when unknown (type ids of cIGZVariant::Type; STRING is 0x800e).
std::uint32_t GetVariantTypeFromName(const cRZString& name);

// Space separated lists: LTrim + split at ' ' for the first count-1 items, the rest (trimmed) is the last item.
void ConvertStringToBoolValues(cRZString text, bool* out, std::uint32_t count);          // 'T...' or non-zero number
void ConvertStringToUnsignedValues8(cRZString text, std::uint8_t* out, std::uint32_t count);
void ConvertStringToUnsignedValues16(cRZString text, std::uint16_t* out, std::uint32_t count);
void ConvertStringToUnsignedValues32(cRZString text, std::uint32_t* out, std::uint32_t count);
void ConvertStringToSignedValues8(cRZString text, std::int8_t* out, std::uint32_t count);
void ConvertStringToSignedValues16(cRZString text, std::int16_t* out, std::uint32_t count);
void ConvertStringToSignedValues32(cRZString text, std::int32_t* out, std::uint32_t count);
void ConvertStringToSignedValues64(cRZString text, std::int64_t* out, std::uint32_t count);
void ConvertStringToFloatValues(cRZString text, float* out, std::uint32_t count);

// getDefaultValue: fills `variant` from `text` for the given type; count 1 stores a scalar, |count| > 1 an
// array. STRING stores the text verbatim. False for an empty text or an unsupported type.
bool GetDefaultVariantValue(cIGZVariant& variant, std::uint32_t type, std::int32_t count, const cRZString& text);

// getUnquotedString: the text between the first pair of double quotes, or the part before the first comma
// (the whole text when there is no comma).
cRZString GetUnquotedString(cRZString text);

}  // namespace gz
