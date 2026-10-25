// cGZDrawContextRenderToHardware — the hardware drawing engine behind cGZDrawContext (iid 0x2ae78a07, vtable
// 0x100addaf0, 32 functions at 0x100654eb0..0x1007b4f4c). It owns the buffer attached as its target and turns every
// primitive into a vertex batch: a fill becomes a quad list of coloured vertices, a triangle list becomes a triangle list,
// and a blit source is cut into tiles/fragments by the hardware before each fragment is drawn with its own clip rectangle
// and ortho transform. The state machine (clip intersection, blt geometry, gradient ramps, dirty flags) is faithful; the
// actual Direct3D/Metal calls sit behind IHardwareRenderHost.
//
// The original implements the hardware part as the class's own *derived* class: cGZDrawContextRenderToSimGL (0x1007b38ec..
// 0x1007b4e58) overrides the ten pure virtuals that occupy vtable slots 0xc8..0x110, right after cIGZDrawEngine's 23 slots
// and this class's two deleting-destructor slots. This port keeps those ten calls as the seam below instead of a second
// vtable group, so the engine can be instantiated directly; the original slots are noted on every seam method.
//
// Layout of the original (0xf0 bytes): +8 refcount, +0x10 target cIGZBuffer, +0x18/+0x1c width/height, +0x20 clip,
// +0x30 blt source, +0x38/+0x3c blt origin, +0x40/+0x44 blt scale, +0x48 blt scaled, +0x49 blt modulate, +0x4a alpha
// blend, +0x4b strong alpha blend, +0x4c antialias, +0x4d state dirty, +0x4e transform dirty, +0x50 tile rect, +0x60
// tiling, +0x64 gradient base[4] (24.8 B/G/R/A), +0x74 gradient dx[4], +0x84 gradient dy[4], +0x94 gradient set,
// +0x98 vertex batch, +0xb0 image textures, +0xc8 image fragments, +0xe0 native colours[4].
// The port adds one pointer past the end (+0xf0) for the seam.
//
// Quirks kept (all asserted in the test):
// * SetBltSource derives its destination rectangle from the *target's* area (not the source's), rounds a scaled one
//   through the original's magic-number sequence (round to nearest, ties toward -infinity) and shifts it by the
//   *truncated* blt origin.
// * SetClipRect accepts an inverted rectangle (right < left or bottom < top) whose edges still straddle the target
//   area, and keeps it inverted; it never re-checks the intersection for emptiness. It also always re-raises the
//   transform-dirty flag, so restoring a clip forces another UpdateTransform.
// * SetColorTable swizzles ARGB to ABGR with integer masks instead of asking the target (cGZDrawContextRenderToBuffer
//   asks the buffer for each native colour).
// * DrawRects ignores the gradient entirely and always colours its vertices with native colour 0; DrawTriangles uses
//   0xffffffff as the default vertex colour when a blt source is installed but only native colour 0 when there is none.
// * DrawTriangleBlt normalises blit texture coordinates with the *scaled* texture size (1 / (scale * texture size)), and
//   uses the fragment's u0/v0 as the texture-space origin of the fragment.
// * UpdateTransform builds a D3D-style ortho matrix whose last row is the translation (1 - left/width, 1 - top/height,
//   0.5, 1); with an empty clip it installs a degenerate matrix that has only z = 0.5, w = 1.
// * Shutdown only tells the backend; the target stays attached until the object is destroyed.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/Buffer.h"
#include "gz/DrawContext.h"

// One hardware vertex as the original lays it out (0x18 bytes, vertex format 10 / stride 0x18 in the backend).
struct cGZDrawVertex {
    float x = 0, y = 0, z = 0.5f;     // +0, +4, +8
    std::uint32_t color = 0;          // +0xc (ABGR)
    float u = 0, v = 0;               // +0x10, +0x14
};
static_assert(sizeof(cGZDrawVertex) == 0x18);

// A texture the backend wants to draw this frame (cGZDrawContextRenderToHardware::cImageTexture, 0x10 bytes):
// buffer-cache texture id, the number of fragments that follow it, and the atlas size used to normalise u/v.
struct cGZDrawImageTexture {
    std::uint32_t id = 0;             // +0x0
    std::uint32_t fragmentCount = 0;  // +0x4
    std::uint32_t width = 0;          // +0x8
    std::uint32_t height = 0;         // +0xc
};
static_assert(sizeof(cGZDrawImageTexture) == 0x10);

// One tile of the blt source (0x20 bytes): the source-space rectangle, then its texture coordinates and size.
struct cGZDrawImageFragment {
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;   // +0x00..+0x0c
    std::int32_t u0 = 0, v0 = 0, u1 = 0, v1 = 0;             // +0x10..+0x1c
};
static_assert(sizeof(cGZDrawImageFragment) == 0x20);

// Everything that would be real Direct3D/Metal work. Each method stands for one call of the original through the
// class's continued vtable; the slot and the cGZDrawContextRenderToSimGL implementation address are in the comments.
// cGZDrawContextRenderToSimGL is not recovered, so this is a seam: a port that wants to draw installs a host with
// SetHardwareRenderHost(), exactly like the game installs the SimGL subclass.
class IHardwareRenderHost {
public:
    virtual ~IHardwareRenderHost() = default;
    virtual bool Init() = 0;                                                              // vslot 0xc8  (0x1007b4588)
    virtual void Shutdown() = 0;                                                          // vslot 0xd0  (0x1007b466a)
    virtual void UpdateState() = 0;                                                       // vslot 0xd8  (0x1007b4724)
    virtual void SetViewport(const cRZRect& viewport) = 0;                                // vslot 0xe0  (0x1007b4950)
    virtual void SetTransform(const float* matrix) = 0;                                   // vslot 0xe8  (0x1007b498a)
    virtual bool SetImage(cIGZBuffer* image) = 0;                                         // vslot 0xf0  (0x1007b49ce)
    virtual void GetImageFragments(std::vector<cGZDrawImageTexture>& textures,
                                   std::vector<cGZDrawImageFragment>& fragments) = 0;      // vslot 0xf8  (0x1007b4b6c)
    virtual void SetTexture(std::uint32_t texture, bool scaled, bool tiled) = 0;          // vslot 0x100 (0x1007b4cf6)
    virtual void DrawTriangleList(cGZDrawVertex* vertices, int count) = 0;                // vslot 0x108 (0x1007b4de2)
    virtual void DrawQuadList(cGZDrawVertex* vertices, int count) = 0;                    // vslot 0x110 (0x1007b4e58)
    // The derived class's own Suspend / Resume (0x1007b3abe / 0x1007b3b5e) run after / instead of the engine's.
    virtual void Suspend() {}
    virtual void Resume() {}
};

class cGZDrawContextRenderToHardware : public gz::cIGZDrawEngine {
public:
    cGZDrawContextRenderToHardware();                       // 0x100654eb0
    virtual ~cGZDrawContextRenderToHardware();              // 0x100654f2c

    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x100655016: 1 and 0x2ae78a07
    std::uint32_t AddRef() override;                               // 0x100654fde
    std::uint32_t Release() override;                              // 0x100654fec

    bool Init(gz::IDrawTargetBuffer* target) override;             // 0x100655038
    void Shutdown() override;                                      // 0x1006550ce
    void GetCanvasArea(cRZRect& out) override;                     // 0x1006550dc
    void SetClipRect(const cRZRect& clip) override;                // 0x1006550ee
    void SetColorTable(int first, int count, const std::uint32_t* colors) override;   // 0x1006551a4
    void SetAlphaBlend(int blend) override;                        // 0x100655574
    void SetBltModulate(bool on) override;                         // 0x10065558c
    void SetAntialiasing(bool on) override;                        // 0x10065559a
    void SetLineThickness(float thickness) override;               // 0x1006555a4 (ignored)
    void SetGradientTransform(const float (*matrix)[4], float x, float y) override;   // 0x100655496
    bool SetBltSource(cIGZUnknown* source, float x, float y, float sx, float sy, cRZRect& clip) override;   // 0x1006551ce
    void ClearBltTiling() override;                                // 0x100655472
    void SetBltTiling(const cRZRect& tile) override;               // 0x10065547c
    void Flush() override;                                         // 0x1007b4f4c (nothing to do)
    void Suspend() override;                                       // 0x1006555aa
    void Resume() override;                                        // 0x100656e7c (nothing to do)
    bool LockTarget(gz::cGZBufferSurfaceDesc& desc, bool write) override;   // 0x100656e82 (false: no lockable surface)
    void UnlockTarget() override;                                  // 0x100656e8a (nothing to do)
    gz::eEngineDrawResult Draw(gz::eDrawingOp op, const float* points, int count) override;   // 0x1006555b6
    void DrawPreclippedSpanList(const std::uint32_t* spans, int count) override;             // 0x100656a7c (nothing to do)

    // 0x10065564c / 0x100655782 / 0x100655bb6 / 0x100656bbc / 0x100656afe
    void UpdateTransform();
    void DrawTriangles(const float* points, int count);
    void DrawRects(const float* points, int count);
    void DrawTriangleBlt(float x, float y, const cRZRect& rect);
    std::uint32_t ComputeGradientColor(int x, int y);

    // The seam. The game installs cGZDrawContextRenderToSimGL here; a port installs its Metal backend.
    IHardwareRenderHost* HardwareRenderHost() const { return hardware_; }
    void SetHardwareRenderHost(IHardwareRenderHost* host) { hardware_ = host; }

    // --- inspection (tests) ---
    cIGZBuffer* Target() const { return target_; }
    std::int32_t CanvasWidth() const { return width_; }
    std::int32_t CanvasHeight() const { return height_; }
    const cRZRect& ClipRect() const { return clip_; }
    const cRZRect& TileRect() const { return tile_; }
    cIGZBuffer* BltSource() const { return bltSource_; }
    bool BltScaled() const { return bltScaled_; }
    bool BltModulate() const { return bltModulate_; }
    bool AlphaBlend() const { return alphaBlend_; }
    bool AlphaBlendStrong() const { return alphaBlendStrong_; }
    bool Antialiasing() const { return antialiasing_; }
    bool StateDirty() const { return dirty_; }
    bool TransformDirty() const { return transformDirty_; }
    bool Tiling() const { return tiling_; }
    bool GradientSet() const { return gradientSet_; }
    std::uint32_t NativeColor(int index) const { return nativeColors_[index]; }
    const std::int32_t* GradientBase() const { return gradientBase_; }
    const std::int32_t* GradientDx() const { return gradientDx_; }
    const std::int32_t* GradientDy() const { return gradientDy_; }
    const std::vector<cGZDrawVertex>& Vertices() const { return vertices_; }
    const std::vector<cGZDrawImageTexture>& ImageTextures() const { return imageTextures_; }
    const std::vector<cGZDrawImageFragment>& ImageFragments() const { return imageFragments_; }

private:
    std::int32_t refCount_ = 0;                            // +8
    cIGZBuffer* target_ = nullptr;                         // +0x10
    std::int32_t width_ = 0, height_ = 0;                  // +0x18, +0x1c
    cRZRect clip_{};                                       // +0x20
    cIGZBuffer* bltSource_ = nullptr;                      // +0x30
    float bltX_ = 0, bltY_ = 0;                            // +0x38, +0x3c
    float bltScaleX_ = 1.0f, bltScaleY_ = 1.0f;            // +0x40, +0x44
    bool bltScaled_ = false;                               // +0x48
    bool bltModulate_ = false;                             // +0x49
    bool alphaBlend_ = false;                              // +0x4a
    bool alphaBlendStrong_ = false;                        // +0x4b
    bool antialiasing_ = false;                            // +0x4c
    bool dirty_ = false;                                   // +0x4d
    bool transformDirty_ = false;                          // +0x4e
    cRZRect tile_{};                                       // +0x50
    bool tiling_ = false;                                  // +0x60
    std::int32_t gradientBase_[4] = {};                    // +0x64 (24.8 fixed point, B/G/R/A)
    std::int32_t gradientDx_[4] = {};                      // +0x74
    std::int32_t gradientDy_[4] = {};                      // +0x84
    bool gradientSet_ = false;                             // +0x94
    std::vector<cGZDrawVertex> vertices_;                  // +0x98
    std::vector<cGZDrawImageTexture> imageTextures_;       // +0xb0
    std::vector<cGZDrawImageFragment> imageFragments_;     // +0xc8
    std::uint32_t nativeColors_[4] = {};                   // +0xe0
    IHardwareRenderHost* hardware_ = nullptr;              // +0xf0 (port-only seam pointer; past the original object)
};
