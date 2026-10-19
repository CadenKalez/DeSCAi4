// cSC43DRender's backing-store logic against a recording IBackingStore, plus a pixel-level model of the translated-view blit.
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "sc4/BackingStore.h"

using namespace sc4;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Fake final : gd::IBackingStore {
    bool exists = false;
    std::uint32_t w = 0, h = 0;
    std::vector<std::string> calls;
    bool Create(std::uint32_t cw, std::uint32_t ch) override { w = cw; h = ch; exists = true; calls.push_back("create"); return true; }
    void Destroy() override { exists = false; calls.push_back("destroy"); }
    bool Exists() const override { return exists; }
    void SaveFromFrame(int sx, int sy, int cw, int ch, int dx, int dy) override { calls.push_back("save " + std::to_string(sx) + "," + std::to_string(sy) + " " + std::to_string(cw) + "x" + std::to_string(ch) + " -> " + std::to_string(dx) + "," + std::to_string(dy)); }
    void RestoreToFrame(int sx, int sy, int cw, int ch, int dx, int dy) override { calls.push_back("restore " + std::to_string(sx) + "," + std::to_string(sy) + " " + std::to_string(cw) + "x" + std::to_string(ch) + " -> " + std::to_string(dx) + "," + std::to_string(dy)); }
};
struct Driver final : gd::cIGZGDriver {
    // only GetViewport matters
    std::uint32_t vp[4] = {0, 0, 640, 480};
    void GetViewport(std::uint32_t out[4]) override { for (int i = 0; i < 4; ++i) out[i] = vp[i]; }
#define U(sig) sig override { throw 0; }
    bool QueryInterface(std::uint32_t, void**) override { throw 0; } void AddRef() override {} std::int32_t Release() override { return 0; }
    void DrawArrays(gd::eGDPrimType, int, int) override {} void DrawElements(gd::eGDPrimType, int, gd::eGDType, const void*) override {}
    void InterleavedArrays(gd::eGDVertexFormat, int, const void*) override {}
    gd::eGDVertexFormat MakeVertexFormat(std::uint32_t, gd::eGDElementType*) override { return 0; } gd::eGDVertexFormat MakeVertexFormat(gd::eGDVertexFormat f) override { return f; }
    int VertexFormatStride(gd::eGDVertexFormat) const override { return 0; } int VertexFormatElementOffset(gd::eGDVertexFormat, gd::eGDElementType, std::uint32_t) const override { return 0; }
    int VertexFormatNumElements(gd::eGDVertexFormat, gd::eGDElementType) const override { return 0; }
    void Clear(std::uint32_t) override {} void ClearColor(float, float, float, float) override {} void ClearDepth(double) override {} void ClearStencil(int) override {}
    void ColorMask(bool) override {} void DepthFunc(gd::eGDTestFunc) override {} void DepthMask(bool) override {}
    void StencilFunc(gd::eGDTestFunc, int, std::uint32_t) override {} void StencilMask(std::uint32_t) override {} void StencilOp(gd::eGDStencilOp, gd::eGDStencilOp, gd::eGDStencilOp) override {}
    void BlendFunc(gd::eGDBlend, gd::eGDBlend) override {} void AlphaFunc(gd::eGDTestFunc, float) override {} void ShadeModel(gd::eGDShade) override {}
    void BindTexture(gd::eGDTextureTarget, std::uint32_t) override {}
    void TexImage2D(gd::eGDTextureTarget, int, gd::eGDInternalTextureFormat, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*) override {}
    void PixelStore(gd::eGDParameter, int) override {} void TexEnv(gd::eGDTextureEnvTarget, gd::eGDTextureEnvParamType, gd::eGDTextureEnvModeParam) override {}
    void TexEnv(gd::eGDTextureEnvTarget, gd::eGDTextureEnvParamType, const float*) override {} void TexParameter(gd::eGDTextureTarget, gd::eGDTextureParamType, gd::eGDTextureParam) override {}
    void Fog(gd::eGDFogParamType, gd::eGDFogParam) override {} void Fog(gd::eGDFogParamType, const float*) override {}
    void ColorMultiplier(float, float, float) override {} void AlphaMultiplier(float) override {} void EnableVertexColors(bool, bool) override {}
    void GenTextures(int, std::uint32_t*) override {} void DeleteTextures(int, const std::uint32_t*) override {} bool IsTexture(std::uint32_t) override { return false; }
    void PrioritizeTextures(int, const std::uint32_t*, const float*) override {} bool AreTexturesResident(int, const std::uint32_t*, bool*) override { return false; }
    void MatrixMode(gd::eGDMatrixTarget) override {} void LoadMatrix(const float*) override {} void LoadIdentity() override {} void Flush() override {}
    void Enable(gd::eGDDriverState) override {} void Disable(gd::eGDDriverState) override {} bool IsEnabled(gd::eGDDriverState) override { return false; }
    void GetBoolean(gd::eGDParameter, bool*) override {} void GetInteger(gd::eGDParameter, int*) override {} void GetFloat(gd::eGDParameter, float*) override {} int GetError() override { return 0; }
    void TexStage(std::uint32_t) override {} void TexStageCoord(gd::eGDTexCoordSource) override {} void TexStageMatrix(const float*, std::uint32_t, std::uint32_t, gd::eGDTexMatFlags) override {}
    void TexStageCombine(gd::eGDTextureStageCombineParamType, gd::eGDTextureStageCombineModeParam) override {} void TexStageCombine(gd::eGDTextureStageCombineSourceParamType, gd::eGDTextureStageCombineSourceParam) override {}
    void TexStageCombine(gd::eGDTextureStageCombineOperandParamType, gd::eGDBlend) override {} void TexStageCombine(gd::eGDTextureStageCombineScaleParamType, gd::eGDTextureStageCombineScaleParam) override {}
    void SetTexture(std::uint32_t, std::uint32_t) override {} std::uint32_t GetTexture(std::uint32_t) const override { return 0; }
    std::uint32_t CreateTexture(gd::eGDInternalTextureFormat, std::uint32_t, std::uint32_t, std::uint32_t, gd::eGDTextureHintFlags) override { return 0; }
    void LoadTextureLevel(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, gd::eGDTextureFormat, gd::eGDType, std::uint32_t, const void*) override {}
    void SetCombiner(const gd::cGDCombiner&, std::uint32_t) override {} int CountVideoModes() override { return 0; } bool GetVideoModeInfo(std::uint32_t, gd::sGDMode&) override { return false; }
    bool GetVideoModeInfo(gd::sGDMode&) override { return false; } bool SetVideoMode(int, void*, bool, bool) override { return false; } void PolygonOffset(std::uint32_t) override {}
    void BitBlt(int, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*, bool, const void*) override {} void StretchBlt(int, int, int, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*, bool, const void*) override {}
    void BitBltAlpha(int, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*, bool, const void*, std::uint32_t) override {}
    void StretchBltAlpha(int, int, int, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*, bool, const void*, std::uint32_t) override {}
    void BitBltAlphaModulate(int, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*, bool, const void*, std::uint32_t) override {}
    void StretchBltAlphaModulate(int, int, int, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*, bool, const void*, std::uint32_t) override {}
    void SetViewport() override {} void SetViewport(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
    const void* GetDriverInfo() override { return nullptr; } std::uint32_t GetGZCLSID() const override { return 0; }
    bool Init() override { return true; } bool Shutdown() override { return true; } bool IsDeviceReady() override { return true; } bool Punt(std::uint32_t, void*) override { return false; }
};
}  // namespace

int main() {
    Fake store;
    cSC43DBackingStore bs(&store);
    bs.viewWidth = 640; bs.viewHeight = 480;
    Driver drv;
    drv.vp[0] = 10; drv.vp[1] = 20; drv.vp[2] = 330; drv.vp[3] = 260;   // a 320x240 viewport at (10,20)
    CHECK(!bs.HasBackingStore() && bs.CreateBackingStore(drv) && bs.HasBackingStore() && store.w == 320 && store.h == 240);
    bs.DestroyBackingStore();
    CHECK(!bs.HasBackingStore());

    // plain save / restore
    store.calls.clear();
    CHECK(bs.SaveBackingStore());
    CHECK(store.calls.size() == 1 && store.calls[0] == "save 0,0 640x480 -> 0,0");
    store.calls.clear();
    CHECK(bs.SaveBackingStore(10, 20, 100, 50) && store.calls[0] == "save 10,20 100x50 -> 10,20");
    CHECK(!bs.SaveBackingStore(10, 20, 100, 0));                  // empty height: refused
    CHECK(!bs.SaveBackingStore(10, 20, 0, 5));                    // empty width: refused
    bs.partialWritesOk = false;
    store.calls.clear();
    CHECK(!bs.SaveBackingStore(10, 20, 100, 50) && store.calls.empty());   // partial copy refused
    CHECK(bs.SaveBackingStore(0, 0, 640, 480) && store.calls.size() == 1); // full copy allowed
    bs.partialWritesOk = true;
    // shaken view: the block is shifted by the offset and clipped
    bs.shakeX = 8; bs.shakeY = -6;
    store.calls.clear();
    CHECK(bs.SaveBackingStore(100, 100, 50, 40) && store.calls[0] == "save 100,100 50x40 -> 92,106");
    store.calls.clear();
    CHECK(bs.SaveBackingStore(4, 100, 50, 40) && store.calls[0] == "save 12,100 42x40 -> 0,106");   // clipped on the left
    store.calls.clear();
    CHECK(bs.SaveBackingStore(630, 100, 50, 40) && store.calls[0] == "save 630,100 18x40 -> 622,106");   // clipped on the right
    store.calls.clear();
    CHECK(bs.SaveBackingStore(0, 0, 6, 40) && store.calls.empty());   // entirely off screen: nothing to do, success
    // after zooming the first full save ignores the shake offsets
    cSC43DBackingStore::justZoomed = true;
    store.calls.clear();
    bs.SaveBackingStore();
    CHECK(store.calls.size() == 2 && store.calls[0] == "save 0,0 640x480 -> 0,0" && store.calls[1] == "save 8,0 632x474 -> 0,6");
    CHECK(!cSC43DBackingStore::justZoomed && bs.shakeX == 8 && bs.shakeY == -6);
    // restore honours the shake offset
    store.calls.clear();
    bs.RestoreBackingStore();
    CHECK(store.calls[0] == "restore 0,6 632x474 -> 8,0");
    bs.shakeX = bs.shakeY = 0;
    store.calls.clear();
    bs.RestoreBackingStore();
    CHECK(store.calls[0] == "restore 0,0 640x480 -> 0,0");
    bs.RestoreBackingStore(5, 6, 7, 8, 9, 10);
    CHECK(store.calls[1] == "restore 5,6 7x8 -> 9,10");

    // ---- translated view: model the frame as pixels and check the blit + exposed strips tile the view exactly ----
    std::mt19937 rng(7);
    for (int trial = 0; trial < 500; ++trial) {
        const int W = 20 + static_cast<int>(rng() % 30), H = 20 + static_cast<int>(rng() % 30);
        const int dx = static_cast<int>(rng() % 15) - 7, dy = static_cast<int>(rng() % 15) - 7;
        if (dx == 0 && dy == 0) continue;
        const TranslatedBlit b = ComputeTranslatedBlit(W, H, dx, dy);
        std::vector<int> covered(static_cast<std::size_t>(W) * H, 0);
        // blit region
        bool ok = b.width > 0 && b.height > 0 && b.srcX >= 0 && b.srcY >= 0 && b.srcX + b.width <= W && b.srcY + b.height <= H &&
                  b.dstX >= 0 && b.dstY >= 0 && b.dstX + b.width <= W && b.dstY + b.height <= H;
        CHECK(ok);
        for (int y = 0; y < b.height; ++y)
            for (int x = 0; x < b.width; ++x) {
                ++covered[static_cast<std::size_t>(b.dstY + y) * W + b.dstX + x];
            }
        for (const auto* s : {&b.stripA, &b.stripB})
            for (int y = (*s)[1]; y < (*s)[3]; ++y)
                for (int x = (*s)[0]; x < (*s)[2]; ++x) ++covered[static_cast<std::size_t>(y) * W + x];
        for (int c : covered) if (c != 1) { std::printf("coverage broken: W=%d H=%d dx=%d dy=%d\n", W, H, dx, dy); ++failures; break; }
        // the moved block is displaced by exactly (-dx, +dy) in x/y terms of the original code: dst - src
        CHECK(b.dstX - b.srcX == -dx);
        CHECK(b.dstY - b.srcY == dy);
    }
    // spot values
    TranslatedBlit t = ComputeTranslatedBlit(100, 80, 10, 0);
    CHECK(t.srcX == 10 && t.width == 90 && t.dstX == 0 && t.stripA[0] == 90 && t.stripA[2] == 100 && t.validW == 90);
    t = ComputeTranslatedBlit(100, 80, -10, 5);
    CHECK(t.srcX == 0 && t.width == 90 && t.height == 75 && t.dstX == 10 && t.dstY == 5 && t.stripB[0] == 10 && t.stripB[3] == 5);
    t = ComputeTranslatedBlit(100, 80, 0, -5);
    CHECK(t.srcY == 5 && t.height == 75 && t.stripA[1] == 75 && t.validH == 75);
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("OK");
    return 0;
}
