// cRZRGBColorConverter: format masks, channel rescaling (truncating), filling absent channels, pitches.
#include <cstdio>
#include <cstring>

#include "gz/RGBColorConverter.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using F = cRZRGBColorConverter::Format;

int main() {
    // formats
    {
        F a(cGZBufferColorType{kBufferColorARGB4444, 16});
        CHECK(a.bitsPerPixel == 16 && a.alphaMask == 0xf000 && a.redMask == 0xf00 && a.greenMask == 0xf0 && a.blueMask == 0xf);
        F b(cGZBufferColorType{kBufferColorARGB8888, 32});
        CHECK(b.bitsPerPixel == 32 && b.alphaMask == 0xff000000 && b.redMask == 0xff0000);
        F c(cGZBufferColorType{kBufferColorRGB565, 16});
        CHECK(c.redMask == 0xf800 && c.greenMask == 0x7e0 && c.alphaMask == 0);
        F d(cGZBufferColorType{3, 16});
        CHECK(d.bitsPerPixel == 0 && d.redMask == 0);
    }
    // 32 -> 565: channels truncate to their top bits
    {
        cRZRGBColorConverter conv(F(cGZBufferColorType{kBufferColorRGB565, 16}), F(cGZBufferColorType{kBufferColorARGB8888, 32}));
        const std::uint32_t src[3] = {0xff102030, 0x00ffffff, 0x80000000};
        std::uint16_t dst[3] = {};
        conv.Convert(dst, 6, src, 12, 3, 1);
        CHECK(dst[0] == (((0x10 >> 3) << 11) | ((0x20 >> 2) << 5) | (0x30 >> 3)));
        CHECK(dst[1] == 0xffff && dst[2] == 0);
    }
    // 24 (BGR bytes) -> ARGB8888: alpha is filled opaque
    {
        const F tga24(24, 0, 0xff0000, 0xff00, 0xff);
        cRZRGBColorConverter conv(F(cGZBufferColorType{kBufferColorARGB8888, 32}), tga24);
        const std::uint8_t src[6] = {1, 2, 3, 4, 5, 6};   // pixel bytes B G R
        std::uint32_t dst[2] = {};
        conv.Convert(dst, 8, src, 6, 2, 1);
        CHECK(dst[0] == 0xff030201 && dst[1] == 0xff060504);
    }
    // 16 -> 4444 with and without a source alpha bit
    {
        const F tga16(16, 0x8000, 0x7c00, 0x3e0, 0x1f);
        cRZRGBColorConverter conv(F(cGZBufferColorType{kBufferColorARGB4444, 16}), tga16);
        const std::uint16_t src[2] = {0x8000 | (31 << 10) | (16 << 5) | 1, (31 << 10)};
        std::uint16_t dst[2] = {};
        conv.Convert(dst, 4, src, 4, 2, 1);
        CHECK(((dst[0] >> 12) & 0xf) == 0xf && ((dst[0] >> 8) & 0xf) == 0xf && ((dst[0] >> 4) & 0xf) == 8 && (dst[0] & 0xf) == 0);
        CHECK((dst[1] >> 12) == 0);   // no alpha bit set -> transparent
        const F opaque(16, 0, 0x7c00, 0x3e0, 0x1f);
        cRZRGBColorConverter conv2(F(cGZBufferColorType{kBufferColorARGB4444, 16}), opaque);
        conv2.Convert(dst, 4, src, 4, 2, 1);
        CHECK((dst[0] >> 12) == 0xf && (dst[1] >> 12) == 0xf);   // missing source alpha: filled
    }
    // negative pitch flips the rows
    {
        cRZRGBColorConverter conv(F(cGZBufferColorType{kBufferColorXRGB8888, 32}), F(cGZBufferColorType{kBufferColorXRGB8888, 32}));
        const std::uint32_t src[4] = {0x010101, 0x020202, 0x030303, 0x040404};   // 2x2
        std::uint32_t dst[4] = {};
        conv.Convert(dst, 8, src + 2, -8, 2, 2);
        CHECK(dst[0] == 0x030303 && dst[1] == 0x040404 && dst[2] == 0x010101 && dst[3] == 0x020202);
    }
    // 4444 -> 8888 and 32 -> 24 byte writes
    {
        cRZRGBColorConverter widen(F(cGZBufferColorType{kBufferColorXRGB8888, 32}), F(cGZBufferColorType{kBufferColorARGB4444, 16}));
        const std::uint16_t src[1] = {0xf321};
        std::uint32_t dst[1] = {};
        widen.Convert(dst, 4, src, 2, 1, 1);
        CHECK(dst[0] == 0x332211);   // 4-bit levels are scaled by value * 0xffffffff / 15, not replicated into both nibbles
        const std::uint16_t full[1] = {0xffff};
        widen.Convert(dst, 4, full, 2, 1, 1);
        CHECK((dst[0] & 0xffffff) == 0xffffff);
        cRZRGBColorConverter narrow(F(24, 0, 0xff0000, 0xff00, 0xff), F(cGZBufferColorType{kBufferColorXRGB8888, 32}));
        const std::uint32_t one[1] = {0x00112233};
        std::uint8_t out[4] = {0xee, 0xee, 0xee, 0xee};
        narrow.Convert(out, 3, one, 4, 1, 1);
        CHECK(out[0] == 0x33 && out[1] == 0x22 && out[2] == 0x11 && out[3] == 0xee);
        // unsupported byte counts do nothing
        cRZRGBColorConverter bad(F(8, 0, 0, 0, 0xff), F(8, 0, 0, 0, 0xff));
        std::uint8_t d1 = 5; const std::uint8_t s1 = 9;
        bad.Convert(&d1, 1, &s1, 1, 1, 1);
        CHECK(d1 == 5);
    }
    if (failures == 0) std::printf("gz_rgbcolorconverter OK\n");
    return failures == 0 ? 0 : 1;
}
