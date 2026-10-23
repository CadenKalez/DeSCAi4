// cSC4ViewInputControlQuery: lifecycle / message plumbing, the query state machine, the dialog control helpers,
// picking, and the SetOrGet rows that do not need an unrecovered simulator.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fake_occupant.h"
#include "gz/Win.h"
#include "gz/WinBMP.h"
#include "gz/WinSlider.h"
#include "gz/WinText.h"
#include "gz/WinTextEdit.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/SC4Utilities.h"
#include "sc4/SCProperty.h"
#include "sc4/ViewInputControlQuery.h"
#include "vic_fakes.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;
using gz::cIGZWinText;
using gz::cIGZWinTextEdit;
using gz::cIGZWinSlider;
using gz::cIGZWinBMP;

namespace {

// ---- the language formatter SC4MakeMoneyString goes through -------------------------------------------------
struct Formatter final : ILanguageFormatter {
    void FormatMoney(std::int64_t value, cIGZString& out, const cRZString&) override {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%s", std::to_string(value).c_str());
        out.AssignFromCString(buffer);
    }
    void FormatMoney(double value, cIGZString& out, const cIGZString&, const cRZString&) override {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%.2f", value);
        out.AssignFromCString(buffer);
    }
    void FormatNumber(std::int64_t value, cRZString& out) override { out.AssignFromCString(std::to_string(value).c_str()); }
};

// ---- window manager: DestroyWindow really unparents (ChildDeleteAll loops until the child is gone) -------------
struct Mgr final : cIGZWinMgr {
    vic_fakes::CursorMgr cursors;
    cIGZWin* capture = nullptr;
    cIGZWin* setCapture = nullptr;
    cIGZWin* released = nullptr;
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    cIGZWin* GetMainWindow() override { return nullptr; }
    void AppendToPlotStrategy(cIGZWin*) override {}
    void Plot() override {}
    void PostMessageToTarget(cIGZWin*, const cGZMessage&) override {}
    void AddWindowToValidList(cIGZWin*) override {}
    void RemoveWindowFromValidList(cIGZWin*) override {}
    void CleanUpWindowReferences(cIGZWin*) override {}
    bool DestroyWindow(cIGZWin* w) override {
        if (!w) return true;
        if (cIGZWin* parent = w->GetParentWin()) parent->ChildRemove(w);
        return true;
    }
    bool IsWindowValid(cIGZWin*) override { return true; }
    cIGZWin* GetWindowFromPoint(int, int) override { return nullptr; }
    void SetCurrentMouseWin(cIGZWin*) override {}
    cIGZWin* GZGetCapture() override { return capture; }
    void GZSetCapture(cIGZWin* w) override { setCapture = capture = w; }
    void GZReleaseCapture(cIGZWin* w) override { released = w; capture = nullptr; }
    cIGZCursorManager* GetCursorManager() override { return &cursors; }
    cIGZWin* GZGetFocus() override { return nullptr; }
    void GZSetFocus(cIGZWin*) override {}
    cIGZKeyboard* GetKeyboard() override { return nullptr; }
};

// ---- a property holder that answers one bool property -----------------------------------------------------
struct Holder final : cISCPropertyHolder {
    cSCProperty property;
    std::uint32_t id = 0;
    bool value = false;
    std::vector<std::uint32_t> values;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool HasProperty(std::uint32_t propertyId) const override { return propertyId == id; }
    bool GetPropertyList(cIGZUnknownList**) const override { return false; }
    cISCProperty* GetProperty(std::uint32_t propertyId) const override {
        if (propertyId != id) return nullptr;
        auto* self = const_cast<Holder*>(this);
        cRZVariant v;
        if (!values.empty()) v.RefUint32(values.data(), static_cast<std::uint32_t>(values.size()));
        else v.SetValBool(value);
        self->property.SetPropertyID(id);
        self->property.SetPropertyValue(v);
        return &self->property;
    }
    bool GetProperty(std::uint32_t, std::uint32_t&) const override { return false; }
    bool GetProperty(std::uint32_t, cIGZString&) const override { return false; }
    bool GetProperty(std::uint32_t, std::uint32_t, void**) const override { return false; }
    bool GetProperty(std::uint32_t, void*, std::uint32_t&) const override { return false; }
    void AddProperty(cISCProperty*, bool) override {}
    void AddProperty(std::uint32_t, const cIGZVariant*, bool) override {}
    void AddProperty(std::uint32_t, std::uint32_t, bool) override {}
    void AddProperty(std::uint32_t, const cIGZString&, bool) override {}
    void AddProperty(std::uint32_t, cIGZUnknown*, bool) override {}
    void AddProperty(std::uint32_t, const void*, std::uint32_t, bool) override {}
    void CopyAddProperty(cISCProperty*, bool) override {}
    bool RemoveProperty(std::uint32_t, bool) override { return false; }
    void RemoveAllProperties(bool) override {}
    void EnumProperties(void (*)(cISCProperty*, void*), void*) const override {}
    void EnumProperties(bool (*)(cISCProperty*, void*), void (*)(cISCProperty*, void*), void*) const override {}
    void CompactProperties() override {}
};

struct TestOccupant final : cISC4Occupant, IQueryBuildingOccupant {
    std::uint32_t type = 0x278128a0;
    int highlight = 0;
    int highlightSets = 0;
    int names = 0;
    std::string name = "Test Building";
    std::string lastSetName;
    Holder* holder = nullptr;

    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 0x87dda3a9) { *out = static_cast<IQueryBuildingOccupant*>(this); return true; }
        if (iid == 0x26bb767e) { *out = static_cast<cISC4Occupant*>(this); return true; }
        return false;
    }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool IsInitialized() const override { return true; }
    cISCPropertyHolder* AsPropertyHolder() override { return holder; }
    std::uint32_t GetType() const override { return type; }
    void GetPosition(float* xyz) override { xyz[0] = 64.0f; xyz[1] = 0.0f; xyz[2] = 128.0f; }
    void SetPosition(const float*) override {}
    void GetBoundingBox(float*, float*) override {}
    void GetBoundingCityCells(SC4CellRect&) override {}
    void SetRemovalFlags(int) override {}
    void UnsetRemovalFlags(int) override {}
    bool CanRemove(int) override { return true; }
    void PostOccupantMessage(std::uint32_t, std::uint32_t) override {}
    std::uint32_t GetHighlight() const override { return static_cast<std::uint32_t>(highlight); }
    void SetHighlight(int h, bool) override { highlight = h; ++highlightSets; }
    void SetVisibility(bool, bool) override {}
    cISC43DPlaceableObject* GetPlaceableObject() const override { return nullptr; }
    cISC43DPlaceableObject* GetOrCreatePlaceableObject() override { return nullptr; }
    void SetPlaceableObject(cISC43DPlaceableObject*) override {}
    bool IsOccupantGroup(std::uint32_t) const override { return true; }
    void AddOccupantGroup(std::uint32_t) override {}
    bool GetOccupantGroups(std::set<std::uint32_t>&) const override { return false; }
    bool GetOccupantManagerBBox(std::uint8_t*) const override { return false; }
    void SetOccupantManagerBBox(const std::uint8_t*) override {}
    bool GetLotTag(std::uint32_t&) const override { return false; }
    void SetLotTag(std::uint32_t) override {}
    void SetFlag(std::uint32_t) override {}
    void SetAllFlags(std::uint32_t) override {}
    void ClearFlag(std::uint32_t) override {}
    bool IsFlagSet(std::uint32_t) override { return false; }
    std::uint32_t GetFlags() override { return 0; }

    void SetName(cIGZString& n) override { ++names; lastSetName = n.ToChar(); }
    bool GetName(cRZString& n) override { n.AssignFromCString(name.c_str()); return true; }
};

struct Dummy final : cIGZUnknown {
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 0x26bb767e || iid == 0xa9b40f05 || iid == 0xe9793a65) { *out = this; return true; }
        return false;
    }
    std::uint32_t AddRef() override { return 2; }
    std::uint32_t Release() override { return 1; }
};

// ---- the view ---------------------------------------------------------------------------------------------
struct View final : IQueryView {
    int refs = 1;
    cIGZWin* window = nullptr;
    bool showCoordinates = false;
    bool pickOk = true;
    float terrain[3] = {10.0f, 1.0f, 20.0f};
    int tips = 0, clears = 0, coverage = 0, ended = -1;
    std::string lastTitle, lastText;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void EndInput(int result) override { ended = result; }
    cSC4ViewInputControl* TopControl() override { return nullptr; }
    cIGZWin* ViewWindow() override { return window; }
    bool PickTerrain(int, int, float out[3], bool) override {
        out[0] = terrain[0]; out[1] = terrain[1]; out[2] = terrain[2];
        return pickOk;
    }
    bool ShowCoordinates() override { return showCoordinates; }
    void SetTip(std::uint32_t, int, const cIGZString* title, const cIGZString* text, int) override {
        ++tips;
        lastTitle = title ? title->ToChar() : "";
        lastText = text ? text->ToChar() : "";
    }
    void ClearTip(std::uint32_t) override { ++clears; }
    void RecalcCoverage() override { ++coverage; }
};

// ---- the callout box --------------------------------------------------------------------------------------
struct Callout final : cISC4WinCalloutBox {
    int refs = 1;
    cIGZWin* win = nullptr;
    std::string title, text;
    std::uint32_t textStyle = 0, titleStyle = 0;
    cRZRect avoid{};
    cRZPoint point{};
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    cIGZWin* AsIGZWin() override { return win; }
    void SetBackgroundImage(cIGZBuffer*, int, int) override {}
    cIGZBuffer* GetBackgroundImage() const override { return nullptr; }
    void SetTitle(const cIGZString& t) override { title = t.ToChar(); }
    const cIGZString& GetTitle() const override { return title_; }
    void SetText(const cIGZString& t) override { text = t.ToChar(); }
    const cIGZString& GetText() const override { return text_; }
    void SetTitleFontStyle(std::uint32_t style) override { titleStyle = style; }
    void SetTextFontStyle(std::uint32_t style) override { textStyle = style; }
    void SetIcon(cIGZBuffer*) override {}
    cIGZBuffer* GetIcon() const override { return nullptr; }
    void SetIconMeter(cIGZBuffer*, std::uint32_t, float) override {}
    void SetPointOfInterest(const cRZPoint& p) override { point = p; }
    const cRZPoint& GetPointOfInterest() const override { return point; }
    void SetAvoidanceRect(const cRZRect& r) override { avoid = r; }
    const cRZRect& GetAvoidanceRect() const override { return avoid; }
    void SetConstraintRect(const cRZRect&) override {}
    const cRZRect& GetConstraintRect() const override { return avoid; }

private:
    cRZString title_, text_;
};

// ---- picking ----------------------------------------------------------------------------------------------
struct Filter final : IOccupantFilter {
    int refs = 1;
    int kind = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool IsValid(const cISC4Occupant*) override { return true; }
};

struct PickResult final : IPickResult {
    int refs = 1;
    cIGZUnknown* object = nullptr;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    cIGZUnknown* GetObject() override { return object; }
};

struct FakeRenderer final : IQueryRenderer {
    cIGZUnknown* nextObject = nullptr;
    int picks = 0, filters = 0, layersAdded = 0, layersRemoved = 0;
    Filter* lastFilter = nullptr;
    int lastX = 0, lastY = 0;
    IOccupantFilter* CreateFilter(int kind) override {
        ++filters;
        auto* f = new Filter();
        f->kind = kind;
        lastFilter = f;
        return f;
    }
    IPickResult* Pick(int x, int y, IOccupantFilter*) override {
        ++picks;
        lastX = x;
        lastY = y;
        if (!nextObject) return nullptr;
        auto* r = new PickResult();
        r->object = nextObject;
        return r;
    }
    void AddLayer(cIGZUnknown*) override { ++layersAdded; }
    void RemoveLayer(cIGZUnknown*) override { ++layersRemoved; }
};

// ---- lots -------------------------------------------------------------------------------------------------
struct Lot final : IQueryLot {
    int refs = 1;
    SC4CellRect rect{};
    int state = 0;
    int reason = 0;
    bool historical = false;
    std::vector<cISC4Occupant*> occupants;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void GetOccupants(std::vector<cISC4Occupant*>& out) override { out = occupants; }
    void GetCellRect(SC4CellRect& out) override { out = rect; }
    int GetState() override { return state; }
    bool IsHistorical() override { return historical; }
    void SetHistorical(bool on) override { historical = on; }
    int GetAbandonReason() override { return reason; }
};

struct LotMgr final : IQueryLotManager {
    Lot* byOccupant = nullptr;
    Lot* byCell = nullptr;
    int occupantLookups = 0, cellLookups = 0;
    IQueryLot* GetLotForOccupant(cISC4Occupant*) override { ++occupantLookups; return byOccupant; }
    IQueryLot* GetLotAtCell(int, int, int) override { ++cellLookups; return byCell; }
};

// ---- the simulators the ported rows use -------------------------------------------------------------------
struct Power final : IQueryPowerSim { bool powered = false; bool IsPowered(cISC4Occupant*) override { return powered; } };
struct Plumbing final : IQueryPlumbingSim { bool watered = false; bool IsWatered(cISC4Occupant*) override { return watered; } };
struct Aura final : IQueryAuraSim { std::int8_t value = 0; std::int8_t GetAura(int, int) override { return value; } };
struct Sim final : IQuerySimulator { std::uint32_t frame = 0; std::uint32_t FrameCount() override { return frame; } };
struct ResSim final : IQueryResidentialSim {
    int calls = 0, x = 0, z = 0;
    void SetQueryCell(int cx, int cz) override { ++calls; x = cx; z = cz; }
};

// ---- the fire / police / budget / neighbour / traffic seams the new rows read ------------------------------
struct FireSimFake final : IQueryFireSim {
    float effectiveness = 0.5f;
    int fundingCost = 0;
    float fundingFraction = 0.0f;
    float setFraction = -1.0f;
    int setFundingCalls = 0;
    std::uint32_t availability = 0, baseCall = 0, dispatchCount = 0;
    bool dispatchOk = true;
    float StationEffectiveness(cISC4Occupant*) override { return effectiveness; }
    bool StationFunding(cISC4Occupant*, int* cost, float* fraction) override {
        if (cost) *cost = fundingCost;
        if (fraction) *fraction = fundingFraction;
        return true;
    }
    void SetStationFunding(cISC4Occupant*, float fraction) override {
        ++setFundingCalls;
        setFraction = fraction;
    }
    bool DispatchInfo(cISC4Occupant*, std::uint32_t* a, std::uint32_t* b, std::uint32_t* c) override {
        if (!dispatchOk) return false;
        if (a) *a = availability;
        if (b) *b = baseCall;
        if (c) *c = dispatchCount;
        return true;
    }
};

struct PoliceSimFake final : IQueryPoliceSim {
    float effectiveness = 0.25f;
    int arrests = 0, crimes = 0;
    int fundingCost = 0;
    float fundingFraction = 0.0f;
    float setFraction = -1.0f;
    int setFundingCalls = 0;
    std::uint32_t dispatchCount = 0;
    bool dispatchOk = true;
    float jailEfficiency = 0.0f;
    int jailCapacity = 0;
    std::int64_t jailUpkeep = 0;
    float StationEffectiveness(cISC4Occupant*) override { return effectiveness; }
    int ArrestCount(cISC4Occupant*) override { return arrests; }
    int CrimeCount(cISC4Occupant*) override { return crimes; }
    bool StationFunding(cISC4Occupant*, int* cost, float* fraction) override {
        if (cost) *cost = fundingCost;
        if (fraction) *fraction = fundingFraction;
        return true;
    }
    void SetStationFunding(cISC4Occupant*, float fraction) override {
        ++setFundingCalls;
        setFraction = fraction;
    }
    bool DispatchInfo(cISC4Occupant*, std::uint32_t* a, std::uint32_t* b, std::uint32_t* c) override {
        if (!dispatchOk) return false;
        if (a) *a = 0;
        if (b) *b = 0;
        if (c) *c = dispatchCount;
        return true;
    }
    float JailEfficiency() override { return jailEfficiency; }
    int JailCapacity(cISC4Occupant*) override { return jailCapacity; }
    std::int64_t JailMaintenance(cISC4Occupant*) override { return jailUpkeep; }
};

struct BudgetSimFake final : IQueryBudgetSim {
    std::int64_t cost = 0;
    bool ok = true;
    int calls = 0;
    bool OccupantValueCost(cISCPropertyHolder*, std::uint32_t, std::int64_t& out) override {
        ++calls;
        if (!ok) return false;
        out = cost;
        return true;
    }
};

struct NeighborCityFake final : IQueryNeighborCity {
    int refs = 1;
    std::string cityName, mayorName;
    int cityNameCalls = 0, mayorNameCalls = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void GetCityName(cIGZString& out) const override {
        ++const_cast<NeighborCityFake*>(this)->cityNameCalls;
        out.AssignFromCString(cityName.c_str());
    }
    void GetMayorName(cIGZString& out) const override {
        ++const_cast<NeighborCityFake*>(this)->mayorNameCalls;
        out.AssignFromCString(mayorName.c_str());
    }
};

struct NeighborsSimFake final : IQueryNeighborsSim {
    bool validCell = true;
    bool hasConnection = true;
    NeighborCityFake* city = nullptr;
    int lastType = -1, lastX = 0, lastZ = 0;
    bool IsValidConnectionCell(int, int) override { return validCell; }
    bool FindConnection(std::uint32_t type, int x, int z) override {
        lastType = static_cast<int>(type);
        lastX = x;
        lastZ = z;
        return hasConnection;
    }
    IQueryNeighborCity* FindCityOnOtherSide(int, int) override {
        if (city) city->AddRef();   // the caller releases (the original's returned pointer is owned)
        return city;
    }
};

struct TrafficSimFake final : IQueryTrafficSim {
    std::uint8_t cell = 0;
    int x = -1, z = -1;
    std::uint8_t CongestionCell(int cx, int cz) override {
        x = cx;
        z = cz;
        return cell;
    }
};

// A cIGZWinMessageFilter the filter rows can install and remove.
struct WinFilterFake final : cIGZWinMessageFilter {
    int refs = 1;
    int messages = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool DoMessage(cIGZWin*, cGZMessage&) override {
        ++messages;
        return false;
    }
};

// ---- messages ---------------------------------------------------------------------------------------------
struct MsgServer final : cIGZMessageServer2 {
    int refs = 1;
    std::vector<std::uint32_t> added, removed;
    std::vector<std::uint32_t> sent, posted, targeted;
    std::size_t queue = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool MessageSend(cIGZMessage2* m) override { sent.push_back(m->GetType()); return true; }
    bool MessagePost(cIGZMessage2* m, bool) override { posted.push_back(m->GetType()); return true; }
    bool AddNotification(cIGZMessageTarget2*, std::uint32_t type) override { added.push_back(type); return true; }
    bool RemoveNotification(cIGZMessageTarget2*, std::uint32_t type) override { removed.push_back(type); return true; }
    bool GeneralMessagePostToTarget(cIGZMessage2* m, cIGZMessageTarget2*) override {
        targeted.push_back(m->GetType());
        return true;
    }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return static_cast<std::uint32_t>(queue); }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return static_cast<std::uint32_t>(refs); }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
};

// ---- host -------------------------------------------------------------------------------------------------
struct Host final : IViewInputHost, IQueryHost {
    View view;
    Mgr mgr;
    vic_fakes::CursorMgr cursors;
    Callout* callout = nullptr;
    FakeRenderer renderer;
    LotMgr lots;
    Power power;
    Plumbing plumbing;
    Aura aura;
    Sim sim;
    ResSim residential;
    FireSimFake fireSim;
    PoliceSimFake policeSim;
    BudgetSimFake budgetSim;
    NeighborsSimFake neighborsSim;
    TrafficSimFake trafficSim;
    MsgServer messages;
    cIGZWin* legendParent = nullptr;
    int attached = 0, detached = 0, sounds = 0;
    std::uint32_t soundId = 0;
    bool debug = false;
    std::vector<std::uint32_t> keys;
    std::string money = "1,234.56";
    std::vector<std::uint32_t> strings;   // instances the tool asked for
    std::string yes = "Yes", no = "No";

    IViewInputView* View3D() override { return &view; }
    cIGZWinMgr* WindowManager() override { return &mgr; }
    void PlaySound(int, std::uint32_t id) override { ++sounds; soundId = id; }

    cIGZMessageServer2* MessageServer() override { return &messages; }
    IQueryView* QueryView() override { return &view; }
    IQueryRenderer* Renderer() override { return &renderer; }
    IQueryCity* City() override { return &city; }
    IQueryLotManager* LotManager() override { return &lots; }
    IQueryPowerSim* PowerSim() override { return &power; }
    IQueryPlumbingSim* PlumbingSim() override { return &plumbing; }
    IQueryAuraSim* AuraSim() override { return &aura; }
    IQuerySimulator* Simulator() override { return &sim; }
    IQueryResidentialSim* ResidentialSim() override { return &residential; }
    IQueryFireSim* FireSim() override { return &fireSim; }
    IQueryPoliceSim* PoliceSim() override { return &policeSim; }
    IQueryBudgetSim* BudgetSim() override { return &budgetSim; }
    IQueryNeighborsSim* NeighborsSim() override { return &neighborsSim; }
    IQueryTrafficSim* TrafficSim() override { return &trafficSim; }
    IQueryPathView* CreatePathView() override { return nullptr; }
    cISC4WinCalloutBox* CreateCalloutBox() override { return callout; }
    cIGZWin* CreateWindowFromScript(std::uint32_t instance, cIGZWin*, std::uint32_t) override {
        legendInstances.push_back(instance);
        return legendParent;
    }
    void AttachLegendWinProc(cIGZWin*) override { ++attached; }
    void DetachWinProc(cIGZWin*) override { ++detached; }
    bool IsKeyDown(std::uint32_t key) override {
        for (std::uint32_t k : keys) if (k == key) return true;
        return false;
    }
    bool IsDebugMode() override { return debug; }
    void FormatMoneyFloat(double value, cRZString& out) override {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%s%.2f", money.c_str(), value);
        out.AssignFromCString(buffer);
    }
    void LoadString(std::uint32_t, std::uint32_t instance, cIGZString& out) override {
        strings.push_back(instance);
        if (instance == 0xca5d4f33) out.AssignFromCString(yes.c_str());
        else if (instance == 0x6a5d4f37) out.AssignFromCString(no.c_str());
        else if (instance == 0xcc06a5e0) out.AssignFromCString("Abandoned: commute");
        else out.AssignFromCString("Loaded");
    }

    // cISC4City stand-in
    struct City final : IQueryCity {
        int follows = 0, props = 0, toCell = 0;
        int cellX = 0, cellZ = 0;
        int cellsX = 64, cellsZ = 64;
        bool QueryInterface(std::uint32_t, void**) { return false; }
        void PositionToCell(float, float, int& x, int& z) override { ++toCell; x = cellX; z = cellZ; }
        int CellCountX() override { return cellsX; }
        int CellCountZ() override { return cellsZ; }
        void FollowAutomaton(cIGZUnknown*) override { ++follows; }
        void SelectStoplightProp(cIGZUnknown*) override { ++props; }
    } city;

    std::vector<std::uint32_t> legendInstances;
};

}  // namespace

int main() {
    Formatter formatter;
    SetLanguageFormatter(&formatter);
    Host host;
    auto* viewWin = new cGZWin(&host.mgr);
    host.view.window = viewWin;
    auto* calloutWin = new cGZWin(&host.mgr);
    auto callout = new Callout();
    callout->win = calloutWin;
    host.callout = callout;
    SetViewInputHost(&host);
    SetQueryHost(&host);

    // --- A: construction, ids, refcount, QueryInterface -----------------------------------------------------
    auto* tool = new cSC4ViewInputControlQuery(cSC4ViewInputControlQuery::kQueryBuilding, 250);
    CHECK(tool->QueryType() == cSC4ViewInputControlQuery::kQueryBuilding);
    CHECK(tool->TipPeriod() == 250 && tool->TipEnabled() && !tool->HoverValid() && !tool->MouseDown());
    CHECK(tool->ShouldStack() == false && tool->OnKeyDown(1, 2) == false);

    CHECK(tool->AddRef() == 1);
    {
        void* out = nullptr;
        CHECK(tool->QueryInterface(kGZIID_cSC4ViewInputControlQuery, &out) &&
              out == static_cast<cIGZMessageTarget2*>(tool));
        CHECK(tool->QueryInterface(1, &out));
        CHECK(tool->QueryInterface(kGZIID_cISC4ViewInputControl, &out) && out == static_cast<cSC4ViewInputControl*>(tool));
        CHECK(!tool->QueryInterface(0x1234, &out));
        CHECK(tool->Release() >= 1);   // the two QueryInterface references are still held
        CHECK(tool->AddRefFromMessageTarget() >= 2);
        CHECK(tool->ReleaseFromMessageTarget() >= 1);
        CHECK(tool->QueryInterfaceFromMessageTarget(1, &out));
        CHECK(tool->Release() >= 1);
    }

    // --- B: Init -------------------------------------------------------------------------------------------
    tool->SetWindow(viewWin);
    CHECK(tool->Init());
    CHECK(tool->GetID() == 0xc7af928e);
    CHECK(tool->QueryType() == cSC4ViewInputControlQuery::kQueryBuilding);
    CHECK(host.messages.added.size() == 6);
    CHECK(host.messages.added[0] == 0xa456d95 && host.messages.added[5] == 0x69247dc7);
    CHECK(host.callout == callout && callout->refs == 1);
    CHECK(callout->titleStyle == 0x0a52f5c6 && callout->textStyle == 0xa85f1a83);
    CHECK(callout->avoid.left == -40 && callout->avoid.right == 40);
    CHECK(!calloutWin->IsVisible());

    // --- C: SetTip -----------------------------------------------------------------------------------------
    cRZString title, text;
    title.AssignFromCString("Title");
    text.AssignFromCString("Body");
    callout->title.clear();
    callout->text.clear();
    tool->SetTip(&title, &text);
    CHECK(callout->title == "Title" && callout->text == "Body" && calloutWin->IsVisible());
    const int clearsBefore = host.view.clears;
    tool->SetTip(nullptr, nullptr);
    CHECK(host.view.clears == clearsBefore + 1 && !calloutWin->IsVisible());

    // --- D: DoMessage --------------------------------------------------------------------------------------
    {
        // the tick with tips turned off does nothing
        cRZMessage2Standard off;
        off.SetType(0x26d31ec2);
        CHECK(!tool->DoMessage(&off) && !tool->TipEnabled());
        const int tips = host.view.tips;
        cRZMessage2Standard tick;
        tick.SetType(0xa456d95);
        tool->DoMessage(&tick);
        CHECK(host.view.tips == tips);
        CHECK(tool->DoMessageFromMessageTarget(&tick) == false);

        cRZMessage2Standard coverage;
        coverage.SetType(0xa456d96);
        tool->DoMessage(&coverage);
        CHECK(host.view.coverage == 1);

        // dialog refresh is gated to every 10th frame and needs the dialog + callback
        cRZMessage2Standard dialog;
        dialog.SetType(0x66956814);
        host.sim.frame = 5;
        tool->DoMessage(&dialog);
        CHECK(cSC4ViewInputControlQuery::QueryDialogWindow() == nullptr);

        // mouse-move message: the window manager's position reaches OnMouseMove
        cRZMessage2Standard move;
        move.SetType(0xa6b79602);
        tool->DoMessage(&move);
        CHECK(host.renderer.picks >= 1);

        // path update without a path view is a no-op
        cRZMessage2Standard paths;
        paths.SetType(0x69247dc7);
        CHECK(!tool->DoMessage(&paths));
    }

    // --- E: hover state machine ----------------------------------------------------------------------------
    {
        TestOccupant occupant;
        Holder holder;
        holder.id = 0x6a95e503;
        holder.value = true;
        occupant.holder = &holder;
        host.renderer.nextObject = static_cast<cISC4Occupant*>(&occupant);
        auto* lot = new Lot();
        lot->occupants.push_back(static_cast<cISC4Occupant*>(&occupant));
        host.lots.byOccupant = lot;
        host.lots.byCell = nullptr;

        host.view.pickOk = true;
        host.view.terrain[0] = 64.0f;
        host.view.terrain[2] = 128.0f;
        CHECK(tool->OnMouseMove(5, 6, 0));
        CHECK(tool->HoverValid() && tool->HoveredOccupant() == static_cast<cISC4Occupant*>(&occupant));
        CHECK(occupant.highlight == 7 && occupant.highlightSets >= 1);
        // SetHighlightState sweeps the lot: the occupant carries property 0x6a95e503, so it is set to 7 again
        CHECK(occupant.highlightSets >= 2);
        CHECK(tool->TipTimer().IsRunning());

        CHECK(tool->OnMouseExit());
        CHECK(!tool->HoverValid() && tool->HoveredOccupant() == nullptr);
        CHECK(occupant.highlight == 0);
        CHECK(!tool->TipTimer().IsRunning());
        delete lot;
        host.lots.byOccupant = nullptr;
    }

    // --- F: clicks -----------------------------------------------------------------------------------------
    {
        host.renderer.nextObject = nullptr;   // misses
        host.messages.sent.clear();
        host.messages.targeted.clear();
        const std::size_t adds = host.messages.targeted.size();
        host.view.terrain[0] = 40.0f;
        host.view.terrain[2] = 80.0f;
        host.city.cellX = 40;
        host.city.cellZ = 80;
        CHECK(tool->OnMouseDownL(3, 4, 1));
        CHECK(host.renderer.picks >= 1);   // automaton + stoplight picks
        CHECK(host.messages.sent.size() == 1 && host.messages.sent[0] == 0x2acd2cf3);
        CHECK(host.messages.targeted.size() == adds);   // the tip refresh was not requested here
        CHECK(!tool->MouseDown());   // the building flavour clears it after DescribePick
        CHECK(host.mgr.setCapture == viewWin);
        CHECK(tool->OnMouseUpL(3, 4, 1) && !tool->MouseDown());

        host.sim.frame = 0;
        host.residential.calls = 0;
        CHECK(tool->OnMouseDownL(3, 4, 2));
        CHECK(host.residential.calls == 1 && host.residential.x == 40 >> 2 && host.residential.z == 80 >> 2);
    }

    // --- G: dialog controls --------------------------------------------------------------------------------
    {
        auto* dialog = new cGZWin(&host.mgr);
        auto* label = new gz::cGZWinText(&host.mgr);
        label->SetID(0x1200);
        auto* edit = new gz::cGZWinTextEdit(&host.mgr);
        edit->SetID(0x1210);
        auto* slider = new gz::cGZWinSlider(&host.mgr);
        slider->SetID(0x1220);
        auto* meter = new gz::cGZWinBMP(&host.mgr);
        meter->SetID(0x1230);
        auto* hidden = new gz::cGZWinText(&host.mgr);
        hidden->SetID(0x1240);
        hidden->ShowWindow();
        CHECK(dialog->ChildAdd(label) && dialog->ChildAdd(edit) && dialog->ChildAdd(slider));
        CHECK(dialog->ChildAdd(meter) && dialog->ChildAdd(hidden));

        cRZString value;
        value.AssignFromCString("hello");
        CHECK(tool->SetEditControlText(dialog, 0x1210, value));
        CHECK(tool->SetStaticControlText(dialog, 0x1200, value));
        CHECK(tool->SetStaticControlNumeric(dialog, 0x1200, 42));
        CHECK(tool->SetStaticControlNumericPercent(dialog, 0x1200, 37));
        CHECK(tool->SetStaticControlSimoleons(dialog, 0x1200, 1234567));
        CHECK(tool->SetStaticControlSimoleonsFloat(dialog, 0x1200, 12.5));
        CHECK(tool->SetPerformanceMeterValue(dialog, 0x1230, 0.25f));
        CHECK(!tool->SetEditControlText(dialog, 0x9999, value));
        CHECK(!tool->SetStaticControlText(dialog, 0x9999, value));

        cRZString readBack;
        std::uint32_t length = 0;
        CHECK(tool->GetEditControlText(dialog, 0x1210, readBack, length));
        CHECK(std::strcmp(readBack.ToChar(), "hello") == 0);

        int sliderValue = 0;
        // without the money row at id + 0x100 the call still drives the slider but answers false
        CHECK(!tool->SetSliderControlValue(dialog, 0x1220, 10, 90, 55, 1000, nullptr, nullptr, nullptr, 0));
        CHECK(tool->GetSliderControlValue(dialog, 0x1220, sliderValue));
        CHECK(sliderValue == 55);
        CHECK(slider->GetMinimumValue() == 10 && slider->GetMaximumValue() == 90);
        // the money row of the slider is the static control at id + 0x100
        auto* moneyRow = new gz::cGZWinText(&host.mgr);
        moneyRow->SetID(0x1220 + 0x100);
        CHECK(dialog->ChildAdd(moneyRow));
        CHECK(tool->SetSliderControlValue(dialog, 0x1220, 0, 100, 50, 2000, nullptr, nullptr, nullptr, 0));
        CHECK(std::string(moneyRow->GetCaption().ToChar()) == "1000");   // 2000 * 50% + 0.5, truncated

        dialog->Release();
    }

    // --- H: SetOrGet rows ----------------------------------------------------------------------------------
    {
        auto* dialog = new cGZWin(&host.mgr);
        auto* edit = new gz::cGZWinTextEdit(&host.mgr);
        edit->SetID(0x1300);
        auto* label = new gz::cGZWinText(&host.mgr);
        label->SetID(0x1310);
        label->HideWindow();
        CHECK(dialog->ChildAdd(edit) && dialog->ChildAdd(label));

        TestOccupant occupant;
        occupant.name = "Rutherford";
        CHECK(tool->SetOrGetName(dialog, 0x1300, true, &occupant));
        cRZString shown;
        shown.AssignFromCString(edit->GetText());
        CHECK(std::strcmp(shown.ToChar(), "Rutherford") == 0);

        // the edited name differs: the building occupant is renamed
        cRZString changed;
        changed.AssignFromCString("New Name");
        tool->SetEditControlText(dialog, 0x1300, changed);
        CHECK(tool->SetOrGetName(dialog, 0x1300, false, &occupant));
        CHECK(occupant.names == 1 && occupant.lastSetName == "New Name");

        // the name back to the occupant's: no rename
        cRZString same;
        same.AssignFromCString("Rutherford");
        tool->SetEditControlText(dialog, 0x1300, same);
        CHECK(tool->SetOrGetName(dialog, 0x1300, false, &occupant));
        CHECK(occupant.names == 1);

        host.power.powered = true;
        host.strings.clear();
        CHECK(tool->SetOrGetOccupantIsPowered(dialog, 0x1310, true, &occupant));
        CHECK(host.strings.size() == 1 && host.strings[0] == 0xca5d4f33);   // "Yes"
        host.plumbing.watered = false;
        host.strings.clear();
        CHECK(tool->SetOrGetOccupantIsWatered(dialog, 0x1310, true, &occupant));
        CHECK(host.strings.size() == 1 && host.strings[0] == 0x6a5d4f37);   // "No"
        CHECK(tool->SetOrGetOccupantIsPowered(dialog, 0x1310, false, &occupant));
        CHECK(tool->SetOrGetOccupantIsWatered(dialog, 0x1310, false, &occupant));

        // the abandoned-reason row uses the lot state and hides the control when there is no reason
        auto* lot = new Lot();
        host.lots.byOccupant = lot;
        lot->state = 4;
        lot->reason = 3;
        CHECK(tool->SetOrGetWhyOccupantAbandoned(dialog, 0x1310, true, &occupant));
        CHECK(label->IsVisible());
        CHECK(std::strcmp(label->GetCaption().ToChar(), "Abandoned: commute") == 0);
        lot->reason = 5;   // no string for reason 5 (quirk: case 5 falls through)
        CHECK(tool->SetOrGetWhyOccupantAbandoned(dialog, 0x1310, true, &occupant));
        CHECK(!label->IsVisible());
        host.lots.byOccupant = nullptr;
        CHECK(!tool->SetOrGetWhyOccupantAbandoned(dialog, 0x1310, false, &occupant));
        delete lot;

        // rating thresholds and the mayor approval row
        cRZString rating;
        host.strings.clear();
        tool->GetRatingIn3DegreesFromPercent(rating, 10.0f);
        CHECK(host.strings.back() == 0x4a5fe266);
        tool->GetRatingIn3DegreesFromPercent(rating, 50.0f);
        CHECK(host.strings.back() == 0xa5fe260);
        tool->GetRatingIn3DegreesFromPercent(rating, 90.0f);
        CHECK(host.strings.back() == 0xaa5fe249);

        auto* mayorLot = new Lot();
        mayorLot->rect.x0 = 0; mayorLot->rect.z0 = 0;
        mayorLot->rect.x1 = 1; mayorLot->rect.z1 = 1;
        host.lots.byCell = mayorLot;
        host.city.cellX = 1; host.city.cellZ = 1;
        host.aura.value = 0;
        const float percent = tool->GetMayorRatingPercent(&occupant);
        CHECK(percent > 49.0f && percent < 51.0f);    // average aura 0 + 0x80 over 4 cells = 50.2%
        host.aura.value = -128;                        // clamps nothing: (0 + 0x80 - 128) = 0
        CHECK(tool->GetMayorRatingPercent(&occupant) == 0.0f);
        host.strings.clear();
        CHECK(tool->SetOrGetResidentialBuildingMayorApproval(dialog, 0x1310, true, &occupant));
        CHECK(host.strings.size() == 1);   // the rating string (the row text itself is a static control)
        host.lots.byCell = nullptr;
        delete mayorLot;

        dialog->Release();
    }

    // --- I: picking and the debug query --------------------------------------------------------------------
    {
        TestOccupant occupant;
        host.renderer.nextObject = static_cast<cISC4Occupant*>(&occupant);
        cISC4Occupant* picked = nullptr;
        CHECK(tool->PickQueriableOccupant(10, 20, &picked, nullptr) && picked == static_cast<cISC4Occupant*>(&occupant));

        Dummy dummy;
        host.renderer.nextObject = &dummy;
        cIGZUnknown* automaton = nullptr;
        CHECK(tool->PickAutomaton(10, 20, &automaton) && automaton == &dummy);
        cIGZUnknown* prop = nullptr;
        CHECK(tool->PickStoplightProp(10, 20, &prop) && prop == &dummy);

        host.renderer.nextObject = nullptr;
        CHECK(!tool->PickQueriableOccupant(10, 20, &picked, nullptr) && picked == nullptr);

        host.debug = false;
        host.keys = {0x10, 0x11, 0x12};
        CHECK(!tool->IsDebugQueryEnabled());
        host.debug = true;
        CHECK(tool->IsDebugQueryEnabled());
        host.keys = {0x10, 0x11};
        CHECK(!tool->IsDebugQueryEnabled());
    }

    // --- J: audio feedback --------------------------------------------------------------------------------
    {
        TestOccupant occupant;
        Holder holder;
        holder.id = 0xaa1dd397;   // the default query sound
        holder.value = true;
        occupant.holder = &holder;
        host.sounds = 0;
        host.soundId = 0;
        tool->PlayOccupantQuerySound(static_cast<cISC4Occupant*>(&occupant));
        CHECK(host.sounds == 1);
        tool->PlayOccupantQuerySound(nullptr);
        CHECK(host.sounds == 1);
    }

    // --- K: Shutdown --------------------------------------------------------------------------------------
    CHECK(tool->Shutdown());
    CHECK(host.messages.removed.size() == 6);
    CHECK(host.view.refs == 1 && host.mgr.refs == 0);
    CHECK(tool->GetCursor() == nullptr);
    CHECK(tool->Shutdown() && host.messages.removed.size() == 6);   // already shut down: no second removal
    tool->Release();

    // --- L: the network flavour -----------------------------------------------------------------------------
    {
        auto* viewWin2 = new cGZWin(&host.mgr);
        host.view.window = viewWin2;
        // the previous tool's Shutdown deleted its callout window, so build a fresh one
        callout = new Callout();
        callout->win = new cGZWin(&host.mgr);
        host.callout = callout;
        auto* legendParent = new cGZWin(&host.mgr);
        auto* minimised = new cGZWin(&host.mgr);
        minimised->SetID(cSC4ViewInputControlQuery::kLegendMinimizedChild);
        auto* expanded = new cGZWin(&host.mgr);
        expanded->SetID(cSC4ViewInputControlQuery::kLegendExpandedChild);
        minimised->SetArea(0, 0, 100, 20);
        expanded->SetArea(0, 0, 200, 40);
        CHECK(legendParent->ChildAdd(minimised) && legendParent->ChildAdd(expanded));
        legendParent->AddRef();   // the tool takes and releases its own reference to the script window
        host.legendParent = legendParent;
        host.legendInstances.clear();
        auto* net = new cSC4ViewInputControlQuery(cSC4ViewInputControlQuery::kQueryNetwork, 100);
        CHECK(net->Init());
        CHECK(host.legendInstances.size() == 1);   // instance 0xabb0120f
        CHECK(host.attached == 2 && minimised->IsVisible() && !expanded->IsVisible());
        net->SetLegendMinimized(true);
        CHECK(!minimised->IsVisible() && !expanded->IsVisible());
        net->SetLegendMinimized(false);
        CHECK(minimised->IsVisible() && expanded->IsVisible());
        CHECK(net->ShouldStack() == false);
        net->Release();
        host.legendParent = nullptr;
        delete legendParent;   // owns both legend children
    }

    // --- M: fire / police / jail / park / occupant-cost rows ------------------------------------------------
    {
        auto* dialog = new cGZWin(&host.mgr);
        auto* label = new gz::cGZWinText(&host.mgr);
        label->SetID(0x1007);
        auto* meter = new gz::cGZWinBMP(&host.mgr);
        meter->SetID(0x1001);
        auto* slider = new gz::cGZWinSlider(&host.mgr);
        slider->SetID(0x1002);
        slider->SetMinimumValue(0);   // the dialog script gives the funding slider its 0..100 range
        slider->SetMaximumValue(100);
        auto* moneyRow = new gz::cGZWinText(&host.mgr);
        moneyRow->SetID(0x1102);   // the slider's amount row is id + 0x100
        CHECK(dialog->ChildAdd(label) && dialog->ChildAdd(meter) && dialog->ChildAdd(slider));
        CHECK(dialog->ChildAdd(moneyRow));

        TestOccupant occupant;

        // effectiveness goes through the performance-meter parameter
        host.fireSim.effectiveness = 0.75f;
        CHECK(cSC4ViewInputControlQuery::SetOrGetFireStationEffectiveness(dialog, 0x1001, true, &occupant));
        cIGZVariant* meterValue = nullptr;
        CHECK(meter->AsIGZWin()->GetParam(0xea563ab0, &meterValue) && meterValue);
        CHECK(std::fabs(meterValue->GetValFloat32() - 0.75f) < 1e-6f);
        if (meterValue) meterValue->Release();
        // every row is set-only: clearing leaves the control alone and answers true
        CHECK(cSC4ViewInputControlQuery::SetOrGetFireStationEffectiveness(dialog, 0x1001, false, &occupant));

        // engine count is the dispatch interface's third out value
        host.fireSim.dispatchCount = 4;
        host.fireSim.dispatchOk = true;
        CHECK(cSC4ViewInputControlQuery::SetOrGetFireStationEngineCount(dialog, 0x1007, true, &occupant));
        CHECK(std::string(label->GetCaption().ToChar()) == "4");
        host.fireSim.dispatchOk = false;
        CHECK(!cSC4ViewInputControlQuery::SetOrGetFireStationEngineCount(dialog, 0x1007, true, &occupant));

        // local funding: the slider is the percentage, the amount row the cost
        host.fireSim.fundingCost = 500;
        host.fireSim.fundingFraction = 0.4f;
        CHECK(cSC4ViewInputControlQuery::SetOrGetFireStationLocalFunding(dialog, 0x1002, true, &occupant));
        int sliderValue = 0;
        CHECK(cSC4ViewInputControlQuery::GetSliderControlValue(dialog, 0x1002, sliderValue) && sliderValue == 40);
        CHECK(std::string(moneyRow->GetCaption().ToChar()) == "200");   // 40% of 500
        // ... and writing it back scales the slider by 0.01
        host.fireSim.setFundingCalls = 0;
        CHECK(cSC4ViewInputControlQuery::SetOrGetFireStationLocalFunding(dialog, 0x1002, false, &occupant));
        CHECK(host.fireSim.setFundingCalls == 1 && std::fabs(host.fireSim.setFraction - 0.4f) < 1e-6f);

        // maintenance cost is the same funding call's cost out value
        host.fireSim.fundingCost = 1234;
        CHECK(cSC4ViewInputControlQuery::SetOrGetFireStationMaintenanceCost(dialog, 0x1007, true, &occupant));
        CHECK(std::string(label->GetCaption().ToChar()) == "1234");

        // police effectiveness / arrests / crimes
        host.policeSim.effectiveness = 0.5f;
        CHECK(cSC4ViewInputControlQuery::SetOrGetPoliceStationEffectiveness(dialog, 0x1001, true, &occupant));
        meterValue = nullptr;
        CHECK(meter->AsIGZWin()->GetParam(0xea563ab0, &meterValue) && meterValue);
        CHECK(std::fabs(meterValue->GetValFloat32() - 0.5f) < 1e-6f);
        if (meterValue) meterValue->Release();
        host.policeSim.arrests = 12;
        host.policeSim.crimes = 34;
        CHECK(cSC4ViewInputControlQuery::SetOrGetPoliceStationArrestCount(dialog, 0x1007, true, &occupant));
        CHECK(std::string(label->GetCaption().ToChar()) == "12");
        CHECK(cSC4ViewInputControlQuery::SetOrGetPoliceStationCrimeCount(dialog, 0x1007, true, &occupant));
        CHECK(std::string(label->GetCaption().ToChar()) == "34");
        host.policeSim.dispatchCount = 3;
        CHECK(cSC4ViewInputControlQuery::SetOrGetPoliceStationPatrolCarCount(dialog, 0x1007, true, &occupant));
        CHECK(std::string(label->GetCaption().ToChar()) == "3");
        host.policeSim.fundingCost = 800;
        host.policeSim.fundingFraction = 0.25f;
        CHECK(cSC4ViewInputControlQuery::SetOrGetPoliceStationLocalFunding(dialog, 0x1002, true, &occupant));
        CHECK(cSC4ViewInputControlQuery::GetSliderControlValue(dialog, 0x1002, sliderValue) && sliderValue == 25);
        CHECK(std::string(moneyRow->GetCaption().ToChar()) == "200");   // 25% of 800
        host.policeSim.setFundingCalls = 0;
        CHECK(cSC4ViewInputControlQuery::SetOrGetPoliceStationLocalFunding(dialog, 0x1002, false, &occupant));
        CHECK(host.policeSim.setFundingCalls == 1 && std::fabs(host.policeSim.setFraction - 0.25f) < 1e-6f);
        host.policeSim.fundingCost = 77;
        CHECK(cSC4ViewInputControlQuery::SetOrGetPoliceStationMaintenanceCost(dialog, 0x1007, true, &occupant));
        CHECK(std::string(label->GetCaption().ToChar()) == "77");

        // the jail: cells are 0.8 * capacity, inmates capacity * efficiency, and the meter fills the band above 0.8
        host.policeSim.jailCapacity = 10;
        host.policeSim.jailEfficiency = 0.5f;
        CHECK(cSC4ViewInputControlQuery::SetOrGetJailCellCount(dialog, 0x1007, true, &occupant));
        CHECK(std::string(label->GetCaption().ToChar()) == "8");
        CHECK(cSC4ViewInputControlQuery::SetOrGetJailInmateCount(dialog, 0x1007, true, &occupant));
        CHECK(std::string(label->GetCaption().ToChar()) == "5");
        CHECK(cSC4ViewInputControlQuery::SetOrGetJailCondition(dialog, 0x1001, true, &occupant));
        meterValue = nullptr;
        CHECK(meter->AsIGZWin()->GetParam(0xea563ab0, &meterValue) && meterValue);
        CHECK(std::fabs(meterValue->GetValFloat32() - 1.0f) < 1e-6f);   // at or below the design capacity
        if (meterValue) meterValue->Release();
        host.policeSim.jailEfficiency = 0.9f;    // 9 inmates, one free cell: 1 / (10 * 0.2)
        CHECK(cSC4ViewInputControlQuery::SetOrGetJailCondition(dialog, 0x1001, true, &occupant));
        meterValue = nullptr;
        CHECK(meter->AsIGZWin()->GetParam(0xea563ab0, &meterValue) && meterValue);
        CHECK(std::fabs(meterValue->GetValFloat32() - 0.5f) < 1e-5f);
        if (meterValue) meterValue->Release();
        host.policeSim.jailEfficiency = 1.0f;    // completely full: no free cell, meter 0
        CHECK(cSC4ViewInputControlQuery::SetOrGetJailCondition(dialog, 0x1001, true, &occupant));
        meterValue = nullptr;
        CHECK(meter->AsIGZWin()->GetParam(0xea563ab0, &meterValue) && meterValue);
        CHECK(meterValue->GetValFloat32() == 0.0f);
        if (meterValue) meterValue->Release();
        host.policeSim.jailCapacity = 0;         // no jail at all
        CHECK(cSC4ViewInputControlQuery::SetOrGetJailCondition(dialog, 0x1001, true, &occupant));
        meterValue = nullptr;
        CHECK(meter->AsIGZWin()->GetParam(0xea563ab0, &meterValue) && meterValue);
        CHECK(meterValue->GetValFloat32() == 0.0f);
        if (meterValue) meterValue->Release();
        host.policeSim.jailUpkeep = 750;
        CHECK(cSC4ViewInputControlQuery::SetOrGetJailMaintenanceCost(dialog, 0x1007, true, &occupant));
        CHECK(std::string(label->GetCaption().ToChar()) == "750");

        // the park row is the generic occupant-cost row: one budget lookup per property value
        Holder holder;
        holder.id = 0xea54d285;
        holder.values = {1, 2};
        occupant.holder = &holder;
        host.budgetSim.calls = 0;
        host.budgetSim.ok = true;
        host.budgetSim.cost = 250;
        CHECK(cSC4ViewInputControlQuery::SetOrGetParkMaintenanceCost(dialog, 0x1007, true, &occupant));
        CHECK(host.budgetSim.calls == 2);
        CHECK(std::string(label->GetCaption().ToChar()) == "500");
        // a failed lookup contributes nothing
        host.budgetSim.ok = false;
        CHECK(cSC4ViewInputControlQuery::SetOrGetOccupantCost(dialog, 0x1007, true, &occupant));
        CHECK(std::string(label->GetCaption().ToChar()) == "0");
        // no property vector at all: the row declines
        holder.id = 0;
        CHECK(!cSC4ViewInputControlQuery::SetOrGetOccupantCost(dialog, 0x1007, true, &occupant));
        CHECK(cSC4ViewInputControlQuery::SetOrGetOccupantCost(dialog, 0x1007, false, &occupant));   // set-only

        dialog->Release();
    }

    // --- N: neighbour-connection rows ----------------------------------------------------------------------
    {
        auto* dialog = new cGZWin(&host.mgr);
        auto* label = new gz::cGZWinText(&host.mgr);
        label->SetID(0x1007);
        auto* cityRow = new gz::cGZWinText(&host.mgr);
        cityRow->SetID(0x104b);
        auto* mayorRow = new gz::cGZWinText(&host.mgr);
        mayorRow->SetID(0x104c);
        auto* congestionRow = new gz::cGZWinText(&host.mgr);
        congestionRow->SetID(0x104d);
        CHECK(dialog->ChildAdd(label) && dialog->ChildAdd(cityRow));
        CHECK(dialog->ChildAdd(mayorRow) && dialog->ChildAdd(congestionRow));

        NeighborCityFake city;
        city.cityName = "Neighbourville";
        city.mayorName = "Mayor Bob";
        host.neighborsSim.validCell = true;
        host.neighborsSim.hasConnection = true;
        host.neighborsSim.city = &city;

        CHECK(cSC4ViewInputControlQuery::SetOrGetNeighborConnectionNeighborCityName(dialog, 0x1007, true, 5, 6, 2));
        CHECK(std::string(label->GetCaption().ToChar()) == "Neighbourville");
        CHECK(host.neighborsSim.lastType == 2 && host.neighborsSim.lastX == 5 && host.neighborsSim.lastZ == 6);
        CHECK(cSC4ViewInputControlQuery::SetOrGetNeighborConnectionNeighborCityMayorName(dialog, 0x1007, true, 5, 6, 2));
        CHECK(std::string(label->GetCaption().ToChar()) == "Mayor Bob");

        // the connection lookup runs first: an invalid cell or a missing connection means "no city"
        host.neighborsSim.validCell = false;
        CHECK(!cSC4ViewInputControlQuery::SetOrGetNeighborConnectionNeighborCityMayorName(dialog, 0x1007, true, 5, 6, 2));
        host.neighborsSim.validCell = true;
        host.neighborsSim.hasConnection = false;
        CHECK(!cSC4ViewInputControlQuery::SetOrGetNeighborConnectionNeighborCityMayorName(dialog, 0x1007, true, 5, 6, 2));
        host.neighborsSim.hasConnection = true;
        // the city-name row still writes the placeholder text and answers true
        host.neighborsSim.city = nullptr;
        CHECK(cSC4ViewInputControlQuery::SetOrGetNeighborConnectionNeighborCityName(dialog, 0x1007, true, 5, 6, 2));
        CHECK(std::string(label->GetCaption().ToChar()) == "Loaded");   // the fake host's placeholder
        host.neighborsSim.city = &city;
        CHECK(cSC4ViewInputControlQuery::SetOrGetNeighborConnectionNeighborCityName(dialog, 0x1007, false, 5, 6, 2));

        // congestion: a byte scaled to a percentage and clamped to the city's cell counts
        host.city.cellsX = 64;
        host.city.cellsZ = 48;
        host.trafficSim.cell = 51;   // 20%
        CHECK(cSC4ViewInputControlQuery::SetOrGetNeighborConnectionCongestion(dialog, 0x1007, true, 5, 6));
        CHECK(std::string(label->GetCaption().ToChar()) == "20%");
        CHECK(host.trafficSim.x == 5 && host.trafficSim.z == 6);
        CHECK(cSC4ViewInputControlQuery::SetOrGetNeighborConnectionCongestion(dialog, 0x1007, true, -4, 1000));
        CHECK(host.trafficSim.x == 0 && host.trafficSim.z == 47);
        host.trafficSim.cell = 255;
        CHECK(cSC4ViewInputControlQuery::SetOrGetNeighborConnectionCongestion(dialog, 0x1007, true, 63, 47));
        CHECK(std::string(label->GetCaption().ToChar()) == "100%");
        CHECK(cSC4ViewInputControlQuery::SetOrGetNeighborConnectionCongestion(dialog, 0x1007, false, 5, 6));

        // the cell-based dispatcher picks the row by id and adds the cell info's id offset
        cSC4ViewInputControlQuery::tCellInfo info;
        info.x = 5;
        info.z = 6;
        info.idOffset = 0;
        info.connectionType = 2;
        host.trafficSim.cell = 26;   // 10%
        CHECK(!cSC4ViewInputControlQuery::SetOrGetControlValue(dialog, 0x104d, true, &info));
        CHECK(std::string(congestionRow->GetCaption().ToChar()) == "10%");
        CHECK(!cSC4ViewInputControlQuery::SetOrGetControlValue(dialog, 0x104b, true, &info));
        CHECK(std::string(cityRow->GetCaption().ToChar()) == "Neighbourville");
        CHECK(!cSC4ViewInputControlQuery::SetOrGetControlValue(dialog, 0x104c, true, &info));
        CHECK(std::string(mayorRow->GetCaption().ToChar()) == "Mayor Bob");
        // the row id is offset by the cell info (used to address several neighbour panels)
        info.idOffset = 0x100;
        CHECK(!cSC4ViewInputControlQuery::SetOrGetControlValue(dialog, 0x104d, true, &info));   // 0x114d: no control
        CHECK(!cSC4ViewInputControlQuery::SetOrGetControlValue(dialog, 0x1011, true, &info));   // no row for the airport ids yet
        CHECK(!cSC4ViewInputControlQuery::SetOrGetControlValue(dialog, 0x104b, true, nullptr));

        dialog->Release();
    }

    // --- O: the row enumeration callbacks ------------------------------------------------------------------
    {
        auto* dialog = new cGZWin(&host.mgr);
        auto* label = new gz::cGZWinText(&host.mgr);
        label->SetID(0x1007);
        auto* slider = new gz::cGZWinSlider(&host.mgr);
        slider->SetID(0x1002);
        slider->SetMinimumValue(0);
        slider->SetMaximumValue(100);
        auto* cityRow = new gz::cGZWinText(&host.mgr);
        cityRow->SetID(0x104b);
        CHECK(dialog->ChildAdd(label) && dialog->ChildAdd(slider) && dialog->ChildAdd(cityRow));

        TestOccupant occupant;
        // EnumFuncSetValue applies the row named by the child id (through the 0x7f-entry table)
        host.policeSim.arrests = 7;
        CHECK(cSC4ViewInputControlQuery::EnumFuncSetValue(dialog, 0x1007, label, &occupant));
        CHECK(std::string(label->GetCaption().ToChar()) == "7");
        // a null table entry falls through to the child enumeration (no child answers 0x22ba0121 here)
        CHECK(cSC4ViewInputControlQuery::EnumFuncSetValue(dialog, 0x1040, label, &occupant));
        CHECK(cSC4ViewInputControlQuery::EnumFuncSetValue(dialog, 0x1040, nullptr, &occupant));
        // EnumFuncGetValue calls the same row with set = false: the slider's percentage goes back to the sim
        host.fireSim.fundingFraction = 0.75f;   // control 0x1002 is the *fire* station funding row
        CHECK(cSC4ViewInputControlQuery::SetOrGetFireStationLocalFunding(dialog, 0x1002, true, &occupant));
        host.fireSim.setFundingCalls = 0;
        CHECK(cSC4ViewInputControlQuery::EnumFuncGetValue(dialog, 0x1002, slider, &occupant));
        CHECK(host.fireSim.setFundingCalls == 1 && std::fabs(host.fireSim.setFraction - 0.75f) < 1e-6f);
        // EnumFuncUpdateValue: a plain window is updated like SetValue (id 0x1007 is the arrest-count row)
        host.policeSim.arrests = 19;
        CHECK(cSC4ViewInputControlQuery::EnumFuncUpdateValue(dialog, 0x1007, label, &occupant));
        CHECK(std::string(label->GetCaption().ToChar()) == "19");

        // SetFilter installs the filter on the child; ClearFilter removes it and drops the five parameters
        WinFilterFake filter;
        CHECK(cSC4ViewInputControlQuery::EnumFuncSetFilter(dialog, 0, label, &filter));
        CHECK(filter.refs == 2);   // the child holds one reference
        auto* paramValue = new cRZVariant();
        paramValue->SetValUint32(3);
        label->SetParam(0xea563ab0, paramValue);   // the window takes the only counted reference
        cIGZVariant* stored = nullptr;
        CHECK(label->GetParam(0xea563ab0, &stored) && stored);
        if (stored) stored->Release();
        CHECK(cSC4ViewInputControlQuery::EnumFuncClearFilter(dialog, 0, label, &filter));
        CHECK(filter.refs == 1);
        CHECK(!label->GetParam(0xea563ab0, &stored));

        // the neighbour-dialog wrappers are the same dispatcher with the cell info as context
        NeighborCityFake city;
        city.cityName = "Across";
        host.neighborsSim.validCell = true;
        host.neighborsSim.hasConnection = true;
        host.neighborsSim.city = &city;
        cSC4ViewInputControlQuery::tCellInfo info;
        info.x = 1;
        info.z = 2;
        info.idOffset = 0;
        info.connectionType = 1;
        CHECK(cSC4ViewInputControlQuery::EnumFuncNeighborDialogSetValue(dialog, 0x104b, &info, nullptr));
        CHECK(std::string(cityRow->GetCaption().ToChar()) == "Across");
        CHECK(cSC4ViewInputControlQuery::EnumFuncNeighborDialogGetValue(dialog, 0x104b, &info, nullptr));

        dialog->Release();
    }

    // window 0/1 of the tool may already be gone; the view windows are still alive
    std::printf(failures ? "FAILED (%d)\n" : "sc4_viewinputcontrolquery ok\n", failures);
    return failures ? 1 : 0;
}
