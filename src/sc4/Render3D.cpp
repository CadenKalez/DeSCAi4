// cSC43DRender — see src/sc4/Render3D.h for the layout and the quirks. This file holds the object's lifecycle
// (0x10075f3a8..0x100760dbc), the render-property read (0x100760546), the view-object lists (0x10075f3e6..0x10075f4ce,
// 0x1007693d0..0x100769508), the camera / clip / driver / backing-store wiring, the screen-shake list and the two
// stats strings. The draw passes and the model / quad-grid queue are declared in the header but not implemented here;
// the pending ledger file lists them with the reason.
#include "sc4/Render3D.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "gz/Buffer.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZVariant.h"
#include "render/gd/VideoModes.h"
#include "s3d/ModelInstance.h"
#include "sc4/CameraControl.h"
#include "sc4/DBSegment.h"
#include "sc4/QuadGrid.h"
#include "sc4/RenderProperties.h"

namespace sc4 {

namespace {

// The binary's file-scope tables (0x100a2a170 / 0x100a2a180), indexed by camera rotation.
constexpr float kFVToCXBase[4] = {3000.0f, 1400.0f, 5200.0f, 6800.0f};
constexpr float kFVToCYBase[4] = {4500.0f, 6400.0f, 7200.0f, 5300.0f};
// City size Init stores into the view utilities (+0x28 / +0x2c of SC4ViewUtilities, 0x45800000).
constexpr float kDefaultCitySize = 4096.0f;

// Driver extension ids StaticSetGDriver queries (0x1007614c6): the buffer-region extension (the backing store in this
// port) and the colour-buffer extension (snapshots, unused here).
constexpr std::uint32_t kIIDBufferRegionExtension = 0x669565fe;
constexpr std::uint32_t kIIDSnapshotExtension = 0xe69bfe2a;
// cSC4CameraControl's class id, used by Read for the serialized sub-object (0x1007612a2).
constexpr std::uint32_t kCLSIDCameraControl = 0xc9c628ec;
// The message types Init subscribes to and Shutdown unsubscribes (0x1007602xx / 0x100760dbc).
constexpr std::uint32_t kRender3DMessages[] = {
    0xa6b79601, 0xa6b79602, 0xa6b79612, 0x870bc918, 0xa6b79621, 0x26d31ec1,
    0xc9f775bb, 0xc53d10aa, 0x4a9f2ffe, 0x8acaa95f, 0x6acb92a9,
};
// The highlight-colour properties ReadProperties reads: 0xea639fb1..0xea639fbb -> draw-context slots 0..10.
constexpr std::uint32_t kHighlightPropertyBase = 0xea639fb1;
constexpr int kHighlightPropertyCount = 11;

// SC4DrawContext wants the view utilities through its one-field seam; SC4ViewUtilities keeps +0x15c private, so the
// adapter inverts ViewportUnitsToWorldUnits(1). UpdateCameraPosition writes +0x15c = PixelPerWorld.
class ViewUtilitiesSeam final : public IS4ViewUtilities {
public:
    explicit ViewUtilitiesSeam(SC4ViewUtilities* utilities) : utilities_(utilities) {}
    float ViewportUnitsPerWorld() const override {
        const float worldPerUnit = utilities_ ? utilities_->ViewportUnitsToWorldUnits(1.0f) : 0.0f;
        return worldPerUnit != 0.0f ? 1.0f / worldPerUnit : 0.0f;
    }

private:
    SC4ViewUtilities* utilities_;
};

// The settings seam over the port's cSC4RenderProperties. The original reads fields of the inner cProperties object
// (SL::spRenderProperties +0x18); the port has the generated default table, so the values below are the ones the
// decompilation compares against, and the tests supply their own IRender3DSettings fake.
class DefaultSettings final : public IRender3DSettings {
public:
    bool DisablePartialRegionWrites() const override { return false; }   // +0x7b8
    bool ClipRectSupported() const override { return true; }             // +0x638
    int BackingStoreMaxZoom() const override { return 3; }               // +0x278
    int StaticViewMaxZoomA() const override { return 3; }                // +0x308
    int StaticViewMaxZoomB() const override { return 3; }                // +0x2c0
    int StaticViewMaxZoomC() const override { return 3; }                // +0x350
    bool StaticViewRedrawOnViewportChange() const override { return true; }   // +0xb8
};

struct StreamReader {
    cIGZIStream* stream;
    bool ok = true;
    std::int32_t Int() {
        std::int32_t value = 0;
        ok = stream->GetSint32(value) && ok;
        return value;
    }
    std::uint32_t Uint() {
        std::uint32_t value = 0;
        ok = stream->GetUint32(value) && ok;
        return value;
    }
};

struct StreamWriter {
    cIGZOStream* stream;
    bool ok = true;
    void Int(std::int32_t value) { ok = stream->SetSint32(value) && ok; }
    void Uint(std::uint32_t value) { ok = stream->SetUint32(value) && ok; }
};

}  // namespace

IRender3DHost* Render3DHost() { return cSC43DRender::Host(); }
void SetRender3DHost(IRender3DHost* host) { cSC43DRender::SetHost(host); }

// ---------------------------------------------------------------------------------------------
// cSC43DRender: construction / destruction / COM
// ---------------------------------------------------------------------------------------------
cSC43DRender::cSC43DRender() {
    // The constructor always allocates the view utilities and the draw context (0x1007603a2 / 0x1007603b6); the quad
    // grid comes from Init.
    viewUtilities_ = new SC4ViewUtilities();
    viewUtilitiesCameraHost_.owner = this;
    drawContext_ = new SC4DrawContext();
    frameTimer_.SetUnits(cRZTimer::kMilliseconds);   // cRZTimer::ConstructWithUnits(this + 0x340, 4)
    shakeTimer_.SetUnits(cRZTimer::kSeconds);        // cRZTimer::ConstructWithUnits(this + 0x380, 5)
    lightLevels_[3] = 10000.0f;
    lightLevels_[4] = 15000.0f;
    lightLevelExtra_ = 15000.0f;
    unknown358_ = 10000;
    spInstance = this;
}

cSC43DRender::~cSC43DRender() {
    Shutdown();
    delete viewUtilities_;
    viewUtilities_ = nullptr;
    delete drawContext_;
    drawContext_ = nullptr;
    // The shake list holds plain records; the dirty-rectangle list and the model queue hold referenced models.
    for (DirtyRectangleQueueItem& item : dirtyRectangles_) {
        if (item.model) item.model->Release();
    }
    dirtyRectangles_.clear();
    for (ModelChangeQueueItem& item : modelChanges_) {
        if (item.model) item.model->Release();
    }
    modelChanges_.clear();
    shakes_.clear();
    for (std::list<ViewObjectInfo>& list : viewObjectLists_) ClearViewObjectList(list);
    if (backgroundPicture_) backgroundPicture_->Release();
    backgroundPicture_ = nullptr;
    if (terrainView_) terrainView_->Release();
    terrainView_ = nullptr;
    if (backgroundViewObject_) backgroundViewObject_->Release();
    backgroundViewObject_ = nullptr;
    if (cameraControl_) cameraControl_->Release();
    cameraControl_ = nullptr;
    if (camera_ && camera_->Release() == 0) delete camera_;
    camera_ = nullptr;
    if (uiCamera_ && uiCamera_->Release() == 0) delete uiCamera_;
    uiCamera_ = nullptr;
    if (lightingManager_) delete lightingManager_;
    lightingManager_ = nullptr;
    if (renderProperties_) renderProperties_->Release();
    renderProperties_ = nullptr;
    if (graphicSystem_) graphicSystem_->Release();
    graphicSystem_ = nullptr;
    if (spInstance == this) spInstance = nullptr;
}

bool cSC43DRender::QueryInterface(std::uint32_t iid, void** out) {
    // 0x10075feee: an unknown id returns false and writes nothing; a hit is AddRef'd.
    if (out == nullptr) return false;
    if (iid == 1 || iid == kCLSID) {
        *out = static_cast<cISC43DRender*>(this);
    } else if (iid == 0x681bd529 || iid == 0x452294aa) {
        *out = static_cast<cIGZMessageTarget2*>(this);
    } else if (iid == 0xe4fda3d4) {
        *out = static_cast<cIGZSerializable*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cSC43DRender::AddRef() { return ++refCountPrimary_; }

std::uint32_t cSC43DRender::Release() {
    // 0x10075fe36: below 2 the original calls Shutdown (vslot 0x20) and then the deleting destructor (vslot 0x260),
    // so the last release frees the object. Heap-allocate a renderer before releasing it to zero (AGENT_RULES: COM
    // objects must not be stack-allocated when a test releases them for real).
    if (refCountPrimary_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return --refCountPrimary_;
}

// ---------------------------------------------------------------------------------------------
// Statics
// ---------------------------------------------------------------------------------------------
bool cSC43DRender::StaticInit(void* context) {
    (void)context;
    if (!mbStaticInitialized) {
        mbStaticInitialized = true;
        if (host_) host_->RegisterStaticShutdown(&cSC43DRender::StaticShutdown, nullptr);
    }
    return true;
}

bool cSC43DRender::StaticShutdown(void* context) {
    (void)context;
    if (mbStaticInitialized) {
        mbStaticInitialized = false;
        if (mpGDriver) {
            if (mpBackingStoreExtension) {
                mpBackingStoreExtension->Destroy();
                mpBackingStoreExtension = nullptr;
            }
            mpGDriver->Release();
            mpGDriver = nullptr;
        }
        mpDriverSnapshotExtension = nullptr;
    }
    return true;
}

void cSC43DRender::StaticSetGDriver(gd::cIGZGDriver* driver) {
    // 0x1007614c6
    if (mpGDriver == driver) return;
    if (mpGDriver && mpBackingStoreExtension) {
        mpBackingStoreExtension->Destroy();   // _glDeleteFramebuffersEXT
        mpBackingStoreExtension = nullptr;
    }
    if (driver) driver->AddRef();
    if (mpGDriver) mpGDriver->Release();
    mpGDriver = driver;
    mpDriverSnapshotExtension = nullptr;
    if (driver) {
        void* extension = nullptr;
        if (driver->Punt(kIIDBufferRegionExtension, &extension))
            mpBackingStoreExtension = static_cast<gd::IBackingStore*>(extension);
        void* snapshot = nullptr;
        if (driver->Punt(kIIDSnapshotExtension, &snapshot)) mpDriverSnapshotExtension = snapshot;
        if (mpBackingStoreExtension) CreateBackingStore();
    }
}

bool cSC43DRender::CreateBackingStore() {
    // 0x1007699ee: the size is the driver viewport (GetViewport returns x, y, x + w, y + h). The GL renderbuffers are
    // the driver's IBackingStore service in this port (the recovered logic lives in cSC43DBackingStore).
    if (!mpBackingStoreExtension || !mpGDriver) return false;
    std::uint32_t viewport[4] = {0, 0, 0, 0};
    mpGDriver->GetViewport(viewport);
    const bool ok = mpBackingStoreExtension->Create(viewport[2] - viewport[0], viewport[3] - viewport[1]);
    if (spInstance) spInstance->backingStoreValid_ = ok;
    return ok;
}

bool cSC43DRender::HasBackingStore() { return mpBackingStoreExtension != nullptr; }

// ---------------------------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------------------------
bool cSC43DRender::Init() {
    // 0x10076002a
    if (initialized_) return true;
    if (!mbStaticInitialized) StaticInit(nullptr);

    quadGrid_ = CreateQuadGrid();
    for (int i = 0; i < 4; ++i) {
        fixedViewToContainerX_[i] = static_cast<float>(mapScaleFactor_) * kFVToCXBase[i];
        fixedViewToContainerY_[i] = static_cast<float>(mapScaleFactor_) * kFVToCYBase[i];
    }

    // SL::spRenderProperties: the port host supplies the set; Init takes its own reference (0x1007601e2).
    if (host_) {
        cSC4RenderProperties* properties = host_->RenderProperties();
        if (properties != renderProperties_) {
            if (renderProperties_) renderProperties_->Release();
            renderProperties_ = properties;
            if (renderProperties_) renderProperties_->AddRef();
        }
    }

    IRender3DSettings* settings = host_ ? host_->Settings() : nullptr;
    DefaultSettings defaults;
    if (!settings) settings = &defaults;
    // +0xaf / +0xb0: the buffer-region extension answers "can do partial region writes" at vslot 0x48.
    partialWritesSupported_ = mpBackingStoreExtension != nullptr;
    partialWritesAllowed_ = partialWritesSupported_ && !settings->DisablePartialRegionWrites();

    // The camera / city / property notifications (0x10076025a).
    if (host_) {
        if (cIGZMessageServer2* server = host_->MessageServer()) {
            for (std::uint32_t type : kRender3DMessages) server->AddNotification(this, type);
        }
    }

    // Lighting manager (+0x178, 0x240 bytes in the original, 0x1007602f6).
    if (!lightingManager_ && host_) lightingManager_ = host_->CreateLightingManager();

    // The orthographic camera is +0x130; the 3D camera +0xd8 comes from SetCamera (the camera control owns it), so
    // Init only creates the ortho one (0x1007602f6).
    if (!uiCamera_) {
        uiCamera_ = new s3d::cS3DCamera();
        uiCamera_->AddRef();
        uiCamera_->Init();
        uiCamera_->SetViewport(static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_));
        uiCamera_->SetOrtho(0.0f, static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_), 0.0f,
                            0.0f, 32767.0f);
        uiCamera_->MakeIdentity();
        std::memcpy(projectionMatrix_, uiCamera_->ProjectionMatrix(), sizeof(projectionMatrix_));
    }

    viewUtilities_->SetHost(&viewUtilitiesCameraHost_);   // the original stores the renderer itself at +8
    viewUtilities_->SetCitySize(kDefaultCitySize, kDefaultCitySize);
    if (cameraControl_) viewUtilities_->UpdateCameraPosition(cameraControl_);
    viewUtilities_->BuildDrawFrustumDataFromViewportRect(drawFrustumRect_, drawFrustumRect_ + 2);

    captureEnabled_ = false;
    unknown214_ = false;
    drawContext_->SetRenderPass(0);
    drawContext_->SetDriver(mpGDriver);
    drawContext_->SetRenderProperties(renderProperties_);
    drawContext_->SetCameraControl(cameraControl_);
    drawContext_->SetLightingManager(lightingManager_);
    drawContext_->SetViewportSize(viewportWidth_, viewportHeight_);
    viewUtilitiesSeam_ = std::make_unique<ViewUtilitiesSeam>(viewUtilities_);
    drawContext_->SetViewUtilities(viewUtilitiesSeam_.get());
    drawContext_->SetZoomAndRotation(viewUtilities_->Zoom(), viewUtilities_->Rotation());
    drawContext_->Init();

    ReadProperties();
    dirtyMaxArea_ = 0x7fffffff;   // +0x304
    unknown320_ = 40000;          // +0x320
    initialized_ = true;
    return true;
}

SC4QuadGrid* cSC43DRender::CreateQuadGrid() {
    // 0x1007604c2
    const float size = static_cast<float>(mapScaleFactor_) * 8192.0f;
    unknown190_ = size;   // +0x190, the same value the original caches
    SC4QuadGrid* grid = new SC4QuadGrid(1.0f, 1.0f);
    grid->SetSize(size, static_cast<float>(mapScaleFactor_) * 32.0f);
    return grid;
}

bool cSC43DRender::Shutdown() {
    // 0x100760dbc
    if (!initialized_) return true;
    ClearModelChangeQueue();
    if (quadGrid_) {
        quadGrid_->RemoveAllElements();
        delete quadGrid_;
        quadGrid_ = nullptr;
    }
    for (std::list<ViewObjectInfo>& list : viewObjectLists_) ClearViewObjectList(list);
    if (drawContext_) drawContext_->Shutdown();
    if (host_) {
        if (cIGZMessageServer2* server = host_->MessageServer()) {
            for (std::uint32_t type : kRender3DMessages) server->RemoveNotification(this, type);
        }
    }
    if (lightingManager_) {
        delete lightingManager_;
        lightingManager_ = nullptr;
    }
    if (backgroundPicture_) {
        backgroundPicture_->Release();
        backgroundPicture_ = nullptr;
    }
    if (backgroundTexture_ != 0 && mpGDriver) {
        mpGDriver->DeleteTextures(1, &backgroundTexture_);
        backgroundTexture_ = 0;
    }
    if (terrainView_) {
        terrainView_->Release();
        terrainView_ = nullptr;
    }
    if (backgroundViewObject_) {
        backgroundViewObject_->Release();
        backgroundViewObject_ = nullptr;
    }
    if (cameraControl_) {
        cameraControl_->Release();
        cameraControl_ = nullptr;
    }
    if (camera_) {
        camera_->Shutdown();
        if (camera_->Release() == 0) delete camera_;
        camera_ = nullptr;
    }
    if (uiCamera_) {
        if (uiCamera_->Release() == 0) delete uiCamera_;
        uiCamera_ = nullptr;
    }
    if (renderProperties_) {
        renderProperties_->Release();
        renderProperties_ = nullptr;
    }
    viewUtilitiesSeam_.reset();
    initialized_ = false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------------------------
void cSC43DRender::ReadProperties() {
    // 0x100760546: the render-property group 0x4a639ef2 holds one float[4] per highlight colour.
    if (!host_ || !drawContext_) return;
    cISCPropertyHolder* holder = host_->RenderPropertyHolder();
    if (!holder) return;
    for (int index = 0; index < kHighlightPropertyCount; ++index) {
        cISCProperty* property = holder->GetProperty(kHighlightPropertyBase + index);
        if (!property) continue;
        const cIGZVariant* value = property->GetPropertyValue();
        if (!value) continue;
        const std::uint16_t type = value->GetType();
        if (type != (cIGZVariant::kTypeFloat32 | cIGZVariant::kTypeArrayFlag)) continue;
        if (value->GetCount() != 4) continue;   // the original requires exactly four floats
        const float* data = value->RefFloat32();
        if (!data) continue;
        drawContext_->SetHighlightColor(index, cS3DVector4{data[0], data[1], data[2], data[3]});
    }
}

// ---------------------------------------------------------------------------------------------
// Graphic system / driver
// ---------------------------------------------------------------------------------------------
void cSC43DRender::SetGraphicSystem(IRender3DGraphicSystem* system) {
    // 0x100761392 (PARTIAL: the original also wires the two embedded graphic-port sub-objects and the two name strings
    // the GZ graphic system fills; the port keeps the reference protocol and asks the seam for the two ports).
    if (system) system->AddRef();
    if (graphicSystem_) graphicSystem_->Release();
    graphicSystem_ = system;
    if (system) {
        void* primary = nullptr;
        void* secondary = nullptr;
        system->GetGraphicPorts(&primary, &secondary);
        graphicPortA_ = GraphicPort{system, 0};
        graphicPortB_ = GraphicPort{system, 1};
    } else {
        graphicSystemNameA_.clear();
        graphicSystemNameB_.clear();
        graphicPortA_ = GraphicPort{};
        graphicPortB_ = GraphicPort{};
    }
}

void cSC43DRender::SetGDriver(gd::cIGZGDriver* driver) {
    // 0x10076143c
    StaticSetGDriver(driver);
    DefaultSettings defaults;
    IRender3DSettings* settings = host_ ? host_->Settings() : nullptr;
    if (!settings) settings = &defaults;
    partialWritesSupported_ = mpBackingStoreExtension != nullptr;
    partialWritesAllowed_ = partialWritesSupported_ && !settings->DisablePartialRegionWrites();
    if (camera_) {
        gd::sGDMode mode{};
        if (mpGDriver) mpGDriver->GetVideoModeInfo(mode);   // vslot 0x208, the depth size source
        camera_->SetDepthSize(65536.0f);
    }
    if (drawContext_) drawContext_->SetDriver(mpGDriver);
}

// ---------------------------------------------------------------------------------------------
// Camera / viewport / clip
// ---------------------------------------------------------------------------------------------
SC4ViewUtilities* cSC43DRender::GetViewUtilities() const { return viewUtilities_; }
IS4LightingManager* cSC43DRender::GetLightingManager() const { return lightingManager_; }
SC4DrawContext* cSC43DRender::GetDrawContext() const { return drawContext_; }

void cSC43DRender::GetViewportSize(std::uint32_t& width, std::uint32_t& height) const {
    width = static_cast<std::uint32_t>(viewportWidth_);
    height = static_cast<std::uint32_t>(viewportHeight_);
}

void cSC43DRender::SetViewportSize(std::uint32_t width, std::uint32_t height) {
    viewportWidth_ = static_cast<int>(width);
    viewportHeight_ = static_cast<int>(height);
    drawFrustumRect_[2] = viewportWidth_;
    drawFrustumRect_[3] = viewportHeight_;
}

void cSC43DRender::SetSubpixelImageBias(float x, float y) {
    subpixelImageBiasX_ = x;
    subpixelImageBiasY_ = y;
}

s3d::cS3DCamera* cSC43DRender::GetCamera() { return camera_; }

void cSC43DRender::SetCamera(s3d::cS3DCamera* camera) {
    // 0x100761720
    if (camera_ == camera) return;
    if (camera) camera->AddRef();
    if (camera_ && camera_->Release() == 0) delete camera_;
    camera_ = camera;
    if (camera_) {
        gd::sGDMode mode{};
        if (mpGDriver) mpGDriver->GetVideoModeInfo(mode);
        camera_->SetDepthSize(65536.0f);
        projectionState_ = static_cast<int>(camera_->GetProjectionState()) - 1;
    }
}

cSC4CameraControl* cSC43DRender::GetCameraControl() { return cameraControl_; }

void cSC43DRender::SetCameraControl(cSC4CameraControl* control) {
    // 0x1007617b0
    if (cameraControl_ == control) return;
    if (control) control->AddRef();
    if (cameraControl_) cameraControl_->Release();
    cameraControl_ = control;
    if (cameraControl_ && viewUtilities_) viewUtilities_->UpdateCameraPosition(cameraControl_);
}

const float* cSC43DRender::GetProjectionMatrixEntries() const { return nullptr; }   // 0x10076a1ba returns 0
const float* cSC43DRender::GetViewMatrixEntries() const { return nullptr; }        // 0x10076a1c2 returns 0

void cSC43DRender::SetClipRect(const cRZRect& rect) {
    // 0x10076160e
    DefaultSettings defaults;
    IRender3DSettings* settings = host_ ? host_->Settings() : nullptr;
    if (!settings) settings = &defaults;
    if (!settings->ClipRectSupported()) return;
    const int left = rect.left;
    const int right = rect.right;
    const int x = left;
    const int y = viewportHeight_ - rect.bottom;
    const int width = right - left;
    const int height = rect.bottom - rect.top;
    if (mpGDriver) mpGDriver->SetViewport(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                          static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);
    if (camera_) camera_->SetSubview(fx, fy, fw, fh);
    if (uiCamera_) uiCamera_->SetSubview(fx, fy, fw, fh);
    cameraView_ = 0;
}

void cSC43DRender::UnsetClipRect() {
    // 0x1007616de
    if (mpGDriver) mpGDriver->SetViewport();   // vslot 0x250
    if (camera_) camera_->ClearSubview();
    if (uiCamera_) uiCamera_->ClearSubview();
    cameraView_ = 0;
}

void cSC43DRender::Set3DCameraView() {
    // 0x100766a22
    if (cameraView_ == 2) return;
    if (drawContext_) drawContext_->SetCamera(camera_);
    cameraView_ = 2;
    if (camera_) viewState_ = camera_->GetViewState();
}

void cSC43DRender::Set2DCameraView() {
    // 0x100766a6a
    if (cameraView_ == 1) return;
    if (drawContext_) drawContext_->SetCamera(uiCamera_);
    cameraView_ = 1;
}

void cSC43DRender::SetUICameraView() {
    // 0x100766aa0
    if (cameraView_ == 3) return;
    if (mpGDriver) {
        mpGDriver->MatrixMode(1);
        mpGDriver->LoadMatrix(projectionMatrix_);
        mpGDriver->MatrixMode(0);
        mpGDriver->LoadIdentity();
    }
    if (drawContext_) drawContext_->SetModelViewTransformChanged();
    cameraView_ = 3;
}

// ---------------------------------------------------------------------------------------------
// Zoom / rotation (all forwarded to the camera control; 0x100768e2e..0x100768e9e)
// ---------------------------------------------------------------------------------------------
void cSC43DRender::SetZoom(int zoom) {
    if (cameraControl_) cameraControl_->SetZoom(static_cast<float>(zoom));
}
void cSC43DRender::ZoomIn() {
    if (cameraControl_) cameraControl_->ZoomIn();
}
void cSC43DRender::ZoomOut() {
    if (cameraControl_) cameraControl_->ZoomOut();
}
void cSC43DRender::SetRotation(int rotation) {
    if (cameraControl_) cameraControl_->SetRotation(rotation);
}
void cSC43DRender::RotateLeft() {
    if (cameraControl_) cameraControl_->RotateLeft();
}
void cSC43DRender::RotateRight() {
    if (cameraControl_) cameraControl_->RotateRight();
}
void cSC43DRender::SetZoomAndRotation(int zoom, int rotation) {
    if (cameraControl_) cameraControl_->SetZoomAndRotation(zoom, rotation);
}

// ---------------------------------------------------------------------------------------------
// View-object lists
// ---------------------------------------------------------------------------------------------
void cSC43DRender::InsertIntoViewObjectList(std::list<ViewObjectInfo>& list, cISC4ViewObject3D* object,
                                           std::uint32_t priority) {
    // 0x10075f3e6: the list stays sorted by priority, so the lowest priority is the first node; equal priorities keep
    // insertion order (the new node goes before the first entry with a strictly higher priority).
    object->AddRef();
    auto position = list.begin();
    while (position != list.end() && position->priority <= priority) ++position;
    list.insert(position, ViewObjectInfo{object, priority});
}

bool cSC43DRender::RemoveFromViewObjectList(std::list<ViewObjectInfo>& list, cISC4ViewObject3D* object) {
    // 0x10075f474
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->object == object) {
            list.erase(it);
            object->Release();
            return true;
        }
    }
    return false;
}

void cSC43DRender::ClearViewObjectList(std::list<ViewObjectInfo>& list) {
    // 0x10075f4ce: Release every object, then clear.
    for (ViewObjectInfo& info : list) {
        if (info.object) info.object->Release();
    }
    list.clear();
}

bool cSC43DRender::AddViewObject(cISC4ViewObject3D* object, int list, std::uint32_t priority) {
    // 0x1007693d0: 0 = foreground (+0x258), 2 = overlays (+0x270), 3 = dynamic (+0x228), 5 = static (+0x240), any
    // other value adds nothing and returns false. The two "static" lists also invalidate the backing store.
    switch (list) {
        case 0:
            InsertIntoViewObjectList(viewObjectLists_[2], object, priority);
            backingStoreValid_ = false;
            return true;
        case 2:
            InsertIntoViewObjectList(viewObjectLists_[3], object, priority);
            backingStoreValid_ = false;
            return true;
        case 3:
            InsertIntoViewObjectList(viewObjectLists_[0], object, priority);
            return true;
        case 5:
            InsertIntoViewObjectList(viewObjectLists_[1], object, priority);
            return true;
        default:
            return false;
    }
}

bool cSC43DRender::RemoveViewObject(cISC4ViewObject3D* object) {
    // 0x100769450
    bool found = false;
    if (backgroundViewObject_ == object) {
        if (object) {
            object->Release();
            backgroundViewObject_ = nullptr;
        }
        found = true;
    }
    if (RemoveFromViewObjectList(viewObjectLists_[1], object)) found = true;
    if (RemoveFromViewObjectList(viewObjectLists_[0], object)) found = true;
    if (RemoveFromViewObjectList(viewObjectLists_[3], object)) {
        backingStoreValid_ = false;
        found = true;
    }
    if (RemoveFromViewObjectList(viewObjectLists_[2], object)) {
        backingStoreValid_ = false;
        found = true;
    }
    return found;
}

bool cSC43DRender::FindViewObject(cISC4ViewObject3D* object) const {
    // 0x100769508
    if (backgroundViewObject_ == object) return true;
    for (const std::list<ViewObjectInfo>* list :
         {&viewObjectLists_[1], &viewObjectLists_[0], &viewObjectLists_[3], &viewObjectLists_[2]}) {
        for (const ViewObjectInfo& info : *list) {
            if (info.object == object) return true;
        }
    }
    return false;
}

cSC43DRender::ViewObjectInfo* cSC43DRender::FindViewObjectInfo(cISC4ViewObject3D* object) {
    for (std::list<ViewObjectInfo>& list : viewObjectLists_) {
        for (ViewObjectInfo& info : list) {
            if (info.object == object) return &info;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// View updates / dirty rectangles
// ---------------------------------------------------------------------------------------------
void cSC43DRender::DoFullViewUpdate(ViewUpdateReason reason) {
    // 0x1007625c0. The original also stores the reason in draw-context +0x20, a field nothing in the port reads yet.
    (void)reason;
    if (captureDirtyRectangles_) return;
    captureDirtyRectangles_ = true;
    backingStoreValid_ = false;
    if (viewUtilities_) viewUtilities_->BuildDrawFrustumDataFromViewportRect(drawFrustumRect_, drawFrustumRect_ + 2);
    captureEnabled_ = false;
    unknown214_ = false;
    ClearDirtyRectangleQueue();
}

void cSC43DRender::ForceFullRedraw() { DoFullViewUpdate(kViewUpdateForceFullRedraw); }   // 0x1007658ae

void cSC43DRender::ClearDirtyRectangleQueue() {
    // 0x100763a56
    if (dirtyRectangles_.empty() && dirtyCount_ == 0 && dirtyAccumulatedArea_ == 0) return;
    for (DirtyRectangleQueueItem& item : dirtyRectangles_) {
        if (item.model) item.model->Release();
    }
    dirtyRectangles_.clear();
    dirtyScratch_.clear();
    dirtyScratch_.shrink_to_fit();
    dirtyCount_ = 0;
    dirtyAccumulatedArea_ = 0;
}

void cSC43DRender::AddDirtyRectangle(int kind, const cRZRect& rect, s3d::cIS3DModelInstance* model, int extra) {
    // 0x100769c44
    if (captureDirtyRectangles_) return;
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    if (rect.left >= viewportWidth_ || rect.top >= viewportHeight_) return;
    cRZRect clipped = rect;
    if (clipped.left < 0) clipped.left = 0;
    if (clipped.top < 0) clipped.top = 0;
    if (clipped.right > viewportWidth_) clipped.right = viewportWidth_;
    if (clipped.bottom > viewportHeight_) clipped.bottom = viewportHeight_;
    AddDirtyRectangleInternal(kind, clipped, model, extra);
}

void cSC43DRender::AddDirtyRectangleInternal(int kind, const cRZRect& rect, s3d::cIS3DModelInstance* model,
                                             int extra) {
    // 0x100769cca. The merge heuristic is kept as decompiled: among the queued rectangles pick the one with the best
    // (negative) cost of merging, merge when the cost is above -2501 or when the queue is already 16 long, and fall
    // back to a full update once the accumulated area passes +0x304.
    if (kind != 0) model = nullptr;
    const int area = (rect.bottom - rect.top) * (rect.right - rect.left);
    const int maxArea = dirtyMaxArea_;

    if (dirtyRectangles_.empty()) {
        unknown324_ = unknown1f8_ - extra;   // +0x324: the stamp of the first rectangle after the last full update
    } else {
        int best = -maxArea;
        DirtyRectangleQueueItem* mergeItem = nullptr;
        int bestSaving = 0;
        unknown324_ -= extra;
        for (DirtyRectangleQueueItem& item : dirtyRectangles_) {
            cRZRect merged;
            merged.left = std::min(rect.left, item.rect.left);
            merged.top = std::min(rect.top, item.rect.top);
            merged.right = std::max(rect.right, item.rect.right);
            merged.bottom = std::max(rect.bottom, item.rect.bottom);
            const int itemArea = (item.rect.bottom - item.rect.top) * (item.rect.right - item.rect.left);
            const int intersection = (merged.bottom - merged.top) * (merged.right - merged.left);
            int cost = (itemArea + area) - intersection;
            if (kind == 0 || item.kind == 0) cost -= std::min(area, itemArea);
            if (best < cost) {
                bestSaving = intersection - itemArea;
                mergeItem = &item;
            }
            if (best <= cost) best = cost;
        }
        if (-2501 < best || dirtyCount_ > 15) {
            const int accumulated = dirtyAccumulatedArea_;
            dirtyAccumulatedArea_ = accumulated + bestSaving;
            if (accumulated + bestSaving < maxArea) {
                cRZRect merged;
                merged.left = std::min(rect.left, mergeItem->rect.left);
                merged.top = std::min(rect.top, mergeItem->rect.top);
                merged.right = std::max(rect.right, mergeItem->rect.right);
                merged.bottom = std::max(rect.bottom, mergeItem->rect.bottom);
                if (mergeItem->model) mergeItem->model->Release();
                mergeItem->model = nullptr;
                if (bestSaving > 0 && dirtyCount_ > 1) {
                    // The merge frees a slot: drop this item and requeue the union as a merged rectangle.
                    const cRZRect mergedCopy = merged;
                    dirtyRectangles_.remove_if([mergeItem](const DirtyRectangleQueueItem& item) {
                        return &item == mergeItem;
                    });
                    --dirtyCount_;
                    AddDirtyRectangleInternal(kViewUpdateDirtyRectMerged, mergedCopy, nullptr, 0);
                    return;
                }
                mergeItem->kind = kViewUpdateDirtyRectMerged;
                mergeItem->rect = merged;
                return;
            }
            DoFullViewUpdate(kViewUpdateDirtyRectOverflow);
            return;
        }
    }

    const int accumulated = dirtyAccumulatedArea_;
    dirtyAccumulatedArea_ = area + accumulated;
    if (area + accumulated < maxArea) {
        if (model) model->AddRef();
        DirtyRectangleQueueItem item;
        item.kind = kind;
        item.rect = rect;
        item.model = model;
        item.stamp = static_cast<std::uint32_t>(changeCounter_);
        dirtyRectangles_.push_front(item);
        ++dirtyCount_;
        return;
    }
    DoFullViewUpdate(kViewUpdateDirtyRectOverflow);
}

bool cSC43DRender::AttemptTranslatedViewUpdate(int dx, int dy) {
    // 0x1007658be (PARTIAL: the blit and the redraw of the two uncovered strips live in DoTranslatedViewUpdate, which
    // is not ported; what is kept is the decision and the fallback path).
    const int limitX = (viewportWidth_ * 14) / 16;
    const int limitY = (viewportHeight_ * 14) / 16;
    const int distanceX = dx < 0 ? -dx : dx;
    const int distanceY = dy < 0 ? -dy : dy;
    if (distanceX < limitX && distanceY < limitY && partialWritesAllowed_ && mbBackingStoreEnabled &&
        backingStoreValid_ && mpBackingStoreExtension) {
        // The original scales the queued rectangles by the translation here, then blits the store.
        return true;
    }
    if (drawContext_) drawContext_->SetRenderPass(3);
    captureDirtyRectangles_ = true;
    backingStoreValid_ = false;
    if (viewUtilities_) viewUtilities_->BuildDrawFrustumDataFromViewportRect(drawFrustumRect_, drawFrustumRect_ + 2);
    captureEnabled_ = false;
    unknown214_ = false;
    ClearDirtyRectangleQueue();
    return false;
}

void cSC43DRender::UpdateViewportRect(const cRZRect& rect) {
    // 0x100764096 (PARTIAL: the static-view redraw at the end needs DrawStaticView)
    if (rect.left >= rect.right || rect.top >= rect.bottom) return;
    if (viewUtilities_)
        viewUtilities_->BuildDrawFrustumDataFromViewportRect(&rect.left, &rect.right);
    captureEnabled_ = false;
    unknown214_ = false;
    SetClipRect(rect);
    ForceFullRedraw();
    Set2DCameraView();
}

void cSC43DRender::TurnOnLighting(bool enable) {
    // 0x100764062
    if (drawContext_) drawContext_->SetLighting(enable && lightingEnabled_);
}

// ---------------------------------------------------------------------------------------------
// Screen shake
// ---------------------------------------------------------------------------------------------
std::uint32_t cSC43DRender::StartScreenShake(float amplitude, float frequency) {
    // 0x100768eb0 builds a description {amplitude, frequency, curveA = {0, amplitude, 0}, curveB = {0, 60, 0},
    // falloff = 1, flags = 0} and forwards it. PARTIAL: the decompiler leaves the frequency stack slot uninitialised,
    // so the port passes its two arguments through (see the pending ledger).
    cSC4ShakeDescription description;
    description.amplitude = amplitude;
    description.frequency = frequency;
    description.curveA = {0.0f, amplitude, 0.0f};
    description.curveB = {0.0f, 60.0f, 0.0f};
    description.falloff = 1.0f;
    cSC43DRender::StartScreenShake(description, nullptr);
    return static_cast<std::uint32_t>(shakeSerial_ - 1);
}

void cSC43DRender::StartScreenShake(const cSC4ShakeDescription& description, const cS3DVector2* position) {
    // 0x100769064
    if (shakes_.empty()) {
        shakeTimer_.Restart();
        unknown3a0_ = 0;
    }
    cSC4Shake shake;
    shake.amplitude = description.amplitude;
    shake.frequency = description.frequency;
    shake.curveA = description.curveA;
    shake.curveB = description.curveB;
    shake.falloff = description.falloff;
    shake.duration = description.falloff;   // the node's +0x10 takes the description's +0x38 (falloff)
    if (position) {
        shake.positionX = position->x;
        shake.positionY = position->y;
    }
    shake.positioned = position != nullptr;
    shake.elapsed = 0;
    shake.serial = static_cast<std::uint32_t>(shakeSerial_++);
    shakes_.push_back(shake);
}

void cSC43DRender::StopScreenShake(std::uint32_t serial) {
    // 0x10076929a: a zero falloff removes the shake, otherwise its duration is shortened to now + the remaining
    // falloff window (the original adds the elapsed clock to the description's two time fields).
    for (auto it = shakes_.begin(); it != shakes_.end(); ++it) {
        if (it->serial != serial) continue;
        if (it->falloff == 0.0f) {
            shakes_.erase(it);
            return;
        }
        float now = 0.0f;
        shakeTimer_.GetElapsedTimeFloat(now);
        now = (it->falloff - it->amplitude) + now;
        if (it->duration <= now) return;
        it->duration = now;
        return;
    }
}

void cSC43DRender::GetShakeOffsets(float& x, float& y) {
    x = static_cast<float>(shakeX_);
    y = static_cast<float>(shakeY_);
}

// ---------------------------------------------------------------------------------------------
// Background picture / backing store
// ---------------------------------------------------------------------------------------------
void cSC43DRender::SetViewportClearColor(const float* color) {
    // 0x1007693a0
    if (!color) return;
    viewportClearColorValid_ = false;
    viewportClearColor_[0] = color[0];
    viewportClearColor_[1] = color[1];
    viewportClearColor_[2] = color[2];
    viewportClearColor_[3] = color[3];
}

void cSC43DRender::EnableBackgroundPicture(bool enable) { backgroundPictureEnabled_ = enable; }   // 0x1007697f6

cIGZBuffer* cSC43DRender::GetBackgroundPicture(bool* visible) const {
    // 0x100769804
    if (visible) *visible = backgroundPictureVisible_;
    return backgroundPicture_;
}

bool cSC43DRender::SetBackgroundPicture(cIGZBuffer* buffer, bool visible) {
    // 0x10076981e (PARTIAL: the texture upload through nsTexUtil::SetupTexture is not ported; the port swaps the
    // buffer and forgets the texture id, so the draw path will re-upload it when it is ported).
    backgroundPictureVisible_ = visible;
    if (backgroundPicture_ == buffer) return true;
    if (backgroundPicture_) backgroundPicture_->Release();
    backgroundPicture_ = nullptr;
    if (buffer) buffer->AddRef();
    if (backgroundTexture_ != 0 && mpGDriver) {
        mpGDriver->DeleteTextures(1, &backgroundTexture_);
        backgroundTexture_ = 0;
    }
    if (!buffer) return false;
    if (!mpGDriver) return false;
    backgroundPicture_ = buffer;
    return true;
}

bool cSC43DRender::SetBackgroundPicture(std::uint32_t texture, int width, int height, bool visible) {
    // 0x10076990a
    backgroundPictureVisible_ = visible;
    if (backgroundTexture_ == texture) return true;
    if (backgroundPicture_) {
        backgroundPicture_->Release();
        backgroundPicture_ = nullptr;
    }
    if (backgroundTexture_ != 0 && mpGDriver) {
        mpGDriver->DeleteTextures(1, &backgroundTexture_);
        backgroundTexture_ = 0;
    }
    backgroundTexture_ = texture;
    backgroundTextureWidth_ = width;
    backgroundTextureHeight_ = height;
    return true;
}

void cSC43DRender::EnableTransparentBackground(bool enable) { transparentBackground_ = enable; }   // 0x100769996

bool cSC43DRender::GetBackingStoreEnabled() { return mbBackingStoreEnabled; }   // 0x1007699a4

void cSC43DRender::SetBackingStoreEnabled(bool enable) {
    // 0x1007699b0: only meaningful when the driver supplied the buffer-region extension.
    const bool wanted = HasBackingStore() && enable;
    mbBackingStoreEnabled = wanted;
}

// ---------------------------------------------------------------------------------------------
// Render system borrow / return
// ---------------------------------------------------------------------------------------------
bool cSC43DRender::BorrowRenderSystem() {
    renderSystemBorrowed_ = true;   // 0x100769ff6
    return true;
}

bool cSC43DRender::ReturnRenderSystem() {
    // 0x10076a006
    renderSystemBorrowed_ = false;
    // The original dereferences the control without a check; the port skips the update when none is attached.
    if (cameraControl_) cameraControl_->UpdateCameraPosition(5);
    const int view = cameraView_;
    cameraView_ = 0;
    if (view == 3)
        SetUICameraView();
    else if (view == 2)
        Set3DCameraView();
    else if (view == 1)
        Set2DCameraView();
    if (drawContext_) drawContext_->SetDefaultRenderStateUnilaterally();
    return true;
}

// ---------------------------------------------------------------------------------------------
// Visibility / query
// ---------------------------------------------------------------------------------------------
bool cSC43DRender::GetModelsInVolume(const cS3DBoundingBox&, s3d::cIS3DModelInstance**, std::uint32_t&) {
    return false;   // 0x100768d5c: the original returns 0 as well
}

bool cSC43DRender::IsModelVisible(s3d::cIS3DModelInstance* model, FrustumType frustum) {
    // 0x100768d64: GetWorldBounds then the matching frustum test (the third argument is the frustum type; 2 = view).
    if (!model || !viewUtilities_) return false;
    cS3DBoundingBox box;
    model->GetWorldBounds(box);
    if (frustum == kFrustumView) return viewUtilities_->IsBoundingBoxInViewFrustum(box.min, box.max);
    return viewUtilities_->IsBoundingBoxInDrawFrustum(box.min, box.max);
}

bool cSC43DRender::IsModelVisible(SC43DGridElement* element, FrustumType frustum) {
    // 0x100768dca: the element wraps the model at +0x20.
    if (!element || !element->model) return false;
    return IsModelVisible(element->model, frustum);
}

// ---------------------------------------------------------------------------------------------
// Fog / lighting switches
// ---------------------------------------------------------------------------------------------
bool cSC43DRender::GetViewFogEnabled() const { return fogEnabled_; }   // 0x100769bce
bool cSC43DRender::SetViewFogEnabled(bool enable) {                   // 0x100769bda
    if (fogEnabled_ != enable) {
        fogEnabled_ = enable;
        ForceFullRedraw();   // vslot +0xa0
    }
    return true;
}
bool cSC43DRender::GetLightingEnabled() const { return lightingEnabled_; }   // 0x100769c02
bool cSC43DRender::SetLightingEnabled(bool enable) {                         // 0x100769c0e
    if (lightingEnabled_ != enable) {
        ForceFullRedraw();   // vslot +0xa0
        lightingEnabled_ = enable;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Stats / map scale / serialization
// ---------------------------------------------------------------------------------------------
void cSC43DRender::GetStatsString1(cIGZString& out) {
    // 0x10076a084; the counters are +0x3c8 / +0x3cc / +0x3d0 / +0x3d4 / +0x3ec / +0x3d8 / +0x3fc of the original.
    out.Sprintf("SPS:%.1f BST:%u BRT:%u VCT:%u PrVDT:%u TeDT:%u PoVDT:%u ToDT:%u", static_cast<double>(statsSps_),
                stats_[0], stats_[1], stats_[2], stats_[3], stats_[9], stats_[4], stats_[13]);
}

void cSC43DRender::GetStatsString2(cIGZString& out) {
    // 0x10076a0f2.
    out.Sprintf("QGT:%u MST:%u MTC:%u, MC1:%u MDC:%u AIT:%u AMT:%u ART:%u TPT%u MPT:%u", stats_[7], stats_[8],
                stats_[10], stats_[11], stats_[12], stats_[14], stats_[15], stats2_[0], stats2_[1], stats2_[2]);
}

void cSC43DRender::SetMapScaleFactor(int factor) { mapScaleFactor_ = factor; }   // 0x10076a176

std::uint32_t cSC43DRender::GetGZCLSID() const { return kCLSID; }   // 0x10076a1ca

bool cSC43DRender::Write(cIGZOStream& stream) const {
    // 0x1007611e2
    StreamWriter out{&stream};
    out.ok = stream.SetSint16(2) && out.ok;
    out.Int(viewportWidth_);
    out.Int(viewportHeight_);
    for (int i = 0; i < 4; ++i) out.Int(drawFrustumRect_[i]);
    SC4WriteGZSerializable(stream, cameraControl_);
    out.Uint(static_cast<std::uint32_t>(projectionState_));
    out.Uint(static_cast<std::uint32_t>(viewState_));
    out.Int(cameraView_);
    return !stream.GetError() && out.ok;
}

bool cSC43DRender::Read(cIGZIStream& stream) {
    // 0x1007612a2
    std::int16_t version = 0;
    if (!stream.GetSint16(version) || version != 2) return false;
    StreamReader in{&stream};
    viewportWidth_ = in.Int();
    viewportHeight_ = in.Int();
    for (int i = 0; i < 4; ++i) drawFrustumRect_[i] = in.Int();
    // The camera control is a nested serializable (class id 0xc9c628ec); a non-DB stream skips it, as in the original.
    void* object = nullptr;
    if (SC4ReadGZSerializable(stream, kCLSIDCameraControl, &object, false) && object) {
        auto* control = static_cast<cSC4CameraControl*>(static_cast<cIGZSerializable*>(object));
        SetCameraControl(control);
        static_cast<cIGZSerializable*>(object)->Release();
    }
    projectionState_ = static_cast<int>(in.Uint());
    viewState_ = static_cast<int>(in.Uint());
    cameraView_ = in.Int();
    return !stream.GetError() && in.ok;
}

// ---------------------------------------------------------------------------------------------
// Model change queue (PARTIAL: the queue and its invalidation are here; the quad-grid work the original does when it
// drains the queue, ProcessModelChangeQueue, is not ported, so the list is only emptied and the models released).
// ---------------------------------------------------------------------------------------------
void cSC43DRender::ClearModelChangeQueue() {
    // 0x10076113a: for each queued model, clear the two "in the grid" flag bits on its grid element and release it.
    // The original also calls RemoveModel for each drained item; that is the part which is not ported (see PARTIAL).
    if (walkingModelChanges_) return;
    walkingModelChanges_ = true;
    for (ModelChangeQueueItem& item : modelChanges_) {
        if (!item.model) continue;
        auto* element = static_cast<SC43DGridElement*>(item.model->GetCustomData());
        if (element) element->flags = static_cast<std::uint16_t>(element->flags & ~0x18);
        item.model->Release();
    }
    modelChanges_.clear();
    walkingModelChanges_ = false;
}

bool cSC43DRender::IsModelInserted(s3d::cIS3DModelInstance* model) {
    // 0x10076180e: the model's grid element (model custom data, vslot 0x28) carries the flags.
    if (!model) return false;
    auto* element = static_cast<SC43DGridElement*>(model->GetCustomData());
    if (!element) return false;
    if ((element->flags & 0x10) != 0) {
        for (const ModelChangeQueueItem& item : modelChanges_) {
            if (item.model == model && item.change == 1) return false;
        }
        // The original drains the queue here once it holds more than eight items (ProcessModelChangeQueue).
    }
    return true;
}

bool cSC43DRender::InsertModel(s3d::cIS3DModelInstance*) {
    // 0x100761e58 (PARTIAL: needs SC43DGridElement creation + WorldToContainerRect + the quad-grid insert)
    return false;
}
bool cSC43DRender::RemoveModel(s3d::cIS3DModelInstance*) {
    // 0x100762628 (PARTIAL)
    return false;
}
bool cSC43DRender::MoveModel(s3d::cIS3DModelInstance*) {
    // 0x10076279c (PARTIAL)
    return false;
}
bool cSC43DRender::ChangeModel(s3d::cIS3DModelInstance*, int) {
    // 0x100762968 (PARTIAL)
    return false;
}

// ---------------------------------------------------------------------------------------------
// Draw path / picking / snapshots (not ported; see the pending ledger file)
// ---------------------------------------------------------------------------------------------
void cSC43DRender::Draw() {}
bool cSC43DRender::TakeSnapshot(cIGZBuffer**, bool) { return false; }
bool cSC43DRender::TakeSnapshot(cIGZString&, bool) { return false; }
void cSC43DRender::ClearViewport(bool) {}
void cSC43DRender::ClearViewportWithSolidColor(const float*, bool, bool) {}
void cSC43DRender::DrawViewportRectangle(const cRZRect&, const float*, bool, bool) {}
void cSC43DRender::DrawTexturedViewportRectangle(const cRZRect&, std::uint32_t, float, bool, bool) {}
bool cSC43DRender::PickTerrain(const float*, const float*, float*, bool, bool) { return false; }
bool cSC43DRender::PickTerrain(int, int, float*, bool, bool) { return false; }
bool cSC43DRender::PickTerrainAlways(const float*, const float*, float*, bool) { return false; }
bool cSC43DRender::PickTerrainAlways(int, int, float*, bool) { return false; }
bool cSC43DRender::Pick(int, int, cISC4OccupantFilter*, s3d::cIS3DModelInstance*&) { return false; }
bool cSC43DRender::Pick(int, int, const float*, const float*, cISC4OccupantFilter*, s3d::cIS3DModelInstance*&) {
    return false;
}

bool cSC43DRender::DoMessage(cIGZMessage2*) {
    // 0x100762b36 (PARTIAL: only the two property / lighting notifications are handled; the camera and city messages
    // need UpdateVisibleTerrainMetrics / DoViewZoomChange3 / DoViewRotationChange2, which need the model queue).
    return true;
}

}  // namespace sc4
