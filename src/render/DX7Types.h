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
class IDirectDrawClipper; class IDirectDrawPalette; class IUnknown;
struct tagPALETTEENTRY; struct _DDSURFACEDESC2; struct _DDCAPS_DX7; struct _DDSCAPS2; struct tagDDDEVICEIDENTIFIER2;
struct tagSIZE; struct HDC__; struct HWND__; struct _D3DVERTEXBUFFERDESC;

using EnumDevicesCallback = int (*)(char*, char*, _D3DDeviceDesc7*, void*);
using EnumDisplayModesCallback = int (*)(_DDSURFACEDESC2*, void*);
using EnumSurfacesCallback = int (*)(IDirectDrawSurface7*, _DDSURFACEDESC2*, void*);
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

// IDirect3D7 (slots 3..7).
class IDirect3D7 {
public:
    virtual std::uint32_t QueryInterface(const _GUID& iid, void** out) = 0;
    virtual void AddRef() = 0;
    virtual std::int32_t Release() = 0;
    virtual std::uint32_t EnumDevices(EnumDevicesCallback callback, void* context) = 0;
    virtual std::uint32_t CreateDevice(const _GUID& iid, IDirectDrawSurface7* target, IDirect3DDevice7** out) = 0;
    virtual std::uint32_t CreateVertexBuffer(_D3DVERTEXBUFFERDESC* desc, IDirect3DVertexBuffer7** out, std::uint32_t flags) = 0;
    virtual std::uint32_t EnumZBufferFormats(const _GUID& iid, EnumTextureFormatsCallback callback, void* context) = 0;
    virtual std::uint32_t EvictManagedTextures() = 0;
    virtual ~IDirect3D7() = default;
};

// IDirectDraw7 (slots 3..29).
class IDirectDraw7 {
public:
    virtual std::uint32_t QueryInterface(const _GUID& iid, void** out) = 0;
    virtual void AddRef() = 0;
    virtual std::int32_t Release() = 0;
    virtual std::uint32_t Compact() = 0;
    virtual std::uint32_t CreateClipper(std::uint32_t flags, IDirectDrawClipper** out, IUnknown* outer) = 0;
    virtual std::uint32_t CreatePalette(std::uint32_t flags, tagPALETTEENTRY* entries, IDirectDrawPalette** out, IUnknown* outer) = 0;
    virtual std::uint32_t CreateSurface(_DDSURFACEDESC2* desc, IDirectDrawSurface7** out, IUnknown* outer) = 0;
    virtual std::uint32_t DuplicateSurface(IDirectDrawSurface7* source, IDirectDrawSurface7** out) = 0;
    virtual std::uint32_t EnumDisplayModes(std::uint32_t flags, _DDSURFACEDESC2* desc, void* context, EnumDisplayModesCallback callback) = 0;
    virtual std::uint32_t EnumSurfaces(std::uint32_t flags, _DDSURFACEDESC2* desc, void* context, EnumSurfacesCallback callback) = 0;
    virtual std::uint32_t FlipToGDISurface() = 0;
    virtual std::uint32_t GetCaps(_DDCAPS_DX7* driverCaps, _DDCAPS_DX7* helCaps) = 0;
    virtual std::uint32_t GetDisplayMode(_DDSURFACEDESC2* desc) = 0;
    virtual std::uint32_t GetFourCCCodes(std::uint32_t* count, std::uint32_t* codes) = 0;
    virtual std::uint32_t GetGDISurface(IDirectDrawSurface7** out) = 0;
    virtual std::uint32_t GetMonitorFrequency(std::uint32_t* frequency) = 0;
    virtual std::uint32_t GetScanLine(std::uint32_t* line) = 0;
    virtual std::uint32_t GetVerticalBlankStatus(int* inBlank) = 0;
    virtual std::uint32_t Initialize(_GUID* guid) = 0;
    virtual std::uint32_t RestoreDisplayMode() = 0;
    virtual std::uint32_t SetCooperativeLevel(HWND__* window, std::uint32_t flags) = 0;
    virtual std::uint32_t SetDisplayMode(std::uint32_t w, std::uint32_t h, std::uint32_t bpp, std::uint32_t refresh, std::uint32_t flags) = 0;
    virtual std::uint32_t WaitForVerticalBlank(std::uint32_t flags, void* event) = 0;
    virtual std::uint32_t GetAvailableVidMem(_DDSCAPS2* caps, std::uint32_t* total, std::uint32_t* free) = 0;
    virtual std::uint32_t GetSurfaceFromDC(HDC__* dc, IDirectDrawSurface7** out) = 0;
    virtual std::uint32_t RestoreAllSurfaces() = 0;
    virtual std::uint32_t TestCooperativeLevel() = 0;
    virtual std::uint32_t GetDeviceIdentifier(tagDDDEVICEIDENTIFIER2* id, std::uint32_t flags) = 0;
    virtual std::uint32_t StartModeTest(tagSIZE* sizes, std::uint32_t count, std::uint32_t flags) = 0;
    virtual std::uint32_t EvaluateMode(std::uint32_t flags, std::uint32_t* timeout) = 0;
    virtual ~IDirectDraw7() = default;
};

struct _DDBLTFX; struct _DDBLTBATCH; struct _DDCOLORKEY; struct _DDOVERLAYFX; class IDirectDraw;

// IDirectDrawSurface7 (slots 3..50 after QueryInterface / AddRef / Release; the vtable ends with the two destructors).
class IDirectDrawSurface7 {
public:
    virtual std::uint32_t QueryInterface(const _GUID& iid, void** out) = 0;
    virtual void AddRef() = 0;
    virtual std::int32_t Release() = 0;
    virtual std::uint32_t AddAttachedSurface(IDirectDrawSurface7*) = 0;
    virtual std::uint32_t AddOverlayDirtyRect(tagRECT*) = 0;
    virtual std::uint32_t Blt(tagRECT* dest, IDirectDrawSurface7* src, tagRECT* srcRect, std::uint32_t flags, _DDBLTFX* fx) = 0;
    virtual std::uint32_t BltBatch(_DDBLTBATCH*, std::uint32_t, std::uint32_t) = 0;
    virtual std::uint32_t BltFast(std::uint32_t, std::uint32_t, IDirectDrawSurface7*, tagRECT*, std::uint32_t) = 0;
    virtual std::uint32_t DeleteAttachedSurface(std::uint32_t, IDirectDrawSurface7*) = 0;
    virtual std::uint32_t EnumAttachedSurfaces(void*, EnumSurfacesCallback) = 0;
    virtual std::uint32_t EnumOverlayZOrders(std::uint32_t, void*, EnumSurfacesCallback) = 0;
    virtual std::uint32_t Flip(IDirectDrawSurface7*, std::uint32_t) = 0;
    virtual std::uint32_t GetAttachedSurface(_DDSCAPS2*, IDirectDrawSurface7**) = 0;
    virtual std::uint32_t GetBltStatus(std::uint32_t) = 0;
    virtual std::uint32_t GetCaps(_DDSCAPS2*) = 0;
    virtual std::uint32_t GetClipper(IDirectDrawClipper**) = 0;
    virtual std::uint32_t GetColorKey(std::uint32_t, _DDCOLORKEY*) = 0;
    virtual std::uint32_t GetDC(HDC__**) = 0;
    virtual std::uint32_t GetFlipStatus(std::uint32_t) = 0;
    virtual std::uint32_t GetOverlayPosition(std::int32_t*, std::int32_t*) = 0;
    virtual std::uint32_t GetPalette(IDirectDrawPalette**) = 0;
    virtual std::uint32_t GetPixelFormat(_DDPIXELFORMAT*) = 0;
    virtual std::uint32_t GetSurfaceDesc(_DDSURFACEDESC2*) = 0;
    virtual std::uint32_t Initialize(IDirectDraw*, _DDSURFACEDESC2*) = 0;
    virtual std::uint32_t IsLost() = 0;
    virtual std::uint32_t Lock(tagRECT* rect, _DDSURFACEDESC2* desc, std::uint32_t flags, void* event) = 0;
    virtual std::uint32_t ReleaseDC(HDC__*) = 0;
    virtual std::uint32_t Restore() = 0;
    virtual std::uint32_t SetClipper(IDirectDrawClipper*) = 0;
    virtual std::uint32_t SetColorKey(std::uint32_t, _DDCOLORKEY*) = 0;
    virtual std::uint32_t SetOverlayPosition(std::int32_t, std::int32_t) = 0;
    virtual std::uint32_t SetPalette(IDirectDrawPalette*) = 0;
    virtual std::uint32_t Unlock(tagRECT*) = 0;
    virtual std::uint32_t UpdateOverlay(tagRECT*, IDirectDrawSurface7*, tagRECT*, std::uint32_t, _DDOVERLAYFX*) = 0;
    virtual std::uint32_t UpdateOverlayDisplay(std::uint32_t) = 0;
    virtual std::uint32_t UpdateOverlayZOrder(std::uint32_t, IDirectDrawSurface7*) = 0;
    virtual std::uint32_t GetDDInterface(void**) = 0;
    virtual std::uint32_t PageLock(std::uint32_t) = 0;
    virtual std::uint32_t PageUnlock(std::uint32_t) = 0;
    virtual std::uint32_t SetSurfaceDesc(_DDSURFACEDESC2*, std::uint32_t) = 0;
    // The shim abuses these two as an in-process key/value slot: the "GUID" is ignored (see CDirectDrawSurface7).
    virtual std::uint32_t SetPrivateData(const _GUID& key, void* data, std::uint32_t size, std::uint32_t flags) = 0;
    virtual std::uint32_t GetPrivateData(const _GUID& key, void* data, std::uint32_t* size) = 0;
    virtual std::uint32_t FreePrivateData(const _GUID& key) = 0;
    virtual std::uint32_t GetUniquenessValue(std::uint32_t*) = 0;
    virtual std::uint32_t ChangeUniquenessValue() = 0;
    virtual std::uint32_t SetPriority(std::uint32_t) = 0;
    virtual std::uint32_t GetPriority(std::uint32_t*) = 0;
    virtual std::uint32_t SetLOD(std::uint32_t) = 0;
    virtual std::uint32_t GetLOD(std::uint32_t*) = 0;
    virtual ~IDirectDrawSurface7() = default;
};

// IDirect3DVertexBuffer7 (slots 3..8).
class IDirect3DVertexBuffer7 {
public:
    virtual std::uint32_t QueryInterface(const _GUID& iid, void** out) = 0;
    virtual void AddRef() = 0;
    virtual std::int32_t Release() = 0;
    virtual std::uint32_t Lock(std::uint32_t flags, void** data, std::uint32_t* size) = 0;
    virtual std::uint32_t Unlock() = 0;
    virtual std::uint32_t ProcessVertices(std::uint32_t, std::uint32_t, std::uint32_t, IDirect3DVertexBuffer7*, std::uint32_t, IDirect3DDevice7*, std::uint32_t) = 0;
    virtual std::uint32_t GetVertexBufferDesc(_D3DVERTEXBUFFERDESC*) = 0;
    virtual std::uint32_t Optimize(IDirect3DDevice7*, std::uint32_t) = 0;
    virtual std::uint32_t ProcessVerticesStrided(std::uint32_t, std::uint32_t, std::uint32_t, _D3DDRAWPRIMITIVESTRIDEDDATA*, std::uint32_t, IDirect3DDevice7*, std::uint32_t) = 0;
    virtual ~IDirect3DVertexBuffer7() = default;
};

}  // namespace sgl
