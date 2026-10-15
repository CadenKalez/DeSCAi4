#include <cstdio>
#include <cstring>

#include "render/D3D7Stub.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sgl;

int main() {
    auto* dev = new cSGLDX7Direct3DDevice7Stub();
    dev->AddRef();
    void* out = nullptr;
    static_assert(sizeof(_GUID) == 24);
    unsigned char buf[24] = {};
    _GUID bad;
    std::memcpy(&bad, buf, sizeof bad);
    CHECK(dev->QueryInterface(bad, &out) == kDDErrNoInterface && out == nullptr);
    const unsigned char d3d[24] = {0x79, 0x9e, 0x04, 0xf5, 0, 0, 0, 0, 0x61, 0x48, 0xd2, 0x11, 0xa4, 0x07, 0, 0xa0, 0xc9, 0x06, 0x29, 0xa8};
    std::memcpy(buf, d3d, 24);
    CHECK(dev->QueryInterface(*reinterpret_cast<const _GUID*>(buf), &out) == kDDOk && out == dev);
    IDirect3DDevice7* iface = dev;
    CHECK(iface->BeginScene() == 0 && iface->EndScene() == 0);
    CHECK(iface->GetMaterial(nullptr) == kDDErrUnsupported && iface->GetRenderState(_D3DRENDERSTATETYPE{}, nullptr) == kDDErrUnsupported);
    CHECK(iface->Clear(0, nullptr, 0, 0, 0.0f, 0) == 0);
    CHECK(iface->Release() == 1);
    CHECK(iface->Release() == 0);   // frees
    std::printf(failures ? "FAILED\n" : "render_d3d7stub ok\n");
    return failures != 0;
}
