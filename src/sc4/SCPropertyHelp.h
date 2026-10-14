// cSCPropertyHelp — typed accessors over a cISCPropertyHolder (29 functions, 0x10004b46e..0x10004c580).
// Get* return false when the holder has no such property; the vector getters do not check the stored
// type and copy straight out of the variant's array (RefUint8/RefUint32/...), then shrink the
// destination to the exact size. Set* create the property when missing (cSCProperty with that id) and
// then store the value into its variant.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "sc4/SCProperty.h"

namespace sc4 {

struct SC4Percentage { float value = 0.0f; };   // stored as a fraction (property value / 100)

class cSCPropertyHelp {
public:
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, bool& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, std::uint8_t& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, std::int16_t& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, std::uint16_t& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, std::int32_t& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, std::uint32_t& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, std::int64_t& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, float& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, double& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, cIGZString& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, cGZPersistResourceKey& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, SC4Percentage& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, std::vector<std::uint8_t>& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, std::vector<std::uint32_t>& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, std::vector<std::int32_t>& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, std::vector<float>& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, std::vector<bool>& out);
    static bool GetPropertyValue(const cISCPropertyHolder* holder, std::uint32_t id, std::vector<std::int64_t>& out);

    static void SetPropertyValue(cISCPropertyHolder* holder, std::uint32_t id, bool value);
    static void SetPropertyValue(cISCPropertyHolder* holder, std::uint32_t id, std::uint8_t value);
    static void SetPropertyValue(cISCPropertyHolder* holder, std::uint32_t id, std::int32_t value);
    static void SetPropertyValue(cISCPropertyHolder* holder, std::uint32_t id, std::uint32_t value);
    static void SetPropertyValue(cISCPropertyHolder* holder, std::uint32_t id, std::int64_t value);
    static void SetPropertyValue(cISCPropertyHolder* holder, std::uint32_t id, float value);
    static void SetPropertyValue(cISCPropertyHolder* holder, std::uint32_t id, const cIGZString& value);
    static void SetPropertyValue(cISCPropertyHolder* holder, std::uint32_t id, const std::vector<std::uint8_t>& value);
    static void SetPropertyValue(cISCPropertyHolder* holder, std::uint32_t id, const std::vector<std::uint32_t>& value);
    static void SetPropertyValue(cISCPropertyHolder* holder, std::uint32_t id, const std::vector<float>& value);
    static void SetPropertyValue(cISCPropertyHolder* holder, std::uint32_t id, const std::vector<std::int64_t>& value);
};

}  // namespace sc4
