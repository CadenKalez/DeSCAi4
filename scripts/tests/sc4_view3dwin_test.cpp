// cSC4View3DWin: the recovered slice — object lifecycle, QueryInterface / refcount, the camera and view
// state (zoom, rotation, scroll velocity, the scroll render-property snapshot), the view level / filter
// information messages, the input-tool stack and its dispatch, the two picks, the global effect triggers,
// the frame (GZPaint) and DoMessage(cGZMessage&). See src/sc4/View3DWin.h for the offsets and the quirks.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "rz/cRZMessage2Standard.h"
#include "sc4/View3DWin.h"
#include "s3d/ModelInstance.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace sc4 {
// Access to the members the surviving game code sets from the GUI paths that are not recovered.
struct View3DWinTestAccess {
    static bool& PaintEnabled(cSC4View3DWin& v) { return v.paintEnabled_; }
    static float& FrameTimeThreshold(cSC4View3DWin& v) { return v.frameTimeThreshold_; }
    static std::list<cRZAutoRefCount<cIGZMessage2>>& Deferred(cSC4View3DWin& v) { return v.deferredMessages_; }
    static std::vector<cSC4ViewInputControl*>& Tools(cSC4View3DWin& v) { return v.tools_; }
    static std::vector<cSC4ViewInputControl*>& Persistent(cSC4View3DWin& v) { return v.persistentTools_; }
    static cSC4CameraControl*& Camera(cSC4View3DWin& v) { return v.cameraControl_; }
    static IRenderer3D*& Renderer(cSC4View3DWin& v) { return v.renderer_; }
    static ICoverageHelper3D*& Coverage(cSC4View3DWin& v) { return v.coverageHelper_; }
    static cIGZWin*& ViewManagerSource(cSC4View3DWin& v) { return v.viewManagerSource_; }
    static ISimulatorPauseSink*& PauseSink(cSC4View3DWin& v) { return v.simulatorToPause_; }
    static cSC4RenderProperties& Snapshot(cSC4View3DWin& v) { return v.renderProperties_; }
    static bool& Scrolling(cSC4View3DWin& v) { return v.scrolling_; }
    static bool& Stopping(cSC4View3DWin& v) { return v.scrollStopping_; }
    static float& ScrollSpeed(cSC4View3DWin& v) { return v.scrollSpeed_; }
    static float& ScrollStopSpeed(cSC4View3DWin& v) { return v.scrollStopSpeed_; }
    static float& ScrollAngle(cSC4View3DWin& v) { return v.scrollAngle_; }
    static bool& ScrollStopScaled(cSC4View3DWin& v) { return v.scrollStopScaled_; }
    static int& KeyboardScrollX(cSC4View3DWin& v) { return v.keyboardScrollX_; }
    static int& KeyboardScrollY(cSC4View3DWin& v) { return v.keyboardScrollY_; }
    static int& EdgeScrollX(cSC4View3DWin& v) { return v.edgeScrollX_; }
    static int& EdgeScrollY(cSC4View3DWin& v) { return v.edgeScrollY_; }
    static bool& SimHidden(cSC4View3DWin& v) { return v.simHiddenPaused_; }
    static cIGZUnknown*& FilterInfo(cSC4View3DWin& v) { return v.filterInfo_; }
    static std::uint32_t& ViewLevel(cSC4View3DWin& v) { return v.viewLevel_; }
    static int& ZoomMin(cSC4View3DWin& v) { return v.zoomMin_; }
    static int& ZoomMax(cSC4View3DWin& v) { return v.zoomMax_; }
    static std::uint8_t& FocusState(cSC4View3DWin& v) { return v.focusState_; }
    static std::uint32_t& LastModifiers(cSC4View3DWin& v) { return v.lastMessageModifiers_; }
    static std::uint16_t& Unknown1c8(cSC4View3DWin& v) { return v.unknown1c8_; }
    static std::uint32_t& SimHiddenTimeout(cSC4View3DWin& v) { return v.simHiddenTimeoutMs_; }
};
}  // namespace sc4

namespace {

// ------------------------------------------------------------------------------------------------ GZ fakes
struct FakeCursor final : cIGZCursor {
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
struct FakeCursors final : cIGZCursorManager {
    FakeCursor byId;
    std::uint32_t lastLoaded = 0;
    int loadCount = 0;
    cIGZCursor* LoadCursor(std::uint32_t id) override { lastLoaded = id; ++loadCount; return &byId; }
    void SetCursor(cIGZCursor*) override {}
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
};
struct FakeMgr final : cIGZWinMgr {
    cIGZWin* mainWin = nullptr;
    cIGZWin *focus = nullptr, *capture = nullptr;
    FakeCursors cursors;
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    cIGZWin* GetMainWindow() override { return mainWin; }
    void AppendToPlotStrategy(cIGZWin*) override {}
    void Plot() override {}
    void PostMessageToTarget(cIGZWin*, const cGZMessage&) override {}
    void AddWindowToValidList(cIGZWin*) override {}
    void RemoveWindowFromValidList(cIGZWin*) override {}
    void CleanUpWindowReferences(cIGZWin* w) override { if (focus == w) focus = nullptr; if (capture == w) capture = nullptr; }
    bool DestroyWindow(cIGZWin*) override { return true; }
    bool IsWindowValid(cIGZWin*) override { return true; }
    cIGZWin* GetWindowFromPoint(int, int) override { return nullptr; }
    void SetCurrentMouseWin(cIGZWin*) override {}
    cIGZWin* GZGetCapture() override { return capture; }
    void GZSetCapture(cIGZWin* w) override { capture = w; }
    void GZReleaseCapture(cIGZWin* w) override { if (capture == w) capture = nullptr; }
    cIGZCursorManager* GetCursorManager() override { return &cursors; }
    cIGZWin* GZGetFocus() override { return focus; }
    void GZSetFocus(cIGZWin* w) override { focus = w; }
    cIGZKeyboard* GetKeyboard() override { return nullptr; }
};
// A window that answers the view-manager interface the view asks for (0x67c5bede).
struct ViewManagerWindow final : cGZWin {
    explicit ViewManagerWindow(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kView3DViewManagerIid) { *out = this; AddRef(); return true; }
        return cGZWin::QueryInterface(iid, out);
    }
};
struct FakeMessageServer final : cIGZMessageServer2 {
    int refs = 0;
    std::vector<std::uint32_t> sent, posted;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return refs ? --refs : 0; }
    bool MessageSend(cIGZMessage2* message) override {
        if (message) { sent.push_back(message->GetType()); Record(message); }
        return true;
    }
    bool MessagePost(cIGZMessage2* message, bool) override {
        if (message) { posted.push_back(message->GetType()); Record(message); }
        return true;
    }
    // The payload of the last message (the messages are freed right after the call, so copy it now).
    void* lastVoid1 = nullptr;
    void* lastVoid2 = nullptr;
    std::uint64_t lastData1 = 0, lastData2 = 0;
    void Record(cIGZMessage2* message) {
        lastVoid1 = lastVoid2 = nullptr;
        lastData1 = lastData2 = 0;
        if (auto* standard = dynamic_cast<cIGZMessage2Standard*>(message)) {
            lastVoid1 = standard->GetVoid1();
            lastVoid2 = standard->GetVoid2();
            lastData1 = standard->GetData1();
            lastData2 = standard->GetData2();
        }
    }
    bool AddNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool RemoveNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool GeneralMessagePostToTarget(cIGZMessage2*, cIGZMessageTarget2*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return 0; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
};
struct FakeNotification final : INotificationHelper3D {
    int addTypes = 0, addAll = 0, removeAll = 0;
    int lastCount = 0;
    std::uint32_t lastType = 0, firstType = 0;
    void AddMessageTypes(const std::uint32_t* types, int count) override {
        ++addTypes;
        lastCount = count;
        if (types && count > 0) { firstType = types[0]; lastType = types[count - 1]; }
    }
    void AddAllNotifications(cIGZMessageTarget2*) override { ++addAll; }
    void RemoveAllNotifications(cIGZMessageTarget2*) override { ++removeAll; }
};
struct FakeAccelerator final : cIGZWinKeyAccelerator {
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool RegisterAccelerator(std::uint32_t, std::uint32_t) override { return true; }
    bool RegisterAccelerator(std::uint32_t, const cGZMessage&) override { return true; }
    bool UnregisterAccelerator(std::uint32_t) override { return true; }
    bool IsAcceleratorPresent(std::uint32_t, bool, bool) override { return false; }
    bool TestForAccelerator(const cGZMessage&, std::uint32_t&) override { return false; }
    bool TestForAccelerator(const cGZMessage&, cGZMessage&) override { return false; }
    // The overload the view's key path (0x100784ed5) uses: key, modifiers, key-down flag, out command.
    bool TestForAccelerator(std::uint32_t key, std::uint32_t modifiers, bool keyDown, std::uint32_t& command) override {
        lastKey = key;
        lastModifiers = modifiers;
        lastDown = keyDown;
        ++tests;
        if (found) command = 0x4242;
        return found;
    }
    bool TestForAccelerator(std::uint32_t, std::uint32_t, bool, cGZMessage&) override { return false; }
    int GetKeysForAccelerator(std::uint32_t, std::uint32_t*, std::uint32_t&) override { return 0; }
    bool ProcessAccelerator(const cGZMessage&) override { return false; }
    int tests = 0;
    std::uint32_t lastKey = 0, lastModifiers = 0;
    bool lastDown = false, found = false;
};
struct FakeCoverage final : ICoverageHelper3D {
    int inits = 0, shutdowns = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    void Init() override { ++inits; }
    void Shutdown() override { ++shutdowns; }
};
struct FakePauseSink final : ISimulatorPauseSink {
    int pauses = 0, resumes = 0, refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void Pause() override { ++pauses; }
    void Resume() override { ++resumes; }
};
struct FakeProgressScreen final : cIGZUnknown {
    int refs = 0, shutdowns = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { if (refs) --refs; return refs; }
};
struct FakeRenderer final : IRenderer3D {
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init() override { return true; }
    void Shutdown() override { ++shutdowns; }
    void* GetDrawContext() override { return &drawContextMarker; }
    void* GetCamera() override { return nullptr; }
    void ForceFullRedraw() override { ++redraws; }
    void Draw() override { ++draws; }
    bool SetZoom(int zoom) override { lastZoom = zoom; ++zoomCalls; return true; }
    bool ZoomIn() override { ++zoomIn; return zoomInResult; }
    bool ZoomOut() override { ++zoomOut; return zoomOutResult; }
    bool SetRotation(int rotation) override { lastRotation = rotation; ++rotationCalls; return true; }
    bool RotateLeft() override { ++lefts; return true; }
    bool RotateRight() override { ++rights; return true; }
    bool SetZoomAndRotation(int zoom, int rotation) override {
        lastZoom = zoom; lastRotation = rotation; ++zoomAndRotation;
        return true;
    }
    bool PickTerrain(int x, int y, float* out3, bool always, bool arg5) override {
        lastPickX = x; lastPickY = y; pickTerrainAlways = always; pickTerrainArg5 = arg5; ++terrainPicks;
        if (!pickTerrainResult) return false;
        out3[0] = 1.5f; out3[1] = 2.5f; out3[2] = 3.5f;
        return true;
    }
    bool Pick(int x, int y, cISC4OccupantFilter*, s3d::cIS3DModelInstance*& model) override {
        lastPickX = x; lastPickY = y; ++picks;
        model = pickResult;
        if (model) model->AddRef();
        return pickResult != nullptr;
    }
    int shutdowns = 0, redraws = 0, draws = 0, zoomCalls = 0, zoomIn = 0, zoomOut = 0, rotationCalls = 0;
    int lefts = 0, rights = 0, zoomAndRotation = 0, terrainPicks = 0, picks = 0;
    int lastZoom = -1, lastRotation = -1, lastPickX = 0, lastPickY = 0;
    bool pickTerrainResult = false, pickTerrainAlways = true, pickTerrainArg5 = false;
    bool zoomInResult = true, zoomOutResult = true;
    std::uint32_t drawContextMarker = 0;
    s3d::cIS3DModelInstance* pickResult = nullptr;
};
struct FakeModelInstance final : s3d::cIS3DModelInstance {
    int refs = 1, releases = 0;
    cIGZUnknown* owner = nullptr;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { ++releases; return --refs; }
    cIGZUnknown* GetOwner() override { return owner; }
    void SetOwner(cIGZUnknown*) override {}
    void* GetCustomData() override { return nullptr; }
    void SetCustomData(void*) override {}
    void GetBounds(cS3DBoundingBox&) override {}
    void GetWorldBounds(cS3DBoundingBox&) override {}
    void GetTransform(cS3DTransform&) override {}
    void SetTransform(const cS3DTransform&) override {}
    bool Pick(cSC4PickContext*) override { return false; }
    int Draw(SC4DrawContext*, bool) override { return 0; }
    int GetProperties() override { return 0; }
    void SetProperties(int) override {}
    s3d::SC4DrawAttributes GetDrawAttributes() const override { return {}; }
    void SetDrawAttributes(const s3d::SC4DrawAttributes&) override {}
    void GetSortValue(std::uint32_t& out) const override { out = 0; }
    s3d::cIS3DResModel* GetResModel() const override { return nullptr; }
};
struct FakeOccupant final : cIGZUnknown {
    int refs = 0;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != 0x26bb767e) return false;
        *out = this; AddRef(); return true;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { if (refs) --refs; return refs; }
};

// ------------------------------------------------------------------------------------------------ the host
struct FakeHost final : IView3DWinHost {
    FakeMessageServer server;
    FakeNotification notifications;
    FakeAccelerator accelerator;
    FakeCoverage coverage;
    FakePauseSink pauseSink;
    FakeProgressScreen progress;
    std::uint8_t preferences[0x1734] = {};
    bool cityExists = false, cheatPresent = false, lotPlop = false, progressWanted = false;
    int virtualZoomLimit = 5;
    int onView3DCreate = 0, onView3DShutdown = 0;
    std::vector<std::uint32_t> registeredCheats, unregisteredCheats;
    std::vector<std::string> registeredCheatStrings;
    int addTargets = 0, removeTargets = 0, registerTables = 0, unregisterTables = 0;
    int lotConfigShutdowns = 0, guiRemovals = 0, modelMakerShutdowns = 0, cameraShutdowns = 0;
    int clockShutdowns = 0, validationShutdowns = 0, progressShutdowns = 0;
    std::vector<std::uint32_t> gameMessages;
    int lightingHighlights = 0, lightingRestores = 0;
    int costUpdates = 0;
    float lastCostDt = -1.0f;
    bool popupSafe = false, popupEnabled = false;
    std::uint32_t frameCounter = 0x1234;
    int hoverUpdates = 0;
    int cursorTextUpdates = 0, lastCursorX = 0, lastCursorY = 0;
    int frameMarkers = 0;
    std::uint32_t lastFrameMarker = 0;
    std::vector<std::uint32_t> cheatRuns;
    std::vector<std::string> cheatTexts;
    int catalogInvokes = 0;
    std::uint32_t catalogItem = 0, catalogParam = 0;
    bool catalogFlag = false;
    int acceleratorCommands = 0;
    std::uint32_t lastAcceleratorCommand = 0;
    int uiFilters = 0, adviceLinks = 0, urlHandles = 0;
    bool urlResult = false;
    // In the game both accessors answer the one render-property object (SL::spRenderProperties and
    // cISC4App vslot 0x160 are the same instance).
    cSC4RenderProperties globalProperties;

    cIGZMessageServer2* MessageServer2() override { return &server; }
    std::uint8_t* Preferences() override { return preferences; }
    bool CityExists() override { return cityExists; }
    void PostGameMessage(std::uint32_t type) override { gameMessages.push_back(type); }
    INotificationHelper3D* NotificationHelper() override { return &notifications; }
    void RegisterCommandTable() override { ++registerTables; }
    void UnregisterCommandTable() override { ++unregisterTables; }
    void AddCheatCodeTarget(cIGZWin*) override { ++addTargets; }
    void RemoveCheatCodeTarget(cIGZWin*) override { ++removeTargets; }
    bool RegisterCheatCode(std::uint32_t id, const char* text) override {
        registeredCheats.push_back(id);
        registeredCheatStrings.push_back(text ? text : "");
        return true;
    }
    bool UnregisterCheatCode(std::uint32_t id) override { unregisteredCheats.push_back(id); return true; }
    bool CheatCodeManagerPresent() override { return cheatPresent; }
    bool LotPlopCheatsEnabled() override { return lotPlop; }
    cIGZWinKeyAccelerator* CreateKeyAccelerator() override { accelerator.AddRef(); return &accelerator; }
    cSC4RenderProperties* GlobalRenderProperties() override { return &globalProperties; }
    cSC4RenderProperties* AppRenderProperties() override { return &globalProperties; }
    void SetScrollLightingHighlight() override { ++lightingHighlights; }
    void RestoreLighting() override { ++lightingRestores; }
    int VirtualZoomLimit() override { return virtualZoomLimit; }
    bool ProgressScreenWanted() override { return progressWanted; }
    cIGZUnknown* CreateProgressScreen(cIGZWin*, bool) override { return &progress; }   // the view takes the reference
    void ShutdownProgressScreen(cIGZUnknown*) override { ++progressShutdowns; }
    void ShutdownValidationWindow(cIGZUnknown*) override { ++validationShutdowns; }
    void ShutdownLotConfigurationEditors() override { ++lotConfigShutdowns; }
    void RemoveGui() override { ++guiRemovals; }
    void ShutdownModelMaker(cIGZUnknown*) override { ++modelMakerShutdowns; }
    void ShutdownCameraObject(cIGZUnknown*) override { ++cameraShutdowns; }
    void ShutdownSimulationClock(cIGZUnknown*) override { ++clockShutdowns; }
    void UpdateCostIndicator(float dt) override { ++costUpdates; lastCostDt = dt; }
    bool PopupModalDialogsSafe() override { return popupSafe; }
    bool PopupModalDialogsEnabled() override { return popupEnabled; }
    std::uint32_t SimulationFrameCounter() override { return frameCounter; }
    void FilterUiCommandMessage(cGZMessage&) override { ++uiFilters; }
    bool HandleUrl(cIGZString*) override { ++urlHandles; return urlResult; }
    void HandleAdviceLink(cGZMessage&) override { ++adviceLinks; }
    void ProcessCheatCode(std::uint32_t id, const char* cmdLine) override {
        cheatRuns.push_back(id);
        cheatTexts.push_back(cmdLine ? cmdLine : "");
    }
    void InvokeCatalogItemCommand(std::uint32_t item, std::uint32_t param, bool flag) override {
        ++catalogInvokes; catalogItem = item; catalogParam = param; catalogFlag = flag;
    }
    void ExecuteAcceleratorCommand(std::uint32_t command) override {
        ++acceleratorCommands; lastAcceleratorCommand = command;
    }
    void Update24HourClockHover() override { ++hoverUpdates; }
    void UpdateCursorTextPositions(int x, int y) override { ++cursorTextUpdates; lastCursorX = x; lastCursorY = y; }
    void SetDrawContextFrameMarker(void* context, std::uint32_t frame) override {
        ++frameMarkers;
        lastFrameMarker = frame;
        if (context) *static_cast<std::uint32_t*>(context) = frame;
    }
    void OnView3DCreate(cISC4View3DWin* view) override { ++onView3DCreate; lastView = view; }
    void OnView3DShutdown() override { ++onView3DShutdown; }
    cISC4View3DWin* lastView = nullptr;
};

// ------------------------------------------------------------------------------------------------ tool double
struct ControlLog {
    int inits = 0, shutdowns = 0, activations = 0, deactivations = 0, mouseExits = 0;
    int characters = 0, keysDown = 0, keysUp = 0, wheels = 0;
    std::vector<std::uint32_t> keys;
    bool handleCharacter = false, handleKeyDown = false, handleKeyUp = false, handleWheel = false;
    bool initResult = true;
};
struct TestControl final : cSC4ViewInputControl {
    explicit TestControl(ControlLog& log) : log_(log) {}
    bool Init() override { ++log_.inits; return log_.initResult; }
    bool Shutdown() override { ++log_.shutdowns; return true; }
    void Activate() override { ++log_.activations; }
    void Deactivate() override { ++log_.deactivations; }
    bool OnCharacter(char) override { ++log_.characters; return log_.handleCharacter; }
    bool OnKeyDown(std::uint32_t key, std::uint32_t) override {
        ++log_.keysDown; log_.keys.push_back(key); return log_.handleKeyDown;
    }
    bool OnKeyUp(std::uint32_t key, std::uint32_t) override {
        ++log_.keysUp; log_.keys.push_back(key); return log_.handleKeyUp;
    }
    bool OnMouseWheel(int, int, std::uint32_t, int) override { ++log_.wheels; return log_.handleWheel; }
    bool OnMouseExit() override { ++log_.mouseExits; return true; }
    ControlLog& log_;
};

}  // namespace

int main() {
    FakeHost host;
    SetView3DWinHost(&host);
    FakeMgr mgr;
    ViewManagerWindow mainWindow(&mgr);
    mainWindow.SetArea(0, 0, 800, 600);
    mgr.mainWin = &mainWindow;
    FakeRenderer renderer;

    // ------------------------------------------------------------------ constructor
    auto* view = new cSC4View3DWin(&mgr);
    CHECK(host.onView3DCreate == 1 && host.lastView == view);
    CHECK(host.notifications.addTypes == 1 && host.notifications.lastCount == 132);
    CHECK(host.notifications.firstType == 0xa6b79602 && host.notifications.lastType == 0xabcdee11);
    CHECK(View3DWinTestAccess::Unknown1c8(*view) == 0x0100);
    CHECK(View3DWinTestAccess::ViewLevel(*view) == 1);
    CHECK(View3DWinTestAccess::ZoomMin(*view) == 0 && View3DWinTestAccess::ZoomMax(*view) == 5);
    View3DWinTestAccess::Coverage(*view) = &host.coverage;

    // ------------------------------------------------------------------ Init
    host.cheatPresent = true;
    host.lotPlop = true;
    host.progressWanted = true;
    CHECK(view->Init());
    CHECK(host.notifications.addAll == 1);
    CHECK(host.registerTables == 1);
    CHECK(host.addTargets == 1);
    CHECK(host.registeredCheats.size() == 13);
    CHECK(host.registeredCheats[0] == kCheatGameOfLife && host.registeredCheatStrings[0] == "GOL");
    CHECK(host.registeredCheats[4] == kCheatSetRenderProperty && host.registeredCheatStrings[4] == "RenderProp");
    CHECK(host.registeredCheats[10] == kCheatLotPlop && host.registeredCheatStrings[10] == "LotPlop");
    CHECK(host.registeredCheats[12] == kCheatTollBooth && host.registeredCheatStrings[12] == "toll");
    CHECK(std::strcmp(view->GetCaption().ToChar(), "cSC4View3DWin") == 0);
    CHECK(view->GetID() == kView3DWindowId);
    CHECK(view->GetW() == 800 && view->GetH() == 600 && view->GetL() == 0 && view->GetT() == 0);
    CHECK(mgr.cursors.loadCount == 1 && mgr.cursors.lastLoaded == kView3DCursorId);
    CHECK(view->GetKeyAccelerator() == &host.accelerator);
    CHECK(host.coverage.inits == 1);
    CHECK(host.progress.refs == 1);
    CHECK(view->Init());                                       // second Init: nothing happens again
    CHECK(host.notifications.addAll == 1 && host.registerTables == 1 && host.addTargets == 1);

    // Without the lot-plop cheats only ten codes are registered.
    {
        host.registeredCheats.clear();
        host.registeredCheatStrings.clear();
        host.lotPlop = false;
        auto* bare = new cSC4View3DWin(&mgr);
        View3DWinTestAccess::Coverage(*bare) = &host.coverage;
        host.cheatPresent = false;
        CHECK(bare->Init());
        CHECK(host.registeredCheats.empty() && host.addTargets == 1);
        CHECK(bare->Shutdown());
        host.cheatPresent = true;
        host.registeredCheats.clear();
        host.registeredCheatStrings.clear();
        auto* copy = new cSC4View3DWin(&mgr);
        View3DWinTestAccess::Coverage(*copy) = &host.coverage;
        CHECK(copy->Init());
        CHECK(host.registeredCheats.size() == 10);
        CHECK(copy->Shutdown());
        delete copy;
        host.cheatPresent = true;
        host.lotPlop = true;
        delete bare;
        host.registeredCheats.clear();
        host.registeredCheatStrings.clear();
    }

    // ------------------------------------------------------------------ QueryInterface and refcount
    void* out = nullptr;
    CHECK(view->QueryInterface(1, &out) && out == static_cast<cISC4View3DWin*>(view));
    CHECK(view->QueryInterface(kGZIID_cISC4View3DWin, &out) && out == static_cast<cISC4View3DWin*>(view));
    CHECK(view->QueryInterface(kGZIID_cIGZWin, &out) && out == static_cast<cIGZWin*>(static_cast<cGZWin*>(view)));
    CHECK(view->QueryInterface(kGZIID_cIGZMessageTarget2, &out) && out == static_cast<cIGZMessageTarget2*>(view));
    CHECK(view->QueryInterface(kGZIID_cIGZWinMessageFilter, &out) && out == static_cast<cIGZWinMessageFilter*>(view));
    CHECK(!view->QueryInterface(0xe98b2f57, &out));            // refused although cGZWin answers it
    CHECK(!view->QueryInterface(0x12345678, &out));
    CHECK(view->AsIGZWin() == static_cast<cIGZWin*>(static_cast<cGZWin*>(view)));
    const std::uint32_t before = view->AddRef();
    CHECK(before == view->Release() + 1);

    CHECK(view->GetViewManager() == nullptr);
    View3DWinTestAccess::ViewManagerSource(*view) = &mainWindow;
    cIGZUnknown* manager = view->GetViewManager();
    CHECK(manager == static_cast<cIGZUnknown*>(static_cast<cGZWin*>(&mainWindow)));
    if (manager) manager->Release();

    // ------------------------------------------------------------------ plain accessors
    CHECK(view->GetRenderer() == nullptr);                     // set below
    CHECK(view->GetModelMaker() == nullptr);
    // The cGZWin key hooks answer false; GZOnCommand forwards to cGZWin.
    CHECK(!view->GZOnKeyDown(0x51, 0x2));
    CHECK(!view->GZOnKeyUp(0x51, 0x2));
    CHECK(!view->GZOnCommand(0x1234, 0));
    CHECK(view->GetCoverageHelper() == &host.coverage);
    CHECK(view->GetTerrainQueryEnabled() == false);
    view->DisplayTerrainPickDebugString(4, 5);
    view->SetErrorReportString("ignored");
    CHECK(view->GetLastSelectedBuilding() == 0 && view->GetLastSelectedLot() == 0);
    view->SetLastSelectedBuilding(0x11111111);
    view->SetLastSelectedLot(0x22222222);
    CHECK(view->GetLastSelectedBuilding() == 0x11111111 && view->GetLastSelectedLot() == 0x22222222);

    // The key accelerator is reference counted and only swapped when it changes.
    FakeAccelerator replacement;
    CHECK(host.accelerator.refs == 2);                         // the object's own + the view's reference
    view->SetKeyAccelerator(&replacement);
    CHECK(view->GetKeyAccelerator() == &replacement && replacement.refs == 2 && host.accelerator.refs == 1);
    view->SetKeyAccelerator(&replacement);
    CHECK(replacement.refs == 2);
    view->SetKeyAccelerator(nullptr);
    CHECK(view->GetKeyAccelerator() == nullptr && replacement.refs == 1);

    // ------------------------------------------------------------------ camera / zoom / rotation
    auto* camera = new cSC4CameraControl();
    camera->AddRef();                                          // the reference the view owns
    CHECK(camera->Init({1000.0f, 0.0f, 2000.0f}, new s3d::cS3DCamera()));
    View3DWinTestAccess::Camera(*view) = camera;
    View3DWinTestAccess::Renderer(*view) = &renderer;
    CHECK(view->GetRenderer() == &renderer);
    CHECK(camera->Zoom() == 0 && camera->Rotation() == 0);

    CHECK(view->SetZoom(3) && renderer.lastZoom == 3);
    CHECK(view->SetZoom(9) && renderer.lastZoom == 5);
    CHECK(view->SetZoom(-2) && renderer.lastZoom == 0);
    CHECK(renderer.zoomCalls == 3);

    camera->SetZoom(2.0f);
    CHECK(camera->GetVirtualZoom() == 2);
    CHECK(view->CanZoomIn() && view->CanZoomOut());
    CHECK(view->ZoomIn() && renderer.zoomIn == 1);
    CHECK(view->ZoomOut() && renderer.zoomOut == 1);
    renderer.zoomInResult = false;
    CHECK(!view->ZoomIn() && renderer.zoomIn == 2);
    View3DWinTestAccess::ZoomMax(*view) = 2;
    CHECK(!view->CanZoomIn() && !view->ZoomIn());
    View3DWinTestAccess::ZoomMin(*view) = 2;
    CHECK(!view->CanZoomOut() && !view->ZoomOut());
    View3DWinTestAccess::ZoomMin(*view) = 0;
    View3DWinTestAccess::ZoomMax(*view) = 5;

    CHECK(view->SetRotation(2) && renderer.lastRotation == 2);
    CHECK(view->RotateLeft() && renderer.lefts == 1);
    CHECK(view->RotateRight() && renderer.rights == 1);
    CHECK(view->SetZoomAndRotation(9, 3) && renderer.lastZoom == 5 && renderer.lastRotation == 3);
    CHECK(renderer.zoomAndRotation == 1);

    // SetZoomLimits: the stored max becomes the app's virtual limit when the argument is > 4, but the
    // clamp compares against the argument (quirk, 0x100781e96).
    host.virtualZoomLimit = 4;
    CHECK(view->SetZoomLimits(0, 6));
    CHECK(View3DWinTestAccess::ZoomMax(*view) == 4);
    camera->SetZoom(1.0f);
    host.virtualZoomLimit = 5;
    CHECK(view->SetZoomLimits(0, 6));
    CHECK(View3DWinTestAccess::ZoomMax(*view) == 5);
    CHECK(renderer.zoomCalls == 3);                            // nothing to change: the target is the current zoom
    CHECK(view->SetZoomLimits(3, 2));
    CHECK(renderer.lastZoom == 2);                             // raised to the min, then clamped to the max
    View3DWinTestAccess::ZoomMin(*view) = 0;
    View3DWinTestAccess::ZoomMax(*view) = 5;

    // ------------------------------------------------------------------ the sim-hidden pause
    View3DWinTestAccess::PauseSink(*view) = &host.pauseSink;
    host.pauseSink.refs = 1;
    view->DoTemporarySimHiddenPause();
    CHECK(View3DWinTestAccess::SimHidden(*view) && host.pauseSink.pauses == 1);
    view->DoTemporarySimHiddenPause();
    CHECK(host.pauseSink.pauses == 1);                         // already paused
    view->DoTemporarySimHiddenResume();
    CHECK(!View3DWinTestAccess::SimHidden(*view) && host.pauseSink.resumes == 1);
    view->DoTemporarySimHiddenResume();
    CHECK(host.pauseSink.resumes == 1);

    // ------------------------------------------------------------------ scrolling
    CHECK(view->ScrollStop());                                 // not scrolling: no-op
    CHECK(!View3DWinTestAccess::Stopping(*view));
    host.preferences[kPrefsEdgeScrollEnabled] = 0;
    View3DWinTestAccess::KeyboardScrollX(*view) = 1;           // east
    View3DWinTestAccess::ScrollSpeed(*view) = 0.2f;
    camera->SetZoom(0.0f);
    view->RecalcScrollVelocity();
    CHECK(View3DWinTestAccess::Scrolling(*view));
    CHECK(std::fabs(View3DWinTestAccess::ScrollAngle(*view) - 0.0f) < 1e-6f);
    CHECK(std::fabs(View3DWinTestAccess::ScrollSpeed(*view) - 0.2f) < 1e-6f);
    CHECK(host.gameMessages.size() == 1 && host.gameMessages[0] == kMsgViewScrollBegin);
    View3DWinTestAccess::KeyboardScrollX(*view) = -1;
    View3DWinTestAccess::KeyboardScrollY(*view) = 1;
    camera->SetZoom(0.0f);
    view->RecalcScrollVelocity();
    CHECK(std::fabs(View3DWinTestAccess::ScrollAngle(*view) - 2.3561945f) < 1e-5f);
    View3DWinTestAccess::KeyboardScrollX(*view) = 0;
    View3DWinTestAccess::KeyboardScrollY(*view) = 0;
    view->RecalcScrollVelocity();
    // The scroll stays "active" until GZPaint finishes the stop animation; RecalcScrollVelocity only
    // raises the stop speed (0x1007829..0x100781df6).
    CHECK(View3DWinTestAccess::Stopping(*view) && View3DWinTestAccess::Scrolling(*view));
    View3DWinTestAccess::KeyboardScrollX(*view) = 1;
    View3DWinTestAccess::KeyboardScrollY(*view) = 1;
    view->KillKeyboardScrolling();
    CHECK(View3DWinTestAccess::KeyboardScrollX(*view) == 0 && View3DWinTestAccess::KeyboardScrollY(*view) == 0);
    CHECK(View3DWinTestAccess::Scrolling(*view));             // still stopping
    View3DWinTestAccess::EdgeScrollX(*view) = 1;               // the preference drops both edge inputs
    View3DWinTestAccess::EdgeScrollY(*view) = 1;
    host.preferences[kPrefsEdgeScrollEnabled] = 1;
    view->RecalcScrollVelocity();
    CHECK(View3DWinTestAccess::EdgeScrollX(*view) == 0 && View3DWinTestAccess::EdgeScrollY(*view) == 0);
    host.preferences[kPrefsEdgeScrollEnabled] = 0;

    // ScrollStop: the stop speed comes from +0x4f0, scaled by the current speed when +0x4f4 is set.
    View3DWinTestAccess::Scrolling(*view) = true;
    View3DWinTestAccess::ScrollSpeed(*view) = 0.5f;
    View3DWinTestAccess::Stopping(*view) = false;
    CHECK(view->ScrollStop());
    CHECK(View3DWinTestAccess::Stopping(*view));
    CHECK(std::fabs(View3DWinTestAccess::ScrollStopSpeed(*view) - 1.0f) < 1e-6f);
    View3DWinTestAccess::ScrollStopScaled(*view) = true;
    View3DWinTestAccess::Stopping(*view) = false;
    CHECK(view->ScrollStop());
    CHECK(std::fabs(View3DWinTestAccess::ScrollStopSpeed(*view) - 0.5f) < 1e-6f);
    View3DWinTestAccess::ScrollStopScaled(*view) = false;
    View3DWinTestAccess::Scrolling(*view) = false;
    View3DWinTestAccess::Stopping(*view) = false;

    // SetScrolling drives DoScrollBegin / DoScrollEnd, which snapshot and restore the render properties.
    host.globalProperties.SetBoolValue(kPropNoPartialBackingStoreCopies, true);
    host.globalProperties.SetBoolValue(kPropRenderShadows, true);
    host.globalProperties.SetBoolValue(kPropRenderDecals, true);
    host.globalProperties.SetBoolValue(kPropRenderAutomata, true);
    host.globalProperties.SetBoolValue(kPropRenderParticleEffects, true);
    host.globalProperties.SetFloatValue(kPropRenderModelThreshold, 6.0f);
    host.globalProperties.SetFloatValue(kPropRenderModelThresholdScroll, 40.0f);
    view->SetScrolling(true, 1.25f, 0.75f);
    CHECK(View3DWinTestAccess::Scrolling(*view));
    CHECK(std::fabs(View3DWinTestAccess::ScrollAngle(*view) - 1.25f) < 1e-6f);
    CHECK(host.lightingHighlights == 1);
    // The snapshot keeps the state the scroll started from; the live object gets the reduced settings.
    CHECK(View3DWinTestAccess::Snapshot(*view).BoolValue(kPropRenderShadows));
    CHECK(View3DWinTestAccess::Snapshot(*view).BoolValue(kPropRenderAutomata));
    CHECK(std::fabs(View3DWinTestAccess::Snapshot(*view).FloatValue(kPropRenderModelThreshold) - 6.0f) < 1e-6f);
    CHECK(!host.globalProperties.BoolValue(kPropRenderShadows));
    CHECK(!host.globalProperties.BoolValue(kPropRenderDecals));
    CHECK(!host.globalProperties.BoolValue(kPropRenderAutomata));
    CHECK(!host.globalProperties.BoolValue(kPropRenderParticleEffects));
    CHECK(std::fabs(host.globalProperties.FloatValue(kPropRenderModelThreshold) - 40.0f) < 1e-6f);
    view->SetScrolling(false, 0.0f, 0.0f);
    CHECK(!View3DWinTestAccess::Scrolling(*view));
    CHECK(host.lightingRestores == 1);
    CHECK(renderer.redraws == 1);
    CHECK(host.globalProperties.BoolValue(kPropRenderShadows));      // restored from the snapshot
    CHECK(std::fabs(host.globalProperties.FloatValue(kPropRenderModelThreshold) - 6.0f) < 1e-6f);
    CHECK(host.gameMessages.size() >= 2);
    CHECK(host.gameMessages.back() == kMsgViewScrollEnd);
    // With the property switched off the snapshot is skipped entirely.
    host.globalProperties.SetBoolValue(kPropNoPartialBackingStoreCopies, false);
    host.globalProperties.SetBoolValue(kPropRenderShadows, true);
    view->SetScrolling(true, 0.0f, 0.0f);
    CHECK(host.lightingHighlights == 1 && host.globalProperties.BoolValue(kPropRenderShadows));
    view->SetScrolling(false, 0.0f, 0.0f);
    CHECK(host.lightingRestores == 1);

    // ------------------------------------------------------------------ the tool stack
    {
        ControlLog logA, logB;
        auto* controlA = new TestControl(logA);
        controlA->AddRef();                              // the caller's reference (the view AddRefs too)
        auto* controlB = new TestControl(logB);
        controlB->AddRef();
        host.server.posted.clear();
        CHECK(view->SetCurrentViewInputControl(controlA, 0));
        controlA->Release();                             // from here the stack owns it
        CHECK(view->GetCurrentViewInputControl() == controlA);
        CHECK(logA.inits == 1 && logA.activations == 1);
        CHECK(host.server.posted.size() == 1 && host.server.posted[0] == kMsgViewInputControlChanged);
        CHECK(host.server.lastVoid1 == nullptr && host.server.lastVoid2 == controlA);
        // Pushing a second tool deactivates the first one but keeps it on the stack.
        CHECK(view->SetCurrentViewInputControl(controlB, 0));
        controlB->Release();
        CHECK(view->GetCurrentViewInputControl() == controlB);
        CHECK(logA.mouseExits == 1 && logA.deactivations == 1);
        CHECK(logB.activations == 1);
        CHECK(View3DWinTestAccess::Tools(*view).size() == 2);
        // Removing the top tool deactivates / shuts down the control twice (the view's Shutdown and the
        // FinalRelease's), tells the message server about the change and re-activates nothing.
        host.server.posted.clear();
        CHECK(view->RemoveCurrentViewInputControl(false));
        CHECK(view->GetCurrentViewInputControl() == controlA);
        CHECK(logB.deactivations == 1 && logB.shutdowns == 2);
        CHECK(View3DWinTestAccess::Tools(*view).size() == 1);
        CHECK(host.server.posted.size() == 1);
        CHECK(host.server.lastVoid1 != nullptr && host.server.lastVoid2 == controlA);
        // Removing the last tool with keepCursor leaves the cursor alone; without it the default cursor
        // is loaded again.
        mgr.cursors.loadCount = 0;
        CHECK(view->RemoveCurrentViewInputControl(true));
        CHECK(view->GetCurrentViewInputControl() == nullptr);
        CHECK(logA.deactivations == 2 && logA.shutdowns == 2);
        CHECK(mgr.cursors.loadCount == 0);
        CHECK(!view->RemoveCurrentViewInputControl(false));      // nothing left
        CHECK(!view->RemoveAllViewInputControls(false));         // returns false by design
    }
    {
        ControlLog logC;
        auto* controlC = new TestControl(logC);
        controlC->AddRef();
        CHECK(view->SetCurrentViewInputControl(controlC, 0));
        controlC->Release();
        mgr.cursors.loadCount = 0;
        CHECK(view->RemoveCurrentViewInputControl(false));
        CHECK(mgr.cursors.loadCount == 1 && mgr.cursors.lastLoaded == kView3DCursorId);
        CHECK(logC.shutdowns == 2);
    }

    // Persistent tools: Init, AddRef, Activate; removal deactivates / shuts down / releases.
    {
        ControlLog logP, logQ;
        auto* persistent = new TestControl(logP);
        persistent->AddRef();
        CHECK(view->AddPersistentViewInputControl(persistent));
        persistent->Release();
        CHECK(View3DWinTestAccess::Persistent(*view).size() == 1);
        CHECK(logP.inits == 1 && logP.activations == 1);
        auto* other = new TestControl(logQ);
        CHECK(!view->RemovePersistentViewInputControl(other));    // not registered
        delete other;
        CHECK(view->RemovePersistentViewInputControl(persistent));
        CHECK(View3DWinTestAccess::Persistent(*view).empty());
        CHECK(logP.deactivations == 1 && logP.shutdowns == 2);
        auto* second = new TestControl(logP);
        second->AddRef();
        CHECK(view->AddPersistentViewInputControl(second));
        second->Release();
        CHECK(logP.inits == 2 && logP.activations == 2);
        CHECK(view->RemoveAllPersistentViewInputControls());
        CHECK(View3DWinTestAccess::Persistent(*view).empty());
        CHECK(logP.deactivations == 2 && logP.shutdowns == 4);
    }
    // A tool whose Init fails is not added at all.
    {
        ControlLog logF;
        auto* failing = new TestControl(logF);
        failing->AddRef();
        logF.initResult = false;
        CHECK(view->SetCurrentViewInputControl(failing, 0));
        CHECK(View3DWinTestAccess::Tools(*view).empty());
        CHECK(logF.inits == 1 && logF.activations == 0);
        CHECK(view->AddPersistentViewInputControl(failing));      // returns true, adds nothing
        CHECK(View3DWinTestAccess::Persistent(*view).empty());
        failing->Release();                                       // the caller's reference (FinalRelease deletes)
    }

    // GZOnCharacter: the current stack first, and a handled character skips the persistent tools.
    {
        ControlLog stackLog, persistLog;
        auto* stacked = new TestControl(stackLog);
        stacked->AddRef();
        auto* persistent = new TestControl(persistLog);
        persistent->AddRef();
        CHECK(view->AddPersistentViewInputControl(persistent));
        persistent->Release();
        CHECK(view->SetCurrentViewInputControl(stacked, 0));
        stacked->Release();
        stackLog.handleCharacter = true;
        CHECK(view->GZOnCharacter('a'));
        CHECK(stackLog.characters == 1 && persistLog.characters == 0);
        stackLog.handleCharacter = false;
        CHECK(!view->GZOnCharacter('b'));
        CHECK(stackLog.characters == 2 && persistLog.characters == 1);
        // The persistent loop is bounded by the current stack's size (quirk, 0x100782ab8): with two
        // persistent tools and one stacked tool the top persistent tool is skipped entirely.
        ControlLog lowLog;
        auto* low = new TestControl(lowLog);
        low->AddRef();
        CHECK(view->AddPersistentViewInputControl(low));
        low->Release();
        CHECK(!view->GZOnCharacter('c'));
        CHECK(persistLog.characters == 1 && lowLog.characters == 0);
        CHECK(view->RemoveAllPersistentViewInputControls());
        CHECK(!view->RemoveAllViewInputControls(true));
        CHECK(view->GetCurrentViewInputControl() == nullptr);
    }

    // ProcessKeyDown / ProcessKeyUp / GZOnMouseWheel dispatch to the tools.
    {
        ControlLog stackLog;
        auto* stacked = new TestControl(stackLog);
        stacked->AddRef();
        CHECK(view->SetCurrentViewInputControl(stacked, 0));
        stacked->Release();
        CHECK(!view->ProcessKeyDown(0x51, 0x2));
        CHECK(stackLog.keysDown == 1);
        View3DWinTestAccess::LastModifiers(*view) = 0;
        CHECK(!view->ProcessKeyUp(0x51, 0x4));
        CHECK(stackLog.keysUp == 1 && View3DWinTestAccess::LastModifiers(*view) == 0x4);
        stackLog.handleKeyDown = true;
        CHECK(view->ProcessKeyDown(0x52, 0));
        stackLog.handleKeyDown = false;
        CHECK(view->GZOnMouseWheel(10, 20, 0, 1));
        CHECK(stackLog.wheels == 1);
        // The wheel falls back to ZoomIn / ZoomOut (the camera sits at zoom 2, the limits are 0 / 5).
        camera->SetZoom(2.0f);
        const int zoomIns = renderer.zoomIn, zoomOuts = renderer.zoomOut;
        CHECK(view->GZOnMouseWheel(10, 20, 0, -1));
        CHECK(renderer.zoomOut == zoomOuts + 1);
        CHECK(view->GZOnMouseWheel(10, 20, 0, 1));
        CHECK(renderer.zoomIn == zoomIns + 1);
        CHECK(!view->RemoveAllViewInputControls(true));
    }

    // GZOnMouseExit: stops the edge scroll, updates the cursor text, tells the top tools.
    {
        ControlLog stackLog, persistLog;
        auto* stacked = new TestControl(stackLog);
        stacked->AddRef();
        auto* persistent = new TestControl(persistLog);
        persistent->AddRef();
        CHECK(view->AddPersistentViewInputControl(persistent));
        persistent->Release();
        CHECK(view->SetCurrentViewInputControl(stacked, 0));
        stacked->Release();
        View3DWinTestAccess::EdgeScrollX(*view) = 1;
        View3DWinTestAccess::EdgeScrollY(*view) = 1;
        host.cursorTextUpdates = 0;
        CHECK(view->GZOnMouseExit(nullptr));
        CHECK(View3DWinTestAccess::EdgeScrollX(*view) == 0 && View3DWinTestAccess::EdgeScrollY(*view) == 0);
        CHECK(host.cursorTextUpdates == 1 && host.lastCursorX == -10000 && host.lastCursorY == -10000);
        CHECK(stackLog.mouseExits == 1 && persistLog.mouseExits == 1);
        CHECK(view->RemoveAllPersistentViewInputControls());
        CHECK(!view->RemoveAllViewInputControls(true));
        View3DWinTestAccess::Scrolling(*view) = false;
    }

    // ------------------------------------------------------------------ picks
    {
        renderer.pickTerrainResult = true;
        float terrain[3] = {9.0f, 9.0f, 9.0f};
        CHECK(view->PickTerrain(10, 20, terrain, true));
        CHECK(renderer.terrainPicks == 1 && renderer.lastPickX == 10 && renderer.lastPickY == 20);
        CHECK(renderer.pickTerrainAlways == false && renderer.pickTerrainArg5 == true);
        CHECK(terrain[0] == 1.5f && terrain[1] == 2.5f && terrain[2] == 3.5f);
        renderer.pickTerrainResult = false;
        CHECK(!view->PickTerrain(0, 0, terrain, false));
        // No renderer: the out parameter gets two zeroed words, not three (quirk, 0x100782952).
        View3DWinTestAccess::Renderer(*view) = nullptr;
        terrain[0] = terrain[1] = terrain[2] = 9.0f;
        CHECK(!view->PickTerrain(0, 0, terrain, true));
        CHECK(terrain[0] == 0.0f && terrain[1] == 0.0f && terrain[2] == 9.0f);
        cISC4Occupant* occupantOut = reinterpret_cast<cISC4Occupant*>(0x1);
        CHECK(!view->PickOccupant(0, 0, occupantOut) && occupantOut == nullptr);
        View3DWinTestAccess::Renderer(*view) = &renderer;
        FakeOccupant occupant;
        FakeModelInstance model;
        model.owner = &occupant;
        renderer.pickResult = &model;
        CHECK(view->PickOccupant(3, 4, occupantOut));
        CHECK(occupantOut == reinterpret_cast<cISC4Occupant*>(&occupant));
        CHECK(model.releases == 1 && renderer.lastPickX == 3 && renderer.lastPickY == 4);
        // A model without an occupant answers false and is still released.
        FakeModelInstance bareModel;
        renderer.pickResult = &bareModel;
        CHECK(!view->PickOccupant(0, 0, occupantOut) && occupantOut == nullptr);
        CHECK(bareModel.releases == 1);
        renderer.pickResult = nullptr;
        renderer.pickTerrainResult = false;
    }

    // ------------------------------------------------------------------ global effect triggers
    host.server.sent.clear();
    CHECK(!view->GetGlobalEffectTriggerState(0xabc));
    view->AssertGlobalEffectTrigger(0xabc);
    CHECK(view->GetGlobalEffectTriggerState(0xabc));
    view->AssertGlobalEffectTrigger(0xabc);                       // a second holder
    CHECK(host.server.sent.size() == 1 && host.server.sent[0] == kMsgViewGlobalEffectTrigger);
    CHECK(host.server.lastData1 == 0xabc && host.server.lastData2 == 1);
    view->RetractGlobalEffectTrigger(0xabc);
    CHECK(view->GetGlobalEffectTriggerState(0xabc));               // still one holder
    view->RetractGlobalEffectTrigger(0xabc);
    CHECK(!view->GetGlobalEffectTriggerState(0xabc));
    CHECK(host.server.sent.size() == 2 && host.server.lastData2 == 0);
    view->RetractGlobalEffectTrigger(0xabc);                       // nothing to retract

    // ------------------------------------------------------------------ view filter / level information
    {
        auto* filterA = new FakeOccupant();                        // any refcounted object
        auto* filterB = new FakeOccupant();
        filterA->AddRef();
        filterB->AddRef();
        cSC4ViewFilterInformation info;
        view->GetCurrentViewFilterInfo(info);
        CHECK(info.filter == nullptr);
        info.filter = filterA;
        host.server.sent.clear();
        CHECK(view->SetCurrentViewFilterInfo(info));
        CHECK(View3DWinTestAccess::FilterInfo(*view) == filterA);
        CHECK(filterA->refs == 2);                                 // ours + the view's
        CHECK(host.server.sent.size() == 2);
        CHECK(host.server.sent[0] == kMsgViewFilterInfo && host.server.sent[1] == kMsgViewFilterInfoDone);
        cSC4ViewFilterInformation current;
        view->GetCurrentViewFilterInfo(current);
        CHECK(current.filter == filterA && filterA->refs == 3);    // the out parameter takes a reference
        if (current.filter) current.filter->Release();
        info.filter = filterB;
        CHECK(view->SetCurrentViewFilterInfo(info));
        CHECK(filterA->refs == 1 && filterB->refs == 2);
        // The level information sends the pair only when the level changes.
        host.server.sent.clear();
        cSC4ViewLevelInformation level;
        view->GetCurrentViewLevelInfo(level);
        CHECK(level.level == View3DWinTestAccess::ViewLevel(*view));
        level.level = 4;
        view->SetCurrentViewLevelInfo(level);
        CHECK(view->GetCurrentViewLevel() == 4);
        CHECK(host.server.sent.size() == 2);
        CHECK(host.server.sent[0] == kMsgViewLevelInfo && host.server.sent[1] == kMsgViewLevelInfoDone);
        host.server.sent.clear();
        view->SetCurrentViewLevel(4);                              // unchanged
        CHECK(host.server.sent.empty());
        view->SetCurrentViewLevel(2);
        CHECK(view->GetCurrentViewLevel() == 2 && host.server.sent.size() == 2);
        view->SetCurrentViewLevelInfo(level);                      // back to 4
        filterA->Release();
        filterB->Release();
        View3DWinTestAccess::FilterInfo(*view)->Release();
        View3DWinTestAccess::FilterInfo(*view) = nullptr;
    }

    // ------------------------------------------------------------------ DoMessage(cGZMessage&)
    {
        cGZMessage message;
        // Mouse enter / exit / move refresh the 24-hour clock hover and the focus byte; the message then
        // falls through to cGZWin::DoMessage, which dispatches it again (0x100784d1e).
        View3DWinTestAccess::FocusState(*view) = 0xAA;
        message.type = kMsgViewMouseEnter;
        host.hoverUpdates = 0;
        CHECK(!view->DoMessage(message));
        CHECK(View3DWinTestAccess::FocusState(*view) == 1 && host.hoverUpdates == 1);
        message.type = kMsgViewMouseExit;
        CHECK(view->DoMessage(message));                           // GZOnMouseExit answers true
        CHECK(View3DWinTestAccess::FocusState(*view) == 0 && host.hoverUpdates == 2);
        View3DWinTestAccess::FocusState(*view) = 0x55;
        message.type = kMsgViewMouseMove;
        CHECK(!view->DoMessage(message));
        CHECK(host.hoverUpdates == 3);
        CHECK(host.uiFilters == 3);

        // Focus: clears every scroll input and leaves the mouse.
        host.cityExists = true;
        View3DWinTestAccess::KeyboardScrollX(*view) = 1;
        View3DWinTestAccess::KeyboardScrollY(*view) = 1;
        View3DWinTestAccess::EdgeScrollX(*view) = 1;
        View3DWinTestAccess::EdgeScrollY(*view) = 1;
        View3DWinTestAccess::Scrolling(*view) = true;
        host.cursorTextUpdates = 0;
        message.type = kMsgViewFocus;
        CHECK(view->DoMessage(message));
        CHECK(View3DWinTestAccess::KeyboardScrollX(*view) == 0 && View3DWinTestAccess::KeyboardScrollY(*view) == 0);
        CHECK(View3DWinTestAccess::EdgeScrollX(*view) == 0 && View3DWinTestAccess::EdgeScrollY(*view) == 0);
        CHECK(host.cursorTextUpdates == 1);
        View3DWinTestAccess::Scrolling(*view) = false;
        View3DWinTestAccess::Stopping(*view) = false;

        // A catalog command message.
        message = cGZMessage{};
        message.type = kMsgViewCommand;
        message.param1 = kCmdViewCatalogItem;
        message.param2 = 0x1234;
        message.param3 = 0x5678;
        CHECK(view->DoMessage(message));
        CHECK(host.catalogInvokes == 1 && host.catalogItem == 0x1234 && host.catalogParam == 0x5678);
        CHECK(host.catalogFlag && mgr.focus == static_cast<cIGZWin*>(static_cast<cGZWin*>(view)));

        // The cheat-code message hands the command line of the string in param2 to the cheat handler.
        cRZString cheatText("GOL");
        message = cGZMessage{};
        message.type = kMsgViewCheatCode;
        message.param1 = 0x8973a69c;
        message.param2 = reinterpret_cast<std::uint64_t>(static_cast<cIGZString*>(&cheatText));
        CHECK(view->DoMessage(message));
        CHECK(host.cheatRuns.size() == 1 && host.cheatRuns[0] == 0x8973a69c);
        CHECK(host.cheatTexts.size() == 1 && host.cheatTexts[0] == "GOL");

        // The key message: param2 selects down / down-with-modifier / up, param3 points at the key record.
        std::uint32_t keyRecord[5] = {0, 0, 0x51, 0, 0x2};
        CHECK(view->GetKeyAccelerator() == nullptr);
        message = cGZMessage{};
        message.type = kMsgViewKey;
        message.param2 = 5;
        message.param3 = reinterpret_cast<std::uint64_t>(keyRecord);
        CHECK(!view->DoMessage(message));                          // no tool, no accelerator
        View3DWinTestAccess::LastModifiers(*view) = 0;
        message.param2 = 6;                                        // key up
        CHECK(!view->DoMessage(message));
        CHECK(View3DWinTestAccess::LastModifiers(*view) == 0x2);
        // With an accelerator the command is dispatched and the message is handled.
        view->SetKeyAccelerator(&host.accelerator);
        host.accelerator.found = true;
        host.accelerator.tests = 0;
        message.param2 = 4;                                        // key down with 0x20000000 added
        host.acceleratorCommands = 0;
        CHECK(view->DoMessage(message));
        CHECK(host.accelerator.tests == 1);
        CHECK(host.accelerator.lastKey == 0x51);
        CHECK(host.accelerator.lastModifiers == (0x2u | kKeyModifierKeyUp));
        CHECK(host.accelerator.lastDown);
        CHECK(host.acceleratorCommands == 1);
        host.accelerator.found = false;
        CHECK(!view->DoMessage(message));
        view->SetKeyAccelerator(nullptr);

        // Advice links and the HTML link message.
        message = cGZMessage{};
        host.adviceLinks = 0;
        for (std::uint32_t type : {kMsgViewAdviceLink, kMsgViewAdviceLink2, kMsgViewAdviceLink3}) {
            message.type = type;
            view->DoMessage(message);
        }
        CHECK(host.adviceLinks == 3);
        host.urlHandles = 0;
        host.urlResult = false;
        message = cGZMessage{};
        message.type = kMsgViewHtmlLink;
        message.param2 = reinterpret_cast<std::uint64_t>(static_cast<cIGZString*>(&cheatText));
        CHECK(view->DoMessage(message));                           // DoURL returned false -> handled
        CHECK(host.urlHandles == 1);
        host.urlResult = true;
        CHECK(!view->DoMessage(message));                          // DoURL returned true -> not handled (quirk)

        // An unhandled type falls through to cGZWin::DoMessage.
        message = cGZMessage{};
        message.type = 0x7777;
        CHECK(!view->DoMessage(message));
        host.cityExists = false;
    }

    // ------------------------------------------------------------------ GZPaint
    {
        View3DWinTestAccess::PaintEnabled(*view) = true;
        View3DWinTestAccess::FrameTimeThreshold(*view) = 0.0f;      // run the frame block every time
        View3DWinTestAccess::Scrolling(*view) = false;
        host.costUpdates = 0;
        host.frameMarkers = 0;
        renderer.draws = 0;
        CHECK(view->GZPaint());
        CHECK(host.costUpdates == 1 && host.lastCostDt >= 0.0f && host.lastCostDt <= 0.5f);
        CHECK(host.frameMarkers == 1 && host.lastFrameMarker == host.frameCounter);
        CHECK(renderer.draws == 1 && renderer.drawContextMarker == host.frameCounter);
        // The frame only resumes the simulation once the hidden timeout has passed (2 s by default, so
        // the pause started above survives this frame).
        View3DWinTestAccess::SimHidden(*view) = false;
        view->DoTemporarySimHiddenPause();
        CHECK(View3DWinTestAccess::SimHidden(*view));
        CHECK(view->GZPaint());
        CHECK(View3DWinTestAccess::SimHidden(*view));
        // A deferred message is drained only when popups are safe.
        auto* deferred = new cRZMessage2Standard();
        deferred->SetType(0x5150);
        View3DWinTestAccess::Deferred(*view).push_back(cRZAutoRefCount<cIGZMessage2>(deferred));
        host.popupSafe = false;
        host.server.posted.clear();
        CHECK(view->GZPaint() && host.server.posted.empty());
        CHECK(View3DWinTestAccess::Deferred(*view).size() == 1);
        host.popupSafe = true;
        host.popupEnabled = true;
        CHECK(view->GZPaint());
        CHECK(View3DWinTestAccess::Deferred(*view).empty());
        CHECK(host.server.posted.size() == 1 && host.server.posted[0] == 0x5150);
        host.popupSafe = false;
        host.popupEnabled = false;
        // A scrolling frame moves the camera by the angle / speed and consumes the stop speed.
        View3DWinTestAccess::Scrolling(*view) = true;
        View3DWinTestAccess::Stopping(*view) = true;
        View3DWinTestAccess::ScrollSpeed(*view) = 0.5f;
        View3DWinTestAccess::ScrollStopSpeed(*view) = 0.25f;
        View3DWinTestAccess::ScrollAngle(*view) = 0.0f;
        host.gameMessages.clear();
        const int draws = renderer.draws;
        CHECK(view->GZPaint());
        CHECK(renderer.draws == draws + 1);
        CHECK(std::fabs(View3DWinTestAccess::ScrollSpeed(*view) - 0.25f) < 1e-6f);
        CHECK(View3DWinTestAccess::Scrolling(*view));               // not finished yet
        CHECK(view->GZPaint());
        CHECK(!View3DWinTestAccess::Scrolling(*view));              // the stop speed caught up
        CHECK(host.gameMessages.size() == 1 && host.gameMessages[0] == kMsgViewScrollEnd);
        View3DWinTestAccess::Stopping(*view) = false;
        View3DWinTestAccess::PaintEnabled(*view) = false;
        View3DWinTestAccess::SimHidden(*view) = false;              // do not carry it into Shutdown
    }

    // ------------------------------------------------------------------ Shutdown
    {
        const int removeAllBefore = host.notifications.removeAll;
        const int unregisterBefore = host.unregisterTables;
        const int onShutdownBefore = host.onView3DShutdown;
        host.unregisteredCheats.clear();
        host.removeTargets = 0;
        host.progress.refs = 2;                                    // the host's own reference + the view's
        renderer.shutdowns = 0;
        const int coverageShutdowns = host.coverage.shutdowns;
        const int lotConfigBefore = host.lotConfigShutdowns;
        const int guiRemovalsBefore = host.guiRemovals;
        const int progressShutdownsBefore = host.progressShutdowns;
        CHECK(view->Shutdown());
        CHECK(host.notifications.removeAll == removeAllBefore + 1);
        CHECK(host.unregisterTables == unregisterBefore + 1);
        CHECK(host.removeTargets == 1);
        CHECK(host.unregisteredCheats.size() == 13 && host.unregisteredCheats[0] == kCheatGameOfLife);
        CHECK(host.lotConfigShutdowns == lotConfigBefore + 1 && host.guiRemovals == guiRemovalsBefore + 1);
        CHECK(host.progressShutdowns == progressShutdownsBefore + 1);
        CHECK(host.onView3DShutdown == onShutdownBefore + 1);
        CHECK(renderer.shutdowns == 1);
        CHECK(host.coverage.shutdowns == coverageShutdowns + 1);
        CHECK(view->GetKeyAccelerator() == nullptr);
        CHECK(view->GetCoverageHelper() == &host.coverage);        // the helper object lives in the window
        CHECK(View3DWinTestAccess::Tools(*view).empty() && View3DWinTestAccess::Persistent(*view).empty());
        CHECK(View3DWinTestAccess::Camera(*view) == nullptr);       // released; the camera deleted itself
        CHECK(View3DWinTestAccess::Renderer(*view) == nullptr);
        // A second Shutdown is a no-op.
        CHECK(view->Shutdown() && host.notifications.removeAll == removeAllBefore + 1);
    }
    delete view;

    // ------------------------------------------------------------------ without a host
    SetView3DWinHost(nullptr);
    auto* orphan = new cSC4View3DWin(&mgr);
    CHECK(orphan->Init());                                         // cGZWin::Init runs; no service is touched
    CHECK(!orphan->SetZoom(1));
    CHECK(!orphan->RotateLeft() && !orphan->RotateRight() && !orphan->SetRotation(1));
    CHECK(!orphan->ZoomIn() && !orphan->ZoomOut() && !orphan->CanZoomIn() && !orphan->CanZoomOut());
    cISC4Occupant* orphanOccupant = nullptr;
    CHECK(!orphan->PickOccupant(0, 0, orphanOccupant) && orphanOccupant == nullptr);
    orphan->RecalcScrollVelocity();                                 // no inputs, no camera: no crash
    orphan->DoScrollBegin();
    orphan->DoScrollEnd();
    orphan->SetScrolling(true, 0.0f, 0.0f);
    orphan->SetScrolling(false, 0.0f, 0.0f);
    orphan->KillKeyboardScrolling();
    orphan->DoTemporarySimHiddenPause();
    orphan->DoTemporarySimHiddenResume();
    CHECK(orphan->ScrollStop());
    cSC4ViewLevelInformation level;
    level.level = 3;
    orphan->SetCurrentViewLevelInfo(level);
    CHECK(orphan->GetCurrentViewLevel() == 3);
    cSC4ViewFilterInformation info;
    orphan->GetCurrentViewFilterInfo(info);
    CHECK(info.filter == nullptr);
    orphan->AssertGlobalEffectTrigger(7);
    CHECK(orphan->GetGlobalEffectTriggerState(7));
    orphan->RetractGlobalEffectTrigger(7);
    CHECK(!orphan->GetGlobalEffectTriggerState(7));
    cGZMessage message;
    message.type = 0x7777;
    CHECK(!orphan->DoMessage(message));
    CHECK(orphan->Shutdown());
    delete orphan;

    std::printf(failures ? "FAILED\n" : "sc4_view3dwin ok\n");
    return failures ? 1 : 0;
}
