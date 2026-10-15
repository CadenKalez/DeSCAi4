// Compares src/s3d/TriangleIterator.cpp with the Ghidra decompile (work/s3dtri/oracle.c) and checks the
// index-type conversions and iteration against hand-computed expectations.
#include "s3d/TriangleIterator.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
extern "C" {
int oracle_1001a9eb8(int primType, int n);
void oracle_1001aa050(int primType, unsigned i, unsigned* out);
}
static int bad = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++bad; } } while (0)
using namespace s3d;
int main() {
    for (int t = -1; t < 10; ++t)
        for (int n = -3; n < 40; ++n) {
            CHECK(cS3DTriangleIterator::GetTriangleCountFromPrimitiveType(t, n) == oracle_1001a9eb8(t, n));
        }
    for (int t = -1; t < 10; ++t)
        for (int i = 0; i < 60; ++i) {
            int a[3] = {-7, -7, -7};
            unsigned b[3] = {unsigned(-7), unsigned(-7), unsigned(-7)};
            cS3DTriangleIterator::GetTriangleIndices(t, i, a);
            oracle_1001aa050(t, unsigned(i), b);
            if (std::memcmp(a, b, sizeof a)) { CHECK(false); std::printf("  prim %d tri %d\n", t, i); }
        }
    // iteration over an indexed strip of 5 vertices (3 triangles)
    float verts[5][4];
    for (int i = 0; i < 5; ++i) { verts[i][0] = float(i); verts[i][1] = float(10 * i); verts[i][2] = float(100 * i); verts[i][3] = 9.0f; }
    const std::uint16_t indices[8] = {99, 4, 3, 2, 1, 0, 77, 66};
    MeshPrimitive prims[1] = {{kPrimTriangleStrip, 1, 5}};
    MeshView mesh;
    mesh.vertexStride = 16;
    mesh.vertexData = reinterpret_cast<const std::uint8_t*>(verts);
    mesh.indexData = indices;
    mesh.primitives = prims;
    cS3DTriangleIterator it;
    it.Init(mesh, 0);
    CHECK(it.TriangleCount() == 3);
    int idx[3];
    CHECK(it.GetNextTriangleIndices(idx) && idx[0] == 4 && idx[1] == 3 && idx[2] == 2);
    cS3DVector3 pts[3];
    CHECK(it.GetNextTrianglePoints(pts) && pts[0].x == 3 && pts[0].y == 30 && pts[1].x == 2 && pts[2].z == 100);
    CHECK(it.GetNextTrianglePoints(pts) && pts[0].x == 2 && pts[2].x == 0);
    CHECK(!it.GetNextTriangleIndices(idx) && !it.GetNextTrianglePoints(pts));
    // non-indexed fan starting at vertex 1: vertex data is offset by `first` * stride
    MeshPrimitive fan[1] = {{kPrimTriangleFan, 1, 4}};
    mesh.indexData = nullptr;
    mesh.primitives = fan;
    it.Init(mesh, 0);
    CHECK(it.TriangleCount() == 2);
    CHECK(it.GetNextTrianglePoints(pts) && pts[0].x == 1 && pts[1].x == 2 && pts[2].x == 3);
    CHECK(it.GetNextTrianglePoints(pts) && pts[0].x == 1 && pts[1].x == 3 && pts[2].x == 4);
    // index widths
    {
        cS3DTriangleIterator conv;
        const int in[3] = {0, 1, 2};
        int out[3];
        const std::int8_t i8[3] = {-1, 5, 127};
        conv.SetIndexSource(i8, kIndexInt8);   conv.GetIndicesFromIndexData(in, out); CHECK(out[0] == -1 && out[1] == 5 && out[2] == 127);
        const std::uint8_t u8[3] = {255, 5, 0};
        conv.SetIndexSource(u8, kIndexUint8);  conv.GetIndicesFromIndexData(in, out); CHECK(out[0] == 255 && out[1] == 5);
        const std::int16_t i16[3] = {-300, 5, 32767};
        conv.SetIndexSource(i16, kIndexInt16); conv.GetIndicesFromIndexData(in, out); CHECK(out[0] == -300 && out[2] == 32767);
        const std::uint16_t u16[3] = {65535, 5, 0};
        conv.SetIndexSource(u16, kIndexUint16); conv.GetIndicesFromIndexData(in, out); CHECK(out[0] == 65535);
        const std::int32_t i32[3] = {-70000, 5, 1 << 20};
        conv.SetIndexSource(i32, kIndexInt32); conv.GetIndicesFromIndexData(in, out); CHECK(out[0] == -70000 && out[2] == 1 << 20);
        conv.SetIndexSource(i32, kIndexUint32); conv.GetIndicesFromIndexData(in, out); CHECK(out[1] == 5);
        const float f[3] = {2.7f, 3.0f, -0.2f};
        conv.SetIndexSource(f, kIndexFloat);   conv.GetIndicesFromIndexData(in, out); CHECK(out[0] == 2 && out[1] == 3 && out[2] == -1);
        const float g[3] = {3.5f, 4.0f, 0.0f};
        conv.SetIndexSource(g, kIndexFloat);   conv.GetIndicesFromIndexData(in, out); CHECK(out[0] == 3 && out[1] == 4 && out[2] == 0);
        const double d[3] = {7.9, 8.0, 9.25};
        conv.SetIndexSource(d, kIndexDouble);  conv.GetIndicesFromIndexData(in, out); CHECK(out[0] == 7 && out[1] == 8 && out[2] == 9);
    }
    if (bad) return 1;
    std::printf("s3d_triangleiterator oracle ok\n");
}
