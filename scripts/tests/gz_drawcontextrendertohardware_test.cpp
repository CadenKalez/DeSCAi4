// cGZDrawContextRenderToHardware: the hardware draw engine's state machine and vertex batching. A recording
// IHardwareRenderHost stands in for the Metal backend (cGZDrawContextRenderToSimGL is not recovered), so every seam call
// is checked: init/shutdown, state pushes, viewport + ortho transform, colour table swizzle, gradient ramps, blt source
// geometry (including the magic-number tie rounding and the truncated origin), solid quads/triangles, and the textured
// non-tiled / tiled / sub-tiled paths.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "gz/DrawContextRenderToHardware.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {

cGZBuffer* MakeBuffer(std::uint32_t w, std::uint32_t h) {
    auto* b = new cGZBuffer();
    b->AddRef();
    CHECK(b->Init(w, h, {kBufferColorARGB8888, 32}));
    b->Fill(0, nullptr);
    return b;
}

// A target that is not a cIGZBuffer: Init's QueryInterface must fail and the engine must not adopt it.
class NotABuffer final : public gz::IDrawTargetBuffer {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 0; }
    const cRZRect* GetBufferArea() const override { return &area_; }
    std::uint32_t ConvertRGBValueToNative(std::uint8_t, std::uint8_t, std::uint8_t) override { return 0; }

private:
    cRZRect area_{0, 0, 4, 4};
};

class RecordingHardware final : public IHardwareRenderHost {
public:
    bool initResult = true;
    int initCalls = 0;
    int shutdownCalls = 0;
    int updateStateCalls = 0;
    int setViewportCalls = 0;
    int setTransformCalls = 0;
    int setImageCalls = 0;
    int getFragmentsCalls = 0;
    int setTextureCalls = 0;
    int drawTriangleCalls = 0;
    int drawQuadCalls = 0;

    cRZRect viewport{};
    float transform[16] = {};
    cIGZBuffer* image = nullptr;
    bool imageWasCleared = false;
    std::uint32_t lastTexture = 0;
    bool lastScaled = false;
    bool lastTiled = false;
    bool tiledTextureSeen = false;
    std::vector<cGZDrawVertex> lastTriangles;
    std::vector<cGZDrawVertex> lastQuads;

    std::vector<cGZDrawImageTexture> textures;
    std::vector<cGZDrawImageFragment> fragments;

    void Reset() { *this = RecordingHardware(); }

    bool Init() override { ++initCalls; return initResult; }
    void Shutdown() override { ++shutdownCalls; }
    void UpdateState() override { ++updateStateCalls; }
    void SetViewport(const cRZRect& viewportIn) override {
        ++setViewportCalls;
        viewport = viewportIn;
    }
    void SetTransform(const float* matrix) override {
        ++setTransformCalls;
        std::memcpy(transform, matrix, sizeof(transform));
    }
    bool SetImage(cIGZBuffer* imageIn) override {
        ++setImageCalls;
        image = imageIn;
        imageWasCleared = imageIn == nullptr;
        return imageIn != nullptr;
    }
    void GetImageFragments(std::vector<cGZDrawImageTexture>& outTextures,
                           std::vector<cGZDrawImageFragment>& outFragments) override {
        ++getFragmentsCalls;
        outTextures = textures;
        outFragments = fragments;
    }
    void SetTexture(std::uint32_t texture, bool scaled, bool tiled) override {
        ++setTextureCalls;
        lastTexture = texture;
        lastScaled = scaled;
        lastTiled = tiled;
        tiledTextureSeen = tiledTextureSeen || tiled;
    }
    void DrawTriangleList(cGZDrawVertex* vertices, int count) override {
        ++drawTriangleCalls;
        lastTriangles.assign(vertices, vertices + (count > 0 ? count : 0));
    }
    void DrawQuadList(cGZDrawVertex* vertices, int count) override {
        ++drawQuadCalls;
        lastQuads.assign(vertices, vertices + (count > 0 ? count : 0));
    }
};

cGZDrawContextRenderToHardware* NewEngine(cGZBuffer* target, RecordingHardware& host) {
    auto* engine = new cGZDrawContextRenderToHardware();
    engine->AddRef();
    engine->SetHardwareRenderHost(&host);
    CHECK(engine->Init(target));
    return engine;
}

constexpr std::uint32_t Swizzle(std::uint32_t rgb) {
    return ((rgb & 0xffffffu) >> 16) | ((rgb << 8 & 0xff0000u) >> 8) | ((rgb << 8 & 0xff00u) << 8) |
           (rgb & 0xff000000u);
}

bool Near(float a, float b) { return std::fabs(a - b) <= 1e-6f; }

cGZDrawImageTexture Texture(std::uint32_t id, std::uint32_t w, std::uint32_t h, std::uint32_t fragments) {
    return cGZDrawImageTexture{id, fragments, w, h};
}

cGZDrawImageFragment Fragment(std::int32_t l, std::int32_t t, std::int32_t r, std::int32_t b) {
    return cGZDrawImageFragment{l, t, r, b, l, t, r, b};
}

}  // namespace

int main() {
    // ---- object lifetime, QueryInterface, refcount ----
    {
        auto* engine = new cGZDrawContextRenderToHardware();
        CHECK(engine->AddRef() == 1);
        CHECK(engine->AddRef() == 2);
        void* out = nullptr;
        CHECK(engine->QueryInterface(kGZIID_cIGZUnknown, &out) && out == engine);
        CHECK(engine->QueryInterface(0x2ae78a07u, &out) && out == engine);   // kGZIID_cIGZDrawEngine
        CHECK(!engine->QueryInterface(0x12345678, &out));
        // Two QueryInterface calls took their own references: 4 in total.
        CHECK(engine->Release() == 3);
        CHECK(engine->Release() == 2);
        CHECK(engine->Release() == 1);
        CHECK(engine->Release() == 0);   // deletes itself
    }

    // ---- Init / canvas / Shutdown ----
    {
        auto* buffer = MakeBuffer(8, 4);
        RecordingHardware host;
        auto* engine = new cGZDrawContextRenderToHardware();
        engine->AddRef();
        engine->SetHardwareRenderHost(&host);
        CHECK(engine->Init(buffer));
        CHECK(engine->Target() == buffer && engine->CanvasWidth() == 8 && engine->CanvasHeight() == 4);
        CHECK(host.initCalls == 1);
        CHECK(engine->StateDirty());   // Init marks the state dirty
        cRZRect area;
        engine->GetCanvasArea(area);
        CHECK(area.left == 0 && area.top == 0 && area.right == 8 && area.bottom == 4);
        // Re-initialising releases the old target once (the test keeps the only other reference).
        auto* second = MakeBuffer(4, 2);
        CHECK(engine->Init(second));
        CHECK(buffer->Release() == 0);   // would be 1 if Init had leaked its reference
        CHECK(engine->CanvasWidth() == 4 && engine->CanvasHeight() == 2);
        engine->Shutdown();
        CHECK(host.shutdownCalls == 1 && engine->Target() == second);   // Shutdown keeps the target
        CHECK(engine->Release() == 0);
        CHECK(second->Release() == 0);
    }
    {
        // A host that refuses leaves the engine without a target.
        auto* buffer = MakeBuffer(4, 4);
        RecordingHardware host;
        host.initResult = false;
        auto* engine = new cGZDrawContextRenderToHardware();
        engine->AddRef();
        engine->SetHardwareRenderHost(&host);
        CHECK(!engine->Init(buffer));
        CHECK(engine->Target() == nullptr && host.initCalls == 1);
        CHECK(engine->Release() == 0);
        CHECK(buffer->Release() == 0);   // Init's failure path released its reference
    }
    {
        auto* engine = new cGZDrawContextRenderToHardware();
        engine->AddRef();
        CHECK(!engine->Init(nullptr));
        NotABuffer notABuffer;
        CHECK(!engine->Init(&notABuffer));
        CHECK(engine->Target() == nullptr);
        CHECK(engine->Release() == 0);
        // No host at all: Init cannot succeed either, and the target is not leaked.
        auto* buffer = MakeBuffer(4, 4);
        auto* bare = new cGZDrawContextRenderToHardware();
        bare->AddRef();
        CHECK(!bare->Init(buffer));
        CHECK(bare->Target() == nullptr);
        CHECK(bare->Release() == 0);
        CHECK(buffer->Release() == 0);
    }

    // ---- clip rectangle: intersection, reject, inverted-in-range quirk, dirty flags ----
    {
        auto* buffer = MakeBuffer(8, 4);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        CHECK(engine->StateDirty());   // Init's dirty flag has not been consumed yet
        engine->SetClipRect({2, 1, 20, 10});
        CHECK(engine->ClipRect().left == 2 && engine->ClipRect().top == 1);
        CHECK(engine->ClipRect().right == 8 && engine->ClipRect().bottom == 4);
        engine->SetClipRect({-5, -5, -1, -1});
        CHECK(engine->ClipRect().left == 0 && engine->ClipRect().top == 0 && engine->ClipRect().right == 0 &&
              engine->ClipRect().bottom == 0);
        // Quirk: a rectangle that is inside the area but inverted (right < left) is accepted and kept inverted.
        engine->SetClipRect({6, 0, 5, 4});
        CHECK(engine->ClipRect().left == 6 && engine->ClipRect().top == 0 && engine->ClipRect().right == 5 &&
              engine->ClipRect().bottom == 4);
        engine->SetClipRect({0, 3, 8, 2});
        CHECK(engine->ClipRect().top == 3 && engine->ClipRect().bottom == 2);
        CHECK(engine->TransformDirty());
        engine->SetClipRect({0, 0, 8, 4});
        CHECK(engine->TransformDirty());   // every SetClipRect raises it again
        engine->Release();
        buffer->Release();
    }

    // ---- colour table swizzle, state setters, no-op entry points ----
    {
        auto* buffer = MakeBuffer(8, 4);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        const std::uint32_t colors[4] = {0x12345678, 0xffeeddcc, 0x01020304, 0xdeadbeef};
        engine->SetColorTable(0, 4, colors);
        for (int i = 0; i < 4; ++i) CHECK(engine->NativeColor(i) == Swizzle(colors[i]));
        CHECK(engine->NativeColor(0) == 0x12785634u);   // ARGB -> ABGR (rotl 8 + bswap)
        engine->SetColorTable(2, 1, colors);            // arbitrary offset, no clamping
        CHECK(engine->NativeColor(2) == 0x12785634u && engine->NativeColor(3) == 0xdeefbeadu);

        engine->SetAlphaBlend(0);
        CHECK(!engine->AlphaBlend() && !engine->AlphaBlendStrong() && engine->StateDirty());
        engine->SetAlphaBlend(1);
        CHECK(engine->AlphaBlend() && !engine->AlphaBlendStrong());
        engine->SetAlphaBlend(2);
        CHECK(engine->AlphaBlend() && engine->AlphaBlendStrong());
        engine->SetBltModulate(true);
        CHECK(engine->BltModulate() && engine->StateDirty());
        engine->SetAntialiasing(true);
        CHECK(engine->Antialiasing());
        engine->SetLineThickness(3.0f);   // ignored
        engine->Suspend();
        CHECK(engine->StateDirty() && engine->TransformDirty());
        engine->Resume();
        engine->Flush();
        engine->UnlockTarget();
        gz::cGZBufferSurfaceDesc desc;
        CHECK(!engine->LockTarget(desc, true));
        const std::uint32_t spans[2] = {0, 0};
        engine->DrawPreclippedSpanList(spans, 1);
        engine->Release();
        buffer->Release();
    }

    // ---- gradient transform: 24.8 ramps, the origin shift and the 0x800000 bias ----
    {
        auto* buffer = MakeBuffer(16, 8);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        const float matrix[3][4] = {{0.5f, 0.25f, 0.0f, 1.0f}, {0.1f, 0.0f, 0.5f, 0.0f}, {0.0f, 0.2f, 0.0f, 0.0f}};
        engine->SetGradientTransform(matrix, 2.0f, 3.0f);
        CHECK(engine->GradientSet());
        const auto roundFixed = [](double v) { return static_cast<std::int32_t>(std::floor(v + 0.5)); };
        const std::int32_t dx0 = roundFixed(static_cast<double>(0.1f * 16711680.0f));
        const std::int32_t dx2 = roundFixed(static_cast<double>(0.5f * 16711680.0f));
        const std::int32_t dy0 = roundFixed(static_cast<double>(0.0f * 16711680.0f));
        const std::int32_t dy1 = roundFixed(static_cast<double>(0.2f * 16711680.0f));
        CHECK(engine->GradientDx()[0] == dx0 && engine->GradientDx()[2] == dx2 && engine->GradientDy()[1] == dy1);
        CHECK(engine->GradientDx()[1] == 0 && engine->GradientDy()[0] == dy0);
        const std::int32_t start0 = roundFixed(static_cast<double>(0.5f * 16711680.0f));
        const float shifted0 =
            0.5f * 16711680.0f - (static_cast<float>(dy0) * 3.0f + static_cast<float>(dx0) * 2.0f);
        const std::int32_t base0 = roundFixed(static_cast<double>(shifted0));
        CHECK(engine->GradientBase()[0] == static_cast<std::int32_t>(static_cast<std::uint32_t>(base0) * 0x100u + 0x800000u));
        CHECK(engine->GradientBase()[0] != static_cast<std::int32_t>(static_cast<std::uint32_t>(start0) * 0x100u + 0x800000u));
        const std::int32_t storedDx0 = engine->GradientDx()[0];
        const std::int32_t storedBase0 = engine->GradientBase()[0];
        engine->SetGradientTransform(nullptr, 0.0f, 0.0f);
        CHECK(!engine->GradientSet());
        CHECK(engine->GradientDx()[0] == storedDx0 && engine->GradientBase()[0] == storedBase0);   // ramps untouched
        engine->Release();
        buffer->Release();
    }

    // ---- ComputeGradientColor: B/G/R/A packed out of the 24.8 ramps ----
    {
        auto* buffer = MakeBuffer(4, 4);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        const float flat[3][4] = {{0.25f, 0.25f, 0.25f, 0.25f}, {0, 0, 0, 0}, {0, 0, 0, 0}};
        engine->SetGradientTransform(flat, 0.0f, 0.0f);
        CHECK(engine->ComputeGradientColor(0, 0) == 0x40404040u);
        const float stepped[3][4] = {{0.25f, 0.25f, 0.25f, 0.25f}, {1.0f, 0, 0, 0}, {1.0f, 0, 0, 0}};
        engine->SetGradientTransform(stepped, 0.0f, 0.0f);
        CHECK(engine->ComputeGradientColor(0, 0) == 0x40404040u);
        CHECK(engine->ComputeGradientColor(1, 0) == 0x40404041u);   // blue ramp advanced one step
        CHECK(engine->ComputeGradientColor(0, 1) == 0x40404041u);
        CHECK(engine->ComputeGradientColor(1, 1) == 0x40404042u);
        engine->Release();
        buffer->Release();
    }

    // ---- SetBltSource: geometry, magic tie rounding, truncated origin, failure paths ----
    {
        auto* buffer = MakeBuffer(8, 4);
        auto* source = MakeBuffer(4, 2);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        engine->SetClipRect({0, 0, 8, 4});
        cRZRect destination{};
        // The destination rectangle is the *target's* area scaled, then shifted by the truncated origin (2.7, -1.5).
        CHECK(engine->SetBltSource(static_cast<cIGZUnknown*>(static_cast<cIGZBuffer*>(source)), 2.7f, -1.5f, 1.0f, 1.0f, destination));
        CHECK(engine->BltSource() == source && !engine->BltScaled());
        CHECK(host.setImageCalls == 1 && host.image == source);
        CHECK(destination.left == 2 && destination.top == -1 && destination.right == 10 && destination.bottom == 3);
        CHECK(engine->StateDirty());

        // Scaled: target area 8x4 with scale (0.75, 0.5) then shifted by the truncated origin (3, 7).
        CHECK(engine->SetBltSource(static_cast<cIGZUnknown*>(static_cast<cIGZBuffer*>(source)), 3.0f, 7.0f, 0.75f, 0.5f, destination));
        CHECK(engine->BltScaled());
        CHECK(destination.left == 3 && destination.top == 7 && destination.right == 9 && destination.bottom == 9);

        // Exact .5 ties round toward -infinity: a 4x2 target at scale 0.875 has a right edge of exactly 3.5 -> 3.
        auto* smallTarget = MakeBuffer(4, 2);
        RecordingHardware tieHost;
        auto* tieEngine = NewEngine(smallTarget, tieHost);
        cRZRect tieDestination{};
        CHECK(tieEngine->SetBltSource(static_cast<cIGZUnknown*>(static_cast<cIGZBuffer*>(source)), 0.0f, 0.0f, 0.875f, 1.0f,
                                     tieDestination));
        CHECK(tieDestination.right == 3 && tieDestination.bottom == 2);
        tieEngine->Release();
        smallTarget->Release();

        // Null source and a source that is not a buffer both fail and clear the backend image.
        host.setImageCalls = 0;
        CHECK(!engine->SetBltSource(nullptr, 0.0f, 0.0f, 1.0f, 1.0f, destination));
        CHECK(engine->BltSource() == nullptr && host.setImageCalls == 1 && host.imageWasCleared);
        CHECK(!engine->SetBltSource(static_cast<cIGZUnknown*>(static_cast<gz::cIGZDrawEngine*>(engine)), 0.0f, 0.0f, 1.0f, 1.0f, destination));
        CHECK(engine->BltSource() == nullptr && host.setImageCalls == 2 && host.imageWasCleared);

        engine->Release();
        source->Release();
        buffer->Release();
    }

    // ---- solid fills: quad building, state/transform push, clip restore ----
    {
        auto* buffer = MakeBuffer(8, 4);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        const std::uint32_t colors[1] = {0xaabbccdd};
        engine->SetColorTable(0, 1, colors);
        engine->SetClipRect({0, 0, 8, 4});
        const float rect[4] = {1.0f, 1.0f, 3.0f, 2.0f};
        CHECK(engine->Draw(gz::eDrawingOp::FillRect, rect, 2) == gz::eEngineDrawResult::Done);
        CHECK(host.updateStateCalls == 1);   // Init's dirty flag
        CHECK(!engine->StateDirty());
        CHECK(host.setViewportCalls == 1 && host.viewport.right == 8 && host.viewport.bottom == 4);
        CHECK(host.setTransformCalls == 1);
        CHECK(Near(host.transform[0], 0.25f) && Near(host.transform[5], -0.5f));   // 2/8, -2/4 (the literal pool of 0x10065564c)
        CHECK(Near(host.transform[10], 0.0f) && Near(host.transform[12], -1.0f) && Near(host.transform[13], 1.0f));
        CHECK(Near(host.transform[14], 0.5f) && Near(host.transform[15], 1.0f));
        CHECK(host.drawQuadCalls == 1);
        CHECK(host.lastQuads.size() == 4);
        const cGZDrawVertex expected[4] = {{1, 1, 0.5f, 0xaaddccbbu, 0, 0},
                                          {1, 2, 0.5f, 0xaaddccbbu, 0, 0},
                                          {3, 2, 0.5f, 0xaaddccbbu, 0, 0},
                                          {3, 1, 0.5f, 0xaaddccbbu, 0, 0}};
        for (int i = 0; i < 4; ++i) {
            CHECK(host.lastQuads[i].x == expected[i].x && host.lastQuads[i].y == expected[i].y);
            CHECK(host.lastQuads[i].z == 0.5f && host.lastQuads[i].color == expected[i].color);
            CHECK(host.lastQuads[i].u == 0.0f && host.lastQuads[i].v == 0.0f);
        }
        // A second draw re-uses the pushed state (no UpdateState) and the current transform (no SetTransform).
        CHECK(engine->Draw(gz::eDrawingOp::FillRect, rect, 2) == gz::eEngineDrawResult::Done);
        CHECK(host.updateStateCalls == 1 && host.setTransformCalls == 1 && host.drawQuadCalls == 2);
        // Two rectangles in one call become eight vertices.
        const float two[8] = {0, 0, 1, 1, 2, 2, 4, 4};
        CHECK(engine->Draw(gz::eDrawingOp::FillRect, two, 4) == gz::eEngineDrawResult::Done);
        CHECK(host.lastQuads.size() == 8);
        // Unsupported operations are left to the context's tesselator and do not touch the backend.
        const int before = host.updateStateCalls + host.drawQuadCalls + host.drawTriangleCalls;
        CHECK(engine->Draw(gz::eDrawingOp::Line, two, 2) == gz::eEngineDrawResult::Tesselate);
        CHECK(engine->Draw(gz::eDrawingOp::Point, two, 1) == gz::eEngineDrawResult::Tesselate);
        CHECK(before == host.updateStateCalls + host.drawQuadCalls + host.drawTriangleCalls);
        // A later clip change marks the transform dirty again.
        engine->SetClipRect({2, 1, 6, 3});
        CHECK(engine->TransformDirty());
        CHECK(engine->Draw(gz::eDrawingOp::FillRect, rect, 2) == gz::eEngineDrawResult::Done);
        CHECK(host.setTransformCalls == 2 && Near(host.transform[0], 0.5f) && Near(host.transform[5], -1.0f));
        CHECK(Near(host.transform[12], -2.0f) && Near(host.transform[13], 2.0f));
        // An empty clip installs the degenerate matrix (only z / w survive).
        engine->SetClipRect({-8, -8, -4, -4});
        CHECK(engine->Draw(gz::eDrawingOp::FillRect, rect, 2) == gz::eEngineDrawResult::Done);
        CHECK(Near(host.transform[0], 0.0f) && Near(host.transform[5], 0.0f));
        CHECK(Near(host.transform[14], 0.5f) && Near(host.transform[15], 1.0f));
        CHECK(host.lastQuads.size() == 4);   // the fill itself only grows the batch, the clip is not applied here
        engine->Release();
        buffer->Release();
    }

    // ---- solid triangles: vertex batch and gradient colours ----
    {
        auto* buffer = MakeBuffer(8, 4);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        const std::uint32_t colors[1] = {0x00112233};
        engine->SetColorTable(0, 1, colors);
        engine->SetClipRect({0, 0, 8, 4});
        const float triangle[6] = {1.0f, 1.0f, 5.0f, 1.0f, 1.0f, 3.0f};
        CHECK(engine->Draw(gz::eDrawingOp::FillTri, triangle, 3) == gz::eEngineDrawResult::Done);
        CHECK(host.drawTriangleCalls == 1 && host.lastTriangles.size() == 3);
        for (int i = 0; i < 3; ++i) {
            CHECK(host.lastTriangles[i].z == 0.5f);
            CHECK(host.lastTriangles[i].color == 0x00332211u);
            CHECK(host.lastTriangles[i].u == 0.0f && host.lastTriangles[i].v == 0.0f);
        }
        CHECK(host.lastTriangles[1].x == 5.0f && host.lastTriangles[2].y == 3.0f);
        // With a gradient the vertex colour is the packed ramp at round(x * 256), round(y * 256).
        const float flat[3][4] = {{0.25f, 0.25f, 0.25f, 0.25f}, {0.1f, 0, 0, 0}, {0, 0.1f, 0, 0}};
        engine->SetGradientTransform(flat, 0.0f, 0.0f);
        CHECK(engine->Draw(gz::eDrawingOp::FillTri, triangle, 3) == gz::eEngineDrawResult::Done);
        const auto roundFixed = [](double v) { return static_cast<std::int32_t>(std::floor(v + 0.5)); };
        for (int i = 0; i < 3; ++i) {
            const std::uint32_t expectedColor =
                engine->ComputeGradientColor(roundFixed(static_cast<double>(triangle[i * 2] * 256.0f)),
                                             roundFixed(static_cast<double>(triangle[i * 2 + 1] * 256.0f)));
            CHECK(host.lastTriangles[i].color == expectedColor);
        }
        CHECK(host.lastTriangles[0].color != 0x40404040u);
        engine->Release();
        buffer->Release();
    }

    // ---- textured triangles, single fragment: seam calls, uv normalisation, clip restore ----
    {
        auto* buffer = MakeBuffer(8, 4);
        auto* source = MakeBuffer(4, 2);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        host.textures = {Texture(7, 4, 2, 1)};
        host.fragments = {Fragment(0, 0, 4, 2)};
        cRZRect destination{};
        CHECK(engine->SetBltSource(static_cast<cIGZUnknown*>(static_cast<cIGZBuffer*>(source)), 0, 0, 1.0f, 1.0f, destination));
        engine->SetClipRect({0, 0, 8, 4});
        const float triangle[6] = {1.0f, 1.0f, 3.0f, 1.0f, 1.0f, 3.0f};
        CHECK(engine->Draw(gz::eDrawingOp::FillTri, triangle, 3) == gz::eEngineDrawResult::Done);
        CHECK(host.getFragmentsCalls == 1);
        CHECK(host.setTextureCalls == 1 && host.lastTexture == 7 && !host.lastScaled && !host.lastTiled);
        CHECK(host.drawTriangleCalls == 1 && host.lastTriangles.size() == 3);
        // Default vertex colour with a blt source is opaque white (the gradient is off).
        for (const auto& vertex : host.lastTriangles) CHECK(vertex.color == 0xffffffffu);
        // u = (u0 + (x - bltX - frag.left)) / (scaleX * textureWidth), v / (scaleY * textureHeight)
        CHECK(Near(host.lastTriangles[0].u, 1.0f * 0.25f) && Near(host.lastTriangles[0].v, 1.0f * 0.5f));
        CHECK(Near(host.lastTriangles[1].u, 3.0f * 0.25f) && Near(host.lastTriangles[1].v, 1.0f * 0.5f));
        CHECK(Near(host.lastTriangles[2].u, 1.0f * 0.25f) && Near(host.lastTriangles[2].v, 3.0f * 0.5f));
        // DrawTriangleBlt clips to the fragment rectangle (scaled and shifted) and pushes its own transform.
        CHECK(host.setViewportCalls >= 2 && host.viewport.right == 4 && host.viewport.bottom == 2);
        CHECK(engine->ClipRect().right == 8 && engine->ClipRect().bottom == 4);   // clip restored afterwards
        // The source's buffer area is queried (and discarded); a second draw repeats the whole path.
        CHECK(engine->Draw(gz::eDrawingOp::FillTri, triangle, 3) == gz::eEngineDrawResult::Done);
        CHECK(host.drawTriangleCalls == 2 && host.getFragmentsCalls == 2);
        // A texture with no fragments draws nothing but still sets the texture.
        host.textures = {Texture(9, 4, 2, 0)};
        host.fragments.clear();
        CHECK(engine->Draw(gz::eDrawingOp::FillTri, triangle, 3) == gz::eEngineDrawResult::Done);
        CHECK(host.drawTriangleCalls == 2 && host.lastTexture == 9);
        engine->Release();
        source->Release();
        buffer->Release();
    }

    // ---- textured triangles, tiled: one draw per tile the source rectangle covers ----
    {
        auto* buffer = MakeBuffer(8, 4);
        auto* source = MakeBuffer(4, 4);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        host.textures = {Texture(3, 4, 4, 1)};
        host.fragments = {Fragment(0, 0, 4, 4)};
        cRZRect destination{};
        CHECK(engine->SetBltSource(static_cast<cIGZUnknown*>(static_cast<cIGZBuffer*>(source)), 0, 0, 1.0f, 1.0f, destination));
        engine->SetBltTiling({0, 0, 2, 2});
        CHECK(engine->Tiling());
        engine->SetClipRect({0, 0, 8, 4});
        const float triangle[6] = {1.0f, 1.0f, 3.0f, 1.0f, 1.0f, 3.0f};
        CHECK(engine->Draw(gz::eDrawingOp::FillTri, triangle, 3) == gz::eEngineDrawResult::Done);
        // texture rect {1,1,3,3} over a 2x2 tile: columns 0..1, rows 0..1 -> four blits.
        CHECK(host.drawTriangleCalls == 4);
        engine->ClearBltTiling();
        CHECK(!engine->Tiling());
        engine->Release();
        source->Release();
        buffer->Release();
    }

    // ---- textured rectangles, non-tiled: one quad per source-space intersection ----
    {
        auto* buffer = MakeBuffer(8, 4);
        auto* source = MakeBuffer(4, 2);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        host.textures = {Texture(7, 4, 2, 1)};
        host.fragments = {Fragment(0, 0, 4, 2)};
        cRZRect destination{};
        CHECK(engine->SetBltSource(static_cast<cIGZUnknown*>(static_cast<cIGZBuffer*>(source)), 0, 0, 1.0f, 1.0f, destination));
        engine->SetClipRect({0, 0, 8, 4});
        const float rect[4] = {1.0f, 1.0f, 3.0f, 3.0f};
        CHECK(engine->Draw(gz::eDrawingOp::FillRect, rect, 2) == gz::eEngineDrawResult::Done);
        CHECK(host.setTextureCalls == 1 && host.lastTexture == 7 && !host.lastTiled);
        CHECK(host.drawQuadCalls == 1 && host.lastQuads.size() == 4);
        // The intersection is clamped to the fragment (bottom 3 -> 2), uv is (u0 + (p - frag.left)) / texture size.
        CHECK(host.lastQuads[0].x == 1.0f && host.lastQuads[0].y == 1.0f);
        CHECK(Near(host.lastQuads[0].u, 0.25f) && Near(host.lastQuads[0].v, 0.5f));
        CHECK(host.lastQuads[1].x == 1.0f && host.lastQuads[1].y == 2.0f);
        CHECK(Near(host.lastQuads[1].u, 0.25f) && Near(host.lastQuads[1].v, 1.0f));
        CHECK(host.lastQuads[2].x == 3.0f && host.lastQuads[2].y == 2.0f);
        CHECK(Near(host.lastQuads[2].u, 0.75f) && Near(host.lastQuads[2].v, 1.0f));
        CHECK(host.lastQuads[3].x == 3.0f && host.lastQuads[3].y == 1.0f);
        CHECK(Near(host.lastQuads[3].u, 0.75f) && Near(host.lastQuads[3].v, 0.5f));
        CHECK(engine->ClipRect().right == 8 && engine->ClipRect().bottom == 4);
        engine->Release();
        source->Release();
        buffer->Release();
    }

    // ---- textured rectangles, tiling with the fragment equal to the tile: scaled texture path ----
    {
        auto* buffer = MakeBuffer(8, 4);
        auto* source = MakeBuffer(4, 2);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        host.textures = {Texture(5, 4, 2, 1)};
        host.fragments = {Fragment(0, 0, 4, 2)};
        cRZRect destination{};
        CHECK(engine->SetBltSource(static_cast<cIGZUnknown*>(static_cast<cIGZBuffer*>(source)), 0, 0, 1.0f, 1.0f, destination));
        engine->SetBltTiling({0, 0, 4, 2});
        engine->SetClipRect({0, 0, 8, 4});
        const float rect[4] = {1.0f, 1.0f, 3.0f, 3.0f};
        CHECK(engine->Draw(gz::eDrawingOp::FillRect, rect, 2) == gz::eEngineDrawResult::Done);
        CHECK(host.setTextureCalls == 2 && host.lastTiled);   // once plain, once tiled
        CHECK(host.tiledTextureSeen);
        CHECK(host.drawQuadCalls == 1 && host.lastQuads.size() == 4);
        // Vertices keep the destination corners (2 points: opposite corners of the rect).
        CHECK(host.lastQuads[0].x == 1.0f && host.lastQuads[0].y == 1.0f);
        CHECK(host.lastQuads[1].x == 1.0f && host.lastQuads[1].y == 3.0f);
        CHECK(host.lastQuads[2].x == 3.0f && host.lastQuads[2].y == 3.0f);
        CHECK(host.lastQuads[3].x == 3.0f && host.lastQuads[3].y == 1.0f);
        // uv = source-space rectangle / texture size.
        CHECK(Near(host.lastQuads[0].u, 0.25f) && Near(host.lastQuads[0].v, 0.5f));
        CHECK(Near(host.lastQuads[1].u, 0.25f) && Near(host.lastQuads[1].v, 1.5f));
        CHECK(Near(host.lastQuads[2].u, 0.75f) && Near(host.lastQuads[2].v, 1.5f));
        CHECK(Near(host.lastQuads[3].u, 0.75f) && Near(host.lastQuads[3].v, 0.5f));
        engine->Release();
        source->Release();
        buffer->Release();
    }

    // ---- textured rectangles, sub-tiled: the rect is cut into tile-sized pieces ----
    {
        auto* buffer = MakeBuffer(8, 8);
        auto* source = MakeBuffer(4, 4);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        host.textures = {Texture(2, 4, 4, 1)};
        host.fragments = {Fragment(0, 0, 4, 4)};
        cRZRect destination{};
        CHECK(engine->SetBltSource(static_cast<cIGZUnknown*>(static_cast<cIGZBuffer*>(source)), 0, 0, 1.0f, 1.0f, destination));
        engine->SetBltTiling({0, 0, 2, 2});
        engine->SetClipRect({0, 0, 8, 8});
        const float rect[4] = {1.0f, 1.0f, 5.0f, 5.0f};
        CHECK(engine->Draw(gz::eDrawingOp::FillRect, rect, 2) == gz::eEngineDrawResult::Done);
        CHECK(!host.lastTiled);   // the fragment differs from the tile: the plain texture path
        CHECK(host.drawQuadCalls == 1);
        CHECK(host.lastQuads.size() == 36);   // 3 columns x 3 rows of quads
        // First quad: the clamp of {1,1,5,5} shifted by (0,0) to the tile {0,0,2,2}.
        CHECK(host.lastQuads[0].x == 1.0f && host.lastQuads[0].y == 1.0f);
        CHECK(host.lastQuads[2].x == 2.0f && host.lastQuads[2].y == 2.0f);
        CHECK(Near(host.lastQuads[0].u, 0.25f) && Near(host.lastQuads[0].v, 0.25f));
        // Second quad (column 1): the rect shifted by -tileWidth, clamped to the tile.
        CHECK(host.lastQuads[4].x == 2.0f && host.lastQuads[4].y == 1.0f);
        CHECK(host.lastQuads[6].x == 4.0f && host.lastQuads[6].y == 2.0f);
        CHECK(Near(host.lastQuads[4].u, 0.0f) && Near(host.lastQuads[4].v, 0.25f));
        CHECK(Near(host.lastQuads[6].u, 0.5f) && Near(host.lastQuads[6].v, 0.5f));
        // Last quad (row 2, column 2): the far corner of the rect.
        CHECK(host.lastQuads[34].x == 5.0f && host.lastQuads[34].y == 5.0f);
        engine->Release();
        source->Release();
        buffer->Release();
    }

    // ---- a fragmented texture list walks the fragments across textures ----
    {
        auto* buffer = MakeBuffer(8, 4);
        auto* source = MakeBuffer(4, 2);
        RecordingHardware host;
        auto* engine = NewEngine(buffer, host);
        host.textures = {Texture(11, 4, 2, 1), Texture(12, 4, 2, 1)};
        host.fragments = {Fragment(0, 0, 2, 2), Fragment(2, 0, 4, 2)};
        cRZRect destination{};
        CHECK(engine->SetBltSource(static_cast<cIGZUnknown*>(static_cast<cIGZBuffer*>(source)), 0, 0, 1.0f, 1.0f, destination));
        engine->SetClipRect({0, 0, 8, 4});
        const float rect[4] = {0.0f, 0.0f, 4.0f, 2.0f};
        CHECK(engine->Draw(gz::eDrawingOp::FillRect, rect, 2) == gz::eEngineDrawResult::Done);
        CHECK(host.drawQuadCalls == 2);          // one quad list per texture
        CHECK(host.lastTexture == 12);
        CHECK(host.lastQuads.size() == 4);       // only the second fragment matches the second texture
        engine->Release();
        source->Release();
        buffer->Release();
    }

    if (failures == 0) std::printf("ok\n");
    return failures == 0 ? 0 : 1;
}
