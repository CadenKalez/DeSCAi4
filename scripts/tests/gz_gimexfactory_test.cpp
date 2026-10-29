// cGZGimexFactory: interface lookup, resource-factory creation and the load path through reader seams.
#include <cstdio>
#include <cstring>
#include <vector>
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
    {   // error-diffusion to ARGB4444: first pixel worked out by hand from the original (0x80 - 8 + 4 = 0x7c per channel)
        cGZGimexInfo flat;
        flat.width = 4; flat.height = 2; flat.bitsPerPixel = 32;
        std::vector<std::uint8_t> source(4 * 2 * 4, 0x80);
        std::vector<std::uint8_t> target(4 * 2 * 2 + 16, 0xee);
        CHECK(factory.DitherToARGB4444(&flat, source.data(), target.data(), 8));
        std::uint16_t first, firstOfSecondRow;
        std::memcpy(&first, target.data(), 2);
        std::memcpy(&firstOfSecondRow, target.data() + 8, 2);
        CHECK(first == 0x7777);
        // the second row is written (its columns run right to left) and every output nibble stays near the input's 8
        for (int i = 0; i < 8; ++i) {
            std::uint16_t v;
            std::memcpy(&v, target.data() + i * 2, 2);
            for (int n = 0; n < 4; ++n) CHECK(((v >> (n * 4)) & 0xf) >= 6 && ((v >> (n * 4)) & 0xf) <= 9);
        }
        CHECK(target[16] == 0xee);                                   // nothing past the image is touched
    }
    if (failures == 0) std::puts("gz_gimexfactory: all passed");
    return failures ? 1 : 0;
}
