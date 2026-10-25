// The window tree on the native Metal driver: native::Graphics (hardware engine for the screen buffer), native::WinMgr and flat
// rectangle windows (recovered cGZWin / cGZWinMgrBase / cGZWinFlatRect), headless, read back from the frame target.
#import <Foundation/Foundation.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "app/NativeGraphics.h"
#include "app/NativeWinMgr.h"
#include "gz/WinFlatRect.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

constexpr int W = 256, H = 192;

namespace {
// FINDING (carried from the early host): with the draw system active cGZWin::SetAreaToDrawTo leaves drawArea_.bottom unrebased,
// so a window whose top is not 0 paints down to the bottom of its parent. Repaired here, not in the recovered class.
class Rect : public gz::cGZWinFlatRect {
public:
    using gz::cGZWinFlatRect::cGZWinFlatRect;
    void SetAreaToDrawTo() override {
        gz::cGZWinFlatRect::SetAreaToDrawTo();
        const int h = area_.bottom - area_.top;
        drawArea_.bottom = drawArea_.top + h;
        accumulatedArea_.bottom = accumulatedArea_.top + h;
    }
};
Rect* Add(native::WinMgr& mgr, cIGZWin* parent, int l, int t, int r, int b, std::uint32_t rgb) {
    auto* w = new Rect(&mgr);
    w->Init();
    w->SetArea(l, t, r, b);
    w->SetFillColor(std::uint8_t(rgb >> 16), std::uint8_t(rgb >> 8), std::uint8_t(rgb));
    parent->ChildAdd(w);
    return w;
}
struct Px { std::uint8_t b, g, r, a; };
Px At(const std::vector<std::uint8_t>& f, int x, int y) { const std::uint8_t* p = &f[(std::size_t(y) * W + x) * 4]; return {p[0], p[1], p[2], p[3]}; }
bool Is(Px p, int r, int g, int b) { return p.r == r && p.g == g && p.b == b; }
}  // namespace

int main() {
    @autoreleasepool {
        auto driver = gd::cMetalGDriver::Create(nullptr, W, H);
        if (!driver) { std::printf("no Metal device\n"); return 0; }
        native::Graphics graphics(driver.get(), W, H);
        native::WinMgr mgr;
        mgr.AddRef();
        mgr.Init();
        cIGZWin* desktop = mgr.GetMainWindow();
        desktop->SetArea(0, 0, W, H);
        Add(mgr, desktop, 0, 0, W, H, 0x102030);                       // backdrop
        auto* panel = Add(mgr, desktop, 32, 24, 224, 168, 0x2a3f5f);
        Add(mgr, panel, 16, 16, 96, 64, 0xc03030);                     // a child of the panel (offset by the panel's origin)
        auto* outlined = Add(mgr, desktop, 120, 40, 200, 100, 0x30a030);
        outlined->SetOutlineFlags(0xf);
        outlined->SetOutlineColor(0xff, 0xff, 0x00);
        desktop->SetBufferToDrawToRecursive();
        desktop->InvalidateSelf();

        driver->ClearColor(0, 0, 0, 1);
        driver->Clear(gd::kClearColor | gd::kClearDepth);
        mgr.Pump();
        mgr.Plot();
        graphics.Services().Tick();

        std::vector<std::uint8_t> frame;
        std::uint32_t w = 0, h = 0;
        CHECK(driver->ReadBack(frame, w, h) && w == W && h == H);
        if (getenv("NATIVEUI_DEBUG")) {
            for (auto pt : {std::pair{4, 4}, {40, 30}, {60, 50}, {100, 60}, {130, 60}, {120, 60}, {199, 60}})
                std::printf("px(%d,%d) = %02x %02x %02x\n", pt.first, pt.second, At(frame, pt.first, pt.second).r, At(frame, pt.first, pt.second).g, At(frame, pt.first, pt.second).b);
            std::printf("draws=%u\n", driver->DrawCalls());
            for (auto& kv : driver->UnsupportedCalls()) std::printf("unsupported %s x%d\n", kv.first.c_str(), kv.second);
        }
        CHECK(Is(At(frame, 4, 4), 0x10, 0x20, 0x30));        // backdrop
        CHECK(Is(At(frame, 40, 30), 0x2a, 0x3f, 0x5f));      // panel
        CHECK(Is(At(frame, 60, 50), 0xc0, 0x30, 0x30));      // the panel's child at (32+16, 24+16)
        CHECK(Is(At(frame, 160, 70), 0x30, 0xa0, 0x30));     // the outlined rectangle's fill
        CHECK(Is(At(frame, 120, 70), 0xff, 0xff, 0x00));     // its left outline
        desktop->ChildDeleteAll();   // the manager must outlive its windows (its destructor tears the hash tables down first)
    }
    if (failures) { std::printf("render_nativeui: %d failure(s)\n", failures); return 1; }
    std::printf("render_nativeui: all checks passed\n");
    return 0;
}
