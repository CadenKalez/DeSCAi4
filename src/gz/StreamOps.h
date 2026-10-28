// The framework's stream insertion/extraction operators for small value types (0x1000ce510..0x1000d0xxx).
// `<<` writes to a cIGZOStream and `>>` reads from a cIGZIStream. Vectors are a u32 count followed by
// the elements; reads refuse counts of 1,000,000 or more (the container is left unchanged).
#pragma once
#include <cstdint>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "rz/cRZDate.h"
#include "rz/cRZTimer.h"

// ---- value types used only by the stream operators --------------------------------------------------
struct cRZRect { std::int32_t left = 0, top = 0, right = 0, bottom = 0; };
struct cS3DVector2 { float x = 0, y = 0; };
struct cS3DVector3 {
    float x = 0, y = 0, z = 0;
    void SetValues(float a, float b, float c) { x = a; y = b; z = c; }   // @ 0x100628210
};
// 16 bytes; SC4DrawContext keeps eleven of them as highlight colours (+0x88).
struct cS3DVector4 { float x = 0, y = 0, z = 0, w = 0; };
struct cS3DBoundingBox { float min[3] = {0, 0, 0}, max[3] = {0, 0, 0}; };
struct cS3DMatrix4 { float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}; };
// +0 change counter, +1 flags (1 translation, 2 uniform scale, 4 rotation), +4 3x3 rotation, +0x28 translation,
// +0x34 scale.
struct cS3DTransform {
    std::uint8_t changeCounter = 0;
    std::uint8_t flags = 0;
    float rotation[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float translation[3] = {0, 0, 0};
    float scale = 1.0f;
};

constexpr std::uint32_t kStreamVectorLimit = 1000000;

cIGZOStream& operator<<(cIGZOStream& s, bool v);
cIGZIStream& operator>>(cIGZIStream& s, bool& v);
cIGZOStream& operator<<(cIGZOStream& s, const cRZRect& v);
cIGZIStream& operator>>(cIGZIStream& s, cRZRect& v);
cIGZOStream& operator<<(cIGZOStream& s, const cGZPersistResourceKey& v);
cIGZIStream& operator>>(cIGZIStream& s, cGZPersistResourceKey& v);
cIGZOStream& operator<<(cIGZOStream& s, const cRZDate& v);
cIGZIStream& operator>>(cIGZIStream& s, cRZDate& v);
cIGZOStream& operator<<(cIGZOStream& s, const cRZTimer& v);
cIGZIStream& operator>>(cIGZIStream& s, cRZTimer& v);
cIGZOStream& operator<<(cIGZOStream& s, const cS3DVector2& v);
cIGZIStream& operator>>(cIGZIStream& s, cS3DVector2& v);
cIGZOStream& operator<<(cIGZOStream& s, const cS3DVector3& v);
cIGZIStream& operator>>(cIGZIStream& s, cS3DVector3& v);
cIGZOStream& operator<<(cIGZOStream& s, const cS3DBoundingBox& v);
cIGZIStream& operator>>(cIGZIStream& s, cS3DBoundingBox& v);
cIGZOStream& operator<<(cIGZOStream& s, const cS3DMatrix4& v);
cIGZIStream& operator>>(cIGZIStream& s, cS3DMatrix4& v);
cIGZOStream& operator<<(cIGZOStream& s, const cS3DTransform& v);
cIGZIStream& operator>>(cIGZIStream& s, cS3DTransform& v);
// Pre-version-5 network occupant records: 3x3 rotation, translation, scale and the change counter as plain
// fields (S3DTransformLegacyLoad, 0x1000cf954).
void LoadLegacyS3DTransform(cIGZIStream& s, cS3DTransform& v);
cIGZOStream& operator<<(cIGZOStream& s, const std::vector<float>& v);
cIGZIStream& operator>>(cIGZIStream& s, std::vector<float>& v);
cIGZOStream& operator<<(cIGZOStream& s, const std::vector<std::uint8_t>& v);
cIGZIStream& operator>>(cIGZIStream& s, std::vector<std::uint8_t>& v);
cIGZOStream& operator<<(cIGZOStream& s, const std::vector<std::uint32_t>& v);
cIGZIStream& operator>>(cIGZIStream& s, std::vector<std::uint32_t>& v);
// rows x columns grids (the column count of the first row is written; all rows are assumed equal)
cIGZOStream& operator<<(cIGZOStream& s, const std::vector<std::vector<std::uint8_t>>& v);
cIGZIStream& operator>>(cIGZIStream& s, std::vector<std::vector<std::uint8_t>>& v);
cIGZOStream& operator<<(cIGZOStream& s, const std::vector<std::vector<bool>>& v);
cIGZIStream& operator>>(cIGZIStream& s, std::vector<std::vector<bool>>& v);
