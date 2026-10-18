// cGDriverBase — shared half of every cIGZGDriver implementation here: it mirrors the *requested* fixed-function state from
// the slot calls (the mirror is what pipelines are keyed on, and what the packed-render-state assertion checks) and counts
// calls the back end does not support yet, instead of silently ignoring them.
#pragma once
#include <cstdint>
#include <map>
#include <string>

#include "render/gd/IGZGDriver.h"

namespace gd {

// Observable value conversions of the shipped driver.
std::uint32_t QuantizeUnitFloat(float f);           // ClearColor channel: round-half-even(f*256) with the 256 -> 255 fix-up
std::uint32_t PackClearColor(float r, float g, float b, float a);   // 0xAARRGGBB as handed to the D3D clear
std::uint32_t AlphaRefByte(float ref);              // AlphaFunc reference as the 8-bit D3D alpha ref (same quantisation)

struct StageMirror {
    bool enabled = false;
    std::uint32_t texture = 0;
    std::uint32_t envMode = 0;
};

struct StateMirror {
    // requested state (slot calls)
    bool colorMask = true;              // note: ColorMask() is a no-op in the shipped driver (flush only); kept for the assertion
    bool depthWrite = false, depthTest = false, alphaTest = false, blend = false, cull = false, stencil = false, fog = false;
    eGDTestFunc depthFunc = 0, alphaFunc = 0;
    float alphaRef = 0.0f;
    eGDBlend blendSrc = 0, blendDst = 0;
    std::uint32_t currentStage = 0;
    StageMirror stage[2];
    // clear values
    std::uint32_t clearColor = 0;       // packed 0xAARRGGBB
    float clearDepth = 1.0f;
    int clearStencil = 0;
};

class cGDriverBase : public cIGZGDriver {
public:
    // Calls that reached a back end which does not implement them yet (name -> count).
    const std::map<std::string, int>& UnsupportedCalls() const { return unsupported_; }
    const StateMirror& Mirror() const { return mirror_; }

    bool QueryInterface(std::uint32_t, void**) override { return Unsupported("QueryInterface"), false; }
    void AddRef() override { ++refCount_; }
    std::int32_t Release() override { return --refCount_; }
    void DrawArrays(eGDPrimType, int, int) override { Unsupported("DrawArrays"); }
    void DrawElements(eGDPrimType, int, eGDType, const void*) override { Unsupported("DrawElements"); }
    void InterleavedArrays(eGDVertexFormat, int, const void*) override { Unsupported("InterleavedArrays"); }
    eGDVertexFormat MakeVertexFormat(std::uint32_t, eGDElementType*) override { Unsupported("MakeVertexFormat"); return 0; }
    eGDVertexFormat MakeVertexFormat(eGDVertexFormat f) override { Unsupported("MakeVertexFormat"); return f; }
    int VertexFormatStride(eGDVertexFormat) const override { Unsupported("VertexFormatStride"); return 0; }
    int VertexFormatElementOffset(eGDVertexFormat, eGDElementType, std::uint32_t) const override { Unsupported("VertexFormatElementOffset"); return 0; }
    int VertexFormatNumElements(eGDVertexFormat, eGDElementType) const override { Unsupported("VertexFormatNumElements"); return 0; }
    void Clear(std::uint32_t) override { Unsupported("Clear"); }
    void ClearColor(float r, float g, float b, float a) override { mirror_.clearColor = PackClearColor(r, g, b, a); }
    void ClearDepth(double d) override { mirror_.clearDepth = static_cast<float>(d); }
    void ClearStencil(int v) override { mirror_.clearStencil = v; }
    void ColorMask(bool e) override { mirror_.colorMask = e; }
    void DepthFunc(eGDTestFunc f) override { mirror_.depthFunc = f; }
    void DepthMask(bool e) override { mirror_.depthWrite = e; }
    void StencilFunc(eGDTestFunc, int, std::uint32_t) override { Unsupported("StencilFunc"); }
    void StencilMask(std::uint32_t) override { Unsupported("StencilMask"); }
    void StencilOp(eGDStencilOp, eGDStencilOp, eGDStencilOp) override { Unsupported("StencilOp"); }
    void BlendFunc(eGDBlend s, eGDBlend d) override { mirror_.blendSrc = s; mirror_.blendDst = d; }
    void AlphaFunc(eGDTestFunc f, float ref) override { mirror_.alphaFunc = f; mirror_.alphaRef = ref; }
    void ShadeModel(eGDShade) override { Unsupported("ShadeModel"); }
    void BindTexture(eGDTextureTarget, std::uint32_t) override { Unsupported("BindTexture"); }
    void TexImage2D(eGDTextureTarget, int, eGDInternalTextureFormat, int, int, int, eGDTextureFormat, eGDType, const void*) override { Unsupported("TexImage2D"); }
    void PixelStore(eGDParameter, int) override { Unsupported("PixelStore"); }
    void TexEnv(eGDTextureEnvTarget, eGDTextureEnvParamType, eGDTextureEnvModeParam mode) override { mirror_.stage[mirror_.currentStage & 1].envMode = mode; }
    void TexEnv(eGDTextureEnvTarget, eGDTextureEnvParamType, const float*) override { Unsupported("TexEnv(float*)"); }
    void TexParameter(eGDTextureTarget, eGDTextureParamType, eGDTextureParam) override { Unsupported("TexParameter"); }
    void Fog(eGDFogParamType, eGDFogParam) override { Unsupported("Fog"); }
    void Fog(eGDFogParamType, const float*) override { Unsupported("Fog(float*)"); }
    void ColorMultiplier(float, float, float) override { Unsupported("ColorMultiplier"); }
    void AlphaMultiplier(float) override { Unsupported("AlphaMultiplier"); }
    void EnableVertexColors(bool, bool) override { Unsupported("EnableVertexColors"); }
    void GenTextures(int, std::uint32_t*) override { Unsupported("GenTextures"); }
    void DeleteTextures(int, const std::uint32_t*) override { Unsupported("DeleteTextures"); }
    bool IsTexture(std::uint32_t) override { Unsupported("IsTexture"); return false; }
    void PrioritizeTextures(int, const std::uint32_t*, const float*) override { Unsupported("PrioritizeTextures"); }
    bool AreTexturesResident(int, const std::uint32_t*, bool*) override { Unsupported("AreTexturesResident"); return false; }
    void MatrixMode(eGDMatrixTarget) override { Unsupported("MatrixMode"); }
    void LoadMatrix(const float*) override { Unsupported("LoadMatrix"); }
    void LoadIdentity() override { Unsupported("LoadIdentity"); }
    void Flush() override {}
    void Enable(eGDDriverState s) override { SetState(s, true); }
    void Disable(eGDDriverState s) override { SetState(s, false); }
    bool IsEnabled(eGDDriverState) override { Unsupported("IsEnabled"); return false; }
    void GetBoolean(eGDParameter, bool*) override { Unsupported("GetBoolean"); }
    void GetInteger(eGDParameter, int*) override { Unsupported("GetInteger"); }
    void GetFloat(eGDParameter, float*) override { Unsupported("GetFloat"); }
    int GetError() override { return 0; }
    void TexStage(std::uint32_t s) override { mirror_.currentStage = s; }
    void TexStageCoord(eGDTexCoordSource) override { Unsupported("TexStageCoord"); }
    void TexStageMatrix(const float*, std::uint32_t, std::uint32_t, eGDTexMatFlags) override { Unsupported("TexStageMatrix"); }
    void TexStageCombine(eGDTextureStageCombineParamType, eGDTextureStageCombineModeParam) override { Unsupported("TexStageCombine"); }
    void TexStageCombine(eGDTextureStageCombineSourceParamType, eGDTextureStageCombineSourceParam) override { Unsupported("TexStageCombine"); }
    void TexStageCombine(eGDTextureStageCombineOperandParamType, eGDBlend) override { Unsupported("TexStageCombine"); }
    void TexStageCombine(eGDTextureStageCombineScaleParamType, eGDTextureStageCombineScaleParam) override { Unsupported("TexStageCombine"); }
    void SetTexture(std::uint32_t, std::uint32_t) override { Unsupported("SetTexture"); }
    std::uint32_t GetTexture(std::uint32_t) const override { Unsupported("GetTexture"); return 0; }
    std::uint32_t CreateTexture(eGDInternalTextureFormat, std::uint32_t, std::uint32_t, std::uint32_t, eGDTextureHintFlags) override { Unsupported("CreateTexture"); return 0; }
    void LoadTextureLevel(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, eGDTextureFormat, eGDType, std::uint32_t, const void*) override { Unsupported("LoadTextureLevel"); }
    void SetCombiner(const cGDCombiner&, std::uint32_t) override { Unsupported("SetCombiner"); }
    int CountVideoModes() override { Unsupported("CountVideoModes"); return 0; }
    bool GetVideoModeInfo(std::uint32_t, sGDMode&) override { Unsupported("GetVideoModeInfo"); return false; }
    bool GetVideoModeInfo(sGDMode&) override { Unsupported("GetVideoModeInfo"); return false; }
    bool SetVideoMode(int, void*, bool, bool) override { Unsupported("SetVideoMode"); return false; }
    void PolygonOffset(std::uint32_t) override { Unsupported("PolygonOffset"); }
    void BitBlt(int, int, int, int, eGDTextureFormat, eGDType, const void*, bool, const void*) override { Unsupported("BitBlt"); }
    void StretchBlt(int, int, int, int, int, int, eGDTextureFormat, eGDType, const void*, bool, const void*) override { Unsupported("StretchBlt"); }
    void BitBltAlpha(int, int, int, int, eGDTextureFormat, eGDType, const void*, bool, const void*, std::uint32_t) override { Unsupported("BitBltAlpha"); }
    void StretchBltAlpha(int, int, int, int, int, int, eGDTextureFormat, eGDType, const void*, bool, const void*, std::uint32_t) override { Unsupported("StretchBltAlpha"); }
    void BitBltAlphaModulate(int, int, int, int, eGDTextureFormat, eGDType, const void*, bool, const void*, std::uint32_t) override { Unsupported("BitBltAlphaModulate"); }
    void StretchBltAlphaModulate(int, int, int, int, int, int, eGDTextureFormat, eGDType, const void*, bool, const void*, std::uint32_t) override { Unsupported("StretchBltAlphaModulate"); }
    void SetViewport() override { Unsupported("SetViewport"); }
    void SetViewport(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { Unsupported("SetViewport"); }
    void GetViewport(std::uint32_t[4]) override { Unsupported("GetViewport"); }
    const void* GetDriverInfo() override { Unsupported("GetDriverInfo"); return nullptr; }
    std::uint32_t GetGZCLSID() const override { return 0; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool IsDeviceReady() override { return true; }
    bool Punt(std::uint32_t, void*) override { Unsupported("Punt"); return false; }

protected:
    void Unsupported(const char* name) const { ++unsupported_[name]; }
    StateMirror mirror_;

private:
    void SetState(eGDDriverState s, bool on) {
        switch (s) {
        case state::AlphaTest: mirror_.alphaTest = on; break;
        case state::DepthTest: mirror_.depthTest = on; break;
        case state::Stencil: mirror_.stencil = on; break;
        case state::CullFace: mirror_.cull = on; break;
        case state::Blend: mirror_.blend = on; break;
        case state::Texturing: mirror_.stage[mirror_.currentStage & 1].enabled = on; break;
        case state::Fog: mirror_.fog = on; break;
        default: break;
        }
    }
    std::int32_t refCount_ = 1;
    mutable std::map<std::string, int> unsupported_;
};

}  // namespace gd
