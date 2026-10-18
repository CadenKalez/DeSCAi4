// A logging cIGZDrawContext for window-control tests.
#pragma once
#include <string>
#include <vector>

#include "gz/DrawContext.h"

namespace fake_dc {
using namespace gz;
struct Ctx : cIGZDrawContext {
    std::vector<std::string> log;
    // Extra recording for the tests of the buffer-over-context adapters (the `log` above is left alone).
    std::vector<std::string> calls;
    bool attached = true, suspended = false, lockResult = true;
    int refs = 1;
    cGZBufferSurfaceDesc lockDesc;
    cRZPoint origin{0, 0};
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void PushTransform() override { calls.push_back("push"); } void ClipReset() override {}
    void Clip(const cRZRect& r) override { calls.push_back("clip " + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," + std::to_string(r.bottom)); }
    void ClipAbsolute(const cRZRect&) override {} void Offset(const cRZPoint&) override {}
    void OffsetAbsolute(const cRZPoint&) override {} void PopTransform() override { calls.push_back("pop"); } void PushState() override { log.push_back("push"); }
    void PopState() override { log.push_back("pop"); } cRZRect GetCanvasArea() override { return {}; } cRZRect GetClipRect() override { return {}; }
    cRZPoint GetOrigin() override { return origin; } int GetTransformStackDepth() override { return 0; }
    bool IsTargetAttached() override { return attached; } void SetTarget(IDrawTargetBuffer* t) override { attached = t != nullptr; calls.push_back(t ? "target" : "untarget"); }
    cIGZDrawEngine* GetEngine() override { return nullptr; } void ResetToDefaultState() override {}
    void SetColor(std::uint32_t rgb) override { log.push_back("color " + std::to_string(rgb)); }
    void SetColor(std::uint32_t i, std::uint32_t c) override { calls.push_back("color " + std::to_string(i) + " " + std::to_string(c)); } void SetAlphaBlend(int) override {}
    void SetBltModulate(bool) override {} void SetAntialiasing(bool) override {} void SetLineThickness(float t) override { calls.push_back("thick " + std::to_string(int(t))); }
    void ClearGradient() override {} void SetGradientTransform(const float (*)[4], float, float) override {}
    void ClearBlt() override {} void SetBlt(cIGZUnknown*, float, float, float, float) override {}
    void ClearBltTiling() override {} void SetBltTiling(const cRZRect&) override {} void Clear() override { calls.push_back("clear"); }
    void Point(int x, int y) override { calls.push_back("point " + std::to_string(x) + "," + std::to_string(y)); }
    void Line(int a, int b, int c, int d) override { log.push_back("line " + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c) + "," + std::to_string(d)); }
    void Fill(const cRZRect& r) override { log.push_back("fill " + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," + std::to_string(r.bottom)); }
    void Ellipse(int, int, int, int) override {} void FillEllipse(int, int, int, int) override {}
    void Blt(IDrawTargetBuffer*, const cRZRect& s, const cRZRect& d) override {
        log.push_back("blt " + std::to_string(s.left) + "," + std::to_string(s.top) + "," + std::to_string(s.right) + "," + std::to_string(s.bottom) +
                      ">" + std::to_string(d.left) + "," + std::to_string(d.top) + "," + std::to_string(d.right) + "," + std::to_string(d.bottom)); }
    void BltTiled(IDrawTargetBuffer*, const cRZRect&, const cRZRect&, int, int) override { log.push_back("tile"); }
    void Flush() override { calls.push_back("flush"); } bool IsSuspended() override { return suspended; } void Suspend() override { suspended = true; calls.push_back("suspend"); } void Resume() override { suspended = false; calls.push_back("resume"); }
    void Draw(eDrawingOp op, const float* p, int n) override { calls.push_back("draw " + std::to_string(int(op)) + " " + std::to_string(int(p[0])) + "," + std::to_string(int(p[1])) + "," + std::to_string(int(p[2])) + "," + std::to_string(int(p[3])) + " n=" + std::to_string(n)); } void DrawNoTransform(eDrawingOp, const float*, int) override {}
    bool Lock(cGZBufferSurfaceDesc& d, bool write) override { calls.push_back(write ? "lock w" : "lock r"); d = lockDesc; return lockResult; } void Unlock() override {}
};
}  // namespace fake_dc
