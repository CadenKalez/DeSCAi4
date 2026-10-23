// cGZWinHTML test: construction / interface queries / Init / Shutdown, the document load and the URL / link handling,
// the layout and the paint of the parsed elements, the scrolling (bars, keys, wheel, buttons, anchors) and the rest
// (flags, gutters, text-size table, scrollbar images, background images, the blink timer).
//
// The window reaches the framework through IWinHtmlHost: the recording canvas / font system / image decoder / WWW system
// below stand in for the engine, the same way the other control tests replace the window manager.
#include <cstdio>
#include <string>
#include <vector>

#include "SDL.h"

#include "fake_winmgr.h"
#include "gz/Buffer.h"
#include "gz/WinCtrlFactory.h"
#include "gz/WinHTML.h"
#include "html/HTMLDocument.h"
#include "rz/cRZMessage2Standard.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {

// ---- html::IHtmlFont / IHtmlFontSystem: monospace, 8 px per character, 10 px lines ------------------------------

struct FakeFont final : html::IHtmlFont {
    int releases = 0;
    std::uint32_t style = 0;
    void Release() override { ++releases; }
    void SetStyle(std::uint32_t s, bool enable) override {
        if (s == 0xffffffffu) style = 0;
        else if (enable) style |= s;
    }
    int LineHeight() override { return 10; }
    int GlyphHeight(int, int) override { return 9; }
    int Ascent() override { return 8; }
    int CharsThatFit(const char* text, int length, int maxWidth, int, int) override {
        const int fit = maxWidth / 8;
        if (fit >= length) return length;
        int last = -1;
        for (int i = 0; i < fit + 1 && i < length; ++i) if (text[i] == ' ') last = i;
        return last >= 0 && last < length ? last + 1 : 0;
    }
    int FirstWordLength(const char* text, int length, int, int* wordWidth) override {
        int n = 0;
        while (n < length && text[n] != ' ') ++n;
        if (n < length) ++n;
        *wordWidth = n * 8;
        return n;
    }
    int StringWidth(const char*, int length) override { return length * 8; }
    void DrawText(html::IHtmlSurface*, int, int, const char*, int, html::Color, std::uint32_t,
                  const cRZRect*) override {}
};

struct FakeFonts final : html::IHtmlFontSystem {
    FakeFont font;
    std::vector<std::string> requests;
    html::IHtmlFont* CreateFont(const std::string& face, std::uint32_t kind, std::uint32_t size, std::uint32_t style,
                                bool) override {
        requests.push_back(face + "/" + std::to_string(kind) + "/" + std::to_string(size) + "/" +
                           std::to_string(style));
        return &font;
    }
};

struct FakeBitmap final : html::IHtmlBitmap {
    int width = 16, height = 16, refs = 0;
    void AddRef() override { ++refs; }
    void Release() override { --refs; }
    int Width() override { return width; }
    int Height() override { return height; }
    cRZRect Bounds() override { return cRZRect{0, 0, width, height}; }
};

struct FakeImages final : html::IHtmlImageDecoder {
    FakeBitmap bitmap;
    int loads = 0;
    bool fail = false;
    bool LoadImageFile(const std::string&, html::IHtmlBitmap*& out) override {
        ++loads;
        if (fail) return false;
        out = &bitmap;
        return true;
    }
};

struct FakeWeb final : html::IHtmlWebSystem {
    std::vector<std::string> started;
    std::vector<std::uint32_t> cancelled;
    std::uint32_t StartJob(const std::string& url) override {
        started.push_back(url);
        return 100 + static_cast<std::uint32_t>(started.size());
    }
    void CancelJob(std::uint32_t id) override { cancelled.push_back(id); }
    bool IsVisited(const std::string& url) override { return url.find("visited") != std::string::npos; }
};

// ---- the recording canvas (the window's drawing buffer) ------------------------------------------------------------

struct FakeCanvas final : IWinHtmlCanvas {
    int begins = 0, ends = 0, fills = 0, lines = 0, blits = 0, blitStretched = 0, outlines = 0, tiles = 0;
    std::uint32_t lastFill = 0, lastOutline = 0, lastLineColor = 0;
    int lastTileOffsetX = 0, lastTileOffsetY = 0;
    cRZRect lastTileSource, lastTileDest;
    html::IHtmlBitmap* lastTileImage = nullptr;
    bool beginResult = true;
    bool Begin(std::uint32_t) override { ++begins; return beginResult; }
    void End(std::uint32_t) override { ++ends; }
    void FillRect(html::Color color, const cRZRect&, const cRZRect*) override { ++fills; lastFill = color; }
    void DrawLine(int, int, int, int, html::Color color, int) override { ++lines; lastLineColor = color; }
    void Blit(html::IHtmlBitmap*, const cRZRect&, const cRZRect&, const cRZRect*) override { ++blits; }
    void BlitStretched(html::IHtmlBitmap*, const cRZRect&, const cRZRect&, const cRZRect*) override { ++blitStretched; }
    void Outline(const cRZRect&, std::uint32_t color) override { ++outlines; lastOutline = color; }
    void BltTiled(html::IHtmlBitmap* image, const cRZRect& source, const cRZRect& dest, int offsetX,
                  int offsetY) override {
        ++tiles;
        lastTileImage = image;
        lastTileSource = source;
        lastTileDest = dest;
        lastTileOffsetX = offsetX;
        lastTileOffsetY = offsetY;
    }
};

struct FakeHost final : IWinHtmlHost {
    FakeCanvas* canvas = nullptr;
    FakeFonts* fonts = nullptr;
    FakeImages* images = nullptr;
    FakeWeb* web = nullptr;
    int bullets = 0;
    bool bulletFails = true;
    IWinHtmlCanvas* Canvas(IWinBuffer*) override { return canvas; }
    html::IHtmlFontSystem* Fonts() override { return fonts; }
    html::IHtmlImageDecoder* Images() override { return images; }
    html::IHtmlWebSystem* WebSystem() override { return web; }
    html::IHtmlBitmap* CreateBullet(int, html::Color, html::Color, bool) override {
        ++bullets;
        return bulletFails ? nullptr : &images->bitmap;
    }
};

// ---- cursors: the hand (0x12) and the arrow (1) are distinct objects --------------------------------------------------

struct FakeCursor final : cIGZCursor {
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
};

struct TestCursors final : cIGZCursorManager {
    FakeCursor hand, arrow;
    cIGZCursor* current = nullptr;
    int loads = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    cIGZCursor* LoadCursor(std::uint32_t id) override {
        ++loads;
        if (id == 0x12) return &hand;
        if (id == 1) return &arrow;
        return nullptr;
    }
    void SetCursor(cIGZCursor* cursor) override { current = cursor; }
};

// ---- the toolbars' factory: real cGZWinScrollbar objects, sized like the originals ------------------------------------

struct Factory final : gz::IWinCtrlFactory {
    cIGZWinMgr* mgr = nullptr;
    std::vector<gz::cIGZWinScrollbar*> bars;
    int created = 0;
    bool fail = false;
    bool CreateCtrl(std::uint32_t, std::uint32_t, void**) override { return false; }
    gz::cIGZWinScrollbar* CreateScrollbar(std::uint32_t id, int direction, int, int, int) override {
        if (fail) return nullptr;
        ++created;
        auto* bar = new gz::cGZWinScrollbar(mgr);
        bar->AddRef();
        gz::cIGZWinScrollbar* view = nullptr;
        bar->QueryInterface(gz::kGZIID_cIGZWinScrollbar, reinterpret_cast<void**>(&view));
        bar->Release();
        view->AsIGZWin()->SetID(id);
        view->SetOrientation(direction);
        view->AsIGZWin()->SetArea(0, 0, direction == 2 ? 16 : 100, direction == 2 ? 100 : 16);
        bars.push_back(view);
        return view;
    }
    std::uint32_t DefaultColor(int id) override { return 0xff000000u | static_cast<unsigned>(id & 0xffffff); }
    std::uint32_t DoubleClickMilliseconds() override { return 500; }
};

// ---- the notification target -----------------------------------------------------------------------------------------

struct Target final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool claimKeys = false;
    bool allowLinkJump = true;
    std::string linkText;
    int invalidations = 0;
    bool DoMessage(cGZMessage& message) override {
        got.push_back(message);
        if (message.type == kWinHtmlMsgKeyDown) return claimKeys;
        if (message.type == kWinHtmlMsgLinkJump) {
            if (message.param2 != 0) linkText = reinterpret_cast<cIGZString*>(message.param2)->Data();
            return allowLinkJump;
        }
        return true;
    }
    void InvalidateSelf() override { ++invalidations; }
    int Count(std::uint32_t type) const {
        int count = 0;
        for (const cGZMessage& message : got) count += message.type == type ? 1 : 0;
        return count;
    }
    const cGZMessage* Last(std::uint32_t type) const {
        for (auto it = got.rbegin(); it != got.rend(); ++it) if (it->type == type) return &*it;
        return nullptr;
    }
};

std::string TallPage() {
    std::string page =
        "<html><head><title>Help</title></head><body bgcolor=\"#102030\" text=\"white\">"
        "<h1>Help</h1><hr><ul><li>first</li><li>second</li></ul>";
    for (int i = 0; i < 24; ++i) {
        if (i == 10) page += "<p><a name=\"p10\">anchored</a> paragraph with a few words.</p>";
        else page += "<p>Paragraph " + std::to_string(i) + " with a few words.</p>";
    }
    page += "</body></html>";
    return page;
}

std::string WidePage() {
    std::string page =
        "<html><body><pre>"
        "a very long preformatted line that keeps going well past the right edge of the viewport, no wrapping here"
        "</pre>";
    for (int i = 0; i < 24; ++i) {
        if (i == 10) page += "<p><a name=\"p10\">anchored</a> paragraph with a few words.</p>";
        else page += "<p>Paragraph " + std::to_string(i) + " with a few words.</p>";
    }
    page += "</body></html>";
    return page;
}

std::string LinkPage() {
    return "<html><body><p>go <a href=\"next.html\" name=\"anchor1\">next page</a></p></body></html>";
}

std::string ImagePage(bool fixed) {
    std::string page = std::string("<html><body background=\"bg.gif\"") +
                       (fixed ? " bgproperties=\"fixed\"" : "") + "><p>picture</p>";
    for (int i = 0; i < 24; ++i) {
        if (i == 10) page += "<p><a name=\"p10\">anchored</a> paragraph with a few words.</p>";
        else page += "<p>Paragraph " + std::to_string(i) + " with a few words.</p>";
    }
    page += "</body></html>";
    return page;
}

std::string BlinkPage() {
    return "<html><body><p>blink <blink>here</blink></p></body></html>";
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    FakeMgr mgr;
    TestCursors cursors;
    mgr.cursors = &cursors;
    FakeWinGraphics graphics;
    SetWinGraphics(&graphics);

    FakeFonts fonts;
    FakeImages images;
    FakeWeb web;
    FakeCanvas canvas;
    FakeHost host;
    host.canvas = &canvas;
    host.fonts = &fonts;
    host.images = &images;
    host.web = &web;
    SetWinHtmlHost(&host);

    Factory factory;
    factory.mgr = &mgr;
    gz::SetWinCtrlFactory(&factory);

    auto* parent = new Target(&mgr);
    parent->AddRef();
    parent->SetArea(0, 0, 400, 400);
    mgr.valid.insert(parent);

    auto* win = new cGZWinHTML(&mgr);
    win->AddRef();
    mgr.valid.insert(win);
    win->SetID(0x1234);
    win->SetParentWin(parent);
    win->SetNotificationTarget(parent);
    win->SetArea(10, 20, 210, 120);   // 200 x 100

    // ---- construction, defaults, interface queries
    CHECK(win->GetFlags() == 0xfa);
    int left = 0, top = 0, right = 0, bottom = 0;
    win->GetGutters(left, top, right, bottom);
    CHECK(left == 4 && top == 4 && right == 0x16 && bottom == 4);
    CHECK(win->GetLoadStatus() == kHtmlLoadIdle);
    CHECK(win->canvas() == nullptr);   // bound by Init
    CHECK(std::string(win->GetCaption().Data()) == "");   // the caption is set in Init
    CHECK(win->Init());
    CHECK(win->canvas() == &canvas);
    CHECK(std::string(win->GetCaption().Data()) == "HTML Window");
    CHECK(cursors.loads == 2);   // 0x12 (hand) then 1 (arrow); 0x11 is only tried when 0x12 fails

    // The paint area is the draw area minus the gutters; the document rect is the viewport at scroll (0,0).
    {
        const html::cHTMLDrawContext& context = win->drawContext();
        CHECK(context.screenRect.left == 14 && context.screenRect.top == 24);
        CHECK(context.screenRect.right == 188 && context.screenRect.bottom == 116);
        CHECK(context.documentRect.left == 0 && context.documentRect.top == 0);
        CHECK(context.documentRect.right == 174 && context.documentRect.bottom == 92);
        CHECK(context.surface == static_cast<html::IHtmlSurface*>(&canvas));
    }

    cIGZWinHTML* view = nullptr;
    CHECK(win->QueryInterface(1, reinterpret_cast<void**>(&view)) && view == static_cast<cIGZWinHTML*>(win));
    view->Release();
    view = nullptr;
    CHECK(win->QueryInterface(kGZIID_cIGZWinHTML, reinterpret_cast<void**>(&view)) && view != nullptr);
    view->Release();
    view = nullptr;
    cIGZWin* plain = nullptr;
    CHECK(win->QueryInterface(kGZIID_cIGZWin, reinterpret_cast<void**>(&plain)) && plain == static_cast<cIGZWin*>(win));
    plain->Release();
    plain = nullptr;
    cIGZMessageTarget2* target = nullptr;
    CHECK(win->QueryInterface(kGZIID_cIGZMessageTarget2, reinterpret_cast<void**>(&target)) && target != nullptr);
    target->Release();
    target = nullptr;
    CHECK(!win->QueryInterface(0xdeadbeef, reinterpret_cast<void**>(&plain)));

    // ---- loading a document: parse, notify, layout, paint
    const std::string tallPage = TallPage();
    CHECK(win->SetDocument(tallPage, cRZString("http://example.com/dir/page.html")));
    CHECK(win->GetLoadStatus() == kHtmlLoadLoaded);
    CHECK(win->document() != nullptr && win->document()->title() == "Help");
    {
        const cGZMessage* notification = parent->Last(kWinHtmlMsgLoadStatus);
        CHECK(notification != nullptr && notification->param2 == 2 && notification->param3 == 1);
    }
    std::string loaded;
    CHECK(win->GetDocument(loaded) && loaded == tallPage);
    CHECK(host.bullets == 2);   // the two <li> elements asked the host for a bullet each
    CHECK(win->document()->Body() != nullptr);
    CHECK(!win->document()->IsPageLayoutDone());

    // The first GZPaint lays the page out and adds the scrollbars.
    const int notificationsBefore = static_cast<int>(parent->got.size());
    CHECK(win->GZPaint());
    // Adding the scrollbar marks the page layout dirty again (AddScrollBar), so the second paint settles it.
    CHECK(!win->document()->IsPageLayoutDone());
    CHECK(parent->got.size() >= static_cast<std::size_t>(notificationsBefore));
    CHECK(win->GZPaint());
    CHECK(win->document()->IsPageLayoutDone());
    CHECK(canvas.begins == canvas.ends);
    CHECK(canvas.fills >= 1);                    // no background image: the fill colour path
    CHECK(canvas.lastFill == 0xff102030u);       // BODY BGCOLOR
    CHECK(canvas.outlines >= 1 && canvas.lastOutline == 0);   // the outline flag is on by default
    CHECK(canvas.lines >= 1);                    // the <hr>
    CHECK(win->GetFillColorRGB() == 0xff102030u);   // SetFillColor(cRZColor) stores the packed colour

    // The tall page needs the vertical scrollbar; it was created by the layout and sized at the right edge.
    CHECK(factory.created == 1);
    gz::cIGZWinScrollbar* vbar = factory.bars.empty() ? nullptr : factory.bars.back();
    CHECK(vbar != nullptr && vbar->AsIGZWin()->GetID() == 0 && vbar->GetOrientation() == 2);
    int minimum = -1, maximum = -1;
    CHECK(vbar->GetMinMax(minimum, maximum));
    gz::cRZSize documentSize;
    win->GetDocumentSize(documentSize);
    CHECK(documentSize.width > 0 && documentSize.height > 92);
    CHECK(minimum == 0 && maximum == documentSize.height - 92);
    std::uint32_t lines = 0, pages = 0;
    vbar->GetLineAndPageCount(lines, pages);
    CHECK(lines == static_cast<std::uint32_t>(documentSize.height / 20));
    CHECK(pages == static_cast<std::uint32_t>(documentSize.height / 92 + 1));
    CHECK(vbar->GetValue() == 0);
    CHECK(vbar->AsIGZWin()->GetL() == 185 && vbar->AsIGZWin()->GetT() == 0 && vbar->AsIGZWin()->GetR() == 200 &&
          vbar->AsIGZWin()->GetB() == 100);
    CHECK(factory.bars.size() == 1);

    // Further paints reuse the layout (no new scrollbar) and draw the elements again.
    const int fillsBefore = canvas.fills;
    CHECK(win->GZPaint());
    CHECK(factory.created == 1);
    CHECK(canvas.fills > fillsBefore);

    // ---- scrolling: positions, clamping, the scrollbar command
    int x = -1, y = -1;
    win->GetScrollPosition(x, y);
    CHECK(x == 0 && y == 0);
    win->SetScrollPosition(0, 100);
    win->GetScrollPosition(x, y);
    CHECK(x == 0 && y == 100);
    CHECK(vbar->GetValue() == 100);
    win->SetScrollPosition(-50, -50);
    win->GetScrollPosition(x, y);
    CHECK(x == 0 && y == 0);
    win->SetScrollPosition(10000, 10000);   // clamped to (docWidth - viewWidth, docHeight - viewHeight)
    win->GetScrollPosition(x, y);
    CHECK(x == 0);
    CHECK(y == documentSize.height - 92);

    // ScrollToAnchor: an anchor that is already visible keeps the scroll, one below the viewport is brought into view.
    CHECK(win->ScrollToAnchor(cRZString("p10")));
    win->GetScrollPosition(x, y);
    {
        cRZRect anchorArea{};
        CHECK(win->GetAnchorPosition(cRZString("p10"), anchorArea));
        CHECK(y <= anchorArea.top && anchorArea.bottom <= y + 92);
    }
    CHECK(!win->ScrollToAnchor(cRZString("missing")));
    win->SetScrollPosition(0, 0);

    // A scrollbar command (0x887113a3, p2 = the bar's window id) takes the bar's value as the scroll position.
    CHECK(vbar->SetValue(40));
    CHECK(win->GZOnCommand(0x887113a3, 0));
    win->GetScrollPosition(x, y);
    CHECK(y == 40);
    CHECK(win->GZOnCommand(0x887113a3, 99));   // unknown bar id: unchanged
    win->GetScrollPosition(x, y);
    CHECK(y == 40);
    CHECK(win->GZOnCommand(3, 0));

    // ---- keyboard navigation (the scrolling keys are enabled by default, flag 0x10)
    win->SetScrollPosition(0, 100);
    CHECK(win->GZOnKeyDown(0x21, 0));   // Page Up: 90% of the visible height
    win->GetScrollPosition(x, y);
    CHECK(y == 100 - (92 * 9) / 10);
    CHECK(win->GZOnKeyDown(0x22, 0));   // Page Down
    win->GetScrollPosition(x, y);
    CHECK(y == 100);
    CHECK(win->GZOnKeyDown(0x26, 0));   // Up: a line (visible height / 25)
    win->GetScrollPosition(x, y);
    CHECK(y == 100 - 92 / 25);
    CHECK(win->GZOnKeyDown(0x28, 0));   // Down
    win->GetScrollPosition(x, y);
    CHECK(y == 100);
    CHECK(win->GZOnKeyDown(0x23, 0));   // End
    win->GetScrollPosition(x, y);
    CHECK(y == documentSize.height - 92);
    // Quirk kept: End writes the visible *height* into the visible right edge (0x100567e07).
    CHECK(win->drawContext().documentRect.right == 92);
    CHECK(win->GZOnKeyDown(0x24, 0));   // Home
    win->GetScrollPosition(x, y);
    CHECK(x == 0 && y == 0);
    CHECK(win->drawContext().documentRect.right == 92);   // the quirk's value survives Home (left was 0)

    // Flag 0x10 gates the keys.
    win->ClearFlag(kHtmlKeyboardScroll);
    win->SetScrollPosition(0, 100);
    CHECK(win->GZOnKeyDown(0x26, 0));
    win->GetScrollPosition(x, y);
    CHECK(y == 100);
    win->SetFlag(kHtmlKeyboardScroll);

    // The target sees every key first and can claim it.
    parent->claimKeys = true;
    win->SetScrollPosition(0, 100);
    CHECK(win->GZOnKeyDown(0x26, 0));
    win->GetScrollPosition(x, y);
    CHECK(y == 100);
    parent->claimKeys = false;
    const cGZMessage* keyMessage = parent->Last(kWinHtmlMsgKeyDown);
    CHECK(keyMessage != nullptr && keyMessage->param2 == 0x26 && keyMessage->param1 == reinterpret_cast<std::uint64_t>(win));

    // ---- the mouse wheel: 100 units per line, the flag is switched on
    win->SetScrollPosition(0, 100);
    CHECK(win->GZOnMouseWheel(0, 0, 0, 250));
    win->GetScrollPosition(x, y);
    CHECK(y == 100 - 2 * (92 / 25));
    CHECK(win->GZOnMouseWheel(0, 0, 0, -250));
    win->GetScrollPosition(x, y);
    CHECK(y == 100);
    CHECK(win->GZOnMouseWheel(0, 0, 0, 50));   // below one notch: no scroll
    win->GetScrollPosition(x, y);
    CHECK(y == 100);
    win->ClearFlag(kHtmlKeyboardScroll);
    CHECK(win->GZOnMouseWheel(0, 0, 0, 10));
    CHECK(win->IsFlagSet(kHtmlKeyboardScroll));   // the wheel always turns the keys on
    win->SetScrollPosition(0, 0);

    // ---- scrollbar images
    // Heap-allocated: the window takes a reference and the scrollbar one more (a stack object must never reach 0).
    // 120x100 is cut into twelve 10-pixel cells; the vertical bar uses the second half (cells 6..11).
    auto* image = new cGZBuffer();
    image->AddRef();
    CHECK(image->Init(120, 100, cGZBufferColorType{kBufferColorXRGB8888, 0x20}));
    CHECK(win->SetScrollBarImageVertical(image, cRZRect{0, 0, 0, 0}));
    cIGZBuffer* storedImage = nullptr;
    cRZRect storedArea{};
    CHECK(win->GetScrollBarImageVertical(storedImage, storedArea));
    CHECK(storedImage == image && storedArea.left == 0 && storedArea.right == 0);
    cRZRect barImageArea{};
    cIGZBuffer* barImage = nullptr;
    int barImageFlags = -1;
    CHECK(vbar->GetImage(barImageArea, barImage, barImageFlags));
    // The vertical segment of the 120x100 image is cells 6..11 wide (60..70) and the full height.
    CHECK(barImage == image && barImageArea.left == 60 && barImageArea.right == 70 && barImageArea.bottom == 100);
    CHECK(vbar->AsIGZWin()->GetW() == 10);
    CHECK(win->SetScrollBarImageVertical(nullptr, cRZRect{1, 2, 3, 4}));
    CHECK(!win->GetScrollBarImageVertical(storedImage, storedArea));
    // Quirk kept: clearing the window's image does not clear the scrollbar's (the original returns before ImageSetup).
    CHECK(vbar->GetImage(barImageArea, barImage, barImageFlags));
    image->Release();   // the window keeps its own reference

    // ---- horizontal page: the horizontal bar is opt-in (flag 4 is clear in the default 0xfa), so enable it and
    // check that the quirk pushes the horizontal range into the *vertical* bar.
    win->SetFlag(kHtmlHScroll);
    CHECK(win->GetFlags() == 0xfe);
    CHECK(win->SetDocument(WidePage(), cRZString("http://example.com/dir/wide.html")));
    CHECK(win->GetLoadStatus() == kHtmlLoadLoaded);
    CHECK(win->GZPaint());
    gz::cRZSize wideSize;
    win->GetDocumentSize(wideSize);
    CHECK(wideSize.width > 174);
    CHECK(factory.created == 3);   // vertical + horizontal (the old two were released by InternalSetDocument)
    CHECK(factory.bars.size() == 3);
    gz::cIGZWinScrollbar* wideVertical = factory.bars.size() == 3 ? factory.bars[1] : nullptr;
    gz::cIGZWinScrollbar* wideHorizontal = factory.bars.size() == 3 ? factory.bars[2] : nullptr;
    CHECK(wideVertical != nullptr && wideHorizontal != nullptr);
    if (wideVertical == nullptr || wideHorizontal == nullptr) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    CHECK(wideVertical->AsIGZWin()->GetID() == 0 && wideHorizontal->AsIGZWin()->GetID() == 1);
    // The horizontal branch of SetScrollBarValues writes the horizontal range and position into the vertical bar.
    CHECK(wideVertical->GetMinMax(minimum, maximum));
    CHECK(minimum == 0 && maximum == wideSize.width - 174);
    wideVertical->GetLineAndPageCount(lines, pages);
    CHECK(lines == static_cast<std::uint32_t>(wideSize.width / 0xf));
    CHECK(pages == static_cast<std::uint32_t>(wideSize.width / 174 + 1));
    win->SetScrollPosition(0, 0);
    CHECK(wideVertical->GetValue() == 0);
    // The horizontal bar was never given a range of its own (the quirk): it still has the slider default 0..0x20.
    int hmin = -1, hmax = -1;
    CHECK(wideHorizontal->GetMinMax(hmin, hmax));
    CHECK(hmin == 0 && hmax == 0x20);

    // Horizontal keys now move the visible window (the visible right edge starts at the viewport width).
    win->SetScrollPosition(0, 0);
    CHECK(win->GZOnKeyDown(0x27, 0));   // Right: visible width / 25
    win->GetScrollPosition(x, y);
    CHECK(x == 174 / 25);
    CHECK(win->GZOnKeyDown(0x25, 0));   // Left
    win->GetScrollPosition(x, y);
    CHECK(x == 0);
    // The horizontal image is cut into twelve cells along the width; the horizontal bar uses the first cell.
    auto* hImage = new cGZBuffer();
    hImage->AddRef();
    CHECK(hImage->Init(120, 100, cGZBufferColorType{kBufferColorXRGB8888, 0x20}));
    CHECK(win->SetScrollBarImageHorizontal(hImage, cRZRect{0, 0, 120, 100}));
    cRZRect horizontalArea{};
    cIGZBuffer* horizontalImage = nullptr;
    int horizontalFlags = 0;
    CHECK(wideHorizontal->GetImage(horizontalArea, horizontalImage, horizontalFlags));
    CHECK(horizontalImage == hImage && horizontalArea.right == 10 && horizontalArea.bottom == 100);
    CHECK(win->SetScrollBarImageHorizontal(nullptr, cRZRect{0, 0, 0, 0}));
    hImage->Release();   // the horizontal bar keeps its own reference until it is released below
    win->ClearFlag(kHtmlHScroll);
    CHECK(win->GetFlags() == 0xfa);

    // ---- anchors and links
    CHECK(win->SetDocument(LinkPage(), cRZString("http://example.com/dir/page.html")));
    CHECK(win->GZPaint());
    html::cHTMLAnchor* anchor = win->document()->FindAnchor(cRZString("anchor1"));
    CHECK(anchor != nullptr && anchor->laidOut());
    cRZRect anchorRect{};
    CHECK(win->GetAnchorPosition(cRZString("anchor1"), anchorRect));
    CHECK(anchorRect.left == anchor->drawRect().left && anchorRect.top == anchor->drawRect().top &&
          anchorRect.right == anchor->drawRect().right && anchorRect.bottom == anchor->drawRect().bottom);
    CHECK(!win->GetAnchorPosition(cRZString("missing"), anchorRect));

    win->GetScrollPosition(x, y);
    const int screenLeft = win->drawContext().screenRect.left;
    const int screenTop = win->drawContext().screenRect.top;
    const int clickX = anchorRect.left + 2 - x + screenLeft;
    const int clickY = anchorRect.top + 2 - y + screenTop;
    CHECK(mgr.capture == nullptr);
    win->GZOnMouseMove(clickX, clickY, 0);
    CHECK(cursors.current == &cursors.hand);
    win->GZOnMouseMove(2, 2, 0);
    CHECK(cursors.current == &cursors.arrow);
    CHECK(win->GZOnMouseDownL(clickX, clickY, 0));
    const std::size_t startedBefore = web.started.size();
    CHECK(win->GZOnMouseUpL(clickX, clickY, 0));
    CHECK(parent->linkText == "http://example.com/dir/next.html");   // the absolute URL is preferred
    CHECK(web.started.size() == startedBefore + 1);
    CHECK(web.started.back() == "http://example.com/dir/next.html");
    CHECK(win->GetLoadStatus() == kHtmlLoadLoading);

    // ---- link navigation is gated by the message target and by flag 0x20
    win->SetDocument(LinkPage(), cRZString("http://example.com/dir/page.html"));
    CHECK(win->GZPaint());
    anchor = win->document()->FindAnchor(cRZString("anchor1"));
    CHECK(anchor != nullptr);
    win->GetScrollPosition(x, y);
    const int linkX = anchor->drawRect().left + 2 - x + win->drawContext().screenRect.left;
    const int linkY = anchor->drawRect().top + 2 - y + win->drawContext().screenRect.top;
    const std::size_t startedAfterLoad = web.started.size();
    parent->allowLinkJump = true;
    win->ClearFlag(kHtmlFollowLinks);
    CHECK(win->GZOnMouseUpL(linkX, linkY, 0));
    CHECK(web.started.size() == startedAfterLoad);   // no flag: the notification is sent, the link is not followed
    win->SetFlag(kHtmlFollowLinks);
    parent->allowLinkJump = false;
    CHECK(win->GZOnMouseUpL(linkX, linkY, 0));
    CHECK(web.started.size() == startedAfterLoad);   // the target refused
    parent->allowLinkJump = true;

    // ---- URL handling: SetURL / GetURL / GetNewURL / SetRelativeURL / SetBaseURL / CancelCurrentLoadRequest
    win->SetDocument(TallPage(), cRZString("http://example.com/dir/page.html"));
    cRZString currentUrl;
    win->GetURL(currentUrl);                       // left over from the link jump above
    const std::string urlBefore = currentUrl.str();
    CHECK(!win->SetURL(cRZString(""), false));     // an empty URL is rejected
    CHECK(win->SetURL(cRZString("http://example.com/dir/a.html"), false));
    CHECK(win->GetLoadStatus() == kHtmlLoadLoading);
    CHECK(web.started.back() == "http://example.com/dir/a.html");
    {
        cRZString newUrl;
        CHECK(win->GetNewURL(newUrl) && std::string(newUrl.Data()) == "http://example.com/dir/a.html");
        cRZString stillCurrent;
        CHECK(win->GetURL(stillCurrent) && stillCurrent.str() == urlBefore);   // the current URL changes on arrival only
    }
    // CancelCurrentLoadRequest answers "there was nothing to cancel": false when it abandoned the request.
    CHECK(!win->CancelCurrentLoadRequest());
    CHECK(win->GetLoadStatus() == kHtmlLoadIdle);
    {
        const cGZMessage* notification = parent->Last(kWinHtmlMsgLoadStatus);
        CHECK(notification != nullptr && notification->param2 == 0 && notification->param3 == 1);
    }
    CHECK(win->CancelCurrentLoadRequest());        // not loading: still true

    // Load a.html for real (the completion message needs status == loading).
    CHECK(win->SetURL(cRZString("http://example.com/dir/a.html"), false));
    {
        const std::string page = "<html><body><p>page a</p></body></html>";
        html::tDownloadedJob job;
        job.url = "http://example.com/dir/a.html";
        job.isFile = false;
        job.data = page;
        cRZMessage2Standard message;
        message.SetType(kWinHtmlMsgDownloaded);
        message.SetData2(1);
        message.SetVoid3(&job);
        CHECK(win->DoMessage(&message));
        CHECK(win->GetLoadStatus() == kHtmlLoadLoaded);
        CHECK(win->GetDocument(loaded) && loaded == page);
        const cGZMessage* notification = parent->Last(kWinHtmlMsgLoadStatus);
        CHECK(notification != nullptr && notification->param2 == 2 && notification->param3 == 1);
    }
    CHECK(win->GetURL(currentUrl) && currentUrl.str() == "http://example.com/dir/a.html");

    // Requesting the URL that is already current while another one loads is the only "no-op" case.
    CHECK(win->SetURL(cRZString("http://example.com/dir/b.html"), false));
    const std::size_t startedWhileLoading = web.started.size();
    CHECK(win->SetURL(cRZString("http://example.com/dir/a.html"), false));
    CHECK(web.started.size() == startedWhileLoading);
    {
        cRZString newUrl;
        CHECK(win->GetNewURL(newUrl) && newUrl.str() == "http://example.com/dir/b.html");   // the pending URL is kept
    }
    // A different URL while loading starts a new request (the old one is only notified, not cancelled in the web system).
    CHECK(win->SetURL(cRZString("http://example.com/dir/c.html"), false));
    CHECK(win->GetLoadStatus() == kHtmlLoadLoading);
    CHECK(web.started.back() == "http://example.com/dir/c.html");
    {
        const std::string page = "<html><body><p>page c</p></body></html>";
        html::tDownloadedJob job;
        job.url = "http://example.com/dir/c.html";
        job.data = page;
        cRZMessage2Standard message;
        message.SetType(kWinHtmlMsgDownloaded);
        message.SetData2(1);
        message.SetVoid3(&job);
        CHECK(win->DoMessage(&message));
        CHECK(win->GetLoadStatus() == kHtmlLoadLoaded);
        CHECK(win->GetURL(currentUrl) && currentUrl.str() == "http://example.com/dir/c.html");
    }
    CHECK(win->SetBaseURL(cRZString("http://example.com/dir/")));
    // SetRelativeURL appends to the current URL string and resolves that; a URL with a scheme keeps the concatenation.
    CHECK(win->SetRelativeURL(cRZString("rel.html")));
    {
        cRZString newUrl;
        CHECK(win->GetNewURL(newUrl) && newUrl.str() == "http://example.com/dir/c.htmlrel.html");
    }
    // A failure message while loading falls back to "loaded" when a document exists, and notifies the target.
    {
        cRZMessage2Standard message;
        message.SetType(kWinHtmlMsgDownloaded);
        message.SetData2(0);
        message.SetVoid3(nullptr);
        CHECK(win->DoMessage(&message));
        CHECK(win->GetLoadStatus() == kHtmlLoadLoaded);
        const cGZMessage* notification = parent->Last(kWinHtmlMsgLoadStatus);
        CHECK(notification != nullptr && notification->param2 == 2 && notification->param3 == 1);
    }
    {
        cRZMessage2Standard other;
        other.SetType(3);
        CHECK(!win->DoMessage(&other));
    }

    // ---- the body background image: the blit goes through the canvas, fixed backgrounds do not scroll
    CHECK(win->SetDocument(ImagePage(true), cRZString("http://example.com/dir/img.html")));
    CHECK(win->GZPaint());
    CHECK(!web.started.empty());
    {
        html::tDownloadedJob job;
        job.url = web.started.back();
        job.isFile = true;
        job.data = "/tmp/bg.gif";
        CHECK(win->document()->DoMessage(html::cHTMLDocument::kMessageDownloaded, &job));
        CHECK(images.loads >= 1);
        CHECK(win->document()->Body() != nullptr && win->document()->Body()->background() == &images.bitmap);
    }
    CHECK(win->GZPaint());
    CHECK(canvas.tiles >= 1);
    CHECK(canvas.lastTileImage == &images.bitmap);
    CHECK(canvas.lastTileOffsetX == 0 && canvas.lastTileOffsetY == 0);   // fixed background
    CHECK(canvas.lastTileSource.right == 16 && canvas.lastTileSource.bottom == 16);
    CHECK(canvas.lastTileDest.left == 10 && canvas.lastTileDest.top == 20);   // the draw area, not the paint area

    // A floating background is tiled from an offset that depends on the scroll position.
    CHECK(win->SetDocument(ImagePage(false), cRZString("http://example.com/dir/img.html")));
    CHECK(win->GZPaint());
    {
        html::tDownloadedJob job;
        job.url = web.started.back();
        job.isFile = true;
        job.data = "/tmp/bg.gif";
        CHECK(win->document()->DoMessage(html::cHTMLDocument::kMessageDownloaded, &job));
    }
    win->SetScrollPosition(0, 7);
    CHECK(win->GZPaint());
    CHECK(canvas.tiles >= 2);
    CHECK(canvas.lastTileOffsetX == 0);
    CHECK(canvas.lastTileOffsetY == 16 - (7 % 16));

    // ---- the blink timer: <blink> starts the 500 ms redraw, a page without it stops it
    CHECK(win->SetDocument(BlinkPage(), cRZString("http://example.com/dir/blink.html")));
    CHECK(g_activeTimerId != 0);
    CHECK(g_timerInterval == 500);
    CHECK(g_timerCallback != nullptr && g_timerParam != nullptr);
    {
        const int invalidationsBefore = parent->invalidations;
        if (g_timerCallback != nullptr) g_timerCallback(g_timerInterval, g_timerParam);
        CHECK(parent->invalidations > invalidationsBefore);   // the timer invalidates the window (and its parents)
    }
    CHECK(win->SetDocument(TallPage(), cRZString("http://example.com/dir/page.html")));
    CHECK(g_activeTimerId == 0);

    // ---- flags, gutters, text-size table
    win->SetFlags(0xfa);
    win->ClearFlag(kHtmlOutline);
    CHECK(!win->IsFlagSet(kHtmlOutline));
    win->SetFlag(kHtmlOutline);
    CHECK(win->IsFlagSet(kHtmlOutline));
    CHECK(win->GetFlags() == 0xfa);

    win->SetGutters(5, 6, 7, 8);
    win->GetGutters(left, top, right, bottom);
    CHECK(left == 5 && top == 6 && right == 7 && bottom == 8);
    CHECK(win->drawContext().screenRect.left == 15 && win->drawContext().screenRect.top == 26);
    CHECK(win->drawContext().screenRect.right == 203 && win->drawContext().screenRect.bottom == 112);
    CHECK(win->drawContext().documentRect.right == 188 && win->drawContext().documentRect.bottom == 86);
    win->SetGutters(4, 4, 0x16, 4);

    const std::uint32_t sizes[7] = {9, 11, 13, 15, 19, 25, 37};
    win->SetTextSizeTable(sizes);
    CHECK(win->SetDocument(TallPage(), cRZString("http://example.com/dir/page.html")));
    std::uint32_t applied[7] = {};
    CHECK(win->document()->GetFontSizeTable(applied, 7) == 7);
    for (int i = 0; i < 7; ++i) CHECK(applied[i] == sizes[i]);

    // The scrollbar factory failing drops the permission flags but still invalidates (quirk kept).
    factory.fail = true;
    win->SetDocument(TallPage(), cRZString("http://example.com/dir/page.html"));
    CHECK(win->GZPaint());
    CHECK(!win->IsFlagSet(kHtmlVScroll));
    CHECK(!win->IsFlagSet(kHtmlHScroll));
    win->SetFlags(0xfa);
    factory.fail = false;

    // A page that fits needs no scrollbar; RemoveScrollBar takes it away again.
    CHECK(win->SetDocument("<html><body><p>short</p></body></html>", cRZString("http://example.com/dir/s.html")));
    CHECK(win->GZPaint());
    win->GetDocumentSize(documentSize);
    win->SetDocument(TallPage(), cRZString("http://example.com/dir/page.html"));
    CHECK(win->GZPaint());
    const std::size_t barsBeforeRemove = factory.bars.size();
    win->RemoveScrollBar(true, true);
    CHECK(factory.bars.size() == barsBeforeRemove);   // the window drops both bars; the factory only counts creations

    // SetArea: a new size recomputes the paint area and dirties the layout; the same size only invalidates the parent
    // (quirk kept).
    {
        const int invalidationsBefore = parent->invalidations;
        win->SetArea(10, 20, 210, 120);
        CHECK(parent->invalidations > invalidationsBefore);
    }
    win->SetArea(10, 20, 410, 320);
    CHECK(win->drawContext().screenRect.right == 388 && win->drawContext().screenRect.bottom == 316);
    CHECK(!win->document()->IsPageLayoutDone());

    // ---- SetWWWSystem, SetMissingBitmapPath and the no-document paths
    CHECK(win->SetWWWSystem(&web));
    win->SetMissingBitmapPath(cRZString("missing.jpg"));
    html::cHTMLElement* none = nullptr;
    CHECK(!win->PickElement(none, -10, -10));   // outside the window
    CHECK(win->GZOnMouseDownL(-10, -10, 0));
    CHECK(win->GZOnMouseUpL(-10, -10, 0));
    CHECK(win->Shutdown());
    {
        gz::cRZSize emptySize{1, 1};
        win->GetDocumentSize(emptySize);
        CHECK(emptySize.width == 0 && emptySize.height == 0);
        int noX = 1, noY = 1;
        win->GetScrollPosition(noX, noY);
        win->SetScrollPosition(0, 0);   // no document: nothing to clamp against
        CHECK(!win->GetDocument(loaded));
        CHECK(!win->GetAnchorPosition(cRZString("x"), anchorRect));
        CHECK(!win->ScrollToAnchor(cRZString("x")));
        win->GZPaint();                 // not loaded: nothing to paint
    }
    CHECK(win->SetWWWSystem(nullptr));
    CHECK(!win->SetURL(cRZString("http://example.com/dir/x.html"), false));   // no web system
    CHECK(win->SetWWWSystem(&web));

    // ---- Shutdown releases the document / bars / cursors; Init is possible again afterwards.
    CHECK(win->document() == nullptr);
    CHECK(win->Shutdown());
    CHECK(win->GetLoadStatus() == kHtmlLoadLoaded);   // the original does not reset the status in Shutdown
    CHECK(win->Init());

    win->Release();
    parent->Release();
    SetWinHtmlHost(nullptr);
    gz::SetWinCtrlFactory(nullptr);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("gz_winhtml: ok\n");
    return 0;
}
