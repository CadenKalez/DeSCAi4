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
    // ---- model-instance placement
    {
        auto mul = [](const float a[3][3], const float b[3][3], float out[3][3]) {
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
        };
        const float ax = 0.4f, ay = -0.9f, az = 1.3f;
        const float sx = std::sin(ax), cx = std::cos(ax), sy = std::sin(ay), cy = std::cos(ay), sz = std::sin(az), cz = std::cos(az);
        const float Rx[3][3] = {{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}}, Ry[3][3] = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}}, Rz[3][3] = {{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}};
        float t1[3][3], full[3][3], xy[3][3], xz[3][3], yz[3][3];
        mul(Rx, Ry, xy); mul(xy, Rz, full); mul(Rx, Rz, xz); mul(Ry, Rz, yz);
        (void)t1;
        auto same = [&](const cS3DTransform& t, const float e[3][3]) {
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) if (!Near(t.rotation[r][c], e[r][c], 1e-5f)) return false;
            return true;
        };
        cS3DTransform t;
        CHECK(nS3D::SetOrientationEuler(t, {ax, ay, az}) && t.flags == 4 && t.changeCounter == 1 && same(t, full));
        CHECK(nS3D::SetOrientationEuler(t, {ax, nS3D::kOrientationUnset, nS3D::kOrientationUnset}) && same(t, Rx));
        CHECK(t.flags == 4 && t.changeCounter == 3);           // reset + apply
        CHECK(nS3D::SetOrientationEuler(t, {nS3D::kOrientationUnset, ay, nS3D::kOrientationUnset}) && same(t, Ry));
        CHECK(nS3D::SetOrientationEuler(t, {nS3D::kOrientationUnset, nS3D::kOrientationUnset, az}) && same(t, Rz));
        CHECK(nS3D::SetOrientationEuler(t, {ax, ay, nS3D::kOrientationUnset}) && same(t, xy));
        CHECK(nS3D::SetOrientationEuler(t, {ax, nS3D::kOrientationUnset, az}) && same(t, xz));
        CHECK(nS3D::SetOrientationEuler(t, {nS3D::kOrientationUnset, ay, az}) && same(t, yz));
        cS3DTransform untouched;
        CHECK(!nS3D::SetOrientationEuler(untouched, {-100.0f, -100.0f, -100.0f}) && untouched.flags == 0 && untouched.changeCounter == 0);
        // forward / up
        cS3DTransform f;
        nS3D::SetOrientationForwardUp(f, {0, 0, 2}, {0, 1, 0});
        CHECK(Near(f.rotation[0][2], 0) && Near(f.rotation[2][2], 1) && Near(f.rotation[1][1], 1) && f.flags == 4 && f.changeCounter == 1);
        // side = up x forward
        CHECK(Near(f.rotation[0][0], 1) || Near(f.rotation[0][0], -1));
        // placement
        cS3DTransform placed;
        nS3D::SetTranslation(placed, {1, 2, 3});
        CHECK(placed.flags == 1 && placed.changeCounter == 1 && placed.translation[2] == 3);
        placed.scale = 2.0f;
        cS3DVector3 p{1, 0, 0}, v{1, 0, 0};
        nS3D::ModelPointToWorldPoint(placed, p);
        nS3D::ModelVectorToWorldVector(placed, v);
        CHECK(p.x == 3 && p.y == 2 && p.z == 3);      // scale applies to points only
        CHECK(v.x == 1 && v.y == 0 && v.z == 0);
        nS3D::SetOrientationEuler(placed, {nS3D::kOrientationUnset, 1.5707964f, nS3D::kOrientationUnset});   // yaw 90 degrees
        cS3DVector3 q{1, 0, 0};
        nS3D::ModelVectorToWorldVector(placed, q);
        CHECK(Near(q.x, 0, 1e-5f) && Near(q.z, -1, 1e-5f));
    }
    {   // free operators
        cS3DVector3 a{1, 2, 3}, b{4, 5, 6};
        cS3DVector3 s = a + b, d = b - a, n = -a, m = a * 2.0f, c = cross(a, b);
        CHECK(s.x == 5 && s.y == 7 && s.z == 9 && d.x == 3 && d.z == 3 && n.y == -2 && m.z == 6);
        CHECK(c.x == -3 && c.y == 6 && c.z == -3 && dot(a, b) == 32);
        cS3DMatrix4 x, y;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { x.m[i][j] = float(i * 4 + j + 1); y.m[i][j] = float(j == i ? 2 : 0); }
        cS3DMatrix4 xy = x * y;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) CHECK(xy.m[i][j] == 2.0f * x.m[i][j]);
        cS3DMatrix4 id;
        cS3DMatrix4 xi = x * id;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) CHECK(xi.m[i][j] == x.m[i][j]);
        cS3DMatrix4 p, q;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { p.m[i][j] = float(i + 2 * j); q.m[i][j] = float(3 * i - j); }
        cS3DMatrix4 pq = p * q;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
            float want = 0; for (int k = 0; k < 4; ++k) want += p.m[i][k] * q.m[k][j];
            CHECK(pq.m[i][j] == want);
        }
        cS3DVector3 pts[4];
        Bezier3D4ControlsArray({0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, pts, 4);
        CHECK(Near(pts[3].x, 3, 1e-5f) && Near(pts[1].x, 1.5f, 1e-5f) && pts[0].y == 0);   // a straight line is uniform in t
        Bezier3D4ControlsArray({0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, pts, 0);
        vector4 v4; v4.v[0] = 1; v4.v[1] = 2; v4.v[2] = 3; v4.v[3] = 4;
        matrix44 mm;
        mm.m[12] = 10;   // translation row
        vector4 r = mm * v4;
        CHECK(r.v[0] == 1 + 40 && r.v[1] == 2 && r.v[3] == 4);
        matrix44 rx = RotateRadMatrix44('X', 1.5707964f), rz = RotateRadMatrix44('z', 1.5707964f), ry = RotateRadMatrix44('Y', 0.0f);
        CHECK(rx.m[0] == 1 && Near(rx.m[6], 1, 1e-5f) && Near(rx.m[9], -1, 1e-5f) && rx.m[15] == 1);
        CHECK(Near(rz.m[1], 1, 1e-5f) && Near(rz.m[4], -1, 1e-5f) && rz.m[10] == 1);
        CHECK(ry.m[0] == 1 && ry.m[2] == 0 && ry.m[5] == 1 && ry.m[8] == 0 && ry.m[10] == 1);
        matrix44 none = RotateRadMatrix44('Q', 1.0f);
        CHECK(none.m[0] == 0 && none.m[15] == 1);
        nS3D::S3DBlockTag tag{2.5f};
        CHECK(float(tag) == 2.5f);
    }
    {
        cS3DVector3 sv;
        sv.SetValues(1.5f, -2.0f, 3.25f);
        CHECK(sv.x == 1.5f && sv.y == -2.0f && sv.z == 3.25f);
        cS3DTransform t;
        nS3D::Rotate(t, {0, 0, 2.0f}, 1.5707964f);      // the axis is normalised; a quarter turn about z
        CHECK(t.flags == 4 && t.changeCounter == 1);
        CHECK(Near(t.rotation[0][0], 0, 1e-6f) && Near(t.rotation[0][1], -1, 1e-6f) && Near(t.rotation[1][0], 1, 1e-6f));
        CHECK(Near(t.rotation[2][2], 1, 1e-6f) && Near(t.rotation[0][2], 0, 1e-6f));
        cS3DTransform e;
        nS3D::SetOrientationEuler(e, {nS3D::kOrientationUnset, nS3D::kOrientationUnset, 0.7f});
        cS3DTransform r;
        nS3D::Rotate(r, {0, 0, 1}, 0.7f);
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) CHECK(Near(r.rotation[i][j], e.rotation[i][j], 1e-6f));
    }
    if (failures) return 1;
    std::printf("s3d_math: all passed\n");
}
