#include <cstdio>

#include "rz/cRZAutoRefCount.h"
#include "rz/cRZSysServPtr.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

struct Obj {
    int refs = 0, adds = 0, releases = 0;
    std::uint32_t AddRef() { ++adds; return ++refs; }
    std::uint32_t Release() { ++releases; return --refs; }
};

static Obj* g_service = nullptr;
static bool Resolve(std::uint32_t service, std::uint32_t iface, void** out) {
    if (service != 7 || iface != 9 || !g_service) return false;
    g_service->AddRef();
    *out = g_service;
    return true;
}

int main() {
    Obj a, b;
    {
        cRZAutoRefCount<Obj> p(&a);                    // cRZAutoRefCount(T*) takes a reference
        CHECK(a.refs == 1 && p.get() == &a && p.operator->() == &a && static_cast<Obj*>(p) == &a);
        cRZAutoRefCount<Obj> copy(p);                   // copy constructor
        CHECK(a.refs == 2);
        copy = &b;                                      // operator=(T*): AddRef new, Release old
        CHECK(a.refs == 1 && b.refs == 1);
        copy = p;                                       // operator=(const cRZAutoRefCount&)
        CHECK(a.refs == 2 && b.refs == 0);
        copy = copy;                                    // self assignment is safe
        CHECK(a.refs == 2);
        // AsPPVoidParam releases and returns the emptied slot (used before an out-parameter call)
        cRZAutoRefCount<Obj>* slot = copy.AsPPVoidParam();
        CHECK(slot == &copy && !copy && a.refs == 1);
        void** out = copy.AsOutParam();
        *out = &b;                                      // a creation call stores its (already counted) result here
        b.refs = 1;
        CHECK(copy.get() == &b);
    }
    CHECK(a.refs == 0 && b.refs == 0);                  // destructors released everything

    g_service = &a;
    SetRZServiceResolver(Resolve);
    {
        cRZSysServPtr<Obj, 7, 9> service;               // looked up and held
        CHECK(static_cast<Obj*>(service) == &a && service.operator->() == &a && a.refs == 1);
        service = &b;                                   // operator=(T*)
        CHECK(a.refs == 0 && b.refs == 1 && service.get() == &b);
        cRZSysServPtr<Obj, 7, 8> missing;               // unknown interface id: empty
        CHECK(missing.get() == nullptr);
    }
    CHECK(b.refs == 0);
    SetRZServiceResolver(nullptr);
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
