// CDirectDraw7 (0x10063b4e0..0x10063b7aa) and CDirect3D7 (0x100637626..0x1006376fc): the DirectDraw / Direct3D
// entry points of the DX7 shim. Nearly everything succeeds without doing anything; what matters is that
// CreateSurface builds a CDirectDrawSurface7, QueryInterface(IID_IDirect3D7) builds a CDirect3D7 whose
// CreateVertexBuffer builds a CDirect3DVertexBuffer7, and GetDeviceIdentifier reports the Mac display.
// Layout: +0 vptr, +8 refcount.
#pragma once
#include <string>

#include "render/DX7Types.h"

namespace sgl {

// Services behind the shim: object construction (the surface / vertex-buffer classes) and the display query.
class IDX7Host {
public:
    virtual ~IDX7Host() = default;
    virtual IDirectDrawSurface7* NewSurface(_DDSURFACEDESC2* desc) = 0;                                   // CDirectDrawSurface7
    virtual IDirect3DVertexBuffer7* NewVertexBuffer(_D3DVERTEXBUFFERDESC* desc, std::uint32_t flags) = 0;   // CDirect3DVertexBuffer7
    // GetPCIDisplayInfo(MacDisplay::GetDisplayID()): false on failure; `deviceId`, `vendorId` and the board name.
    virtual bool PCIDisplayInfo(std::uint32_t& vendorId, std::uint32_t& deviceId, std::string& name) = 0;
    virtual void SetDeviceID(std::uint32_t deviceId, std::uint32_t vendorId) = 0;                          // MacOpenGL::SetDeviceID(.., true)
};
IDX7Host* DX7Host();
void SetDX7Host(IDX7Host* host);

class CDirect3D7 final : public IDirect3D7 {
public:
    std::uint32_t QueryInterface(const _GUID&, void** out) override { *out = this; AddRef(); return kDDOk; }
    void AddRef() override { ++refCount_; }
    std::int32_t Release() override;
    std::uint32_t EnumDevices(EnumDevicesCallback, void*) override { return kDDOk; }
    std::uint32_t CreateDevice(const _GUID&, IDirectDrawSurface7*, IDirect3DDevice7**) override { return kDDOk; }
    std::uint32_t CreateVertexBuffer(_D3DVERTEXBUFFERDESC* desc, IDirect3DVertexBuffer7** out, std::uint32_t flags) override;
    std::uint32_t EnumZBufferFormats(const _GUID&, EnumTextureFormatsCallback, void*) override { return kDDOk; }
    std::uint32_t EvictManagedTextures() override { return kDDOk; }

private:
    std::int32_t refCount_ = 1;     // starts at 1 (the creator's reference)
};

class CDirectDraw7 final : public IDirectDraw7 {
public:
    std::uint32_t QueryInterface(const _GUID& iid, void** out) override;
    void AddRef() override { ++refCount_; }
    std::int32_t Release() override;
    std::uint32_t Compact() override { return kDDOk; }
    std::uint32_t CreateClipper(std::uint32_t, IDirectDrawClipper**, IUnknown*) override { return kDDOk; }
    std::uint32_t CreatePalette(std::uint32_t, tagPALETTEENTRY*, IDirectDrawPalette**, IUnknown*) override { return kDDOk; }
    std::uint32_t CreateSurface(_DDSURFACEDESC2* desc, IDirectDrawSurface7** out, IUnknown*) override;
    std::uint32_t DuplicateSurface(IDirectDrawSurface7*, IDirectDrawSurface7**) override { return kDDOk; }
    std::uint32_t EnumDisplayModes(std::uint32_t, _DDSURFACEDESC2*, void*, EnumDisplayModesCallback) override { return kDDOk; }
    std::uint32_t EnumSurfaces(std::uint32_t, _DDSURFACEDESC2*, void*, EnumSurfacesCallback) override { return kDDOk; }
    std::uint32_t FlipToGDISurface() override { return kDDOk; }
    std::uint32_t GetCaps(_DDCAPS_DX7* driverCaps, _DDCAPS_DX7* helCaps) override;
    std::uint32_t GetDisplayMode(_DDSURFACEDESC2*) override { return kDDOk; }
    std::uint32_t GetFourCCCodes(std::uint32_t*, std::uint32_t*) override { return kDDOk; }
    std::uint32_t GetGDISurface(IDirectDrawSurface7**) override { return kDDOk; }
    std::uint32_t GetMonitorFrequency(std::uint32_t*) override { return kDDOk; }
    std::uint32_t GetScanLine(std::uint32_t*) override { return kDDOk; }
    std::uint32_t GetVerticalBlankStatus(int*) override { return kDDOk; }
    std::uint32_t Initialize(_GUID*) override { return kDDOk; }
    std::uint32_t RestoreDisplayMode() override { return kDDOk; }
    std::uint32_t SetCooperativeLevel(HWND__*, std::uint32_t) override { return kDDOk; }
    std::uint32_t SetDisplayMode(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return kDDOk; }
    std::uint32_t WaitForVerticalBlank(std::uint32_t, void*) override { return kDDOk; }
    std::uint32_t GetAvailableVidMem(_DDSCAPS2*, std::uint32_t*, std::uint32_t*) override { return kDDOk; }
    std::uint32_t GetSurfaceFromDC(HDC__*, IDirectDrawSurface7**) override { return kDDOk; }
    std::uint32_t RestoreAllSurfaces() override { return kDDOk; }
    std::uint32_t TestCooperativeLevel() override { return kDDOk; }
    std::uint32_t GetDeviceIdentifier(tagDDDEVICEIDENTIFIER2* id, std::uint32_t flags) override;
    std::uint32_t StartModeTest(tagSIZE*, std::uint32_t, std::uint32_t) override { return kDDOk; }
    std::uint32_t EvaluateMode(std::uint32_t, std::uint32_t*) override { return kDDOk; }

private:
    std::int32_t refCount_ = 1;     // +8
};

}  // namespace sgl
