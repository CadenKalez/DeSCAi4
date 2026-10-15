// Compares src/s3d/S3DMath.cpp with the Ghidra decompilation (work/s3dmath/oracle.c).
#include "s3d/S3DMath.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
void oracle_1001c60b8(const void* box, void* corners);
void oracle_1002e944b(const void* a, const void* b, const void* c, void* out);
void oracle_1002e9722(const void* d, void* m);
void oracle_1002e9802(const void* d, void* m);
void oracle_1002e98ef(const void* d, void* m);
void oracle_1002e9a63(const void* d, void* m);
void oracle_1002e9b45(const void* d, void* m);
void oracle_10017670c(const void* t, const void* in, void* out, int n);
}
static long bad = 0;
static float R(float lo, float hi) { return lo + (hi - lo) * (float(rand()) / float(RAND_MAX)); }
static cS3DVector3 PickDir(int i) {
    switch (i % 7) {
        case 0: return {0.0f, R(-1, 1), 0.0f};        // vertical: degenerate branch
        case 1: return {-0.0f, 1.0f, -0.0f};
        case 2: return {R(-1, 1), 0.0f, 0.0f};
        case 3: return {0.0f, 0.0f, R(-1, 1)};
        default: return {R(-3, 3), R(-3, 3), R(-3, 3)};
    }
}
template <class F, class G>
static void AlignCase(const char* name, F mine, G ref) {
    int local = 0;
    for (int i = 0; i < 20000; ++i) {
        const cS3DVector3 d = PickDir(i);
        nS3D::Matrix3 a, b;
        std::memset(a.m, 0xAB, sizeof a.m);
        std::memset(b.m, 0xAB, sizeof b.m);
        mine(d, a);
        ref(&d, b.m);
        if (std::memcmp(a.m, b.m, sizeof a.m)) { ++bad; if (++local < 4) std::printf("MISMATCH %s dir=(%g,%g,%g)\n", name, d.x, d.y, d.z); }
    }
}
int main() {
    srand(11);
    for (int i = 0; i < 100; ++i) {
        cS3DBoundingBox b;
        for (int k = 0; k < 3; ++k) { b.min[k] = R(-5, 0); b.max[k] = R(0, 5); }
        cS3DVector3 x[8], y[8];
        nS3D::MakeBoxCorners(b, x);
        oracle_1001c60b8(&b, y);
        if (std::memcmp(x, y, sizeof x)) { ++bad; std::printf("MISMATCH MakeBoxCorners\n"); break; }
    }
    for (int i = 0; i < 20000; ++i) {
        cS3DVector3 a{R(-9, 9), R(-9, 9), R(-9, 9)}, b{R(-9, 9), R(-9, 9), R(-9, 9)}, c{R(-9, 9), R(-9, 9), R(-9, 9)}, x, y;
        nS3D::TriangleDoubleAreaNormal(a, b, c, x);
        oracle_1002e944b(&a, &b, &c, &y);
        if (std::memcmp(&x, &y, sizeof x)) { if (++bad < 12) std::printf("MISMATCH TriangleDoubleAreaNormal\n"); }
    }
    AlignCase("AlignZToDirWithYUp", nS3D::AlignZToDirWithYUp, oracle_1002e9722);
    AlignCase("AlignXToDirWithYUp", nS3D::AlignXToDirWithYUp, oracle_1002e9802);
    AlignCase("AlignYToDir", nS3D::AlignYToDir, oracle_1002e98ef);
    AlignCase("AlignDirToZWithYUp", nS3D::AlignDirToZWithYUp, oracle_1002e9a63);
    AlignCase("AlignDirToXWithYUp", nS3D::AlignDirToXWithYUp, oracle_1002e9b45);
    for (int i = 0; i < 20000; ++i) {
        cS3DTransform t;
        t.flags = std::uint8_t(i % 8);
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) t.rotation[r][c] = R(-1, 1);
        for (int k = 0; k < 3; ++k) t.translation[k] = R(-50, 50);
        t.scale = R(0.1f, 3);
        cS3DVector3 in[5], x[5], y[5];
        for (auto& v : in) v = {R(-20, 20), R(-20, 20), R(-20, 20)};
        nS3D::TransformPoints(t, in, x, 5);
        oracle_10017670c(&t, in, y, 5);
        if (std::memcmp(x, y, sizeof x)) { if (++bad < 12) std::printf("MISMATCH TransformPoints flags=%d\n", t.flags); }
    }
    if (bad) { std::printf("%ld mismatches\n", bad); return 1; }
    std::printf("s3dmath oracle ok\n");
}
