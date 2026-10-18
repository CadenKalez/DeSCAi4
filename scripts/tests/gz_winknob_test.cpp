#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/WinKnob.h"

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

struct Style final : cIGZUnknown {
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
struct Styles final : IWinStyleManager {
    Style defaultStyle, other;
    cIGZUnknown* GetFontStyle(std::uint32_t id) override {
        if (id == kKnobDefaultFontStyle) return &defaultStyle;
        if (id == 0x1234) return &other;
        return nullptr;
    }
};
struct Knob final : cGZWinKnob {
    using cGZWinKnob::cGZWinKnob;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
};
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics; Ctx ctx; Styles styles;
    SetWinGraphics(&graphics);
    SetWinStyleManager(&styles);
    auto* knob = new Knob(&mgr);
    knob->AddRef();
    knob->SetArea(0, 0, 32, 32);
    knob->SetCtx(&ctx);
    CHECK(knob->GetFlag(kWinIgnoreCursor));
    CHECK(knob->Init());
    knob->SetCtx(&ctx);                                    // Init re-binds the draw context
    CHECK(!knob->GetFlag(kWinPrivateBuffer) && knob->GetFlag(kWinFlag4));
    CHECK(styles.defaultStyle.refs == 1 && knob->GetFontStyle() == kKnobDefaultFontStyle);

    // a 40x10 strip of 5 frames
    auto* strip = new cGZBuffer();
    strip->AddRef();
    strip->Init(40, 10, {kBufferColorARGB8888, 32});
    knob->SetKnobImage(strip);
    CHECK(knob->GetKnobImage() == strip);
    std::uint32_t key = 0;
    CHECK(strip->GetTransparentColor(key));
    knob->SetFrameCount(5);
    CHECK(knob->GetFrameCount() == 5 && knob->GetMinValue() == 0.0f && knob->GetMaxValue() == 1.0f);
    CHECK(knob->CurrentFrame() == 1);                       // value 0: first animated frame
    knob->SetValue(0.5f);
    CHECK(knob->GetValue() == 0.5f && knob->CurrentFrame() == 3);
    knob->SetValue(5.0f);
    CHECK(knob->GetValue() == 1.0f && knob->CurrentFrame() == 4);
    knob->SetValue(-5.0f);
    CHECK(knob->GetValue() == 0.0f);
    knob->SetMaxValue(10.0f);
    knob->SetValue(5.0f);
    CHECK(knob->CurrentFrame() == 3);
    knob->SetMinValue(6.0f);                                // value follows the new minimum
    CHECK(knob->GetValue() == 6.0f && knob->CurrentFrame() == 1);
    knob->SetMaxValue(4.0f);                                // inverted range: frame 0
    CHECK(knob->CurrentFrame() == 0);
    knob->SetMaxValue(10.0f);
    knob->SetValue(10.0f);
    CHECK(knob->CurrentFrame() == 4);

    // paint blits the current frame
    ctx.log.clear();
    CHECK(knob->GZPaint());
    CHECK(ctx.log.size() == 1 && ctx.log[0] == "blt 32,0,40,10>0,0,8,10");
    knob->SetFlag(kWinEnabled, false);
    ctx.log.clear();
    knob->GZPaint();
    CHECK(ctx.log[0] == "blt 0,0,8,10>0,0,8,10");          // disabled shows frame 0

    // style
    knob->SetFontStyle(0xdead);                             // unknown id: unchanged
    CHECK(knob->GetFontStyle() == kKnobDefaultFontStyle);
    knob->SetFontStyle(0x1234);
    CHECK(knob->GetFontStyle() == 0x1234 && styles.other.refs == 1 && styles.defaultStyle.refs == 0);
    knob->SetKnobFlag(4, true);
    CHECK(knob->GetKnobFlag(4) && !knob->GetKnobFlag(2));
    void* out = nullptr;
    CHECK(knob->QueryInterface(kGZIID_cIGZWinKnob, &out) && out == static_cast<cIGZWinKnob*>(knob));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(knob->Shutdown() && knob->GetKnobImage() == nullptr && styles.other.refs == 0);
    knob->Release();
    strip->Release();
    std::printf(failures ? "FAILED\n" : "gz_winknob ok\n");
    return failures != 0;
}
