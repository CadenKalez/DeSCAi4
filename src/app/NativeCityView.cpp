#include "app/NativeCityView.h"

#include <cstdio>
#include <cstdlib>

#include "gz/DrawContext.h"
#include "render/gd/IGZGDriver.h"
#include "s3d/Camera.h"
#include "sc4/ViewUtilities.h"
#include "sc4/CameraControl.h"
#include "app/NativeTerrainView.h"
#include "app/NativeModelMaker.h"
#include "sc4/Render3D.h"
#include "sc4/View3DWin.h"
#include "sc4/cSC4City.h"
#include "sc4/cSTETerrain.h"
#include "sc4/ISC4City.h"
#include "sc4/OccupantManager.h"
#include <map>

namespace native {
namespace {

// The window's IRenderer3D seam over the recovered renderer.
class RendererAdapter final : public sc4::IRenderer3D {
public:
    RendererAdapter(sc4::cSC43DRender* render, sc4::cSC4CameraControl* control, gd::cIGZGDriver* driver, gz::cIGZDrawContext* screen) : render_(render), control_(control), driver_(driver), screen_(screen) { render_->AddRef(); }
    ~RendererAdapter() override { render_->Release(); }

    bool QueryInterface(std::uint32_t iid, void** out) override { return render_->QueryInterface(iid, out); }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override {
        if (--refs_ == 0) { delete this; return 0; }
        return refs_;
    }

    bool Init() override { return render_->Init(); }
    void Shutdown() override { render_->Shutdown(); }
    void* GetDrawContext() override { return render_->GetDrawContext(); }
    void* GetCamera() override { return render_->GetCamera(); }
    void ForceFullRedraw() override { render_->ForceFullRedraw(); }
    void Draw() override {
        // The window manager's 2-D drawing leaves its clip as the driver viewport and its own state on the device: hand the
        // device to the 3-D renderer for the frame (full viewport) and give it back afterwards.
        if (screen_) screen_->Suspend();
        driver_->SetViewport();
        // The terrain does not cover every pixel yet (and the window frame persists between paints): start each frame from a clear.
        driver_->ClearColor(0.12f, 0.12f, 0.20f, 1.0f);
        driver_->ClearDepth(1.0);
        driver_->Clear(0x5000);
        // PARTIAL: the renderer's camera / city notifications (DoMessage) are not recovered; the view utilities follow the camera here.
        if (SC4ViewUtilities* utilities = render_->GetViewUtilities()) utilities->UpdateCameraPosition(control_);
        if (std::getenv("SC4_DEBUG")) {
            static int printed = 0;
            if (printed++ < 2) {
                const cS3DVector3 t = control_->ViewTarget(), c = control_->CameraPosition(), d = control_->ViewDirection();
                if (s3d::cS3DCamera* camera = control_->Camera()) {
                    float l, r, t, b, n, f;
                    camera->GetViewVolume(l, r, t, b, n, f);
                    std::fprintf(stderr, "camera: view volume l %.1f r %.1f t %.1f b %.1f near %.1f far %.1f\n", l, r, t, b, n, f);
                }
                std::fprintf(stderr, "camera: target %.1f %.1f %.1f  pos %.1f %.1f %.1f  dir %.3f %.3f %.3f  zoom %d rot %d wpp %.4f\n", t.x, t.y, t.z, c.x, c.y, c.z, d.x, d.y, d.z, control_->Zoom(), control_->Rotation(), control_->WorldPerPixel());
            }
        }
        render_->Draw();
        if (screen_) screen_->Resume();
    }
    bool SetZoom(int zoom) override { render_->SetZoom(zoom); return true; }
    bool ZoomIn() override { render_->ZoomIn(); return true; }
    bool ZoomOut() override { render_->ZoomOut(); return true; }
    bool SetRotation(int rotation) override { render_->SetRotation(rotation); return true; }
    bool RotateLeft() override { render_->RotateLeft(); return true; }
    bool RotateRight() override { render_->RotateRight(); return true; }
    bool SetZoomAndRotation(int zoom, int rotation) override { render_->SetZoomAndRotation(zoom, rotation); return true; }
    bool PickTerrain(int x, int y, float* out3, bool always, bool arg5) override { return render_->PickTerrain(x, y, out3, always, arg5); }
    bool Pick(int x, int y, cISC4OccupantFilter* filter, s3d::cIS3DModelInstance*& model) override { return render_->Pick(x, y, filter, model); }
    cIGZUnknown* ViewUtilities() override { return nullptr; }
    cIGZUnknown* LightingManager() override { return nullptr; }

private:
    sc4::cSC43DRender* render_;
    sc4::cSC4CameraControl* control_;
    gd::cIGZGDriver* driver_;
    gz::cIGZDrawContext* screen_;
    std::uint32_t refs_ = 0;
};

}  // namespace

bool AttachCityRenderStack(sc4::cSC4View3DWin& view, cISC4City* cityInterface, gd::cIGZGDriver* driver, gz::cIGZDrawContext* screen, int width, int height) {
    sc4::cSTETerrain* terrain = nullptr;
    if (auto* city = dynamic_cast<sc4::cSC4City*>(cityInterface)) {
        cIGZUnknown* terrainObject = city->GetComponent(sc4::CityComponent::Terrain);
        void* serializable = nullptr;
        if (terrainObject && terrainObject->QueryInterface(0xab953253, &serializable)) terrain = static_cast<sc4::cSTETerrain*>(static_cast<cIGZSerializable*>(serializable));
        if (terrain) terrain->Init();   // the port's terrain is not a cIGZInitShutdown, so the city's component pass skips it
        std::printf("city view: terrain component %p -> %p", static_cast<void*>(terrainObject), static_cast<void*>(terrain));
        if (terrain) std::printf(" cells %u x %u", terrain->CellCountX(), terrain->CellCountZ());
        std::printf("\n");
        // The occupant manager is not a cIGZInitShutdown in the port, so the city's component pass skips it; its Init
        // initialises every occupant the save restored (and lets the queries below and the model maker see them).
        cIGZUnknown* om = city->GetComponent(sc4::CityComponent::OccupantManager);
        void* iface = nullptr;
        if (om && om->QueryInterface(sc4::kGZIID_cISC4OccupantManager, &iface)) {
            auto* manager = static_cast<sc4::cISC4OccupantManager*>(iface);
            manager->Init();
            if (std::getenv("SC4_DEBUG")) {   // census of the loaded occupants by type
                static std::map<std::uint32_t, int> census;
                census.clear();
                const float lo[3] = {-1e6f, -1e6f, -1e6f}, hi[3] = {1e6f, 1e6f, 1e6f};
                auto* all = new sc4::cSC4OccupantFilter();
                all->AddRef();
                manager->IterateOccupantsByBBox([](sc4::cISC4Occupant* o, void*) { ++census[o->GetType()]; return true; }, nullptr, lo, hi,
                                                static_cast<sc4::cISC4OccupantFilter*>(all));
                all->Release();
                for (auto& [type, n] : census) std::printf("occupants: type %08x x%d\n", type, n);
            }
            manager->Release();
        }
    }
    // DoPreCityInit: the camera, its control (looking at the middle of a 1 km city) and the renderer.
    auto* camera = new s3d::cS3DCamera();
    camera->AddRef();
    camera->Init();
    auto* control = new sc4::cSC4CameraControl();
    control->AddRef();
    control->SetViewportSize(width, height);
    control->Init(cS3DVector3{512.0f, 0.0f, 512.0f}, camera);
    if (terrain) {   // DoPostCityInit: the camera knows the city's size; look at its middle
        const float sizeX = static_cast<float>(terrain->CellCountX()) * 16.0f, sizeZ = static_cast<float>(terrain->CellCountZ()) * 16.0f;
        control->SetCitySize(sizeX, sizeZ);
        control->SetZoomAndRotation(std::getenv("SC4_ZOOM") ? std::atoi(std::getenv("SC4_ZOOM")) : 0, 0);
        float focusX = sizeX * 0.5f, focusZ = sizeZ * 0.5f;
        if (const char* focus = std::getenv("SC4_FOCUS")) std::sscanf(focus, "%f,%f", &focusX, &focusZ);   // dev: look at a world x,z
        control->SetViewTargetPosition(cS3DVector3{focusX, 0.0f, focusZ});
        control->SendViewChangeMessage();
    }

    auto* render = new sc4::cSC43DRender();
    render->AddRef();
    sc4::cSC43DRender::StaticSetGDriver(driver);
    if (terrain) native::AttachTerrainView(*render, *terrain, driver);
    render->SetViewportSize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    render->SetCamera(camera);
    render->SetCameraControl(control);
    const bool ok = render->Init();
    render->SetBackingStoreEnabled(false);   // the Metal driver keeps no backing store yet: a restore would replay whatever the screen held
    std::printf("city view: renderer %s\n", ok ? "up" : "FAILED");
    if (auto* city = dynamic_cast<sc4::cSC4City*>(cityInterface); city && ok) {
        native::InstallModelResources(driver);
        native::AttachModelMaker(*render, *city);
    }

    auto* adapter = new RendererAdapter(render, control, driver, screen);
    view.AttachRenderStack(adapter, nullptr, control);
    view.EnablePainting(true);
    return ok;
}

}  // namespace native
