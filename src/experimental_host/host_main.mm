// EARLY TEST / EXPERIMENTAL HOST — not recovered game code. See README_EARLY_TEST.md.
// Cocoa window + event pump over the recovered window manager and window tree, painted by the recovered software draw
// engine into a CPU buffer that is shown through a layer. Usage: sc4_host [--screenshot out.png] [--frames N]
#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "experimental_host/HostGraphics.h"
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
exphost::HostWinMgr* gMgr;

gz::cGZWinFlatRect* AddRect(cIGZWin* parent, int l, int t, int r, int b, std::uint32_t rgb, gz::cGZWinFlatRect* made = nullptr) {
    auto* w = made ? made : new HostRect(gMgr);
    w->Init();
    w->SetArea(l, t, r, b);
    w->SetFillColor(std::uint8_t(rgb >> 16), std::uint8_t(rgb >> 8), std::uint8_t(rgb));
    parent->ChildAdd(w);
    return w;
}

void BuildScene() {
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

static void Frame() { gMgr->Pump(); gMgr->Plot(); }

int main(int argc, char** argv) {
    std::string shot; int frames = -1; bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--selftest")) selftest = true;
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
    }
    @autoreleasepool {
        gGraphics = new exphost::HostGraphics(kW, kH);
        gGraphics->Install();
        gMgr = new exphost::HostWinMgr();
        gMgr->AddRef();
        gMgr->Init();
        BuildScene();
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
