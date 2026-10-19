// cGZScreenBuffer: initialisation, interface lookup, window rectangles, fills through the driver, blits and swaps.
#include <cstdio>
#include <vector>

#include "gz/ScreenBuffer.h"
#include "render/gd/GDriverBase.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Driver final : gd::cGDriverBase {
    std::vector<std::string> log; float r = 0, g = 0, b = 0; std::uint32_t vp[4] = {0, 0, 640, 480};
    void Clear(std::uint32_t mask) override { log.push_back("clear " + std::to_string(mask)); }
    void ClearColor(float cr, float cg, float cb, float) override { r = cr; g = cg; b = cb; }
    void SetViewport(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) override {
        log.push_back("viewport " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," + std::to_string(h));
        vp[0] = x; vp[1] = y; vp[2] = x + w; vp[3] = y + h;
    }
    void GetViewport(std::uint32_t out[4]) override { for (int i = 0; i < 4; ++i) out[i] = vp[i]; }
};
struct Graphics final : IGraphicSystemControl {
    Driver driver; int renders = 0, flushes = 0;
    DisplayMetrics GetScreenMetrics() override { return {640, 480, 32}; }
    void PreInitSetDesiredGameResolution(const DisplayMetrics&) override {}
    void PreInitSetWindowedMode(bool) override {}
    void DisableScreenBuffer() override {}
    void EnableCursorEmulation(bool) override {}
    void EnablePageFlipping(bool) override {}
    bool IsPageFlippingEnabled() override { return false; }
    void SetDefaultDriverClassID(std::uint32_t) override {}
    bool CanvasReady() override { return true; }
    DisplayMetrics GetGameMetrics() override { return {640, 480, 32}; }
    gd::cIGZGDriver* GetGDriver() override { return &driver; }
    int GetGDriverCount() override { return 1; }
    gd::cIGZGDriver* GetGDriverByIndex(int) override { return &driver; }
    void SetGDriver(gd::cIGZGDriver*) override {}
    void SetForceNoTranslucency(bool, float) override {}
    cIGZBuffer* CreateBuffer() override { return nullptr; }
    void RenderBufferToScreen(cIGZBuffer*, const cRZRect&, const cRZRect&) override { ++renders; }
    void LazyFlush() override { ++flushes; }
    cRZRect CanvasRect() override { return cRZRect{0, 0, 640, 480}; }
    bool IsFullScreenMode() override { return false; }
    std::uint32_t GetScreenPixelFormatBits() override { return 32; }
};
}  // namespace

int main() {
    Graphics graphics;
    auto* screen = new cGZScreenBuffer(); screen->AddRef();
    screen->SetGraphicSystem(&graphics);
    CHECK(!screen->IsInitialized() && !screen->IsReady() && !screen->Swap());
    cGZBufferColorType type{};
    type.bitsPerPixel = 32;
    CHECK(!screen->Init(0, 480, false, type));
    CHECK(screen->Init(640, 480, false, type) && screen->IsInitialized() && screen->IsReady());
    CHECK(screen->Init(10, 10, false, type));                                 // already initialised: answers true, changes nothing
    CHECK(screen->GetBufferWidthInPixels() == 640 && screen->GetBufferHeightInPixels() == 480);
    cRZRect area;
    screen->GetBufferArea(area); CHECK(area.right == 640 && area.bottom == 480);

    void* q = nullptr;
    CHECK(screen->QueryInterface(0xe3094dc8, &q) && q == static_cast<cIGZScreenBuffer*>(screen));
    CHECK(screen->QueryInterface(0x20732180, &q) && screen->QueryInterface(0x86d72b57, &q) && !screen->QueryInterface(5, &q));
    for (int i = 0; i < 3; ++i) screen->Release();

    // the application window moves: rectangles and points are offset by it
    cRZRect window{100, 50, 740, 530};
    CHECK(screen->ModifyAppWindow(window) && screen->GetBufferWidthInPixels() == 640);
    cRZRect r{1, 2, 3, 4}; screen->AdjustClientRect(r); CHECK(r.left == 101 && r.top == 52 && r.right == 103 && r.bottom == 54);
    cRZPoint p{5, 6}; screen->AdjustClientPoint(p); CHECK(p.x == 105 && p.y == 56);

    // fills clear through the driver with the viewport set to the rectangle (bottom-left origin) and then restored
    graphics.driver.log.clear();
    CHECK(screen->Fill(0xff0000, 10, 20, 110, 70, nullptr));
    CHECK(graphics.driver.log.size() == 3 && graphics.driver.log[0] == "viewport 10,410,100,50" && graphics.driver.log[1] == "clear 16384");
    CHECK(graphics.driver.log[2] == "viewport 0,0,640,480");

    // blits and swaps present through the graphic system
    CHECK(screen->Swap() && graphics.renders == 1 && graphics.flushes == 1);
    cRZRect src{0, 0, 10, 10}, dst{0, 0, 10, 10}, empty{5, 5, 5, 9};
    CHECK(screen->Blt(screen, src, dst, nullptr) && graphics.renders == 2);
    CHECK(screen->Blt(screen, src, empty, nullptr) && graphics.renders == 2);   // empty destination: nothing drawn

    // locks: undefined flag bits are refused, a write lock bumps the change counter
    auto before = screen->GetChangeCounter();
    CHECK(!screen->Lock(static_cast<cIGZBuffer::eLockFlags>(0x20)));
    CHECK(screen->Lock(cIGZBuffer::kLockWrite) && screen->IsLocked() && screen->GetChangeCounter() > before);
    CHECK(screen->Unlock(cIGZBuffer::kLockWrite) && !screen->IsLocked());

    CHECK(screen->Uninitialize() && !screen->IsReady());
    CHECK(screen->Shutdown() && !screen->IsInitialized());
    screen->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
