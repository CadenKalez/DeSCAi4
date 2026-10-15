// cS3DTriangleIterator (0x1001a9e8c..0x1001aa3d2): walks the triangles of one primitive of a mesh, whatever its
// primitive type (list, strip, fan, quads, lines/points as degenerate triangles) and index type, and returns the
// three vertex indices or positions. Primitive type numbers are eGDPrimType.
//   0 triangle list, 1 triangle strip, 2 triangle fan, 3 points, 4 line list, 5 line strip, 6 quad list-ish
//   (two triangles per 4 vertices, shared edge), 7 quad strip.
// Points and lines come out as degenerate triangles (all three indices on the same segment), as in the original.
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"

namespace s3d {

enum PrimType : int { kPrimTriangles = 0, kPrimTriangleStrip = 1, kPrimTriangleFan = 2, kPrimPoints = 3,
                      kPrimLines = 4, kPrimLineStrip = 5, kPrimQuads = 6, kPrimQuadStrip = 7 };
enum IndexType : int { kIndexInt8 = 0, kIndexUint8, kIndexInt16, kIndexUint16, kIndexInt32, kIndexUint32, kIndexFloat, kIndexDouble };

// A mesh as the iterator sees it: strided vertex data (position first: 3 floats), optional index data, and the
// primitive table (type, first element, element count).
struct MeshPrimitive { std::int32_t type = 0, first = 0, count = 0; };
struct MeshView {
    std::uint32_t vertexFormat = 0;
    std::uint16_t vertexStride = 0;
    const std::uint8_t* vertexData = nullptr;
    const std::uint16_t* indexData = nullptr;       // null: primitives address vertices directly
    const MeshPrimitive* primitives = nullptr;
};

class cS3DTriangleIterator {
public:
    static int GetTriangleCountFromPrimitiveType(int primType, int elementCount);
    static void GetTriangleIndices(int primType, int triangle, int out[3]);

    void Init(const MeshView& mesh, int primitiveIndex);
    bool GetNextTriangleIndices(int out[3]);
    bool GetNextTrianglePoints(cS3DVector3 out[3]);
    void GetIndicesFromIndexData(const int in[3], int out[3]) const;
    void GetPointsFromVertexData(const int indices[3], cS3DVector3 out[3]) const;

    int TriangleCount() const { return triangleCount_; }
    int NextTriangle() const { return next_; }
    // normally the index data is 16-bit unsigned; the other widths are reachable for custom data
    void SetIndexSource(const void* data, int indexType) { indexData_ = data; indexType_ = indexType; }

private:
    int primType_ = 0;
    int elementCount_ = 0;
    std::uint32_t vertexFormat_ = 0;
    int vertexStride_ = 0;
    const std::uint8_t* vertexData_ = nullptr;
    int indexType_ = kIndexUint16;
    const void* indexData_ = nullptr;
    int triangleCount_ = 0;
    int next_ = 0;
};

}  // namespace s3d
