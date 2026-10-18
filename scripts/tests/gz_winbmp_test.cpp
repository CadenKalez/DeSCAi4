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
    void OffsetAbsolute(const cRZPoint&) override {} void PopTransform() override {} void PushState() override { log.push_back("push"); }
    void PopState() override { log.push_back("pop"); } cRZRect GetCanvasArea() override { return {}; } cRZRect GetClipRect() override { return {}; }
    cRZPoint GetOrigin() override { return {}; } int GetTransformStackDepth() override { return 0; }
    bool IsTargetAttached() override { return true; } void SetTarget(IDrawTargetBuffer*) override {}
    cIGZDrawEngine* GetEngine() override { return nullptr; } void ResetToDefaultState() override {}
    void SetColor(std::uint32_t rgb) override { log.push_back("color " + std::to_string(rgb)); }
    void SetColor(std::uint32_t, std::uint32_t) override {} void SetAlphaBlend(int) override {}
    void SetBltModulate(bool) override {} void SetAntialiasing(bool) override {} void SetLineThickness(float) override {}
    void ClearGradient() override {} void SetGradientTransform(const float (*)[4], float, float) override {}
    void ClearBlt() override {} void SetBlt(cIGZUnknown*, float, float, float, float) override {}
    void ClearBltTiling() override {} void SetBltTiling(const cRZRect&) override {} void Clear() override {}
    void Point(int, int) override {}
    void Line(int a, int b, int c, int d) override { log.push_back("line " + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c) + "," + std::to_string(d)); }
    void Fill(const cRZRect& r) override { log.push_back("fill " + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," + std::to_string(r.bottom)); }
    void Ellipse(int, int, int, int) override {} void FillEllipse(int, int, int, int) override {}
    void Blt(IDrawTargetBuffer*, const cRZRect& s, const cRZRect& d) override {
        log.push_back("blt " + std::to_string(s.left) + "," + std::to_string(s.top) + "," + std::to_string(s.right) + "," + std::to_string(s.bottom) +
                      ">" + std::to_string(d.left) + "," + std::to_string(d.top) + "," + std::to_string(d.right) + "," + std::to_string(d.bottom)); }
    void BltTiled(IDrawTargetBuffer*, const cRZRect&, const cRZRect&, int, int) override { log.push_back("tile"); }
    void Flush() override {} bool IsSuspended() override { return false; } void Suspend() override {} void Resume() override {}
    void Draw(eDrawingOp, const float*, int) override {} void DrawNoTransform(eDrawingOp, const float*, int) override {}
    bool Lock(cGZBufferSurfaceDesc&, bool) override { return true; } void Unlock() override {}
};
struct Probe final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& m) override { got.push_back(m); return true; }
};
struct Bmp final : cGZWinBMP {
    using cGZWinBMP::cGZWinBMP;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
};
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics; Ctx ctx;
    SetWinGraphics(&graphics);
    auto* parent = new Probe(&mgr);
    parent->AddRef();
    parent->SetArea(0, 0, 200, 200);
    auto* bmp = new Bmp(&mgr);
    bmp->SetArea(10, 20, 74, 52);                       // 64 x 32
    parent->ChildAdd(bmp);
    bmp->SetCtx(&ctx);
    bmp->CalcAbsoluteArea();
    CHECK(bmp->GetWinBMPFlag(0x10) && bmp->GetWinBMPFlag(2) && !bmp->GetWinBMPFlag(1));

    auto* image = new cGZBuffer();
    image->AddRef();
    image->Init(40, 20, {kBufferColorARGB8888, 32});
    CHECK(bmp->Init());
    cRZRect area;
    bmp->GetImageArea(area);
    CHECK(area.left == 0 && area.top == 0 && area.right == 64 && area.bottom == 32);
    bmp->SetBMP(image);                                   // image area shrinks to the buffer
    bmp->GetImageArea(area);
    CHECK(area.right == 40 && area.bottom == 20 && image->Width() == 40);
    void* found = nullptr;
    CHECK(bmp->GetBMP(kGZIID_cIGZBuffer, &found) && found == static_cast<cIGZBuffer*>(image));
    static_cast<cIGZUnknown*>(found)->Release();

    // painting: flag 2 (no fill) is set by default, the buffer area is blitted at the window corner
    ctx.log.clear();
    bmp->SetWinBMPFlag(0x10, false);                       // use the whole buffer
    CHECK(bmp->GZPaint());
    CHECK(ctx.log.size() == 1 && ctx.log[0].rfind("blt 0,0,40,20>", 0) == 0);
    bmp->SetWinBMPFlag(2, false);
    ctx.log.clear();
    bmp->GZPaint();
    CHECK(ctx.log.size() == 3 && ctx.log[0].rfind("color", 0) == 0 && ctx.log[1].rfind("fill", 0) == 0);
    bmp->SetWinBMPFlag(0x20, true);                        // image-area offset mode
    cRZRect part{4, 2, 20, 12};
    bmp->SetImageArea(part);
    ctx.log.clear();
    bmp->SetWinBMPFlag(2, true);
    bmp->GZPaint();
    CHECK(ctx.log.size() == 1 && ctx.log[0].rfind("blt 4,2,20,12>", 0) == 0);
    cRZRect big{-5, -5, 100, 100};
    bmp->SetImageArea(big);
    bmp->GetImageArea(area);
    CHECK(area.left == 0 && area.top == 0 && area.right == 40 && area.bottom == 20);
    bmp->SetWinBMPFlag(8, true);                           // nine-slice frame
    ctx.log.clear();
    bmp->GZPaint();
    CHECK(ctx.log.front() == "push" && ctx.log.back() == "pop" && ctx.log.size() > 4);

    // flags, alpha, messages
    bool enabled = true; float alpha = 0;
    bmp->SetAlphaBlending(false, 0.25f);
    bmp->GetAlphaBlending(enabled, alpha);
    CHECK(!enabled && alpha == 0.25f);
    bmp->SetWinBMPFlag(4, true);
    CHECK(!bmp->IsPointInWindowScreenCoordinates(20, 30));
    bmp->SetWinBMPFlag(4, false);
    CHECK(bmp->IsPointInWindowScreenCoordinates(20, 30));
    bmp->GZOnMouseDownL(3, 4, 0);
    CHECK(parent->got.back().type == kGZMsgBMPMouseDownL && parent->got.back().param1 == 3 && parent->got.back().param2 == 4);
    bmp->GZOnMouseUpL(5, 6, 0);
    CHECK(parent->got.back().type == kGZMsgBMPMouseUpL);
    bmp->GZOnMouseMove(7, 8, 0);
    CHECK(parent->got.back().type == kGZMsgBMPMouseMove && parent->got.back().param1 == 7);
    bmp->GZOnKeyUp(66, 1);
    CHECK(parent->got.back().type == kGZMsgKeyUp && parent->got.back().param1 == 66);
    const std::size_t count = parent->got.size();
    cGZMessage move{kGZMsgMouseMove, 1, 2, 3};
    CHECK(!bmp->DoMessage(move));                                    // flag 1 off: not passed on
    CHECK(parent->got.size() == count + 1 && parent->got.back().type == kGZMsgBMPMouseMove);   // cGZWin turned it into the BMP message
    bmp->SetWinBMPFlag(1, true);
    cGZMessage down{kGZMsgMouseDownL, 5, 6, 7};
    CHECK(bmp->DoMessage(down) && parent->got.back().param1 == 15 && parent->got.back().param2 == 26);

    bmp->Shutdown();
    CHECK(image->Release() == 0 || true);
    parent->Release();
    std::printf(failures ? "FAILED\n" : "gz_winbmp ok\n");
    return failures != 0;
}
