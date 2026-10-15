// nsTexelOp: scanline pixel-format converters used by the SGL texture upload path.
// Every converter has the original signature: (dst, src, dstStride, srcStride, width, height), strides in bytes.
// Pixel layouts (little-endian memory values):
//   ARGB8 0xAARRGGBB, ABGR8 0xAABBGGRR, RGB8 bytes B,G,R, BGR8 bytes R,G,B,
//   ARGB4 0xARGB, ABGR4 0xABGR, A1RGB5 A<<15|R<<10|G<<5|B, A1BGR5 swapped, RGB565, BGR565.
#pragma once
#include <cstdint>

namespace nsTexelOp {

using ConvertFn = void (*)(void* dst, const void* src, int dstStride, int srcStride, int width, int height);

void ConvertARGB8ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertARGB8ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertARGB8ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertARGB8ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertARGB8ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertABGR8ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertABGR8ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertABGR8ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertABGR8ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertABGR8ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertRGB8ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertRGB8ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertRGB8ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertRGB8ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertRGB8ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertBGR8ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertBGR8ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertBGR8ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertBGR8ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertBGR8ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertARGB4ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertARGB4ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertARGB4ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertARGB4ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertARGB4ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertABGR4ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertABGR4ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertABGR4ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertABGR4ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertABGR4ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertA1RGB5ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertA1RGB5ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertA1RGB5ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertA1RGB5ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertA1RGB5ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertA1BGR5ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertA1BGR5ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertA1BGR5ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertA1BGR5ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertA1BGR5ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertRGB565ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertRGB565ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertRGB565ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertRGB565ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertRGB565ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertBGR565ToARGB4(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertBGR565ToRGB565(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertBGR565ToA1RGB5(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertBGR565ToRGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void ConvertBGR565ToARGB8(void* dst, const void* src, int dstStride, int srcStride, int width, int height);

// Row copies (the original picks memcpy/memmove by overlap and size).
void Copy1UB(void* dst, const void* src, int dstStride, int srcStride, int width, int height);
void Copy4UB(void* dst, const void* src, int dstStride, int srcStride, int width, int height);

}  // namespace nsTexelOp
