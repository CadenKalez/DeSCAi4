#include <cstdio>

#include "rz/cRZString.h"
#include "sc4/SCPropertyHelp.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

int main() {
    cSCPropertyHolder h;
    cISCPropertyHolder* ih = &h;
    // creation on first set, typed read back
    cSCPropertyHelp::SetPropertyValue(ih, 1, std::uint32_t{0xcafe});
    std::uint32_t u = 0;
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 1, u) && u == 0xcafe && h.Count() == 1);
    cSCPropertyHelp::SetPropertyValue(ih, 1, std::uint32_t{7});     // existing property reused
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 1, u) && u == 7 && h.Count() == 1);
    std::uint32_t missing = 5;
    CHECK(!cSCPropertyHelp::GetPropertyValue(ih, 99, missing) && missing == 5);

    cSCPropertyHelp::SetPropertyValue(ih, 2, 2.5f);
    float f = 0;
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 2, f) && f == 2.5f);
    cSCPropertyHelp::SetPropertyValue(ih, 3, true);
    bool b = false;
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 3, b) && b);
    cSCPropertyHelp::SetPropertyValue(ih, 4, std::int64_t{-9000000000});
    std::int64_t i64 = 0;
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 4, i64) && i64 == -9000000000);

    // percentage conversion: value / 100 as float, for the scalar types the switch handles
    cSCPropertyHelp::SetPropertyValue(ih, 5, std::uint8_t{50});
    SC4Percentage p;
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 5, p) && p.value == 50.0f * 0.01f);
    cSCPropertyHelp::SetPropertyValue(ih, 6, std::int32_t{-200});
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 6, p) && p.value == -2.0f);
    cSCPropertyHelp::SetPropertyValue(ih, 7, true);                    // bool: not a percentage type
    p.value = 9;
    CHECK(!cSCPropertyHelp::GetPropertyValue(ih, 7, p) && p.value == 9);

    // arrays
    std::vector<std::uint32_t> in{1, 2, 3, 4};
    cSCPropertyHelp::SetPropertyValue(ih, 10, in);
    std::vector<std::uint32_t> out{9, 9, 9, 9, 9, 9};
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 10, out) && out == in && out.capacity() == 4);
    std::vector<float> fin{0.5f, 1.5f};
    cSCPropertyHelp::SetPropertyValue(ih, 11, fin);
    std::vector<float> fout;
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 11, fout) && fout == fin);
    std::vector<std::uint8_t> bin{1, 0, 1};
    cSCPropertyHelp::SetPropertyValue(ih, 12, bin);
    std::vector<std::uint8_t> bout;
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 12, bout) && bout == bin);
    std::vector<std::int64_t> lin{-1, 1ll << 40};
    cSCPropertyHelp::SetPropertyValue(ih, 13, lin);
    std::vector<std::int64_t> lout{5, 6, 7};
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 13, lout) && lout == lin);

    // key = three dwords of a uint32 array
    cSCPropertyHelp::SetPropertyValue(ih, 14, std::vector<std::uint32_t>{0xaaaa, 0xbbbb, 0xcccc});
    cGZPersistResourceKey key;
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 14, key) && key.type == 0xaaaa && key.group == 0xbbbb && key.instance == 0xcccc);

    cRZString s("text");
    cSCPropertyHelp::SetPropertyValue(ih, 15, static_cast<const cIGZString&>(s));
    cRZString got;
    CHECK(cSCPropertyHelp::GetPropertyValue(ih, 15, static_cast<cIGZString&>(got)) && got.Strlen() == 4);

    std::printf(failures ? "FAILED (%d)\n" : "PASS sc4_propertyhelp\n", failures);
    return failures != 0;
}
