// gz_winttextticker_test — cGZWinTextTicker: queueing, the scrolling strip, span shifts, clicks, keys and the guarded
// quirks. Built and run with ASan/UBSan.
#include <SDL.h>

#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

#include "fake_drawcontext.h"
#include "fake_winmgr.h"
#include "gz/Buffer.h"
#include "gz/WinTextTicker.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace gz;

namespace {

// A counted cIGZBuffer that records every operation the ticker performs on the strip.
struct TestBuffer final : cIGZBuffer {
    int refs = 0, initCount = 0;
    std::uint32_t initW = 0, initH = 0;
    cGZBufferColorType initType{};
    cRZRect area{0, 0, 0, 0};
    std::uint32_t transparent = 0;
    bool hasTransparent = false;
    std::vector<std::uint32_t> locks, unlocks;
    std::vector<std::pair<cRZRect, cRZRect>> blits;                               // (source, destination)
    std::vector<std::pair<std::uint32_t, cRZRect>> fills;
    std::vector<std::uint32_t> fullFills;                                          // Fill(color, nullptr)
    std::vector<std::tuple<int, int, int, int, std::uint32_t, std::uint32_t>> lines;
    int transparentQueries = 0;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { if (refs > 0) --refs; return static_cast<std::uint32_t>(refs); }
    bool Init(std::uint32_t width, std::uint32_t height, cGZBufferColorType type) override {
        ++initCount; initW = width; initH = height; initType = type; area = {0, 0, static_cast<int>(width), static_cast<int>(height)};
        return true;
    }
    bool Shutdown() override { return true; }
    bool Uninitialize() override { return true; }
    bool Lock(eLockFlags flags) override { locks.push_back(static_cast<std::uint32_t>(flags)); return true; }
    bool Unlock(eLockFlags flags) override { unlocks.push_back(static_cast<std::uint32_t>(flags)); return true; }
    bool IsLocked() const override { return false; }
    std::uint32_t Width() const override { return static_cast<std::uint32_t>(area.right - area.left); }
    std::uint32_t Height() const override { return static_cast<std::uint32_t>(area.bottom - area.top); }
    const cRZRect* GetBufferArea() const override { return &area; }
    void GetBufferArea(cRZRect& out) const override { out = area; }
    std::uint32_t GetMemoryUsage() const override { return 0; }
    cGZBufferColorType GetColorType() const override { return initType; }
    void GetColorType(cGZBufferColorType& out) const override { out = initType; }
    std::uint32_t GetBitsPerPixel() const override { return 0; }
    std::uint32_t GetBytesPerPixel() const override { return 0; }
    bool Fill(std::uint32_t color, const cRZRect& rect, const cRZRect* clip) override {
        (void)clip; fills.emplace_back(color, rect); return true;
    }
    bool Fill(std::uint32_t color, int l, int t, int r, int b, const cRZRect* clip) override {
        (void)clip; fills.emplace_back(color, cRZRect{l, t, r, b}); return true;
    }
    bool Fill(std::uint32_t color, const cRZRect* clip) override { (void)clip; fullFills.push_back(color); return true; }
    std::uint32_t GetPixel(std::uint32_t, std::uint32_t) const override { return 0; }
    void SetPixel(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void SetTransparency(std::uint32_t color) override { transparent = color; hasTransparent = true; }
    bool GetTransparency() const override { return hasTransparent; }
    bool IsPixelTransparent(std::uint32_t, std::uint32_t) override { return false; }
    bool GetTransparentColor(std::uint32_t& color) const override {
        const_cast<TestBuffer*>(this)->transparentQueries++;
        if (!hasTransparent) return false;
        color = transparent;
        return true;
    }
    void RemoveTransparency() override { hasTransparent = false; }
    bool DrawLine(int x0, int y0, int x1, int y1, int color, std::uint32_t thickness) override {
        lines.emplace_back(x0, y0, x1, y1, static_cast<std::uint32_t>(color), thickness);
        return true;
    }
    bool Blt(cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destination, const cRZRect* clip) override {
        (void)source; (void)clip; blits.emplace_back(sourceRect, destination); return true;
    }
    std::uint32_t ConvertRGBValueToNative(std::uint8_t r, std::uint8_t g, std::uint8_t b) override {
        return 0xff000000u | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
    }
    bool ConvertNativeValueToRGB(std::uint32_t n, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override {
        r = (n >> 16) & 0xff; g = (n >> 8) & 0xff; b = n & 0xff; return true;
    }
    std::uint32_t ConvertRGBAValueToNative(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t) override {
        return ConvertRGBValueToNative(r, g, b);
    }
    bool ConvertNativeValueToRGBA(std::uint32_t n, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) override {
        a = 0xff; return ConvertNativeValueToRGB(n, r, g, b);
    }
    void* GetColorSurfaceBits() override { return nullptr; }
    std::uint32_t GetColorSurfaceStride() override { return 0; }
    std::uint32_t GetChangeCounter() override { return 0; }
    cIGZBufferHardwareCache* GetHardwareCache() override { return nullptr; }
    bool SetHardwareCache(cIGZBufferHardwareCache*) override { return false; }
    bool IsReady() override { return true; }
};

// A counted font: five pixels per byte, one line ten pixels high.
struct TestFont final : cIGZFont {
    int refs = 0;
    int charHeight = 10, charWidth = 5;
    mutable int fontColorCalls = 0, stringWidthCalls = 0;
    int charHeightCalls = 0, advanceWidthCalls = 0, lockCalls = 0, unlockCalls = 0;
    std::vector<std::string> drawn;
    int lastX = 0, lastY = 0;
    std::uint32_t lastColor = 0, lastBackground = 0;
    void* lastSurface = nullptr;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { if (refs > 0) --refs; return static_cast<std::uint32_t>(refs); }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    void GetName(cIGZString&) const override {}
    std::uint32_t GetStyle() const override { return 0; }
    bool GetStyle(int) const override { return false; }
    bool SetStyle(int, bool) override { return false; }
    std::uint32_t GetType() const override { return 0; }
    std::uint32_t GetSize() const override { return 0; }
    bool SetSize(std::uint32_t) override { return false; }
    std::uint32_t GetFontColor() const override { ++fontColorCalls; return 0xffabcdef; }
    std::uint32_t GetFontBackgroundColor() const override { return 0; }
    void SetFontColor(std::uint32_t, std::uint32_t) override {}
    int GetAntialiasing() override { return 0; }
    void SetAntialiasing(int) override {}
    float GetAntialiasingGammaCorrection() override { return 1.0f; }
    void SetAntialiasingGammaCorrection(float) override {}
    float GetAntialiasingSharpness() override { return 1.0f; }
    void SetAntialiasingSharpness(float) override {}
    void GetFilePath(cIGZString&) const override {}
    void SetFilePath(const cIGZString&) override {}
    bool GetCodePage(int& codePage) const override { codePage = 8; return true; }
    bool SetCodePage(int) override { return true; }
    bool IsCodePageAvailable(int) override { return true; }
    bool IsCharEncodingAvailable(int) override { return true; }
    bool IsCharAvailable(void*, std::uint32_t) override { return true; }
    bool PreloadChars(void*, std::uint32_t) override { return true; }
    void GetScaling(float& x, float& y) const override { x = y = 1.0f; }
    void SetScaling(float, float) override {}
    void GetAdvanceScaling(float& x, float& y) const override { x = y = 1.0f; }
    void SetAdvanceScaling(float, float) override {}
    void GetSpacingAdjustment(int& x, int& y) const override { x = y = 0; }
    void SetSpacingAdjustment(int, int) override {}
    int GetLineHeight() override { return charHeight; }
    int CharWidth(const void*, std::uint32_t length) override { return static_cast<int>(length) * charWidth; }
    int CharHeight(const void*, std::uint32_t length) override {
        ++charHeightCalls;
        return length ? static_cast<int>(length) * charHeight : charHeight;
    }
    int GetSizeAboveBaseline() override { return charHeight; }
    int GetSizeBelowBaseline() override { return 0; }
    int GetAdvanceWidth(const void*, std::uint32_t length) override {
        ++advanceWidthCalls;
        return static_cast<int>(length) * charWidth;
    }
    std::uint32_t CalculateWordsToFitInWidth(const void* text, std::uint32_t length, std::uint32_t width,
                                             std::uint32_t* fitted, WordWrapMode) override {
        return CalculateCharsToFitInWidth(text, length, width, fitted);
    }
    std::uint32_t CalculateCharsToFitInWidth(const void*, std::uint32_t length, std::uint32_t width,
                                             std::uint32_t* fitted) override {
        const std::uint32_t n = width / static_cast<std::uint32_t>(charWidth);
        if (fitted) *fitted = n;
        return n < length ? n : length;
    }
    std::uint32_t CalculateNumberOfLines(const void*, std::uint32_t, std::uint32_t) override { return 1; }
    std::uint32_t CalculateWidthOfLines(const void*, std::uint32_t length, std::uint32_t) override {
        return length * static_cast<std::uint32_t>(charWidth);
    }
    bool CalculateTextArea(const void*, std::uint32_t length, cRZRect& area, bool, bool) override {
        area = {0, 0, static_cast<int>(length) * charWidth, charHeight}; return true;
    }
    bool CalculateTextArea(const void*, std::uint32_t length, cRZRect& area, int, int, int) override {
        area = {0, 0, static_cast<int>(length) * charWidth, charHeight}; return true;
    }
    bool TextArea(const void*, std::uint32_t length, cRZRect& area) override {
        area = {0, 0, static_cast<int>(length) * charWidth, charHeight}; return true;
    }
    int GetStringWidth(const void*, std::uint32_t length) override {
        ++stringWidthCalls;
        return static_cast<int>(length) * charWidth;
    }
    int GetStringVisibleWidth(const void*, std::uint32_t length, std::uint32_t& visible) override {
        visible = length; return static_cast<int>(length) * charWidth;
    }
    bool FormatForEllipsis(cIGZString& out, const cIGZString& in, const cRZRect&, bool) override {
        out.AssignFromIGZString(in); return true;
    }
    bool DrawTextLine(void* surface, int x, int y, const void* text, std::uint32_t length, std::uint32_t color,
                      std::uint32_t background, const cRZRect*, cRZPoint*) override {
        drawn.emplace_back(static_cast<const char*>(text), length);
        lastX = x; lastY = y; lastColor = color; lastBackground = background; lastSurface = surface;
        return true;
    }
    bool DrawTextParagraph(void*, const cRZRect&, const void*, std::uint32_t, int, std::uint32_t, std::uint32_t,
                           const cRZRect*, cRZPoint*) override { return true; }
    cIGZFontRenderer* GetRenderer() const override { return nullptr; }
    void SetRenderer(cIGZFontRenderer*) override {}
    bool LockSurfaceForFontUse(void*) override { ++lockCalls; return true; }
    bool UnlockSurfaceForFontUse(void*) override { ++unlockCalls; return true; }
};

struct TestHost final : IWinTextTickerHost {
    TestBuffer buffer;
    std::uint32_t keyColor = 0xff00ff00;
    std::uint32_t lastColorId = 0;
    int created = 0;
    std::vector<cGZMessage> posted;

    cIGZBuffer* CreateBuffer() override { ++created; buffer.AddRef(); return &buffer; }
    std::uint32_t DefaultColor(std::uint32_t colorId) override { lastColorId = colorId; return keyColor; }
    bool MessagePost(const cGZMessage& message) override { posted.push_back(message); return true; }
};

struct Ticker final : cGZWinTextTicker {
    using cGZWinTextTicker::cGZWinTextTicker;
    void SetContext(cIGZDrawContext* context) { drawContext_ = context; }
};

struct Target final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& message) override { got.push_back(message); return true; }
};

// A click callback that records what the ticker hands over.
std::uint32_t g_callbackId = 0;
void* g_callbackContext = nullptr;
int g_callbackCalls = 0;
void RecordCallback(std::uint32_t id, void* context) { g_callbackId = id; g_callbackContext = context; ++g_callbackCalls; }

int left(const cRZRect& r) { return r.left; }

}  // namespace

int main() {
    FakeMgr mgr;
    FakeWinGraphics graphics;
    fake_dc::Ctx context;
    TestHost host;
    TestFont font;
    SetWinGraphics(&graphics);
    SetWinTextTickerHost(&host);
    g_ticks = 1000;   // cRZTimer treats a zero start tick as "stopped"

    auto* target = new Target(&mgr);
    target->AddRef();

    // ---- construction, vtables and defaults ----------------------------------------------------------------------
    auto* ticker = new Ticker(&mgr);
    ticker->AddRef();
    CHECK(host.created == 1 && host.buffer.refs == 1);          // RZ::GS() vslot 0x20: the strip is made in the ctor
    CHECK(ticker->GetSpeed() == kTextTickerDefaultSpeed);
    CHECK(ticker->GetFrequency() == kTextTickerDefaultFrequency);
    CHECK(ticker->ActiveCount() == 0 && ticker->QueueCount() == 0);
    CHECK(ticker->ScrollPosition() == 0 && ticker->CurrentMessage() == nullptr);

    void* out = nullptr;
    CHECK(ticker->QueryInterface(kGZIID_cIGZWinTextTicker, &out));
    CHECK(out == static_cast<cIGZWinTextTicker*>(ticker));
    static_cast<cIGZWinTextTicker*>(out)->Release();            // the reference QueryInterface took
    CHECK(!ticker->QueryInterface(kGZIID_cIGZWinTextTicker, nullptr));
    out = nullptr;
    CHECK(ticker->QueryInterface(kGZIID_cIGZWin, &out));        // anything else goes to cGZWin
    CHECK(out == static_cast<cGZWin*>(ticker));
    static_cast<cGZWin*>(out)->Release();
    out = nullptr;
    // Through the cGZWin view the non-virtual thunk must adjust back to the interface (the original's second group).
    CHECK(static_cast<cGZWin*>(ticker)->QueryInterface(kGZIID_cIGZWinTextTicker, &out));
    CHECK(out == static_cast<cIGZWinTextTicker*>(ticker));
    static_cast<cIGZWinTextTicker*>(out)->Release();
    out = nullptr;
    CHECK(!ticker->QueryInterface(0x12345678u, &out));
    CHECK(ticker->AsIGZWin() == static_cast<cIGZWin*>(static_cast<cGZWin*>(ticker)));

    // ---- messages need a font; AddMessage and SetFont reference counting -------------------------------------------
    auto* hello = new cRZString("Hello");
    CHECK(ticker->AddMessage(hello, 0xffff0000u, 0xff112233u, kTextTickerUnderline,
                             &RecordCallback, reinterpret_cast<void*>(0x1234)) == 0);   // no font yet
    CHECK(ticker->SetFont(&font) && font.refs == 1);
    CHECK(ticker->SetFont(&font) && font.refs == 1);            // the new reference is taken before the old is dropped
    CHECK(ticker->AddMessage(nullptr, 0, 0, 0, &RecordCallback, nullptr) == 0);   // guarded: no text

    // ---- Init -----------------------------------------------------------------------------------------------------
    ticker->SetArea(0, 0, 100, 50);
    CHECK(ticker->Init());
    CHECK(font.charHeightCalls == 1);                            // font CharHeight(0, 0) decides the strip height
    CHECK(ticker->GetW() == 100 && ticker->GetH() == 10);        // one font line high
    CHECK(host.buffer.initCount == 1);
    CHECK(host.buffer.initW == 100 && host.buffer.initH == 10);
    CHECK(host.buffer.initType.type == kBufferColorDefault && host.buffer.initType.bitsPerPixel == 1);
    CHECK(host.lastColorId == kTextTickerKeyColorId);
    CHECK(host.buffer.hasTransparent && host.buffer.transparent == host.keyColor);
    CHECK(host.buffer.fullFills.size() == 1 && host.buffer.fullFills[0] == 0xff000000u);   // the window's fill colour
    CHECK(host.buffer.locks.size() == 1 && host.buffer.locks[0] == 0x8001u);
    CHECK(ticker->Init());                                        // already initialised: no second strip init
    CHECK(host.buffer.initCount == 1);
    ticker->SetContext(&context);

    // ---- queueing --------------------------------------------------------------------------------------------------
    const std::uint32_t id = ticker->AddMessage(hello, 0xffff0000u, 0xff112233u, kTextTickerUnderline,
                                                &RecordCallback, reinterpret_cast<void*>(0x1234));
    CHECK(id == 1 && ticker->QueueCount() == 1 && ticker->ActiveCount() == 0);
    CHECK(font.refs == 2);                                        // the message took a reference on the font
    ticker->advanceMessage();
    CHECK(ticker->QueueCount() == 0 && ticker->ActiveCount() == 1);
    TickerMessage* message = ticker->CurrentMessage();
    CHECK(message != nullptr && message->id_ == 1);
    CHECK(message->state20_ == 100 + kTextTickerLeftMargin);
    CHECK(message->state28_ == 5 * 5 + kTextTickerLeftMargin + 100);   // string width + 20 + width
    CHECK(ticker->ScrollPosition() == -kTextTickerLeftMargin);
    CHECK(ticker->TextCursor() == hello->Data());
    CHECK(ticker->CurrentFlags() == kTextTickerUnderline);
    message->state20_ = 120;                                      // documented layout
    message->state28_ = 145;

    // ---- the scrolling strip --------------------------------------------------------------------------------------
    host.buffer.blits.clear(); host.buffer.fills.clear(); host.buffer.fullFills.clear();
    host.buffer.locks.clear(); host.buffer.unlocks.clear(); host.buffer.lines.clear();
    ticker->DoDraw();                                            // scroll position -20 -> -18
    CHECK(host.buffer.blits.size() == 1);
    CHECK(host.buffer.blits[0].first.left == 2 && host.buffer.blits[0].first.right == 100);      // source [2..100]
    CHECK(host.buffer.blits[0].second.left == 0 && host.buffer.blits[0].second.right == 98);     // destination [0..98]
    CHECK(host.buffer.blits[0].first.bottom == 10 && host.buffer.blits[0].second.bottom == 10);
    CHECK(host.buffer.fills.size() == 2);                        // the cleared edge and the message background
    CHECK(host.buffer.fills[0].first == 0xff000000u && left(host.buffer.fills[0].second) == 98);
    CHECK(host.buffer.fills[1].first == 0xff112233u);            // the background differs from the strip key colour
    CHECK(host.buffer.locks.size() == 4);
    CHECK(host.buffer.locks[0] == 0x8010u && host.buffer.locks[1] == 0x8001u);
    CHECK(host.buffer.locks[2] == 0x8001u && host.buffer.locks[3] == 0x8004u);   // background + underline
    CHECK(host.buffer.unlocks == host.buffer.locks);
    CHECK(ticker->ScrollPosition() == -18);
    CHECK(message->state20_ == 118 && message->state28_ == 143);
    CHECK(font.drawn.empty());                                   // nothing is drawn while the text is still off-screen
    CHECK(host.buffer.lines.size() == 1);                        // the underline is drawn regardless of scrollX
    CHECK(std::get<0>(host.buffer.lines[0]) == 118 && std::get<1>(host.buffer.lines[0]) == 9);
    CHECK(std::get<4>(host.buffer.lines[0]) == 0xffff0000u && std::get<5>(host.buffer.lines[0]) == 1);

    for (int i = 0; i < 10; ++i) ticker->DoDraw();               // 10 more ticks: scroll position reaches +2
    CHECK(ticker->ScrollPosition() == 2);
    CHECK(font.drawn.size() == 1);
    CHECK(font.drawn[0] == "H");
    CHECK(font.lastX == 98 && font.lastY == 0);
    CHECK(font.lastColor == 0xffff0000u && font.lastBackground == 0xff112233u);
    CHECK(font.lastSurface == &host.buffer);
    CHECK(font.fontColorCalls == 1 && font.lockCalls == 1 && font.unlockCalls == 1);
    CHECK(host.buffer.transparentQueries == 11);                 // every tick asks for the strip's key colour

    // Four more pixels carry the cursor past the first character (the font advances five pixels).
    for (int i = 0; i < 3; ++i) ticker->DoDraw();
    CHECK(ticker->ScrollPosition() == 3);
    CHECK(ticker->TextCursor() == hello->Data() + 1);

    // ---- a click on the message text -------------------------------------------------------------------------------
    g_callbackCalls = 0; g_callbackId = 0; g_callbackContext = nullptr;
    CHECK(ticker->GZOnMouseDownL(static_cast<int>(message->state20_) - 1, 0, 0));
    CHECK(g_callbackCalls == 0);                                 // left of the span
    CHECK(ticker->GZOnMouseDownL(static_cast<int>(message->state20_), 12345, 7));
    CHECK(g_callbackCalls == 1);
    CHECK(g_callbackId == 1 && g_callbackContext == reinterpret_cast<void*>(0x1234));
    CHECK(host.posted.empty());                                  // no click message on this one
    CHECK(ticker->GZOnMouseDownL(static_cast<int>(message->state28_), 0, 0));
    CHECK(g_callbackCalls == 1);                                 // right edge is exclusive

    // ---- GZPaint: the timer gates DoDraw and always blits the strip to the draw context -----------------------------
    g_ticks += static_cast<std::uint32_t>(kTextTickerDefaultFrequency - 1);
    const std::size_t blits = host.buffer.blits.size();
    CHECK(ticker->GZPaint());
    CHECK(host.buffer.blits.size() == blits);                    // not due yet
    CHECK(context.log.size() == 1 && context.log[0] == "blt 0,0,100,10>0,0,100,10");
    g_ticks += 2;
    CHECK(ticker->GZPaint());
    CHECK(host.buffer.blits.size() == blits + 1);                // now a tick ran, and the timer restarted
    CHECK(ticker->GZPaint());
    CHECK(host.buffer.blits.size() == blits + 1);

    // ---- a clicked message posts its cGZMessage --------------------------------------------------------------------
    CHECK(ticker->ClearQueue());
    CHECK(ticker->ActiveCount() == 0 && ticker->QueueCount() == 0);
    CHECK(ticker->CurrentMessage() == nullptr);
    auto* postedText = new cRZString("Message");
    const cGZMessage click{kGZMsgCommand, 7, 8, 9};
    CHECK(ticker->AddMessage(postedText, 1, 2, 0, &click) == 2);
    CHECK(ticker->QueueCount() == 1 && ticker->ActiveCount() == 0);
    ticker->advanceMessage();
    CHECK(ticker->ActiveCount() == 1);
    TickerMessage* clickable = ticker->CurrentMessage();
    CHECK(clickable != nullptr && clickable->message_.type == kGZMsgCommand);
    CHECK(ticker->GZOnMouseDownL(static_cast<int>(clickable->state20_), 0, 0));
    CHECK(host.posted.size() == 1);
    CHECK(host.posted[0].type == kGZMsgCommand && host.posted[0].param1 == 7 && host.posted[0].param2 == 8);
    CHECK(ticker->GZOnMouseDownL(10000, 0, 0));                  // nothing under the cursor: no extra post
    CHECK(host.posted.size() == 1);

    // ---- RemoveMessage only sees the queue ---------------------------------------------------------------------------
    auto* first = new cRZString("first");
    auto* second = new cRZString("second");
    const std::uint32_t firstId = ticker->AddMessage(first, 0, 0, 0, &click);
    const std::uint32_t secondId = ticker->AddMessage(second, 0, 0, 0, &click);
    CHECK(firstId == 3 && secondId == 4 && ticker->QueueCount() == 2);
    CHECK(ticker->RemoveMessage(firstId));
    CHECK(ticker->QueueCount() == 1);
    CHECK(!ticker->RemoveMessage(firstId));                      // already gone
    CHECK(!ticker->RemoveMessage(999));
    CHECK(!ticker->RemoveMessage(clickable->id_));               // the on-screen message is not searched (quirk)

    // ---- ClearQueue keeps the scroll position (quirk) -----------------------------------------------------------------
    const std::int32_t scrollBefore = ticker->ScrollPosition();
    host.buffer.fullFills.clear();
    CHECK(ticker->ClearQueue());
    CHECK(ticker->QueueCount() == 0 && ticker->ActiveCount() == 0);
    CHECK(ticker->ScrollPosition() == scrollBefore);
    CHECK(host.buffer.fullFills.size() == 1 && host.buffer.fullFills[0] == 0xff000000u);
    CHECK(host.buffer.locks.back() == 0x8001u);

    // ---- the guarded deletion path: a span that runs out while the message is current --------------------------------
    ticker->SetSpeed(100);
    auto* shortText = new cRZString("Hi");
    CHECK(ticker->AddMessage(shortText, 0, 0, 0, &click) == 5);
    ticker->advanceMessage();
    CHECK(ticker->ActiveCount() == 1 && ticker->CurrentMessage() != nullptr);
    ticker->DoDraw();                                            // the span survives one tick
    CHECK(ticker->ActiveCount() == 1);
    ticker->DoDraw();                                            // then it runs out and the message is deleted
    CHECK(ticker->ActiveCount() == 0);
    CHECK(ticker->CurrentMessage() == nullptr);                  // guarded: the original would dangle here
    CHECK(ticker->TextCursor() == nullptr);

    // ---- a lone message restarts from the right when its text has been drawn to the end (quirk) ---------------------
    ticker->SetSpeed(2);
    auto* lone = new cRZString("Hello");
    CHECK(ticker->AddMessage(lone, 0, 0, 0, &click) == 6);
    ticker->advanceMessage();
    for (int i = 0; i < 400; ++i) ticker->DoDraw();
    CHECK(ticker->ActiveCount() == 1);                           // restarting resets the span, so it never scrolls out
    CHECK(ticker->CurrentMessage() != nullptr);
    CHECK(ticker->ClearQueue() && ticker->ActiveCount() == 0);

    // ---- keys are forwarded to the notification target ---------------------------------------------------------------
    ticker->SetNotificationTarget(target);
    CHECK(ticker->GZOnKeyDown(0x41, 3));
    CHECK(target->got.size() == 1 && target->got[0].type == kGZMsgKeyDown);
    CHECK(target->got[0].param1 == 0x41 && target->got[0].param2 == 3);
    CHECK(ticker->GZOnKeyUp(0x42, 1));
    CHECK(target->got.size() == 2 && target->got[1].type == kGZMsgKeyUp && target->got[1].param1 == 0x42);

    // ---- the caption is ignored -------------------------------------------------------------------------------------
    ticker->SetCaption(cRZString("ignored"));
    CHECK(ticker->GetCaption().Strlen() == 0);

    // ---- Shutdown releases the font and the strip --------------------------------------------------------------------
    CHECK(font.refs == 1);                                       // only SetFont's reference is left
    CHECK(ticker->Shutdown());
    CHECK(font.refs == 0);
    CHECK(host.buffer.refs == 0);
    CHECK(ticker->QueueCount() == 0 && ticker->ActiveCount() == 0);

    ticker->Release();
    target->Release();
    SetWinTextTickerHost(nullptr);

    std::printf(failures ? "FAILED\n" : "gz_winttextticker ok\n");
    return failures != 0;
}
