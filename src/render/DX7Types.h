// Minimal DirectX 7 vocabulary (Direct3D / DirectDraw) used by the graphics-driver shim. The structs are
// opaque here; only pointers to them are passed around.
#pragma once
#include <cstdint>

namespace sgl {

constexpr std::uint32_t kDDOk = 0, kDDErrUnsupported = 0x80000001, kDDErrNoInterface = 0x80000004;

// 24 bytes: Data1 is an `unsigned long` (8 bytes on the x86_64 build).
struct _GUID { std::uint64_t data1; std::uint16_t data2, data3; std::uint8_t data4[8]; };
struct tagRECT; struct tagPOINT;
struct _D3DDeviceDesc7; struct _D3DLIGHT7; struct _D3DVECTOR; struct _D3DMATRIX; struct _D3DRECT;
struct _D3DDRAWPRIMITIVESTRIDEDDATA; struct _D3DCLIPSTATUS; struct _D3DMATERIAL7; struct _D3DVIEWPORT7;
struct _DDPIXELFORMAT;
enum _D3DSTATEBLOCKTYPE : std::uint32_t {};
enum _D3DPRIMITIVETYPE : std::uint32_t {};
enum _D3DTRANSFORMSTATETYPE : std::uint32_t {};
enum _D3DTEXTURESTAGESTATETYPE : std::uint32_t {};
enum _D3DRENDERSTATETYPE : std::uint32_t {};
class IDirect3D7; class IDirect3DVertexBuffer7; class IDirectDrawSurface7;

using EnumTextureFormatsCallback = int (*)(_DDPIXELFORMAT*, void*);

// IDirect3DDevice7 (slots 3..48 after QueryInterface / AddRef / Release).
class IDirect3DDevice7 {
public:
    virtual std::uint32_t QueryInterface(const _GUID& iid, void** out) = 0;
    virtual void AddRef() = 0;
    virtual std::int32_t Release() = 0;
    virtual std::uint32_t GetCaps(_D3DDeviceDesc7* a0) = 0;
    virtual std::uint32_t EnumTextureFormats(EnumTextureFormatsCallback callback, void* context) = 0;
    virtual std::uint32_t BeginScene() = 0;
    virtual std::uint32_t EndScene() = 0;
    virtual std::uint32_t GetDirect3D(IDirect3D7** a0) = 0;
    virtual std::uint32_t SetRenderTarget(IDirectDrawSurface7* a0, std::uint32_t a1) = 0;
    virtual std::uint32_t GetRenderTarget(IDirectDrawSurface7** a0) = 0;
    virtual std::uint32_t Clear(std::uint32_t a0, _D3DRECT* a1, std::uint32_t a2, std::uint32_t a3, float a4, std::uint32_t a5) = 0;
    virtual std::uint32_t SetTransform(_D3DTRANSFORMSTATETYPE a0, _D3DMATRIX* a1) = 0;
    virtual std::uint32_t GetTransform(_D3DTRANSFORMSTATETYPE a0, _D3DMATRIX* a1) = 0;
    virtual std::uint32_t SetViewport(_D3DVIEWPORT7* a0) = 0;
    virtual std::uint32_t MultiplyTransform(_D3DTRANSFORMSTATETYPE a0, _D3DMATRIX* a1) = 0;
    virtual std::uint32_t GetViewport(_D3DVIEWPORT7* a0) = 0;
    virtual std::uint32_t SetMaterial(_D3DMATERIAL7* a0) = 0;
    virtual std::uint32_t GetMaterial(_D3DMATERIAL7* a0) = 0;
    virtual std::uint32_t SetLight(std::uint32_t a0, _D3DLIGHT7* a1) = 0;
    virtual std::uint32_t GetLight(std::uint32_t a0, _D3DLIGHT7* a1) = 0;
    virtual std::uint32_t SetRenderState(_D3DRENDERSTATETYPE a0, std::uint32_t a1) = 0;
    virtual std::uint32_t GetRenderState(_D3DRENDERSTATETYPE a0, std::uint32_t* a1) = 0;
    virtual std::uint32_t BeginStateBlock() = 0;
    virtual std::uint32_t EndStateBlock(std::uint32_t* a0) = 0;
    virtual std::uint32_t PreLoad(IDirectDrawSurface7* a0) = 0;
    virtual std::uint32_t DrawPrimitive(_D3DPRIMITIVETYPE a0, std::uint32_t a1, void* a2, std::uint32_t a3, std::uint32_t a4) = 0;
    virtual std::uint32_t DrawIndexedPrimitive(_D3DPRIMITIVETYPE a0, std::uint32_t a1, void* a2, std::uint32_t a3, std::uint16_t* a4, std::uint32_t a5, std::uint32_t a6) = 0;
    virtual std::uint32_t SetClipStatus(_D3DCLIPSTATUS* a0) = 0;
    virtual std::uint32_t GetClipStatus(_D3DCLIPSTATUS* a0) = 0;
    virtual std::uint32_t DrawPrimitiveStrided(_D3DPRIMITIVETYPE a0, std::uint32_t a1, _D3DDRAWPRIMITIVESTRIDEDDATA* a2, std::uint32_t a3, std::uint32_t a4) = 0;
    virtual std::uint32_t DrawIndexedPrimitiveStrided(_D3DPRIMITIVETYPE a0, std::uint32_t a1, _D3DDRAWPRIMITIVESTRIDEDDATA* a2, std::uint32_t a3, std::uint16_t* a4, std::uint32_t a5, std::uint32_t a6) = 0;
    virtual std::uint32_t DrawPrimitiveVB(_D3DPRIMITIVETYPE a0, IDirect3DVertexBuffer7* a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t a4) = 0;
    virtual std::uint32_t DrawIndexedPrimitiveVB(_D3DPRIMITIVETYPE a0, IDirect3DVertexBuffer7* a1, std::uint32_t a2, std::uint32_t a3, std::uint16_t* a4, std::uint32_t a5, std::uint32_t a6) = 0;
    virtual std::uint32_t ComputeSphereVisibility(_D3DVECTOR* a0, float* a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t* a4) = 0;
    virtual std::uint32_t GetTexture(std::uint32_t a0, IDirectDrawSurface7** a1) = 0;
    virtual std::uint32_t SetTexture(std::uint32_t a0, IDirectDrawSurface7* a1) = 0;
    virtual std::uint32_t GetTextureStageState(std::uint32_t a0, _D3DTEXTURESTAGESTATETYPE a1, std::uint32_t* a2) = 0;
    virtual std::uint32_t SetTextureStageState(std::uint32_t a0, _D3DTEXTURESTAGESTATETYPE a1, std::uint32_t a2) = 0;
    virtual std::uint32_t ValidateDevice(std::uint32_t* a0) = 0;
    virtual std::uint32_t ApplyStateBlock(std::uint32_t a0) = 0;
    virtual std::uint32_t CaptureStateBlock(std::uint32_t a0) = 0;
    virtual std::uint32_t DeleteStateBlock(std::uint32_t a0) = 0;
    virtual std::uint32_t CreateStateBlock(_D3DSTATEBLOCKTYPE a0, std::uint32_t* a1) = 0;
    virtual std::uint32_t Load(IDirectDrawSurface7* a0, tagPOINT* a1, IDirectDrawSurface7* a2, tagRECT* a3, std::uint32_t a4) = 0;
    virtual std::uint32_t LightEnable(std::uint32_t a0, std::int32_t a1) = 0;
    virtual std::uint32_t GetLightEnable(std::uint32_t a0, std::int32_t* a1) = 0;
    virtual std::uint32_t SetClipPlane(std::uint32_t a0, float* a1) = 0;
    virtual std::uint32_t GetClipPlane(std::uint32_t a0, float* a1) = 0;
    virtual std::uint32_t GetInfo(std::uint32_t a0, void* a1, std::uint32_t a2) = 0;
    virtual ~IDirect3DDevice7() = default;
};

}  // namespace sgl
