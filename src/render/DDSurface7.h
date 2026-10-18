// CDirectDrawSurface7 (0x10063b7b4..0x10063cd62): a DirectDraw surface for the OpenGL back end. Every surface keeps its
// description, a block of system-memory pixels and a private-data slot; what the surface *is* is decided at creation from
// its caps ("kind"):
//   1 primary (3D device + primary)   2 depth (3D device + z-buffer)   3 back buffer (3D device + back buffer)
//   4 texture   5 front buffer   6 plain z-buffer   0 anything else (plain system-memory surface)
// Kinds 3 and 5 / 2 and 6 move pixels between their memory and the GL framebuffer in Lock and Blt; kind 4 keeps the GL
// texture bookkeeping (OpenGLTextureState) in the private-data slot. Layout: +8 refcount, +0xc kind, +0x10 locked mip
// level, +0x18 the description, +0xa0 private data, +0xa8 pixel block.
#pragma once
#include <cstdint>

#include "render/DX7Structs.h"
#include "render/DX7Types.h"

namespace sgl {

// A byte block aligned to 32 (or 4096 for vertex buffers): {size, aligned pointer, raw allocation}.
struct AlignedBlock {
    std::uint64_t size = 0;
    std::uint8_t* aligned = nullptr;
    std::uint8_t* raw = nullptr;
};

class CDirectDrawSurface7 final : public IDirectDrawSurface7 {
public:
    explicit CDirectDrawSurface7(const _DDSURFACEDESC2& desc);   // @ 0x10063b98e
    ~CDirectDrawSurface7() override;                              // @ 0x10063bb36

    void AllocatePrivateData(std::uint32_t size);                 // @ 0x10063b7b4
    void FreePrivateData();                                        // @ 0x10063b828
    std::uint32_t GetLevelSizeInBytes(std::uint32_t width, std::uint32_t height, const _DDPIXELFORMAT& pf) const;   // @ 0x10063b866
    std::uint32_t GetImageSizeInBytes() const;                    // @ 0x10063b8fa

    std::uint32_t QueryInterface(const _GUID&, void** out) override;                  // @ 0x10063bc90
    void AddRef() override { ++refCount; }                                              // @ 0x10063bca2
    std::int32_t Release() override;                                                    // @ 0x10063bcb0
    std::uint32_t AddAttachedSurface(IDirectDrawSurface7*) override { return 0; }      // @ 0x10063bcda
    std::uint32_t AddOverlayDirtyRect(tagRECT*) override { return 0; }                  // @ 0x10063bce2
    std::uint32_t Blt(tagRECT* dest, IDirectDrawSurface7* src, tagRECT* srcRect, std::uint32_t flags, _DDBLTFX* fx) override;   // @ 0x10063bcea
    std::uint32_t BltBatch(_DDBLTBATCH*, std::uint32_t, std::uint32_t) override { return 0; }        // @ 0x10063c46c
    std::uint32_t BltFast(std::uint32_t, std::uint32_t, IDirectDrawSurface7*, tagRECT*, std::uint32_t) override { return 0; }   // @ 0x10063c474
    std::uint32_t DeleteAttachedSurface(std::uint32_t, IDirectDrawSurface7*) override { return 0; }   // @ 0x10063c484
    std::uint32_t EnumAttachedSurfaces(void*, EnumSurfacesCallback) override { return 0; }            // @ 0x10063c48c
    std::uint32_t EnumOverlayZOrders(std::uint32_t, void*, EnumSurfacesCallback) override { return 0; }   // @ 0x10063c494
    std::uint32_t Flip(IDirectDrawSurface7*, std::uint32_t) override;                   // @ 0x10063c49c
    std::uint32_t GetAttachedSurface(_DDSCAPS2*, IDirectDrawSurface7** out) override;   // @ 0x10063c4b8
    std::uint32_t GetBltStatus(std::uint32_t) override { return 0; }                    // @ 0x10063c4cc
    std::uint32_t GetCaps(_DDSCAPS2*) override { return 0; }                             // @ 0x10063c4d4
    std::uint32_t GetClipper(IDirectDrawClipper**) override { return 0; }               // @ 0x10063c4dc
    std::uint32_t GetColorKey(std::uint32_t, _DDCOLORKEY*) override { return 0; }       // @ 0x10063c4e4
    std::uint32_t GetDC(HDC__**) override { return 0; }                                  // @ 0x10063c4ec
    std::uint32_t GetFlipStatus(std::uint32_t) override { return 0; }                   // @ 0x10063c4fc
    std::uint32_t GetOverlayPosition(std::int32_t*, std::int32_t*) override { return 0; }   // @ 0x10063c50c
    std::uint32_t GetPalette(IDirectDrawPalette**) override { return 0; }               // @ 0x10063c514
    std::uint32_t GetPixelFormat(_DDPIXELFORMAT* out) override;                          // @ 0x10063c51c
    std::uint32_t GetSurfaceDesc(_DDSURFACEDESC2* out) override;                         // @ 0x10063c56e
    std::uint32_t Initialize(IDirectDraw*, _DDSURFACEDESC2*) override { return 0; }     // @ 0x10063c592
    std::uint32_t IsLost() override { return 0; }                                        // @ 0x10063c59a
    std::uint32_t Lock(tagRECT* rect, _DDSURFACEDESC2* out, std::uint32_t flags, void* event) override;   // @ 0x10063c5a2
    std::uint32_t ReleaseDC(HDC__*) override { return 0; }                              // @ 0x10063cc94
    std::uint32_t Restore() override { return 0; }                                       // @ 0x10063cc9c
    std::uint32_t SetClipper(IDirectDrawClipper*) override { return 0; }                // @ 0x10063cca4
    std::uint32_t SetColorKey(std::uint32_t, _DDCOLORKEY*) override { return 0; }       // @ 0x10063ccac
    std::uint32_t SetOverlayPosition(std::int32_t, std::int32_t) override { return 0; } // @ 0x10063ccbc
    std::uint32_t SetPalette(IDirectDrawPalette*) override { return 0; }                // @ 0x10063ccc4
    std::uint32_t Unlock(tagRECT*) override;                                              // @ 0x10063cd42
    std::uint32_t UpdateOverlay(tagRECT*, IDirectDrawSurface7*, tagRECT*, std::uint32_t, _DDOVERLAYFX*) override { return 0; }   // @ 0x10063cd52
    std::uint32_t UpdateOverlayDisplay(std::uint32_t) override { return 0; }            // @ 0x10063cd5a
    std::uint32_t UpdateOverlayZOrder(std::uint32_t, IDirectDrawSurface7*) override { return 0; }   // @ 0x10063cd62
    std::uint32_t GetDDInterface(void**) override { return 0; }                          // @ 0x10063c4f4
    std::uint32_t PageLock(std::uint32_t) override { return 0; }                         // @ 0x10063cc84
    std::uint32_t PageUnlock(std::uint32_t) override { return 0; }                       // @ 0x10063cc8c
    std::uint32_t SetSurfaceDesc(_DDSURFACEDESC2*, std::uint32_t) override { return 0; } // @ 0x10063cd3a
    std::uint32_t SetPrivateData(const _GUID& key, void* data, std::uint32_t size, std::uint32_t flags) override;   // @ 0x10063ccd4
    std::uint32_t GetPrivateData(const _GUID& key, void* data, std::uint32_t* size) override;                        // @ 0x10063c54e
    std::uint32_t FreePrivateData(const _GUID& key) override;                            // @ 0x10063c4aa
    std::uint32_t GetUniquenessValue(std::uint32_t*) override { return 0; }              // @ 0x10063c58a
    std::uint32_t ChangeUniquenessValue() override { return 0; }                         // @ 0x10063c47c
    std::uint32_t SetPriority(std::uint32_t) override { return 0; }                      // @ 0x10063cccc
    std::uint32_t GetPriority(std::uint32_t*) override { return 0; }                     // @ 0x10063c546
    std::uint32_t SetLOD(std::uint32_t) override { return 0; }                           // @ 0x10063ccb4
    std::uint32_t GetLOD(std::uint32_t*) override { return 0; }                          // @ 0x10063c504

    std::int32_t refCount = 0;          // +8
    std::int32_t kind = 0;              // +0xc
    std::int32_t lockLevel = 0;         // +0x10
    _DDSURFACEDESC2 desc{};             // +0x18
    AlignedBlock* privateData = nullptr;   // +0xa0
    AlignedBlock* pixels = nullptr;        // +0xa8
};

}  // namespace sgl
