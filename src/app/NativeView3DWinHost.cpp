// The native IView3DWinHost (started from scripts/gen_stub_hosts.py's neutral host, now hand edited and skipped by the
// generator): the city exists once the view is up, the zoom stops at the last level, the right-button scroll tool
// is the recovered cSC4ViewInputControlCityScroll, and AddGUI builds its windows through the UI-script service and
// nSC4UI's window-set helpers. Everything else is still neutral.
#include <string>
#include <vector>
#include "sc4/View3DWin.h"
#include "sc4/ViewInputControlCityScroll.h"
#include "app/NativeUi.h"
#include "gz/Buffer.h"
#include "sc4/SC4UI.h"

namespace native {
namespace {
using namespace sc4;
struct StubView3DWin final : IView3DWinHost {
    cIGZMessageServer2* MessageServer2() override { return {}; }
    std::uint8_t* Preferences() override { return {}; }
    bool CityExists() override { return true; }
    void PostGameMessage(std::uint32_t type) override {}
    INotificationHelper3D* NotificationHelper() override { return {}; }
    void RegisterCommandTable() override {}
    void UnregisterCommandTable() override {}
    void AddCheatCodeTarget(cIGZWin* target) override {}
    void RemoveCheatCodeTarget(cIGZWin* target) override {}
    bool RegisterCheatCode(std::uint32_t id, const char* text) override { return {}; }
    bool UnregisterCheatCode(std::uint32_t id) override { return {}; }
    bool CheatCodeManagerPresent() override { return {}; }
    bool LotPlopCheatsEnabled() override { return {}; }
    cIGZWinKeyAccelerator* CreateKeyAccelerator() override { return {}; }
    cSC4RenderProperties* GlobalRenderProperties() override { return {}; }
    cSC4RenderProperties* AppRenderProperties() override { return {}; }
    void SetScrollLightingHighlight() override {}
    void RestoreLighting() override {}
    int VirtualZoomLimit() override { return 4; }
    bool ProgressScreenWanted() override { return {}; }
    cIGZUnknown* CreateProgressScreen(cIGZWin* parent, const cRZRect& area, bool flag) override { return {}; }
    void InitProgressScreen(cIGZUnknown* screen) override {}
    void SetProgressCaption(cIGZUnknown* screen, int id, const cIGZString& text) override {}
    void ShutdownProgressScreen(cIGZUnknown* screen) override {}
    void ShutdownValidationWindow(cIGZUnknown* window) override {}
    void ShutdownLotConfigurationEditors() override {}
    void ShutdownModelMaker(cIGZUnknown* modelMaker) override {}
    void ShutdownCameraObject(cIGZUnknown* camera) override {}
    void ShutdownSimulationClock(cIGZUnknown* clock) override {}
    bool UserDataDirectory(cIGZString& out) override { return {}; }
    cIGZWin* CreateWinAnimationRecorder() override { return {}; }
    void RecorderSetAnimationOutputDirectory(cIGZWin* recorder, const cIGZString& directory, bool create) override {}
    ICityMouseRouter* CityMouseRouter() override { return {}; }
    bool CanInvokeZoningTool() override { return true; }   // an established city: the start panel stays closed
    ITerrainEditor* TerrainEditor() override { return {}; }
    bool GraphicsSystemAllowsSelfScroll() override { return {}; }
    void RunChangeTerrainDialog(int& type) override {}
    std::uint32_t GetWindowChildrenSet(cIGZWin* window, std::set<cIGZWin*>& children) override { return nSC4UI::GetWindowChildrenSet(window, children); }
    std::uint32_t GetWindowChildrenNotInSet(cIGZWin* window, const std::set<cIGZWin*>& excluded, std::set<cIGZWin*>& out) override { return nSC4UI::GetWindowChildrenNotInSet(window, excluded, out); }
    void AlignWindowSetToWindow(const std::set<cIGZWin*>& windows, cIGZWin* parent) override { nSC4UI::AlignWindowSetToWindow(windows, parent); }
    void ClearAcceptFocusFlagRecursive(cIGZWin* window) override { nSC4UI::ClearAcceptFocusFlagRecursive(window); }
    cIGZWin* CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent, std::uint32_t id) override { return native::CreateWindowFromScript(key, parent, id); }
    bool CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent, std::uint32_t id, std::uint32_t iid, void** out) override {
        cIGZWin* window = native::CreateWindowFromScript(key, parent, id);
        if (!window) return false;
        const bool ok = window->QueryInterface(iid, out);
        window->Release();
        return ok;
    }
    cIGZBuffer* LoadImageBuffer(std::uint32_t, std::uint32_t group, std::uint32_t instance) override { return native::LoadBitmapResource(group, instance); }
    cIGZWin* CreateToolbarButton() override { return {}; }
    cIGZWin* CreateCompassWindow() override { return {}; }
    void LocalisedString(std::uint32_t group, std::uint32_t instance, cRZString& out) override {}
    gz::cIGZWinProc* CreatePanelUIWinProc() override { return {}; }
    INewsTickerWindow* CreateNewsTickerWinProc() override { return {}; }
    IMySimWindow* CreateMySimWinProc(gz::cIGZWinGen* panel) override { return {}; }
    IBudgetUIWindow* CreateBudgetUIWinProc() override { return {}; }
    IGraphsViewWindow* CreateGraphsViewWinProc(cIGZWin* window) override { return {}; }
    IBuildingSelectWindow* CreateBuildingSelectWinProc(cIGZWin* window) override { return {}; }
    IAdvisorPopupQueue* CreateAdvisorPopupTracker() override { return {}; }
    void AttachCompassWinProc(gz::cIGZWinGen* window) override {}
    cISC4WinCalloutBox* CreateCalloutBox() override { return {}; }
    gz::cIGZWinBMP* CreateWinBMP() override { return {}; }
    void InitMapView(cIGZWin* window) override {}
    bool IsKeyDownNow(std::uint32_t key) override { return {}; }
    void ExecuteCommand(std::uint32_t command) override {}
    bool ShowEstablishCityDialog() override { return {}; }
    bool DisasterAlertActive() override { return {}; }
    bool SituationAlertActive() override { return {}; }
    bool SimulatorBusy() override { return {}; }
    cIGZBuffer* CreateImageBuffer() override { return {}; }
    IAdvisorWindow* AdvisorWindow() override { return {}; }
    cSC4ViewInputControl* CreateZoningTool(std::uint32_t type) override { return {}; }
    cSC4ViewInputControl* CreateTerrainScrubberTool(std::uint32_t type, bool flag) override { return {}; }
    cSC4ViewInputControl* CreateLevelTerrainTool() override { return {}; }
    cSC4ViewInputControl* CreateCityScrollTool() override { return new cSC4ViewInputControlCityScroll(); }
    void CityScrollSetMouseScroll(cSC4ViewInputControl* tool, int x, int y) override {
        if (auto* scroll = dynamic_cast<cSC4ViewInputControlCityScroll*>(tool)) scroll->SetMouseScroll(x, y);
    }
    void PlayGlobalTerrainEffectSound() override {}
    int AddContourCallback() override { return {}; }
    void RemoveContourCallback(int handle) override {}
    bool ProjectPointToViewport(cS3DVector3& point) override { return {}; }
    void UpdateCostIndicator(float dt) override {}
    bool PopupModalDialogsSafe() override { return {}; }
    bool PopupModalDialogsEnabled() override { return {}; }
    std::uint32_t SimulationFrameCounter() override { return {}; }
    bool FilterUiCommandMessage(cGZMessage&) override { return true; }   // no tutorial running: every message is kept
    bool HandleUrl(cIGZString* link) override { return {}; }
    void HandleAdviceLink(cGZMessage& message) override {}
    void ProcessCheatCode(std::uint32_t id, const char* cmdLine) override {}
    void InvokeCatalogItemCommand(std::uint32_t item, std::uint32_t param, bool flag) override {}
    void ExecuteAcceleratorCommand(std::uint32_t command) override {}
    void Update24HourClockHover() override {}
    void UpdateCursorTextPositions(int x, int y) override {}
    void SetDrawContextFrameMarker(void* drawContext, std::uint32_t frame) override {}
    void OnView3DCreate(cISC4View3DWin* view) override {}
    void OnView3DShutdown() override {}
    void GetCityName(cIGZString& out) override {}
    int VisualTimeMode() override { return {}; }
    int CityPopulation() override { return {}; }
    std::int64_t CityFunds() override { return {}; }
    void FormatNumber(std::int64_t value, cIGZString& out) override {}
    void MakeMoneyString(std::int64_t value, cIGZString& out) override {}
    std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { return {}; }
    bool BudgetDialogOpen(cIGZUnknown* dialog) override { return {}; }
    void RefreshBudgetDialog(cIGZUnknown* dialog) override {}
    void ShutdownNewsTickerWinProc(cIGZUnknown* proc) override {}
    void ShutdownMySimWinProc(cIGZUnknown* proc) override {}
    void ShutdownBudgetUIWinProc(cIGZUnknown* proc) override {}
    void ShutdownGraphsViewWinProc(cIGZUnknown* proc) override {}
    void ShutdownBuildingSelectWinProc(cIGZUnknown* proc) override {}
};
}  // namespace

void InstallView3DWinStub() { static StubView3DWin s; sc4::SetView3DWinHost(&s); }

}  // namespace native
