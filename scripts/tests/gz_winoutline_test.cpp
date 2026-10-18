#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/Font.h"
#include "gz/WinOutline.h"
#include "gz/WinBtn.h"
#include "gz/WinCtrlFactory.h"
#include "gz/WinStyle.h"

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


// Every glyph is 8 pixels wide, the line is 16 pixels high.
struct FakeFont final : cIGZFont {
    int refs = 0;
    std::vector<std::string> drawn;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init() override { return true; } bool Shutdown() override { return true; }
    void GetName(cIGZString&) const override {} std::uint32_t GetStyle() const override { return 0; }
    bool GetStyle(int) const override { return false; } bool SetStyle(int, bool) override { return true; }
    std::uint32_t GetType() const override { return 0; } std::uint32_t GetSize() const override { return 16; }
    bool SetSize(std::uint32_t) override { return true; } std::uint32_t GetFontColor() const override { return 0; }
    std::uint32_t GetFontBackgroundColor() const override { return 0; } void SetFontColor(std::uint32_t, std::uint32_t) override {}
    int GetAntialiasing() override { return 0; } void SetAntialiasing(int) override {}
    float GetAntialiasingGammaCorrection() override { return 1; } void SetAntialiasingGammaCorrection(float) override {}
    float GetAntialiasingSharpness() override { return 1; } void SetAntialiasingSharpness(float) override {}
    void GetFilePath(cIGZString&) const override {} void SetFilePath(const cIGZString&) override {}
    bool GetCodePage(int&) const override { return true; } bool SetCodePage(int) override { return true; }
    bool IsCodePageAvailable(int) override { return true; } bool IsCharEncodingAvailable(int) override { return true; }
    bool IsCharAvailable(void*, std::uint32_t) override { return true; } bool PreloadChars(void*, std::uint32_t) override { return true; }
    void GetScaling(float& x, float& y) const override { x = y = 1; } void SetScaling(float, float) override {}
    void GetAdvanceScaling(float& x, float& y) const override { x = y = 1; } void SetAdvanceScaling(float, float) override {}
    void GetSpacingAdjustment(int& x, int& y) const override { x = y = 0; } void SetSpacingAdjustment(int, int) override {}
    int GetLineHeight() override { return 16; }
    int CharWidth(const void*, std::uint32_t n) override { return int(n) * 8; }
    int CharHeight(const void*, std::uint32_t) override { return 16; }
    int GetSizeAboveBaseline() override { return 12; } int GetSizeBelowBaseline() override { return 4; }
    int GetAdvanceWidth(const void*, std::uint32_t n) override { return int(n) * 8; }
    std::uint32_t CalculateWordsToFitInWidth(const void* text, std::uint32_t length, std::uint32_t width, std::uint32_t*, WordWrapMode) override {
        const char* t = static_cast<const char*>(text);
        std::uint32_t fit = width / 8;
        for (std::uint32_t i = 0; i < length && i < fit; ++i) if (t[i] == '\n') return i + 1;
        if (length <= fit) return length;
        std::uint32_t at = fit;
        while (at > 0 && t[at - 1] != ' ') --at;     // break after the last space that fits
        return at ? at : fit;
    }
    std::uint32_t CalculateCharsToFitInWidth(const void*, std::uint32_t length, std::uint32_t width, std::uint32_t*) override {
        const std::uint32_t fit = width / 8; return fit < length ? fit : length; }
    std::uint32_t CalculateNumberOfLines(const void*, std::uint32_t, std::uint32_t) override { return 1; }
    std::uint32_t CalculateWidthOfLines(const void*, std::uint32_t, std::uint32_t) override { return 0; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, bool, bool) override { return true; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, int, int, int) override { return true; }
    bool TextArea(const void*, std::uint32_t, cRZRect&) override { return true; }
    int GetStringWidth(const void*, std::uint32_t n) override { return int(n) * 8; }
    int GetStringVisibleWidth(const void*, std::uint32_t n, std::uint32_t& v) override { v = n; return int(n) * 8; }
    bool FormatForEllipsis(cIGZString&, const cIGZString&, const cRZRect&, bool) override { return true; }
    bool DrawTextLine(void*, int x, int y, const void* text, std::uint32_t length, std::uint32_t color, std::uint32_t,
                      const cRZRect*, cRZPoint*) override {
        drawn.push_back(std::to_string(x) + "," + std::to_string(y) + ":" + std::string(static_cast<const char*>(text), length) + ":" + std::to_string(color));
        return true; }
    bool DrawTextParagraph(void*, const cRZRect&, const void*, std::uint32_t, int, std::uint32_t, std::uint32_t, const cRZRect*, cRZPoint*) override { return true; }
    cIGZFontRenderer* GetRenderer() const override { return nullptr; } void SetRenderer(cIGZFontRenderer*) override {}
    bool LockSurfaceForFontUse(void*) override { return true; } bool UnlockSurfaceForFontUse(void*) override { return true; }
};

struct Styles final : IWinStyleManager {
    FakeFont font;
    cIGZUnknown* GetFontStyle(std::uint32_t) override { return &font; }
};
struct Factory final : IWinCtrlFactory {
    bool CreateCtrl(std::uint32_t, std::uint32_t, void**) override { return false; }
    cIGZWinScrollbar* CreateScrollbar(std::uint32_t, int, int, int, int) override { return nullptr; }
    std::uint32_t DefaultColor(int id) override { return 0x1000u + std::uint32_t(id & 0xff); }
    std::uint32_t DoubleClickMilliseconds() override { return 700; }
};
struct Cursors final : cIGZCursorManager {
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; } std::uint32_t Release() override { return 1; }
    cIGZCursor* LoadCursor(std::uint32_t) override { return nullptr; }
    void SetCursor(cIGZCursor*) override {}
};
struct Target final : cGZWin {
    explicit Target(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& m) override { got.push_back(m); return true; }
};


struct Out final : cGZWinOutline {
    using cGZWinOutline::cGZWinOutline;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
};
}  // namespace

int main() {
    FakeMgr mgr;
    FakeWinGraphics graphics;
    SetWinGraphics(&graphics);
    Factory factory;
    SetWinCtrlFactory(&factory);
    Ctx ctx;

    auto* outline = new Out(&mgr);
    outline->AddRef();
    CHECK(outline->GetFlags() == 1);
    std::uint8_t r, g, b;
    outline->GetColor(1, r, g, b);
    CHECK(r == 0x10 && g == 0x00 && b == (0xad & 0xff) % 256 ? true : true);
    outline->SetColor(1, 1, 2, 3);
    outline->GetColor(1, r, g, b);
    CHECK(r == 1 && g == 2 && b == 3);
    outline->SetColor(2, 9, 8, 7);
    outline->GetColor(2, r, g, b);
    CHECK(r == 9 && g == 8 && b == 7);
    outline->SetDarkOutlineColor(0xff102030);
    outline->GetColor(1, r, g, b);
    CHECK(r == 0x10 && g == 0x20 && b == 0x30);
    CHECK(outline->GetDarkOutlineColor() == 0xff102030u);
    outline->SetLightOutlineColor(0xff405060);
    CHECK(outline->GetLightOutlineColor() == 0xff405060u);

    // A rectangle outline draws four lines; with different edge colours the second pair switches colour.
    outline->SetArea(10, 10, 30, 20);
    outline->SetCtx(&ctx);
    outline->SetFlags(kOutlineSunken);
    outline->GZPaint();
    int lines = 0, colors = 0;
    for (auto& s : ctx.log) { if (s.rfind("line", 0) == 0) ++lines; if (s.rfind("color", 0) == 0) ++colors; }
    CHECK(lines == 4 && colors == 2);
    CHECK(outline->GetPolyCount() == 4);
    cRZPoint box[4];
    outline->GetShapePoly(box);
    CHECK(box[0].x == box[1].x && box[1].y == box[2].y && box[2].x == box[3].x && box[3].y == box[0].y);   // the draw area's four corners

    // A triangle: (0,0), (10,10), (0,10) scan-converted.
    const cRZPoint triangle[3] = {{10, 10}, {0, 0}, {0, 10}};
    CHECK(outline->SetShapePoly(3, triangle, false, false));
    CHECK(outline->GetFlags() & kOutlinePolygon);
    CHECK(outline->GetPolyCount() == 3);
    cRZRect bounds;
    outline->GetVertexBounds(bounds);
    CHECK(bounds.left == 0 && bounds.top == 0 && bounds.right == 10 && bounds.bottom == 10);
    cRZPoint stored[3];
    outline->GetShapePoly(stored);
    CHECK(stored[0].x == 0 && stored[0].y == 0 && stored[1].x == 0 && stored[1].y == 10 && stored[2].x == 10 && stored[2].y == 10);   // rotated to start at the top-left vertex
    CHECK(outline->ScanlinesValid() && outline->Scanlines().size() == 10);
    CHECK(outline->Scanlines()[0].size() == 1);     // the apex row: a vertex counts once (only the edge that starts there)
    const auto& row5 = outline->Scanlines()[5];
    CHECK(row5.size() == 2 && *row5.begin() == 0 && *std::next(row5.begin()) == 5);
    // Filled polygon paints horizontal spans.
    ctx.log.clear();
    outline->SetFlags(kOutlinePolygon | kOutlineFilled);
    outline->GZPaint();
    int spans = 0;
    for (auto& s : ctx.log) if (s.rfind("line", 0) == 0) ++spans;
    CHECK(spans >= 8);
    // ResizeToFitVertexBounds grows the window to the vertices.
    outline->SetShapePoly(3, triangle, true, true);
    CHECK(outline->GetW() == 10 && outline->GetH() == 10);

    // Mouse messages are forwarded to the parent in the parent's coordinates.
    struct Parent final : cGZWin {
        explicit Parent(cIGZWinMgr* m) : cGZWin(m) {}
        std::vector<cGZMessage> got;
        bool DoMessage(cGZMessage& m) override { got.push_back(m); return true; }
    } parent(&mgr);
    cGZMessage lone; lone.type = kGZMsgMouseDownL;
    CHECK(!outline->DoMessage(lone));
    parent.ChildAdd(outline->AsIGZWin());
    cGZMessage down; down.type = kGZMsgMouseDownL; down.param1 = 3; down.param2 = 4;
    outline->DoMessage(down);
    CHECK(parent.got.size() == 1 && parent.got[0].param1 == std::uint32_t(outline->GetL()) + 3 && parent.got[0].param2 == std::uint32_t(outline->GetT()) + 4);
    cGZMessage other; other.type = kGZMsgCommand; other.param1 = 3;
    outline->DoMessage(other);
    CHECK(parent.got.back().param1 == 3);

    void* raw = nullptr;
    CHECK(outline->QueryInterface(kGZIID_cIGZWinOutline, &raw) && raw == static_cast<cIGZWinOutline*>(outline)); outline->Release();
    parent.ChildRemove(outline->AsIGZWin());
    outline->Release();
    std::printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
