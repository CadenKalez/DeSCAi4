#include "render/DirectDraw7.h"

#include <cstdio>
#include <cstring>

namespace sgl {

static IDX7Host* g_host = nullptr;
IDX7Host* DX7Host() { return g_host; }
void SetDX7Host(IDX7Host* host) { g_host = host; }

namespace {
// _IID_IDirect3D7 as stored by the game (24 bytes compared).
const std::uint8_t kIidDirect3D7[24] = {0x77, 0x9e, 0x04, 0xf5, 0, 0, 0, 0, 0x61, 0x48, 0xd2, 0x11,
                                        0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8, 0, 0, 0, 0};
}  // namespace

std::int32_t CDirect3D7::Release() {
    const std::int32_t count = --refCount_;
    if (count == 0) delete this;
    return count;
}

std::uint32_t CDirect3D7::CreateVertexBuffer(_D3DVERTEXBUFFERDESC* desc, IDirect3DVertexBuffer7** out, std::uint32_t flags) {
    *out = g_host->NewVertexBuffer(desc, flags);
    return kDDOk;
}

// Only IID_IDirect3D7 is served; any other id yields a null interface (the original then calls through it).
std::uint32_t CDirectDraw7::QueryInterface(const _GUID& iid, void** out) {
    *out = nullptr;
    if (std::memcmp(kIidDirect3D7, &iid, sizeof(_GUID)) != 0) return kDDErrNoInterface;
    auto* direct3d = new CDirect3D7();
    *out = direct3d;
    return kDDOk;
}

std::int32_t CDirectDraw7::Release() {
    const std::int32_t count = --refCount_;
    if (count == 0) delete this;
    return count;
}

std::uint32_t CDirectDraw7::CreateSurface(_DDSURFACEDESC2* desc, IDirectDrawSurface7** out, IUnknown*) {
    *out = g_host->NewSurface(desc);
    return kDDOk;
}

std::uint32_t CDirectDraw7::GetCaps(_DDCAPS_DX7*, _DDCAPS_DX7* helCaps) {
    *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(helCaps) + 4) = 1;
    return kDDOk;
}

// Fills DDDEVICEIDENTIFIER2: driver "Mac OS", the board name as description, driver version 1.0 (high dword 1),
// vendor / device ids from the PCI query, guid starting "SCAM".
std::uint32_t CDirectDraw7::GetDeviceIdentifier(tagDDDEVICEIDENTIFIER2* id, std::uint32_t) {
    std::uint32_t vendorId = 0, deviceId = 0;
    std::string name;
    if (!g_host->PCIDisplayInfo(vendorId, deviceId, name)) {
        vendorId = 0;
        deviceId = 0;
        name.clear();
    }
    auto* bytes = reinterpret_cast<std::uint8_t*>(id);
    std::memset(bytes + 7, 0, 0x431);
    bytes[6] = 0;
    std::memcpy(bytes, "Mac OS", 6);
    std::snprintf(reinterpret_cast<char*>(bytes + 0x200), 0x200, "%s", name.c_str());
    const std::uint64_t driverVersion = 0x100000000ull, guidStart = 0x4d414353;
    std::memcpy(bytes + 0x400, &driverVersion, 8);
    std::memcpy(bytes + 0x418, &guidStart, 8);
    std::memcpy(bytes + 0x408, &deviceId, 4);
    std::memcpy(bytes + 0x40c, &vendorId, 4);
    std::memset(bytes + 0x420, 0, 4);
    g_host->SetDeviceID(deviceId, vendorId);
    return kDDOk;
}

}  // namespace sgl
