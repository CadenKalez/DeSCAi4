#include "sc4/ViewInputControlQuery.h"

#include <cmath>
#include <cstring>

#include "gz/StreamOps.h"
#include "gz/Win.h"
#include "gz/WinBMP.h"
#include "gz/WinGen.h"
#include "gz/WinSlider.h"
#include "gz/WinText.h"
#include "gz/WinTextEdit.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZPoint.h"
#include "rz/cRZVariant.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4/SC4Utilities.h"

namespace sc4 {
namespace {

IQueryHost* g_queryHost = nullptr;

// The three tip strings the original keeps as file-scope cRZString globals (gsQueryString, gsQueryString2, gsQueryString3).
cRZString gsQueryString1;
cRZString gsQueryString2;
cRZString gsQueryString3;

// Message types the tool listens to (registered in Init, removed in Shutdown).
constexpr std::uint32_t kMsgTick = 0xa456d95;           // refresh the mouse tip
constexpr std::uint32_t kMsgCoverage = 0xa456d96;       // recompute the coverage rings
constexpr std::uint32_t kMsgDisableTip = 0x26d31ec2;    // the view left query mode
constexpr std::uint32_t kMsgMouseMove = 0xa6b79602;     // the window manager forwarded a mouse move
constexpr std::uint32_t kMsgDialogTick = 0x66956814;    // periodic dialog row refresh
constexpr std::uint32_t kMsgUpdatePaths = 0x69247dc7;   // routes changed
constexpr std::uint32_t kMsgAutomatonPick = 0x2acd2cf3; // click that missed an automaton: send it to the automaton code
constexpr std::uint32_t kTipOwner = 0x2a456cb1;         // the view's tool-tip id for the query tool

// iids the tool queries
constexpr std::uint32_t kIID_cISC4Automaton = 0xa9b40f05;
constexpr std::uint32_t kIID_cISC4PropOccupant = 0xe9793a65;
constexpr std::uint32_t kIID_cISC4BuildingOccupant = 0x87dda3a9;

// String instances (group 0xea5524eb).
constexpr std::uint32_t kStringGroup = 0xea5524eb;
constexpr std::uint32_t kStrYes = 0xca5d4f33;
constexpr std::uint32_t kStrNo = 0x6a5d4f37;
constexpr std::uint32_t kStrSimoleon = 0xa7c204;
constexpr std::uint32_t kStrRatingBad = 0x4a5fe266;
constexpr std::uint32_t kStrRatingFair = 0xa5fe260;
constexpr std::uint32_t kStrRatingGood = 0xaa5fe249;
constexpr std::uint32_t kStrAbandoned[7] = {0, 0xcc06a5de, 0xcc06a5df, 0xcc06a5e0,
                                            0xcc06a5e1, 0, 0xcc06a5e2};

// Properties the tool reads off occupants and lots.
constexpr std::uint32_t kPropHighlighted = 0x6a95e503;   // bool: occupant takes part in the highlight sweep
constexpr std::uint32_t kPropQuerySound = 0xaa1dd397;    // the default query sound of an occupant
constexpr std::uint32_t kPropLotQuerySound = 0xa902434;  // sound when the lot state is 4 (abandoned / derelict?)
constexpr std::uint32_t kPropBuildingQuerySound = 0xaa905ab9;   // sound for lot states 2 and 3

template <typename T>
T* ChildAs(cIGZWin* win, std::uint32_t id, std::uint32_t iid) {
    if (!win) return nullptr;
    void* out = nullptr;
    if (!win->GetChildAsRecursive(id, iid, &out)) return nullptr;
    return static_cast<T*>(out);
}

void HideWindow(cIGZWin* win) {
    if (win) win->HideWindow();
}
void ShowWindow(cIGZWin* win) {
    if (win) win->ShowWindow();
}

// nsGZWinUtils::SetWindowVisible (@ 0x1006a1f74): show or hide a child and re-layout.
void SetWindowVisible(cIGZWin* parent, std::uint32_t id, bool visible) {
    cIGZWin* child = ChildAs<cIGZWin>(parent, id, kGZIID_cIGZWin);
    if (!child) return;
    if (visible) child->ShowWindow();
    else child->HideWindow();
    child->Release();
}

std::uint32_t StringLength(const cIGZString* s) { return s ? s->Strlen() : 0; }

}  // namespace

IQueryHost* QueryHost() { return g_queryHost; }
void SetQueryHost(IQueryHost* host) { g_queryHost = host; }

cIGZWin* cSC4ViewInputControlQuery::sQueryDialog_ = nullptr;
cIGZWin::EnumChildFn cSC4ViewInputControlQuery::sEnumSetFunction_ = nullptr;
void* cSC4ViewInputControlQuery::sEnumContext1_ = nullptr;
void* cSC4ViewInputControlQuery::sEnumContext2_ = nullptr;

cIGZWin* cSC4ViewInputControlQuery::QueryDialogWindow() { return sQueryDialog_; }
void cSC4ViewInputControlQuery::SetQueryDialogWindow(cIGZWin* window) { sQueryDialog_ = window; }
cIGZWin::EnumChildFn cSC4ViewInputControlQuery::EnumSetFunction() { return sEnumSetFunction_; }
void cSC4ViewInputControlQuery::SetEnumSetFunction(cIGZWin::EnumChildFn fn, void* context1, void* context2) {
    sEnumSetFunction_ = fn;
    sEnumContext1_ = context1;
    sEnumContext2_ = context2;
}

// @ 0x1002c1ec2
cSC4ViewInputControlQuery::cSC4ViewInputControlQuery(EQueryType type, std::uint32_t tipPeriod) {
    queryType_ = type;
    tipPeriod_ = tipPeriod;
    tipEnabled_ = true;
    showCoordinates_ = false;
    unknownD8_ = 3;
    pathView_ = nullptr;
    occupant_ = nullptr;
    callout_ = nullptr;
    legendMinimized_ = nullptr;
    legendExpanded_ = nullptr;
    occupantHighlight_ = 0;
    pickX_ = pickY_ = pickZ_ = 0.0f;
    hoverValid_ = false;
    mouseDown_ = false;
}

// @ 0x1002dc524 (the deleting variant at 0x1002dbdc8 only adds the delete)
cSC4ViewInputControlQuery::~cSC4ViewInputControlQuery() {
    if (pathView_) { pathView_->Release(); pathView_ = nullptr; }
    // Quirk kept: the original releases the hovered occupant (+0x48) only when the hover moves, never here.
    if (legendExpanded_) { legendExpanded_->Release(); legendExpanded_ = nullptr; }
    if (legendMinimized_) { legendMinimized_->Release(); legendMinimized_ = nullptr; }
    if (callout_) { callout_->Release(); callout_ = nullptr; }
}

std::uint32_t cSC4ViewInputControlQuery::AddRef() { return ++refCount_; }

std::uint32_t cSC4ViewInputControlQuery::Release() {
    if (refCount_ > 1) return --refCount_;
    FinalRelease();
    return 0;
}

bool cSC4ViewInputControlQuery::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cSC4ViewInputControlQuery || iid == kGZIID_cIGZUnknown) {
        *out = static_cast<cIGZMessageTarget2*>(this);
    } else if (iid == kGZIID_cISC4ViewInputControl) {
        *out = static_cast<cSC4ViewInputControl*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

// The message-target sub-object (this + 0x40 in the original) has its own vtable; these four thunks forward to the
// primary object.
std::uint32_t cSC4ViewInputControlQuery::AddRefFromMessageTarget() { return AddRef(); }
std::uint32_t cSC4ViewInputControlQuery::ReleaseFromMessageTarget() { return Release(); }
bool cSC4ViewInputControlQuery::QueryInterfaceFromMessageTarget(std::uint32_t iid, void** out) {
    return QueryInterface(iid, out);
}
bool cSC4ViewInputControlQuery::DoMessageFromMessageTarget(cIGZMessage2* message) { return DoMessage(message); }

// @ 0x1002c2078
bool cSC4ViewInputControlQuery::Init() {
    if (initialized_) return true;
    const bool network = queryType_ == kQueryNetwork;
    id_ = network ? (0xc7af928e | 1) : 0xc7af928e;
    cursorId_ = network ? 0xcb8d5b54 : 0x816d7f74;
    cSC4ViewInputControl::Init();
    IQueryHost* host = QueryHost();
    if (!host) return true;
    if (cIGZMessageServer2* messages = host->MessageServer()) {
        cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
        messages->AddNotification(target, kMsgTick);
        messages->AddNotification(target, kMsgCoverage);
        messages->AddNotification(target, kMsgDisableTip);
        messages->AddNotification(target, kMsgMouseMove);
        messages->AddNotification(target, kMsgDialogTick);
        messages->AddNotification(target, kMsgUpdatePaths);
    }
    tipTimer_.SetCallbackFunction(&cSC4ViewInputControlQuery::MouseTipCallback, this);
    tipEnabled_ = true;
    if (IQueryView* view = host->QueryView()) showCoordinates_ = view->ShowCoordinates();
    if (cISC4WinCalloutBox* callout = host->CreateCalloutBox()) {
        callout->SetTextFontStyle(0xa85f1a83);
        callout->SetTitleFontStyle(0x0a52f5c6);
        cRZRect avoid;
        avoid.left = -40;
        avoid.top = -40;
        avoid.right = 40;
        avoid.bottom = 40;
        callout->SetAvoidanceRect(avoid);
        HideWindow(callout->AsIGZWin());
        if (cIGZWin* viewWin = ViewWindow()) viewWin->ChildAdd(callout->AsIGZWin());
        callout_ = callout;
    }
    if (network) CreateLegendWindows();
    return true;
}

cIGZWin* cSC4ViewInputControlQuery::ViewWindow() {
    IQueryHost* host = QueryHost();
    if (!host) return nullptr;
    IQueryView* view = host->QueryView();
    return view ? view->ViewWindow() : nullptr;
}

// The network-mode legend: one script window with the minimised and the expanded legend children.
void cSC4ViewInputControlQuery::CreateLegendWindows() {
    IQueryHost* host = QueryHost();
    cIGZWin* viewWin = ViewWindow();
    if (!host || !viewWin) return;
    cIGZWin* created = host->CreateWindowFromScript(kLegendInstance, viewWin, kLegendWindowId);
    if (!created) return;
    cIGZWin* minimised = ChildAs<cIGZWin>(created, kLegendMinimizedChild, kGZIID_cIGZWin);
    cIGZWin* expanded = ChildAs<cIGZWin>(created, kLegendExpandedChild, kGZIID_cIGZWin);
    if (minimised) {
        created->ChildRemove(minimised);
        viewWin->ChildAdd(minimised);
        minimised->GZWinMoveTo(viewWin->GetW() - 2 - minimised->GetW(), 2);
        host->AttachLegendWinProc(minimised);
        minimised->PullToFront();
        minimised->ShowWindow();
        legendMinimized_ = minimised;
    }
    if (expanded) {
        created->ChildRemove(expanded);
        viewWin->ChildAdd(expanded);
        const int x = legendMinimized_ ? legendMinimized_->GetR() - expanded->GetW() : 2;
        const int y = legendMinimized_ ? legendMinimized_->GetT() : 2;
        expanded->GZWinMoveTo(x, y);
        host->AttachLegendWinProc(expanded);
        expanded->PullToFront();
        expanded->HideWindow();
        legendExpanded_ = expanded;
    }
    viewWin->ChildAdd(created);
    created->Release();
}

// @ 0x1002c2642
bool cSC4ViewInputControlQuery::Shutdown() {
    if (!initialized_) return true;
    IQueryHost* host = QueryHost();
    if (IQueryView* view = host ? host->QueryView() : nullptr) view->ClearTip(kTipOwner);
    if (callout_) {
        if (cIGZWin* viewWin = ViewWindow()) viewWin->ChildDelete(callout_->AsIGZWin());
        callout_->Release();
        callout_ = nullptr;
    }
    if (legendMinimized_) {
        if (host) host->DetachWinProc(legendMinimized_);
        if (cIGZWin* viewWin = ViewWindow()) viewWin->ChildDelete(legendMinimized_);
        legendMinimized_->Release();
        legendMinimized_ = nullptr;
    }
    if (legendExpanded_) {
        if (host) host->DetachWinProc(legendExpanded_);
        if (cIGZWin* viewWin = ViewWindow()) viewWin->ChildDelete(legendExpanded_);
        legendExpanded_->Release();
        legendExpanded_ = nullptr;
    }
    if (pathView_) {
        if (host && host->Renderer()) host->Renderer()->RemoveLayer(pathView_);
        pathView_->ShutdownView();
        pathView_->Release();
        pathView_ = nullptr;
    }
    if (occupant_) {
        occupant_->SetHighlight(static_cast<int>(occupantHighlight_), false);
        SetHighlightState(0);
        occupantHighlight_ = 0;
        occupant_->Release();
        occupant_ = nullptr;
    }
    if (cIGZMessageServer2* messages = host ? host->MessageServer() : nullptr) {
        cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
        messages->RemoveNotification(target, kMsgTick);
        messages->RemoveNotification(target, kMsgCoverage);
        messages->RemoveNotification(target, kMsgDisableTip);
        messages->RemoveNotification(target, kMsgMouseMove);
        messages->RemoveNotification(target, kMsgDialogTick);
        messages->RemoveNotification(target, kMsgUpdatePaths);
    }
    if (tipTimer_.IsRunning()) {
        tipEnabled_ = false;
        tipTimer_.Stop();
    }
    cSC4ViewInputControl::Shutdown();
    return true;
}

// @ 0x1002c28f6: highlight every occupant of the hovered lot that carries the highlight property, then the occupant
// itself (the original calls SetHighlight(state, false) on each lot occupant with property 0x6a95e503 set).
void cSC4ViewInputControlQuery::SetHighlightState(int state) {
    if (!occupant_) return;
    IQueryHost* host = QueryHost();
    IQueryLotManager* lots = host ? host->LotManager() : nullptr;
    if (!lots) return;
    IQueryLot* lot = lots->GetLotForOccupant(occupant_);
    if (!lot) return;
    lot->AddRef();
    std::vector<cISC4Occupant*> occupants;
    lot->GetOccupants(occupants);
    for (cISC4Occupant* o : occupants) {
        if (!o) continue;
        bool highlighted = false;
        cISCPropertyHolder* holder = o->AsPropertyHolder();
        if (holder && cSCPropertyHelp::GetPropertyValue(holder, kPropHighlighted, highlighted) && highlighted) {
            o->SetHighlight(state, false);
        }
    }
    lot->Release();
}

// @ 0x1002c1dca
void cSC4ViewInputControlQuery::UpdatePaths() {
    if (!pathView_) return;
    pathView_->ClearPaths();
    ShowAllRoutesThatIntersectCityRect(cityCellRect_);
}

// @ 0x1002c1dfe
void cSC4ViewInputControlQuery::SetLegendMinimized(bool minimized) {
    if (!legendMinimized_ || !legendExpanded_) return;
    if (minimized) {
        legendMinimized_->HideWindow();
        legendExpanded_->HideWindow();
        legendMinimized_->GZWinMoveTo(legendExpanded_->GetR() - legendMinimized_->GetW(), legendExpanded_->GetT());
    } else {
        legendExpanded_->ShowWindow();
        legendExpanded_->GZWinMoveTo(legendMinimized_->GetR() - legendExpanded_->GetW(), legendMinimized_->GetT());
        legendMinimized_->ShowWindow();
    }
}

// @ 0x1002c2596
void cSC4ViewInputControlQuery::MouseTipCallback(void* self) {
    auto* tool = static_cast<cSC4ViewInputControlQuery*>(self);
    IQueryHost* host = QueryHost();
    if (!host || !tool) return;
    cIGZMessageServer2* messages = host->MessageServer();
    if (!messages) return;
    cRZMessage2Standard message;
    message.SetType(kMsgTick);
    messages->GeneralMessagePostToTarget(&message, static_cast<cIGZMessageTarget2*>(tool));
}

// @ 0x1002d4eb0
void cSC4ViewInputControlQuery::CoverageRingCallback(void* self) {
    auto* tool = static_cast<cSC4ViewInputControlQuery*>(self);
    IQueryHost* host = QueryHost();
    if (!host || !tool) return;
    cIGZMessageServer2* messages = host->MessageServer();
    if (!messages) return;
    cRZMessage2Standard message;
    message.SetType(kMsgCoverage);
    messages->MessagePost(&message, false);
}

// @ 0x1002c2a0e
bool cSC4ViewInputControlQuery::DoMessage(cIGZMessage2* message) {
    IQueryHost* host = QueryHost();
    if (!message || !host) return false;
    // The original compares the message type as a SIGNED 32-bit int (0xa6b79602 is negative).
    const std::int32_t type = static_cast<std::int32_t>(message->GetType());
    if (type > 0x26d31ec1) {
        if (type == kMsgUpdatePaths) {
            UpdatePaths();
            return false;
        }
        if (type == kMsgDialogTick) {
            IQuerySimulator* sim = host->Simulator();
            if (!sim || sim->FrameCount() % 10 != 0) return false;
            cIGZWin* dialog = sQueryDialog_;
            if (!dialog || !sEnumSetFunction_) return false;
            if (sEnumContext1_) dialog->EnumChildren(kGZIID_cIGZWin, sEnumSetFunction_, sEnumContext1_);
            if (!sEnumContext2_) return false;
            dialog->EnumChildren(kGZIID_cIGZWin, sEnumSetFunction_, sEnumContext2_);
            return false;
        }
        if (type == kMsgDisableTip) {
            tipEnabled_ = false;
            return false;
        }
        return false;
    }
    if (type == kMsgMouseMove) {
        if (cIGZWinMgr* wm = ViewInputHost() ? ViewInputHost()->WindowManager() : nullptr) {
            int x = 0, y = 0;
            wm->GetMousePosition(x, y);
            OnMouseMove(x, y, 0);
        }
        return false;
    }
    if (type == kMsgCoverage) {
        if (IQueryView* view = host->QueryView()) view->RecalcCoverage();
        return false;
    }
    if (type != kMsgTick) return false;
    if (!tipEnabled_) return false;

    cRZString title, text;
    std::uint32_t icon = 0;
    std::uint32_t meter = 0;
    float meterValue = 0.5f;   // local_28 = 0x3f000000
    if (!occupant_) {
        if (hoverValid_) {
            GetTerrainSummaryInfo(title, text);
        } else if (!showCoordinates_) {
            return false;
        }
    } else {
        const std::uint32_t occupantType = occupant_->GetType();
        if (queryType_ == kQueryNetwork) {
            if (occupantType == 0xc772bf98) GetNetworkOccupantTrafficInfo(title, text);
            else if (occupantType == 0x278128a0) GetBuildingOccupantTrafficInfo(title, text, icon);
        } else {
            if (occupantType == 0xa823821e) GetPropOccupantSummaryInfo(title);
            else if (occupantType == 0xc772bf98) GetNetworkOccupantSummaryInfo(title, text);
            else if (occupantType == 0x278128a0) GetBuildingOccupantTipInfo(title, text, icon, meter, meterValue);
            else if (occupantType == 0x2890d4de) { host->LoadString(kStringGroup, 0x0a5bb9fd, title); }
            else if (occupantType == 0x6a11bd2b) { host->LoadString(kStringGroup, 0x4a5bba21, title); }
            else if (occupantType == 0x74758926) GetFloraOccupantSummaryInfo(title);
        }
    }
    if (showCoordinates_) {
        if (StringLength(&title) == 0) title.AssignFromCString("Terrain");
        cRZString coords;
        coords.Sprintf("x=%.1f y=%.1f z=%.1f\ncell x=%d cell z=%d", pickX_, pickY_, pickZ_,
                       static_cast<int>(pickX_ * 0.0625f), static_cast<int>(pickZ_ * 0.0625f));
        text.AppendString(coords);
    }
    if (!callout_ || icon == 0) {
        HideWindow(callout_ ? callout_->AsIGZWin() : nullptr);
        if (IQueryView* view = host->QueryView()) view->SetTip(kTipOwner, 0, &title, &text, 0);
    } else {
        // PARTIAL: the original also feeds the icon buffer (cSLBuffer from resource 0x856ddbac / 0x46a006b0) and its
        // meter into cISC4WinCalloutBox::SetIconMeter; the buffer path is not recovered.
        (void)meter;
        (void)meterValue;
        SetTip(&title, &text);
        if (IQueryView* view = host->QueryView()) view->ClearTip(kTipOwner);
    }
    return false;
}

// @ 0x1002c5b68 (title, text; either may be null or empty)
void cSC4ViewInputControlQuery::SetTip(const cIGZString* title, const cIGZString* text) {
    if (StringLength(title) != 0 || StringLength(text) != 0) {
        if (!callout_) return;
        cRZString empty;
        callout_->SetTitle(title ? *title : empty);
        callout_->SetText(text ? *text : empty);
        cIGZWin* win = callout_->AsIGZWin();
        if (!win->IsVisible()) {
            if (cIGZWinMgr* wm = ViewInputHost() ? ViewInputHost()->WindowManager() : nullptr) {
                int x = 0, y = 0;
                wm->GetCursorPos(ViewWindow(), x, y);
                cRZPoint point;
                point.x = x;
                point.y = y;
                callout_->SetPointOfInterest(point);
            }
            ShowWindow(win);
        }
        return;
    }
    IQueryHost* host = QueryHost();
    if (host && host->QueryView()) host->QueryView()->ClearTip(kTipOwner);
    HideWindow(callout_ ? callout_->AsIGZWin() : nullptr);
}

// @ 0x1002c5ca0
bool cSC4ViewInputControlQuery::OnKeyDown(std::uint32_t, std::uint32_t) { return false; }

// @ 0x1002d4ea8
bool cSC4ViewInputControlQuery::ShouldStack() { return false; }

// @ 0x1002cf312: the debug query needs the SC4 debug flag and the three debug keys (shift, control, alt) held.
bool cSC4ViewInputControlQuery::IsDebugQueryEnabled() {
    IQueryHost* host = QueryHost();
    if (!host || !host->IsDebugMode()) return false;
    return host->IsKeyDown(0x10) && host->IsKeyDown(0x11) && host->IsKeyDown(0x12);
}

// @ 0x1002cdd7e
bool cSC4ViewInputControlQuery::PickQueriableOccupant(int x, int y, cISC4Occupant** out, IOccupantFilter* filter) {
    *out = nullptr;
    IQueryHost* host = QueryHost();
    IQueryRenderer* renderer = host ? host->Renderer() : nullptr;
    if (!renderer) return false;
    IPickResult* result = renderer->Pick(x, y, filter);
    if (!result) return false;
    bool found = false;
    if (cIGZUnknown* object = result->GetObject()) {
        void* occupant = nullptr;
        if (object->QueryInterface(kGZIID_cISC4Occupant, &occupant)) {
            *out = static_cast<cISC4Occupant*>(occupant);
            found = true;
        }
    }
    result->Release();
    return found;
}

// @ 0x1002d45fc / 0x1002d470a (same body, different filter and interface)
bool cSC4ViewInputControlQuery::PickAutomaton(int x, int y, cIGZUnknown** out) {
    *out = nullptr;
    IQueryHost* host = QueryHost();
    IQueryRenderer* renderer = host ? host->Renderer() : nullptr;
    if (!renderer) return false;
    IOccupantFilter* filter = renderer->CreateFilter(2);   // cSC4AutomatonFilter
    if (!filter) return false;
    bool found = false;
    if (IPickResult* result = renderer->Pick(x, y, filter)) {
        if (cIGZUnknown* object = result->GetObject()) {
            void* automaton = nullptr;
            if (object->QueryInterface(kIID_cISC4Automaton, &automaton)) {
                *out = static_cast<cIGZUnknown*>(automaton);
                found = true;
            }
        }
        result->Release();
    }
    filter->Release();
    return found;
}

bool cSC4ViewInputControlQuery::PickStoplightProp(int x, int y, cIGZUnknown** out) {
    *out = nullptr;
    IQueryHost* host = QueryHost();
    IQueryRenderer* renderer = host ? host->Renderer() : nullptr;
    if (!renderer) return false;
    IOccupantFilter* filter = renderer->CreateFilter(3);   // cStoplightPropFilter
    if (!filter) return false;
    bool found = false;
    if (IPickResult* result = renderer->Pick(x, y, filter)) {
        if (cIGZUnknown* object = result->GetObject()) {
            void* prop = nullptr;
            if (object->QueryInterface(kIID_cISC4PropOccupant, &prop)) {
                *out = static_cast<cIGZUnknown*>(prop);
                found = true;
            }
        }
        result->Release();
    }
    filter->Release();
    return found;
}

// @ 0x1002d4342
bool cSC4ViewInputControlQuery::OnMouseDownL(int x, int y, std::uint32_t modifiers) {
    IQueryHost* host = QueryHost();
    tipTimer_.Stop();
    if (IQueryView* view = host ? host->QueryView() : nullptr) view->ClearTip(kTipOwner);
    HideWindow(callout_ ? callout_->AsIGZWin() : nullptr);
    cIGZUnknown* automaton = nullptr;
    if (modifiers & 1) {
        if (PickAutomaton(x, y, &automaton)) {
            if (host->City()) host->City()->FollowAutomaton(automaton);
            automaton->Release();
        } else if (cIGZMessageServer2* messages = host->MessageServer()) {
            cRZMessage2Standard message;
            message.SetType(kMsgAutomatonPick);
            message.SetData1(static_cast<std::uint64_t>(x));
            message.SetData2(static_cast<std::uint64_t>(y));
            messages->MessageSend(&message);
        }
        cIGZUnknown* prop = nullptr;
        if (PickStoplightProp(x, y, &prop)) {
            if (host->City()) host->City()->SelectStoplightProp(prop);
            prop->Release();
        }
    }
    if (modifiers & 2) {
        float pos[3] = {0.0f, 0.0f, 0.0f};
        if (IQueryView* view = host->QueryView()) view->PickTerrain(x, y, pos, false);
        int cellX = 0, cellZ = 0;
        if (host->City()) host->City()->PositionToCell(pos[0], pos[2], cellX, cellZ);
        if (host->ResidentialSim()) host->ResidentialSim()->SetQueryCell(cellX >> 2, cellZ >> 2);
    }
    mouseDown_ = true;
    if (queryType_ == kQueryNetwork) {
        GetOccupantPaths();
    } else {
        mouseDown_ = false;
        DescribePick(x, y, gsQueryString1, gsQueryString2, gsQueryString3, IsDebugQueryEnabled());
    }
    SetCapture();
    return true;
}

// @ 0x1002d4818
bool cSC4ViewInputControlQuery::OnMouseUpL(int, int, std::uint32_t) {
    IQueryHost* host = QueryHost();
    tipTimer_.Stop();
    if (IQueryView* view = host ? host->QueryView() : nullptr) view->ClearTip(kTipOwner);
    HideWindow(callout_ ? callout_->AsIGZWin() : nullptr);
    mouseDown_ = false;
    return true;
}

// @ 0x1002d4e2a
bool cSC4ViewInputControlQuery::OnMouseExit() {
    IQueryHost* host = QueryHost();
    tipTimer_.Stop();
    if (IQueryView* view = host ? host->QueryView() : nullptr) view->ClearTip(kTipOwner);
    HideWindow(callout_ ? callout_->AsIGZWin() : nullptr);
    if (occupant_) {
        occupant_->SetHighlight(static_cast<int>(occupantHighlight_), false);
        SetHighlightState(0);
        occupant_->Release();
        occupant_ = nullptr;
    }
    hoverValid_ = false;
    return true;
}

// @ 0x1002d4862
bool cSC4ViewInputControlQuery::OnMouseMove(int x, int y, std::uint32_t modifiers) {
    (void)modifiers;
    IQueryHost* host = QueryHost();
    hoverValid_ = false;
    if (!host || !view3D_) return true;
    cISC4Occupant* picked = nullptr;
    IOccupantFilter* filter = nullptr;
    if (IQueryRenderer* renderer = host->Renderer()) filter = renderer->CreateFilter(queryType_ == kQueryNetwork ? 0 : 1);
    if (filter && PickQueriableOccupant(x, y, &picked, filter)) {
        hoverValid_ = true;
        // A building occupant is replaced by the occupant of its lot.
        bool highlighted = false;
        cISCPropertyHolder* holder = picked->AsPropertyHolder();
        if (holder && cSCPropertyHelp::GetPropertyValue(holder, kPropHighlighted, highlighted) && highlighted) {
            if (IQueryLot* lot = host->LotManager() ? host->LotManager()->GetLotForOccupant(picked) : nullptr) {
                lot->AddRef();
                std::vector<cISC4Occupant*> occupants;
                lot->GetOccupants(occupants);
                if (!occupants.empty() && occupants.front()) {
                    cISC4Occupant* lotOccupant = occupants.front();
                    lotOccupant->AddRef();
                    picked->Release();
                    picked = lotOccupant;
                }
                lot->Release();
            }
        }
    }
    float pos[3] = {0.0f, 0.0f, 0.0f};
    if (!hoverValid_) {
        IQueryView* view = host->QueryView();
        if (view && view->PickTerrain(x, y, pos, false)) {
            int cellX = 0, cellZ = 0;
            if (host->City()) host->City()->PositionToCell(pos[0], pos[2], cellX, cellZ);
            if (IQueryLot* lot = host->LotManager() ? host->LotManager()->GetLotAtCell(cellX, cellZ, 0) : nullptr) {
                lot->AddRef();
                std::vector<cISC4Occupant*> occupants;
                lot->GetOccupants(occupants);
                cISC4Occupant* candidate = occupants.empty() ? nullptr : occupants.front();
                if (queryType_ == kQueryNetwork) {
                    if (candidate && candidate->IsOccupantGroup(0x1006)) {
                        candidate->AddRef();
                        picked = candidate;
                        hoverValid_ = true;
                    }
                } else if (candidate) {
                    candidate->AddRef();
                    picked = candidate;
                    hoverValid_ = true;
                }
                lot->Release();
            }
            if (queryType_ != kQueryNetwork) hoverValid_ = true;
        }
        if (std::fabs(pickX_ - pos[0]) >= 0.0001f || std::fabs(pickY_ - pos[1]) >= 0.0001f ||
            std::fabs(pickZ_ - pos[2]) >= 0.0001f) {
            pickX_ = pos[0];
            pickY_ = pos[1];
            pickZ_ = pos[2];
        }
    } else if (showCoordinates_) {
        if (IQueryView* view = host->QueryView()) {
            view->PickTerrain(x, y, pos, false);
            pickX_ = pos[0];
            pickY_ = pos[1];
            pickZ_ = pos[2];
        }
    }
    if (picked != occupant_) {
        if (occupant_) {
            occupant_->SetHighlight(static_cast<int>(occupantHighlight_), false);
            SetHighlightState(0);
            occupant_->Release();
        }
        occupant_ = picked;
        if (occupant_) {
            occupantHighlight_ = occupant_->GetHighlight();
            occupant_->SetHighlight(7, false);
            SetHighlightState(7);
            occupant_->AddRef();      // the member's own reference
        } else {
            occupantHighlight_ = 0;
        }
        if (picked) picked->Release();   // drop the reference the pick handed back
    } else if (picked) {
        picked->Release();
    }
    if (!mouseDown_) {
        tipTimer_.Stop();
        tipEnabled_ = true;
        tipTimer_.StartOneShot(tipPeriod_, static_cast<cIGZUnknown*>(static_cast<cSC4ViewInputControl*>(this)));
    }
    if (callout_) {
        cIGZWin* win = callout_->AsIGZWin();
        if (win && win->IsVisible()) {
            cRZPoint point;
            point.x = x;
            point.y = y;
            callout_->SetPointOfInterest(point);
        }
    }
    if (filter) filter->Release();
    return true;
}

// @ 0x1002cde0c
void cSC4ViewInputControlQuery::PlayOccupantQuerySound(cISC4Occupant* occupant) {
    IQueryHost* host = QueryHost();
    IViewInputHost* base = ViewInputHost();
    if (!occupant || !host || !base) return;
    std::uint32_t sound = 0;
    cISCPropertyHolder* holder = occupant->AsPropertyHolder();
    IQueryLot* lot = host->LotManager() ? host->LotManager()->GetLotForOccupant(occupant) : nullptr;
    if (lot) {
        lot->AddRef();
        if (lot->GetState() == 4 && holder && cSCPropertyHelp::GetPropertyValue(holder, kPropLotQuerySound, sound)) {
            base->PlaySound(1, sound);
            lot->Release();
            return;
        }
        const int state = lot->GetState();
        if ((state == 2 || state == 3) && holder &&
            cSCPropertyHelp::GetPropertyValue(holder, kPropBuildingQuerySound, sound)) {
            base->PlaySound(1, sound);
            lot->Release();
            return;
        }
    }
    if (holder && cSCPropertyHelp::GetPropertyValue(holder, kPropQuerySound, sound)) base->PlaySound(1, sound);
    if (lot) lot->Release();
}

// @ 0x1002d4f40
bool cSC4ViewInputControlQuery::SetEditControlText(cIGZWin* win, std::uint32_t id, cIGZString& text) {
    gz::cIGZWinTextEdit* edit = ChildAs<gz::cIGZWinTextEdit>(win, id, gz::kGZIID_cIGZWinTextEdit);
    if (!edit) return false;
    edit->SetText(text, false);
    edit->ClearFlag(8);       // vslot 0x270
    edit->SetFlag(0x200);     // vslot 0x260
    edit->Release();
    return true;
}

// @ 0x1002d4fb2
bool cSC4ViewInputControlQuery::GetEditControlText(cIGZWin* win, std::uint32_t id, cIGZString& text,
                                                   std::uint32_t& length) {
    gz::cIGZWinTextEdit* edit = ChildAs<gz::cIGZWinTextEdit>(win, id, gz::kGZIID_cIGZWinTextEdit);
    if (!edit) return false;
    length = edit->GetTextLength();
    text.Resize(length);
    text.AssignFromCString(edit->GetText());
    edit->Release();
    return true;
}

// @ 0x1002d5094
bool cSC4ViewInputControlQuery::SetStaticControlText(cIGZWin* win, std::uint32_t id, const cIGZString& text) {
    gz::cIGZWinText* control = ChildAs<gz::cIGZWinText>(win, id, gz::kGZIID_cIGZWinText);
    if (!control) return false;
    control->SetCaption(text);
    control->Release();
    return true;
}

// @ 0x1002d50dc
bool cSC4ViewInputControlQuery::SetStaticControlNumeric(cIGZWin* win, std::uint32_t id, std::uint32_t value) {
    cRZString text;
    text.Sprintf("%d", static_cast<int>(value));
    return SetStaticControlText(win, id, text);
}

// @ 0x1002d5168
bool cSC4ViewInputControlQuery::SetStaticControlNumericPercent(cIGZWin* win, std::uint32_t id, std::uint32_t value) {
    cRZString text;
    text.Sprintf("%d%%", static_cast<int>(value));
    return SetStaticControlText(win, id, text);
}

// @ 0x1002d51f4
bool cSC4ViewInputControlQuery::SetStaticControlSimoleons(cIGZWin* win, std::uint32_t id, std::int64_t value) {
    cRZString text;
    SC4MakeMoneyString(value, text);
    return SetStaticControlText(win, id, text);
}

// @ 0x1002d527a: the locale-formatted amount with the simoleon symbol appended.
bool cSC4ViewInputControlQuery::SetStaticControlSimoleonsFloat(cIGZWin* win, std::uint32_t id, double value) {
    IQueryHost* host = QueryHost();
    if (!host) return false;
    cRZString text;
    host->FormatMoneyFloat(value, text);
    cRZString symbol;
    host->LoadString(kStringGroup, kStrSimoleon, symbol);
    text.AppendString(symbol);
    return SetStaticControlText(win, id, text);
}

// @ 0x1002d54a0
bool cSC4ViewInputControlQuery::SetSliderControlValue(cIGZWin* win, std::uint32_t id, int minimum, int maximum,
                                                      int value, std::int64_t money, cISC4Occupant* occupant,
                                                      tSetOrGetFn callback, std::uint32_t* array,
                                                      std::uint32_t arraySize) {
    gz::cIGZWinSlider* slider = ChildAs<gz::cIGZWinSlider>(win, id, gz::kGZIID_cIGZWinSlider);
    if (!slider) return false;
    if (minimum < maximum) {
        slider->SetMinimumValue(minimum);
        slider->SetMaximumValue(maximum);
    }
    // Quirk kept: the original sets maximum + 1 and then value, both through SetValue.
    slider->SetValue(value + 1);
    slider->SetValue(value);
    cIGZWin* sliderWin = slider->AsIGZWin();
    sliderWin->PullToFront();
    // The window keeps a counted reference to every parameter, so the variants are heap-allocated.
    cIGZVariant* existing = nullptr;
    if (sliderWin->GetParam(0x6a8cc9d6, &existing) && existing) existing->Release();
    auto* fn = new cRZVariant();
    fn->SetValVoidPtr(reinterpret_cast<void*>(callback));
    sliderWin->SetParam(0x6a8cc9d6, fn);
    auto* amount = new cRZVariant();
    amount->SetValSint64(money);
    sliderWin->SetParam(0x2aa6e8ad, amount);
    if (array) {
        auto* list = new cRZVariant();
        list->SetValVoid(array, arraySize * static_cast<std::uint32_t>(sizeof(std::uint32_t)));
        sliderWin->SetParam(0x0ab3f6db, list);
    }
    (void)occupant;
    slider->Release();
    return SetStaticControlSimoleons(
        win, id + 0x100, static_cast<std::int64_t>(static_cast<float>(money) * (static_cast<float>(value) / 100.0f) + 0.5f));
}

// @ 0x1002d5758
bool cSC4ViewInputControlQuery::GetSliderControlValue(cIGZWin* win, std::uint32_t id, int& value) {
    gz::cIGZWinSlider* slider = ChildAs<gz::cIGZWinSlider>(win, id, gz::kGZIID_cIGZWinSlider);
    if (!slider) return false;
    value = slider->GetValue();
    slider->Release();
    return true;
}

// @ 0x1002d5926
bool cSC4ViewInputControlQuery::SetPerformanceMeterValue(cIGZWin* win, std::uint32_t id, float value) {
    gz::cIGZWinBMP* meter = ChildAs<gz::cIGZWinBMP>(win, id, gz::kGZIID_cIGZWinBMP);
    if (!meter) return false;
    auto* variant = new cRZVariant();
    variant->SetValFloat32(value);
    meter->AsIGZWin()->SetParam(0xea563ab0, variant);   // the window keeps the counted reference
    meter->Release();
    return true;
}

// @ 0x1002daf96
// PARTIAL: the original is a switch over the control ids 0x1000..0x107e that calls one SetOrGet* row handler per id
// (EnumFunc* go through the same 0x7f-entry table). None of those row handlers that need an unrecovered simulator is
// ported, and the id -> handler table was not recovered, so only the dispatcher's shape is kept here.
bool cSC4ViewInputControlQuery::SetOrGetControlValue(cIGZWin*, std::uint32_t, bool, tCellInfo*) { return false; }

// @ 0x1002d5a0a
bool cSC4ViewInputControlQuery::SetOrGetName(cIGZWin* win, std::uint32_t id, bool set, cISC4Occupant* occupant) {
    cRZString name;
    GetOccupantName(occupant, name);
    if (set) return SetEditControlText(win, id, name);
    cRZString edited;
    std::uint32_t length = 0;
    if (!GetEditControlText(win, id, edited, length)) return true;
    if (edited.IsEqualToChars(name.ToChar(), name.Strlen(), true)) return true;
    if (occupant) {
        void* building = nullptr;
        if (occupant->QueryInterface(kIID_cISC4BuildingOccupant, &building)) {
            auto* named = static_cast<IQueryBuildingOccupant*>(building);
            named->SetName(edited);
            named->Release();
        }
    }
    return true;
}

// @ 0x1002d5bc0
bool cSC4ViewInputControlQuery::SetOrGetWhyOccupantAbandoned(cIGZWin* win, std::uint32_t id, bool set,
                                                             cISC4Occupant* occupant) {
    IQueryHost* host = QueryHost();
    if (!set || !host) return false;
    cRZString reason;
    IQueryLot* lot = host->LotManager() ? host->LotManager()->GetLotForOccupant(occupant) : nullptr;
    if (lot) {
        lot->AddRef();
        const int state = lot->GetState();
        const int abandoned = lot->GetAbandonReason();
        if (state == 4 && abandoned >= 1 && abandoned <= 6) {
            const std::uint32_t instance = (abandoned == 5) ? 0 : kStrAbandoned[abandoned];
            if (instance) host->LoadString(kStringGroup, instance, reason);
        }
        lot->Release();
    }
    SetWindowVisible(win, id, reason.Strlen() != 0);
    SetStaticControlText(win, id, reason);
    return true;
}

// @ 0x1002dacd2
bool cSC4ViewInputControlQuery::SetOrGetOccupantIsPowered(cIGZWin* win, std::uint32_t id, bool set,
                                                          cISC4Occupant* occupant) {
    IQueryHost* host = QueryHost();
    if (!set || !host) return true;
    cRZString text;
    IQueryPowerSim* power = host->PowerSim();
    if (power && power->IsPowered(occupant)) host->LoadString(kStringGroup, kStrYes, text);
    else host->LoadString(kStringGroup, kStrNo, text);
    SetStaticControlText(win, id, text);
    return true;
}

// @ 0x1002dadd6
bool cSC4ViewInputControlQuery::SetOrGetOccupantIsWatered(cIGZWin* win, std::uint32_t id, bool set,
                                                          cISC4Occupant* occupant) {
    IQueryHost* host = QueryHost();
    if (!set || !host) return true;
    cRZString text;
    IQueryPlumbingSim* plumbing = host->PlumbingSim();
    if (plumbing && plumbing->IsWatered(occupant)) host->LoadString(kStringGroup, kStrYes, text);
    else host->LoadString(kStringGroup, kStrNo, text);
    SetStaticControlText(win, id, text);
    return true;
}

// @ 0x1002daed6
// PARTIAL: the original reads/writes the toggle state of a button control through nsGZWinUtils::GetButtonToggleState
// (0x1006a21eb) / SetButtonToggleState (0x1006a215d), which are not recovered; only the lot lookup is ported.
bool cSC4ViewInputControlQuery::SetOrGetOccupantIsHistorical(cIGZWin*, std::uint32_t, bool, cISC4Occupant* occupant) {
    IQueryHost* host = QueryHost();
    if (!host || !host->LotManager()) return false;
    IQueryLot* lot = host->LotManager()->GetLotForOccupant(occupant);
    if (!lot) return false;
    lot->AddRef();
    lot->Release();
    return true;
}

// @ 0x1002da692
bool cSC4ViewInputControlQuery::SetOrGetResidentialBuildingMayorApproval(cIGZWin* win, std::uint32_t id, bool set,
                                                                         cISC4Occupant* occupant) {
    if (!occupant || !set) return true;
    cRZString text;
    GetRatingIn3DegreesFromPercent(text, GetMayorRatingPercent(occupant));
    SetStaticControlText(win, id, text);
    return true;
}

// @ 0x1002d2616
void cSC4ViewInputControlQuery::GetOccupantName(cISC4Occupant* occupant, cRZString& out) {
    if (!occupant) return;
    void* building = nullptr;
    if (occupant->QueryInterface(kIID_cISC4BuildingOccupant, &building)) {
        auto* named = static_cast<IQueryBuildingOccupant*>(building);
        named->GetName(out);
        named->Release();
    }
}

// @ 0x1002da640. Quirk kept: the `percent < 33.3` arm of the outer test can never run (the outer test already
// established 33.3 <= percent); only the boundary values matter.
void cSC4ViewInputControlQuery::GetRatingIn3DegreesFromPercent(cRZString& out, float percent) {
    std::uint32_t instance;
    if (33.3f <= percent) {
        instance = (66.6f <= percent) ? kStrRatingGood : kStrRatingFair;
    } else {
        instance = kStrRatingBad;
    }
    IQueryHost* host = QueryHost();
    if (host) host->LoadString(kStringGroup, instance, out);
}

// @ 0x1002da4f0: the average aura over the cells of the occupant's lot, as a percentage (0..100).
float cSC4ViewInputControlQuery::GetMayorRatingPercent(cISC4Occupant* occupant) {
    IQueryHost* host = QueryHost();
    if (!occupant || !host || !host->City() || !host->LotManager() || !host->AuraSim()) return 0.0f;
    float position[3] = {0.0f, 0.0f, 0.0f};
    occupant->GetPosition(position);
    int cellX = 0, cellZ = 0;
    host->City()->PositionToCell(position[0], position[2], cellX, cellZ);
    IQueryLot* lot = host->LotManager()->GetLotAtCell(cellX, cellZ, 0);
    if (!lot) return 0.0f;
    lot->AddRef();
    SC4CellRect rect;
    lot->GetCellRect(rect);
    int count = 0;
    int total = 0;
    for (int x = rect.x0; x <= rect.x1; ++x) {
        for (int z = rect.z0; z <= rect.z1; ++z) {
            ++count;
            total += 0x80 + static_cast<int>(host->AuraSim()->GetAura(x, z));
        }
    }
    lot->Release();
    if (count == 0) return 0.0f;
    return (static_cast<float>(total) / static_cast<float>(count)) * 100.0f / 255.0f;
}

// @ 0x1002db37e
bool cSC4ViewInputControlQuery::EnumFuncSetFilter(cIGZWin*, std::uint32_t, void* child, void* context) {
    // PARTIAL: the original calls the child's vslot 0x3a0 (a cGZWin method past the cIGZWin interface) with the
    // context, then EnumChildren(kGZIID_cIGZWin, EnumFuncSetFilter, context).
    (void)child;
    (void)context;
    return true;
}

// @ 0x1002db3bc
bool cSC4ViewInputControlQuery::EnumFuncClearFilter(cIGZWin*, std::uint32_t, void* child, void* context) {
    // PARTIAL: clears the five slider/filter variants (0xea563ab0, 0x8adc29d4, 0x6a8cc9d6, 0x2aa6e8ad, 0xab3f6db)
    // through vslot 0x3a8 / 0x388 / 0x390 and re-enumerates; needs cGZWin's methods past the cIGZWin interface.
    (void)child;
    (void)context;
    return true;
}

// @ 0x1002db566 / 0x1002db5d8 / 0x1002db69e (all three index the same id -> handler table)
bool cSC4ViewInputControlQuery::EnumFuncSetValue(cIGZWin*, std::uint32_t, void* child, void* context) {
    (void)child;
    (void)context;
    return true;
}
bool cSC4ViewInputControlQuery::EnumFuncUpdateValue(cIGZWin*, std::uint32_t, void* child, void* context) {
    (void)child;
    (void)context;
    return true;
}
bool cSC4ViewInputControlQuery::EnumFuncGetValue(cIGZWin*, std::uint32_t, void* child, void* context) {
    (void)child;
    (void)context;
    return true;
}

// @ 0x1002db70e / 0x1002db748
bool cSC4ViewInputControlQuery::EnumFuncNeighborDialogSetValue(cIGZWin* win, std::uint32_t id, void* cellInfo,
                                                              void* occupant) {
    // PARTIAL: the original also re-enumerates the dialog with this same callback; the row dispatcher itself is
    // partial (see SetOrGetControlValue), so only the call is kept.
    (void)occupant;
    SetOrGetControlValue(win, id, true, static_cast<tCellInfo*>(cellInfo));
    return true;
}
bool cSC4ViewInputControlQuery::EnumFuncNeighborDialogGetValue(cIGZWin* win, std::uint32_t id, void* cellInfo,
                                                              void* occupant) {
    (void)occupant;
    SetOrGetControlValue(win, id, false, static_cast<tCellInfo*>(cellInfo));
    return true;
}

// --- PARTIAL text builders ---------------------------------------------------------------------------------
// None of these is implemented yet; they are declared so the message plumbing compiles. Every one of them needs
// unrecovered neighbours (listed in the ledger PARTIAL section), so they leave the strings empty.
void cSC4ViewInputControlQuery::GetBuildingOccupantTrafficInfo(cRZString&, cRZString&, std::uint32_t& icon) {
    icon = 0;
}
void cSC4ViewInputControlQuery::GetNetworkOccupantTrafficInfo(cRZString&, cRZString&) {}
void cSC4ViewInputControlQuery::GetBuildingOccupantTipInfo(cRZString&, cRZString&, std::uint32_t& icon,
                                                           std::uint32_t& meter, float&) {
    icon = 0;
    meter = 0;
}
void cSC4ViewInputControlQuery::GetNetworkOccupantSummaryInfo(cRZString&, cRZString&) {}
void cSC4ViewInputControlQuery::GetPropOccupantSummaryInfo(cRZString&) {}
void cSC4ViewInputControlQuery::GetFloraOccupantSummaryInfo(cRZString&) {}
void cSC4ViewInputControlQuery::GetTerrainSummaryInfo(cRZString&, cRZString&) {}
void cSC4ViewInputControlQuery::DescribePick(int, int, cRZString&, cRZString&, cRZString&, bool) {}
void cSC4ViewInputControlQuery::GetOccupantPaths() {}
void cSC4ViewInputControlQuery::ShowAllRoutesThatIntersectCityRect(const SC4CellRect&) {}
void cSC4ViewInputControlQuery::WriteDetailedDebugInfo(int, int) {}

}  // namespace sc4
