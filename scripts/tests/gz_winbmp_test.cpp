#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/WinBMP.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
struct Ctx final : cIGZDrawContext {
    std::vector<std::string> log;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    void PushTransform() override {} void ClipReset() override {} void Clip(const cRZRect&) override {}
    void ClipAbsolute(const cRZRect&) override {} void Offset(const cRZPoint&) override {}
    void OffsetAbsolute(const cRZPoint&) override {} void PopTransform() override {} void PushState() override {}
    void PopState() override {} cRZRect GetCanvasArea() override { return {}; } cRZRect GetClipRect() override { return {}; }
    cRZPoint GetOrigin() override { return {}; } int GetTransformStackDepth() override { return 0; }
    bool IsTargetAttached() override { return true; } void SetTarget(IDrawTargetBuffer*) override {}
    cIGZDrawEngine* GetEngine() override { return nullptr; } void ResetToDefaultState() override {}
    void SetColor(std::uint32_t) override { log.push_back("color"); }
    void SetColor(std::uint32_t, std::uint32_t) override {} void SetAlphaBlend(int) override {}
    void SetBltModulate(bool) override {} void SetAntialiasing(bool) override {} void SetLineThickness(float) override {}
    void ClearGradient() override {} void SetGradientTransform(const float (*)[4], float, float) override {}
    void ClearBlt() override {} void SetBlt(cIGZUnknown*, float, float, float, float) override {}
    void ClearBltTiling() override {} void SetBltTiling(const cRZRect&) override {} void Clear() override {}
    void Point(int, int) override {} void Line(int, int, int, int) override {}
    void Fill(const cRZRect&) override { log.push_back("fill"); }
    void Ellipse(int, int, int, int) override {} void FillEllipse(int, int, int, int) override {}
    void Blt(IDrawTargetBuffer*, const cRZRect& s, const cRZRect& d) override {
        log.push_back("blt " + std::to_string(s.right) + "," + std::to_string(s.bottom) + "->" + std::to_string(d.left) + "," + std::to_string(d.top) + "," + std::to_string(d.right) + "," + std::to_string(d.bottom));
    }
    void BltTiled(IDrawTargetBuffer*, const cRZRect&, const cRZRect&, int, int) override {}
    void Flush() override {} bool IsSuspended() override { return false; } void Suspend() override {} void Resume() override {}
    void Draw(eDrawingOp, const float*, int) override {} void DrawNoTransform(eDrawingOp, const float*, int) override {}
    void Lock(cGZBufferSurfaceDesc&, bool) override {} void Unlock() override {}
};
struct Bitmap final : IWinBitmap {
    int refs = 0; cRZRect area{0, 0, 30, 20};
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    const cRZRect* GetBufferArea() const override { return &area; }
    std::uint32_t ConvertRGBValueToNative(std::uint8_t, std::uint8_t, std::uint8_t) override { return 0; }
    int Width() const override { return area.right; }
    int Height() const override { return area.bottom; }
};
struct Sink final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& m) override { got.push_back(m); return true; }
    void SendMsg(cIGZWin*, std::uint32_t t, std::uint64_t a, std::uint64_t b, std::uint64_t c) override { got.push_back({t, a, b, c}); }
};
struct Win final : cGZWinBMP {
    using cGZWinBMP::cGZWinBMP;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
};
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics; Ctx ctx; Bitmap bmp;
    SetWinGraphics(&graphics);
    auto* parent = new Sink(&mgr);
    parent->AddRef();
    auto* w = new Win(&mgr);
    w->SetID(7);
    w->SetArea(10, 10, 50, 40);               // 40 x 30
    w->Init();                                // before ChildAdd marks it initialised
    parent->ChildAdd(w);
    w->SetCtx(&ctx);
    w->CalcAbsoluteArea();
    cRZRect area;
    w->GetImageArea(area);
    CHECK(area.right == 40 && area.bottom == 30);
    w->SetBMP(&bmp);
    w->GetImageArea(area);
    CHECK(bmp.refs == 1 && area.right == 30 && area.bottom == 20);       // clamped to the bitmap
    w->SetImageArea({-5, -5, 100, 100});
    w->GetImageArea(area);
    CHECK(area.left == 0 && area.top == 0 && area.right == 30 && area.bottom == 20);

    ctx.log.clear();
    w->GZPaint();
    CHECK(ctx.log.size() == 1 && ctx.log[0].rfind("blt 30,20->", 0) == 0);   // default flags 0x12: no fill
    w->SetWinBMPFlag(kWinBMPNoFill, false);
    ctx.log.clear(); w->GZPaint();
    CHECK(ctx.log.size() == 3);

    // notifications
    w->GZOnMouseDownL(3, 4, 0);
    CHECK(parent->got.back().type == kWinMsgMouseDownL && parent->got.back().param1 == 3 && parent->got.back().param3 == 7);
    w->GZOnKeyUp(9, 1);
    CHECK(parent->got.back().type == kGZMsgKeyUp);

    // pass-through
    parent->got.clear();
    cGZMessage cmd{kGZMsgCommand, 1, 2, 3};
    CHECK(!w->DoMessage(cmd));                                            // flag off: nothing forwarded
    w->SetWinBMPFlag(kWinBMPPassThrough, true);
    cGZMessage move{kGZMsgMouseMove, 1, 2, 3};
    CHECK(w->DoMessage(move) && parent->got.back().param1 == 11 && parent->got.back().param2 == 12);
    w->SetWinBMPFlag(kWinBMPNoHit, true);
    CHECK(!w->IsPointInWindowScreenCoordinates(20, 20));
    w->SetAlphaBlending(true, 0.25f);
    bool on; float amt;
    w->GetAlphaBlending(on, amt);
    CHECK(on && amt == 0.25f);
    w->Shutdown();
    CHECK(bmp.refs == 0);
    parent->Release();
    std::printf(failures ? "FAILED\n" : "gz_winbmp ok\n");
    return failures != 0;
}
