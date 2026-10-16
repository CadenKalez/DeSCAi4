// A logging cIGZDrawContext for window-control tests.
#pragma once
#include <string>
#include <vector>

#include "gz/DrawContext.h"

namespace fake_dc {
using namespace gz;
struct Ctx final : cIGZDrawContext {
    std::vector<std::string> log;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    void PushTransform() override {} void ClipReset() override {} void Clip(const cRZRect&) override {}
    void ClipAbsolute(const cRZRect&) override {} void Offset(const cRZPoint&) override {}
    void OffsetAbsolute(const cRZPoint&) override {} void PopTransform() override {} void PushState() override { log.push_back("push"); }
    void PopState() override { log.push_back("pop"); } cRZRect GetCanvasArea() override { return {}; } cRZRect GetClipRect() override { return {}; }
    cRZPoint GetOrigin() override { return {}; } int GetTransformStackDepth() override { return 0; }
    bool IsTargetAttached() override { return true; } void SetTarget(IDrawTargetBuffer*) override {}
    cIGZDrawEngine* GetEngine() override { return nullptr; } void ResetToDefaultState() override {}
    void SetColor(std::uint32_t rgb) override { log.push_back("color " + std::to_string(rgb)); }
    void SetColor(std::uint32_t, std::uint32_t) override {} void SetAlphaBlend(int) override {}
    void SetBltModulate(bool) override {} void SetAntialiasing(bool) override {} void SetLineThickness(float) override {}
    void ClearGradient() override {} void SetGradientTransform(const float (*)[4], float, float) override {}
    void ClearBlt() override {} void SetBlt(cIGZUnknown*, float, float, float, float) override {}
    void ClearBltTiling() override {} void SetBltTiling(const cRZRect&) override {} void Clear() override {}
    void Point(int, int) override {}
    void Line(int a, int b, int c, int d) override { log.push_back("line " + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c) + "," + std::to_string(d)); }
    void Fill(const cRZRect& r) override { log.push_back("fill " + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," + std::to_string(r.bottom)); }
    void Ellipse(int, int, int, int) override {} void FillEllipse(int, int, int, int) override {}
    void Blt(IDrawTargetBuffer*, const cRZRect& s, const cRZRect& d) override {
        log.push_back("blt " + std::to_string(s.left) + "," + std::to_string(s.top) + "," + std::to_string(s.right) + "," + std::to_string(s.bottom) +
                      ">" + std::to_string(d.left) + "," + std::to_string(d.top) + "," + std::to_string(d.right) + "," + std::to_string(d.bottom)); }
    void BltTiled(IDrawTargetBuffer*, const cRZRect&, const cRZRect&, int, int) override { log.push_back("tile"); }
    void Flush() override {} bool IsSuspended() override { return false; } void Suspend() override {} void Resume() override {}
    void Draw(eDrawingOp, const float*, int) override {} void DrawNoTransform(eDrawingOp, const float*, int) override {}
    void Lock(cGZBufferSurfaceDesc&, bool) override {} void Unlock() override {}
};
}  // namespace fake_dc
