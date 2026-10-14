// SimCity 4 native macOS host: opens the first window and draws a city's terrain with Metal.
//   sc4  [city.sc4]                      interactive window
//   sc4  --screenshot out.png [city.sc4] [--size WxH] [--yaw d] [--pitch d] [--zoom z]
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

#include "app/GameAudio.h"
#include "sc4/OccupantManager.h"
#include "render/MetalRenderer.h"
#include "render/Terrain.h"

static render::MetalRenderer gRenderer;
static render::Camera gCamera;
static GameAudio gAudio;
static std::string gCityName;
static bool gDemo = false;

// ---- game-logic demo: the recovered occupant manager populated with a deterministic set of lots ----
namespace {
class DemoOccupant final : public sc4::cSC4Occupant {
public:
    DemoOccupant(float x, float z, float w, float d, float h, std::uint32_t type) : x_(x), z_(z), w_(w), d_(d), h_(h) { type_ = type; }
    void GetBoundingBox(float* lo, float* hi) override {
        lo[0] = x_; lo[1] = 0; lo[2] = z_;
        hi[0] = x_ + w_; hi[1] = h_; hi[2] = z_ + d_;
    }
private:
    float x_, z_, w_, d_, h_;
};
struct GameLogic {
    sc4::cSC4OccupantManager* manager = nullptr;
    std::size_t population = 0;
    double lastQueryMs = 0;
    std::size_t inView = 0, tall = 0, small = 0;
    void Build() {
        manager = new sc4::cSC4OccupantManager();
        manager->AddRef();
        manager->SetCitySize(4096, 4096);
        manager->Init();
        std::uint32_t seed = 12345;
        auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) & 0xffff; };
        for (int gx = 0; gx < 128; ++gx) {
            for (int gz = 0; gz < 128; ++gz) {
                if (rnd() % 100 < 45) continue;
                const float w = 16.0f * float(1 + rnd() % 3), d = 16.0f * float(1 + rnd() % 3);
                const float h = 8.0f + float(rnd() % 90);
                auto* o = new DemoOccupant(gx * 32.0f, gz * 32.0f, w, d, h, h > 60 ? 0x10 : 0x20);
                o->AddRef();
                if (manager->InsertOccupant(o, 0)) ++population;
                o->Release();
            }
        }
    }
    // what the camera currently sees: a box around the view centre that grows with the zoom distance
    void Query(const render::Camera& cam) {
        const float cx = cam.centerX != 0 ? cam.centerX : 2048.0f, cz = cam.centerZ != 0 ? cam.centerZ : 2048.0f;
        const float half = 350.0f * (cam.distance > 0 ? cam.distance : 1.0f);
        const float lo[3] = {cx - half, -1e9f, cz - half}, hi[3] = {cx + half, 1e9f, cz + half};
        auto t0 = std::chrono::steady_clock::now();
        sc4::OccupantList list;
        manager->GetOccupantsByBBox(list, lo, hi, std::uint32_t{0}, 100000);
        inView = list.size();
        tall = small = 0;
        for (auto* o : list) (o->GetType() == 0x10 ? tall : small)++;
        manager->ReleaseOccupantList(list);
        lastQueryMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
};
GameLogic gLogic;
}  // namespace

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
@property(strong) NSDate* demoStart;
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
    gLogic.Build();
    self.demoStart = [NSDate date];
    self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60 repeats:YES block:^(NSTimer*) {
        NSSize s = [self.view convertSizeToBacking:self.view.bounds.size];
        gRenderer.DrawToLayer(gCamera, uint32_t(s.width), uint32_t(s.height));
        gAudio.Tick();
        if (gDemo) {
            // scripted tour: slow zoom in/out breathing, a 90-degree turn (with click) every 6 s
            const double t = -[self.demoStart timeIntervalSinceNow];
            gCamera.distance = float(1.0 + 0.55 * std::sin(t * 0.9));
            static int turns = 0;
            const int due = int(t / 6.0);
            if (due > turns) { turns = due; gCamera.yawDegrees += 90; gAudio.PlayClick(); }
        }
        gLogic.Query(gCamera);
        ++self.frames;
        double t = -[self.started timeIntervalSinceNow];
        if (t >= 0.5) {
            self.hud.stringValue = [NSString stringWithFormat:
                @"SimCity 4  \u2014  %s\n%.0f fps   yaw %.0f\u00b0   zoom %.2f\n"
                 "Occupants: %zu placed, %zu in view (%zu tall / %zu low)  query %.3f ms\nMusic: %s%s",
                gCityName.c_str(), self.frames / t, gCamera.yawDegrees, gCamera.distance,
                gLogic.population, gLogic.inView, gLogic.tall, gLogic.small, gLogic.lastQueryMs,
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
    NSVisualEffectView* top = [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(12, content.bounds.size.height - 12 - 80, 520, 80)];
    top.material = NSVisualEffectMaterialHUDWindow;
    top.blendingMode = NSVisualEffectBlendingModeWithinWindow;
    top.state = NSVisualEffectStateActive;
    top.wantsLayer = YES;
    top.layer.cornerRadius = 8;
    top.autoresizingMask = NSViewMinYMargin;
    self.hud = [NSTextField labelWithString:@""];
    self.hud.frame = NSMakeRect(10, 4, 500, 72);
    self.hud.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    self.hud.maximumNumberOfLines = 4;
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
        else if (!strcmp(argv[i], "--demo")) gDemo = true;
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
