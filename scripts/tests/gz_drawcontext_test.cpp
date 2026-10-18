#include <cstdio>
#include <string>
#include <vector>

#include "gz/DrawContext.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
struct Engine final : cIGZDrawEngine {
    std::vector<std::string> log;
    cRZRect lastClip, canvas{0, 0, 100, 50};
    int lastBlend = -1; float lastThickness = -1;
    std::vector<float> lastPoints; eDrawingOp lastOp = eDrawingOp::Point;
    bool bltResult = true; cRZRect bltClip{10, 10, 20, 20};
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool Init(IDrawTargetBuffer*) override { log.push_back("init"); return true; }
    void Shutdown() override { log.push_back("shutdown"); }
    void GetCanvasArea(cRZRect& out) override { out = canvas; }
    void SetClipRect(const cRZRect& c) override { lastClip = c; log.push_back("clip"); }
    void SetColorTable(int, int, const std::uint32_t*) override { log.push_back("colors"); }
    void SetAlphaBlend(int b) override { lastBlend = b; log.push_back("alpha"); }
    void SetBltModulate(bool) override { log.push_back("mod"); }
    void SetAntialiasing(bool) override { log.push_back("aa"); }
    void SetLineThickness(float t) override { lastThickness = t; log.push_back("thick"); }
    void SetGradientTransform(const float (*)[4], float, float) override { log.push_back("grad"); }
    bool SetBltSource(cIGZUnknown*, float, float, float, float, cRZRect& clip) override { clip = bltClip; log.push_back("bltsrc"); return bltResult; }
    void ClearBltTiling() override { log.push_back("cleartile"); }
    void SetBltTiling(const cRZRect&) override { log.push_back("tile"); }
    void Flush() override { log.push_back("flush"); }
    void Suspend() override { log.push_back("suspend"); }
    void Resume() override { log.push_back("resume"); }
    bool LockTarget(cGZBufferSurfaceDesc&, bool) override { log.push_back("lock"); return true; }
    void UnlockTarget() override { log.push_back("unlock"); }
    eEngineDrawResult Draw(eDrawingOp op, const float* p, int n) override {
        lastOp = op; lastPoints.assign(p, p + n * 2); log.push_back("draw"); return eEngineDrawResult::Done;
    }
    void DrawPreclippedSpanList(const std::uint32_t*, int) override {}
};
struct Host final : IDrawContextHost {
    Engine engine;
    cIGZDrawEngine* CreateEngine(IDrawTargetBuffer*) override { return &engine; }
};
struct Buffer final : IDrawTargetBuffer {
    cRZRect area{0, 0, 100, 50};
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    const cRZRect* GetBufferArea() const override { return &area; }
    std::uint32_t ConvertRGBValueToNative(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { return (r << 16) | (g << 8) | b | 0xff000000u; }
};
}  // namespace

int main() {
    Host host; Buffer buffer;
    SetDrawContextHost(&host);
    auto* dc = new cGZDrawContext();
    dc->AddRef();
    CHECK(!dc->IsTargetAttached());
    dc->SetTarget(&buffer);
    CHECK(dc->IsTargetAttached() && buffer.refs == 1 && dc->GetEngine() == &host.engine);
    CHECK(dc->GetCanvasArea().right == 100 && dc->GetClipRect().bottom == 50);

    // clip + transform stack
    dc->PushTransform();
    dc->Offset({10, 5});
    CHECK(dc->GetOrigin().x == 10 && dc->GetOrigin().y == 5 && dc->GetTransformStackDepth() == 1);
    dc->Clip({0, 0, 20, 20});                         // moved to (10,5)-(30,25)
    CHECK(dc->GetClipRect().left == 10 && dc->GetClipRect().top == 5 && dc->GetClipRect().right == 30 && dc->GetClipRect().bottom == 25);
    CHECK(host.engine.lastClip.right == 30);
    dc->Clip({200, 200, 210, 210});                   // misses entirely
    CHECK(dc->GetClipRect().left == 0 && dc->GetClipRect().right == 0);
    dc->PopTransform();
    CHECK(dc->GetClipRect().right == 100 && dc->GetOrigin().x == 0 && dc->GetTransformStackDepth() == 0);
    dc->ClipReset();

    // drawing goes through the origin
    dc->Offset({3, 4});
    dc->Point(1, 2);
    CHECK(host.engine.lastOp == eDrawingOp::Point && host.engine.lastPoints[0] == 4.5f && host.engine.lastPoints[1] == 6.5f);
    dc->Line(0, 0, 10, 10);
    CHECK(host.engine.lastOp == eDrawingOp::Line && host.engine.lastPoints.size() == 4 && host.engine.lastPoints[2] == 13.5f);
    dc->Ellipse(0, 0, 4, 4);
    CHECK(host.engine.lastOp == eDrawingOp::FillEllipse);       // the original quirk
    dc->Fill({1, 1, 5, 5});
    CHECK(host.engine.lastOp == eDrawingOp::FillRect && host.engine.lastPoints[0] == 4.0f);
    dc->Clear();                                                 // untransformed
    CHECK(host.engine.lastPoints[0] == 0.0f && host.engine.lastPoints[2] == 100.0f);
    dc->OffsetAbsolute({0, 0});

    // state
    dc->PushState();
    dc->SetAlphaBlend(3);
    dc->SetLineThickness(-2.0f);
    CHECK(host.engine.lastBlend == 3 && host.engine.lastThickness == 0.0f);
    dc->SetLineThickness(2.4f);
    dc->PopState();
    CHECK(host.engine.lastBlend == 0 && host.engine.lastThickness == 1.0f);
    const std::size_t before = host.engine.log.size();
    dc->SetAlphaBlend(0);                                        // unchanged: engine not touched
    CHECK(host.engine.log.size() == before);
    dc->SetColor(2, 0x102030);
    CHECK(host.engine.log.back() == "colors");

    // blt sets the engine's clip to the intersection with the blit clip
    Buffer source;
    dc->Blt(&source, {0, 0, 10, 10}, {20, 20, 40, 40});
    CHECK(host.engine.lastPoints.size() == 4);                   // the destination fill
    host.engine.log.clear();
    dc->SetBlt(nullptr, 0, 0, 1, 1);
    CHECK(host.engine.lastClip.left == 10 && host.engine.lastClip.right == 20);   // blit clip inside the user clip
    dc->SetBltTiling({0, 0, 4, 4});
    CHECK(host.engine.lastClip.right == 100);                    // tiling: plain clip again
    dc->ClearBltTiling();
    dc->ClearBlt();
    CHECK(host.engine.lastClip.right == 100);

    dc->Suspend(); dc->Suspend();
    CHECK(dc->IsSuspended());
    dc->Resume();
    CHECK(dc->IsSuspended() && host.engine.log.back() == "suspend");
    dc->Resume();
    CHECK(!dc->IsSuspended() && host.engine.log.back() == "resume");

    dc->SetTarget(nullptr);
    CHECK(!dc->IsTargetAttached() && buffer.refs == 0 && dc->GetEngine() == nullptr);
    dc->Release();
    std::printf(failures ? "FAILED\n" : "gz_drawcontext ok\n");
    return failures != 0;
}
