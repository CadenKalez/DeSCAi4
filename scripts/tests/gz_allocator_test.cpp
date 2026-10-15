#include <cstdio>
#include <cstring>

#include "gz/cGZAllocator.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

int main() {
    auto* a = new cGZDefaultAllocator();
    a->AddRef();
    void* p = a->Allocate(16);
    std::memset(p, 7, 16);
    p = a->Reallocate(p, 64);
    CHECK(p != nullptr && static_cast<unsigned char*>(p)[15] == 7);
    a->Deallocate(p);
    void* out = nullptr;
    CHECK(a->QueryInterface(kGZIID_cIGZAllocator, &out) && out == a && !a->QueryInterface(5, &out));
    CHECK(a->Release() == 1);   // drops the reference the query took
    CHECK(a->Release() == 0);

    auto* s = new cGZAllocatorService();
    s->AddRef();
    CHECK(s->GetServiceID() == 0x3ae4bea3 && s->GetServicePriority() == 9000000);
    CHECK(s->QueryInterface(kGZIID_cIGZAllocatorService, &out) && out == static_cast<cIGZAllocator*>(s));
    cIGZAllocator* alloc = static_cast<cIGZAllocator*>(out);
    void* m = alloc->Allocate(8);
    CHECK(m != nullptr);
    alloc->Deallocate(m);
    alloc->Release();
    s->Release();

    auto* e = new cRZExceptionNotification();
    e->AddRef();
    CHECK(e->Notify("boom", 3) && e->QueryInterface(kGZIID_cIRZExceptionNotification, &out) && !e->QueryInterface(9, &out));
    CHECK(e->Release() == 1);
    e->Release();
    std::printf(failures ? "FAILED\n" : "gz_allocator ok\n");
    return failures != 0;
}
