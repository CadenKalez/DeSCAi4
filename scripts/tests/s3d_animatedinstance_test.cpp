#include <cmath>
#include <cstdio>
#include <vector>
#include "s3d/AnimatedInstance.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace s3d;
struct Host : IAnimatedModelHost {
    int frames = 5, rate = 2000;
    bool failLoads = false;
    std::vector<int> loaded;
    std::vector<std::pair<std::uint32_t, int>> instances;
    int ModelFrameCount() override { return frames; }
    int ModelDefaultRate() override { return rate; }
    bool LoadModelFrame(int f) override { if (failLoads) return false; loaded.push_back(f); return true; }
    bool LoadModelInstance(std::uint32_t id, int f) override { instances.push_back({id, f}); return true; }
};
int main() {
    Host host;
    cAnimatedInstanceState a(&host);
    CHECK(a.playMode == kPlayLoop && a.direction == 1 && a.autoFrameRate == -100.0f && a.rotation == 0xff && a.zoom == 0xff);
    // plain loop over 5 frames
    CHECK(a.GetFrameCount() == 5 && a.GetFrame() == 0);
    std::vector<int> seen;
    for (int i = 0; i < 7; ++i) { a.IncrementFrame(); seen.push_back(a.GetFrame()); }
    CHECK((seen == std::vector<int>{1, 2, 3, 4, 0, 1, 2}));
    // ping-pong
    cAnimatedInstanceState p(&host);
    p.SetPlayMode(kPlayPingPong);
    seen.clear();
    for (int i = 0; i < 10; ++i) { p.IncrementFrame(); seen.push_back(p.GetFrame()); }
    CHECK((seen == std::vector<int>{1, 2, 3, 4, 3, 2, 1, 0, 1, 2}));
    CHECK(p.direction == 1);
    // play once stops at the end and clears the accumulator
    cAnimatedInstanceState o(&host);
    o.SetPlayMode(kPlayOnce);
    o.frameAccumulator = 0.5f;
    for (int i = 0; i < 6; ++i) o.IncrementFrame();
    CHECK(o.GetFrame() == 4 && o.direction == 0 && o.frameAccumulator == 0.0f);
    o.IncrementFrame();
    CHECK(o.GetFrame() == 4);
    // single-frame models never move
    host.frames = 1;
    cAnimatedInstanceState s(&host);
    CHECK(s.IncrementFrame() == 2 && s.GetFrame() == 0);
    host.frames = 5;
    // SetFrame: same frame returns 2 without loading, failed loads return 0
    host.loaded.clear();
    CHECK(a.SetFrame(a.GetFrame()) == 2 && host.loaded.empty());
    host.failLoads = true;
    CHECK(a.SetFrame(3) == 0 && a.GetFrame() != 3);
    host.failLoads = false;
    CHECK(a.SetFrame(3) == 2 && a.GetFrame() == 3 && host.loaded.back() == 3);
    // auto frame rate: default -200 resolves to the model rate * 0.001 (2 frames per second)
    cAnimatedInstanceState r(&host);
    CHECK(r.DoAutoFrameUpdate(0.25f) == 0);                    // resolves the rate, accumulates 0.5
    CHECK(r.autoFrameRate == 2.0f && r.frameAccumulator == 0.5f);
    CHECK(r.DoAutoFrameUpdate(0.25f) == 2);                    // reaches 1.0 -> one step
    CHECK(r.GetFrame() == 1 && r.frameAccumulator == 0.0f);
    r.SetAutoFrameRate(0.0f);
    CHECK(r.DoAutoFrameUpdate(10.0f) == 0);
    // big time steps: without catch-up a single step happens and the accumulator is forced to 1.0
    r.SetAutoFrameRate(2.0f);
    r.frameAccumulator = 0.0f;
    CHECK(r.DoAutoFrameUpdate(1.5f) == 2 && r.GetFrame() == 2 && r.frameAccumulator == 1.0f);
    r.catchUp = true;
    r.frameAccumulator = 0.0f;
    CHECK(r.DoAutoFrameUpdate(2.2f) == 2);                     // 4.4 frames -> 4 steps, remainder 0.4
    CHECK(r.GetFrame() == (2 + 4) % 5 && std::fabs(r.frameAccumulator - 0.4f) < 1e-5f);
    // framesets: three sets of 3, 2 and 4 frames
    {
        cAnimatedInstanceState f(&host);
        FramesetInfo info;
        info.count = 3;
        info.starts = {0, 3, 5, 9};
        info.frameCount = 3;
        info.playMode = kPlayLoop;
        info.direction = 1;
        f.framesets = &info;
        CHECK(f.GetFrameCount() == 3 && f.GetFrame() == 0);
        f.IncrementFrame(); f.IncrementFrame();
        CHECK(f.GetFrame() == 2 && info.absoluteFrame == 2);
        f.IncrementFrame();                                     // loop to the start of the set
        CHECK(f.GetFrame() == 0);
        CHECK(f.SetFrameset(1) == 2 && info.frameCount == 2 && f.SetFrameset(1) == 0 && f.SetFrameset(5) == 0);
        CHECK(f.GetFrame() == -3);                              // absolute frame is not reset by SetFrameset
        CHECK(f.SetFrame(1) == 2 && info.absoluteFrame == 4 && f.GetFrame() == 1);
        CHECK(f.SetFrame(7) == 2 && f.GetFrame() == 7 % 2);     // wraps with the modulo
        CHECK(f.SetFrame(2) == 2 && f.GetFrame() == 2);         // frameCount itself is kept (off by one in the original)
        f.SetPlayMode(kPlayPingPong);
        CHECK(info.playMode == kPlayPingPong);
        // ping-pong inside set 1 (frames 3,4)
        info.absoluteFrame = 3; info.direction = 1;
        info.IncrementFrame(); CHECK(info.absoluteFrame == 4 && info.direction == 1);
        info.IncrementFrame(); CHECK(info.absoluteFrame == 3 && info.direction == -1);
        info.IncrementFrame(); CHECK(info.absoluteFrame == 4 && info.direction == 1);
        info.playMode = kPlayOnce; info.absoluteFrame = 4;
        info.IncrementFrame(); CHECK(info.direction == 0);
        info.IncrementFrame(); CHECK(info.absoluteFrame == 4);
        // a one-frame set is inert
        info.current = 0; info.frameCount = 1; info.direction = 1; info.absoluteFrame = 0;
        info.IncrementFrame(); CHECK(info.absoluteFrame == 0);
        // backwards loop wraps to the last frame of the set
        info.current = 2; info.frameCount = 4; info.playMode = kPlayLoop; info.direction = -1; info.absoluteFrame = 5;
        info.IncrementFrame(); CHECK(info.absoluteFrame == 8);
    }
    // flag setters
    {
        cAnimatedInstanceState f(&host);
        CHECK(f.SetHighlight(0) == 0 && f.SetHighlight(2) == 2 && (f.flags & 1) && f.highlight == 2);
        CHECK(f.SetHighlight(2) == 0 && f.SetHighlight(3) == 2 && f.SetHighlight(0) == 2 && !(f.flags & 1));
        CHECK(f.SetSelfIlluminated(false) == 0 && f.SetSelfIlluminated(true) == 2 && (f.flags & 4) && f.SetSelfIlluminated(true) == 0);
        CHECK(f.SetOpacity(true, 0.5f) == 2 && (f.flags & 2) && f.opacity == 0.5f);
        CHECK(f.SetOpacity(true, 1.0f) == 2 && !(f.flags & 2) && f.opacity == 1.0f);
        CHECK(f.SetOpacity(true, -100.0f) == 2 && !(f.flags & 2));
        CHECK(f.SetOpacity(false, 0.2f) == 2 && !(f.flags & 2));
        f.modelFlags = 5;
        CHECK(f.SetOpacity(false, 1.0f) == 2 && (f.flags & 2));          // models flagged translucent keep the bit
        // zoom / rotation select the model instance of the zoom level
        f.zoomInstances = {10, 11, 12, 13, 14, 15};
        host.instances.clear();
        CHECK(f.SetZoomAndRotation(2, 3) == 2 && f.rotation == 3 && f.currentInstance == 12 && host.instances.size() == 1);
        CHECK(f.SetZoomAndRotation(2, 1) == 0 && f.rotation == 1);
        CHECK(f.SetZoomAndRotation(3, 1) == 2 && f.currentInstance == 13);
    }
    if (failures) return 1;
    std::printf("s3d_animatedinstance: all passed\n");
}
