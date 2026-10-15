#include <cmath>
#include <cstdio>
#include "s3d/S3DMath.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e * (1.0f + std::fabs(a) + std::fabs(b)); }
int main() {
    CHECK(nS3D::CeilPow2(0) == 1 && nS3D::CeilPow2(1) == 1 && nS3D::CeilPow2(2) == 2 && nS3D::CeilPow2(3) == 4);
    CHECK(nS3D::CeilPow2(5) == 8 && nS3D::CeilPow2(1024) == 1024 && nS3D::CeilPow2(1025) == 2048 && nS3D::CeilPow2(-7) == 1);
    cS3DVector3 v{3, 4, 12};
    CHECK(nS3D::GetLength(v) == 13.0f);
    nS3D::Normalize(v);
    CHECK(Near(nS3D::GetLength(v), 1.0f) && Near(v.x, 3.0f / 13.0f));
    nS3D::ScaleBy(v, 2.0f);
    CHECK(Near(nS3D::GetLength(v), 2.0f));
    cRZRandom rng(99);
    for (int i = 0; i < 2000; ++i) {
        const auto d2 = nS3D::RandomDirection2(rng);
        const auto d3 = nS3D::RandomDirection3(rng);
        CHECK(Near(std::sqrt(d2.x * d2.x + d2.y * d2.y), 1.0f, 1e-5f));
        CHECK(Near(nS3D::GetLength(d3), 1.0f, 1e-5f));
    }
    // transform composition: (A *= B) applies B first, then A
    cS3DTransform a, b;
    a.flags = 7; b.flags = 7;
    const float ca = std::cos(0.7f), sa = std::sin(0.7f), cb = std::cos(-1.1f), sb = std::sin(-1.1f);
    float ra[3][3] = {{ca, -sa, 0}, {sa, ca, 0}, {0, 0, 1}};
    float rb[3][3] = {{1, 0, 0}, {0, cb, -sb}, {0, sb, cb}};
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { a.rotation[r][c] = ra[r][c]; b.rotation[r][c] = rb[r][c]; }
    a.translation[0] = 5; a.translation[1] = -2; a.translation[2] = 1; a.scale = 2.0f;
    b.translation[0] = -1; b.translation[1] = 4; b.translation[2] = 3; b.scale = 0.5f;
    const cS3DVector3 p{1.5f, -2.0f, 0.25f};
    cS3DVector3 viaB, viaAB, direct;
    nS3D::TransformPoints(b, &p, &viaB, 1);
    nS3D::TransformPoints(a, &viaB, &viaAB, 1);
    cS3DTransform ab = a;
    const std::uint8_t counter = ab.changeCounter;
    nS3D::MultiplyTransform(ab, b);
    nS3D::TransformPoints(ab, &p, &direct, 1);
    CHECK(Near(direct.x, viaAB.x) && Near(direct.y, viaAB.y) && Near(direct.z, viaAB.z));
    CHECK(Near(ab.scale, 1.0f) && ab.flags == 7 && ab.changeCounter == std::uint8_t(counter + 1));
    // identity right operand changes nothing; identity left operand takes the other
    cS3DTransform id;
    cS3DTransform same = a;
    nS3D::MultiplyTransform(same, id);
    CHECK(same.translation[0] == a.translation[0] && same.changeCounter == a.changeCounter);
    cS3DTransform left;
    nS3D::MultiplyTransform(left, b);
    CHECK(left.flags == b.flags && left.translation[1] == 4.0f && left.changeCounter == 1);
    // flags 0..7 of TransformPoints agree with the analytic formula
    for (int flags = 0; flags < 8; ++flags) {
        cS3DTransform t = a;
        t.flags = std::uint8_t(flags);
        cS3DVector3 out;
        nS3D::TransformPoints(t, &p, &out, 1);
        float x = p.x, y = p.y, z = p.z;
        if (flags & 4) { const float nx = ra[0][0] * x + ra[0][1] * y + ra[0][2] * z, ny = ra[1][0] * x + ra[1][1] * y + ra[1][2] * z, nz = ra[2][0] * x + ra[2][1] * y + ra[2][2] * z; x = nx; y = ny; z = nz; }
        if (flags & 2) { x *= 2; y *= 2; z *= 2; }
        // flags 6 (rotation + scale) reaches the general case of the original, which always adds the translation
        if (flags & 1 || flags == 6) { x += 5; y += -2; z += 1; }
        CHECK(Near(out.x, x) && Near(out.y, y) && Near(out.z, z));
    }
    if (failures) return 1;
    std::printf("s3d_math: all passed\n");
}
