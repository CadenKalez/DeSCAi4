// cGZDrawContext — the 2-D drawing state machine windows paint with (0x10064edbe..0x1006527fc, iid 0x2ae63219).
// It tracks the target buffer, a translation origin plus clip rectangle (with a transform stack), colour table,
// alpha blend / modulate / antialias / line thickness (with a state stack), an optional blit source and tiling,
// and forwards everything to a drawing engine (cIGZDrawEngine, vtable of cGZDrawContextRenderToBuffer
// 0x100adda10) created by the draw system for the target.
//
// Layout (0x120 bytes): +8 colours[4], +0x18 native colours[4], +0x28 target, +0x30 engine, +0x38 origin (floats),
// +0x40 clip rect, +0x50 antialias, +0x51 blt modulate, +0x54 alpha blend, +0x58 line thickness, +0x5c thickness
// in pixels, +0x60 canvas area, +0x70 blit clip, +0x80 blit active, +0x81 tiling active, +0x84 suspend count,
// +0x88 transform stack, +0xa0 state stack, +0xb8 / +0xd0 / +0xe8 float scratch vectors, +0x100 span vector,
// +0x118 refcount.
//
// Not recovered (left partial): the software Rasterize* / Tesselate* fallbacks the engine asks for when it
// cannot draw a primitive itself (0x100650260..0x1006527fc).
#pragma once
#include <cstdint>
#include <vector>

#include "gz/StreamOps.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZPoint.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZDrawContext = 0x2ae63219;

enum class eDrawingOp : std::uint32_t {
    Point = 0, Line = 1, CubicBezier = 2, FillRect = 3, Ellipse = 4, FillEllipse = 5, FillTri = 6, FillQuad = 7,
    FillPolygon = 8,
};
// What Draw() on the engine answers: how the context should carry the primitive out itself.
enum class eEngineDrawResult : int { Done = 0, Tesselate = 1, Rasterize = 2 };

// What the engine's LockTarget fills in (0x20 bytes copied from the engine): the locked pixels and their layout.
struct cGZBufferSurfaceDesc {
    std::uint8_t* pixels = nullptr;      // +0
    std::int64_t pitch = 0;              // +8 (bytes per row)
    std::uint32_t width = 0;             // +0x10
    std::uint32_t height = 0;            // +0x14
    std::uint32_t format = 0;            // +0x18
    std::uint32_t bytesPerPixel = 0;     // +0x1c
};

// The target buffer as far as the context uses it (cIGZBuffer vslots 0x68 and 0x100).
class IDrawTargetBuffer : public cIGZUnknown {
public:
    virtual const cRZRect* GetBufferArea() const = 0;                                                      // vslot 0x68
    virtual std::uint32_t ConvertRGBValueToNative(std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;    // vslot 0x100
};

// cGZDrawContextRenderToBuffer's interface.
class cIGZDrawEngine : public cIGZUnknown {
public:
    virtual bool Init(IDrawTargetBuffer* target) = 0;                                                  // [3]
    virtual void Shutdown() = 0;                                                                        // [4]
    virtual void GetCanvasArea(cRZRect& out) = 0;                                                       // [5]
    virtual void SetClipRect(const cRZRect& clip) = 0;                                                  // [6]
    virtual void SetColorTable(int first, int count, const std::uint32_t* colors) = 0;                  // [7]
    virtual void SetAlphaBlend(int blend) = 0;                                                          // [8]
    virtual void SetBltModulate(bool on) = 0;                                                           // [9]
    virtual void SetAntialiasing(bool on) = 0;                                                          // [10]
    virtual void SetLineThickness(float thickness) = 0;                                                 // [11]
    virtual void SetGradientTransform(const float (*matrix)[4], float a, float b) = 0;                  // [12]
    virtual bool SetBltSource(cIGZUnknown* source, float x, float y, float sx, float sy, cRZRect& clip) = 0;   // [13]
    virtual void ClearBltTiling() = 0;                                                                  // [14]
    virtual void SetBltTiling(const cRZRect& tile) = 0;                                                 // [15]
    virtual void Flush() = 0;                                                                           // [16]
    virtual void Suspend() = 0;                                                                         // [17]
    virtual void Resume() = 0;                                                                          // [18]
    virtual bool LockTarget(cGZBufferSurfaceDesc& desc, bool write) = 0;                                // [19]
    virtual void UnlockTarget() = 0;                                                                    // [20]
    virtual eEngineDrawResult Draw(eDrawingOp op, const float* points, int count) = 0;                  // [21]
    virtual void DrawPreclippedSpanList(const std::uint32_t* spans, int count) = 0;                     // [22]
};

// What the context needs from the draw system service (cIGZDrawSystem vslot 0x20).
class IDrawContextHost {
public:
    virtual ~IDrawContextHost() = default;
    virtual cIGZDrawEngine* CreateEngine(IDrawTargetBuffer* target) = 0;     // returns a counted reference
};
IDrawContextHost* DrawContextHost();
void SetDrawContextHost(IDrawContextHost* host);

class cIGZDrawContext : public cIGZUnknown {
public:
    virtual void PushTransform() = 0;                                                    // [3]
    virtual void ClipReset() = 0;                                                        // [4]
    virtual void Clip(const cRZRect& rect) = 0;                                          // [5]
    virtual void ClipAbsolute(const cRZRect& rect) = 0;                                  // [6]
    virtual void Offset(const cRZPoint& offset) = 0;                                     // [7]
    virtual void OffsetAbsolute(const cRZPoint& origin) = 0;                             // [8]
    virtual void PopTransform() = 0;                                                     // [9]
    virtual void PushState() = 0;                                                        // [10]
    virtual void PopState() = 0;                                                         // [11]
    virtual cRZRect GetCanvasArea() = 0;                                                 // [12]
    virtual cRZRect GetClipRect() = 0;                                                   // [13]
    virtual cRZPoint GetOrigin() = 0;                                                    // [14]
    virtual int GetTransformStackDepth() = 0;                                            // [15]
    virtual bool IsTargetAttached() = 0;                                                 // [16]
    virtual void SetTarget(IDrawTargetBuffer* target) = 0;                               // [17]
    virtual cIGZDrawEngine* GetEngine() = 0;                                             // [18]
    virtual void ResetToDefaultState() = 0;                                              // [19]
    virtual void SetColor(std::uint32_t rgb) = 0;                                        // [20]
    virtual void SetColor(std::uint32_t index, std::uint32_t rgb) = 0;                   // [21]
    virtual void SetAlphaBlend(int blend) = 0;                                           // [22]
    virtual void SetBltModulate(bool on) = 0;                                            // [23]
    virtual void SetAntialiasing(bool on) = 0;                                           // [24]
    virtual void SetLineThickness(float thickness) = 0;                                  // [25]
    virtual void ClearGradient() = 0;                                                    // [26]
    virtual void SetGradientTransform(const float (*matrix)[4], float a, float b) = 0;   // [27]
    virtual void ClearBlt() = 0;                                                         // [28]
    virtual void SetBlt(cIGZUnknown* source, float x, float y, float sx, float sy) = 0;  // [29]
    virtual void ClearBltTiling() = 0;                                                   // [30]
    virtual void SetBltTiling(const cRZRect& tile) = 0;                                  // [31]
    virtual void Clear() = 0;                                                            // [32]
    virtual void Point(int x, int y) = 0;                                                // [33]
    virtual void Line(int x0, int y0, int x1, int y1) = 0;                               // [34]
    virtual void Fill(const cRZRect& rect) = 0;                                          // [35]
    virtual void Ellipse(int l, int t, int r, int b) = 0;                                // [36]
    virtual void FillEllipse(int l, int t, int r, int b) = 0;                            // [37]
    virtual void Blt(IDrawTargetBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect) = 0;   // [38]
    virtual void BltTiled(IDrawTargetBuffer* source, const cRZRect& tile, const cRZRect& destRect, int dx, int dy) = 0;   // [39]
    virtual void Flush() = 0;                                                            // [40]
    virtual bool IsSuspended() = 0;                                                      // [41]
    virtual void Suspend() = 0;                                                          // [42]
    virtual void Resume() = 0;                                                           // [43]
    virtual void Draw(eDrawingOp op, const float* points, int count) = 0;                // [44]
    virtual void DrawNoTransform(eDrawingOp op, const float* points, int count) = 0;     // [45]
    virtual bool Lock(cGZBufferSurfaceDesc& desc, bool write) = 0;                       // [46]
    virtual void Unlock() = 0;                                                           // [47]
};

class cGZDrawContext final : public cIGZDrawContext {
public:
    cGZDrawContext();
    ~cGZDrawContext();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    void PushTransform() override;
    void ClipReset() override;
    void Clip(const cRZRect& rect) override;
    void ClipAbsolute(const cRZRect& rect) override;
    void Offset(const cRZPoint& offset) override;
    void OffsetAbsolute(const cRZPoint& origin) override;
    void PopTransform() override;
    void PushState() override;
    void PopState() override;
    cRZRect GetCanvasArea() override { return canvas_; }
    cRZRect GetClipRect() override { return clip_; }
    cRZPoint GetOrigin() override { return {static_cast<int>(originX_), static_cast<int>(originY_)}; }
    int GetTransformStackDepth() override { return static_cast<int>(transforms_.size()); }
    bool IsTargetAttached() override { return target_ != nullptr; }
    void SetTarget(IDrawTargetBuffer* target) override;
    cIGZDrawEngine* GetEngine() override { return engine_; }
    void ResetToDefaultState() override;
    void SetColor(std::uint32_t rgb) override { SetColor(0, rgb); }
    void SetColor(std::uint32_t index, std::uint32_t rgb) override;
    void SetAlphaBlend(int blend) override;
    void SetBltModulate(bool on) override;
    void SetAntialiasing(bool on) override;
    void SetLineThickness(float thickness) override;
    void ClearGradient() override { engine_->SetGradientTransform(nullptr, 0.0f, 0.0f); }
    void SetGradientTransform(const float (*matrix)[4], float a, float b) override { engine_->SetGradientTransform(matrix, a, b); }
    void ClearBlt() override;
    void SetBlt(cIGZUnknown* source, float x, float y, float sx, float sy) override;
    void ClearBltTiling() override;
    void SetBltTiling(const cRZRect& tile) override;
    void Clear() override;
    void Point(int x, int y) override;
    void Line(int x0, int y0, int x1, int y1) override;
    void Fill(const cRZRect& rect) override;
    void Ellipse(int l, int t, int r, int b) override;
    void FillEllipse(int l, int t, int r, int b) override;
    void Blt(IDrawTargetBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect) override;
    void BltTiled(IDrawTargetBuffer* source, const cRZRect& tile, const cRZRect& destRect, int dx, int dy) override;
    void Flush() override { engine_->Flush(); }
    bool IsSuspended() override { return suspendCount_ > 0; }
    void Suspend() override;
    void Resume() override;
    void Draw(eDrawingOp op, const float* points, int count) override;
    void DrawNoTransform(eDrawingOp op, const float* points, int count) override;
    bool Lock(cGZBufferSurfaceDesc& desc, bool write) override { return engine_->LockTarget(desc, write); }
    void Unlock() override { engine_->UnlockTarget(); }

    void RecomputeClipRect();

private:
    struct cTransform { float x, y; cRZRect clip; };            // 0x18 bytes
    struct cState { bool antialias, modulate; int alphaBlend; float thickness; int thicknessPixels; };   // 0x10 bytes

    // The engine answers 1 / 2 when it wants the context to tesselate / rasterize the primitive itself; those
    // software fallbacks are not recovered, so such primitives are dropped here.
    void DirectDraw(eDrawingOp op, const float* points, int count);

    std::uint32_t colors_[4] = {};          // +8
    std::uint32_t nativeColors_[4] = {};    // +0x18
    IDrawTargetBuffer* target_ = nullptr;   // +0x28
    cIGZDrawEngine* engine_ = nullptr;      // +0x30
    float originX_ = 0, originY_ = 0;       // +0x38
    cRZRect clip_;                          // +0x40
    bool antialias_ = false;                // +0x50
    bool bltModulate_ = false;              // +0x51
    int alphaBlend_ = 0;                    // +0x54
    float lineThickness_ = 1.0f;            // +0x58
    int thicknessPixels_ = 1;               // +0x5c
    cRZRect canvas_;                        // +0x60
    cRZRect bltClip_;                       // +0x70
    bool bltActive_ = false;                // +0x80
    bool tiling_ = false;                   // +0x81
    int suspendCount_ = 0;                  // +0x84
    std::vector<cTransform> transforms_;    // +0x88
    std::vector<cState> states_;            // +0xa0
    std::vector<float> transformed_;        // +0xb8
    std::vector<float> scratchA_, scratchB_;    // +0xd0, +0xe8
    std::vector<std::uint32_t> spans_;      // +0x100
    std::uint32_t refCount_ = 0;            // +0x118
};

}  // namespace gz
