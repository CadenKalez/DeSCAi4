// cSC4View3DWin — see View3DWin.h for the layout, the vtable groups, the seams and the quirks.
#include "sc4/View3DWin.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "gz/CommandParameterSet.h"
#include "rz/cRZMessage2Standard.h"
#include "s3d/ModelInstance.h"

namespace sc4 {

namespace {

IView3DWinHost* g_host = nullptr;

// cRZMessage2ChangeInputControl (vtable 0x100b18710, 0x58 bytes): a cRZMessage2Standard with one extra
// referenced tool at +0x50. Only the part cSC4View3DWin::SendViewInputControlChangeMessage uses is here.
class cRZMessage2ChangeInputControl : public cRZMessage2Standard {
public:
    cRZMessage2ChangeInputControl() = default;                       // @ 0x10004136a + the vtable store
    ~cRZMessage2ChangeInputControl() override { SetNext(nullptr); }   // @ 0x100041906 releases the tool
    void SetNext(cSC4ViewInputControl* next) {
        if (next == next_) return;
        if (next) next->AddRef();
        cSC4ViewInputControl* old = next_;
        next_ = next;
        if (old) old->Release();
    }

private:
    cSC4ViewInputControl* next_ = nullptr;      // +0x50
};

}  // namespace

const std::uint32_t kView3DMessageTypes[kView3DMessageTypeCount] = {
    // Same table, with external linkage (the header declares it).
    0xa6b79602, 0xa6b79612, 0xa6b79621, 0x06cdb65b, 0xaa7fb7e0, 0x4a7fb7e2, 0x4a7fb7e9, 0xabbbca64, 0x26d31ec0,
    0x26d31ec1, 0x26d31ec4, 0x26d31ec5, 0x0ab99380, 0x26c63341, 0x26c63343, 0x26c63344, 0x0772fad4, 0x08dac3e8,
    0x08dac3e7, 0x66956816, 0x66956814, 0x44778231, 0x44778233, 0x26d31ec2, 0xaa5cf1e3, 0xca1b1e64, 0xaa1b2002,
    0x4a44667f, 0xac083d7d, 0x6a935c34, 0x6a935ca4, 0x6a935ca8, 0x6a935cab, 0x6a935cad, 0x6a935cb0, 0x6a935cb2,
    0x6a935cb5, 0x6a935cb7, 0x6a935cba, 0x6a935cbd, 0x6a935cbf, 0x6a935cc2, 0x6a935cc4, 0x6a935cc7, 0x2bcbd5f1,
    0x6a935cc9, 0x6a935ccc, 0x6a935cd5, 0x6a935cd1, 0x6a935cd8, 0x2a9484da, 0x2a948275, 0x6a935cdd, 0x2a9484f2,
    0x2a948272, 0x6a935ce0, 0x2a9484ff, 0x2a94826e, 0x6a935ce2, 0x2a94850b, 0x2a94826b, 0x6a935ce5, 0x6a935ce8,
    0x6a935cf1, 0x6a935cf3, 0x6a935cf4, 0x6a935cf7, 0x6a935d0b, 0x6a935d0e, 0x6a935d11, 0x6a935d17, 0x6a935d1a,
    0x6a935d1d, 0x6a935d25, 0x6a935d2b, 0x6a935d28, 0x6a935d2e, 0x6a935d36, 0x6a935d39, 0xca935dbd, 0x6a935dc0,
    0x6a935dc2, 0x6a935dc5, 0x6b730f78, 0x6a935dc9, 0x6a935dda, 0x2b79e72b, 0x6a935ddd, 0x6a935ddf, 0x2bfe11c9,
    0x6a935de3, 0x6a935de6, 0x6a935deb, 0x6a935dee, 0x6a935df1, 0x6a935df4, 0x2bfe0bea, 0x6a935df7, 0x6a935dfa,
    0x6a935dfd, 0x6a935e0a, 0x6a935e0d, 0x6a935e14, 0x6a935e11, 0x6a935e17, 0x2bfe1095, 0xec3e82f8, 0x6a935e1e,
    0x6a935e21, 0x6a935e27, 0x6a935e2a, 0x6a935e2d, 0x6a935e34, 0x6a935e45, 0x6a935e4b, 0x6a935e36, 0x6a935e37,
    0x6a935e39, 0x6a935e3c, 0x6a935e3f, 0x2a94a04b, 0x2a94a5b6, 0x6aa9fe51, 0x0b980581, 0x0b980587, 0x6b98058a,
    0x6a9391a0, 0x6a9753ea, 0x6a9753ed, 0x6a9757c2, 0xcab22c64, 0xabcdee11};

IView3DWinHost* View3DWinHost() { return g_host; }
void SetView3DWinHost(IView3DWinHost* host) { g_host = host; }

// ==================================================================================================== lifecycle

cSC4View3DWin::cSC4View3DWin(cIGZWinMgr* mgr) : cGZWin(mgr) {
    host_ = g_host;
    if (host_) {
        notifications_ = host_->NotificationHelper();
        if (notifications_) notifications_->AddMessageTypes(kView3DMessageTypes, kView3DMessageTypeCount);
    }
    // The remaining constructor stores are the member initialisers above: the original zeroes
    // +0x168..+0x6aa in this same order and leaves the rest of the GUI blocks at +0x270..+0x4d0
    // (bzero 0xb8 at +0x270, the seven panel vectors at +0x298, the five windows at +0x328, the
    // panel ids at +0x3e4..+0x430) for AddGUI, which is not recovered.
    if (host_) host_->OnView3DCreate(this);
}

cSC4View3DWin::~cSC4View3DWin() {
    // The original's destructor (0x10077f6f6) releases only the objects it owns outright: the mission
    // panel (+0x640), the validation window (+0x648), the coverage helper's destructor (+0x5f8) and the
    // cursor-text / cost-indicator windows the port does not model (+0x5c0..+0x5f0). The camera, the
    // renderer, the model maker, the key accelerator and the progress screen are released by Shutdown,
    // and the two tool vectors are freed without releasing their tools.
    if (missionPanel_) { missionPanel_->Release(); missionPanel_ = nullptr; }
    if (validationWindow_) { validationWindow_->Release(); validationWindow_ = nullptr; }
    if (coverageHelper_) { coverageHelper_->Shutdown(); coverageHelper_ = nullptr; }
    persistentTools_.clear();
    tools_.clear();
    deferredMessages_.clear();
}

bool cSC4View3DWin::QueryInterface(std::uint32_t iid, void** out) {
    void* result = nullptr;
    if (static_cast<std::int32_t>(iid) < 1) {                  // 0x10078145c: the id is compared as signed
        if (iid == kGZIID_cIGZWinMessageFilter) {
            result = static_cast<cIGZWinMessageFilter*>(this);
        } else if (iid != kGZIID_cISC4View3DWin) {
            return false;
        } else {
            result = static_cast<cISC4View3DWin*>(this);
        }
    } else if (iid == 1) {
        result = static_cast<cISC4View3DWin*>(this);
    } else if (iid == kGZIID_cIGZWin) {
        result = static_cast<cIGZWin*>(static_cast<cGZWin*>(this));
    } else if (iid == kGZIID_cIGZMessageTarget2) {
        result = static_cast<cIGZMessageTarget2*>(this);
    } else {
        return false;
    }
    *out = result;
    AddRef();
    return true;
}

// ==================================================================================================== Init

bool cSC4View3DWin::Init() {
    if (cGZWin::get_init_flag()) return initialized_;
    if (notifications_) notifications_->AddAllNotifications(static_cast<cIGZMessageTarget2*>(this));
    if (host_) host_->RegisterCommandTable();
    if (host_ && host_->CheatCodeManagerPresent()) {
        host_->AddCheatCodeTarget(static_cast<cIGZWin*>(static_cast<cGZWin*>(this)));
        for (int i = 0; i < 10; ++i) host_->RegisterCheatCode(kView3DCheatCodes[i], kView3DCheatStrings[i]);
        // 0x10077f5a0: the lot / building plop and toll booth cheats only exist when the app allows them.
        if (host_->LotPlopCheatsEnabled())
            for (int i = 10; i < 13; ++i) host_->RegisterCheatCode(kView3DCheatCodes[i], kView3DCheatStrings[i]);
    }
    initialized_ = cGZWin::Init();
    SetCaption(cRZString("cSC4View3DWin"));                                  // vslot 0x250, 0x10077fe0a
    SetID(kView3DWindowId);                                                 // vslot 0x200
    cIGZWin* main = GetMainWindow();
    SetArea(0, 0, main ? main->GetW() : 0, main ? main->GetH() : 0);        // vslot 0x1b0
    if (cIGZWinMgr* manager = GetWindowManager())
        if (cIGZCursorManager* cursors = manager->GetCursorManager())
            SetCursor(cursors->LoadCursor(kView3DCursorId), false);         // vslot 0x2a8(id, false)
    if (coverageHelper_) coverageHelper_->Init();
    paintEnabled_ = false;                                                  // +0x244
    frameTimer_.Start();
    if (host_) {
        keyAccelerator_ = host_->CreateKeyAccelerator();                     // exemplar 0x42e3ea4b + GZCOM
        CreateProgressScreen(host_->ProgressScreenWanted());                 // @ 0x100780910
    }
    return initialized_;
}

bool cSC4View3DWin::Shutdown() {
    if (!initialized_) return true;
    paintEnabled_ = false;                                                  // +0x244
    if (validationWindow_) {
        if (host_) host_->ShutdownValidationWindow(validationWindow_);
        validationWindow_->Release();
        validationWindow_ = nullptr;
    }
    if (host_ && host_->CheatCodeManagerPresent()) {
        host_->RemoveCheatCodeTarget(static_cast<cIGZWin*>(static_cast<cGZWin*>(this)));
        for (std::uint32_t id : kView3DCheatCodes) host_->UnregisterCheatCode(id);
    }
    RemoveAllViewInputControls(true);
    RemoveAllPersistentViewInputControls();
    if (keyAccelerator_) { keyAccelerator_->Release(); keyAccelerator_ = nullptr; }
    if (cIGZWin* recorder = GetChildWindowFromID(kView3DAnimationRecorderId)) ChildRemove(recorder);
    if (host_) host_->ShutdownLotConfigurationEditors();                    // +0x270 / +0x278
    if (coverageHelper_) coverageHelper_->Shutdown();
    if (host_) host_->RemoveGui();                                          // @ 0x100780eaa
    if (progressScreen_) {
        if (host_) host_->ShutdownProgressScreen(progressScreen_);
        progressScreen_->Release();
        progressScreen_ = nullptr;
    }
    if (host_) host_->UnregisterCommandTable();
    if (notifications_) notifications_->RemoveAllNotifications(static_cast<cIGZMessageTarget2*>(this));
    if (host_) host_->OnView3DShutdown();
    if (windowService_) { windowService_->Release(); windowService_ = nullptr; }
    if (modelMaker_) {
        if (host_) host_->ShutdownModelMaker(modelMaker_);
        modelMaker_->Release();
        modelMaker_ = nullptr;
    }
    if (simulationClock_) {
        if (host_) host_->ShutdownSimulationClock(simulationClock_);
        simulationClock_->Release();
        simulationClock_ = nullptr;
    }
    if (renderer_) { renderer_->Shutdown(); renderer_->Release(); renderer_ = nullptr; }
    if (filterInfo_) { filterInfo_->Release(); filterInfo_ = nullptr; }
    if (cameraControl_) { cameraControl_->Shutdown(); cameraControl_->Release(); cameraControl_ = nullptr; }
    if (cameraObject_) {
        if (host_) host_->ShutdownCameraObject(cameraObject_);
        cameraObject_->Release();
        cameraObject_ = nullptr;
    }
    if (simulatorToPause_) { simulatorToPause_->Release(); simulatorToPause_ = nullptr; }
    initialized_ = false;
    return cGZWin::Shutdown();
}

// ==================================================================================================== accessors

cIGZWin* cSC4View3DWin::AsIGZWin() {
    // The original answers this+0x18, its cGZWin subobject.
    return static_cast<cIGZWin*>(static_cast<cGZWin*>(this));
}

void* cSC4View3DWin::GetRenderer() { return renderer_; }                    // +0x188
cIGZUnknown* cSC4View3DWin::GetModelMaker() { return modelMaker_; }         // +0x190 (400 decimal)
cIGZUnknown* cSC4View3DWin::GetCoverageHelper() { return coverageHelper_; } // +0x5f8

cIGZUnknown* cSC4View3DWin::GetViewManager() {
    if (!viewManagerSource_) return nullptr;
    void* manager = nullptr;
    if (!viewManagerSource_->QueryInterface(kView3DViewManagerIid, &manager)) return nullptr;
    auto* unknown = static_cast<cIGZUnknown*>(manager);
    unknown->AddRef();
    return unknown;
}

void cSC4View3DWin::DisplayTerrainPickDebugString(int, int) {}              // @ 0x100783d74: empty in the original
void cSC4View3DWin::SetErrorReportString(const char*) {}                    // @ 0x100781454: empty in the original
bool cSC4View3DWin::GetTerrainQueryEnabled() { return terrainQueryEnabled_; }   // +0x1ca

std::uint32_t cSC4View3DWin::GetLastSelectedBuilding() {
    return hasLastSelectedBuilding_ ? lastSelectedBuilding_ : 0;            // 0x100793ae0
}
std::uint32_t cSC4View3DWin::GetLastSelectedLot() {
    return hasLastSelectedLot_ ? lastSelectedLot_ : 0;                      // 0x100793afa
}
void cSC4View3DWin::SetLastSelectedBuilding(std::uint32_t building) {
    lastSelectedBuilding_ = building;                                        // 0x100793b14
    hasLastSelectedBuilding_ = true;
}
void cSC4View3DWin::SetLastSelectedLot(std::uint32_t lot) {
    lastSelectedLot_ = lot;                                                  // 0x100793b28
    hasLastSelectedLot_ = true;
}

// ==================================================================================================== camera

bool cSC4View3DWin::SetZoom(int zoom) {
    // 0x100781e1a: clamp to +0x520 / +0x524, then ask the renderer.
    int clamped = zoom;
    if (zoomMin_ > zoom) clamped = zoomMin_;
    else if (zoomMax_ <= zoom) clamped = zoomMax_;
    if (!renderer_) return false;
    const bool changed = renderer_->SetZoom(clamped);
    if (changed) DoTemporarySimHiddenPause();
    return changed;
}

bool cSC4View3DWin::SetZoomLimits(int minZoom, int maxZoom) {
    zoomMin_ = minZoom;
    zoomMax_ = maxZoom;
    if (maxZoom > 4) zoomMax_ = host_ ? host_->VirtualZoomLimit() : 5;
    if (!cameraControl_) return true;
    const int virtualZoom = cameraControl_->GetVirtualZoom();
    // Quirk (0x100781e96): the comparison uses the argument, not the stored +0x524.
    int target = maxZoom;
    if (virtualZoom <= maxZoom) target = virtualZoom;
    if (virtualZoom < minZoom) target = minZoom;
    if (target != virtualZoom) SetZoom(target);
    return true;
}

bool cSC4View3DWin::CanZoomIn() {
    return cameraControl_ && cameraControl_->GetVirtualZoom() < zoomMax_;    // 0x100781ef2
}
bool cSC4View3DWin::CanZoomOut() {
    return cameraControl_ && zoomMin_ < cameraControl_->GetVirtualZoom();    // 0x100781f18
}

bool cSC4View3DWin::SetRotation(int rotation) {
    if (!renderer_) return false;                                           // 0x100781f3e
    const bool changed = renderer_->SetRotation(rotation);
    if (changed) DoTemporarySimHiddenPause();
    return changed;
}
bool cSC4View3DWin::RotateLeft() {
    if (!renderer_) return false;                                           // 0x100781f6e
    const bool changed = renderer_->RotateLeft();
    if (changed) DoTemporarySimHiddenPause();
    return changed;
}
bool cSC4View3DWin::RotateRight() {
    if (!renderer_) return false;                                           // 0x100781f9e
    const bool changed = renderer_->RotateRight();
    if (changed) DoTemporarySimHiddenPause();
    return changed;
}
bool cSC4View3DWin::ZoomIn() {
    if (!cameraControl_ || !renderer_) return false;                        // 0x100781fce
    if (cameraControl_->GetVirtualZoom() >= zoomMax_) return false;
    if (!renderer_->ZoomIn()) return false;
    DoTemporarySimHiddenPause();
    return true;
}
bool cSC4View3DWin::ZoomOut() {
    if (!cameraControl_ || !renderer_) return false;                        // 0x100782014
    if (zoomMin_ >= cameraControl_->GetVirtualZoom()) return false;
    if (!renderer_->ZoomOut()) return false;
    DoTemporarySimHiddenPause();
    return true;
}
bool cSC4View3DWin::SetZoomAndRotation(int zoom, int rotation) {
    if (!renderer_) return false;                                           // 0x10078205a
    int clamped = zoom;
    if (zoomMin_ > zoom) clamped = zoomMin_;
    else if (zoomMax_ <= zoom) clamped = zoomMax_;
    const bool changed = renderer_->SetZoomAndRotation(clamped, rotation);   // only the zoom is clamped
    if (changed) DoTemporarySimHiddenPause();
    return changed;
}

cIGZWinKeyAccelerator* cSC4View3DWin::GetKeyAccelerator() const { return keyAccelerator_; }   // 0x100781ab2

void cSC4View3DWin::SetKeyAccelerator(cIGZWinKeyAccelerator* accelerator) {   // 0x100781ac0
    cIGZWinKeyAccelerator* old = keyAccelerator_;
    if (old == accelerator) return;
    if (accelerator) accelerator->AddRef();
    if (old) old->Release();
    keyAccelerator_ = accelerator;
}

// ==================================================================================================== scrolling

void cSC4View3DWin::SetScrolling(bool scrolling, float angle, float speed) {   // 0x100781b06
    if (!scrolling_ && scrolling) DoScrollBegin();
    else if (scrolling_ && !scrolling) DoScrollEnd();
    scrolling_ = scrolling;
    scrollAngle_ = angle;
    scrollSpeed_ = speed;
    scrollStopping_ = false;
}

bool cSC4View3DWin::ScrollStop() {                                          // 0x100781cb4
    if (!scrolling_) return true;
    scrollStopping_ = true;
    float speed = scrollStopFactor_;
    if (scrollStopScaled_) speed *= scrollSpeed_;
    scrollStopSpeed_ = speed;
    return true;
}

void cSC4View3DWin::KillKeyboardScrolling() {                               // 0x100781cee
    keyboardScrollX_ = 0;
    keyboardScrollY_ = 0;
    RecalcScrollVelocity();
}

void cSC4View3DWin::RecalcScrollVelocity() {                                // 0x100781d04
    const int keyboardX = keyboardScrollX_;
    int x = edgeScrollX_;
    int y = edgeScrollY_;
    const bool keyboard = (keyboardX != 0) || (keyboardScrollY_ != 0);
    const int zoom = cameraControl_ ? cameraControl_->Zoom() : 0;
    float speedScale = 0.0f;
    if (cameraControl_) speedScale = std::pow(kScrollSpeedBase, static_cast<float>(zoom));
    if (host_) {
        if (const std::uint8_t* preferences = host_->Preferences())
            if (preferences[kPrefsEdgeScrollEnabled]) edgeScrollX_ = 0, edgeScrollY_ = 0;
    }
    if (keyboardX != 0) x = keyboardX;
    if (keyboardScrollY_ != 0) y = keyboardScrollY_;
    if (x != 0 || y != 0) {
        const float speed = keyboard ? keyboardScrollSpeed_ : edgeScrollSpeed_;
        const float angle = kScrollDirectionAngle[y + 1][x + 1];
        SetScrolling(true, angle, speedScale * speed);
        return;
    }
    if (!scrollStopping_) ScrollStop();
}

void cSC4View3DWin::DoScrollBegin() {                                       // 0x100781b86
    if (host_) host_->PostGameMessage(kMsgViewScrollBegin);
    cSC4RenderProperties* global = host_ ? host_->GlobalRenderProperties() : nullptr;
    if (!global || !global->BoolValue(kPropNoPartialBackingStoreCopies)) return;
    cSC4RenderProperties* app = host_ ? host_->AppRenderProperties() : nullptr;
    if (app && app != &renderProperties_) {
        renderProperties_ = *app;   // +0x540 := the live properties (the snapshot DoScrollEnd restores)
        renderPropertiesAux_ = 0;   // the dword at the source's +0x10 is not modelled by the port's class
    }
    // The switches are turned off on the *live* object (0x100781bf4 onwards), not on the snapshot.
    if (app) {
        app->SetBoolValue(kPropRenderShadows, false);
        app->SetBoolValue(kPropRenderDecals, false);
        app->SetBoolValue(kPropRenderAutomata, false);
        app->SetBoolValue(kPropRenderParticleEffects, false);
        app->SetFloatValue(kPropRenderModelThreshold, app->FloatValue(kPropRenderModelThresholdScroll));
    }
    if (host_) host_->SetScrollLightingHighlight();
}

void cSC4View3DWin::DoScrollEnd() {                                         // 0x100781908
    if (host_) host_->PostGameMessage(kMsgViewScrollEnd);
    cSC4RenderProperties* global = host_ ? host_->GlobalRenderProperties() : nullptr;
    if (!global || !global->BoolValue(kPropNoPartialBackingStoreCopies)) return;
    if (global != &renderProperties_) {
        renderPropertiesAux_ = 0;
        *global = renderProperties_;
    }
    if (host_) host_->RestoreLighting();
    if (renderer_) renderer_->ForceFullRedraw();
}

void cSC4View3DWin::DoTemporarySimHiddenPause() {                           // 0x1007819e0
    if (!simHiddenPaused_ && simulatorToPause_) {
        simHiddenPaused_ = true;
        simulatorToPause_->Pause();
    }
    simHiddenTimer_.Restart();
}

void cSC4View3DWin::DoTemporarySimHiddenResume() {                          // 0x100781a20
    if (simHiddenPaused_) {
        simHiddenPaused_ = false;
        if (simulatorToPause_) simulatorToPause_->Resume();
    }
    simHiddenTimer_.Reset();
}

// ==================================================================================================== view information

void cSC4View3DWin::GetCurrentViewFilterInfo(cSC4ViewFilterInformation& out) {   // 0x1007820a4
    out.filter = filterInfo_;
    if (filterInfo_) filterInfo_->AddRef();
}

bool cSC4View3DWin::SetCurrentViewFilterInfo(cSC4ViewFilterInformation& info) {   // 0x1007820ba
    cIGZUnknown* saved = filterInfo_;
    if (saved) saved->AddRef();
    if (cIGZMessageServer2* server = host_ ? host_->MessageServer2() : nullptr) {
        auto* message = new cRZMessage2Standard();
        message->AddRef();
        message->SetType(kMsgViewFilterInfo);
        message->SetVoid1(&saved);
        message->SetVoid2(&info);
        server->MessageSend(message);
        message->Release();
    }
    cIGZUnknown* next = info.filter;
    if (filterInfo_ != next) {
        if (next) next->AddRef();
        if (filterInfo_) filterInfo_->Release();
        filterInfo_ = next;
    }
    if (cIGZMessageServer2* server = host_ ? host_->MessageServer2() : nullptr) {
        auto* message = new cRZMessage2Standard();
        message->AddRef();
        message->SetType(kMsgViewFilterInfoDone);
        message->SetVoid1(&saved);
        message->SetVoid2(&info);
        server->MessageSend(message);
        message->Release();
    }
    if (saved) saved->Release();
    return true;
}

void cSC4View3DWin::GetCurrentViewLevelInfo(cSC4ViewLevelInformation& out) {   // 0x100782230
    out.level = viewLevel_;
}

void cSC4View3DWin::SetCurrentViewLevelInfo(cSC4ViewLevelInformation& info) {  // 0x10078223e
    const std::uint32_t previous = viewLevel_;
    if (previous == info.level) return;
    viewLevel_ = info.level;
    if (cIGZMessageServer2* server = host_ ? host_->MessageServer2() : nullptr) {
        std::uint32_t oldLevel = previous;
        std::uint32_t newLevel = info.level;
        for (std::uint32_t type : {kMsgViewLevelInfo, kMsgViewLevelInfoDone}) {
            auto* message = new cRZMessage2Standard();
            message->AddRef();
            message->SetType(type);
            message->SetVoid1(&oldLevel);
            message->SetVoid2(&newLevel);
            server->MessageSend(message);
            message->Release();
        }
    }
}

std::uint32_t cSC4View3DWin::GetCurrentViewLevel() { return viewLevel_; }   // 0x10078235c

void cSC4View3DWin::SetCurrentViewLevel(std::uint32_t level) {              // 0x100782368
    if (viewLevel_ == level) return;
    cSC4ViewLevelInformation info;
    info.level = level;
    SetCurrentViewLevelInfo(info);
}

// ==================================================================================================== tool stack

cSC4ViewInputControl* cSC4View3DWin::GetCurrentViewInputControl() {         // 0x100782390
    return tools_.empty() ? nullptr : tools_.back();
}

bool cSC4View3DWin::SetCurrentViewInputControl(cSC4ViewInputControl* control, int operation) {   // 0x1007823ae
    if (control) control->SetWindow(static_cast<cIGZWin*>(static_cast<cGZWin*>(this)));
    cSC4ViewInputControl* previous = GetCurrentViewInputControl();
    if (previous) previous->AddRef();
    SendViewInputControlChangeMessage(previous, control);
    if (operation == 2) RemoveAllViewInputControls(false);
    else if (operation == 1) RemoveCurrentViewInputControl(false);
    cSC4ViewInputControl* current = GetCurrentViewInputControl();
    cSC4ViewInputControl* displaced = previous;
    if (previous != current) {
        if (current) current->AddRef();
        displaced = current;
        if (previous) previous->Release();
    }
    if (displaced) {
        displaced->OnMouseExit();       // 0xb0: the tool that was on top stops seeing the mouse
        displaced->Deactivate();        // 0xc0
    }
    if (control && control->Init()) {
        control->AddRef();
        tools_.push_back(control);
        control->Activate();            // 0xb8
        if (cIGZCursor* cursor = control->GetCursor()) SetCursor(cursor, true);
    }
    if (displaced) displaced->Release();
    return true;
}

void cSC4View3DWin::SendViewInputControlChangeMessage(cSC4ViewInputControl* previous,
                                                     cSC4ViewInputControl* next) {   // 0x100782556
    cIGZMessageServer2* server = host_ ? host_->MessageServer2() : nullptr;
    if (!server) return;
    auto* message = new cRZMessage2ChangeInputControl();
    message->AddRef();
    message->SetType(kMsgViewInputControlChanged);
    message->SetVoid1(previous);
    message->SetIGZUnknown(previous);
    message->SetVoid2(next);
    message->SetNext(next);             // the message's own +0x50 reference
    server->MessagePost(message, false);
    message->Release();
}

bool cSC4View3DWin::RemoveCurrentViewInputControl(bool keepCursor) {        // 0x100782660
    cSC4ViewInputControl* control = GetCurrentViewInputControl();
    if (!control) return false;
    control->AddRef();
    GZReleaseCapture();
    tools_.pop_back();                  // the reference the vector held is released by the two Releases below
    control->Deactivate();
    control->Shutdown();
    control->Release();
    if (!keepCursor) {
        cSC4ViewInputControl* next = GetCurrentViewInputControl();
        if (!next) {
            if (cIGZWinMgr* manager = GetWindowManager())
                if (cIGZCursorManager* cursors = manager->GetCursorManager())
                    if (cIGZCursor* cursor = cursors->LoadCursor(kView3DCursorId)) SetCursor(cursor, true);
        } else {
            next->AddRef();
            SetCursor(next->GetCursor(), true);
            SendViewInputControlChangeMessage(control, next);
            next->Release();
        }
    }
    control->Release();
    return true;
}

bool cSC4View3DWin::RemoveAllViewInputControls(bool keepCursor) {           // 0x100782790
    while (RemoveCurrentViewInputControl(keepCursor)) {}
    return false;                       // the original returns 0
}

bool cSC4View3DWin::AddPersistentViewInputControl(cSC4ViewInputControl* control) {   // 0x1007827b8
    if (control && control->Init()) {
        control->SetWindow(static_cast<cIGZWin*>(static_cast<cGZWin*>(this)));
        control->AddRef();
        persistentTools_.push_back(control);
        control->Activate();
    }
    return true;
}

bool cSC4View3DWin::RemovePersistentViewInputControl(cSC4ViewInputControl* control) {   // 0x10078283e
    bool removed = false;
    for (std::size_t i = 0; i < persistentTools_.size();) {
        if (persistentTools_[i] != control) {
            ++i;
            continue;
        }
        if (control) {
            control->Deactivate();
            control->Shutdown();
            control->Release();
        }
        persistentTools_.erase(persistentTools_.begin() + static_cast<std::ptrdiff_t>(i));
        removed = true;
    }
    return removed;
}

bool cSC4View3DWin::RemoveAllPersistentViewInputControls() {                // 0x1007828c6
    while (!persistentTools_.empty()) {
        cSC4ViewInputControl* control = persistentTools_.back();
        if (control) {
            control->Deactivate();
            control->Shutdown();
            control->Release();
        }
        persistentTools_.pop_back();
    }
    return true;
}

// ==================================================================================================== dispatch

bool cSC4View3DWin::GZOnCharacter(char c) {                                 // 0x1007829f8
    for (std::size_t i = tools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = tools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnCharacter(c);
        if (control) control->Release();
        if (handled) return true;
        if (i == 0) break;
    }
    const std::size_t count = persistentTools_.size();
    if (count == 0) return false;
    bool result = false;
    for (std::size_t index = count; index-- > 0;) {
        // Quirk (0x100782ab8): the bound is the *current* stack's size.
        if (tools_.size() <= index) return result;
        cSC4ViewInputControl* control = persistentTools_[index];
        if (control) control->AddRef();
        const bool handled = control && control->OnCharacter(c);
        if (control) control->Release();
        if (handled) result = true;
    }
    return result;
}

bool cSC4View3DWin::ProcessKeyDown(std::uint32_t key, std::uint32_t modifiers) {   // 0x100782ba2
    lastMessageModifiers_ = modifiers;
    bool result = false;
    for (std::size_t i = tools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = tools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnKeyDown(key, modifiers);
        if (control) control->Release();
        if (handled) { result = true; break; }
        if (i == 0) break;
    }
    bool persistentResult = result;
    for (std::size_t i = persistentTools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = persistentTools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnKeyDown(key, modifiers);
        if (control) control->Release();
        persistentResult = handled ? true : persistentResult;
        if (i == 0) break;
    }
    return (persistentResult & 0x101) != 0;
}

bool cSC4View3DWin::ProcessKeyUp(std::uint32_t key, std::uint32_t modifiers) {   // 0x100782d20
    lastMessageModifiers_ = modifiers;
    bool result = false;
    for (std::size_t i = tools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = tools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnKeyUp(key, modifiers);
        if (control) control->Release();
        if (handled) { result = true; break; }
        if (i == 0) break;
    }
    bool persistentResult = result;
    for (std::size_t i = persistentTools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = persistentTools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnKeyUp(key, modifiers);
        if (control) control->Release();
        persistentResult = handled ? true : persistentResult;
        if (i == 0) break;
    }
    return (persistentResult & 0x101) != 0;
}

bool cSC4View3DWin::GZOnMouseWheel(int x, int y, std::uint32_t modifiers, int delta) {   // 0x100783ae2
    lastMessageModifiers_ = modifiers;
    for (std::size_t i = tools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = tools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnMouseWheel(x, y, modifiers, delta);
        if (control) control->Release();
        if (handled) return true;
        if (i == 0) break;
    }
    for (std::size_t i = persistentTools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = persistentTools_[i];
        if (control) control->AddRef();
        if (control) control->OnMouseWheel(x, y, modifiers, delta);   // the result is ignored
        if (control) control->Release();
        if (i == 0) break;
    }
    if (delta < 1) ZoomOut();
    else ZoomIn();
    return true;
}

bool cSC4View3DWin::GZOnMouseExit(cIGZWin*) {                               // 0x100783c7a
    edgeScrollX_ = 0;
    edgeScrollY_ = 0;
    RecalcScrollVelocity();
    if (host_) host_->UpdateCursorTextPositions(-10000, -10000);
    if (!tools_.empty()) {
        cSC4ViewInputControl* control = tools_.back();
        if (control) { control->AddRef(); control->OnMouseExit(); control->Release(); }
    }
    for (std::size_t i = persistentTools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = persistentTools_[i];
        if (control) control->AddRef();
        if (control) control->OnMouseExit();
        if (control) control->Release();
        if (i == 0) break;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------- the mouse
// The four button handlers and the move handler route the point through the city's mouse router
// (SL::spCity vslot 0x290) first, then offer it to the persistent tools and the input-tool stack, and
// finally do the window's own work (scrolling, cursor text, camera target).

bool cSC4View3DWin::GZOnMouseDownL(int x, int y, std::uint32_t modifiers) {   // 0x100782e9e
    lastMessageModifiers_ = modifiers;
    if (host_) {
        if (ICityMouseRouter* router = host_->CityMouseRouter()) {
            if (router->IsActive() && !router->ProcessMouse(x, y, 0xffffffffu)) return true;
        }
    }
    for (std::size_t i = persistentTools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = persistentTools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnMouseDownL(x, y, modifiers);
        if (control) control->Release();
        if (handled) return true;
    }
    for (std::size_t i = tools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = tools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnMouseDownL(x, y, modifiers);
        if (control) control->Release();
        if (i < 1) return true;           // quirk (0x100783022): the bottom tool's answer is ignored
        if (handled) return true;
    }
    return true;
}

bool cSC4View3DWin::GZOnMouseDownR(int x, int y, std::uint32_t modifiers) {   // 0x100783068
    lastMessageModifiers_ = modifiers;
    GZSetCapture();
    for (std::size_t i = persistentTools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = persistentTools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnMouseDownR(x, y, modifiers);
        if (control) control->Release();
        if (handled) return true;
    }
    for (std::size_t i = tools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = tools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnMouseDownR(x, y, modifiers);
        if (control) control->Release();
        if (handled) return true;
    }
    mouseDownX_ = x;
    mouseDownY_ = y;
    draggedPastDeadZone_ = false;
    if (host_) {
        // new cSC4ViewInputControlCityScroll @ 0x1002c13a2, then SetMouseScroll @ 0x1002c1930.
        if (cSC4ViewInputControl* tool = host_->CreateCityScrollTool()) {
            tool->AddRef();
            SetCurrentViewInputControl(tool, 0);
            host_->CityScrollSetMouseScroll(tool, x, y);
            tool->Release();
        }
    }
    return true;
}

bool cSC4View3DWin::GZOnMouseUpL(int x, int y, std::uint32_t modifiers) {   // 0x100783268
    lastMessageModifiers_ = modifiers;
    cSC4ViewInputControl* current = GetCurrentViewInputControl();
    if (GZAmCapturing() && current && !current->IsSelfScrollingView() && current->AmCapturing())
        RecalcScrollVelocity();
    if (host_) {
        if (ICityMouseRouter* router = host_->CityMouseRouter()) {
            if (router->IsActive() && !router->ProcessMouse(x, y, 1)) return true;
        }
    }
    for (std::size_t i = persistentTools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = persistentTools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnMouseUpL(x, y, modifiers);
        if (control) control->Release();
        if (handled) return true;
    }
    for (std::size_t i = tools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = tools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnMouseUpL(x, y, modifiers);
        if (control) control->Release();
        if (i < 1) return true;           // quirk (0x1007833f8): the bottom tool's answer is ignored
        if (handled) return true;
    }
    return true;
}

bool cSC4View3DWin::GZOnMouseUpR(int x, int y, std::uint32_t modifiers) {   // 0x100783474
    lastMessageModifiers_ = modifiers;
    if (GZAmCapturing()) GZReleaseCapture();
    if ((modifiers & 7) == 0 && !draggedPastDeadZone_) {
        cS3DVector3 terrainPoint;                       // nS3D::k_zero, k_zero, k_zero
        s3d::cIS3DModelInstance* model = nullptr;
        if (renderer_ && renderer_->Pick(x, y, nullptr, model)) {
            cS3DBoundingBox box;                        // the original's constant: {1,1,1}, {0,0,0}
            box.min[0] = box.min[1] = box.min[2] = 1.0f;
            box.max[0] = box.max[1] = box.max[2] = 0.0f;
            if (model) model->GetWorldBounds(box);
            const float centreX = (box.min[0] + box.max[0]) * 0.5f;
            const float centreZ = (box.min[2] + box.max[2]) * 0.5f;
            // Quirk (0x100783561 / 0x100783574): both terrain heights are queried and both answers dropped.
            if (ITerrainEditor* terrain = host_ ? host_->TerrainEditor() : nullptr) {
                terrain->TerrainHeight(centreX, centreZ);
                terrain->TerrainHeightAt(centreX, centreZ);
            }
            const cS3DVector3 target{(box.min[0] + box.max[0]) * 0.5f,
                                     (box.min[1] + box.max[1]) * 0.5f,
                                     (box.min[2] + box.max[2]) * 0.5f};
            if (cameraControl_) cameraControl_->SetViewTargetPosition(target);
        } else if (renderer_ && renderer_->PickTerrain(x, y, &terrainPoint.x, false, true)) {
            if (cameraControl_) cameraControl_->SetViewTargetPosition(terrainPoint);
        }
        if (model) model->Release();
    }
    for (std::size_t i = persistentTools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = persistentTools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnMouseUpR(x, y, modifiers);
        if (control) control->Release();
        if (handled) return true;
    }
    for (std::size_t i = tools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = tools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnMouseUpR(x, y, modifiers);
        if (control) control->Release();
        if (i < 1) return true;
        if (handled) return true;
    }
    return true;
}

bool cSC4View3DWin::GZOnMouseMove(int x, int y, std::uint32_t modifiers) {   // 0x100783786
    lastMessageModifiers_ = modifiers;
    bool allowEdgeScroll = true;
    if (host_) {
        if (ICityMouseRouter* router = host_->CityMouseRouter()) {
            if (router->IsActive()) {
                if (!router->ProcessMouse(x, y, 0)) return true;
                allowEdgeScroll = router->AllowsEdgeScroll();
            }
        }
    }
    const int dx = mouseDownX_ - x;
    if (std::abs(dx) >= 4) {
        draggedPastDeadZone_ = true;
    } else {
        const int dy = mouseDownY_ - y;
        if (std::abs(dy) >= 4) draggedPastDeadZone_ = true;
    }
    UpdateCursorTextPositions(x, y);
    for (std::size_t i = persistentTools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = persistentTools_[i];
        if (control) control->AddRef();
        if (control) control->OnMouseMove(x, y, modifiers);       // the result is ignored
        if (control) control->Release();
        if (i < 1) break;
    }
    for (std::size_t i = tools_.size(); i-- > 0;) {
        cSC4ViewInputControl* control = tools_[i];
        if (control) control->AddRef();
        const bool handled = control && control->OnMouseMove(x, y, modifiers);
        if (control) control->Release();
        if (i < 1 || handled) break;
    }
    cSC4ViewInputControl* current = GetCurrentViewInputControl();
    bool updateEdgeScroll = true;
    if (allowEdgeScroll && host_ && host_->Preferences()[kPrefsEdgeScrollEnabled]) {
        if (!current || !current->IsSelfScrollingView()) {
            int edgeX = -1;
            if (x > 1) edgeX = (GetW() - 1 <= x) ? 1 : 0;
            edgeScrollX_ = edgeX;
            int edgeY = -1;
            if (y > 1) edgeY = (GetH() - 1 <= y) ? 1 : 0;
            edgeScrollY_ = edgeY;
            updateEdgeScroll = false;
        } else {
            if (host_->GraphicsSystemAllowsSelfScroll()) return true;
            if (edgeScrollX_ == 0 && edgeScrollY_ == 0) return true;
            updateEdgeScroll = false;
        }
    }
    if (updateEdgeScroll) {
        edgeScrollX_ = 0;
        edgeScrollY_ = 0;
    }
    RecalcScrollVelocity();
    return true;
}

bool cSC4View3DWin::GZOnKeyDown(std::uint32_t, std::uint32_t) { return false; }   // 0x100782b82
bool cSC4View3DWin::GZOnKeyUp(std::uint32_t, std::uint32_t) { return false; }     // 0x100782b92
bool cSC4View3DWin::GZOnCommand(std::uint32_t command, std::uint64_t param) {     // 0x10078abdc
    return cGZWin::GZOnCommand(command, param);
}

// ==================================================================================================== picking

bool cSC4View3DWin::PickTerrain(int x, int y, float* out, bool arg4) {      // 0x10078292c
    if (!renderer_) {
        // Quirk (0x100782952): two words, not three.
        out[0] = 0.0f;
        out[1] = 0.0f;
        return false;
    }
    return renderer_->PickTerrain(x, y, out, false, arg4);
}

bool cSC4View3DWin::PickOccupant(int x, int y, cISC4Occupant*& out) {       // 0x100782962
    out = nullptr;
    if (!renderer_) return false;
    s3d::cIS3DModelInstance* model = nullptr;
    if (!renderer_->Pick(x, y, nullptr, model)) return false;
    bool found = false;
    if (model) {
        if (cIGZUnknown* owner = model->GetOwner()) {
            void* occupant = nullptr;
            if (owner->QueryInterface(0x26bb767e, &occupant)) {
                out = reinterpret_cast<cISC4Occupant*>(occupant);
                found = true;
            }
        }
        model->Release();
    }
    return found;
}

// ==================================================================================================== cursor text

void cSC4View3DWin::UpdateCursorTextPositions(int x, int y) {               // 0x100783a60
    if (cursorTextWindow_) {
        // The original only repositions the window while it is visible (0x100783a70..0x100783aaa).
        if (cursorTextWindow_->IsVisible()) cursorTextWindow_->SetPosition(x, y);
    }
    cursorTextX_ = x;                       // +0x5b0
    cursorTextY_ = y;                       // +0x5b4
    UpdateCostIndicator(0.0f);              // the cost indicator follows the cursor too
}

void cSC4View3DWin::UpdateCostIndicator(float dt) {                         // 0x10078178a (PARTIAL)
    // The cost-indicator windows (+0x5c0 bitmap / +0x5c8 text) are not recovered, so the whole body is
    // still the host's; see notes/ledger_pending/cSC4View3DWin.txt.
    if (host_) host_->UpdateCostIndicator(dt);
}

void cSC4View3DWin::CursorTextChanged() {                                   // 0x10079155e
    if (!cursorTextWindow_) return;
    if (!cursorText_.empty()) {
        const CursorTextInfo& info = cursorText_.back();   // the original reads the sentinel's back link
        cursorTextWindow_->SetLine1(info.line1);
        cursorTextWindow_->SetLine2(info.line2);
        cursorTextWindow_->SetIcon(info.icon == 1 ? cursorTextIcon_ : nullptr);
        if (!cursorTextWindow_->IsVisible()) {
            int x = 0;
            int y = 0;
            if (cIGZWinMgr* manager = GetWindowManager())
                manager->GetCursorPos(static_cast<cIGZWin*>(static_cast<cGZWin*>(this)), x, y);
            cursorTextWindow_->SetPosition(x, y);
            cursorTextWindow_->ShowWindow();
        }
        return;
    }
    cursorTextWindow_->HideWindow();
    cursorTextWindow_->Reset();
}

void cSC4View3DWin::ClearCursorText(std::uint32_t id) {                     // 0x100791772
    for (auto it = cursorText_.rbegin(); it != cursorText_.rend(); ++it) {
        if (it->key != id) continue;
        cursorText_.erase(std::next(it).base());
        CursorTextChanged();
        return;
    }
    CursorTextChanged();
}

void cSC4View3DWin::SetCursorTextPosition3D(const cS3DVector3& position) {  // 0x100791cc4
    cS3DVector3 projected = position;
    if (!host_ || !host_->ProjectPointToViewport(projected)) return;
    UpdateCursorTextPositions(static_cast<int>(projected.x), static_cast<int>(projected.y));
}

void cSC4View3DWin::SetCursorText(std::uint32_t id, std::uint32_t order, const cIGZString* line1,
                                  const cIGZString* line2, int icon) {    // 0x10079132c
    const bool hasLine1 = line1 && line1->Strlen() != 0;
    const bool hasLine2 = line2 && line2->Strlen() != 0;
    if (!hasLine1 && !hasLine2) {
        ClearCursorText(id);
        CursorTextChanged();                // quirk (0x1007913c6): ClearCursorText already called it
        return;
    }
    auto found = cursorText_.end();
    for (auto it = cursorText_.rbegin(); it != cursorText_.rend(); ++it) {
        if (it->key != id) continue;
        found = std::next(it).base();
        break;
    }
    if (found != cursorText_.end()) {
        if (line1) found->line1.AssignFromIGZString(*line1);
        else found->line1.Erase(0, 0xffffffffu);
        // Quirk (0x10079144a): a missing second line erases the *first* string.
        if (line2) found->line2.AssignFromIGZString(*line2);
        else found->line1.Erase(0, 0xffffffffu);
        found->icon = icon;
    } else {
        // The insert scans back from the last node and inserts before the first whose order is <= this
        // one (0x10079140a); when every node has a larger order it appends at the end, which is the
        // original's behaviour (0x100791419 inserts before the sentinel).
        auto position = cursorText_.end();
        for (auto it = cursorText_.end(); it != cursorText_.begin();) {
            --it;
            if (it->order <= order) { position = it; break; }
        }
        CursorTextInfo info;
        info.key = id;
        info.order = order;
        if (line1) info.line1.AssignFromIGZString(*line1);
        if (line2) info.line2.AssignFromIGZString(*line2);
        info.icon = icon;
        cursorText_.insert(position, info);
    }
    CursorTextChanged();
}

// ==================================================================================================== panels

void cSC4View3DWin::HideMissionStatusPanel() {                              // 0x100792e7e
    if (!missionPanel_) return;
    ChildDelete(missionPanel_);
    cIGZWin* panel = missionPanel_;
    missionPanel_ = nullptr;
    if (panel) panel->Release();
}

void cSC4View3DWin::CloseSecondaryMenu() {                                  // 0x100791d18
    cIGZWin* menu = secondaryMenu_;
    if (!menu) return;
    secondaryMenu_ = nullptr;
    secondaryMenuState_ = 0;
    CloseTertiaryMenu();
    menu->HideWindow();
    // The original Releases before it ChildDeletes (0x100791d52..0x100791d60); the window manager owns
    // a reference there, while the port has to destroy it first or the pointer dangles.
    ChildDelete(menu);
    menu->Release();
}

void cSC4View3DWin::CreateProgressScreen(bool flag) {                       // 0x100780910
    if (!host_) return;
    // new cSC4ProgressScreen(GetArea(), flag) @ 0x10032cecc: the host owns the class, the view holds one
    // reference and initialises it (0x10032d23e).
    progressScreen_ = host_->CreateProgressScreen(static_cast<cIGZWin*>(static_cast<cGZWin*>(this)),
                                                 GetArea(), flag);
    if (!progressScreen_) return;
    progressScreen_->AddRef();
    host_->InitProgressScreen(progressScreen_);
}

void cSC4View3DWin::UpdateProgressMessage() {                               // 0x100788d98
    if (!progressScreen_ || !host_) return;
    cRZString empty;                        // the original passes a default-constructed string
    host_->SetProgressCaption(progressScreen_, 1, empty);
}

void cSC4View3DWin::AddWinAnimationRecorder(bool enable) {                  // 0x100780d78
    cIGZWin* existing = GetChildWindowFromID(kView3DAnimationRecorderId);
    if (!enable) {
        if (existing) ChildRemove(existing);
        return;
    }
    if (existing || !host_) return;
    cIGZWin* recorder = host_->CreateWinAnimationRecorder();     // new + ctor + AddRef, 0x1003728a4
    if (!recorder) return;
    cRZString directory;
    host_->UserDataDirectory(directory);                        // SL::UserDataDirectory @ 0x100040556
    directory.AppendChars("RecordedAnimations", 18);
    directory.AppendChars("/", 1);
    host_->RecorderSetAnimationOutputDirectory(recorder, directory, true);
    recorder->SetID(kView3DAnimationRecorderId);
    ChildAdd(recorder);
    recorder->Release();
}

// ==================================================================================================== tool invokers

void cSC4View3DWin::InvokeZoningTool(std::uint32_t type) {                  // 0x100783d86
    if (!host_ || !host_->CanInvokeZoningTool()) return;        // SL::spCity vslot 0xb0 (0x100783d92)
    if (cSC4ViewInputControl* tool = host_->CreateZoningTool(type)) {   // cSC4ViewInputControlZone
        tool->AddRef();
        SetCurrentViewInputControl(tool, 2);
        tool->Release();
    }
}

void cSC4View3DWin::InvokeLandscapeTool(std::uint32_t type, bool flag) {    // 0x10078408e
    if (!host_) return;
    if (cSC4ViewInputControl* tool = host_->CreateTerrainScrubberTool(type, flag)) {
        tool->AddRef();
        SetCurrentViewInputControl(tool, 2);
        tool->Release();
    }
}

void cSC4View3DWin::ApplyDefaultTerrainBrush(int brush, bool flag) {        // 0x100790b4a
    if (brush == 7 || !host_) return;                           // 7 = no default brush
    ITerrainEditor* terrain = host_->TerrainEditor();
    if (!terrain) return;
    std::uint32_t landscapeType = 0;
    if (terrain->TerrainTypeForBrush(brush, landscapeType)) InvokeLandscapeTool(landscapeType, flag);
}

void cSC4View3DWin::ChangeTerrainType() {                                   // 0x1007928da
    if (!host_) return;
    ITerrainEditor* terrain = host_->TerrainEditor();
    if (!terrain) return;
    int type = terrain->TerrainType();                          // vslot 0x150
    host_->RunChangeTerrainDialog(type);                        // cSTEChangeTerrainUI + DoDialog
    terrain->SetTerrainType(type);                              // vslot 0x148
}

void cSC4View3DWin::RunGlobalTerrainTool(int tool) {                        // 0x1007849bc
    if (host_) host_->PlayGlobalTerrainEffectSound();           // SL::spSS / spViewUtilities / SFX block
    ITerrainEditor* terrain = host_ ? host_->TerrainEditor() : nullptr;
    switch (tool) {
        case 0:
            ChangeTerrainType();
            break;
        case 1:
            if (terrain) terrain->SetToolMode158(1);
            break;
        case 2:
            if (terrain) terrain->SetToolMode160(1);
            break;
        case 3:
            if (terrain) { terrain->CancelActiveTool(); terrain->SetActiveToolFlag(true); }
            break;
        case 4:
            if (terrain) { terrain->CancelActiveTool(); terrain->SetActiveToolFlag(false); }
            break;
        case 5:
        case 6:
            if (cIGZMessageServer2* server = host_ ? host_->MessageServer2() : nullptr) {
                auto* message = new cRZMessage2Standard();
                message->AddRef();
                message->SetType(kMsgViewTerrainDisplayChanged);
                message->SetData1(tool == 6 ? 1 : 0);
                server->MessagePost(message, false);
                message->Release();
            }
            break;
        case 7:
            if (host_) {
                if (cSC4ViewInputControl* levelTool = host_->CreateLevelTerrainTool()) {
                    levelTool->AddRef();
                    SetCurrentViewInputControl(levelTool, 2);
                    levelTool->Release();
                }
            }
            break;
        default:
            break;
    }
}

bool cSC4View3DWin::ToggleContourDisplay() {                                // 0x10078fe00
    contourDisplay_ = !contourDisplay_;
    if (!host_) return contourDisplay_;
    if (!contourDisplay_) {
        if (contourCallback_ != -1) {
            host_->RemoveContourCallback(contourCallback_);
            contourCallback_ = -1;
        }
    } else if (contourCallback_ == -1) {
        contourCallback_ = host_->AddContourCallback();
    }
    return contourDisplay_;
}

// ==================================================================================================== global effects

bool cSC4View3DWin::GetGlobalEffectTriggerState(std::uint32_t effectId) {   // 0x100792974
    return globalEffects_.find(effectId) != globalEffects_.end();
}

void cSC4View3DWin::AssertGlobalEffectTrigger(std::uint32_t effectId) {     // 0x10079299e
    auto it = globalEffects_.find(effectId);
    if (it != globalEffects_.end()) {
        ++it->second;
        return;
    }
    globalEffects_[effectId] = 1;
    if (cIGZMessageServer2* server = host_ ? host_->MessageServer2() : nullptr) {
        auto* message = new cRZMessage2Standard();
        message->AddRef();
        message->SetType(kMsgViewGlobalEffectTrigger);
        message->SetData1(effectId);
        message->SetData2(1);
        server->MessageSend(message);
        message->Release();
    }
}

void cSC4View3DWin::RetractGlobalEffectTrigger(std::uint32_t effectId) {    // 0x100792a6c
    auto it = globalEffects_.find(effectId);
    if (it == globalEffects_.end()) return;
    if (it->second >= 2) {
        --it->second;
        return;
    }
    globalEffects_.erase(it);
    if (cIGZMessageServer2* server = host_ ? host_->MessageServer2() : nullptr) {
        auto* message = new cRZMessage2Standard();
        message->AddRef();
        message->SetType(kMsgViewGlobalEffectTrigger);
        message->SetData1(effectId);
        message->SetData2(0);
        server->MessageSend(message);
        message->Release();
    }
}

// ==================================================================================================== the frame

bool cSC4View3DWin::GZPaint() {                                             // 0x100781590
    if (paintEnabled_) {
        std::uint32_t elapsed = frameTimer_.GetElapsedTimeU32();
        const float threshold = frameTimeThreshold_;
        if ((threshold == 0.0f) || (threshold < static_cast<float>(elapsed))) {
            frameTimer_.Restart();
            float dt = static_cast<float>(elapsed) / 1000000.0f;
            if (dt >= 0.5f) dt = 0.5f;
            UpdateCostIndicator(dt);
            if (scrolling_ && cameraControl_) {
                const bool scrolled = cameraControl_->ScrollByViewportAngle(scrollAngle_,
                                                                            dt * scrollSpeed_ * kScrollSpeedPerFrame);
                if (scrollStopping_) {
                    const float previous = scrollSpeed_;
                    scrollSpeed_ = previous - scrollStopSpeed_;
                    if (previous - scrollStopSpeed_ <= 0.0f) {
                        if (scrolling_) DoScrollEnd();
                        scrolling_ = false;
                    }
                }
                if (scrolled) DoTemporarySimHiddenPause();
            }
            if (simHiddenPaused_ && simHiddenTimeoutMs_ < simHiddenTimer_.GetElapsedTimeU32())
                DoTemporarySimHiddenResume();
            std::uint32_t frame = 0;
            if (!deferredPumpEnabled_ && host_) frame = host_->SimulationFrameCounter();
            if (renderer_) {
                void* drawContext = renderer_->GetDrawContext();
                if (host_) host_->SetDrawContextFrameMarker(drawContext, frame);
                renderer_->Draw();
            }
        }
    }
    if (!deferredMessages_.empty() && host_ && host_->PopupModalDialogsSafe()) {
        cIGZMessage2* message = deferredMessages_.front().get();
        if (message) message->AddRef();
        deferredMessages_.pop_front();
        if (host_->PopupModalDialogsEnabled())
            if (cIGZMessageServer2* server = host_->MessageServer2()) server->MessagePost(message, false);
        if (message) message->Release();
    }
    return true;
}

// ==================================================================================================== message routing

bool cSC4View3DWin::DoMessage(cGZMessage& message) {                         // 0x100784ca4
    if (host_) host_->FilterUiCommandMessage(message);
    const std::int32_t type = static_cast<std::int32_t>(message.type);
    if (type <= 0x1a) {
        if (type > 0x0c) {
            switch (type) {
                case 0x10:                                   // cGZMsgSetFocus
                    if (host_ && host_->CityExists()) {
                        edgeScrollX_ = 0;
                        edgeScrollY_ = 0;
                        keyboardScrollX_ = 0;
                        keyboardScrollY_ = 0;
                        RecalcScrollVelocity();
                        ScrollStop();
                        GZOnMouseExit(nullptr);
                    }
                    return true;
                case 0x13:                                   // cGZMsgMouseEnter
                    focusState_ = 1;
                    if (host_) host_->Update24HourClockHover();
                    break;
                case 0x14:                                   // cGZMsgMouseExit
                    focusState_ = 0;
                    if (host_) host_->Update24HourClockHover();
                    break;
                case 0x0d:                                   // cGZMsgMouseMove
                    if (host_) host_->Update24HourClockHover();
                    break;
                default:
                    break;
            }
        } else if (message.type == kMsgViewHtmlLink) {       // -0x1b57a26f
            const bool handled = host_ && host_->HandleUrl(reinterpret_cast<cIGZString*>(message.param2));
            return !handled;
        } else if (message.type == kMsgViewAdviceLink) {     // -0x15a42044
            if (host_) host_->HandleAdviceLink(message);
        } else if (message.type == kMsgViewCommand && message.param1 == kCmdViewCatalogItem) {
            cGZWin::SetFocus();
            if (host_) host_->InvokeCatalogItemCommand(static_cast<std::uint32_t>(message.param2),
                                                       static_cast<std::uint32_t>(message.param3), true);
            return true;
        }
    } else if (type > 0x6a5bdfcc) {
        if (message.type == kMsgViewAdviceLink2 || message.type == kMsgViewAdviceLink3) {
            if (host_) host_->HandleAdviceLink(message);
        }
    } else if (type == kMsgViewKey) {                        // cGZMsgKey
        const std::uint64_t sub = message.param2;
        if (sub >= 4 && sub <= 6) {
            const auto* keyInfo = reinterpret_cast<const std::uint32_t*>(message.param3);
            std::uint32_t key = 0;
            std::uint32_t modifiers = 0;
            if (keyInfo) {
                key = keyInfo[2];                            // +0x8
                modifiers = keyInfo[4];                      // +0x10
            }
            bool handled = true;
            if (sub == 5) {
                if (ProcessKeyDown(key, modifiers)) return true;
            } else if (sub == 4) {
                modifiers |= kKeyModifierKeyUp;
                if (ProcessKeyDown(key, modifiers)) return true;
            } else {
                if (ProcessKeyUp(key, modifiers)) return true;
                handled = false;
            }
            std::uint32_t command = 0;
            if (!keyAccelerator_ || !keyAccelerator_->TestForAccelerator(key, modifiers, handled, command)) return false;
            if (host_) host_->ExecuteAcceleratorCommand(command);
            return true;
        }
    } else if (message.type == kMsgViewCheatCode) {          // 0x230e27ac
        // vslot 0x38 of the object in param2 is cIGZString::ToChar (0x100784d5e).
        const char* text = nullptr;
        if (message.param2) text = reinterpret_cast<cIGZString*>(message.param2)->ToChar();
        if (host_) host_->ProcessCheatCode(static_cast<std::uint32_t>(message.param1), text ? text : "");
        return true;
    }
    return cGZWin::DoMessage(message);
}

// PARTIAL (see notes/ledger_pending/cSC4View3DWin.txt): the child-window filter entry point
// (0x10078af10, 496 bytes) routes button and menu messages from the GUI windows built by AddGUI; the
// GUI is not recovered, so the port answers "not handled" and leaves the message to the window tree.
bool cSC4View3DWin::DoMessage(cIGZWin*, cGZMessage&) { return false; }

// PARTIAL: the engine-notification entry point (0x100785a5e, 1657 bytes) drives the advisors, the
// city name / budget / population panels and the mission progress out of ~46 message types.
bool cSC4View3DWin::DoMessage(cIGZMessage2*) { return false; }

}  // namespace sc4
