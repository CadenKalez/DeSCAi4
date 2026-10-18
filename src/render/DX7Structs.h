// Concrete layouts of the DirectX 7 structures the shim reads and writes (the 64-bit Mac build keeps the 4-byte
// DWORDs but 8-byte pointers, so _DDSURFACEDESC2 is 0x88 bytes). Declared opaque in DX7Types.h.
#pragma once
#include <cstdint>

#include "render/DX7Types.h"

namespace sgl {

struct tagRECT { std::int32_t left, top, right, bottom; };
struct tagPOINT { std::int32_t x, y; };
struct _D3DRECT { std::int32_t x1, y1, x2, y2; };
struct _D3DVECTOR { float x, y, z; };
struct _D3DMATRIX { float m[16]; };
struct _D3DVIEWPORT7 { std::uint32_t dwX, dwY, dwWidth, dwHeight; float dvMinZ, dvMaxZ; };
struct _D3DCOLORVALUE { float r, g, b, a; };
struct _D3DMATERIAL7 { _D3DCOLORVALUE diffuse, ambient, specular, emissive; float power; };
struct _D3DVERTEXBUFFERDESC { std::uint32_t dwSize, dwCaps, dwFVF, dwNumVertices; };

struct _DDPIXELFORMAT {
    std::uint32_t dwSize, dwFlags, dwFourCC, dwRGBBitCount;   // +0 .. +0xc
    std::uint32_t dwRBitMask, dwGBitMask, dwBBitMask, dwRGBAlphaBitMask;   // +0x10 .. +0x1c
};
static_assert(sizeof(_DDPIXELFORMAT) == 0x20);

struct _DDSCAPS2 { std::uint32_t dwCaps, dwCaps2, dwCaps3, dwCaps4; };
struct _DDCOLORKEY { std::uint32_t dwColorSpaceLowValue, dwColorSpaceHighValue; };

struct _DDSURFACEDESC2 {
    std::uint32_t dwSize;              // +0   0x88
    std::uint32_t dwFlags;             // +4
    std::uint32_t dwHeight;            // +8
    std::uint32_t dwWidth;             // +0xc
    std::int32_t lPitch;               // +0x10 (shared with dwLinearSize)
    std::uint32_t dwBackBufferCount;   // +0x14
    std::uint32_t dwMipMapCount;       // +0x18
    std::uint32_t dwAlphaBitDepth;     // +0x1c
    std::uint32_t dwReserved;          // +0x20
    void* lpSurface;                   // +0x28
    _DDCOLORKEY ddckCKDestOverlay, ddckCKDestBlt, ddckCKSrcOverlay, ddckCKSrcBlt;   // +0x30 .. +0x4f
    _DDPIXELFORMAT ddpfPixelFormat;    // +0x50
    _DDSCAPS2 ddsCaps;                 // +0x70
    std::uint32_t dwTextureStage;      // +0x80
};
static_assert(sizeof(_DDSURFACEDESC2) == 0x88);

// _D3DDeviceDesc7 is 0xf4 bytes; the shim only ever copies it whole, or fills a few dwords at fixed offsets.
struct _D3DDeviceDesc7 { std::uint8_t bytes[0xf4]; };

}  // namespace sgl
