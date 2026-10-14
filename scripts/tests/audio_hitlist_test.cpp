#include "audio/HitList.h"
#include "audio/HitManAccess.h"
#include "rz/RZConvert.h"
#include "rz/cRZString.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>

namespace {
struct FakeHM final : RZ::cHitManRegisterView {
    std::int32_t next = 0;
    std::int32_t GetRegisterValue(std::int32_t) const override { return 0; }
    std::int32_t Random(std::int32_t range, std::int32_t unused) override {
        assert(unused == 0);
        return range ? next % range : 0;
    }
} g_hm;
}  // namespace
namespace RZ { cHitManRegisterView* HM() { return &g_hm; } }

static std::vector<std::int32_t> Contents(const cHitList& l) {
    std::vector<std::int32_t> v; std::int32_t x;
    for (std::uint32_t i = 0; l.GetIntValueByIndex(i, &x); ++i) v.push_back(x);
    return v;
}

int main() {
    // number parsing
    assert(RZ::ConvertToUint("123") == 123 && RZ::ConvertToUint("0x1F") == 31 && RZ::ConvertToUint("0X10") == 16);
    assert(RZ::ConvertToUint("ff") == 255 && RZ::ConvertToUint("12ab") == 0x12ab && RZ::ConvertToUint("12x") == 12);
    assert(RZ::ConvertToUint("") == 0);

    const auto before = cHitList::LiveListCount();
    {
        cHitList l;
        assert(cHitList::LiveListCount() == before + 1);
        // empty
        assert(l.Choose() == 0 && l.NumItems() == 0);
        // auto criterion = size/2; ids 1..6 -> criterion 3
        for (int i = 1; i <= 6; ++i) l.AddValue(i);
        assert(l.NumItems() == 6);
        // pick index 0 each time (Random fake returns 0): 1,2,3 then recent=[1,2,3] (== criterion, kept)
        assert(l.Choose() == 1 && l.Choose() == 2 && l.Choose() == 3);
        assert((Contents(l) == std::vector<std::int32_t>{4, 5, 6, 1, 2, 3}));
        // next pick pushes 4 -> recent=[1,2,3,4] exceeds 3, oldest (1) returns to available
        assert(l.Choose() == 4);
        assert((Contents(l) == std::vector<std::int32_t>{5, 6, 1, 2, 3, 4}));
        // explicit criterion 0: nothing stays recent
        l.SetCriterion(0);
        assert(l.Choose() == 5 && l.NumItems() == 6);
        // index API spans available then recent
        std::int32_t v = 0;
        assert(l.GetIntValueByIndex(0, &v) && !l.GetIntValueByIndex(6, &v));
        assert(l.SetIntValueByIndex(1, 99) && Contents(l)[1] == 99);
        assert(l.RemoveByIndex(1) && l.NumItems() == 5 && !l.RemoveByIndex(5));
        // Remove all copies
        l.AddValue(7); l.AddValue(7);
        assert(l.Remove(7) && l.NumItems() == 5);
        // stubs
        cRZString unused;
        assert(!l.GetStringValueByIndex(0, unused) && !l.SetStringValueByIndex(0, unused));
        l.RemoveAll(); assert(l.NumItems() == 0);

        // everything recent: recycle oldest
        cHitList r; r.AddValue(10); r.AddValue(20); r.SetCriterion(5);
        r.Choose(); r.Choose();                        // both now recent, available empty
        assert(r.Choose() == 10 && r.Choose() == 20 && r.Choose() == 10);

        // text resource: ids, ranges, hex
        cHitList t;
        const char text[] = "xxxx1,3-5,0x10,7";
        std::uint8_t buf[64]; std::uint32_t len = sizeof(text) - 1 - 4;     // payload length
        std::memcpy(buf, &len, 4); std::memcpy(buf + 4, text + 4, len);
        assert(t.ReadResourceBytes(buf, 4 + len));
        assert((Contents(t) == std::vector<std::int32_t>{1, 3, 4, 5, 16, 7}));
        // criterion = 6/3 + 1 = 3
        t.Choose(); t.Choose(); t.Choose(); t.Choose();                       // 4th pick ages the first back
        assert(t.NumItems() == 6);
        // v1 binary
        std::uint32_t v1[] = {1, 3, 100, 200, 300};
        cHitList b; assert(b.ReadResourceBytes(reinterpret_cast<const std::uint8_t*>(v1), sizeof v1));
        assert((Contents(b) == std::vector<std::int32_t>{100, 200, 300}));
        // v2 binary
        std::uint32_t v2[] = {2, 5, 6};
        cHitList c; assert(c.ReadResourceBytes(reinterpret_cast<const std::uint8_t*>(v2), sizeof v2));
        assert((Contents(c) == std::vector<std::int32_t>{5, 6}));
        // garbage rejected
        std::uint32_t bad[] = {9, 9, 9, 9};
        cHitList d; assert(!d.ReadResourceBytes(reinterpret_cast<const std::uint8_t*>(bad), sizeof bad));
        // serialise = v1 layout and round-trips
        std::vector<std::uint32_t> out; b.WriteResourceDwords(out);
        assert((out == std::vector<std::uint32_t>{1, 3, 100, 200, 300}));
        // refcounting through the interface
        auto* h = new cHitList; void* p = nullptr;
        assert(h->QueryInterface(kGZIID_cIHitList, &p) && p == h && h->GetRefCount() == 1);
        assert(h->Release() == 0);   // count 1 -> deleted
    }
    assert(cHitList::LiveListCount() == before);
    std::puts("cHitList ok");
}
