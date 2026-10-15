#include <cstdio>

#include "gz/GZCOMInterface.h"
#include "rz/cRZSysServPtr.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Thing final : cIGZUnknown {
    int refs = 0;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != 1 && iid != 0x1234) return false;
        *out = this;
        ++refs;
        return true;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
Thing g_service;
bool Resolve(std::uint32_t service, std::uint32_t iid, void** out) {
    if (service != 7 || iid != 0x1234) return false;
    return g_service.QueryInterface(iid, out);
}
}  // namespace

int main() {
    Thing thing;
    {
        auto ref = gz::GZCOM_Interface<Thing, cIGZUnknown>(&thing, 0x1234);
        CHECK(ref.get() == &thing && thing.refs == 1);          // exactly one reference held
        Thing* raw = ref;                                       // implicit conversion
        CHECK(raw == &thing);
    }
    CHECK(thing.refs == 0);
    auto none = gz::GZCOM_Interface<Thing, cIGZUnknown>(&thing, 0x9999);
    CHECK(none.get() == nullptr && thing.refs == 0);
    auto missing = gz::GZCOM_Interface<Thing, cIGZUnknown>(nullptr, 1);
    CHECK(missing.get() == nullptr);

    {
        cRZSysServPtr<Thing, 7, 0x1234> before;
        CHECK(before.get() == nullptr);
    }
    SetRZServiceResolver(&Resolve);
    {
        cRZSysServPtr<Thing, 7, 0x1234> service;
        CHECK(service.get() == &g_service && g_service.refs == 1);
        Thing* raw = service;
        CHECK(raw == &g_service);
        cRZSysServPtr<Thing, 8, 0x1234> unknown;
        CHECK(unknown.get() == nullptr);
    }
    CHECK(g_service.refs == 0);
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
