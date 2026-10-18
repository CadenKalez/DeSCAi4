#include "render/DX7Host.h"

#include <cstring>

#include "render/D3DVertexBuffer7.h"
#include "render/DDSurface7.h"
#include "render/MacDisplay.h"
#include "render/MacOpenGL.h"

namespace sgl {

namespace {
class CDefaultHost final : public IDX7Host {
public:
    IDirectDrawSurface7* NewSurface(_DDSURFACEDESC2* desc) override { return new CDirectDrawSurface7(*desc); }
    IDirect3DVertexBuffer7* NewVertexBuffer(_D3DVERTEXBUFFERDESC* desc, std::uint32_t flags) override {
        return new CDirect3DVertexBuffer7(desc, flags);
    }
    bool PCIDisplayInfo(std::uint32_t& vendorId, std::uint32_t& deviceId, std::string& name) override {
        ASLPCIDisplayInfo_t info{};
        if (GetPCIDisplayInfo(MacDisplay::GetDisplayID(), &info) != 0) return false;
        vendorId = info.vendorID;
        deviceId = info.deviceID;
        name = info.name;
        return true;
    }
    void SetDeviceID(std::uint32_t deviceId, std::uint32_t vendorId) override { MacOpenGL::SetDeviceID(deviceId, vendorId, true); }
};
}  // namespace

IDX7Host* DefaultDX7Host() {
    static CDefaultHost host;
    return &host;
}

}  // namespace sgl
