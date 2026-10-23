#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <SDL.h>

#include "fake_winmgr2.h"
#include "sc4/WinRegionClouds.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Tex final : IRegionCloudTexture {
    int refs = 1; std::uint32_t handle;
    explicit Tex(std::uint32_t h) : handle(h) {}
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    std::uint32_t TextureHandle() override { return handle; }
};
struct Dev final : IRegionCloudsDevice {
    std::vector<std::string> log; const CloudVertex* verts = nullptr; std::vector<CloudVertex> drawn; std::vector<std::uint32_t> handles;
    void SetState168(int v) override { log.push_back("168:" + std::to_string(v)); }
    void SetState160(int v) override { log.push_back("160:" + std::to_string(v)); }
    void SetBlend(int a, int b) override { log.push_back("blend:" + std::to_string(a) + std::to_string(b)); }
    void SetState198(int) override { log.push_back("198"); }
    void SetState1a0(int) override { log.push_back("1a0"); }
    void SetState1a8(int, int, int, int) override { log.push_back("1a8"); }
    void SetVertices(int f, int o, const CloudVertex* v) override { verts = v; log.push_back("verts:" + std::to_string(f) + std::to_string(o)); }
    void SetTexture(std::uint32_t h, int) override { handles.push_back(h); }
    void SetTextureState(int, int, int) override {}
    void SetSamplerState(int, int, int) override {}
    void DrawPrimitive(int t, int f, int c) override { log.push_back("draw:" + std::to_string(t) + std::to_string(f) + std::to_string(c));
        for (int i = 0; i < 4; ++i) drawn.push_back(verts[i]); }
};
struct Host final : IRegionCloudsHost {
    Dev dev; Tex t0{100}, t1{101}, t2{102}, t3{103}; std::vector<std::uint32_t> asked; bool factory = true;
    IRegionCloudsDevice* Device() override { return &dev; }
    bool HasTextureFactory() override { return factory; }
    IRegionCloudTexture* GetTexture(std::uint32_t id) override { asked.push_back(id); Tex* t[4] = {&t0, &t1, &t2, &t3}; t[id - 0x4a624656]->AddRef(); return t[id - 0x4a624656]; }
};
struct Main final : cGZWin { using cGZWin::cGZWin; };
}

int main() {
    FakeMgr2 m; Host host; SetRegionCloudsHost(&host);
    auto* main = new Main(&m); main->AddRef(); main->SetArea(0, 0, 640, 480); m.mainWin = main;
    auto* w = new cSC4WinRegionClouds(&m); w->AddRef();
    CHECK(w->GetFlag(0x200000) && !w->GetFlag(0x10000));
    w->Init();
    CHECK(w->GetID() == 0x6a0af41d && w->GetW() == 640 && w->GetH() == 480);
    CHECK(host.asked.size() == 4 && host.asked[0] == 0x4a624656 && host.asked[3] == 0x4a624659);
    CHECK(host.t0.refs == 2);

    // drift is normalised
    w->SetDriftVector(3, 4);
    cSC4WinRegionClouds::cCloud c;
    w->SetOpacityRange(0x30, 0x31);
    w->InitCloud(c);
    CHECK(c.x == -128.0f && c.y >= -128.0f && c.y < 480.0f - 128.0f);
    CHECK(c.opacity == 0x30 && c.texture >= 0 && c.texture < 4);
    const float speed = std::sqrt(c.vx * c.vx + c.vy * c.vy);
    CHECK(speed >= 5.0f && speed < 10.0f);
    CHECK(std::fabs(c.vx / c.vy - 0.75f) < 1e-4f);

    // area: count = h*w/16384
    w->SetCloudArea(0, 0, 640, 480);
    CHECK(w->Clouds().size() == 18);   // 307200/16384 = 18.75
    for (auto& cl : w->Clouds()) CHECK(cl.x >= -128.0f && cl.x < 512.0f && cl.texture < 4);
    w->SetCloudArea(0, 0, 100, 100);
    CHECK(w->Clouds().empty());

    // paint: the first call starts the timer (dt 0), clouds do not move; at the right edge they die
    w->MutableClouds().clear();
    w->MutableClouds().push_back({10.0f, 20.0f, 100.0f, 50.0f, 2, 0x44});
    w->MutableClouds().push_back({700.0f, 0.0f, 1.0f, 1.0f, 0, 0x20});   // beyond the area (640): removed
    w->SetOffset(cRZPoint{4, 6});
    g_ticks = 1000;
    // a seed that will not spawn: make sure by checking size afterwards (spawn chance 0.3%)
    w->GZPaint();
    CHECK(w->Clouds().size() >= 1);
    CHECK(host.dev.log.size() >= 10 && host.dev.log[0] == "168:3" && host.dev.log[5] == "blend:45");
    CHECK(host.dev.log[9] == "verts:100");
    CHECK(host.dev.handles.size() >= 1 && host.dev.handles[0] == 102);
    const CloudVertex* d = host.dev.drawn.data();
    CHECK(d[0].x == 6.0f && d[0].y == 14.0f && d[1].x == 134.0f && d[2].y == 142.0f && d[3].x == 134.0f && d[3].y == 142.0f);
    CHECK(d[0].color == 0x44ffffff && d[3].color == 0x44ffffff);
    CHECK(d[1].u == 1.0f && d[1].v == 0.0f && d[2].u == 0.0f && d[2].v == 1.0f);
    CHECK(w->Clouds().front().x == 10.0f);
    g_ticks = 1500;   // elapsed 500 ms is capped at 200 ms
    w->GZPaint();
    CHECK(std::fabs(w->Clouds().front().x - 30.0f) < 1e-3f && std::fabs(w->Clouds().front().y - 30.0f) < 1e-3f);

    // missing texture: no drawing at all
    host.dev.log.clear();
    w->Shutdown();
    w->Release();
    CHECK(host.t0.refs == 1);
    main->Release();
    SetRegionCloudsHost(nullptr);
    if (!failures) std::printf("winregionclouds OK\n");
    return failures;
}
