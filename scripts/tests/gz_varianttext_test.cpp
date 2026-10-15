#include <cstdio>

#include "gz/VariantTextValues.h"
#include "rz/cRZVariant.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;
int main() {
    CHECK(GetVariantTypeFromName(cRZString("  uint8 ")) == 2);
    CHECK(GetVariantTypeFromName(cRZString("Sint64")) == 9);
    CHECK(GetVariantTypeFromName(cRZString("string")) == 0x800e);
    CHECK(GetVariantTypeFromName(cRZString("Float32")) == 10);
    CHECK(GetVariantTypeFromName(cRZString("bool")) == 1);
    CHECK(GetVariantTypeFromName(cRZString("int")) == 0);
    CHECK(GetVariantTypeFromName(cRZString("")) == 0);
    std::uint32_t u[3];
    ConvertStringToUnsignedValues32(cRZString("1 0x10   30"), u, 3);
    CHECK(u[0] == 1 && u[1] == 0x10 && u[2] == 30);
    std::int16_t s[2];
    ConvertStringToSignedValues16(cRZString("-5 7 "), s, 2);
    CHECK(s[0] == -5 && s[1] == 7);
    bool b[3];
    ConvertStringToBoolValues(cRZString("T 0 2"), b, 3);
    CHECK(b[0] && !b[1] && b[2]);
    float f[2];
    ConvertStringToFloatValues(cRZString("1.5 -2.25"), f, 2);
    CHECK(f[0] == 1.5f && f[1] == -2.25f);
    cRZVariant v;
    CHECK(GetDefaultVariantValue(v, 6, 1, cRZString(" 0x20")) && v.GetValUint32() == 0x20);
    CHECK(GetDefaultVariantValue(v, 7, 3, cRZString("1 2 3")) && v.GetCount() == 3 && v.RefSint32()[2] == 3);
    CHECK(GetDefaultVariantValue(v, 1, 1, cRZString("true")) && v.GetValBool());
    CHECK(!GetDefaultVariantValue(v, 6, 1, cRZString("   ")));
    CHECK(!GetDefaultVariantValue(v, 8, 1, cRZString("5")));
    CHECK(GetDefaultVariantValue(v, 0x800e, 1, cRZString("hello")));
    CHECK(std::string(GetUnquotedString(cRZString("  \"a b\" tail")).ToChar()) == "a b");
    CHECK(std::string(GetUnquotedString(cRZString("one,two")).ToChar()) == "one");
    CHECK(std::string(GetUnquotedString(cRZString("whole")).ToChar()) == "whole");
    CHECK(GetUnquotedString(cRZString("")).Strlen() == 0);
    std::printf(failures ? "FAILED\n" : "gz_varianttext: all passed\n");
    return failures ? 1 : 0;
}
