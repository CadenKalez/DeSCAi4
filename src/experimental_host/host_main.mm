// EARLY TEST / EXPERIMENTAL HOST — not recovered game code. See README_EARLY_TEST.md.
// Cocoa window + event pump over the recovered window manager and window tree, painted by the recovered software draw
// engine into a CPU buffer that is shown through a layer. Usage: sc4_host [--screenshot out.png] [--frames N]
#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <vector>
#include <string>

#include "experimental_host/HostBoot.h"
#include "experimental_host/HostFonts.h"
#include "experimental_host/HostGraphics.h"
#include "experimental_host/HostUi.h"
#include "dbpf/DBPFReader.h"
#include "gz/DrawFontRenderer.h"
#include "gz/BufferFontRenderer.h"
#include "gz/GZFont.h"
#include "gz/WinFont.h"
#include "gz/cIGZMessage2.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/WinSplashScreen.h"
#include <SDL.h>   // the test stub: its timers never fire by themselves, the host fires them (see PumpTimers)
#include "experimental_host/HostWinMgr.h"
#include "gz/WinFlatRect.h"

namespace {

constexpr int kW = 1024, kH = 768;

// FINDING (early test): with the draw system active, cGZWin::SetAreaToDrawTo leaves drawArea_.bottom unrebased
// (Ghidra shows `bottom -= *(this+0x34)` after the 8-byte store that zeroed it: the same behaviour is recovered in
// Win.cpp). Any window whose top is not 0 then paints down to the bottom of its parent. The game's own windows would
// show it too, so the recovered rule probably needs another look; this host just repairs the two rectangles itself.
class HostRect : public gz::cGZWinFlatRect {
public:
    using gz::cGZWinFlatRect::cGZWinFlatRect;
    void SetAreaToDrawTo() override {
        gz::cGZWinFlatRect::SetAreaToDrawTo();
        const int h = area_.bottom - area_.top;
        drawArea_.bottom = drawArea_.top + h;
        accumulatedArea_.bottom = accumulatedArea_.top + h;
    }
};

// A flat rectangle that reacts to the mouse the way a button would: hover tint, press tint, click counter.
class TestButton final : public HostRect {
public:
    TestButton(cIGZWinMgr* m, std::uint32_t base, std::uint32_t hover, std::uint32_t press)
        : HostRect(m), base_(base), hover_(hover), press_(press) { Tint(base_); }
    int clicks = 0;
    bool DoMessage(cGZMessage& message) override {
        switch (message.type) {
        case kGZMsgMouseEnter: Tint(hover_); InvalidateSelfAndParents(); return true;
        case kGZMsgMouseExit: Tint(base_); InvalidateSelfAndParents(); return true;
        case kGZMsgMouseDownL: Tint(press_); GetWindowManager()->GZSetCapture(this); InvalidateSelfAndParents(); return true;
        case kGZMsgMouseUpL:
            GetWindowManager()->GZReleaseCapture(this);
            ++clicks; Tint(hover_); InvalidateSelfAndParents();
            std::printf("button %u clicked (%d)\n", GetID(), clicks);
            return true;
        default: return HostRect::DoMessage(message);
        }
    }
private:
    void Tint(std::uint32_t rgb) { SetFillColor(std::uint8_t(rgb >> 16), std::uint8_t(rgb >> 8), std::uint8_t(rgb)); }
    std::uint32_t base_, hover_, press_;
};

exphost::HostGraphics* gGraphics;
exphost::FreeTypeFontHost* gFontHost;
cGZFont* gFont;
const char* kLabels[] = {"New City", "Load City", "Options", "Quit"};

// Text goes through the recovered cGZFont (glyph atlas, metrics, renderer) onto the primary buffer after the window tree.
// The draw-context font renderer's collaborators (it keeps a glyph atlas and an inner buffer renderer).
struct HostDrawFontHost final : IDrawFontRendererHost {
    cIGZFontRenderer* CreateBufferFontRenderer() override { auto* r = new cGZBufferFontRenderer(); r->AddRef(); return r; }
    cIGZBuffer* CreateBuffer() override { auto* b = new cGZBuffer(); b->AddRef(); return b; }
    void NativeColorToRGB(std::uint32_t n, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override { r = n >> 16; g = n >> 8; b = n; }
} gDrawFontHost;

bool InitFont() {
    gFontHost = new exphost::FreeTypeFontHost();
    gFont = new cGZFont(gFontHost);
    gFont->AddRef();
    cRZString path("/System/Library/Fonts/Supplemental/Arial.ttf");
    gFont->SetFilePath(path);
    gFont->SetSize(16);
    gFont->SetAntialiasing(3);   // coverage bytes blended as alpha
    gFont->SetCodePage(8);       // the scripts' text is UTF-8
    gFont->SetRenderer(new cGZDrawFontRenderer(&gDrawFontHost));   // draws through the surface's context, as in the game
    return gFont->Init();
}


// The one font the host has (Arial through the recovered cGZFont) as the window system's font object: every style id
// maps to it until the game's font style script is wired.
class HostWinFont final : public gz::IWinFont {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    cIGZFont* AsFont() override { return gFont; }
    std::uint32_t DefaultTextColor() override { return 0xffffffffu; }
    std::uint32_t DefaultShadowColor() override { return 0xff000000u; }
    std::int16_t LineHeight(int, int) override { return std::int16_t(gFont->GetLineHeight()); }
    int CountLines(const char* t, std::uint32_t n, int width) override { return int(gFont->CalculateNumberOfLines(t, n, std::uint32_t(width))); }
    int WrappedWidth(const char* t, std::uint32_t n, int width) override { return int(gFont->CalculateWidthOfLines(t, n, std::uint32_t(width))); }
    void MeasureText(const char* t, std::uint32_t n, cRZRect& out) override { gFont->TextArea(t, n, out); }
    int TextWidth(const char* t, std::uint32_t n) override { return gFont->GetStringWidth(t, n); }
    void DrawText(IWinBuffer* target, int x, int y, const char* t, std::uint32_t n, std::uint32_t color, std::uint32_t color2,
                  const cRZRect* clip, int) override {
        gFont->DrawTextLine(static_cast<exphost::HostBuffer*>(target)->AsBuffer(), x, y, t, n, color & 0xffffffu,
                            color2 == gz::kWinUnsetTextColor ? kFontDefaultColor : (color2 & 0xffffffu), clip, nullptr);
    }
    void DrawTextBox(IWinBuffer* target, const cRZRect& box, const char* t, std::uint32_t n, std::uint32_t alignment,
                     std::uint32_t color, std::uint32_t color2, const cRZRect* clip, int) override {
        gFont->DrawTextParagraph(static_cast<exphost::HostBuffer*>(target)->AsBuffer(), box, t, n, int(alignment), color & 0xffffffu,
                                 color2 == gz::kWinUnsetTextColor ? kFontDefaultColor : (color2 & 0xffffffu), clip, nullptr);
    }
};
struct HostFontSource final : gz::IWinFontSource {
    HostWinFont font;
    gz::IWinFont* Font(std::uint32_t) override { return gFont ? &font : nullptr; }
    bool Available() override { return gFont != nullptr; }
} gFontSource;

void DrawLabels() {
    if (!gFont) return;
    cIGZBuffer* surface = gGraphics->Primary()->Buffer();
    for (int i = 0; i < 4; ++i) {
        const int len = int(std::strlen(kLabels[i]));
        const int w = gFont->GetStringWidth(kLabels[i], len);
        const int x = 312 + 40 + (320 - w) / 2;
        const int y = 184 + 40 + i * 80 + (60 - gFont->GetLineHeight()) / 2;
        gFont->DrawTextLine(surface, x, y, kLabels[i], len, 0x00f4f8ff, kFontDefaultColor, nullptr, nullptr);
    }
}
exphost::HostWinMgr* gMgr;

gz::cGZWinFlatRect* AddRect(cIGZWin* parent, int l, int t, int r, int b, std::uint32_t rgb, gz::cGZWinFlatRect* made = nullptr) {
    auto* w = made ? made : new HostRect(gMgr);
    w->Init();
    w->SetArea(l, t, r, b);
    w->SetFillColor(std::uint8_t(rgb >> 16), std::uint8_t(rgb >> 8), std::uint8_t(rgb));
    parent->ChildAdd(w);
    return w;
}

void BuildMenu() {
    cIGZWin* desktop = gMgr->GetMainWindow();
    desktop->SetArea(0, 0, kW, kH);
    AddRect(desktop, 0, 0, kW, kH, 0x10243c);                          // backdrop
    auto* panel = AddRect(desktop, 312, 184, 712, 584, 0x2a3f5f);       // menu panel
    panel->SetOutlineFlags(0xf);
    panel->SetOutlineColor(0x8fb0e0 >> 16 & 0xff, 0x8fb0e0 >> 8 & 0xff, 0x8fb0e0 & 0xff);
    static const char* names[] = {"New City", "Load City", "Options", "Quit"};
    for (int i = 0; i < 4; ++i) {
        auto* b = new TestButton(gMgr, 0x3d5a80, 0x5b7fb5, 0x9fc3f5);
        AddRect(panel, 40, 40 + i * 80, 360, 100 + i * 80, 0x3d5a80, b);
        b->SetID(100 + i);
        b->SetOutlineFlags(0xf);
        (void)names;
    }
    desktop->SetBufferToDrawToRecursive();
}

}  // namespace

@interface HostView : NSView
@property(nonatomic) CGFloat scale;
@end

@implementation HostView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }
- (NSPoint)gz:(NSEvent*)e { NSPoint p = [self convertPoint:e.locationInWindow fromView:nil]; return NSMakePoint(p.x * kW / self.bounds.size.width, p.y * kH / self.bounds.size.height); }
- (std::uint32_t)flags:(NSEvent*)e { return (e.modifierFlags & NSEventModifierFlagShift ? 4 : 0) | (e.modifierFlags & NSEventModifierFlagControl ? 8 : 0); }
- (void)post:(std::uint32_t)type e:(NSEvent*)e { NSPoint p = [self gz:e]; gMgr->GZWinMsgPost_Mouse(type, int(p.x), int(p.y), [self flags:e]); }
- (void)mouseMoved:(NSEvent*)e { [self post:kGZMsgMouseMove e:e]; }
- (void)mouseDragged:(NSEvent*)e { [self post:kGZMsgMouseMove e:e]; }
- (void)mouseDown:(NSEvent*)e { [self post:kGZMsgMouseDownL e:e]; }
- (void)mouseUp:(NSEvent*)e { [self post:kGZMsgMouseUpL e:e]; }
- (void)rightMouseDown:(NSEvent*)e { [self post:kGZMsgMouseDownR e:e]; }
- (void)rightMouseUp:(NSEvent*)e { [self post:kGZMsgMouseUpR e:e]; }
- (void)keyDown:(NSEvent*)e { gMgr->GZWinMsgPost_Keyboard(kGZMsgKeyDown, e.keyCode, 0); }
- (void)keyUp:(NSEvent*)e { gMgr->GZWinMsgPost_Keyboard(kGZMsgKeyUp, e.keyCode, 0); }
- (void)updateTrackingAreas {
    for (NSTrackingArea* a in self.trackingAreas) [self removeTrackingArea:a];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds
        options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect owner:self userInfo:nil]];
}
@end

static CGImageRef MakeImage() {
    auto* buf = gGraphics->Primary();
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef dp = CGDataProviderCreateWithData(nullptr, buf->Pixels(), size_t(buf->Stride()) * kH, nullptr);
    CGImageRef img = CGImageCreate(kW, kH, 8, 32, buf->Stride(), cs, kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst, dp, nullptr, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(dp); CGColorSpaceRelease(cs);
    return img;
}

static NSString* GameData() {
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents/GameData"];
}

// PNG bytes as a CPU buffer (ImageIO): ARGB8888 with straight (non-premultiplied) alpha, which is what the engine's
// alpha blits expect from a 32-bit image resource.
static cGZBuffer* DecodePngBytes(const std::vector<uint8_t>& png) {
    CGImageSourceRef src = CGImageSourceCreateWithData((__bridge CFDataRef)[NSData dataWithBytes:png.data() length:png.size()], nullptr);
    CGImageRef img = src ? CGImageSourceCreateImageAtIndex(src, 0, nullptr) : nullptr;
    if (!img) return nullptr;
    const std::uint32_t w = std::uint32_t(CGImageGetWidth(img)), h = std::uint32_t(CGImageGetHeight(img));
    auto* buffer = new cGZBuffer();
    buffer->AddRef();
    buffer->Init(w, h, cGZBufferColorType{kBufferColorARGB8888, 32});
    auto* bits = static_cast<std::uint8_t*>(buffer->GetColorSurfaceBits());
    const std::uint32_t stride = buffer->GetColorSurfaceStride();
    std::memset(bits, 0, std::size_t(stride) * h);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef cg = CGBitmapContextCreate(bits, w, h, 8, stride, cs, kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
    CGContextDrawImage(cg, CGRectMake(0, 0, w, h), img);
    CGContextRelease(cg); CGColorSpaceRelease(cs); CGImageRelease(img); CFRelease(src);
    for (std::uint32_t y = 0; y < h; ++y) {   // un-premultiply (B, G, R, A in memory)
        std::uint8_t* row = bits + std::size_t(y) * stride;
        for (std::uint32_t x = 0; x < w; ++x, row += 4) {
            const unsigned a = row[3];
            if (a != 0 && a != 255) for (int c = 0; c < 3; ++c) row[c] = std::uint8_t(std::min(255u, (row[c] * 255u + a / 2) / a));
        }
    }
    return buffer;
}

// A PNG resource of the game data as a CPU buffer (read with the recovered DBPF reader).
static cGZBuffer* LoadPngBuffer(const char* package, std::uint32_t type, std::uint32_t group, std::uint32_t instance) {
    dbpf::Package pkg;
    if (!pkg.Open([[GameData() stringByAppendingPathComponent:[NSString stringWithUTF8String:package]] UTF8String])) return nullptr;
    const dbpf::Entry* e = pkg.Find(type, group, instance);
    std::vector<uint8_t> png;
    if (!e || !pkg.Read(*e, png)) return nullptr;
    return DecodePngBytes(png);
}

// The window the splash script describes: GZWinGen 0xaaa9c9d9, 768 x 600, image {46a006b0, ea7f0eae} (Intro.dat).
class ImageWin final : public HostRect {
public:
    ImageWin(cIGZWinMgr* m, cGZBuffer* image) : HostRect(m), image_(image) {}
    bool GZPaint() override {
        if (!image_) return true;
        auto* ctx = static_cast<gz::cIGZDrawContext*>(drawContext_);
        const cRZRect all{0, 0, int(image_->Width()), int(image_->Height())};
        ctx->Blt(image_, all, drawArea_);
        return true;
    }
private:
    cGZBuffer* image_;
};

// SL::spMS2: posts are delivered by the host's frame loop.
struct HostMessageServer2 final : cIGZMessageServer2 {
    struct Post { cIGZMessage2* message; cIGZMessageTarget2* target; };
    std::vector<Post> queue;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool MessageSend(cIGZMessage2*) override { return true; }
    bool MessagePost(cIGZMessage2*, bool) override { return true; }
    bool AddNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool RemoveNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool GeneralMessagePostToTarget(cIGZMessage2* m, cIGZMessageTarget2* t) override { m->AddRef(); t->AddRef(); queue.push_back({m, t}); return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return std::uint32_t(queue.size()); }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return 1; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
    void Deliver() {
        auto pending = std::move(queue); queue.clear();
        for (auto& p : pending) { p.target->DoMessage(p.message); p.message->Release(); p.target->Release(); }
    }
};

enum class Phase { Splash, Menu };
Phase gPhase = Phase::Menu;
sc4::cSC4WinSplashScreen* gSplash = nullptr;
bool gToMenu = false;
cGZBuffer* gSplashImage = nullptr;
HostMessageServer2 gMS2;

// The splash screen is the recovered cSC4WinSplashScreen; this is what it reaches outside itself.
struct SplashHost final : sc4::ISplashScreenHost {
    struct Unknown final : cIGZUnknown {
        bool QueryInterface(std::uint32_t, void**) override { return false; }
        std::uint32_t AddRef() override { return 1; }
        std::uint32_t Release() override { return 1; }
    } graphics;
    cIGZWin* CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent, std::uint32_t id) override {
        // The script (SimCity_1.dat 0/96a006b0/8aa9aa14) is a 768x600 window with the Intro.dat picture; the script
        // interpreter is not wired to the host yet, so the host builds that one window itself.
        if (cIGZWin* real = exphost::CreateWindowFromScript(key, parent, id)) { std::printf("splash: window built from the game's UI script\n"); return real; }
        std::printf("splash: the UI script path failed, using the host's own window\n");
        if (key.group != sc4::kSplashScriptGroup || key.instance != sc4::kSplashScriptInstance) return nullptr;
        gSplashImage = LoadPngBuffer("Intro.dat", 0x856ddbac, 0x46a006b0, 0xea7f0eae);
        auto* w = new ImageWin(gMgr, gSplashImage);
        w->Init();
        w->SetArea(0, 0, 768, 600);
        w->SetID(id);
        parent->ChildAdd(w);
        w->AddRef();   // the caller's reference
        return w;
    }
    cIGZUnknown* GraphicSystemObject() override { return &graphics; }
    cIGZMessageServer2* MessageServer2() override { return &gMS2; }
    cIGZMessage2Standard* CreateStandardMessage() override { auto* m = new cRZMessage2Standard(); m->AddRef(); return m; }
    void ClearScreenToBlack() override { gGraphics->Primary()->Buffer()->Fill(0, nullptr); }
    void DestroyCurrentScreen() override {}                       // the swap happens after the message is delivered
    void CreateRegionScreen() override { gToMenu = true; }         // (the region screen is not wired: the test menu stands in)
} gSplashHost;

// The SDL stub's timers never fire by themselves: fire the armed one when its period has passed.
static void PumpTimers() {
    static double armedAt = -1; static int armedId = 0;
    const double now = [[NSDate date] timeIntervalSince1970] * 1000.0;
    g_ticks = std::uint32_t(now);
    if (g_activeTimerId != armedId) { armedId = g_activeTimerId; armedAt = armedId ? now : -1; }
    if (armedId && g_timerCallback && now - armedAt >= g_timerInterval) {
        auto cb = g_timerCallback; void* param = g_timerParam;
        armedId = 0; armedAt = -1;
        cb(g_timerInterval, param);
    }
}

// The region screen's own UI script ({0, 96a006b0, aa920991}: the toolbar strip with its buttons, bitmaps and text), the
// script cSC4WinRegionScreen::LoadGUI builds with child id 0x09ebe9ee. The screen object itself (region data, 3D view)
// is not wired: only its script window is.
static void BuildRegionScreen() {
    cIGZWin* desktop = gMgr->GetMainWindow();
    desktop->SetArea(0, 0, kW, kH);
    AddRect(desktop, 0, 0, kW, kH, 0x000000);
    const cGZPersistResourceKey toolbar{0, 0x96a006b0, 0xaa920991};
    cIGZWin* window = exphost::CreateWindowFromScript(toolbar, desktop, 0x09ebe9ee);
    std::printf("region screen: script window %s\n", window ? "built" : "FAILED");
    if (window) window->Release();
    desktop->SetBufferToDrawToRecursive();
    desktop->InvalidateSelf();
}

static void StartSplash() {
    gPhase = Phase::Splash;
    cIGZWin* desktop = gMgr->GetMainWindow();
    desktop->SetArea(0, 0, kW, kH);
    AddRect(desktop, 0, 0, kW, kH, 0x000000);   // black behind the 768 x 600 window
    sc4::SetSplashScreenHost(&gSplashHost);
    gSplash = new sc4::cSC4WinSplashScreen(gMgr);
    gSplash->AddRef();
    desktop->ChildAdd(gSplash);   // Init: builds the script window, arms nothing until the first paint
    desktop->SetBufferToDrawToRecursive();
    desktop->InvalidateSelf();
}

static void Frame() {
    if (gPhase == Phase::Splash) {
        PumpTimers();
        gMS2.Deliver();
        if (gToMenu) {   // DestroyCurrentScreen + CreateRegionScreen ran: drop the splash, show the menu
            gToMenu = false;
            cIGZWin* desktop = gMgr->GetMainWindow();
            desktop->ChildDeleteAll();
            gSplash->Release(); gSplash = nullptr;
            gPhase = Phase::Menu;
            if (std::getenv("HOST_TEST_MENU")) BuildMenu(); else BuildRegionScreen();
            desktop->SetBufferToDrawToRecursive();
            desktop->InvalidateSelf();
        }
    }
    gMgr->Pump(); gMgr->Plot();
    if (gPhase == Phase::Menu && std::getenv("HOST_TEST_MENU")) DrawLabels();
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a crash must not swallow the progress lines
    std::string shot; int frames = -1; bool selftest = false, boot = false, game = false, splash = false; int waitMs = 0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--selftest")) selftest = true;
        else if (!std::strcmp(argv[i], "--boot")) boot = true;
        else if (!std::strcmp(argv[i], "--game")) game = true;
        else if (!std::strcmp(argv[i], "--splash")) game = true;   // same as --game
        else if (!std::strcmp(argv[i], "--wait") && i + 1 < argc) waitMs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
    }
    if (boot) return exphost::RunBoot(true) ? 0 : 1;
    @autoreleasepool {
        gGraphics = new exphost::HostGraphics(kW, kH);
        gGraphics->Install();
        gMgr = new exphost::HostWinMgr();
        gMgr->AddRef();
        gMgr->Init();
        if (game) {
            if (!exphost::RunBoot(false, true)) std::printf("boot: FAILED (continuing to the UI anyway)\n");
            exphost::SetPngDecoder(&DecodePngBytes);
            std::printf("ui scripts: %s\n", exphost::InitUiScripts(gMgr) ? "service ready" : "NOT available");
        }
        std::printf("font: %s\n", InitFont() ? "FreeType + cGZFont ready" : "FAILED to initialise");
        gz::SetWinFontSource(&gFontSource);
        if (game || splash) StartSplash(); else BuildMenu();
        gMgr->GetMainWindow()->InvalidateSelf();
        for (cIGZWin* w : {gMgr->GetMainWindow()}) w->SetBufferToDrawToRecursive();
        Frame();
        if (std::getenv("HOST_DEBUG")) {
            auto* ctx = static_cast<gz::cIGZDrawContext*>(gGraphics->Primary()->GetDrawContext());
            std::printf("debug: ctx=%p canvas=%d,%d,%d,%d px0=%08x\n", (void*)ctx, ctx->GetCanvasArea().left, ctx->GetCanvasArea().top, ctx->GetCanvasArea().right, ctx->GetCanvasArea().bottom, gGraphics->Primary()->Pixels()[0]);
            ctx->Resume(); ctx->SetColor(0xff3366); ctx->Fill(cRZRect{10, 10, 50, 50}); ctx->Suspend();
            std::printf("debug: direct fill px=%08x\n", gGraphics->Primary()->Pixels()[20 * (gGraphics->Primary()->Stride() / 4) + 20]);
            cIGZWin* d = gMgr->GetMainWindow();
            std::printf("debug: desktop vis=%d area=%d,%d,%d,%d children=%u\n", d->IsVisible(), d->GetL(), d->GetT(), d->GetW(), d->GetH(), d->GetChildCount());
        }
        std::printf("EARLY TEST HOST: window tree built, %u children on the desktop\n", gMgr->GetMainWindow()->GetChildCount());

        if (selftest) {   // synthetic mouse path: move onto the second button, press, release
            gMgr->GZWinMsgPost_Mouse(kGZMsgMouseMove, 500, 340, 0); Frame();
            gMgr->GZWinMsgPost_Mouse(kGZMsgMouseDownL, 500, 340, 0); Frame();
            gMgr->GZWinMsgPost_Mouse(kGZMsgMouseUpL, 500, 340, 0); Frame();
            auto* b = gMgr->GetMainWindow()->GetChildWindowFromIDRecursive(101);
            std::printf("selftest: window under pointer id=%u\n", b ? b->GetID() : 0);
            return 0;
        }
        if (waitMs > 0) {   // let the frame loop run (timers, messages) before the screenshot
            const double end = [[NSDate date] timeIntervalSince1970] * 1000.0 + waitMs;
            while ([[NSDate date] timeIntervalSince1970] * 1000.0 < end) { Frame(); [NSThread sleepForTimeInterval:0.008]; }
        }
        if (!shot.empty()) {
            CGImageRef img = MakeImage();
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:shot.c_str()]];
            CGImageDestinationRef d = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
            CGImageDestinationAddImage(d, img, nullptr); CGImageDestinationFinalize(d);
            CFRelease(d); CGImageRelease(img);
            std::printf("wrote %s\n", shot.c_str());
            return 0;
        }

        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, kW, kH)
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
            backing:NSBackingStoreBuffered defer:NO];
        win.title = @"SC4 — EARLY TEST HOST (experimental)";
        HostView* view = [[HostView alloc] initWithFrame:NSMakeRect(0, 0, kW, kH)];
        view.wantsLayer = YES;
        view.layer.magnificationFilter = kCAFilterNearest;
        win.contentView = view; win.acceptsMouseMovedEvents = YES;
        [win makeFirstResponder:view]; [win makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES]; [app finishLaunching];
        __block int n = 0;
        for (;;) {
            @autoreleasepool {
                NSEvent* e;
                while ((e = [app nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.008] inMode:NSDefaultRunLoopMode dequeue:YES]))
                    [app sendEvent:e];
                if (!win.visible) break;
                Frame();
                CGImageRef img = MakeImage();
                view.layer.contents = (__bridge id)img; CGImageRelease(img);
                if (frames > 0 && ++n >= frames) break;
            }
        }
    }
    return 0;
}
