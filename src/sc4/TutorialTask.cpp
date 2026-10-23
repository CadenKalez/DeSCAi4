// cSC4TutorialTask (SC4TutorialTask.cpp in the original): the tutorial task object and its cTutorialString texts.
// Hand-written from the Ghidra decompilation of 0x10011d466..0x10011f4a2 (+ cTutorialString 0x10011d60c..0x10011f468);
// see the header for the layout, the Lua protocol and the quirks kept.
#include "sc4/TutorialTask.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "lua/SCLuaHelp.h"
#include "rz/RZStringUtils.h"
#include "rz/StringConversion.h"
#include "sc4/BuildingOccupant.h"
#include "sc4/DisasterManagers.h"
#include "sc4/SpiralIterator.h"
#include "sc4/ViewInputControlEffect.h"

namespace sc4 {

namespace {

ITutorialTaskHost* g_host = nullptr;

// The cRZString globals of the original static init (0x10011fae2). Their text is what the tutorial scripts use.
const cRZString kTutorialTaskTableName("tutorialtasks");
const cRZString kTutorialInstructionFieldName("instruction_msg");
const cRZString kTutorialTryAgainMsgFieldName("try_again_msg");
const cRZString kTutorialCongratulationMsgFieldName("congratulation_msg");
const cRZString kTutorialTaskActionName("task_action");
const cRZString kTutorialTaskFunctionIndexName("function_index");
const cRZString kTutorialTaskStartXName("startX");
const cRZString kTutorialTaskStartZName("startZ");
const cRZString kTutorialTaskEndXName("endX");
const cRZString kTutorialTaskEndZName("endZ");
const cRZString kTutorialTaskEnableButtonsName("target_buttons");
const cRZString kTutorialTaskTargetGUIDName("target_guid");
const cRZString kTutorialExtraDataName("extraData");
const cRZString kTutorialZoomLevelName("zoomLevel");
const cRZString kTutorialCameraXName("cameraX");
const cRZString kTutorialCameraZName("cameraZ");
const cRZString kTutorialTaskDecalColorName("decal_color");
const cRZString kTutorialTaskDefaultDecalColor("selection_yellow");
const cRZString kTutorialWindowOffsetXName("window_offsetX");
const cRZString kTutorialWindowOffsetYName("window_offsetY");
const cRZString kTutorialNoEdgeScrollName("no_edge_scroll");
const cRZString kTutorialCSIName("csi");

const char kMissingTextPrefix[] = "***_NOT_IN_TEXT_DATABASE_*** ";   // same text cSC4Advice uses

// Window ids the target-window walk looks for (0x10011e890..0x10011ea3d).
constexpr std::uint32_t kTutorialParentWindowId = 0x0a78827a;   // the parent whose child is the alt window
constexpr std::uint32_t kTutorialAltWindowId = 0xc991eda8;      // "city view" window of an unestablished city
constexpr std::uint32_t kTutorialWindowIdA = 0xa9ed5617;
constexpr std::uint32_t kTutorialWindowIdB = 0x69b9324a;
constexpr std::uint32_t kTutorialWindowIdC = 0x4a551a6b;

// The tutorial resource key formats (kTutorialResKeyStringFormat1..4 at 0x100affee0..f8).
constexpr const char* kTutorialResKeyStringFormats[4] = {"text@0x%08x", "text@%08x", "text@%u", "text@%i"};

}  // namespace

ITutorialTaskHost* TutorialTaskHost() { return g_host; }
void SetTutorialTaskHost(ITutorialTaskHost* host) { g_host = host; }

// ---- cTutorialString ---------------------------------------------------------------------------------------------

// The original re-dispatches through the vtable slot of the two-argument overload (0x10011e03c) after taking strlen.
void cTutorialString::FromScriptText(const char* text) {
    FromScriptText(text, static_cast<unsigned int>(std::strlen(text)));
}

void cTutorialString::FromScriptText(const char* text, unsigned int length) { init(text, static_cast<int>(length)); }

// 0x10011f384: parses the instance out of the script text; length is unused.
void cTutorialString::init(const char* text, int /*length*/) {
    cGZPersistResourceKey key{kResourceTypeSLString, kTutorialStringGroup, 0};
    int value = 0;
    bool parsed = false;
    for (const char* format : kTutorialResKeyStringFormats) {
        if (std::sscanf(text, format, &value) == 1) {
            parsed = true;
            break;
        }
    }
    if (parsed) key.instance = static_cast<std::uint32_t>(value);
    SetKey(key);
}

cIGZString& cTutorialString::AssignFromIGZString(const cIGZString& other) {
    return cRZString::AssignFromIGZString(other);
}

// ---- cSC4TutorialTask --------------------------------------------------------------------------------------------

cSC4TutorialTask::cSC4TutorialTask() = default;

cSC4TutorialTask::~cSC4TutorialTask() {
    // The original releases the decal without stopping it first (0x10011d68c).
    if (decal_) {
        IToolVisualEffect* decal = decal_;
        decal_ = nullptr;
        decal->Release();
    }
}

bool cSC4TutorialTask::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4TutorialTask) {
        *out = static_cast<cISC4TutorialTask*>(this);
        AddRef();
        return true;
    }
    return cRZUnknown::QueryInterface(iid, out);
}

std::uint32_t cSC4TutorialTask::AddRef() { return cRZUnknown::AddRef(); }

std::uint32_t cSC4TutorialTask::Release() { return cRZUnknown::Release(); }

// 0x10011d7b6
bool cSC4TutorialTask::Init(cISCLua* lua, int taskNumber) {
    lua_ = lua;
    taskNumber_ = taskNumber;
    const int top = lua->GetTop();
    lua->GetGlobal(kTutorialTaskTableName.ToChar());
    const int taskCount = lua->GetN(-1);
    valid_ = taskNumber <= taskCount;
    if (taskNumber != 0 && taskNumber <= taskCount) {
        lua->PushNumber(static_cast<double>(taskNumber));
        lua->GetTable(-2);
        valid_ = lua->IsTable(-1);
    }

    // The three messages: the script text names a string resource; when it is not in the string database the text
    // itself becomes "<prefix><script text>" converted from code page 1252 (0x10011d98f..0x10011dbdb).
    const auto loadMessage = [this](const cRZString& field, cTutorialString& target) {
        cRZString text;
        if (!GetString(field, text)) return;
        target.FromScriptText(text.ToChar(), static_cast<unsigned int>(std::strlen(text.ToChar())));
        if (target.Load(true, false)) return;
        std::string combined = std::string(kMissingTextPrefix) + text.ToChar();
        text.AssignFromChars(combined.data(), static_cast<std::uint32_t>(combined.size()));
        ConvertStringEncoding(text, 1252, text);
        target.Copy(text);
    };
    loadMessage(kTutorialInstructionFieldName, instruction_);
    loadMessage(kTutorialTryAgainMsgFieldName, tryAgainMsg_);
    loadMessage(kTutorialCongratulationMsgFieldName, congratulationMsg_);

    zoomLevel_ = GetNumber(kTutorialZoomLevelName);
    cameraX_ = GetNumber(kTutorialCameraXName);
    cameraZ_ = GetNumber(kTutorialCameraZName);
    windowOffsetX_ = GetNumber(kTutorialWindowOffsetXName);
    windowOffsetY_ = GetNumber(kTutorialWindowOffsetYName);
    noEdgeScroll_ = GetNumber(kTutorialNoEdgeScrollName) > 0;
    csi_ = GetNumber(kTutorialCSIName) > 0;
    GetString(kTutorialTaskDecalColorName, decalColor_, kTutorialTaskDefaultDecalColor);
    GetTaskInformation();
    GetEnableButtonList(kTutorialTaskEnableButtonsName);
    lua->SetTop(top);
    valid_ = true;   // quirk: the original re-arms the flag even when the task was not found (0x10011dcd5)

    // The region grid of the two functions that highlight cells (0x10011dcf4..0x10011de65).
    if (whichFunction_ == 0x11 || whichFunction_ == 2) {
        const int width = endX_ - startX_;
        const int height = endZ_ - startZ_;
        tallerThanWide_ = width < height;
        if (height >= 0) {
            for (int row = 0; row <= height; ++row) {
                grid_.emplace_back();
                if (width < 0) continue;
                for (int col = 0; col <= width; ++col) {
                    cGridData cell;
                    cell.cellX = startX_ + col;
                    cell.cellZ = startZ_ + row;
                    bool border = (row == height) || (row == 0);
                    if (!tallerThanWide_) border = (col == 0) || (col == width);
                    cell.border = (whichFunction_ == 2) && border;
                    grid_.back().push_back(cell);
                }
            }
        }
    }
    return true;   // always (0x10011de92)
}

// 0x10011df28
bool cSC4TutorialTask::GetString(const cRZString& field, cRZString& out) {
    if (!valid_) return false;
    out.Erase(0, 0xffffffff);
    lua_->PushString(field.ToChar());
    lua_->GetTable(-2);
    if (lua_->IsString(-1)) {
        out.AssignFromCString(lua_->ToString(-1));
        lua_->Pop(1);
        return true;
    }
    lua_->Pop(1);
    return false;
}

// 0x10011e05a: clears the valid flag (and leaks the value on the Lua stack) when the field is not a number.
std::int32_t cSC4TutorialTask::GetNumber(const cRZString& field) {
    std::int32_t value = 0;
    if (!valid_ || lua_ == nullptr) return 0;
    lua_->PushString(field.ToChar());
    lua_->GetTable(-2);
    if (!lua_->IsNumber(-1)) {
        valid_ = false;
        return 0;
    }
    const double number = lua_->ToNumber(-1);
    lua_->Pop(1);
    value = static_cast<std::int32_t>(number);
    return value;
}

// 0x10011e10e: the default is copied (its std::string, as the original does) when the field is not a string.
bool cSC4TutorialTask::GetString(const cRZString& field, cRZString& out, const cRZString& def) {
    if (!valid_) return false;
    out.Erase(0, 0xffffffff);
    lua_->PushString(field.ToChar());
    lua_->GetTable(-2);
    if (lua_->IsString(-1)) out.AssignFromCString(lua_->ToString(-1));
    else out = def;
    lua_->Pop(1);
    return true;
}

// 0x10011e200
bool cSC4TutorialTask::GetTaskInformation() {
    if (!valid_) return false;
    lua_->GetTop();   // the original asks for the stack height and drops the result
    lua_->PushString(kTutorialTaskActionName.ToChar());
    lua_->GetTable(-2);
    valid_ = lua_->IsTable(-1);
    if (!valid_) return false;   // quirk: the action value stays on the stack
    whichFunction_ = GetNumber(kTutorialTaskFunctionIndexName);
    if (whichFunction_ == 0) return false;   // quirk: the action table stays on the stack
    startX_ = GetNumber(kTutorialTaskStartXName);
    startZ_ = GetNumber(kTutorialTaskStartZName);
    endX_ = GetNumber(kTutorialTaskEndXName);
    endZ_ = GetNumber(kTutorialTaskEndZName);
    region_.x0 = startX_;
    region_.z0 = startZ_;
    region_.x1 = endX_;
    region_.z1 = endZ_;
    extraData_ = GetNumber(kTutorialExtraDataName);
    targetGUID_ = GetTargetObjectGuid(kTutorialTaskTargetGUIDName);
    lua_->Pop(1);
    return true;
}

// 0x10011e35c: appends the hex GUIDs of the "target_buttons" array; for function 4 the target GUID is inserted at
// the front (not appended) after the table walk.
bool cSC4TutorialTask::GetEnableButtonList(const cRZString& field) {
    if (!valid_) return false;
    lua_->PushString(field.ToChar());
    lua_->GetTable(-2);
    if (!lua_->IsTable(-1)) {
        lua_->Pop(1);
        valid_ = false;
        return false;
    }
    std::vector<cRZString> names;
    SCLuaHelp::ReadTableAsArray(lua_, names, -1);
    for (const cRZString& name : names)
        buttonGuids_.push_back(RZAtoUint32HexA(name.ToChar()));
    lua_->Pop(1);
    if (targetGUID_ != 0 && whichFunction_ == 4)
        buttonGuids_.insert(buttonGuids_.begin(), targetGUID_);
    return true;
}

// 0x10011e680: parses the target GUID whether or not GetString found the field (the empty text parses to 0).
std::uint32_t cSC4TutorialTask::GetTargetObjectGuid(const cRZString& field) {
    cRZString text;
    GetString(field, text);
    return RZAtoUint32HexA(text.ToChar());
}

bool cSC4TutorialTask::Shutdown() { return true; }

// 0x10011e526: the original body is empty (links in tutorial text are ignored).
bool cSC4TutorialTask::DoLink(const cIGZString& /*link*/) { return true; }

std::uint32_t cSC4TutorialTask::GetTargetGUID() const { return targetGUID_; }

std::uint32_t cSC4TutorialTask::GetGUID() const { return guid_; }

int cSC4TutorialTask::GetZoomLevel() const { return zoomLevel_; }

bool cSC4TutorialTask::HasButtonGUID(std::uint32_t guid) {
    for (std::uint32_t candidate : buttonGuids_)
        if (candidate == guid) return true;
    return false;
}

std::uint32_t cSC4TutorialTask::GetExtraData() const { return static_cast<std::uint32_t>(extraData_); }

int cSC4TutorialTask::GetWhichFunction() const { return whichFunction_; }

std::uint32_t cSC4TutorialTask::GetCatalogButtonItemID() const {
    return buttonGuids_.empty() ? 0u : buttonGuids_.front();
}

const cIGZString& cSC4TutorialTask::GetCongratulationMsg() const { return congratulationMsg_; }

const cIGZString& cSC4TutorialTask::GetInstructionMsg() const { return instruction_; }

const cIGZString& cSC4TutorialTask::GetTryAgainMsg() const { return tryAgainMsg_; }

const SC4TutorialRect& cSC4TutorialTask::GetRegion() const { return region_; }

// 0x10011e5a6
bool cSC4TutorialTask::checkGridDataJobFinished(int cellX, int cellZ) {
    if (whichFunction_ != 0x11 && whichFunction_ != 2) return true;
    if (cellX < startX_ || endX_ < cellX || endZ_ < cellZ || cellZ < startZ_) return false;
    grid_[static_cast<std::size_t>(cellZ - startZ_)][static_cast<std::size_t>(cellX - startX_)].border = true;
    bool all = true;
    for (const std::vector<cGridData>& row : grid_)
        for (const cGridData& cell : row) all = all && cell.border;
    return all;
}

// 0x10011e7c0: walks up from each button window to the 3D view root and answers the first one that qualifies.
// The walk follows the original refcount / visibility dance exactly; the returned pointer is borrowed.
cIGZWin* cSC4TutorialTask::GetTargetWindow() {
    ITutorialTaskHost* host = TutorialTaskHost();
    if (!host) return nullptr;
    const bool established = host->CityEstablished();
    if (buttonGuids_.empty()) return nullptr;
    cIGZWin* result = nullptr;
    for (std::size_t index = 0; index < buttonGuids_.size(); ++index) {
        const std::uint32_t guid = buttonGuids_[index];
        if (guid == 0) continue;
        cIGZWin* root = host->View3DMainWindow();
        if (!root) continue;   // the original dereferences SL::spView3D unchecked
        cIGZWin* window = root->GetChildWindowFromIDRecursive(guid);
        if (!window) continue;
        window->AddRef();
        window->SetFlag(kWinEnabled, true);
        if (result == nullptr) {
            window->AddRef();   // the walk releases `current` as it moves up (0x10011e828)
            bool visible = window->IsVisible();
            cIGZWin* current = window;
            while (current != host->View3DMainWindow() && visible) {
                visible = current->IsVisible();
                if (!established) {
                    cIGZWin* parent = current->GetParentWin();
                    if (parent && parent->GetID() == kTutorialParentWindowId) {
                        cIGZWin* substitute = host->View3DMainWindow()->GetChildWindowFromIDRecursive(kTutorialAltWindowId);
                        if (current != substitute) {
                            if (substitute) substitute->AddRef();
                            current->Release();
                            current = substitute;
                            if (!current) break;   // the original would walk off the null (no check at 0x10011e83e)
                        }
                        const std::uint32_t windowId = window->GetID();
                        std::uint32_t wanted = 0;
                        if (windowId == kTutorialWindowIdA) wanted = kTutorialWindowIdA;
                        else if (windowId == kTutorialWindowIdB) wanted = kTutorialWindowIdB;
                        else if (windowId == kTutorialWindowIdC) wanted = kTutorialWindowIdC;
                        if (wanted != 0) {
                            cIGZWin* child = current->GetChildWindowFromID(wanted);
                            if (window != child) {
                                if (child) child->AddRef();
                                window->Release();
                                window = child;
                            }
                        }
                    } else {
                        // The original asks for the parent a second time here (0x10011e8d5).
                        parent = current->GetParentWin();
                        if (current != parent) {
                            if (parent) parent->AddRef();
                            current->Release();
                            current = parent;
                            if (!current) break;   // as above
                        }
                    }
                } else {
                    cIGZWin* parent = current->GetParentWin();
                    if (current != parent) {
                        if (parent) parent->AddRef();
                        current->Release();
                        current = parent;
                        if (!current) break;
                    }
                }
            }
            if (window == nullptr) result = nullptr;
            else if (visible) {
                window->AddRef();
                result = window;
            }
            if (current) current->Release();
        }
        if (window) window->Release();
    }
    if (result) result->Release();   // balances the result reference; the caller gets the borrowed pointer back
    return result;
}

// 0x10011eb7e
bool cSC4TutorialTask::ExecuteTask() {
    if (started_) return true;
    std::uint32_t function = static_cast<std::uint32_t>(whichFunction_);
    if (function == 0) return false;
    ITutorialTaskHost* host = TutorialTaskHost();
    if (cameraX_ != 0) {
        if (cameraZ_ != 0) {
            JumpToLocation(cameraX_, cameraZ_);
            function = static_cast<std::uint32_t>(whichFunction_);
        }
    } else if (cameraZ_ == 0) {
        cameraX_ = (startX_ + endX_) / 2;
        cameraZ_ = (startZ_ + endZ_) / 2;
        if (static_cast<std::uint32_t>(endX_ + 1 + startX_) >= 3 && cameraZ_ != 0)
            JumpToLocation(cameraX_, cameraZ_);
    }
    if (function <= 0x11) {
        if ((0x2010eU & (1u << (function & 0x1f))) == 0) {
            if (function == 5) {
                if (host) host->HiddenPauseSimulator();
            } else if (function != 9) {
                goto afterEffects;   // 0x10011ec5a
            } else {
                TriggerFire(region_);
            }
        }
        drawDecals(startX_, startZ_, endX_, endZ_);
    }
afterEffects:
    if (host) {
        ITutorialCSIVisibility* csi = host->CSIVisibility();
        if (csi) {
            csiVisibleSaved_ = csi->GetVisible();
            csi->SetVisible(csi_);
        }
    }
    started_ = true;
    return true;
}

// 0x10011ecf2: jumps the camera to a cell when the city, the renderer and its camera controller are all there.
void cSC4TutorialTask::JumpToLocation(int cellX, int cellZ) {
    ITutorialTaskHost* host = TutorialTaskHost();
    if (!host) return;
    if (!host->CityCellIsInBounds(cellX, cellZ)) return;
    if (!host->CameraControlAvailable()) return;
    const float cellWidthX = host->TerrainCellWidth();
    const float cellWidthZ = host->TerrainCellWidth();
    const float x = cellWidthX * static_cast<float>(cellX);
    const float z = cellWidthZ * static_cast<float>(cellZ);
    const float y = host->TerrainAltitude(x, z);
    host->SetCameraTargetPosition(x, y, z);
}

// 0x10011edfe
bool cSC4TutorialTask::EndTask() {
    bool result = true;
    if (started_) {
        ITutorialTaskHost* host = TutorialTaskHost();
        if (host) {
            ITutorialCSIVisibility* csi = host->CSIVisibility();
            if (csi && csiVisibleSaved_ != csi_) csi->SetVisible(csiVisibleSaved_);
        }
        if (decal_) {
            decal_->Stop(1);
            if (decal_) {
                IToolVisualEffect* decal = decal_;
                decal_ = nullptr;
                decal->Release();
            }
        }
        if (whichFunction_ == 0) {
            result = false;   // quirk: leaves started_ set
        } else {
            if (whichFunction_ == 5 && host) host->HiddenResumeSimulator();
            started_ = false;
        }
    }
    return result;
}

// 0x10011eeba: the catalog button GUID compared against the task's target GUID.
bool cSC4TutorialTask::handleButtons(std::uint32_t guid) {
    const std::uint32_t target = GetTargetGUID();
    return target == guid && target != 0;
}

// 0x10011eedc: spiral out from the middle of the rectangle until a lot with a building answers.
cISC4BuildingOccupant* cSC4TutorialTask::FindNearestBuilding(const SC4TutorialRect& area) {
    ITutorialTaskHost* host = TutorialTaskHost();
    if (!host) return nullptr;
    const int left = area.x0;
    const int top = area.z0;
    const int right = area.x1;
    const int bottom = area.z1;
    const int maxX = right < left ? left : right;
    const int maxZ = bottom < top ? top : bottom;
    const int centreX = (left + right) / 2;
    const int centreZ = (top + bottom) / 2;
    SpiralIterator spiral;   // the original inlines a variant of SpiralIterator::Over with the rectangle as range
    spiral.x = centreX;
    spiral.z = centreZ;
    spiral.targetX = centreX + 1;
    spiral.targetZ = centreZ;
    spiral.minX = left;
    spiral.minZ = top;
    spiral.maxX = maxX;
    spiral.maxZ = maxZ;
    spiral.mode = 1;
    spiral.direction = 1;
    spiral.state = 0;
    spiral.onZ = 1;
    spiral.edgesHit = 0;
    spiral.turns = 0;
    if (centreX < left || centreZ < top || maxX < centreX || maxZ < centreZ) {
        spiral.state = 2;
        spiral.edgesHit = 0xf;
    }
    int cellX = 0;
    int cellZ = 0;
    while (spiral.GetNext(cellX, cellZ)) {
        cISC4BuildingOccupant* building = host->BuildingAtCell(cellX, cellZ);
        if (building) return building;
    }
    return nullptr;
}

// 0x10011f04c: pick the nearest building in the rectangle and light it on fire with cause 6.
bool cSC4TutorialTask::TriggerFire(const SC4TutorialRect& area) {
    cISC4BuildingOccupant* building = FindNearestBuilding(area);
    if (!building) return false;
    building->AddRef();
    ITutorialTaskHost* host = TutorialTaskHost();
    cISC4DisasterLayer* layer = host ? host->DisasterLayer() : nullptr;
    if (!layer) {
        building->Release();
        return false;
    }
    layer->AddRef();
    cISC4DisasterManager* manager = layer->GetDisasterManager(kDisasterManagerIdFire);
    if (manager) manager->AddRef();
    bool result = false;
    if (manager) {   // the original dereferences the manager without a null check (0x10011f0c7)
        cISC4DisasterManagerFire* fire = nullptr;
        if (manager->QueryInterface(kGZIID_cISC4DisasterManagerFire, reinterpret_cast<void**>(&fire))) {
            fire->IgniteOccupant(building->AsOccupant());
            fire->SetCause(6);
            result = true;
        }
        if (fire) fire->Release();
        manager->Release();
    }
    layer->Release();
    building->Release();
    return result;
}

// 0x10011f1a4
void cSC4TutorialTask::ShowHintsForUsers() {
    if (whichFunction_ == 0x11 || whichFunction_ == 2 || whichFunction_ == 1)
        drawDecals(startX_, startZ_, endX_, endZ_);
}

// 0x10011f1e2: (re)creates the highlight decal effect; the rectangle is passed one cell larger.
bool cSC4TutorialTask::drawDecals(int x0, int z0, int x1, int z1) {
    ITutorialTaskHost* host = TutorialTaskHost();
    if (!host) return true;
    IToolEffectsManager* manager = host->EffectsManager();
    if (!manager) return true;
    manager->AddRef();
    if (decal_) {
        decal_->Stop(0);
        if (decal_) {
            IToolVisualEffect* decal = decal_;
            decal_ = nullptr;
            decal->Release();
        }
    }
    if (manager->HasEffect(decalColor_.ToChar())) {
        if (decal_) {
            IToolVisualEffect* decal = decal_;
            decal_ = nullptr;
            decal->Release();
        }
        if (manager->CreateEffect(decalColor_.ToChar(), &decal_)) {
            const float values[4] = {static_cast<float>(x0), static_cast<float>(z0),
                                     static_cast<float>(x1 + 1), static_cast<float>(z1 + 1)};
            decal_->SetFloatParams(0xc, values, 4);
            decal_->Start(0);
        }
    }
    manager->Release();
    return true;
}

int cSC4TutorialTask::GetWindowOffsetX() const { return windowOffsetX_; }

int cSC4TutorialTask::GetWindowOffsetY() const { return windowOffsetY_; }

bool cSC4TutorialTask::IsEdgeScrollingAllowed() const { return !noEdgeScroll_; }

}  // namespace sc4
