// sc4_game — the native Metal application. The recovered cSC4App boots against the retail game data; its application window
// (cWinSC4App) creates the splash and region screens from the game's own UI scripts; every window draws through the game's
// own draw context, whose screen engine is the recovered hardware engine issuing cIGZGDriver calls into cMetalGDriver. The frame
// is whatever the driver holds: there is no CPU presentation.
//
//   sc4_game                      open a window
//   sc4_game --shot out.png [--frames N]   run N frames (default 120) headless and write the frame target as a PNG
#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <SDL.h>   // the test stub: its timers never fire by themselves, the frame loop fires them (see PumpTimers)

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "app/NativeBoot.h"
#include "app/NativeCity.h"
#include "app/NativeCityView.h"
#include "app/NativeFonts.h"
#include "app/NativeGraphics.h"
#include "app/NativeCity.h"
#include "app/NativeRegion.h"
#include "app/NativeRegionScreen.h"
#include "app/NativeUi.h"
#include "app/NativeWinMgr.h"
#include "app/NativeWinSC4App.h"
#include "gz/BufferFontRenderer.h"
#include "gz/cGZMessageServer2.h"
#include "gz/DrawFontRenderer.h"
#include "gz/GZFont.h"
#include "gz/GZFramework.h"
#include "gz/WinFont.h"
#include "render/gd/MetalGDriver.h"
#include "sc4/View3DWin.h"
#include "sc4/App.h"
#include "sc4/SC4UI.h"
#include "sc4/SLBuffer.h"
#include "sc4/ServiceLocator.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/WinSC4App.h"
#include "sc4/WinSplashScreen.h"

namespace {

constexpr int kW = 1024, kH = 768;

std::unique_ptr<gd::cMetalGDriver> gDriver;
std::unique_ptr<native::Graphics> gGraphics;
std::unique_ptr<native::WinMgr> gMgr;

// ---- fonts: the recovered cGZFont over FreeType, drawing through the surface's context --------------------------------------
native::FreeTypeFontHost* gFontHost;
cGZFont* gFont;

struct DrawFontHost final : IDrawFontRendererHost {
    cIGZFontRenderer* CreateBufferFontRenderer() override { auto* r = new cGZBufferFontRenderer(); r->AddRef(); return r; }
    cIGZBuffer* CreateBuffer() override { auto* b = new cGZBuffer(); b->AddRef(); return b; }
    void NativeColorToRGB(std::uint32_t n, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override { r = n >> 16; g = n >> 8; b = n; }
} gDrawFontHost;

bool InitFont() {
    gFontHost = new native::FreeTypeFontHost();
    gFont = new cGZFont(gFontHost);
    gFont->AddRef();
    gFont->SetFilePath(cRZString("/System/Library/Fonts/Supplemental/Arial.ttf"));
    gFont->SetSize(16);
    gFont->SetAntialiasing(3);
    gFont->SetCodePage(8);
    gFont->SetRenderer(new cGZDrawFontRenderer(&gDrawFontHost));
    return gFont->Init();
}

// The one font available (Arial through cGZFont) as the window system's font object until the game's font style script is wired.
class WinFont final : public gz::IWinFont {
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
        gFont->DrawTextLine(static_cast<native::Buffer*>(target)->AsBuffer(), x, y, t, n, color & 0xffffffu,
                            color2 == gz::kWinUnsetTextColor ? kFontDefaultColor : (color2 & 0xffffffu), clip, nullptr);
    }
    void DrawTextBox(IWinBuffer* target, const cRZRect& box, const char* t, std::uint32_t n, std::uint32_t alignment, std::uint32_t color,
                     std::uint32_t color2, const cRZRect* clip, int) override {
        gFont->DrawTextParagraph(static_cast<native::Buffer*>(target)->AsBuffer(), box, t, n, int(alignment), color & 0xffffffu,
                                 color2 == gz::kWinUnsetTextColor ? kFontDefaultColor : (color2 & 0xffffffu), clip, nullptr);
    }
};
struct FontSource final : gz::IWinFontSource {
    WinFont font;
    gz::IWinFont* Font(std::uint32_t) override { return gFont ? &font : nullptr; }
    bool Available() override { return gFont != nullptr; }
} gFontSource;

// ---- PNG resources: ImageIO -> a CPU buffer with straight alpha ------------------------------------------------------------
cGZBuffer* DecodePngBytes(const std::vector<uint8_t>& png) {
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

// ---- cSLBuffer's loader: every bitmap the UI names by resource key or by file ------------------------------------------------
class BufferHost final : public ISLBufferHost {
public:
    bool GetBufferResource(const cGZPersistResourceKey& key, std::int32_t, bool, cIGZBuffer** out) override {
        cGZBuffer* buffer = native::LoadBitmapResource(key.group, key.instance);   // PNG resources decode to ARGB8888, the display's 32-bit type
        *out = buffer;
        return buffer != nullptr;
    }
    bool HasResource(const cGZPersistResourceKey& key) override {
        cGZBuffer* buffer = native::LoadBitmapResource(key.group, key.instance);
        if (buffer) buffer->Release();
        return buffer != nullptr;
    }
    bool LowQualityTextures() override { return false; }
    bool LoadBufferFromFile(const std::string& path, std::int32_t, cIGZBuffer** out) override {
        NSData* data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
        if (!data) return false;
        std::vector<std::uint8_t> bytes(static_cast<const std::uint8_t*>(data.bytes), static_cast<const std::uint8_t*>(data.bytes) + data.length);
        *out = DecodePngBytes(bytes);   // ImageIO reads BMP and PNG alike
        return *out != nullptr;
    }
    cIGZBuffer* CreateBuffer() override { auto* b = new cGZBuffer(); b->AddRef(); return b; }
} gBufferHost;

// nSC4UI's services: the script window builder and the window manager (the default host reaches for engine globals the app lacks)
class UiHost final : public nSC4UI::INSC4UIHost {
public:
    bool PopupModalDialogsEnabled() override { return false; }
    bool LoadExemplarString(cIGZString&, sc4::cISCPropertyHolder*, std::uint32_t, sc4::cSCStringDetokenizer*) override { return false; }
    void MakeNumberString(std::int64_t value, cIGZString& out) override { char t[32]; std::snprintf(t, sizeof t, "%lld", (long long)value); out.AssignFromCString(t); }
    cIGZWinMgr* WindowManager() override { return gMgr.get(); }
    cIGZWin* CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent, std::uint32_t id) override { return native::CreateWindowFromScript(key, parent, id); }
    bool CreateWindowFromScriptAs(const cGZPersistResourceKey& key, cIGZWin* parent, std::uint32_t id, std::uint32_t iid, void** out) override {
        cIGZWin* window = CreateWindowFromScript(key, parent, id);
        if (!window) return false;
        const bool ok = window->QueryInterface(iid, out);
        window->Release();
        return ok;
    }
    cSLWinLocationSaver* WindowLocationSaver() override { return nullptr; }
    void BeginGamePause() override {}
    void EndGamePause() override {}
} gUiHost;

// Repaints a window and everything below it (a plain InvalidateSelf marks one window only).
bool InvalidateTreeCallback(cIGZWin*, std::uint32_t, void* child, void*);
void InvalidateTree(cIGZWin* window) {
    window->InvalidateSelf();
    window->EnumChildren(0x22ba0121, InvalidateTreeCallback, nullptr);
}
bool InvalidateTreeCallback(cIGZWin*, std::uint32_t, void* child, void*) { InvalidateTree(static_cast<cIGZWin*>(child)); return true; }

// Debug: prints the window tree below `window` (id, visibility, area) to a depth.
void DumpTree(cIGZWin* window, int depth, int maxDepth) {
    const cRZRect a = window->GetArea();
    std::printf("%*s id=%08x vis=%d area=%d,%d,%d,%d kids=%u\n", depth * 2, "", window->GetID(), window->IsVisible(), a.left, a.top, a.right, a.bottom, window->GetChildCount());
    if (depth >= maxDepth) return;
    struct Ctx { int depth, max; };
    Ctx ctx{depth + 1, maxDepth};
    window->EnumChildren(0x22ba0121, [](cIGZWin*, std::uint32_t, void* child, void* c) { auto* x = static_cast<Ctx*>(c); DumpTree(static_cast<cIGZWin*>(child), x->depth, x->max); return true; }, &ctx);
}

// ---- the frame loop ---------------------------------------------------------------------------------------------------------
// The SDL stub's timers never fire by themselves: fire the armed one when its period has passed.
void PumpTimers() {
    static double armedAt = -1; static int armedId = 0;
    const double now = [[NSDate date] timeIntervalSince1970] * 1000.0;
    g_ticks = std::uint32_t(now);
    if (g_activeTimerId != armedId) { armedId = g_activeTimerId; armedAt = armedId ? now : -1; }
    if (armedId && g_timerCallback && now - armedAt >= g_timerInterval) {
        if (std::getenv("SC4_DEBUG")) std::printf("debug: timer %d fires\n", armedId);
        auto cb = g_timerCallback; void* param = g_timerParam;
        armedId = 0; armedAt = -1;
        cb(g_timerInterval, param);
    }
}

native::HostWinSC4App* gAppHost = nullptr;
cGZMessageServer2* gMessages2 = nullptr;   // ticked by the frame loop, as the framework ticks the service in the game
sc4::cWinSC4App* gAppWindow = nullptr;

// What the splash screen reaches outside itself (SPLASH-1): the game's script window builder, the message server, the driver
// clear, and the application window's screen swap.
struct SplashHost final : sc4::ISplashScreenHost {
    struct Unknown final : cIGZUnknown {
        bool QueryInterface(std::uint32_t, void**) override { return false; }
        std::uint32_t AddRef() override { return 1; }
        std::uint32_t Release() override { return 1; }
    } graphics;
    cIGZWin* CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent, std::uint32_t id) override {
        return native::CreateWindowFromScript(key, parent, id);
    }
    cIGZUnknown* GraphicSystemObject() override { return &graphics; }
    cIGZMessageServer2* MessageServer2() override { return SL::spMS2; }
    cIGZMessage2Standard* CreateStandardMessage() override { auto* m = new cRZMessage2Standard(); m->AddRef(); return m; }
    void ClearScreenToBlack() override {
        gDriver->ClearColor(0, 0, 0, 0);
        gDriver->Clear(gd::kClearColor);
    }
    void DestroyCurrentScreen() override {
        std::printf("splash: DestroyCurrentScreen\n");
        if (gAppWindow) gAppWindow->DestroyCurrentScreen();
        ClearScreenToBlack();   // the frame persists between paints; the next screen draws over a clean one
        gMgr->GetMainWindow()->InvalidateSelf();
    }
    void CreateRegionScreen() override { std::printf("splash: CreateRegionScreen\n"); if (gAppWindow) gAppWindow->CreateRegionScreen(); }
} gSplashHost;

void Frame() {
    PumpTimers();
    cSC4App* app = native::BootApplication();
    if (app) {
        app->RunMessageServerPump(0, 64, 4);
        app->RunMessageServer2Pump(0, 64, 4);
        if (gMessages2) gMessages2->OnTick(16);   // posted and general messages (the splash timer, screen swaps)
        app->OnTick(16);
    }
    const bool regionScreen = gAppWindow && gAppWindow->CurrentScreen() && gAppWindow->CurrentScreen()->GetID() == 0xea659793;
    if (regionScreen && native::DrawRegionLayers()) InvalidateTree(gMgr->GetMainWindow());   // the map is drawn under the windows: they repaint over it
    gMgr->Pump();
    gMgr->Plot();
    gGraphics->Services().Tick();
}

bool Boot() {
    native::SetPngDecoder(&DecodePngBytes);
    SetSLBufferHost(&gBufferHost);
    nSC4UI::SetHost(&gUiHost);
    gGraphics.reset(new native::Graphics(gDriver.get(), kW, kH));
    gMgr.reset(new native::WinMgr());
    gMgr->AddRef();
    gMgr->Init();
    gMgr->GetMainWindow()->SetArea(0, 0, kW, kH);
    if (!InitFont()) std::printf("font: FAILED to initialise\n");
    gz::SetWinFontSource(&gFontSource);

    const bool ok = native::RunBoot(true, true, gDriver.get(), [&](cGZFramework& framework, cSC4App& app) {
        SL::SetFramework(&framework);             // the locator's RZGetFramework(): without it every SL:: service stays null
        framework.AddSystemService(gMgr.get());   // RZ::WM(): the application finds the main window through it
        // The second message server (service 0x04fa845b): the splash timer, the screen swaps and every posted command go through it.
        // The frame loop ticks it by hand (RunMessageServer2Pump), so the framework tick registration is a no-op.
        static struct : IMessageServerFramework {
            void AddTickClient(cIGZSystemService*) override {}
            void RemoveTickClient(cIGZSystemService*) override {}
        } messageFramework;
        SetMessageServerFramework(&messageFramework);
        auto* messages2 = new cGZMessageServer2();
        framework.AddSystemService(messages2);
        messages2->Init();
        gMessages2 = messages2;
        static native::HostWinSC4App host(&framework);
        gAppHost = &host;
        // The window scripts need the UI script service, which needs the resources Init has just registered; the application window
        // (created a few steps later, inside Init) builds the splash from them.
        app.StartupHooks().configureResourceCache = [] {
            std::printf("ui scripts: %s\n", native::InitUiScripts(gMgr.get()) ? "service ready" : "NOT available");
        };
        app.StartupHooks().createApplicationWindow = [&](cSC4App& a) -> sc4::AppStartupHooks::ApplicationWindow {
            sc4::SetSplashScreenHost(&gSplashHost);
            auto* window = new sc4::cWinSC4App(&a, gMgr.get(), &host);
            gAppWindow = window;
            static sc4::WinSC4AppApplication appView(window);
            return {static_cast<cIGZWin*>(window), &appView};
        };
    });
    if (!ok) std::printf("boot: FAILED\n");
    if (cSC4App* app = native::BootApplication()) {
        native::RegionCallbacks callbacks;
        callbacks.dataDirectory = std::string(std::getenv("HOME")) + "/Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents/GameData/";
        callbacks.isRegionScreenCurrent = [] { return gAppWindow && gAppWindow->CurrentScreen() && gAppWindow->CurrentScreen()->GetID() == 0xea659793; };
        callbacks.destroyCurrentScreen = [] { if (gAppWindow) gAppWindow->DestroyCurrentScreen(); };
        callbacks.createRegionScreen = [] { if (gAppWindow) gAppWindow->CreateRegionScreen(); };
        native::InstallRegionServices(*app, callbacks);
        native::RegionScreenServices screen;
        screen.app = app;
        screen.graphics = gGraphics.get();
        screen.createWindowFromScript = &native::CreateWindowFromScript;
        screen.loadBitmap = [](std::uint32_t group, std::uint32_t instance) { return native::LoadBitmapResource(group, instance); };
        native::SetCityScreenCallbacks({[] { if (gAppWindow) gAppWindow->DestroyCurrentScreen(); }, [] {
            if (!gAppWindow || !gAppWindow->CreateCityScreen()) return;
            if (sc4::cSC4View3DWin* view = gAppWindow->View3D()) native::AttachCityRenderStack(*view, native::BootApplication() ? native::BootApplication()->GetCity() : nullptr, gDriver.get(), static_cast<gz::cIGZDrawContext*>(gGraphics->Primary()->GetDrawContext()), kW, kH);
        }});
        screen.loadCity = [app](const std::string& path) { return native::LoadCityFile(*app, path); };
        native::InstallRegionScreenHost(screen);
        cISC4Nation* nation = native::CreateNation();
        app->SetNation(nation);
        std::printf("nation: regions dir \"%s\"\n", cSC4Nation::msRegionDirectory.ToChar());
        std::printf("nation: %u regions, active %d\n", nation->GetRegionCount(), nation->GetActiveRegionIndex());
        if (std::getenv("SC4_DEBUG")) {
            for (std::uint32_t i = 0; i < nation->GetRegionCount(); ++i) {
                cISC4Region* region = nation->GetRegion(i);
                RegionCityList cities;
                region->Init();
                region->GetAllCities(cities);
                std::vector<cISC4Region::cLocation> locations;
                region->GetCityLocations(locations);
                std::printf("  region %u locations=%zu dir=%s\n", i, locations.size(), region->GetDirectoryName()->ToChar());
                std::printf("  region %u \"%s\": %zu cities\n", i, region->GetDirectoryName() ? region->GetDirectoryName()->ToChar() : "?", cities.size());
            }
        }
    }
    if (gAppHost) gAppHost->SetMessageServer(SL::spMS2);
    gMgr->GetMainWindow()->SetBufferToDrawToRecursive();
    gMgr->GetMainWindow()->InvalidateSelf();
    return ok;
}

void WritePng(const std::string& path) {
    std::vector<std::uint8_t> bgra;
    std::uint32_t w = 0, h = 0;
    if (!gDriver->ReadBack(bgra, w, h)) { std::printf("read-back failed\n"); return; }
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(bgra.data(), w, h, 8, w * 4, cs, kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageDestinationRef d = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    CGImageDestinationAddImage(d, img, nullptr); CGImageDestinationFinalize(d);
    CFRelease(d); CGImageRelease(img); CGContextRelease(ctx); CGColorSpaceRelease(cs);
    std::printf("wrote %s\n", path.c_str());
}

}  // namespace

@interface GameView : NSView
@end

@implementation GameView
+ (Class)layerClass { return [CAMetalLayer class]; }
- (CALayer*)makeBackingLayer { return [CAMetalLayer layer]; }
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
// The city view's keys: what the game's key accelerator (the keyboard shortcut table, not loaded natively yet) turns
// into cSC4View3DWin commands - arrows scroll, +/- and Page Up/Down zoom, comma/period rotate.
static bool CityViewKey(NSEvent* e, bool down) {
    sc4::cSC4View3DWin* view = gAppWindow ? gAppWindow->View3D() : nullptr;
    if (!view) return false;
    std::uint32_t command = 0;
    switch (e.keyCode) {
        case 123: command = down ? 0x6a935cd8 : 0x2a94826b; break;   // left
        case 124: command = down ? 0x6a935ce0 : 0x2a94826e; break;   // right
        case 126: command = down ? 0x6a935cdd : 0x2a948272; break;   // up
        case 125: command = down ? 0x6a935ce2 : 0x2a948275; break;   // down
        case 24: case 69: case 116: command = down ? 0x6a935cb7 : 0; break;   // = / keypad + / Page Up: zoom in
        case 27: case 78: case 121: command = down ? 0x6a935cba : 0; break;   // - / keypad - / Page Down: zoom out
        case 43: command = down ? 0x6a935cc9 : 0; break;                      // , rotate left
        case 47: command = down ? 0x6a935ccc : 0; break;                      // . rotate right
        default: return false;
    }
    if (command && !(down && e.isARepeat)) view->ProcessViewCommand(command);
    return true;
}
- (void)keyDown:(NSEvent*)e { if (!CityViewKey(e, true)) gMgr->GZWinMsgPost_Keyboard(kGZMsgKeyDown, e.keyCode, 0); }
- (void)keyUp:(NSEvent*)e { if (!CityViewKey(e, false)) gMgr->GZWinMsgPost_Keyboard(kGZMsgKeyUp, e.keyCode, 0); }
- (void)rightMouseDragged:(NSEvent*)e { [self post:kGZMsgMouseMove e:e]; }
// The wheel: one GZ notch (120) per accumulated line of scrolling, posted as a Windows wParam (delta high, keys low).
- (void)scrollWheel:(NSEvent*)e {
    static double accumulated = 0;
    accumulated += e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / 10.0 : e.scrollingDeltaY;
    if (std::fabs(accumulated) < 1.0) return;
    const int delta = accumulated > 0 ? 120 : -120;
    accumulated = 0;
    NSPoint p = [self gz:e];
    gMgr->GZWinMsgPost_Mouse(kGZMsgMouseWheel, int(p.x), int(p.y), (std::uint32_t(delta) << 16) | [self flags:e]);
}
- (void)updateTrackingAreas {
    for (NSTrackingArea* a in self.trackingAreas) [self removeTrackingArea:a];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds
        options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect owner:self userInfo:nil]];
}
@end

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string shot;
    int frames = -1;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
    }
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        NSWindow* window = nil;
        CAMetalLayer* layer = nil;
        if (shot.empty()) {
            [app setActivationPolicy:NSApplicationActivationPolicyRegular];
            window = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, kW, kH)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
            window.title = @"SimCity 4";
            GameView* view = [[GameView alloc] initWithFrame:NSMakeRect(0, 0, kW, kH)];
            view.wantsLayer = YES;
            layer = (CAMetalLayer*)view.layer;
            layer.device = MTLCreateSystemDefaultDevice();
            layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            layer.drawableSize = CGSizeMake(kW, kH);
            layer.framebufferOnly = NO;   // the driver blits its frame target into the drawable
            window.contentView = view;
            window.acceptsMouseMovedEvents = YES;
            [window makeFirstResponder:view];
            [window makeKeyAndOrderFront:nil];
            [app activateIgnoringOtherApps:YES];
            [app finishLaunching];
        }
        gDriver = gd::cMetalGDriver::Create((__bridge void*)layer, kW, kH);
        if (!gDriver) { std::printf("no Metal device\n"); return 1; }
        Boot();
        if (const char* cityPath = std::getenv("SC4_LOADCITY")) {   // development: load one city file straight after boot and exit
            cSC4App* loadApp = native::BootApplication();
            const bool loaded = loadApp && native::LoadCityFile(*loadApp, cityPath);
            return loaded ? 0 : 2;
        }

        if (!shot.empty()) {
            const int n = frames > 0 ? frames : 120;
            for (int i = 0; i < n; ++i) { Frame(); [NSThread sleepForTimeInterval:0.004]; }
            if (const char* click = std::getenv("SC4_CLICK")) {   // "x,y": a synthetic double click after the frames
                int cx = 0, cy = 0;
                std::sscanf(click, "%d,%d", &cx, &cy);
                gMgr->GZWinMsgPost_Mouse(kGZMsgMouseMove, cx, cy, 0);
                for (int k = 0; k < 2; ++k) {
                    gMgr->GZWinMsgPost_Mouse(kGZMsgMouseDownL, cx, cy, 0);
                    gMgr->GZWinMsgPost_Mouse(kGZMsgMouseUpL, cx, cy, 0);
                    for (int i = 0; i < 10; ++i) { Frame(); [NSThread sleepForTimeInterval:0.004]; }
                }
            }
            if (const char* script = std::getenv("SC4_INPUT")) {   // dev: "token:frames,..." city view input after the click
                // tokens: left/right/up/down (held for the frames), zin/zout/rl/rr (view commands), wheel+/wheel- (at the centre)
                std::string all(script);
                std::size_t at = 0;
                while (at < all.size()) {
                    std::size_t end = all.find(',', at);
                    if (end == std::string::npos) end = all.size();
                    const std::string token = all.substr(at, end - at);
                    at = end + 1;
                    const std::size_t colon = token.find(':');
                    const std::string name = token.substr(0, colon);
                    const int hold = colon == std::string::npos ? 30 : std::atoi(token.c_str() + colon + 1);
                    sc4::cSC4View3DWin* v = gAppWindow ? gAppWindow->View3D() : nullptr;
                    static const std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> kCommands = {
                        {"left", {0x6a935cd8, 0x2a94826b}}, {"right", {0x6a935ce0, 0x2a94826e}}, {"up", {0x6a935cdd, 0x2a948272}},
                        {"down", {0x6a935ce2, 0x2a948275}}, {"zin", {0x6a935cb7, 0}}, {"zout", {0x6a935cba, 0}},
                        {"rl", {0x6a935cc9, 0}}, {"rr", {0x6a935ccc, 0}}};
                    if (name == "wheel+" || name == "wheel-")
                        gMgr->GZWinMsgPost_Mouse(kGZMsgMouseWheel, kW / 2, kH / 2, std::uint32_t(name == "wheel+" ? 120 : -120) << 16);
                    else if (auto it = kCommands.find(name); it != kCommands.end() && v)
                        v->ProcessViewCommand(it->second.first);
                    for (int i = 0; i < hold; ++i) { Frame(); [NSThread sleepForTimeInterval:0.004]; }
                    if (auto it = kCommands.find(name); it != kCommands.end() && v && it->second.second) v->ProcessViewCommand(it->second.second);
                    std::printf("input: %s for %d frames\n", name.c_str(), hold);
                }
            }
            if (std::getenv("SC4_DEBUG")) {
                cIGZWin* desktop = gMgr->GetMainWindow();
                if (cIGZWin* w = desktop->GetChildWindowFromIDRecursive(0xaaa9c9d9)) {
                    const cRZRect a = w->GetArea();
                    std::printf("debug: splash script window found: area %d,%d,%d,%d visible=%d children=%u\n", a.left, a.top, a.right, a.bottom, w->IsVisible(), w->GetChildCount());
                } else std::printf("debug: splash script window NOT found\n");
                if (std::getenv("SC4_DUMP")) {
                    int i = 0;
                    for (native::Buffer* b : native::Buffer::All()) {
                        if (b->OnScreen()) continue;
                        cGZBuffer* px = b->Pixels();
                        std::printf("debug: private buffer %d: %ux%u\n", i, px->Width(), px->Height());
                        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
                        CGContextRef cg = CGBitmapContextCreate(px->GetColorSurfaceBits(), px->Width(), px->Height(), 8, px->GetColorSurfaceStride(), cs, kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
                        CGImageRef img = CGBitmapContextCreateImage(cg);
                        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithFormat:@"work/shots/pbuf%d.png", i++]];
                        CGImageDestinationRef d = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
                        CGImageDestinationAddImage(d, img, nullptr); CGImageDestinationFinalize(d);
                        CFRelease(d); CGImageRelease(img); CGContextRelease(cg); CGColorSpaceRelease(cs);
                    }
                }
                std::printf("debug: spMS2=%p timer id=%d interval=%u cb=%p ticks=%u\n", (void*)SL::spMS2, int(g_activeTimerId), unsigned(g_timerInterval), (void*)g_timerCallback, unsigned(g_ticks));
                if (std::getenv("SC4_TREE")) DumpTree(gMgr->GetMainWindow(), 0, std::atoi(std::getenv("SC4_TREE")));
                std::printf("debug: desktop children=%u visible=%d draws=%u\n", desktop->GetChildCount(), desktop->IsVisible(), gDriver->DrawCalls());
                for (auto& kv : gDriver->UnsupportedCalls()) std::printf("debug: unsupported %s x%d\n", kv.first.c_str(), kv.second);
                if (auto* w = native::BootApplication()) std::printf("debug: app tick ok, window=%p\n", (void*)w->GetMainWindow());
            }
            WritePng(shot);
            return 0;
        }
        int n = 0;
        for (;;) {
            @autoreleasepool {
                NSEvent* e;
                while ((e = [app nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.008] inMode:NSDefaultRunLoopMode dequeue:YES]))
                    [app sendEvent:e];
                if (!window.visible) break;
                Frame();
                gDriver->Present();
                if (frames > 0 && ++n >= frames) break;
            }
        }
    }
    return 0;
}
