#include <cstdio>

#include "gz/GZCOM.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    using namespace gzcom;
    struct Entry { void* (*create)(); std::uint32_t clsid; } entries[] = {
        {&GZCOM_CreatecGZMessageServer2, kCLSID_cGZMessageServer2},
        {&GZCOM_CreatecGZDrawContextAsBuffer, kCLSID_cGZDrawContextAsBuffer},
        {&GZCOM_CreatecSC4BusinessUnit, kCLSID_cSC4BusinessUnit},
        {&GZCOM_CreatecSC4IndustrialSimulator, kCLSID_cSC4IndustrialSimulator}};
    for (const Entry& e : entries) {
        void* object = e.create();
        CHECK(object != nullptr);
        const ClassFactoryInfo* info = FindClassFactory(e.clsid);
        CHECK(info != nullptr && info->create == e.create);
    }
    std::printf(failures ? "FAILED\n" : "gz_gzcomcreators6 ok\n");
    return failures != 0;
}
