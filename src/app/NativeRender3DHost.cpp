// The native IRender3DHost (started from scripts/gen_stub_hosts.py's neutral host, now hand edited and skipped by the
// generator): the renderer listens on the game's message server for the camera's zoom / rotation changes. The
// lighting manager, the render properties and the settings are still the renderer's own defaults.
#include <string>
#include <vector>
#include "app/NativeLighting.h"
#include "sc4/Render3D.h"
#include "sc4/ServiceLocator.h"

namespace native {
namespace {
using namespace sc4;
struct StubRender3D final : IRender3DHost {
    void RegisterStaticShutdown(bool (*function)(void*), void* context) override {}
    cIGZMessageServer2* MessageServer() override { return SL::spMS2; }
    sc4::cISCPropertyHolder* RenderPropertyHolder() override { return {}; }
    IS4LightingManager* CreateLightingManager() override { return CreateNativeLightingManager(); }
    cIGZUnknown* TerrainView() override { return {}; }
    cIGZUnknown* City() override { return {}; }
    IRender3DSettings* Settings() override { return {}; }
    sc4::cSC4RenderProperties* RenderProperties() override { return {}; }
};
}  // namespace

void InstallRender3DStub() { static StubRender3D s; sc4::SetRender3DHost(&s); }

}  // namespace native
