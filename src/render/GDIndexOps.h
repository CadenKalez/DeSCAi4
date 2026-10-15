// nSGLDX7 index/vertex/format helpers. Metal has no quad, quad-strip or triangle-fan primitives, so the
// game's draw calls are rewritten into indexed triangle lists exactly the way the original driver did.
// The *Indexed* variants also report the minimum and maximum vertex index they touched.
#pragma once
#include <cstdint>

namespace nSGLDX7 {

// Non-indexed primitive -> 16-bit triangle list. `vertexCount` is the number of vertices of the draw.
void GenerateIndexedTriListForQuads(int vertexCount, uint16_t* out);
void GenerateIndexedTriListForQuadStrip(int vertexCount, uint16_t* out);
void GenerateIndexedTriListForTriangleStrip(int vertexCount, uint16_t* out);
void GenerateIndexedTriListForTriangleFan(int vertexCount, uint16_t* out);

// Indexed quads / quad strips (32-, 16- and 8-bit source indices).
void GenerateIndexedTriListForIndexedQuads32(int indexCount, const uint32_t* in, uint16_t* out, uint32_t* minIndex, uint32_t* maxIndex);
void GenerateIndexedTriListForIndexedQuads16(int indexCount, const uint16_t* in, uint16_t* out, uint32_t* minIndex, uint32_t* maxIndex);
void GenerateIndexedTriListForIndexedQuads8(int indexCount, const uint8_t* in, uint16_t* out, uint32_t* minIndex, uint32_t* maxIndex);
void GenerateIndexedTriListForIndexedQuadStrip32(int indexCount, const uint32_t* in, uint16_t* out, uint32_t* minIndex, uint32_t* maxIndex);
void GenerateIndexedTriListForIndexedQuadStrip16(int indexCount, const uint16_t* in, uint16_t* out, uint32_t* minIndex, uint32_t* maxIndex);
void GenerateIndexedTriListForIndexedQuadStrip8(int indexCount, const uint8_t* in, uint16_t* out, uint32_t* minIndex, uint32_t* maxIndex);

// Plain index narrowing to 16 bits, reporting min/max.
void ConvertUint32IndicesToUint16(int count, const void* in, uint16_t* out, uint32_t* minIndex, uint32_t* maxIndex);
void ConvertUint16IndicesToUint16(int count, const void* in, uint16_t* out, uint32_t* minIndex, uint32_t* maxIndex);
void ConvertUint8IndicesToUint16(int count, const void* in, uint16_t* out, uint32_t* minIndex, uint32_t* maxIndex);

// Expansion of the smaller vertex formats into V3F_C4UB_2T2F (32 bytes: xyz, rgba8, 2 x uv).
// `srcStrideFloats` is the source vertex stride in 4-byte units. Fields a format lacks are left untouched
// (missing colour is written as opaque white).
void ConvertVerticesFromV3F(uint8_t* dst, const uint8_t* src, long srcStrideFloats, int count);
void ConvertVerticesFromV3F_C4UB(uint8_t* dst, const uint8_t* src, long srcStrideFloats, int count);
void ConvertVerticesFromV3F_T2F(uint8_t* dst, const uint8_t* src, long srcStrideFloats, int count);
void ConvertVerticesFromV3F_2T2F(uint8_t* dst, const uint8_t* src, long srcStrideFloats, int count);
void ConvertVerticesFromV3F_C4UB_T2F(uint8_t* dst, const uint8_t* src, long srcStrideFloats, int count);

// Texture source formats understood by the upload path.
enum eSupportedSourceFormat : uint32_t {
    kSrcARGB8888 = 0, kSrcABGR8888, kSrcRGB888, kSrcBGR888, kSrcARGB4444Rev, kSrcABGR4444Rev,
    kSrcARGB1555Rev, kSrcABGR1555Rev, kSrcRGB565Rev, kSrcBGR565Rev,
    kSrcDXT1 = 10, kSrcDXT2, kSrcDXT3, kSrcDXT4, kSrcDXT5,
};

uint32_t GetFormatInfoNumAlphaBits(int internalTextureFormat);
uint32_t GetFormatInfoNumColorBits(uint32_t internalTextureFormat);
uint32_t GetSourceFormatNumBits(uint32_t sourceFormat);
// Maps (eGDTextureFormat, eGDType) to a source format. Returns false when the pair is unsupported.
bool TranslateGDFormatToSourceFormat(int gdTextureFormat, uint32_t gdType, uint32_t* sourceFormat);

}  // namespace nSGLDX7
