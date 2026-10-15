#include <cstdio>
#include <string>

#include "sc4/SC4Utilities.h"
#include "sc4/SCPropertyHelp.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;
namespace {
class Formatter final : public ILanguageFormatter {
public:
    void FormatMoney(std::int64_t v, cIGZString& out, const cRZString& s) override {
        out.AssignFromCString((std::string(s.ToChar()) + std::to_string(v)).c_str());
    }
    void FormatMoney(double v, cIGZString& out, const cIGZString& f, const cRZString& s) override {
        out.AssignFromCString((std::string(f.ToChar()) + s.ToChar() + std::to_string(static_cast<int>(v))).c_str());
    }
    void FormatNumber(std::int64_t v, cRZString& out) override { out.AssignFromCString(std::to_string(v).c_str()); }
};
}  // namespace
int main() {
    auto* holder = new cSCPropertyHolder();
    holder->AddRef();
    // trip capacity: created on demand as 12 uint16; large values widen the table
    CHECK(GetTripCapacity(holder, 3) == 0);
    SetTripCapacity(holder, 3, 700);
    SetTripCapacity(holder, 5, 12);
    CHECK(GetTripCapacity(holder, 3) == 700 && GetTripCapacity(holder, 5) == 12);
    cISCProperty* p = holder->GetProperty(kPropertyTripCapacity);
    CHECK(p && p->GetPropertyValue()->GetType() == (cIGZVariant::kTypeUint16 | cIGZVariant::kTypeArrayFlag));
    SetTripCapacity(holder, 7, 100000);
    p = holder->GetProperty(kPropertyTripCapacity);
    CHECK(p->GetPropertyValue()->GetType() == (cIGZVariant::kTypeUint32 | cIGZVariant::kTypeArrayFlag));
    CHECK(GetTripCapacity(holder, 7) == 100000 && GetTripCapacity(holder, 3) == 700 && GetTripCapacity(holder, 5) == 12);
    // numeric accessors
    cSCPropertyHelp::SetPropertyValue(holder, 0x10, std::int32_t{-5});
    std::int32_t i = 0;
    CHECK(GetPropertyAsSint32(holder, 0x10, i) && i == -5);
    cSCPropertyHelp::SetPropertyValue(holder, 0x11, std::vector<float>{3.75f, 9.0f});
    CHECK(GetPropertyAsSint32(holder, 0x11, i) && i == 3);
    CHECK(!GetPropertyAsSint32(holder, 0x99, i));
    float f = 0;
    CHECK(GetFloatProperty(holder, 0x11, f) && f == 3.75f);
    CHECK(!GetFloatProperty(holder, 0x10, f));
    // static building
    CHECK(!IsStaticBuilding(holder, true));
    cSCPropertyHelp::SetPropertyValue(holder, 0x27812821, std::uint32_t{1});
    CHECK(IsStaticBuilding(holder, true));
    CHECK(!IsStaticBuilding(holder, false));
    cSCPropertyHelp::SetPropertyValue(holder, 0x8a5e5db8, true);
    CHECK(IsStaticBuilding(holder, false));
    cSCPropertyHelp::SetPropertyValue(holder, 0x029244db, true);
    CHECK(!IsStaticBuilding(holder, true));
    holder->Release();
    // predicates
    CHECK(IsConnectable([](int t) { return t == 3; }));
    CHECK(!IsConnectable([](int t) { return t == 3 || t == 10; }));
    CHECK(!IsConnectable([](int t) { return t == 3 || t == 0; }));
    CHECK(!IsConnectable([](int) { return false; }));
    CHECK(IsStraightConfiguration(0x301) && IsStraightConfiguration(0x3010000) && !IsStraightConfiguration(0x302));
    CHECK(SC4ZoomAndRotationToIndex(2, 3) == 11);
    // strings
    cRZString in("one  two   three"), out;
    SC4SanitizeFileName(cRZString("  a  b:c  "), out);
    CHECK(std::string(out.ToChar()) == "a b c" || std::string(out.ToChar()).find("  ") == std::string::npos);
    cRZString utf("ab\xc3\xa9" "cd"), cut;
    SC4LimitStringLength(utf, utf, 3);                    // would split the 2-byte char: keeps "ab"
    CHECK(std::string(utf.ToChar()) == "ab");
    cRZString src("hello world"), dst("hello world");
    SC4LimitStringLength(src, dst, 5);
    CHECK(std::string(dst.ToChar()) == "hello");
    Formatter formatter;
    SetLanguageFormatter(&formatter);
    cRZString money;
    SC4MakeMoneyString(1234, money);
    CHECK(std::string(money.ToChar()) == "\xc2\xa7" "1234");
    cRZString text("You have # items");
    SC4InsertNumberIntoString(text, 42);
    CHECK(std::string(text.ToChar()) == "You have 42 items");
    SetLanguageFormatter(nullptr);
    std::printf(failures ? "FAILED\n" : "sc4_utilities: all passed\n");
    return failures ? 1 : 0;
}
