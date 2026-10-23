// A recording cIGZGDriver for the cSC43DRender test (and any other renderer test that needs one). It answers
// Punt(0x669565fe) with the backing store it was given, so StaticSetGDriver finds the buffer-region extension, and it
// records the viewport calls SetClipRect / UnsetClipRect make.
#pragma once
#include <cstdint>
#include <vector>

#include "render/gd/IBackingStore.h"
#include "render/gd/IGZGDriver.h"
#include "render/gd/VideoModes.h"

namespace sc4test {

struct FakeGDriver final : gd::cIGZGDriver {
    gd::IBackingStore* store = nullptr;          // answered for Punt(0x669565fe)
    std::uint32_t viewport[4] = {0, 0, 0, 0};    // x, y, x + w, y + h
    std::uint32_t setViewport[4] = {0, 0, 0, 0};
    int setViewportCalls = 0;
    int resetViewportCalls = 0;
    int addRefs = 0;
    int releases = 0;
    // The draw-pass call sequence (cSC43DRender::Draw / the viewport rectangles / ClearViewport).
    int clearCalls = 0;
    std::uint32_t lastClearMask = 0;
    int clearColourCalls = 0;
    float lastClearColour[4] = {0, 0, 0, 0};
    int clearDepthCalls = 0;
    double lastClearDepth = 0;
    int interleavedCalls = 0;
    int lastInterleavedFormat = -1;
    int lastInterleavedStride = 0;
    int drawArrayCalls = 0;
    int lastPrimitive = -1;
    int lastDrawCount = 0;
    int textureCalls = 0;
    std::uint32_t lastTexture = 0;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    void AddRef() override { ++addRefs; }
    std::int32_t Release() override { return ++releases; }
    void DrawArrays(gd::eGDPrimType primitive, int, int count) override {
        ++drawArrayCalls;
        lastPrimitive = static_cast<int>(primitive);
        lastDrawCount = count;
    }
    void DrawElements(gd::eGDPrimType, int, gd::eGDType, const void*) override {}
    void InterleavedArrays(gd::eGDVertexFormat format, int stride, const void*) override {
        ++interleavedCalls;
        lastInterleavedFormat = static_cast<int>(format);
        lastInterleavedStride = stride;
    }
    gd::eGDVertexFormat MakeVertexFormat(std::uint32_t, gd::eGDElementType*) override { return 0; }
    gd::eGDVertexFormat MakeVertexFormat(gd::eGDVertexFormat format) override { return format; }
    int VertexFormatStride(gd::eGDVertexFormat) const override { return 0; }
    int VertexFormatElementOffset(gd::eGDVertexFormat, gd::eGDElementType, std::uint32_t) const override { return 0; }
    int VertexFormatNumElements(gd::eGDVertexFormat, gd::eGDElementType) const override { return 0; }
    void Clear(std::uint32_t mask) override {
        ++clearCalls;
        lastClearMask = mask;
    }
    void ClearColor(float r, float g, float b, float a) override {
        ++clearColourCalls;
        lastClearColour[0] = r;
        lastClearColour[1] = g;
        lastClearColour[2] = b;
        lastClearColour[3] = a;
    }
    void ClearDepth(double depth) override {
        ++clearDepthCalls;
        lastClearDepth = depth;
    }
    void ClearStencil(int) override {}
    void ColorMask(bool) override {}
    void DepthFunc(gd::eGDTestFunc) override {}
    void DepthMask(bool) override {}
    void StencilFunc(gd::eGDTestFunc, int, std::uint32_t) override {}
    void StencilMask(std::uint32_t) override {}
    void StencilOp(gd::eGDStencilOp, gd::eGDStencilOp, gd::eGDStencilOp) override {}
    void BlendFunc(gd::eGDBlend, gd::eGDBlend) override {}
    void AlphaFunc(gd::eGDTestFunc, float) override {}
    void ShadeModel(gd::eGDShade) override {}
    void BindTexture(gd::eGDTextureTarget, std::uint32_t) override {}
    void TexImage2D(gd::eGDTextureTarget, int, gd::eGDInternalTextureFormat, int, int, int, gd::eGDTextureFormat,
                    gd::eGDType, const void*) override {}
    void PixelStore(gd::eGDParameter, int) override {}
    void TexEnv(gd::eGDTextureTarget, gd::eGDTextureEnvParamType, gd::eGDTextureEnvModeParam) override {}
    void TexEnv(gd::eGDTextureTarget, gd::eGDTextureEnvParamType, const float*) override {}
    void TexParameter(gd::eGDTextureTarget, gd::eGDTextureParamType, gd::eGDTextureParam) override {}
    void Fog(gd::eGDFogParamType, gd::eGDFogParam) override {}
    void Fog(gd::eGDFogParamType, const float*) override {}
    void ColorMultiplier(float, float, float) override {}
    void AlphaMultiplier(float) override {}
    void EnableVertexColors(bool, bool) override {}
    void GenTextures(int, std::uint32_t*) override {}
    void DeleteTextures(int, const std::uint32_t*) override {}
    bool IsTexture(std::uint32_t) override { return false; }
    void PrioritizeTextures(int, const std::uint32_t*, const float*) override {}
    bool AreTexturesResident(int, const std::uint32_t*, bool*) override { return false; }
    void MatrixMode(gd::eGDMatrixTarget) override {}
    void LoadMatrix(const float*) override {}
    void LoadIdentity() override {}
    void Flush() override {}
    void Enable(gd::eGDDriverState) override {}
    void Disable(gd::eGDDriverState) override {}
    bool IsEnabled(gd::eGDDriverState) override { return false; }
    void GetBoolean(gd::eGDParameter, bool*) override {}
    void GetInteger(gd::eGDParameter, int*) override {}
    void GetFloat(gd::eGDParameter, float*) override {}
    int GetError() override { return 0; }
    void TexStage(std::uint32_t) override {}
    void TexStageCoord(gd::eGDTexCoordSource) override {}
    void TexStageMatrix(const float*, std::uint32_t, std::uint32_t, gd::eGDTexMatFlags) override {}
    void TexStageCombine(gd::eGDTextureStageCombineParamType, gd::eGDTextureStageCombineModeParam) override {}
    void TexStageCombine(gd::eGDTextureStageCombineSourceParamType, gd::eGDTextureStageCombineSourceParam) override {}
    void TexStageCombine(gd::eGDTextureStageCombineOperandParamType, gd::eGDBlend) override {}
    void TexStageCombine(gd::eGDTextureStageCombineScaleParamType, gd::eGDTextureStageCombineScaleParam) override {}
    void SetTexture(std::uint32_t texture, std::uint32_t) override {
        ++textureCalls;
        lastTexture = texture;
    }
    std::uint32_t GetTexture(std::uint32_t) const override { return 0; }
    std::uint32_t CreateTexture(gd::eGDInternalTextureFormat, std::uint32_t, std::uint32_t, std::uint32_t,
                                gd::eGDTextureHintFlags) override {
        return 0;
    }
    void LoadTextureLevel(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                          gd::eGDTextureFormat, gd::eGDType, std::uint32_t, const void*) override {}
    void SetCombiner(const gd::cGDCombiner&, std::uint32_t) override {}
    int CountVideoModes() override { return 1; }
    bool GetVideoModeInfo(std::uint32_t, gd::sGDMode& mode) override {
        mode = gd::sGDMode{};
        return true;
    }
    bool GetVideoModeInfo(gd::sGDMode& mode) override {
        mode = gd::sGDMode{};
        return true;
    }
    bool SetVideoMode(int, void*, bool, bool) override { return true; }
    void PolygonOffset(std::uint32_t) override {}
    void BitBlt(int, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*, bool, const void*) override {}
    void StretchBlt(int, int, int, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*, bool,
                    const void*) override {}
    void BitBltAlpha(int, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*, bool, const void*,
                     std::uint32_t) override {}
    void StretchBltAlpha(int, int, int, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*, bool,
                         const void*, std::uint32_t) override {}
    void BitBltAlphaModulate(int, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*, bool, const void*,
                             std::uint32_t) override {}
    void StretchBltAlphaModulate(int, int, int, int, int, int, gd::eGDTextureFormat, gd::eGDType, const void*, bool,
                                 const void*, std::uint32_t) override {}
    void SetViewport() override { ++resetViewportCalls; }
    void SetViewport(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) override {
        ++setViewportCalls;
        setViewport[0] = x;
        setViewport[1] = y;
        setViewport[2] = w;
        setViewport[3] = h;
    }
    void GetViewport(std::uint32_t out[4]) override {
        for (int i = 0; i < 4; ++i) out[i] = viewport[i];
    }
    const void* GetDriverInfo() override { return nullptr; }
    std::uint32_t GetGZCLSID() const override { return 0; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool IsDeviceReady() override { return true; }
    bool Punt(std::uint32_t iid, void* data) override {
        if (iid == 0x669565fe && data != nullptr) {
            *static_cast<void**>(data) = store;
            return store != nullptr;
        }
        return false;
    }
};

}  // namespace sc4test
