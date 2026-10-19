// cGZGimexFactory: interface lookup, resource-factory creation and the load path through reader seams.
#include <cstdio>
#include "gz/GimexFactory.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    cGZGimexFactory factory;
    void* out = nullptr;
    CHECK(factory.QueryInterface(kGZIID_cIGZGimexFactory, &out) && out);
    CHECK(!factory.CreateInstance(0x1234, 1, &out));          // only the buffer type is built
    CHECK(!factory.CreateInstance(kGZResourceTypeBuffer, 1, &out));  // no buffer-resource maker installed
    cGZGimexInfo info;
    CHECK(!factory.TranslateGimexToGZBuffer(&info, nullptr, nullptr));
    if (failures == 0) std::puts("gz_gimexfactory: all passed");
    return failures ? 1 : 0;
}
