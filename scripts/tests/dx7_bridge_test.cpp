#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "render/dx7/dx7_to_metal_bridge.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main() {
    using namespace dx7;
    StateRecorder r;
    MetalRenderState s = r.Snapshot();
    CHECK(s.flags == kFlagLighting && s.alphaFunc == 8 && s.world[0] == 1.0f && s.world[1] == 0.0f);

    float m[16] = {};
    for (int i = 0; i < 16; ++i) m[i] = static_cast<float>(i);
    CHECK(r.SetTransform(kTransformWorld, m));
    CHECK(r.Snapshot().world[5] == 5.0f && r.Snapshot().view[5] == 1.0f);
    CHECK(r.SetTransform(kTransformTexture0, m) && (r.Snapshot().texMode & 1u));

    m[3] = NAN;
    CHECK(!r.SetTransform(kTransformView, m));
    CHECK(r.Snapshot().view[3] == 0.0f);
    m[3] = 3.0f;
    CHECK(!r.SetTransform(0x1234, m) && r.UnmappedCount() == 1);

    CHECK(r.SetRenderState(kRenderLighting, 0) && !(r.Snapshot().flags & kFlagLighting));
    CHECK(r.SetRenderState(kRenderAmbient, 0xff804020u));
    s = r.Snapshot();
    CHECK(std::fabs(s.ambient[0] - 128.0f / 255.0f) < 1e-6f && std::fabs(s.ambient[2] - 32.0f / 255.0f) < 1e-6f && s.ambient[3] == 1.0f);
    CHECK(r.SetRenderState(kRenderAlphaTestEnable, 1) && r.SetRenderState(kRenderAlphaRef, 0x1ff) && r.Snapshot().alphaRef == 0xff);
    CHECK(!r.SetRenderState(kRenderAlphaFunc, 9) && !r.SetRenderState(0xdead, 7) && r.UnmappedCount() == 3);
    CHECK(r.SetRenderState(kRenderCullMode, 1) && r.SnapshotFixed().cullMode == 1);

    CHECK(r.SetFVF(kFvfXyz | kFvfNormal | kFvfDiffuse | kFvfTex1));
    CHECK((r.Snapshot().flags & kFlagHasNormal) && (r.Snapshot().flags & kFlagHasDiffuse));
    CHECK(!r.SetFVF(kFvfXyzRhw | kFvfDiffuse) && !r.SetFVF(0x1002 | kFvfXyz));

    StateRecorder shared;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&shared, t] {
            float mat[16] = {};
            mat[0] = mat[5] = mat[10] = mat[15] = static_cast<float>(t + 1);
            for (int i = 0; i < 2000; ++i) {
                shared.SetTransform(kTransformWorld, mat);
                shared.SetRenderState(kRenderLighting, static_cast<std::uint32_t>(i & 1));
                shared.Snapshot();
            }
        });
    for (auto& th : threads) th.join();
    const MetalRenderState f = shared.Snapshot();
    CHECK(f.world[0] == f.world[5] && f.world[5] == f.world[10]);

    if (g_fail == 0) std::printf("dx7_bridge ok\n");
    return g_fail ? 1 : 0;
}
