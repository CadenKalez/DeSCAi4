// cMetalGDriver — the native Metal implementation of cIGZGDriver (RENDER_ARCH.md). No OpenGL, no D3D7 emulation: the slot
// calls update a state mirror (cGDriverBase), the mirror is turned into a pipeline key, and draws are encoded straight into
// Metal. Only the state combinations the survey found SC4 using are implemented; anything else is counted in
// UnsupportedCalls() rather than emulated.
//
// Implemented so far (bring-up step 2a/2b): clear (with the shipped driver's colour quantisation and scissor behaviour),
// viewport / scissor, depth / alpha-test / blend / cull state, matrices, V3F_C4UB_T2F and V3F_C4UB_2T2F client-array draws
// (triangles, strips, fans, quads), present and read-back.
#pragma once
#include <memory>
#include <vector>

#include "render/gd/GDriverBase.h"

namespace gd {

// Clear mask bits of the shipped driver (not GL's): colour 0x4000, depth 0x1000, stencil 0x2000.
constexpr std::uint32_t kClearColor = 0x4000, kClearDepth = 0x1000, kClearStencil = 0x2000;

class cMetalGDriver final : public cGDriverBase {
public:
    // `caMetalLayer` may be null (headless). Size is the drawable size in pixels.
    static std::unique_ptr<cMetalGDriver> Create(void* caMetalLayer, std::uint32_t width, std::uint32_t height);
    ~cMetalGDriver() override;

    // ---- presentation / test hooks (not part of cIGZGDriver) ----
    void Present();                                                   // frame target -> drawable, once per frame
    bool ReadBack(std::vector<std::uint8_t>& bgra, std::uint32_t& width, std::uint32_t& height);   // top-left origin
    std::uint32_t DrawCalls() const;
    std::uint32_t PipelineCount() const;

    // ---- cIGZGDriver ----
    void Clear(std::uint32_t mask) override;
    void ClearColor(float r, float g, float b, float a) override;
    void ColorMask(bool enable) override;                             // a flush-only no-op in the shipped driver
    void DepthFunc(eGDTestFunc func) override;
    void DepthMask(bool enable) override;
    void BlendFunc(eGDBlend src, eGDBlend dst) override;
    void AlphaFunc(eGDTestFunc func, float ref) override;
    void Enable(eGDDriverState state) override;
    void Disable(eGDDriverState state) override;
    void MatrixMode(eGDMatrixTarget target) override;
    void LoadMatrix(const float* m) override;
    void LoadIdentity() override;
    void InterleavedArrays(eGDVertexFormat format, int stride, const void* data) override;
    void DrawArrays(eGDPrimType mode, int first, int count) override;
    void DrawElements(eGDPrimType mode, int count, eGDType indexType, const void* indices) override;
    void Flush() override;
    void SetViewport() override;
    void SetViewport(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) override;
    void GetViewport(std::uint32_t out[4]) override;
    int VertexFormatStride(eGDVertexFormat format) const override;
    int GetError() override;

private:
    cMetalGDriver();
    void Encode(int primitive, std::size_t vertexOffset, int vertexCount, const std::uint16_t* indices, int indexCount);
    float pendingClearRGBA_[4] = {0, 0, 0, 0};
    double pendingClearDepth_ = 1.0;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gd
