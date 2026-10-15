// Free helpers of the SC4 application layer: exemplar property accessors with type conversion, the trip
// capacity table, string helpers over the language manager and a few occupant / network predicates.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "rz/cRZString.h"
#include "sc4/SCProperty.h"

namespace sc4 {

constexpr std::uint32_t kPropertyTripCapacity = 0x8a0b4100;     // 12 trip capacities (uint16, widened to uint32 when needed)
constexpr std::uint32_t kTripCapacityCount = 12;
constexpr std::uint32_t kCurrencySymbolSection = 0xa7c2;         // "§" in UTF-8, the Simoleon sign

// GetTripCapacity(0x10008c46b) / SetTripCapacity(0x10008c2fd)
std::uint32_t GetTripCapacity(cISCPropertyHolder* holder, std::uint32_t index);
void SetTripCapacity(cISCPropertyHolder* holder, std::uint32_t index, std::uint32_t value);

// Reads any numeric property as an int (first element of an array); false for missing / non-numeric values.
bool GetPropertyAsSint32(cISCPropertyHolder* holder, std::uint32_t id, std::int32_t& out);
// float32 / float64 properties (first element of an array); false for any other type.
bool GetFloatProperty(cISCPropertyHolder* holder, std::uint32_t id, float& out);

// IsStaticBuilding(0x1005e3337): the exemplar carries one of the building purpose properties, has no "ignore"
// property and is either a real building occupant or flagged static (0x8a5e5db8).
bool IsStaticBuilding(cISCPropertyHolder* holder, bool isBuildingOccupant);

// IsConnectable(0x100130758): network occupant connection test over its connection-type predicate.
template <class HasConnection>
bool IsConnectable(HasConnection&& has) {
    if ((has(3) || has(0xb)) && !has(10)) return !has(0);
    return false;
}
// IsStraight(0x1001a92df): a cell configuration that runs straight through (4 orientations of two arms).
bool IsStraightConfiguration(std::int32_t configuration);

std::int32_t SC4ZoomAndRotationToIndex(std::int32_t zoom, std::int32_t rotation);

// SC4LimitStringLength(0x100039659): truncate to at most maxLength bytes without splitting a multi-byte char.
void SC4LimitStringLength(const cIGZString& in, cIGZString& out, std::uint32_t maxLength);
// SC4SanitizeFileName(0x1000394a1): MakeFileNameValid, collapse double spaces, trim.
void SC4SanitizeFileName(const cRZString& in, cRZString& out);

// The language manager's number / money formatting (RZ::LM()->vslots 0x80/0x88/0x90).
class ILanguageFormatter {
public:
    virtual ~ILanguageFormatter() = default;
    virtual void FormatMoney(std::int64_t value, cIGZString& out, const cRZString& symbol) = 0;
    virtual void FormatMoney(double value, cIGZString& out, const cIGZString& format, const cRZString& symbol) = 0;
    virtual void FormatNumber(std::int64_t value, cRZString& out) = 0;
};
ILanguageFormatter* GetLanguageFormatter();
void SetLanguageFormatter(ILanguageFormatter* formatter);
void SC4MakeMoneyString(std::int64_t value, cIGZString& out);
void SC4MakeFormattedMoneyString(double value, cIGZString& out, const cIGZString& format);
// Replaces the first '#' of the text by the formatted number.
void SC4InsertNumberIntoString(cRZString& text, std::int64_t value);

}  // namespace sc4
