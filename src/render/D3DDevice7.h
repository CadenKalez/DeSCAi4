// CDirect3DDevice7 (0x100637b82..0x10063ad36): the Direct3D 7 device, implemented as a fixed-function state machine over
// OpenGL (IGLApi). Render and texture-stage states are cached; GL is only told what changed, and most of that is
// deferred to UpdateRenderingState, which every draw call runs first. Only vertex-buffer draws are implemented (the
// game never uses the other forms: they succeed and draw nothing).
// Layout (0xc30 bytes): +8 refcount, +0x10 colour target, +0x18 depth target, +0x20 device description, +0x114 current
// GL matrix mode, +0x118 viewport, +0x130 diffuse / +0x140 ambient material colour, +0x174 view matrix (dirty flag
// first), +0x1b8 projection matrix, +0x1fc active texture unit, +0x200 client-active unit, +0x208 two texture stages
// (0x218 each), +0x638 vertex array, +0x658 colour array, +0x678 texture formats, +0x690 GL state mirror,
// +0x828 blend / +0x82c alpha-test dirty flags, +0x830 render states[256].
#pragma once
#include <cstdint>
#include <vector>

#include "render/D3DVertexBuffer7.h"
#include "render/DX7Structs.h"
#include "render/DX7Types.h"
#include "render/GLState.h"

namespace sgl {

class CDirect3DDevice7 final : public IDirect3DDevice7 {
public:
    CDirect3DDevice7(IDirectDrawSurface7* target, IDirectDrawSurface7* zbuffer, const _D3DDeviceDesc7* desc,
                     const std::vector<_DDPIXELFORMAT>* formats);   // @ 0x100637b82
    ~CDirect3DDevice7() override;                                     // @ 0x100638380

    std::uint32_t QueryInterface(const _GUID&, void** out) override;   // @ 0x10063841e
    void AddRef() override { ++refCount_; }                            // @ 0x100638430
    std::int32_t Release() override;                                   // @ 0x10063843e
    std::uint32_t GetCaps(_D3DDeviceDesc7* out) override;              // @ 0x1006396ca
    std::uint32_t EnumTextureFormats(EnumTextureFormatsCallback callback, void* context) override;   // @ 0x100639656
    std::uint32_t BeginScene() override { return 0; }                  // @ 0x100638470
    std::uint32_t EndScene() override { return 0; }                    // @ 0x100639646
    std::uint32_t GetDirect3D(IDirect3D7**) override { return 0; }     // @ 0x1006396f6
    std::uint32_t SetRenderTarget(IDirectDrawSurface7*, std::uint32_t) override { return 0; }   // @ 0x100639cdc
    std::uint32_t GetRenderTarget(IDirectDrawSurface7** out) override;   // @ 0x100639752
    std::uint32_t Clear(std::uint32_t count, _D3DRECT* rects, std::uint32_t flags, std::uint32_t color, float z, std::uint32_t stencil) override;   // @ 0x100638490
    std::uint32_t SetTransform(_D3DTRANSFORMSTATETYPE type, _D3DMATRIX* matrix) override;   // @ 0x10063ab20
    std::uint32_t GetTransform(_D3DTRANSFORMSTATETYPE, _D3DMATRIX*) override { return 0; }   // @ 0x10063977c
    std::uint32_t SetViewport(_D3DVIEWPORT7* viewport) override;       // @ 0x10063ad0e
    std::uint32_t MultiplyTransform(_D3DTRANSFORMSTATETYPE, _D3DMATRIX*) override { return 0; }   // @ 0x1006397bc
    std::uint32_t GetViewport(_D3DVIEWPORT7* out) override;            // @ 0x100639784
    std::uint32_t SetMaterial(_D3DMATERIAL7* material) override;       // @ 0x1006397e4
    std::uint32_t GetMaterial(_D3DMATERIAL7*) override { return 0; }   // @ 0x100639716
    std::uint32_t SetLight(std::uint32_t, _D3DLIGHT7*) override { return 0; }   // @ 0x1006397dc
    std::uint32_t GetLight(std::uint32_t, _D3DLIGHT7*) override { return 0; }   // @ 0x100639706
    std::uint32_t SetRenderState(_D3DRENDERSTATETYPE type, std::uint32_t value) override;   // @ 0x1006398b0
    std::uint32_t GetRenderState(_D3DRENDERSTATETYPE type, std::uint32_t* out) override;     // @ 0x10063971e
    std::uint32_t BeginStateBlock() override { return 0; }             // @ 0x100638478
    std::uint32_t EndStateBlock(std::uint32_t*) override { return 0; } // @ 0x10063964e
    std::uint32_t PreLoad(IDirectDrawSurface7*) override { return 0; } // @ 0x1006397c4
    std::uint32_t DrawPrimitive(_D3DPRIMITIVETYPE, std::uint32_t, void*, std::uint32_t, std::uint32_t) override { return 0; }   // @ 0x100639514
    std::uint32_t DrawIndexedPrimitive(_D3DPRIMITIVETYPE, std::uint32_t, void*, std::uint32_t, std::uint16_t*, std::uint32_t, std::uint32_t) override { return 0; }   // @ 0x100638faa
    std::uint32_t SetClipStatus(_D3DCLIPSTATUS*) override { return 0; }   // @ 0x1006397d4
    std::uint32_t GetClipStatus(_D3DCLIPSTATUS*) override { return 0; }   // @ 0x1006396ee
    std::uint32_t DrawPrimitiveStrided(_D3DPRIMITIVETYPE, std::uint32_t, _D3DDRAWPRIMITIVESTRIDEDDATA*, std::uint32_t, std::uint32_t) override { return 0; }   // @ 0x10063951c
    std::uint32_t DrawIndexedPrimitiveStrided(_D3DPRIMITIVETYPE, std::uint32_t, _D3DDRAWPRIMITIVESTRIDEDDATA*, std::uint32_t, std::uint16_t*, std::uint32_t, std::uint32_t) override { return 0; }   // @ 0x100638fb2
    std::uint32_t DrawPrimitiveVB(_D3DPRIMITIVETYPE type, IDirect3DVertexBuffer7* vb, std::uint32_t start, std::uint32_t count, std::uint32_t flags) override;   // @ 0x100639524
    std::uint32_t DrawIndexedPrimitiveVB(_D3DPRIMITIVETYPE type, IDirect3DVertexBuffer7* vb, std::uint32_t start, std::uint32_t vertexCount, std::uint16_t* indices, std::uint32_t indexCount, std::uint32_t flags) override;   // @ 0x100638fba
    std::uint32_t ComputeSphereVisibility(_D3DVECTOR*, float*, std::uint32_t, std::uint32_t, std::uint32_t*) override { return 0; }   // @ 0x10063859e
    std::uint32_t GetTexture(std::uint32_t, IDirectDrawSurface7**) override { return 0; }   // @ 0x10063976c
    std::uint32_t SetTexture(std::uint32_t unit, IDirectDrawSurface7* texture) override;    // @ 0x100639ce4
    std::uint32_t GetTextureStageState(std::uint32_t, _D3DTEXTURESTAGESTATETYPE, std::uint32_t*) override { return 0; }   // @ 0x100639774
    std::uint32_t SetTextureStageState(std::uint32_t unit, _D3DTEXTURESTAGESTATETYPE type, std::uint32_t value) override;   // @ 0x100639f10
    std::uint32_t ValidateDevice(std::uint32_t*) override { return 0; }   // @ 0x10063ad36
    std::uint32_t ApplyStateBlock(std::uint32_t) override { return 0; }   // @ 0x100638468
    std::uint32_t CaptureStateBlock(std::uint32_t) override { return 0; }   // @ 0x100638480
    std::uint32_t DeleteStateBlock(std::uint32_t) override { return 0; }   // @ 0x1006385a6
    std::uint32_t CreateStateBlock(_D3DSTATEBLOCKTYPE, std::uint32_t*) override { return 0; }   // @ 0x100638488
    std::uint32_t Load(IDirectDrawSurface7*, tagPOINT*, IDirectDrawSurface7*, tagRECT*, std::uint32_t) override { return 0; }   // @ 0x1006397b4
    std::uint32_t LightEnable(std::uint32_t, std::int32_t) override { return 0; }   // @ 0x1006397ac
    std::uint32_t GetLightEnable(std::uint32_t, std::int32_t*) override { return 0; }   // @ 0x10063970e
    std::uint32_t SetClipPlane(std::uint32_t, float*) override { return 0; }   // @ 0x1006397cc
    std::uint32_t GetClipPlane(std::uint32_t, float*) override { return 0; }   // @ 0x1006396e6
    std::uint32_t GetInfo(std::uint32_t, void*, std::uint32_t) override { return 0; }   // @ 0x1006396fe

    void UpdateRenderingState();                                  // @ 0x1006385b0
    void SetAmbientMaterialColor(const float* color);             // @ 0x10063980c
    void SetDiffuseMaterialColor(const float* color);             // @ 0x10063985e
    void UpdateVarPtrs(CDirect3DVertexBuffer7* vb, std::uint32_t stride, const std::uint8_t* base);      // @ 0x1006390ea
    void UpdateNonVarPtrs(CDirect3DVertexBuffer7* vb, std::uint32_t stride, const std::uint8_t* base);   // @ 0x1006392e2

    // ---- state (kept public so the layout can be checked) ----
    std::int32_t refCount_ = 0;                  // +8
    IDirectDrawSurface7* target_ = nullptr;      // +0x10
    IDirectDrawSurface7* zbuffer_ = nullptr;     // +0x18
    _D3DDeviceDesc7 desc_{};                     // +0x20
    std::uint32_t matrixMode_ = kUnset;          // +0x114
    _D3DVIEWPORT7 viewport_{};                   // +0x118
    float diffuse_[4];                           // +0x130
    float ambient_[4];                           // +0x140
    std::uint32_t reserved150_[9];               // +0x150
    std::uint32_t viewDirty_ = kUnset;           // +0x174
    float view_[16];                             // +0x178
    std::uint32_t projDirty_ = kUnset;           // +0x1b8
    float proj_[16];                             // +0x1bc
    std::uint32_t activeTexture_ = kUnset;       // +0x1fc
    std::uint32_t clientActiveTexture_ = kUnset; // +0x200
    std::uint32_t pad204_ = 0;
    CTextureStage stages_[2];                    // +0x208
    CVertexArray vertexArray_;                   // +0x638
    CColorArray colorArray_;                     // +0x658
    std::vector<_DDPIXELFORMAT> formats_;        // +0x678
    GLState gl_;                                 // +0x690
    std::uint32_t blendDirty_ = 1;               // +0x828
    std::uint32_t alphaDirty_ = 1;               // +0x82c
    std::uint32_t renderState_[256];             // +0x830
};

}  // namespace sgl
