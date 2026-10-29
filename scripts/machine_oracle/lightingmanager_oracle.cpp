// lightingmanager_oracle - test tooling: cSC4LightingManager::CalcShadowProjection against the retail machine code (see GameImage.h).
// Build and run with scripts/run_machine_oracle.sh lightingmanager.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <random>
#include <vector>

#include "GameImage.h"
#define private public
#include "sc4/LightingManager.h"
#undef private
#include "s3d/S3DMath.h"

using namespace machine_oracle;
using namespace sc4;

namespace {
cS3DVector3 gLight{0.4f, -0.8f, 0.3f};
void GetShadowDirectionSlot(cS3DVector3* out, void*) { *out = gLight; }
long gStackGuard = 0x1234567;

cS3DVector3 gSeenDirection;
int gAlignCalls = 0;
void AlignHook(const cS3DVector3* dir, nS3D::Matrix3* out) { gSeenDirection = *dir; ++gAlignCalls; nS3D::AlignDirToZWithYUp(*dir, *out); }

void MulHook(cS3DTransform* self, const cS3DTransform* other) {
    nS3D::MultiplyTransform(*self, *other);
}

using Fn = bool (*)(void*, int, const cS3DVector3*, const cS3DVector2*, const cS3DTransform*, cS3DMatrix4*);
}  // namespace

int main(int argc, char** argv) {
    InstallCrashReport();
    if (!MapGame("game-binaries/MacOS/Sim City 4 Deluxe Editionsub")) return 2;
    At<long*>(0x100a657c8) = &gStackGuard;
    At<float>(0x100b8f8f8) = 0.0f;   // nS3D::k_zero
    At<float>(0x100b8f8fc) = 1.0f;   // nS3D::k_one
    FakeService self;
    self.Set(0x68, reinterpret_cast<const void*>(&GetShadowDirectionSlot));
    // (neither AlignDirToZWithYUp 0x1002e9a63 nor cS3DTransform::operator*= 0x1005f0c20 is hooked: the retail versions run)
    Fn retail = Function<Fn>(0x10077909a);
    cSC4LightingManager manager(nullptr);
    std::mt19937 rng(12345);
    auto uniform = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
    int mismatches = 0, refused = 0, trials = 4000;
    int directionMismatch = 0, byFlags[8] = {};
    for (int trial = 0; trial < trials; ++trial) {
        gLight = {uniform(-1, 1), uniform(-1, -0.1f), uniform(-1, 1)};
        cS3DTransform t;
        t.flags = std::uint8_t(rng() % 8);
        // an orthonormal rotation (Gram-Schmidt of two random vectors)
        cS3DVector3 x{uniform(-1, 1), uniform(-1, 1), uniform(-1, 1)};
        nS3D::Normalize(x);
        cS3DVector3 y{uniform(-1, 1), uniform(-1, 1), uniform(-1, 1)};
        const float d = x.x * y.x + x.y * y.y + x.z * y.z;
        y = {y.x - d * x.x, y.y - d * x.y, y.z - d * x.z};
        nS3D::Normalize(y);
        const cS3DVector3 z{x.y * y.z - x.z * y.y, x.z * y.x - x.x * y.z, x.x * y.y - x.y * y.x};
        const float rot[9] = {x.x, x.y, x.z, y.x, y.y, y.z, z.x, z.y, z.z};
        if (t.flags & 4) std::memcpy(t.rotation, rot, sizeof rot);
        t.translation[0] = uniform(-30, 30); t.translation[1] = uniform(-30, 30); t.translation[2] = uniform(-30, 30);
        t.scale = (t.flags & 2) ? uniform(0.3f, 3.0f) : 1.0f;
        const int n = 1 + int(rng() % 24);
        std::vector<cS3DVector3> pts(n);
        std::vector<cS3DVector2> uv(n);
        for (int i = 0; i < n; ++i) {
            pts[i] = {uniform(-20, 20), uniform(-5, 20), uniform(-20, 20)};
            uv[i] = {uniform(0, 1), uniform(0, 1)};
        }
        cS3DMatrix4 want, got;
        std::memset(&want, 0xcd, sizeof want);
        std::memset(&got, 0xcd, sizeof got);
        const bool okRetail = retail(&self, n, pts.data(), uv.data(), &t, &want);
        if (false) {   // (the direction check needed the hook; AlignDirToZWithYUp now runs as retail code)
            cS3DTransform probe = t;
            float R[9]; std::memcpy(R, probe.rotation, sizeof R);
            float iv[9];
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) iv[3 * r + c] = (t.flags & 4) ? R[3 * c + r] : R[3 * r + c];
            const float nl = -gLight.x;
            cS3DVector3 d{(iv[0] * nl - iv[1] * gLight.y) - iv[2] * gLight.z, (iv[3] * nl - iv[4] * gLight.y) - iv[5] * gLight.z, (iv[6] * nl - iv[7] * gLight.y) - iv[8] * gLight.z};
            if (std::memcmp(&d, &gSeenDirection, sizeof d) != 0) ++directionMismatch;
        }
        manager.rotatedSun_ = gLight;
        const bool okPort = manager.CalcShadowProjection(n, pts.data(), uv.data(), t, &got);
        if (!okRetail) ++refused;
        if (okRetail != okPort || std::memcmp(&want, &got, sizeof want) != 0) {
            ++byFlags[t.flags];
            if (++mismatches <= 2) {
                std::printf("trial %d: flags %d n %d retail %d port %d\n", trial, t.flags, n, int(okRetail), int(okPort));
                for (int r = 0; r < 4; ++r) std::printf("   want %12.7g %12.7g %12.7g %12.7g | got %12.7g %12.7g %12.7g %12.7g\n", want.m[r][0], want.m[r][1], want.m[r][2], want.m[r][3], got.m[r][0], got.m[r][1], got.m[r][2], got.m[r][3]);
            }
        }
    }
    std::printf("mismatches by transform flags:");
    for (int k = 0; k < 8; ++k) std::printf(" %d", byFlags[k]);
    std::printf("\n");
    std::printf("%d of %d trials differ (%d refused by the fit)\n", mismatches, trials, refused);
    (void)argc; (void)argv;
    return 0;
}
