// cSGLDX7Direct3DDevice7Stub (0x100611a64..0x100611c8c): a do-nothing Direct3D device. Every call succeeds
// (0) except the getters that have nothing to return (0x80000001). It answers QueryInterface only for
// IDirect3DDevice7 and IUnknown. Layout: +0 vptr, +8 refcount.
#pragma once
#include "render/DX7Types.h"

namespace sgl {

class cSGLDX7Direct3DDevice7Stub final : public IDirect3DDevice7 {
public:
    std::uint32_t QueryInterface(const _GUID& iid, void** out) override;
    void AddRef() override { ++refCount_; }
    std::int32_t Release() override;
    std::uint32_t GetCaps(_D3DDeviceDesc7*) override { return 0; }
    std::uint32_t EnumTextureFormats(EnumTextureFormatsCallback, void*) override { return 0; }
    std::uint32_t BeginScene() override { return 0; }
    std::uint32_t EndScene() override { return 0; }
    std::uint32_t GetDirect3D(IDirect3D7**) override { return 0; }
    std::uint32_t SetRenderTarget(IDirectDrawSurface7*, std::uint32_t) override { return 0; }
    std::uint32_t GetRenderTarget(IDirectDrawSurface7**) override { return 0; }
    std::uint32_t Clear(std::uint32_t, _D3DRECT*, std::uint32_t, std::uint32_t, float, std::uint32_t) override { return 0; }
    std::uint32_t SetTransform(_D3DTRANSFORMSTATETYPE, _D3DMATRIX*) override { return 0; }
    std::uint32_t GetTransform(_D3DTRANSFORMSTATETYPE, _D3DMATRIX*) override { return 0; }
    std::uint32_t SetViewport(_D3DVIEWPORT7*) override { return 0; }
    std::uint32_t MultiplyTransform(_D3DTRANSFORMSTATETYPE, _D3DMATRIX*) override { return 0; }
    std::uint32_t GetViewport(_D3DVIEWPORT7*) override { return 0; }
    std::uint32_t SetMaterial(_D3DMATERIAL7*) override { return 0; }
    std::uint32_t GetMaterial(_D3DMATERIAL7*) override { return 0x80000001; }
    std::uint32_t SetLight(std::uint32_t, _D3DLIGHT7*) override { return 0; }
    std::uint32_t GetLight(std::uint32_t, _D3DLIGHT7*) override { return 0x80000001; }
    std::uint32_t SetRenderState(_D3DRENDERSTATETYPE, std::uint32_t) override { return 0; }
    std::uint32_t GetRenderState(_D3DRENDERSTATETYPE, std::uint32_t*) override { return 0x80000001; }
    std::uint32_t BeginStateBlock() override { return 0; }
    std::uint32_t EndStateBlock(std::uint32_t*) override { return 0; }
    std::uint32_t PreLoad(IDirectDrawSurface7*) override { return 0; }
    std::uint32_t DrawPrimitive(_D3DPRIMITIVETYPE, std::uint32_t, void*, std::uint32_t, std::uint32_t) override { return 0; }
    std::uint32_t DrawIndexedPrimitive(_D3DPRIMITIVETYPE, std::uint32_t, void*, std::uint32_t, std::uint16_t*, std::uint32_t, std::uint32_t) override { return 0; }
    std::uint32_t SetClipStatus(_D3DCLIPSTATUS*) override { return 0; }
    std::uint32_t GetClipStatus(_D3DCLIPSTATUS*) override { return 0x80000001; }
    std::uint32_t DrawPrimitiveStrided(_D3DPRIMITIVETYPE, std::uint32_t, _D3DDRAWPRIMITIVESTRIDEDDATA*, std::uint32_t, std::uint32_t) override { return 0; }
    std::uint32_t DrawIndexedPrimitiveStrided(_D3DPRIMITIVETYPE, std::uint32_t, _D3DDRAWPRIMITIVESTRIDEDDATA*, std::uint32_t, std::uint16_t*, std::uint32_t, std::uint32_t) override { return 0; }
    std::uint32_t DrawPrimitiveVB(_D3DPRIMITIVETYPE, IDirect3DVertexBuffer7*, std::uint32_t, std::uint32_t, std::uint32_t) override { return 0; }
    std::uint32_t DrawIndexedPrimitiveVB(_D3DPRIMITIVETYPE, IDirect3DVertexBuffer7*, std::uint32_t, std::uint32_t, std::uint16_t*, std::uint32_t, std::uint32_t) override { return 0; }
    std::uint32_t ComputeSphereVisibility(_D3DVECTOR*, float*, std::uint32_t, std::uint32_t, std::uint32_t*) override { return 0; }
    std::uint32_t GetTexture(std::uint32_t, IDirectDrawSurface7**) override { return 0x80000001; }
    std::uint32_t SetTexture(std::uint32_t, IDirectDrawSurface7*) override { return 0; }
    std::uint32_t GetTextureStageState(std::uint32_t, _D3DTEXTURESTAGESTATETYPE, std::uint32_t*) override { return 0x80000001; }
    std::uint32_t SetTextureStageState(std::uint32_t, _D3DTEXTURESTAGESTATETYPE, std::uint32_t) override { return 0; }
    std::uint32_t ValidateDevice(std::uint32_t*) override { return 0; }
    std::uint32_t ApplyStateBlock(std::uint32_t) override { return 0; }
    std::uint32_t CaptureStateBlock(std::uint32_t) override { return 0x80000001; }
    std::uint32_t DeleteStateBlock(std::uint32_t) override { return 0x80000001; }
    std::uint32_t CreateStateBlock(_D3DSTATEBLOCKTYPE, std::uint32_t*) override { return 0x80000001; }
    std::uint32_t Load(IDirectDrawSurface7*, tagPOINT*, IDirectDrawSurface7*, tagRECT*, std::uint32_t) override { return 0; }
    std::uint32_t LightEnable(std::uint32_t, std::int32_t) override { return 0; }
    std::uint32_t GetLightEnable(std::uint32_t, std::int32_t*) override { return 0; }
    std::uint32_t SetClipPlane(std::uint32_t, float*) override { return 0; }
    std::uint32_t GetClipPlane(std::uint32_t, float*) override { return 0x80000001; }
    std::uint32_t GetInfo(std::uint32_t, void*, std::uint32_t) override { return 0x80000001; }

private:
    std::int32_t refCount_ = 0;
};

}  // namespace sgl
