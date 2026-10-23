#include <cstdio>
#include <vector>

#include "gz/DrawSystem.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
struct Engine final : cIGZDrawEngine {
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init(IDrawTargetBuffer*) override { return true; }
    void Shutdown() override {}
    void GetCanvasArea(cRZRect& out) override { out = {0, 0, 16, 16}; }
    void SetClipRect(const cRZRect&) override {}
    void SetColorTable(int, int, const std::uint32_t*) override {}
    void SetAlphaBlend(int) override {}
    void SetBltModulate(bool) override {}
    void SetAntialiasing(bool) override {}
    void SetLineThickness(float) override {}
    void SetGradientTransform(const float (*)[4], float, float) override {}
    bool SetBltSource(cIGZUnknown*, float, float, float, float, cRZRect&) override { return true; }
    void ClearBltTiling() override {}
    void SetBltTiling(const cRZRect&) override {}
    void Flush() override {}
    void Suspend() override {}
    void Resume() override {}
    bool LockTarget(cGZBufferSurfaceDesc&, bool) override { return false; }
    void UnlockTarget() override {}
    eEngineDrawResult Draw(eDrawingOp, const float*, int) override { return eEngineDrawResult::Done; }
    void DrawPreclippedSpanList(const std::uint32_t*, int) override {}
};
Engine g_engine;
struct DcHost final : IDrawContextHost {
    cIGZDrawEngine* CreateEngine(IDrawTargetBuffer*) override { return &g_engine; }
};

struct Factory final : cIGZDrawContextEngineFactory {
    int refs = 1, calls = 0;
    bool accept;
    explicit Factory(bool a) : accept(a) {}
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool CreateEngine(cIGZUnknown*, cIGZDrawEngine** out) override { ++calls; if (accept) *out = &g_engine; return accept; }
};

struct Cache final : cIGZBufferHardwareCache {
    bool resident; std::uint32_t lastUsed; int flushed = 0;
    Cache(bool r, std::uint32_t l) : resident(r), lastUsed(l) {}
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool Attach(cIGZBuffer*) override { return true; }
    void Invalidate() override {}
    void Flush() override { ++flushed; }
    bool IsResident() override { return resident; }
    std::uint32_t GetLastUsedClock() override { return lastUsed; }
};

struct Host final : IDrawSystemHost {
    int added = 0, removed = 0; bool bufferOk = true;
    void AddTickClient(cIGZSystemService*) override { ++added; }
    void RemoveTickClient(cIGZSystemService*) override { ++removed; }
    bool CreateBuffer(cIGZBuffer** out) override {
        if (!bufferOk) return false;
        auto* b = new cGZBuffer();
        b->AddRef();
        *out = b;
        return true;
    }
};

struct Target final : cIGZUnknown {
    int refs = 1;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != kGZIID_cIGZBuffer) return false;
        *out = this;
        ++refs;
        return true;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
gz::cIGZDrawEngine* MakeEngine() { return &g_engine; }
}  // namespace

int main() {
    Host host; DcHost dcHost;
    SetDrawSystemHost(&host);
    SetDrawContextHost(&dcHost);
    auto* sys = new cGZDrawSystem();
    sys->AddRef();
    CHECK(sys->GetServiceID() == kGZSID_cGZDrawSystem && sys->GetServicePriority() == kGZDrawSystemPriority);
    void* raw = nullptr;
    CHECK(sys->QueryInterface(kGZIID_cIGZDrawSystem, &raw) && raw == static_cast<cIGZDrawSystem*>(sys));
    sys->Release();
    CHECK(sys->QueryInterface(kGZIID_cIGZSystemService, &raw));
    sys->Release();
    CHECK(!sys->QueryInterface(0x1234, &raw));

    // Init installs the software factory and registers for ticks.
    CHECK(sys->Init() && host.added == 1);
    Target target;
    cGZDrawContextRenderToBufferFactory::SetEngineCreator(nullptr);
    gz::cIGZDrawEngine* engine = nullptr;
    CHECK(!sys->CreateEngine(&target, &engine));
    cGZDrawContextRenderToBufferFactory::SetEngineCreator(&MakeEngine);
    target.refs = 1; g_engine.refs = 0;
    CHECK(sys->CreateEngine(&target, &engine) && engine == &g_engine && g_engine.refs == 1);
    CHECK(target.refs == 2);   // the buffer reference taken by the query is never released

    // The software factory on its own: its interfaces, and destruction through the last reference.
    {
        auto* factory = new cGZDrawContextRenderToBufferFactory();
        factory->AddRef();
        void* view = nullptr;
        CHECK(factory->QueryInterface(kGZIID_cIGZDrawContextEngineFactory, &view) && view == static_cast<cIGZDrawContextEngineFactory*>(factory));
        CHECK(factory->QueryInterface(kGZIID_cIGZUnknown, &view));
        CHECK(!factory->QueryInterface(0x1234, &view));
        factory->Release(); factory->Release();
        factory->Release();        // the last reference destroys it
    }

    // Added factories go in front; the first that accepts wins.
    Factory refuses(false), accepts(true);
    sys->AddEngine(&refuses);
    sys->AddEngine(&accepts);
    CHECK(refuses.refs == 2 && accepts.refs == 2);
    engine = nullptr;
    CHECK(sys->CreateEngine(&target, &engine) && accepts.calls == 1 && refuses.calls == 0);
    accepts.accept = false;
    CHECK(sys->CreateEngine(&target, &engine) && refuses.calls == 1);   // falls through to the software factory

    // Buffer caches: the clock ticks and old resident caches are flushed every five ticks.
    Cache stale(true, 0), fresh(true, 0), idle(false, 0);
    sys->RegisterBufferCache(&stale);
    sys->RegisterBufferCache(&fresh);
    sys->RegisterBufferCache(&idle);
    CHECK(sys->GetBufferCacheClock() == 0);
    CHECK(sys->OnTick(16));
    fresh.lastUsed = 3;                         // used recently (an earlier stamp would wrap: the age is unsigned)
    for (int i = 0; i < 5; ++i) CHECK(sys->OnTick(16));
    CHECK(sys->GetBufferCacheClock() == 6);
    CHECK(stale.flushed == 1 && fresh.flushed == 0);   // clock 0: nothing old yet; clock 5: stale is 5 steps old
    sys->OnTick(16); sys->OnTick(16); sys->OnTick(16); sys->OnTick(16);   // clock 7..10
    CHECK(sys->GetBufferCacheClock() == 10);
    CHECK(stale.flushed == 1 && fresh.flushed == 0 && idle.flushed == 0);   // no flush pass between clocks 6 and 9
    sys->FlushBufferCaches();
    CHECK(stale.flushed == 2 && fresh.flushed == 1 && idle.flushed == 1);
    sys->UnregisterBufferCache(&fresh);
    sys->UnregisterBufferCache(&fresh);        // unknown caches are ignored
    sys->FlushBufferCaches();
    CHECK(stale.flushed == 3 && fresh.flushed == 1);
    sys->FlushOldBufferCaches(99);             // age argument ignored: clock 10, stale last used 0
    CHECK(stale.flushed == 4);

    // Private buffers wrap a real buffer in a draw context.
    cIGZBuffer* buffer = nullptr;
    CHECK(sys->CreatePrivateBuffer(&buffer, 12, 7, kBufferColorRGB565));
    CHECK(buffer && buffer->Width() == 12 && buffer->Height() == 7);
    CHECK(buffer->GetBitsPerPixel() == 16 && buffer->GetColorType().type == kBufferColorRGB565);
    buffer->Release();
    buffer = nullptr;
    host.bufferOk = false;
    CHECK(!sys->CreatePrivateBuffer(&buffer, 12, 7, kBufferColorRGB565) && !buffer);
    host.bufferOk = true;

    CHECK(sys->Shutdown() && host.removed == 1);
    sys->Release();
    CHECK(refuses.refs == 1 && accepts.refs == 1);   // the system released its references
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
