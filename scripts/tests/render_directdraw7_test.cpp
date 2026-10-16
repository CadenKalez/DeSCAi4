#include <cstdio>
#include <cstring>

#include "render/DirectDraw7.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sgl;

namespace {
struct Host final : IDX7Host {
    int surfaces = 0, buffers = 0; std::uint32_t devId = 0, vendId = 0; bool ok = true;
    IDirectDrawSurface7* NewSurface(_DDSURFACEDESC2*) override { ++surfaces; return reinterpret_cast<IDirectDrawSurface7*>(0x10); }
    IDirect3DVertexBuffer7* NewVertexBuffer(_D3DVERTEXBUFFERDESC*, std::uint32_t) override { ++buffers; return reinterpret_cast<IDirect3DVertexBuffer7*>(0x20); }
    bool PCIDisplayInfo(std::uint32_t& v, std::uint32_t& d, std::string& n) override { v = 0x10de; d = 0x0a20; n = "Test Board"; return ok; }
    void SetDeviceID(std::uint32_t d, std::uint32_t v) override { devId = d; vendId = v; }
};
}  // namespace

int main() {
    Host host;
    SetDX7Host(&host);
    auto* dd = new CDirectDraw7();
    IDirectDrawSurface7* surface = nullptr;
    CHECK(dd->CreateSurface(nullptr, &surface, nullptr) == kDDOk && surface == reinterpret_cast<IDirectDrawSurface7*>(0x10));
    unsigned char iid[24] = {0x77, 0x9e, 0x04, 0xf5, 0, 0, 0, 0, 0x61, 0x48, 0xd2, 0x11, 0xa4, 0x07, 0, 0xa0, 0xc9, 0x06, 0x29, 0xa8};
    void* out = nullptr;
    _GUID guid;
    std::memcpy(&guid, iid, sizeof guid);
    CHECK(dd->QueryInterface(guid, &out) == kDDOk && out != nullptr);
    auto* d3d = static_cast<IDirect3D7*>(out);
    IDirect3DVertexBuffer7* vb = nullptr;
    CHECK(d3d->CreateVertexBuffer(nullptr, &vb, 0) == kDDOk && vb == reinterpret_cast<IDirect3DVertexBuffer7*>(0x20));
    CHECK(d3d->EvictManagedTextures() == kDDOk);
    CHECK(d3d->Release() == 0);
    iid[0] = 0;
    std::memcpy(&guid, iid, sizeof guid);
    CHECK(dd->QueryInterface(guid, &out) == kDDErrNoInterface && out == nullptr);
    std::uint32_t caps[4] = {};
    CHECK(dd->GetCaps(nullptr, reinterpret_cast<_DDCAPS_DX7*>(caps)) == kDDOk && caps[1] == 1);
    alignas(8) unsigned char id[0x440];   // the original zeroes up to 0x438
    std::memset(id, 0xcc, sizeof id);
    CHECK(dd->GetDeviceIdentifier(reinterpret_cast<tagDDDEVICEIDENTIFIER2*>(id), 0) == kDDOk);
    CHECK(std::strcmp(reinterpret_cast<char*>(id), "Mac OS") == 0 && std::strcmp(reinterpret_cast<char*>(id + 0x200), "Test Board") == 0);
    std::uint32_t v; std::memcpy(&v, id + 0x40c, 4);
    CHECK(v == 0x10de && host.devId == 0x0a20 && host.vendId == 0x10de);
    dd->AddRef();
    CHECK(dd->Release() == 1);
    CHECK(dd->Release() == 0);
    std::printf(failures ? "FAILED\n" : "render_directdraw7 ok\n");
    return failures != 0;
}
