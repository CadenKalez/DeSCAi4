#include <cstdio>
#include <string>

#include "gz/AlphaBlt.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Recorder final : IAlphaBltBackend {
    std::string log;
    std::uint16_t shadow[2] = {1, 2}, bright[2] = {3, 4};
    const std::uint16_t* lastTable = nullptr;
    std::uint32_t lastColor = 0;
    float lastAlpha = 0;
    void rec(const char* s) { log += s; log += ';'; }
    void GenericBufferClip(cIGZBuffer*, cRZRect&, cRZRect&, const cRZRect*) override { rec("clip"); }
    void InitAlphaSystem(const cGZBufferColorType&) override { rec("init"); }
    cIGZBuffer* AlphaSurfaceFromFile(const char*, std::uint32_t) override { rec("alphaFile"); return reinterpret_cast<cIGZBuffer*>(0x10); }
    cIGZBuffer* MaskSurfaceFromFile(const char*) override { rec("maskFile"); return reinterpret_cast<cIGZBuffer*>(0x20); }
    void AlphaBlt(cIGZBuffer*, cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZPoint&, const cRZRect*) override { rec("alphaBlt"); }
    void AlphaBltNoClip(cIGZBuffer*, cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZPoint&) override { rec("alphaBltNoClip"); }
    void AlphaBltConstAlpha(cIGZBuffer*, cIGZBuffer*, float a, const cRZRect&, const cRZPoint&, const cRZRect*) override { rec("constAlpha"); lastAlpha = a; }
    void AlphaBltConstAlphaNoClip(cIGZBuffer*, cIGZBuffer*, float a, const cRZRect&, const cRZPoint&) override { rec("constAlphaNoClip"); lastAlpha = a; }
    void AlphaBltConstSource(std::uint32_t c, cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZPoint&, const cRZRect*) override { rec("constSource"); lastColor = c; }
    void AlphaBltConstSourceNoClip(std::uint32_t c, cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZPoint&) override { rec("constSourceNoClip"); lastColor = c; }
    void ShadeBltConstAlpha(cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZPoint&, const std::uint16_t* t, const cRZRect*) override { rec("shadeConst"); lastTable = t; }
    void ShadeBltConstAlphaNoClip(cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZPoint&, const std::uint16_t* t) override { rec("shadeConstNoClip"); lastTable = t; }
    void ShadeBlt(cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZPoint&, const cRZRect*) override { rec("shade"); }
    void ShadeBltNoClip(cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZPoint&) override { rec("shadeNoClip"); }
    void ShadeBltStretch(cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZRect&, const cRZRect*) override { rec("stretch"); }
    void ShadeBltStretchNoClip(cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZRect&) override { rec("stretchNoClip"); }
    void ShadeBltWithAdditionalAlpha(cIGZBuffer*, cIGZBuffer*, float a, const cRZRect&, const cRZPoint&, const cRZRect*) override { rec("addAlpha"); lastAlpha = a; }
    void ShadeBltWithAdditionalAlphaNoClip(cIGZBuffer*, cIGZBuffer*, float a, const cRZRect&, const cRZPoint&) override { rec("addAlphaNoClip"); lastAlpha = a; }
    void ShadeBltStretchWithAdditionalAlpha(cIGZBuffer*, cIGZBuffer*, float a, const cRZRect&, const cRZRect&, const cRZRect*) override { rec("stretchAddAlpha"); lastAlpha = a; }
    void ShadeBltStretchWithAdditionalAlphaNoClip(cIGZBuffer*, cIGZBuffer*, float a, const cRZRect&, const cRZRect&) override { rec("stretchAddAlphaNoClip"); lastAlpha = a; }
    void AlphaBltSelfAlpha(cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZPoint&, const cRZRect*) override { rec("selfAlpha"); }
    void AlphaBltSelfAlphaNoClip(cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZPoint&) override { rec("selfAlphaNoClip"); }
    void AlphaBltHalfAlphaVariableSource1ConstSource2(cIGZBuffer*, cIGZBuffer*, std::uint32_t c, const cRZRect&, const cRZPoint&, const cRZRect*) override { rec("half"); lastColor = c; }
    void AlphaBltHalfAlphaVariableSource1ConstSource2NoClip(cIGZBuffer*, cIGZBuffer*, std::uint32_t c, const cRZRect&, const cRZPoint&) override { rec("halfNoClip"); lastColor = c; }
    const std::uint16_t* DefaultShadowTable() override { return shadow; }
    const std::uint16_t* DefaultBrightTable() override { return bright; }
};
}  // namespace

int main() {
    Recorder backend;
    auto* blt = cGZAlphaBlt::CreateObject(backend);
    blt->AddRef();
    CHECK(blt->GetServiceID() == 0x44136fdc && blt->GetServicePriority() == -1000000 && blt->GetAlphaLevelCount() == 0x20);
    void* iface = nullptr;
    CHECK(blt->QueryInterface(kGZIID_cIGZAlphaBlt, &iface) && iface == static_cast<cIGZAlphaBlt*>(blt));
    CHECK(blt->QueryInterface(kGZIID_cIGZSystemService, &iface) && iface == static_cast<cIGZSystemService*>(blt));
    CHECK(!blt->QueryInterface(0x1234, &iface) && blt->QueryInterface(1, &iface));
    blt->Release(); blt->Release(); blt->Release();   // the three QueryInterface references

    cRZRect r{0, 0, 4, 4};
    cRZPoint p{1, 2};
    cIGZBuffer *d = nullptr, *s = nullptr, *a = nullptr;
    blt->ShadowBlitConstAlpha(d, s, r, p, nullptr);
    CHECK(backend.lastTable == backend.shadow);
    blt->ShadowBlitConstAlphaNoClip(d, s, r, p);
    CHECK(backend.lastTable == backend.shadow);
    blt->BrightBlitConstAlpha(d, s, r, p, nullptr);
    CHECK(backend.lastTable == backend.bright);
    blt->BrightBlitConstAlphaNoClip(d, s, r, p);
    CHECK(backend.lastTable == backend.bright);
    const std::uint16_t custom[1] = {9};
    blt->ShadeBltConstAlpha(d, s, r, p, custom, nullptr);
    CHECK(backend.lastTable == custom);
    blt->AlphaBltConstAlpha(d, s, 0.25f, r, p, nullptr);
    CHECK(backend.lastAlpha == 0.25f);
    blt->AlphaBltConstSource(0xff00ff, d, a, r, p, nullptr);
    CHECK(backend.lastColor == 0xff00ff);
    blt->AlphaBltHalfAlphaVariableSource1ConstSource2NoClip(d, s, 0x1234, r, p);
    CHECK(backend.lastColor == 0x1234);
    CHECK(blt->AlphaSurfaceFromFile("x", 1) == reinterpret_cast<cIGZBuffer*>(0x10) && blt->MaskSurfaceFromFile("y") == reinterpret_cast<cIGZBuffer*>(0x20));
    cRZRect r2{};
    blt->GenericBufferClip(d, r, r2, nullptr);
    CHECK(backend.log.size() >= 5 && backend.log.compare(backend.log.size() - 5, 5, "clip;") == 0);
    std::printf(failures ? "FAILED\n" : "gz_alphablt ok\n");
    blt->Release();
    return failures ? 1 : 0;
}
