// SimCity 4 native macOS host: opens the first window and draws a city's terrain with Metal.
//   sc4  [city.sc4]                      interactive window
//   sc4  --screenshot out.png [city.sc4] [--size WxH] [--yaw d] [--pitch d] [--zoom z]
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "render/MetalRenderer.h"
#include "render/Terrain.h"

static render::MetalRenderer gRenderer;
static render::Camera gCamera;

@interface SC4View : NSView
@end
@implementation SC4View
- (BOOL)wantsUpdateLayer { return YES; }
+ (Class)layerClass { return [CAMetalLayer class]; }
- (CALayer*)makeBackingLayer { return [CAMetalLayer layer]; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)keyDown:(NSEvent*)e {
    NSString* c = e.charactersIgnoringModifiers;
    if ([c isEqualToString:@"q"]) gCamera.yawDegrees -= 90;      // SC4 rotates the view in 90-degree steps
    else if ([c isEqualToString:@"e"]) gCamera.yawDegrees += 90;
    else if ([c isEqualToString:@"="]) gCamera.distance *= 1.25f;
    else if ([c isEqualToString:@"-"]) gCamera.distance /= 1.25f;
}
@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(strong) NSWindow* window;
@property(strong) NSTimer* timer;
@property(strong) SC4View* view;
@end
@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)n {
    NSRect r = NSMakeRect(0, 0, 1280, 800);
    self.window = [[NSWindow alloc] initWithContentRect:r
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"SimCity 4";
    self.view = [[SC4View alloc] initWithFrame:r];
    self.view.wantsLayer = YES;
    self.window.contentView = self.view;
    std::string err;
    gRenderer.AttachLayer((__bridge void*)self.view.layer, err);
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:self.view];
    [NSApp activateIgnoringOtherApps:YES];
    self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60 repeats:YES block:^(NSTimer*) {
        NSSize s = [self.view convertSizeToBacking:self.view.bounds.size];
        gRenderer.DrawToLayer(gCamera, uint32_t(s.width), uint32_t(s.height));
    }];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)a { return YES; }
@end

int main(int argc, char** argv) {
    std::string shot, city = "/Users/fernandojosesampaio/Library/Application Support/Steam/steamapps/common/"
        "SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents/GameData/Regions/Timbuktu/City - Big City Tutorial.sc4";
    uint32_t w = 1280, h = 800;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) std::sscanf(argv[++i], "%ux%u", &w, &h);
        else if (!strcmp(argv[i], "--yaw") && i + 1 < argc) gCamera.yawDegrees = float(atof(argv[++i]));
        else if (!strcmp(argv[i], "--pitch") && i + 1 < argc) gCamera.pitchDegrees = float(atof(argv[++i]));
        else if (!strcmp(argv[i], "--zoom") && i + 1 < argc) gCamera.distance = float(atof(argv[++i]));
        else city = argv[i];
    }
    std::string err;
    render::HeightGrid grid;
    if (!render::LoadCityTerrain(city, grid, err)) { std::fprintf(stderr, "terrain: %s\n", err.c_str()); return 1; }
    if (!gRenderer.Init(err) || !gRenderer.SetTerrain(grid, err)) { std::fprintf(stderr, "metal: %s\n", err.c_str()); return 1; }
    grid.heights.clear(); grid.heights.shrink_to_fit();  // heights now live only in the GPU texture
    if (!shot.empty()) {
        if (!gRenderer.RenderToPng(shot, w, h, gCamera, err)) { std::fprintf(stderr, "render: %s\n", err.c_str()); return 1; }
        std::printf("wrote %s (%ux%u)\n", shot.c_str(), w, h);
        return 0;
    }
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate* d = [AppDelegate new];
        app.delegate = d;
        [app run];
    }
    return 0;
}
