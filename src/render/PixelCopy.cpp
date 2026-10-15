#include "render/PixelCopy.h"

#include <cstring>

namespace nPixelCopy {
namespace {
inline std::uint32_t Scale(std::uint32_t v, std::uint32_t max) { return (v * 0xff) / max; }

template <class Convert>
void Run(const std::uint8_t* src, std::uint8_t* dst, std::uint32_t count, Convert convert) {
    for (; count != 0; --count, src += 2, dst += 4) {
        std::uint16_t pixel;
        std::memcpy(&pixel, src, 2);
        const std::uint32_t out = convert(pixel);
        std::memcpy(dst, &out, 4);
    }
}
}  // namespace

void PixelCopy16_32_565_8888(const std::uint8_t* src, std::uint8_t* dst, std::uint32_t count) {
    Run(src, dst, count, [](std::uint32_t p) {
        return 0xff000000u | Scale(p >> 11, 31) << 16 | Scale((p >> 5) & 0x3f, 63) << 8 | Scale(p & 0x1f, 31);
    });
}

void PixelCopy16_32_555_8888(const std::uint8_t* src, std::uint8_t* dst, std::uint32_t count) {
    Run(src, dst, count, [](std::uint32_t p) {
        return 0xff000000u | Scale((p >> 10) & 0x1f, 31) << 16 | Scale((p >> 5) & 0x1f, 31) << 8 | Scale(p & 0x1f, 31);
    });
}

void PixelCopy16_32_1555_8888(const std::uint8_t* src, std::uint8_t* dst, std::uint32_t count) {
    Run(src, dst, count, [](std::uint32_t p) {
        return (p >> 15) * 0xff000000u | Scale((p >> 10) & 0x1f, 31) << 16 | Scale((p >> 5) & 0x1f, 31) << 8 | Scale(p & 0x1f, 31);
    });
}

void PixelCopy16_32_5551_8888(const std::uint8_t* src, std::uint8_t* dst, std::uint32_t count) {
    Run(src, dst, count, [](std::uint32_t p) {
        return (p & 1) * 0xff000000u | Scale(p >> 11, 31) << 16 | Scale((p >> 6) & 0x1f, 31) << 8 | Scale((p >> 1) & 0x1f, 31);
    });
}

}  // namespace nPixelCopy
