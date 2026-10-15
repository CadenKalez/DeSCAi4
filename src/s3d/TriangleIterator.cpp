#include "s3d/TriangleIterator.h"

#include <cstring>

namespace s3d {

namespace {
// Float -> int as the original does it: bias trick yielding floor-like rounding of x - 0.5, then +1 when that makes
// the float equal to x again.
int RoundIndex(float x) {
    const float t = x + -0.5f + 12582912.0f;
    std::int32_t bits;
    std::memcpy(&bits, &t, 4);
    const int lower = bits + -0x4b400000;
    int result = bits + -0x4b3fffff;
    const float asFloat = float(result);
    if (asFloat != x) result = lower;
    if (asFloat != asFloat || x != x) result = lower;
    return result;
}
}  // namespace

int cS3DTriangleIterator::GetTriangleCountFromPrimitiveType(int primType, int n) {
    switch (primType) {
        case 0: return n / 3;
        case 1: case 2: case 7: return n - 2;
        case 3: return n;
        case 4: case 6: return n / 2;
        case 5: return n - 1;
        default: return 0;
    }
}

void cS3DTriangleIterator::GetTriangleIndices(int primType, int i, int out[3]) {
    const unsigned u = unsigned(i);
    switch (primType) {
        case 0: out[0] = int(u * 3); out[1] = int(u * 3 + 1); out[2] = int(u * 3 + 2); break;
        case 1: out[0] = i; out[1] = i + 1; out[2] = i + 2; break;
        case 2: out[0] = 0; out[1] = i + 1; out[2] = i + 2; break;
        case 3: out[0] = i; out[1] = i; out[2] = i; break;
        case 4: out[0] = i * 2; out[1] = i * 2 + 1; out[2] = i * 2; break;
        case 5: out[0] = i; out[1] = i + 1; out[2] = i; break;
        case 6:
            out[0] = int(u * 2);
            out[1] = int((u & 1) ? u * 2 + 2 : u * 2 + 1);
            out[2] = int((u & 1 | 2) + u * 2);
            break;
        case 7: {
            const int odd = i % 2;
            out[0] = odd + i;
            out[1] = i + 1 + odd;
            out[2] = odd + 2 + i;
            break;
        }
        default: break;   // out is left untouched
    }
}

void cS3DTriangleIterator::Init(const MeshView& mesh, int primitiveIndex) {
    const MeshPrimitive& prim = mesh.primitives[primitiveIndex];
    primType_ = prim.type;
    elementCount_ = prim.count;
    vertexFormat_ = mesh.vertexFormat;
    vertexStride_ = mesh.vertexStride;
    vertexData_ = mesh.vertexData;
    indexType_ = kIndexUint16;
    if (!mesh.indexData) {
        indexData_ = nullptr;
        vertexData_ = mesh.vertexData + std::ptrdiff_t(prim.first) * mesh.vertexStride;
    } else {
        indexData_ = mesh.indexData + prim.first;
    }
    triangleCount_ = GetTriangleCountFromPrimitiveType(primType_, elementCount_);
    next_ = 0;
}

void cS3DTriangleIterator::GetIndicesFromIndexData(const int in[3], int out[3]) const {
    for (int k = 0; k < 3; ++k) {
        const long i = in[k];
        switch (indexType_) {
            case kIndexInt8: out[k] = int(static_cast<const std::int8_t*>(indexData_)[i]); break;
            case kIndexUint8: out[k] = int(static_cast<const std::uint8_t*>(indexData_)[i]); break;
            case kIndexInt16: out[k] = int(static_cast<const std::int16_t*>(indexData_)[i]); break;
            case kIndexUint16: out[k] = int(static_cast<const std::uint16_t*>(indexData_)[i]); break;
            case kIndexInt32: case kIndexUint32: out[k] = int(static_cast<const std::int32_t*>(indexData_)[i]); break;
            case kIndexFloat: out[k] = RoundIndex(static_cast<const float*>(indexData_)[i]); break;
            case kIndexDouble: out[k] = RoundIndex(float(static_cast<const double*>(indexData_)[i])); break;
            default: return;
        }
    }
}

void cS3DTriangleIterator::GetPointsFromVertexData(const int indices[3], cS3DVector3 out[3]) const {
    for (int k = 0; k < 3; ++k) {
        const std::uint8_t* v = vertexData_ + std::ptrdiff_t(vertexStride_) * indices[k];
        std::memcpy(&out[k], v, sizeof(cS3DVector3));
    }
}

bool cS3DTriangleIterator::GetNextTriangleIndices(int out[3]) {
    if (next_ >= triangleCount_) return false;
    if (!indexData_) {
        GetTriangleIndices(primType_, next_, out);
    } else {
        int raw[3];
        GetTriangleIndices(primType_, next_, raw);
        GetIndicesFromIndexData(raw, out);
    }
    ++next_;
    return true;
}

bool cS3DTriangleIterator::GetNextTrianglePoints(cS3DVector3 out[3]) {
    if (next_ >= triangleCount_) return false;
    int raw[3];
    GetTriangleIndices(primType_, next_, raw);
    int resolved[3];
    const int* use = raw;
    if (indexData_) {
        GetIndicesFromIndexData(raw, resolved);
        use = resolved;
    }
    GetPointsFromVertexData(use, out);
    ++next_;
    return true;
}

}  // namespace s3d
