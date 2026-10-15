// PixelCopy16_32_*: expand rows of 16-bit pixels to 32-bit ARGB8888 (0xAARRGGBB). Each channel is scaled
// with value * 255 / max (the original gets there with multiply-shift tricks that are exact for all inputs).
// Formats without alpha produce alpha 0xff; 1-bit alpha is 0 or 0xff.
#pragma once
#include <cstdint>

namespace nPixelCopy {

void PixelCopy16_32_565_8888(const std::uint8_t* src, std::uint8_t* dst, std::uint32_t count);
void PixelCopy16_32_555_8888(const std::uint8_t* src, std::uint8_t* dst, std::uint32_t count);
void PixelCopy16_32_1555_8888(const std::uint8_t* src, std::uint8_t* dst, std::uint32_t count);
void PixelCopy16_32_5551_8888(const std::uint8_t* src, std::uint8_t* dst, std::uint32_t count);

}  // namespace nPixelCopy

// Byte-order swaps of the big-endian audio/bank loaders: no-ops on this little-endian build, as in the original.
inline void SwapInt(int*) {}
inline void SwapShort(short*) {}
inline void SwapUShort(unsigned short*) {}
inline void SwapUInt(unsigned int*) {}
inline void SwapIntArray(int*, int) {}
