// cGZDrawContextRenderToBuffer — the software drawing engine behind cGZDrawContext (iid 0x2ae78a07, vtable 0x100adda00,
// 26 functions at 0x100653c8c..0x100654b2a). It owns a lock on the target cIGZBuffer and a per-pixel-format span renderer
// (nGZDrawSoftRender::cSpanRender*): the context hands it clipped span lists and it fills them with a solid colour, a
// gradient or a blit from another buffer (plain, stretched or tiled, optionally colour keyed / modulated / alpha blended),
// and it can rasterize antialiased polygons itself (8x8 supersampling through cRZSpanList).
//
// Object layout of the original: +8 refcount, +0x10 target, +0x18 canvas size, +0x20 clip, +0x30 blt source, +0x38 blt origin,
// +0x40 / +0x44 blt scale (16.16), +0x48 / +0x4c inverse scale, +0x50 scaled flag, +0x51 alpha blend, +0x52 modulate,
// +0x53 antialiasing, +0x54 gradient (base, dx, dy as 4 x i32 each, 16.16 per channel), +0x84 gradient set, +0x85 target
// locked, +0x88 surface description, +0xac tile rectangle, +0xbc tiling, +0xc0 blt rectangle size, +0xc8 span renderer,
// +0xd0 / +0xe0 the four colours (RGB / native).
#pragma once
#include <cstdint>
#include <memory>

#include "gz/Buffer.h"
#include "gz/DrawContext.h"
#include "gz/SpanRender.h"

constexpr std::uint32_t kGZIID_cIGZDrawEngine = 0x2ae78a07;

class cGZDrawContextRenderToBuffer final : public gz::cIGZDrawEngine {
public:
    cGZDrawContextRenderToBuffer();    // 0x100653c8c
    ~cGZDrawContextRenderToBuffer();   // 0x100653cc4

    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x100653da8: 1 and 0x2ae78a07
    std::uint32_t AddRef() override;                               // 0x100653d52
    std::uint32_t Release() override;                              // 0x100653d60

    bool Init(gz::IDrawTargetBuffer* target) override;             // 0x100653dca
    void Shutdown() override;                                      // 0x100653f78
    void GetCanvasArea(cRZRect& out) override;                     // 0x100653fe0
    void SetClipRect(const cRZRect& clip) override;                // 0x100653ff2
    void SetColorTable(int first, int count, const std::uint32_t* colors) override;   // 0x100654008
    void SetAlphaBlend(int blend) override;                        // 0x100654430
    void SetBltModulate(bool on) override;                         // 0x10065443c
    void SetAntialiasing(bool on) override;                        // 0x100654446
    void SetLineThickness(float thickness) override;               // 0x100654450 (ignored)
    void SetGradientTransform(const float (*matrix)[4], float x, float y) override;   // 0x100654068
    bool SetBltSource(cIGZUnknown* source, float x, float y, float sx, float sy, cRZRect& clip) override;   // 0x100654138
    void ClearBltTiling() override;                                // 0x100654400
    void SetBltTiling(const cRZRect& tile) override;               // 0x10065440e
    void Flush() override;                                         // 0x100654456 (nothing to do)
    void Suspend() override;                                       // 0x10065445c
    void Resume() override;                                        // 0x10065448c
    bool LockTarget(gz::cGZBufferSurfaceDesc& desc, bool write) override;   // 0x1006544bc
    void UnlockTarget() override;                                  // 0x1006544f0
    gz::eEngineDrawResult Draw(gz::eDrawingOp op, const float* points, int count) override;   // 0x1006544f6
    void DrawPreclippedSpanList(const std::uint32_t* spans, int count) override;             // 0x100654b2a

    void FillAntialiasedPolygon(const float* points, int count);   // 0x1006545a6

    // --- inspection (tests) ---
    bool TargetLocked() const { return locked_; }
    bool BltScaled() const { return bltScaled_; }
    std::int32_t BltOriginX() const { return bltOriginX_; }
    std::int32_t BltOriginY() const { return bltOriginY_; }
    std::int32_t BltScaleX() const { return bltScaleX_; }
    std::int32_t BltInverseX() const { return bltInverseX_; }
    const nGZDrawSoftRender::cGradientInfo& Gradient() const { return gradient_; }
    bool GradientSet() const { return gradientSet_; }
    std::uint32_t NativeColor(int index) const { return nativeColors_[index]; }

private:
    std::int32_t refCount_ = 0;                                 // +8
    cIGZBuffer* target_ = nullptr;                              // +0x10
    cRZRect clip_{};                                            // +0x20
    cIGZBuffer* bltSource_ = nullptr;                           // +0x30
    std::int32_t bltOriginX_ = 0, bltOriginY_ = 0;              // +0x38, +0x3c
    std::int32_t bltScaleX_ = 0, bltScaleY_ = 0;                // +0x40, +0x44
    std::int32_t bltInverseX_ = 0, bltInverseY_ = 0;            // +0x48, +0x4c
    bool bltScaled_ = false;                                    // +0x50
    bool alphaBlend_ = false;                                   // +0x51
    bool bltModulate_ = false;                                  // +0x52
    bool antialiasing_ = false;                                 // +0x53
    nGZDrawSoftRender::cGradientInfo gradient_{};               // +0x54
    bool gradientSet_ = false;                                  // +0x84
    bool locked_ = false;                                       // +0x85
    nGZDrawSoftRender::cGZBufferSurfaceDesc surface_{};         // +0x88 (reserved[0] = colour type, [1] = bytes per pixel)
    std::int32_t canvasWidth_ = 0, canvasHeight_ = 0;           // +0x18
    cRZRect tile_{};                                            // +0xac
    bool tiling_ = false;                                       // +0xbc
    std::int32_t bltWidth_ = 0, bltHeight_ = 0;                 // +0xc0, +0xc4
    std::unique_ptr<nGZDrawSoftRender::cSpanRender> spanRender_;   // +0xc8
    std::uint32_t rgbColors_[4] = {};                           // +0xd0
    std::uint32_t nativeColors_[4] = {};                        // +0xe0
};
