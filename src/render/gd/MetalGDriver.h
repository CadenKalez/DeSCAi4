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
#include "render/gd/IBackingStore.h"
#include "render/gd/VideoModes.h"

namespace gd {

// Clear mask bits of the shipped driver (not GL's): colour 0x4000, depth 0x1000, stencil 0x2000.
constexpr std::uint32_t kClearColor = 0x4000, kClearDepth = 0x1000, kClearStencil = 0x2000;

class cMetalGDriver final : public cGDriverBase {
public:
    // `caMetalLayer` may be null (headless). Size is the drawable size in pixels.
    static std::unique_ptr<cMetalGDriver> Create(void* caMetalLayer, std::uint32_t width, std::uint32_t height, IModeSource* modes = nullptr);
    ~cMetalGDriver() override;

    // ---- presentation / test hooks (not part of cIGZGDriver) ----
    void Present();                                                   // frame target -> drawable, once per frame
    bool ReadBack(std::vector<std::uint8_t>& bgra, std::uint32_t& width, std::uint32_t& height);   // top-left origin
    // The city view's backing store (colour + depth copies of the frame; see IBackingStore.h).
    IBackingStore* BackingStore();
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
    int CountVideoModes() override;
    bool GetVideoModeInfo(std::uint32_t index, sGDMode& mode) override;
    bool GetVideoModeInfo(sGDMode& mode) override;
    bool SetVideoMode(int mode, void* window, bool windowed, bool vsync) override;

    // textures (handle semantics of the shipped driver: see TextureTable.h)
    void GenTextures(int count, std::uint32_t* ids) override;
    void DeleteTextures(int count, const std::uint32_t* ids) override;
    bool IsTexture(std::uint32_t texture) override;
    void BindTexture(eGDTextureTarget target, std::uint32_t texture) override;
    void TexParameter(eGDTextureTarget target, eGDTextureParamType param, eGDTextureParam value) override;
    void TexEnv(eGDTextureEnvTarget target, eGDTextureEnvParamType param, eGDTextureEnvModeParam mode) override;
    void TexEnv(eGDTextureEnvTarget target, eGDTextureEnvParamType param, const float* values) override;
    void SetCombiner(const cGDCombiner& combiner, std::uint32_t stage) override;
    void TexStage(std::uint32_t stage) override;
    void TexStageCoord(eGDTexCoordSource source) override;
    void TexStageMatrix(const float* m, std::uint32_t a, std::uint32_t b, eGDTexMatFlags flags) override;
    void ColorMultiplier(float r, float g, float b) override;
    void AlphaMultiplier(float a) override;
    void SetTexture(std::uint32_t texture, std::uint32_t stage) override;
    std::uint32_t GetTexture(std::uint32_t stage) const override;
    std::uint32_t CreateTexture(eGDInternalTextureFormat format, std::uint32_t width, std::uint32_t height, std::uint32_t mipLevels, eGDTextureHintFlags hint) override;
    void LoadTextureLevel(std::uint32_t texture, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, eGDTextureFormat format, eGDType type, std::uint32_t rowLength, const void* pixels) override;

private:
    cMetalGDriver();
    void MarkStage(bool enabled);
    void Encode(int primitive, std::size_t vertexOffset, int vertexCount, const std::uint16_t* indices, int indexCount);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gd
