// cGZWinIMEComposition test: construction, the mirrored focus text edit, composition messages, the underline paint
// path, the colour / flag / caret quirks and shutdown.
#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/DrawContext.h"
#include "gz/WinIMEServer.h"
#include "gz/WinCtrlFactory.h"
#include "gz/WinIMEComposition.h"
#include "gz/WinStyle.h"
#include "rz/cRZString.h"
#include "rz/cRZSysServPtr.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace gz;

namespace {

struct Ctx final : cIGZDrawContext {
    std::vector<std::string> log;
    std::vector<cRZRect> fills;
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
    void Line(int x0, int y0, int x1, int y1) override {
        log.push_back("line " + std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1));
    }
    void Fill(const cRZRect& r) override { fills.push_back(r); log.push_back("fill"); }
    void Ellipse(int, int, int, int) override {} void FillEllipse(int, int, int, int) override {}
    void Blt(IDrawTargetBuffer*, const cRZRect&, const cRZRect&) override {}
    void BltTiled(IDrawTargetBuffer*, const cRZRect&, const cRZRect&, int, int) override {}
    void Flush() override {} bool IsSuspended() override { return false; } void Suspend() override {} void Resume() override {}
    void Draw(eDrawingOp, const float*, int) override {} void DrawNoTransform(eDrawingOp, const float*, int) override {}
    bool Lock(cGZBufferSurfaceDesc&, bool) override { return true; } void Unlock() override {}
    std::size_t Lines() const {
        std::size_t n = 0;
        for (const std::string& s : log) if (s.rfind("line ", 0) == 0) ++n;
        return n;
    }
};

// 8 pixels per character, 16-pixel lines.
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
    int CharWidth(const void*, std::uint32_t n) override { return static_cast<int>(n) * 8; }
    int CharHeight(const void*, std::uint32_t) override { return 16; }
    int GetSizeAboveBaseline() override { return 12; } int GetSizeBelowBaseline() override { return 4; }
    int GetAdvanceWidth(const void*, std::uint32_t n) override { return static_cast<int>(n) * 8; }
    std::uint32_t CalculateWordsToFitInWidth(const void*, std::uint32_t length, std::uint32_t width, std::uint32_t*, WordWrapMode) override {
        const std::uint32_t fit = width / 8;
        if (fit == 0) return 1;   // the text edit loops forever on a zero answer
        return fit < length ? fit : length;
    }
    std::uint32_t CalculateCharsToFitInWidth(const void*, std::uint32_t length, std::uint32_t width, std::uint32_t*) override {
        const std::uint32_t fit = width / 8; return fit < length ? fit : length;
    }
    std::uint32_t CalculateNumberOfLines(const void*, std::uint32_t, std::uint32_t) override { return 1; }
    std::uint32_t CalculateWidthOfLines(const void*, std::uint32_t, std::uint32_t) override { return 0; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, bool, bool) override { return true; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, int, int, int) override { return true; }
    bool TextArea(const void*, std::uint32_t, cRZRect&) override { return true; }
    int GetStringWidth(const void*, std::uint32_t n) override { return static_cast<int>(n) * 8; }
    int GetStringVisibleWidth(const void*, std::uint32_t n, std::uint32_t& v) override { v = n; return static_cast<int>(n) * 8; }
    bool FormatForEllipsis(cIGZString&, const cIGZString&, const cRZRect&, bool) override { return true; }
    bool DrawTextLine(void*, int x, int y, const void* text, std::uint32_t length, std::uint32_t color, std::uint32_t,
                      const cRZRect*, cRZPoint*) override {
        drawn.push_back(std::to_string(x) + "," + std::to_string(y) + ":" + std::string(static_cast<const char*>(text), length) +
                        ":" + std::to_string(color));
        return true;
    }
    bool DrawTextParagraph(void*, const cRZRect&, const void*, std::uint32_t, int, std::uint32_t, std::uint32_t, const cRZRect*, cRZPoint*) override { return true; }
    cIGZFontRenderer* GetRenderer() const override { return nullptr; } void SetRenderer(cIGZFontRenderer*) override {}
    bool LockSurfaceForFontUse(void*) override { return true; } bool UnlockSurfaceForFontUse(void*) override { return true; }
};

struct Buffer final : IWinBuffer {
    Ctx context;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    const cRZRect* GetBounds() const override { static const cRZRect rect{0, 0, 640, 480}; return &rect; }
    std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override {
        return 0xff000000u | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
    }
    void NativeToRGB(std::uint32_t n, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override {
        r = (n >> 16) & 0xff; g = (n >> 8) & 0xff; b = n & 0xff;
    }
    void* GetDrawContext() override { return &context; }
    bool SetFadeAlpha(std::uint8_t) override { return true; }
};

struct Graphics final : IWinGraphics {
    Buffer buffer;
    std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override {
        return 0xff000000u | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
    }
    void NativeToRGB(std::uint32_t n, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override {
        r = (n >> 16) & 0xff; g = (n >> 8) & 0xff; b = n & 0xff;
    }
    IWinBuffer* PrimaryBuffer() override { return &buffer; }
    bool DrawSystemActive() override { return false; }
};

struct Styles final : IWinStyleManager {
    FakeFont font;
    cIGZUnknown* GetFontStyle(std::uint32_t) override { return &font; }
};

struct Factory final : IWinCtrlFactory {
    bool CreateCtrl(std::uint32_t, std::uint32_t, void**) override { return false; }
    cIGZWinScrollbar* CreateScrollbar(std::uint32_t, int, int, int, int) override { return nullptr; }
    std::uint32_t DefaultColor(int id) override { return 0x1000u + static_cast<std::uint32_t>(id & 0xff); }
    std::uint32_t DoubleClickMilliseconds() override { return 500; }
};

struct FakeMessageServer final : cIGZMessageServer {
    struct Note { cIGZMessageTarget* target; std::uint32_t type; };
    std::vector<Note> added, removed, live;
    int refs = 1;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 1) { *out = this; AddRef(); return true; }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override {
        if (--refs == 0) { delete this; return 0; }
        return static_cast<std::uint32_t>(refs);
    }
    bool MessageSend(const cGZMessage&) override { return true; }
    bool MessagePost(const cGZMessage&, bool) override { return true; }
    bool AddNotification(cIGZMessageTarget* target, std::uint32_t type) override {
        target->AddRef();
        added.push_back({target, type});
        live.push_back({target, type});
        return true;
    }
    bool RemoveNotification(cIGZMessageTarget* target, std::uint32_t type) override {
        removed.push_back({target, type});
        for (auto it = live.begin(); it != live.end(); ++it) {
            if (it->target == target && it->type == type) {
                target->Release();
                live.erase(it);
                return true;
            }
        }
        return false;
    }
    bool GeneralMessagePostToTarget(const cGZMessage&, cIGZMessageTarget*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
};

FakeMessageServer* g_server = nullptr;
bool ServiceResolver(std::uint32_t serviceId, std::uint32_t iid, void** out) {
    if (serviceId == kGZSID_cGZMessageServer && iid == kGZIID_cIGZMessageServer && g_server) {
        g_server->AddRef();
        *out = g_server;
        return true;
    }
    return false;
}

// The focus target: a plain text edit that records the messages the composition forwards.
struct FocusEdit final : cGZWinTextEdit {
    using cGZWinTextEdit::cGZWinTextEdit;
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& message) override {
        got.push_back(message);
        return true;
    }
    void SetCtx(cIGZDrawContext* context) { drawContext_ = context; }
};

cGZIMECompositionStringData MakeData(const std::u16string& text, const std::vector<std::uint8_t>& attributes,
                                     std::uint32_t caret, std::uint32_t language) {
    cGZIMECompositionStringData data;
    data.text = reinterpret_cast<const std::uint16_t*>(text.data());
    data.attributes = attributes.empty() ? nullptr : attributes.data();
    data.length = static_cast<std::uint32_t>(text.size());
    data.caret = caret;
    data.language = language;
    return data;
}

}  // namespace

int main() {
    FakeMgr mgr;
    Graphics graphics;
    Styles styles;
    Factory factory;
    SetWinGraphics(&graphics);
    SetWinStyleManager(&styles);
    SetWinCtrlFactory(&factory);
    SetRZServiceResolver(&ServiceResolver);

    g_server = new FakeMessageServer();   // refs starts at 1: our own reference

    FocusEdit* focus = new FocusEdit(&mgr);
    focus->AddRef();
    focus->SetCtx(&graphics.buffer.context);
    focus->Init();
    focus->SetArea(10, 20, 110, 120);
    focus->SetText("AB", 2, false);
    focus->SetInsertionIndex(1);
    mgr.focus = focus;

    auto* ime = new cGZWinIMEComposition(&mgr);
    ime->AddRef();
    cIGZWinIMEComposition* imeView = ime;
    cIGZWinTextEdit* textInterface = ime;
    cIGZWin* winInterface = ime;
    CHECK(imeView->AsIGZWin() == static_cast<cGZWin*>(ime));
    CHECK(ime->Init());
    CHECK(imeView->GetCompositionFlags() == 0);
    CHECK(ime->Language() == 0);
    CHECK(ime->FocusTarget() == focus);
    CHECK(ime->TargetTextEdit() != nullptr);
    CHECK(ime->GetFont() == &styles.font);
    CHECK(ime->GetTextGutterX() == 0 && ime->GetTextGutterY() == 0);
    CHECK(g_server->added.size() == 3);
    CHECK(g_server->added[0].type == kGZMsgIMECompositionData);
    CHECK(g_server->added[1].type == kGZMsgIMECompositionCaret);
    CHECK(g_server->added[2].type == kGZMsgIMECompositionEnd);
    CHECK(g_server->added[0].target == reinterpret_cast<cIGZMessageTarget*>(static_cast<cGZWin*>(ime)));

    // Colours: SetWindowColors installs black/white/red/grey and the attribute colours.
    std::uint32_t native = 0;
    // Colours 0..5 were overwritten by the target edit's own colours (all black here, 3 = 0x40000000 packed);
    // 6 and 7 keep the guard colours because the mirror only touches 0..5.
    CHECK(ime->GetCompositionFontColor(0, native) && native == 0xff000000u);
    CHECK(ime->GetCompositionFontColor(3, native) && native == 0xff000000u);   // the mirror writes the base colour
    CHECK(ime->CompositionColors()[3] == 0xffffffffu);                        // +0x3d4 keeps the default here
    CHECK(ime->GetCompositionFontColor(6, native) && native == 0xffff0000u);
    CHECK(!ime->GetCompositionFontColor(8, native));
    CHECK(ime->CompositionColors()[0] == 0xff000000u && ime->CompositionColors()[7] == 0xff808080u);
    CHECK(ime->AttributeColors()[0] == 0xff000000u);                              // target colour 1 -> attribute 0
    CHECK(ime->AttributeColors()[1] == 0xff000000u && ime->AttributeColors()[2] == 0xff000000u);
    CHECK(ime->AttributeColors()[3] == 0xffc00000u);

    // SetCompositionFontColor stores the native colour and the base colour.
    CHECK(ime->SetCompositionFontColor(3, 0xff00ff00u));
    CHECK(ime->CompositionColors()[3] == 0xff00ff00u);
    CHECK(ime->GetCompositionFontColor(3, native));   // the base colour conversion owns the exact native value
    CHECK(!ime->SetCompositionFontColor(8, 0));

    // Caret colour quirk: SetCaretColor only reads the current colour.
    std::uint32_t caret = 0, caretAfter = 0;
    ime->GetCaretColor(caret);
    ime->SetCaretColor(0xff112233u);
    ime->GetCaretColor(caretAfter);
    CHECK(caretAfter == caret);

    // Caret period and outline use round-trip through the base control.
    CHECK(ime->SetCaretPeriod(250));
    std::uint32_t period = 0;
    CHECK(ime->GetCaretPeriod(period) && period == 250);
    ime->SetOutlineUse(true, 0xff0000ffu);
    bool outline = false;
    std::uint32_t outlineColor = 0;
    ime->GetOutlineUse(outline, outlineColor);
    CHECK(outline && outlineColor == 0xff0000ffu);

    // Composition flags quirks.
    ime->SetCompositionFlags(0);
    ime->SetCompositionFlag(1);
    CHECK(ime->GetCompositionFlags() == 0);           // +0x3d0 was not updated
    CHECK(ime->GetFlags() == 1);                      // the text-edit flags received compositionFlags | 1
    ime->ClearCompositionFlag(1);
    CHECK(ime->GetFlags() == 1);                      // compositionFlags has no bit 1, so nothing was cleared
    ime->SetCompositionFlags(1);
    CHECK(ime->GetCompositionFlags() == 1);
    CHECK(ime->IsFlagSet(kTEOutline));                // bit 0 synchronises the outline flag
    ime->SetCompositionFlags(0);
    CHECK(!ime->IsFlagSet(kTEOutline));
    ime->SetCompositionFlag(2);
    CHECK(imeView->GetCompositionFlags() == 0);
    CHECK(ime->GetFlags() == 2);                      // replaced, not OR-ed with the old flags
    ime->ClearCompositionFlag(2);
    CHECK(ime->GetFlags() == 2);

    // Composition string: UTF-16 -> UTF-8, width from the font, attribute bytes kept and clamped.
    std::u16string text = u"AB";
    std::vector<std::uint8_t> attributes = {0, 2};
    cGZIMECompositionStringData data = MakeData(text, attributes, 2, 0);
    cGZMessage message{kGZMsgIMECompositionData, reinterpret_cast<std::uint64_t>(&data), 0, 0};
    ime->DoMessage(message);   // the custom types fall through to cGZWin::DoMessage, which answers false
    CHECK(std::string(imeView->GetText()) == "AB");
    CHECK(textInterface->GetTextLength() == 2);
    CHECK(ime->GetW() == 2 * 8 + 4);                  // font width + 4 + gutter * 2
    CHECK(ime->Attributes().size() == 2 && ime->Attributes()[0] == 0 && ime->Attributes()[1] == 2);
    std::uint32_t insertion = 0;
    CHECK(ime->GetInsertionIndex(insertion) && insertion == 2);

    // The underline paint path: the attribute-2 character gets two lines.
    ime->SetArea(0, 0, 100, 40);
    CHECK(imeView->GZPaint());
    CHECK(graphics.buffer.context.Lines() == 2);

    // A caret message maps the character index to a byte index.
    data.caret = 1;
    cGZMessage caretMessage{kGZMsgIMECompositionCaret, reinterpret_cast<std::uint64_t>(&data), 0, 0};
    ime->DoMessage(caretMessage);
    CHECK(ime->GetInsertionIndex(insertion) && insertion == 1);
    data.caret = 99;   // past the end: the guard stops at the text end instead of spinning on GetCharLength == 0
    ime->DoMessage(caretMessage);
    CHECK(ime->GetInsertionIndex(insertion) && insertion == 2);
    ime->SetCompositionString(nullptr);   // null data is a no-op
    CHECK(std::string(ime->GetText()) == "AB");

    // Attribute bytes above 3 are zeroed.
    std::vector<std::uint8_t> raw = {7, 3, 2};
    std::u16string rawText = u"ABC";
    cGZIMECompositionStringData clamped = MakeData(rawText, raw, 1, 0);
    imeView->SetCompositionString(&clamped);
    CHECK(ime->Attributes().size() == 3);
    CHECK(ime->Attributes()[0] == 0 && ime->Attributes()[1] == 3 && ime->Attributes()[2] == 2);

    // Language 0x14 switches the Chinese colour/quirk path and disables the composition underlines.
    data.caret = 0;
    data.language = 0x14;
    ime->DoMessage(message);
    CHECK(ime->Language() == 0x14);
    const std::size_t linesBefore = graphics.buffer.context.Lines();
    CHECK(ime->GZPaint());
    CHECK(graphics.buffer.context.Lines() == linesBefore);

    // The end message recomputes the position from the focus window. With no parent the point stays at the
    // caret position of the target edit (font width 8 for column 1, line 0).
    cGZMessage endMessage{kGZMsgIMECompositionEnd, 0, 0, 0};
    focus->SetText("AB", 2, false);
    focus->SetInsertionIndex(1);
    ime->DoMessage(endMessage);
    // The target edit's gutter (5) plus the width of "A" (8) is the x position; there is no parent, so the point
    // is not converted and GZWinMoveTo puts the composition window there.
    CHECK(winInterface->GetL() == 13 && winInterface->GetT() == 0);

    // Character messages are forwarded to the focus window.
    cGZMessage character{kGZMsgCharacter, 'x', 0, 0};
    CHECK(ime->DoMessage(character));
    CHECK(focus->got.size() == 1 && focus->got[0].type == kGZMsgCharacter);

    // QueryInterface from both views.
    void* out = nullptr;
    CHECK(ime->QueryInterface(kGZIID_cIGZWinIMEComposition, &out));
    CHECK(out == static_cast<cIGZWinIMEComposition*>(ime));
    static_cast<cIGZWinIMEComposition*>(out)->Release();
    CHECK(ime->QueryInterface(kGZIID_cIGZWinTextEdit, &out));
    CHECK(out == static_cast<cIGZWinTextEdit*>(ime));
    static_cast<cIGZWinTextEdit*>(out)->Release();
    CHECK(ime->QueryInterface(kGZIID_cIGZWin, &out));
    CHECK(out == static_cast<cGZWin*>(ime));
    static_cast<cGZWin*>(out)->Release();
    CHECK(!ime->QueryInterface(0xdeadbeef, &out));
    cIGZWinTextEdit* textEditView = ime;
    CHECK(textEditView->QueryInterface(kGZIID_cIGZWinIMEComposition, &out));
    CHECK(out == static_cast<cIGZWinIMEComposition*>(ime));
    static_cast<cIGZWinIMEComposition*>(out)->Release();

    // Text getters (the last composition string was "AB").
    cRZString wide;
    ime->GetText(wide);
    CHECK(wide.str() == "AB");
    char buffer[8] = {};
    std::uint32_t length = sizeof(buffer);
    ime->GetText(buffer, length);
    CHECK(length == 2 && std::string(buffer, 2) == "AB");

    // Shutdown unregisters the messages and drops both references.
    CHECK(imeView->Shutdown());
    CHECK(!ime->GetFlag(kWinInitialized));
    CHECK(g_server->removed.size() == 3);
    CHECK(g_server->live.empty());
    CHECK(ime->FocusTarget() == nullptr && ime->TargetTextEdit() == nullptr);
    // The no-focus path of SetWindowPosition moves to (2, 2); callable after Shutdown because it only uses the
    // base control's area now.
    ime->SetWindowPosition();
    CHECK(ime->GetL() == 2 && ime->GetT() == 2);
    ime->Release();

    focus->Release();
    g_server->Release();
    if (failures == 0) std::printf("gz_winimecomposition: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
