#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/WinFlatRect.h"

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
    void Blt(IDrawTargetBuffer*, const cRZRect&, const cRZRect&) override {}
    void BltTiled(IDrawTargetBuffer*, const cRZRect&, const cRZRect&, int, int) override {}
    void Flush() override {} bool IsSuspended() override { return false; } void Suspend() override {} void Resume() override {}
    void Draw(eDrawingOp, const float*, int) override {} void DrawNoTransform(eDrawingOp, const float*, int) override {}
    bool Lock(cGZBufferSurfaceDesc&, bool) override { return true; } void Unlock() override {}
};
struct Colors final : IWinColorManager {
    void GetColor(std::uint32_t id, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override {
        r = g = b = id == kWinColorShadow ? 0x20 : 0xe0;
    }
};
struct Probe final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& m) override { got.push_back(m); return true; }
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
};
struct FlatRect final : cGZWinFlatRect {
    using cGZWinFlatRect::cGZWinFlatRect;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
};
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics; Colors colors; Ctx ctx;
    SetWinGraphics(&graphics);
    SetWinColorManager(&colors);
    auto* parent = new Probe(&mgr);
    parent->AddRef();
    parent->SetArea(0, 0, 200, 200);
    auto* rect = new FlatRect(&mgr);
    rect->SetArea(10, 20, 60, 50);
    parent->ChildAdd(rect);
    rect->SetCtx(&ctx);
    rect->CalcAbsoluteArea();

    // mouse messages are forwarded to the parent, shifted by the rectangle's position
    cGZMessage down{kGZMsgMouseDownL, 5, 6, 7};
    CHECK(rect->DoMessage(down) && parent->got.size() == 1 && parent->got[0].param1 == 15 && parent->got[0].param2 == 26);
    cGZMessage cmd{kGZMsgCommand, 5, 6, 7};
    rect->DoMessage(cmd);
    CHECK(parent->got[1].param1 == 5);                       // other messages untouched
    rect->GZOnKeyDown(65, 2);
    CHECK(parent->got.back().type == kGZMsgKeyDown && parent->got.back().param1 == 65 && parent->got.back().param2 == 2);

    // painting: fill, then selected edges
    rect->SetFillColorRGB(0x112233);
    rect->SetOutlineColor(1, 2, 3);
    rect->SetOutlineFlags(kOutlineLeft | kOutlineBottom);
    ctx.log.clear();
    CHECK(rect->GZPaint());
    CHECK(ctx.log.size() == 6 && ctx.log[0] == "color " + std::to_string(0x112233) && ctx.log[1].rfind("fill", 0) == 0);
    CHECK(ctx.log[2] == "color 66051" && ctx.log[3].rfind("line", 0) == 0 && ctx.log[5].rfind("line", 0) == 0);
    rect->SetOutlineFlags(kOutlineNoFill | kOutlineTop);
    ctx.log.clear(); rect->GZPaint();
    CHECK(ctx.log.size() == 2 && ctx.log[1].rfind("line", 0) == 0);

    // bevels
    rect->SetOutlineFlags(kFlatRectRaised);
    std::uint8_t r, g, b;
    rect->GetOutlineColor(0, r, g, b); CHECK(r == 0xe0);       // raised: light top / left
    rect->GetOutlineColor(3, r, g, b); CHECK(r == 0x20);
    CHECK(rect->GetOutlineFlags() == (kFlatRectRaised | 0xf));
    rect->SetOutlineFlags(kFlatRectSunken);
    rect->GetOutlineColor(0, r, g, b); CHECK(r == 0x20);
    std::uint32_t l, t, rr, bb;
    rect->SetOutlineColors(0xff112233, 0xff445566, 0xff778899, 0xffaabbcc);
    rect->GetOutlineColors(l, t, rr, bb);
    CHECK(l == 0xff112233 && bb == 0xffaabbcc);

    // auto size
    rect->SetSizeMode(5);
    CHECK(rect->AutoSize({0, 0, 100, 80}) && rect->GetL() == 0 && rect->GetR() == 100 && rect->GetB() == 50);
    rect->SetSizeMode(4);
    CHECK(rect->AutoSize({0, 0, 100, 80}) && rect->GetB() == 80);
    rect->SetSizeMode(9);
    CHECK(!rect->AutoSize({0, 0, 1, 1}));
    void* out = nullptr;
    CHECK(rect->QueryInterface(kGZIID_cIGZWinFlatRect, &out) && out == static_cast<cIGZWinFlatRect*>(rect));
    static_cast<cIGZUnknown*>(out)->Release();
    parent->Release();
    std::printf(failures ? "FAILED\n" : "gz_winflatrect ok\n");
    return failures != 0;
}
