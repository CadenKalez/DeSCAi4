// SimCity 4 native macOS host: opens the first window and draws a city's terrain with Metal.
//   sc4  [city.sc4]                      interactive window
//   sc4  --screenshot out.png [city.sc4] [--size WxH] [--yaw d] [--pitch d] [--zoom z]
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/GameAudio.h"
#include "render/MetalRenderer.h"
#include "render/Terrain.h"

static render::MetalRenderer gRenderer;
static render::Camera gCamera;
static GameAudio gAudio;
static std::string gCityName;

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
    else if ([c isEqualToString:@"m"]) gAudio.SetMuted(!gAudio.Muted());
    else if ([c isEqualToString:@"n"]) gAudio.NextAmbient();
    else return;
    gAudio.PlayClick();
}
@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(strong) NSWindow* window;
@property(strong) NSTimer* timer;
@property(strong) SC4View* view;
@property(strong) NSTextField* hud;
@property(strong) NSDate* started;
@property(assign) int frames;
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
    [self buildOverlay];
    self.started = [NSDate date];
    gAudio.NextAmbient();
    self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60 repeats:YES block:^(NSTimer*) {
        NSSize s = [self.view convertSizeToBacking:self.view.bounds.size];
        gRenderer.DrawToLayer(gCamera, uint32_t(s.width), uint32_t(s.height));
        gAudio.Tick();
        ++self.frames;
        double t = -[self.started timeIntervalSinceNow];
        if (t >= 0.5) {
            self.hud.stringValue = [NSString stringWithFormat:
                @"SimCity 4  \u2014  %s\n%.0f fps   yaw %.0f\u00b0   zoom %.2f\nMusic: %s%s",
                gCityName.c_str(), self.frames / t, gCamera.yawDegrees, gCamera.distance,
                gAudio.CurrentName().c_str(), gAudio.Muted() ? "  [muted]" : ""];
            self.frames = 0;
            self.started = [NSDate date];
        }
    }];
}

- (NSButton*)button:(NSString*)title action:(SEL)sel x:(CGFloat)x w:(CGFloat)w parent:(NSView*)parent {
    NSButton* b = [NSButton buttonWithTitle:title target:self action:sel];
    b.frame = NSMakeRect(x, 8, w, 28);
    b.bezelStyle = NSBezelStyleRounded;
    [parent addSubview:b];
    return b;
}

- (void)buildOverlay {
    NSView* content = self.window.contentView;
    // status readout, top left
    NSVisualEffectView* top = [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(12, content.bounds.size.height - 12 - 64, 460, 64)];
    top.material = NSVisualEffectMaterialHUDWindow;
    top.blendingMode = NSVisualEffectBlendingModeWithinWindow;
    top.state = NSVisualEffectStateActive;
    top.wantsLayer = YES;
    top.layer.cornerRadius = 8;
    top.autoresizingMask = NSViewMinYMargin;
    self.hud = [NSTextField labelWithString:@""];
    self.hud.frame = NSMakeRect(10, 4, 440, 56);
    self.hud.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    self.hud.maximumNumberOfLines = 3;
    [top addSubview:self.hud];
    [content addSubview:top];
    // control bar, bottom centre
    NSVisualEffectView* bar = [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(12, 12, 480, 44)];
    bar.material = NSVisualEffectMaterialHUDWindow;
    bar.blendingMode = NSVisualEffectBlendingModeWithinWindow;
    bar.state = NSVisualEffectStateActive;
    bar.wantsLayer = YES;
    bar.layer.cornerRadius = 8;
    [self button:@"\u21b6 Rotate" action:@selector(rotLeft:) x:8 w:90 parent:bar];
    [self button:@"Rotate \u21b7" action:@selector(rotRight:) x:102 w:90 parent:bar];
    [self button:@"Zoom +" action:@selector(zoomIn:) x:196 w:70 parent:bar];
    [self button:@"Zoom \u2212" action:@selector(zoomOut:) x:270 w:70 parent:bar];
    [self button:@"Music \u25b6" action:@selector(nextMusic:) x:344 w:70 parent:bar];
    [self button:@"Mute" action:@selector(toggleMute:) x:418 w:56 parent:bar];
    [content addSubview:bar];
}
- (void)rotLeft:(id)s { gCamera.yawDegrees -= 90; gAudio.PlayClick(); }
- (void)rotRight:(id)s { gCamera.yawDegrees += 90; gAudio.PlayClick(); }
- (void)zoomIn:(id)s { gCamera.distance /= 1.25f; gAudio.PlayClick(); }
- (void)zoomOut:(id)s { gCamera.distance *= 1.25f; gAudio.PlayClick(); }
- (void)nextMusic:(id)s { gAudio.NextAmbient(); }
- (void)toggleMute:(id)s { gAudio.SetMuted(!gAudio.Muted()); }
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
    {
        size_t slash = city.find_last_of('/');
        gCityName = slash == std::string::npos ? city : city.substr(slash + 1);
        std::string sound = "/Users/fernandojosesampaio/Library/Application Support/Steam/steamapps/common/"
            "SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents/GameData/Sound.dat";
        if (shot.empty() && !gAudio.Open(sound, err)) std::fprintf(stderr, "audio: %s\n", err.c_str());
    }
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
