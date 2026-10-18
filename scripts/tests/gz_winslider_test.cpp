#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/WinSlider.h"

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
struct Slider final : cGZWinSlider {
    using cGZWinSlider::cGZWinSlider;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
};
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics; Ctx ctx;
    SetWinGraphics(&graphics);
    auto* parent = new Probe(&mgr);
    parent->AddRef();
    parent->SetArea(0, 0, 300, 300);
    auto* slider = new Slider(&mgr);
    parent->ChildAdd(slider);
    CHECK(slider->GetW() == 0x20 && slider->GetH() == 0x60);      // default size, vertical
    tSliderWindowDirection direction;
    slider->GetDirection(direction);
    CHECK(direction == kSliderVertical && slider->GetMinimumValue() == 0 && slider->GetMaximumValue() == 0x20);

    // vertical: track runs bottom (value 0) to top
    CHECK(slider->GetThumbWidth() == 0x20 && slider->GetThumbHeight() == 0x20);
    slider->CalculateAllMetrics();
    CHECK(slider->ThumbRect().top == 0x60 - 0x20 && slider->ThumbRect().left == 0 && slider->ThumbRect().right == 0x20);
    CHECK(slider->SetValue(16));
    CHECK(slider->GetValue() == 16 && slider->ThumbRect().top == 0x20);
    CHECK(parent->got.size() == 1 && parent->got[0].type == kGZMsgCommand && parent->got[0].param1 == kGZSliderValueChanged);
    CHECK(!slider->SetValue(16));                                    // unchanged: no message
    CHECK(slider->SetValue(99) && slider->GetValue() == 0x20 && slider->ThumbRect().top == 0);
    CHECK(slider->SetValue(-5) && slider->GetValue() == 0);

    // horizontal, with a margin-free track
    slider->SetDirection(kSliderHorizontal);
    slider->SetArea(0, 0, 100, 10);
    CHECK(slider->GetThumbWidth() == 10 && slider->ThumbRect().left == 0);
    slider->SetMaximumValue(100);
    slider->SetMinimumValue(0);
    CHECK(slider->SetValue(50));
    CHECK(slider->ThumbRect().left == 45 && slider->ThumbRect().right == 55);
    CHECK(slider->SetPercentage(1.0f) && slider->GetValue() == 100 && slider->ThumbRect().left == 90);
    CHECK(slider->SetPercentage(7.0f) && slider->GetValue() == 100);   // differs from the stored 1.0: clamped, re-announced
    float fraction = 0.0f;
    CHECK(slider->ConvertCursorPositionToPercentage(fraction, 50, 5) && fraction == 0.5f);
    CHECK(slider->ConvertCursorPositionToPercentage(fraction, -20, 5) && fraction == 0.0f);
    CHECK(slider->ConvertCursorPositionToPercentage(fraction, 500, 5) && fraction == 1.0f);

    // mouse: down starts a drag, move follows, up reports
    parent->got.clear();
    slider->GZOnMouseDownL(5, 5, 0);
    CHECK(slider->GetValue() == 0 || slider->GetValue() == 1 || slider->GetValue() == 0);
    slider->GZOnMouseMove(60, 5, 0);
    CHECK(slider->GetValue() > 50);
    parent->got.clear();
    CHECK(slider->GZOnMouseUpL(25, 5, 0));
    CHECK(parent->got.size() == 2 && parent->got[1].param1 == kGZSliderReleased);
    CHECK(!slider->GZOnMouseMove(30, 5, 0));                         // no longer capturing

    // flat paint: fill, a line along the track, the thumb
    slider->SetCtx(&ctx);
    slider->SetFillColorRGB(0x102030);
    ctx.log.clear();
    CHECK(slider->GZPaint());
    CHECK(ctx.log.size() == 5 && ctx.log[0] == "color " + std::to_string(0x102030) && ctx.log[1].rfind("fill", 0) == 0 &&
          ctx.log[2] == "color " + std::to_string(~0x102030u) && ctx.log[3].rfind("line", 0) == 0 &&
          ctx.log[4].rfind("fill", 0) == 0);

    // image: four 8-wide segments, the window becomes as tall as the segment
    auto* image = new cGZBuffer();
    image->AddRef();
    image->Init(32, 6, {kBufferColorARGB8888, 32});
    CHECK(slider->SetImage(image, nullptr));
    cIGZBuffer* got = nullptr;
    cRZRect segment;
    CHECK(slider->GetImage(got, segment) && got == image && segment.right == 8 && segment.bottom == 6);
    CHECK(slider->GetH() == 6);
    ctx.log.clear();
    slider->GZPaint();
    CHECK(ctx.log.size() >= 4 && ctx.log[0].rfind("blt", 0) == 0);
    slider->SetImage(nullptr, nullptr);
    slider->GetImage(got, segment);
    CHECK(got == nullptr && segment.right == 0x10 && segment.bottom == 0x10);
    void* out = nullptr;
    CHECK(slider->QueryInterface(kGZIID_cIGZWinSlider, &out) && out == static_cast<cIGZWinSlider*>(slider));
    static_cast<cIGZUnknown*>(out)->Release();
    image->Release();
    parent->Release();
    std::printf(failures ? "FAILED\n" : "gz_winslider ok\n");
    return failures != 0;
}
