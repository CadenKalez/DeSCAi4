// The UI draw path end to end on the native Metal driver: cGZDrawContextRenderToHardware (the engine) -> cGZDrawContextRenderToSimGL
// (the hardware half, driver calls only) -> cMetalGDriver, headless, with the result read back from the frame target.
// Solid fills go through the vertex path; a blit goes through cGZBufferSimGLCache (tiles -> driver textures) and the stage-0 texture path.
#import <Foundation/Foundation.h>

#include <cstdio>
#include <vector>

#include "gz/Buffer.h"
#include "gz/DrawContextRenderToHardware.h"
#include "gz/DrawContextRenderToSimGL.h"
#include "render/gd/MetalGDriver.h"

using namespace gd;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

constexpr std::uint32_t W = 64, H = 64;

namespace {

class Services final : public ISimGLDrawServices {
public:
    explicit Services(cIGZGDriver* d) : driver(d) {}
    cIGZGDriver* driver;
    int stateChanged = 0, dirty = 0, registered = 0;
    void RegisterBufferCache(cIGZBufferHardwareCache*) override { ++registered; }
    void UnregisterBufferCache(cIGZBufferHardwareCache*) override { --registered; }
    cIGZGDriver* GetGDriver() override { return driver; }
    void DriverStateChanged() override { ++stateChanged; }
    void ScreenDirty() override { ++dirty; }
    bool TranslucencyAllowed() override { return true; }
    float TranslucencyThreshold() override { return 0.5f; }
    std::uint32_t BufferCacheClock() override { return 1; }
};

cGZBuffer* MakeBuffer(std::uint32_t w, std::uint32_t h) {
    auto* b = new cGZBuffer();
    b->AddRef();
    CHECK(b->Init(w, h, {kBufferColorARGB8888, 32}));
    b->Fill(0, nullptr);
    return b;
}

struct Pixel { std::uint8_t b, g, r, a; };
Pixel At(const std::vector<std::uint8_t>& f, int x, int y) {
    const std::uint8_t* p = &f[(static_cast<std::size_t>(y) * W + x) * 4];
    return {p[0], p[1], p[2], p[3]};
}
bool Is(Pixel p, int r, int g, int b) { return p.r == r && p.g == g && p.b == b; }

}  // namespace

int main() {
    @autoreleasepool {
        auto driver = cMetalGDriver::Create(nullptr, W, H);
        if (!driver) { std::printf("no Metal device\n"); return 0; }
        Services services(driver.get());
        auto* target = MakeBuffer(W, H);
        auto* engine = new cGZDrawContextRenderToHardware();
        engine->AddRef();
        cGZDrawContextRenderToSimGL host(engine, &services);
        engine->SetHardwareRenderHost(&host);
        CHECK(engine->Init(target));

        driver->ClearColor(0, 0, 0, 1);
        driver->Clear(kClearColor | kClearDepth);
        engine->Resume();

        // ---- a solid rectangle: native colour 0, no blending, alpha test off ----
        const std::uint32_t colors[1] = {0xffdd8844};
        engine->SetColorTable(0, 1, colors);
        engine->SetClipRect({0, 0, static_cast<int>(W), static_cast<int>(H)});
        const float rect[4] = {8, 8, 24, 24};
        CHECK(engine->Draw(gz::eDrawingOp::FillRect, rect, 2) == gz::eEngineDrawResult::Done);

        // ---- a blit: a 8x8 source filled with opaque green, drawn scaled up to 16x16 at (32, 8) ----
        auto* source = MakeBuffer(8, 8);
        source->Fill(source->ConvertRGBValueToNative(0, 200, 0) | 0xff000000u, nullptr);
        cRZRect clip{32, 8, 48, 24};
        CHECK(engine->SetBltSource(static_cast<::cIGZBuffer*>(source), 32, 8, 2.0f, 2.0f, clip));
        const float blitRect[4] = {32, 8, 48, 24};
        CHECK(engine->Draw(gz::eDrawingOp::FillRect, blitRect, 2) == gz::eEngineDrawResult::Done);
        engine->ClearBltTiling();

        engine->Suspend();

        std::vector<std::uint8_t> frame;
        std::uint32_t w = 0, h = 0;
        CHECK(driver->ReadBack(frame, w, h) && w == W && h == H);
        if (getenv("UIPATH_DEBUG")) {
            Pixel a = At(frame, 16, 16), b = At(frame, 40, 16);
            std::printf("fill px %02x %02x %02x %02x  blit px %02x %02x %02x %02x  draws=%u\n", a.r, a.g, a.b, a.a, b.r, b.g, b.b, b.a, driver->DrawCalls());
            for (auto& kv : driver->UnsupportedCalls()) std::printf("unsupported %s x%d\n", kv.first.c_str(), kv.second);
        }
        CHECK(Is(At(frame, 0, 0), 0, 0, 0));                    // cleared
        CHECK(Is(At(frame, 16, 16), 0xdd, 0x88, 0x44));         // the fill (ARGB 0xffdd8844)
        CHECK(Is(At(frame, 7, 16), 0, 0, 0) && Is(At(frame, 25, 16), 0, 0, 0));   // and nothing around it
        CHECK(Is(At(frame, 7, 16), 0, 0, 0) && Is(At(frame, 16, 25), 0, 0, 0));
        CHECK(Is(At(frame, 40, 16), 0, 200, 0));                // the textured blit
        CHECK(Is(At(frame, 31, 16), 0, 0, 0) && Is(At(frame, 49, 16), 0, 0, 0));
        CHECK(driver->UnsupportedCalls().empty());
        CHECK(services.stateChanged > 0 && services.dirty > 0);
        CHECK(!host.DriverStateModified() && !host.SomethingDrawn());   // Suspend reported both back

        engine->Release();
        source->Release();
        target->Release();
    }
    if (failures) { std::printf("render_uipath: %d failure(s)\n", failures); return 1; }
    std::printf("render_uipath: all checks passed\n");
    return 0;
}
