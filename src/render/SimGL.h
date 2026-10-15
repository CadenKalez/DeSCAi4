// nRZSimGL: vertex-format arithmetic and texture helpers (colour-key to alpha, tinting) used by the
// SimGL graphics layer. Everything here is independent of the rendering backend.
#pragma once
#include <cstdint>

namespace nRZSimGL {

// ---- standard vertex formats ---------------------------------------------------------------------
// eGDVertexFormat values >= 0 are the predefined formats (1,2,3,10,11,0x20..0x26); a negative value is a
// "packed" format with the sign bit set and per-element counts in bit fields.
int PackStandardVertexFormat(int format);
uint32_t MakeStandardVertexFormat(uint32_t elementCount, const uint32_t* elementTypes);
int StandardVFStride(uint32_t format);
int StandardVFElementOffset(uint32_t format, uint32_t elementType, int index);
uint32_t StandardVFNumElements(uint32_t format, uint32_t elementType);
// Copies `count` elements of one type out of an interleaved vertex array into a tightly packed array.
void ExtractElements(uint32_t count, const void* vertices, void* out, uint32_t format, uint32_t elementType, uint32_t index);

// Bit counts of a texture format; false when the (format, type) pair is unsupported. The four outputs
// keep the original argument order (three colour channels, the fourth being alpha).
bool FindBitCounts(uint32_t textureFormat, uint32_t type, uint32_t& bits0, uint32_t& bits1, uint32_t& bits2, uint32_t& bits3);

// ---- colour key -> alpha -------------------------------------------------------------------------
// Pixels equal to `colorKey` (under the format mask) lose their alpha. Strides are in bytes.
void ConvertColorKeyToAlphaStridedTwin16(const void* src, uint32_t srcStride, void* dst, uint32_t dstStride,
                                         uint32_t width, uint32_t height, uint32_t colorKey, uint32_t mask);
void BltColorkeyToAlpha_ARGB1555(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t colorKey);
void BltColorkeyToAlpha_ARGB4444(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t colorKey);
void BltColorkeyToAlpha_ARGB8888(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t colorKey);

// ---- texture tinting (tint is 0xAARRGGBB; width/height > 0) -----------------------------------------
void TintTexture_ARGB8888(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t tint);
void TintTexture_RGB888(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t tint);
// The two 16-bit tinters advance their row pointers by (stride - width*2), as the original does.
void TintTexture_ARGB4444(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t tint);
void TintTexture_ARGB1555(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t tint);
// Compressed textures: only the endpoint colours are tinted; width/height are in texels.
void TintTexture_DXT1(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t tint);
void TintTexture_DXT2345(void* dst, long dstStride, const void* src, long srcStride, uint32_t width, uint32_t height, uint32_t tint);

// The original queries the driver's capability block through a vtable slot, ignores the answer and always
// reports false.
inline bool IsAntialiasingEnabledOnDevice() { return false; }

}  // namespace nRZSimGL
