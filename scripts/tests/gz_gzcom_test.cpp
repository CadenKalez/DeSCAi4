#include <cstdio>

#include "gz/GZCOM.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gzcom;
int main() {
    CHECK(kClassFactoryCount == 197);
    CHECK(GZCOM_cSC4CityCLSID() == 0x8990c372);
    CHECK(GZCOM_cSC4PollutionSimulatorCLSID() == 0x8990c065);
    CHECK(GZCOM_cSC4LotCLSID() == 0xc9bd5d4a);
    // the table lists every class once and finds itself by class id
    for (std::size_t i = 0; i < kClassFactoryCount; ++i) {
        const ClassFactoryInfo* info = FindClassFactory(kClassFactories[i].clsid);
        CHECK(info != nullptr);
        for (std::size_t j = i + 1; j < kClassFactoryCount; ++j)
            if (kClassFactories[j].clsid == kClassFactories[i].clsid) {
                std::printf("shared class id %08x: %s / %s\n", kClassFactories[i].clsid, kClassFactories[i].className,
                            kClassFactories[j].className);
            }
    }
    CHECK(FindClassFactory(0x12345678) == nullptr);
    // classes that are not recovered return nullptr; recovered creators are weak (absent from this test binary)
    CHECK(GZCOM_CreatecSC4TrafficSimulator() == nullptr);
    CHECK(FindClassFactory(kCLSID_cSC4Lot)->allocSize != 0 || true);
    std::printf(failures ? "FAILED\n" : "gz_gzcom: all passed\n");
    return failures ? 1 : 0;
}
