// cSC4WinLotConfigurationEditor — see WinLotConfigurationEditor.h for the layout and the interfaces.
//
// This file recovers the window plumbing and the editing path of the class. The window-script driven menus and
// dialogs (foundation / fence / object / edit-area menus, the family and texture selectors, the save-as browser,
// the 3D preview rasterisation and the 3D-view rebuild) are not recovered: the ones reached from the recovered
// code go through IWinLotConfigurationEditorHost seams and are marked PARTIAL in
// notes/ledger_pending/cSC4WinLotConfigurationEditor.txt, which also lists the functions left SKIPPED.
#include "sc4/WinLotConfigurationEditor.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gz/SmallObjects.h"
#include "rz/cRZIniFile.h"
#include "sc4/LotConfigurationManager.h"

namespace sc4 {
namespace {

IWinLotConfigurationEditorHost* g_host = nullptr;
bool g_staticInitDone = false;
std::unordered_map<std::uint32_t, cRZString> g_buildingFamilyNames;
std::unordered_map<std::uint32_t, cRZString> g_floraFamilyNames;
std::unordered_map<std::uint32_t, cRZString> g_propFamilyNames;

// ConvertToUint @ 0x10001a307: the ini keys are decimal; a 0x prefix is accepted here as well.
std::uint32_t ConvertToUint(const cIGZString& text) {
    return static_cast<std::uint32_t>(std::strtoul(text.ToChar(), nullptr, 0));
}

// sLotConfigurationObjectTypeName @ 0x100aae580 (cRZString* table, entries 0..7).
const char* const kObjectTypeNames[8] = {
    "building", "prop", "lot texture", "foundation", "flora", "unknown", "unknown", "network",
};

const char* ObjectTypeName(std::uint8_t type) {
    return kObjectTypeNames[type < 8 ? type : 5];
}

// The order LoadCursors stores them in: +0x1f8, +0x200, +0x208, +0x210, +0x218, +0x220, +0x228.
const std::uint32_t kCursorInstances[7] = {0x4a5cdc3f, 0x4a5cdc3a, 0x4a5cdc3c, 0x4a5cdc3e,
                                           0x4a5cdc40, 0x4a5cdc3d, 0x4a5cdc3b};

// Movement step (DAT_100a20a60 = {0.5f, 0.25f}): half a world unit, a quarter with Shift; x4 with Control, x8 with Alt.
float MoveStep(std::uint32_t modifiers) {
    float step = (modifiers & 2) ? 0.25f : 0.5f;
    if (modifiers & 1) step *= 4.0f;
    if (modifiers & 4) step *= 8.0f;
    return step;
}

}  // namespace

IWinLotConfigurationEditorHost* WinLotConfigurationEditorHost() { return g_host; }
void SetWinLotConfigurationEditorHost(IWinLotConfigurationEditorHost* host) { g_host = host; }

const std::unordered_map<std::uint32_t, cRZString>& EditorBuildingFamilyNames() { return g_buildingFamilyNames; }
const std::unordered_map<std::uint32_t, cRZString>& EditorFloraFamilyNames() { return g_floraFamilyNames; }
const std::unordered_map<std::uint32_t, cRZString>& EditorPropFamilyNames() { return g_propFamilyNames; }

// ---- construction / destruction ---------------------------------------------------------------------------------

cSC4WinLotConfigurationEditor::cSC4WinLotConfigurationEditor(cIGZWinMgr* mgr) : cGZWinGen(mgr) {
    // @ 0x100417890: all fields start empty (the interface pointers are the ctor's zero-init in the original).
}

cSC4WinLotConfigurationEditor::~cSC4WinLotConfigurationEditor() {   // @ 0x100417a12
    ReleaseInterface(unused1e0_);
    if (view_) { gz::cIGZWinBMP* old = view_; view_ = nullptr; old->Release(); }
    for (cIGZCursor*& cursor : cursors_) {
        if (cursor) {
            cIGZCursor* old = cursor;
            cursor = nullptr;
            old->Release();
        }
    }
    ReleaseInterface(chooser_);
    if (lotConfigManager_) {
        cSC4LotConfigurationManager* old = lotConfigManager_;
        lotConfigManager_ = nullptr;
        old->Release();
    }
    // Quirks kept: the font (+0x1d8), the status text (+0x1f0), the identity references (+0x278 / +0x280) and the two
    // network objects (+0x290 / +0x298) are released by Shutdown only, never by the destructor.
    if (configuration_) {
        cSC4LotConfiguration* config = configuration_;
        configuration_ = nullptr;
        config->Release();
    }
    for (cIGZUnknown* object : attachedObjects_) {
        if (object) object->Release();
    }
    attachedObjects_.clear();
}

bool cSC4WinLotConfigurationEditor::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x100417d30
    if (iid == gz::kGZIID_cIGZWinSpyMessageFilter) {
        *out = static_cast<cIGZWinMessageFilter*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cGZWinAlt || iid == 1 || iid == kGZIID_cIGZWin) {
        *out = static_cast<cIGZWin*>(this);
        AddRef();
        return true;
    }
    return cGZWinGen::QueryInterface(iid, out);
}

void cSC4WinLotConfigurationEditor::ReleaseInterface(cIGZUnknown*& p) {
    if (!p) return;
    cIGZUnknown* old = p;
    p = nullptr;
    old->Release();
}

bool cSC4WinLotConfigurationEditor::Init() {   // @ 0x100417dac
    if (!g_staticInitDone) StaticInit();
    if (!get_init_flag()) {
        SetFlag(kWinPrivateBuffer, true);
        SetWinGenFlag(gz::kGenMovable, true);
        SetWinGenFlag(gz::kGenCloseButton, true);
        SetWinGenFlag(gz::kGenCloseDisabled, false);
        SetWinGenFlag(gz::kGenEscapeCloses, false);
        cGZWinGen::Init();
        if (!font_ && g_host) font_ = g_host->CreateStatusFont();
        if (g_host) {
            if (cSC4LotConfigurationManager* manager = g_host->LotConfigurationManager()) {
                manager->AddRef();
                cSC4LotConfigurationManager* old = lotConfigManager_;
                lotConfigManager_ = manager;
                if (old) old->Release();
            }
            g_host->RegisterWindow(this);   // window manager vslot 0x78: (window, 0x920b687, 0, 0, 0)
        }
        LoadCursors();
        SetCursor(cursors_[0], true);
    }
    return true;
}

void cSC4WinLotConfigurationEditor::StaticInit() {   // @ 0x100417f5e
    if (g_staticInitDone) return;
    g_staticInitDone = true;
    // The original asks cISC4App::GetDataDirectory(out, -1) (SL::spSC4 vslot 0x198), appends "Sys", '/',
    // "FamilyNames.ini" and enumerates the three sections with the callbacks below.
    if (!g_host) return;
    std::string path = g_host->DataDirectory();
    path += "Sys";
    path += '/';
    path += "FamilyNames.ini";
    cRZIniFile ini;
    ini.SetPath(cRZString(path.c_str()));
    ini.EnumEntries(cRZString("PropFamilies"), &cSC4WinLotConfigurationEditor::AddPropFamilyName, nullptr);
    ini.EnumEntries(cRZString("BuildingFamilies"), &cSC4WinLotConfigurationEditor::AddBuildingFamilyName, nullptr);
    ini.EnumEntries(cRZString("FloraFamilies"), &cSC4WinLotConfigurationEditor::AddFloraFamilyName, nullptr);
}

void cSC4WinLotConfigurationEditor::AddPropFamilyName(const cRZString& name, const cRZString& value, void*) {
    g_propFamilyNames[ConvertToUint(name)] = value;   // @ 0x10041849a
}
void cSC4WinLotConfigurationEditor::AddBuildingFamilyName(const cRZString& name, const cRZString& value, void*) {
    g_buildingFamilyNames[ConvertToUint(name)] = value;   // @ 0x1004184da
}
void cSC4WinLotConfigurationEditor::AddFloraFamilyName(const cRZString& name, const cRZString& value, void*) {
    g_floraFamilyNames[ConvertToUint(name)] = value;   // @ 0x10041851a
}

void cSC4WinLotConfigurationEditor::LoadCursors() {   // @ 0x100418198
    if (!g_host) return;
    for (int i = 0; i < 7; ++i) {
        cIGZCursor* fresh = g_host->CreateCursor(kLotEditorCursorGroup, kCursorInstances[i]);
        cIGZCursor*& slot = cursors_[i];
        if (slot == fresh) {
            if (fresh) fresh->Release();
            continue;
        }
        if (fresh) fresh->AddRef();   // the editor's reference
        cIGZCursor* old = slot;
        slot = fresh;
        if (old) old->Release();
        if (fresh) fresh->Release();   // the cSLCursor temporary's reference (its destructor drops it)
    }
}

void cSC4WinLotConfigurationEditor::PostInit() {   // @ 0x10041855e
    if (!view_) {
        if (!GetChildAs(kLotEditorViewChildId, gz::kGZIID_cIGZWinBMP, reinterpret_cast<void**>(&view_)) || !view_) {
            view_ = nullptr;
        } else {
            cIGZWin* viewWindow = view_->AsIGZWin();
            if (viewWindow) viewWindow->SetFlag(kWinIgnoreCursor, true);
            view_->SetWinBMPFlag(gz::kWinBMPNoFill, true);   // vslot 0x58: (2, 1)
            if (g_host) {
                int width = viewWindow ? viewWindow->GetW() : 0;
                int height = viewWindow ? viewWindow->GetH() : 0;
                if (cIGZBuffer* buffer = g_host->CreateViewBuffer(width, height)) {
                    view_->SetBMP(buffer);   // SetBMP takes its own reference
                    buffer->Release();
                    // InitAlphaSystem(buffer colour type) @ 0x10038fd68 is not recovered (alpha system).
                }
            }
        }
    }
    gz::cGZWinFlatRect* frame = nullptr;
    if (GetChildAs(kLotEditorFrameChildId, gz::kGZIID_cIGZWinFlatRect, reinterpret_cast<void**>(&frame)) && frame) {
        if (cIGZWin* frameWindow = frame->AsIGZWin()) frameWindow->SetFlag(kWinIgnoreCursor, true);
        frame->Release();   // the original does not keep this one
    }
    if (!statusText_) {
        if (!GetChildAs(kLotEditorStatusChildId, gz::kGZIID_cIGZWinText, reinterpret_cast<void**>(&statusText_)))
            statusText_ = nullptr;
    }
    if (!networkObject1_ && g_host) {
        networkObject1_ = g_host->CreateNetworkObject(3);
        if (networkObject1_) networkObject1_->AddRef();
    }
    if (!networkObject2_ && g_host) {
        networkObject2_ = g_host->CreateNetworkObject(0);
        if (networkObject2_) networkObject2_->AddRef();
    }
}

bool cSC4WinLotConfigurationEditor::Shutdown() {   // @ 0x100418776
    if (get_init_flag()) {
        if (g_host && chooser_) g_host->ChooserClosed(this);
        if (font_) { cIGZFont* old = font_; font_ = nullptr; old->Release(); }
        ReleaseInterface(unused1e0_);
        if (view_) { gz::cIGZWinBMP* old = view_; view_ = nullptr; old->Release(); }
        if (statusText_) { gz::cIGZWinText* old = statusText_; statusText_ = nullptr; old->Release(); }
        ReleaseInterface(chooser_);
        if (lotConfigManager_) {
            cSC4LotConfigurationManager* old = lotConfigManager_;
            lotConfigManager_ = nullptr;
            old->Release();
        }
        ReleaseInterface(identity_.exemplar);
        ReleaseInterface(identity_.propertyHolder);
        // The two network objects: vslot 0x20 (Shutdown) then vslot 0x10 (Release) in the original; the seam returns a
        // plain cIGZUnknown, so only the release is mirrored here.
        ReleaseInterface(networkObject1_);
        ReleaseInterface(networkObject2_);
        for (cIGZUnknown* object : attachedObjects_) {
            if (object) object->Release();
        }
        attachedObjects_.clear();
        // Quirks kept: the cursors (+0x1f8..+0x228) and the unreleased string vector (+0x2e0) are not touched here;
        // the cursors are released by the destructor.
    }
    return cGZWinGen::Shutdown();
}

// ---- identity ---------------------------------------------------------------------------------------------------

bool cSC4WinLotConfigurationEditor::GetIdentityInfoForLotConfigurationExemplarKey(
    const cGZPersistResourceKey& key, cSC4LotConfigurationIdentityInfo& out) {   // @ 0x10041891c
    if (!g_host) return false;
    cSC4LotConfigurationIdentityInfo info;
    if (!g_host->GetIdentityInfoForExemplarKey(key, info)) return false;
    out.name = info.name;
    out.exemplar = info.exemplar;
    out.propertyHolder = info.propertyHolder;
    if (out.exemplar) out.exemplar->AddRef();
    // Quirk: the original adds and immediately drops the reference on the property holder, so +0x40 holds a borrowed
    // pointer that Shutdown still releases once.
    if (out.propertyHolder) {
        out.propertyHolder->AddRef();
        out.propertyHolder->Release();
    }
    out.key = key;
    out.id = key.instance;
    return true;
}

bool cSC4WinLotConfigurationEditor::GetIdentityInfoForLotConfigurationType(
    std::uint32_t type, cSC4LotConfigurationIdentityInfo& out) {   // @ 0x1004176a4
    if (!lotConfigManager_) return false;
    cSC4LotConfiguration* configuration = lotConfigManager_->GetLotConfiguration(type);
    if (!configuration) return false;
    const std::uint32_t id = configuration->GetID();   // the manager's reference is not released (quirk kept)
    cGZPersistResourceKey key{0x6534284a, kLotConfigurationGroup, id};
    return GetIdentityInfoForLotConfigurationExemplarKey(key, out);
}

bool cSC4WinLotConfigurationEditor::IsEditingExemplar(std::uint32_t id) const {   // @ 0x100417880
    return identity_.id == id;
}

// ---- title / dirty flag -----------------------------------------------------------------------------------------

void cSC4WinLotConfigurationEditor::SetTitle() {   // @ 0x1004189d6
    std::string text = "Lot Editor -- ";
    if (configuration_) {
        cRZString name;
        configuration_->GetName(name);
        text += name.str();
    }
    if (dataChanged_) text += " *";
    title_.AssignFromCString(text.c_str());
    SetCaption(title_);           // vslot 0x250
    InvalidateSelfAndParents();   // vslot 0x2e0
}

void cSC4WinLotConfigurationEditor::SetDataHasChanged(bool changed) {   // @ 0x100419a1e
    dataChanged_ = changed;
    SetTitle();
}

// ---- configuration lifecycle ------------------------------------------------------------------------------------

bool cSC4WinLotConfigurationEditor::Open(const cGZPersistResourceKey& key) {   // @ 0x100417602
    // PARTIAL: the original continues with SetLotSize (0x100418b44), Prepare3DView (0x100418faa), GoTo3DView
    // (0x100419406) and Refresh3DView (0x10042e836); the first and the last are not recovered.
    PostInit();
    const bool ok = key.type == 0 ? CreateNewLotConfiguration() : ReadLotConfigurationExemplar(key);
    if (!ok) return false;
    if (key.type != 0) {
        if (!GetIdentityInfoForLotConfigurationExemplarKey(key, identity_)) return false;
    }
    SetTitle();
    RemoveOutOfBoundsLotConfigurationObjects();
    GoTo3DView();
    Refresh3DView();
    return true;
}

bool cSC4WinLotConfigurationEditor::CreateNewLotConfiguration() {   // @ 0x10042d9be
    identity_.id = 0;
    identity_.key = cGZPersistResourceKey{};
    identity_.name.AssignFromCString("");
    ReleaseInterface(identity_.exemplar);
    ReleaseInterface(identity_.propertyHolder);
    auto* configuration = new cSC4LotConfiguration();
    cSC4LotConfiguration* old = configuration_;
    if (old != configuration) {
        configuration->AddRef();
        configuration_ = configuration;
        if (old) old->Release();
    }
    configuration_->id_ = 0;
    configuration_->name_ = "New Lot";
    configuration_->sizeX_ = 1;
    configuration_->sizeZ_ = 1;
    configuration_->maxSlopeBeforeFoundation_ = 5.0f;
    configuration_->maxSlope_ = 90.0f;
    configuration_->minSlope_ = 0.0f;
    configuration_->water_ = false;
    dataChanged_ = false;
    SetTitle();
    return true;
}

bool cSC4WinLotConfigurationEditor::ReadLotConfigurationExemplar(const cGZPersistResourceKey& key) {   // @ 0x10042db5a
    if (!g_host) return false;
    cSC4LotConfiguration* configuration = nullptr;
    if (!g_host->ReadLotConfiguration(key, configuration) || !configuration) return false;
    cSC4LotConfiguration* old = configuration_;
    if (old != configuration) {
        configuration->AddRef();
        configuration_ = configuration;
        if (old) old->Release();
    }
    const int fixes = lotConfigManager_ ? lotConfigManager_->FixLotConfiguration(configuration_) : 0;
    if (fixes == 0) return true;
    dataChanged_ = true;
    SetTitle();
    std::string text = "This lot has components that have changed since the last edit.";
    switch (fixes) {
        case 1: text += "\n\nReason: The family has changed."; break;
        case 2: text += "\n\nReason: The family is now empty."; break;
        case 3: text += "\n\nReason: The footprint has changed."; break;
        case 4: text += "\n\nReason: The footprint was out of bounds and needs to be fixed."; break;
        case 5: text += "\n\nReason: The center has changed."; break;
        case 6: text += "\n\nReason: The retaining walls have changed."; break;
        default: break;
    }
    MessageBoxA(cRZString(text.c_str()), kMsgBoxStyleOk);
    return true;
}

bool cSC4WinLotConfigurationEditor::WriteLotData() {   // @ 0x100419a30
    if (!lotConfigManager_ || !g_host) return false;
    return g_host->WriteLotConfiguration(identity_.key, configuration_, nullptr);
}

void cSC4WinLotConfigurationEditor::GetDefaultPath(cRZString& out) {   // @ 0x100419a66
    std::string path = g_host ? g_host->DataDirectory() : std::string();
    path += "Exemplar";
    path += '/';
    path += "Lot Templates";
    path += '/';
    out.AssignFromCString(path.c_str());
}

std::uint32_t cSC4WinLotConfigurationEditor::MessageBoxA(const cRZString& text, std::uint32_t style) {   // @ 0x10041963e
    cRZString title;
    if (configuration_) configuration_->GetName(title);
    if (!g_host) return kMsgBoxCancel;
    return g_host->MessageBox(text, title, style);
}

bool cSC4WinLotConfigurationEditor::Save(bool showMessages, bool saveConfiguration) {   // @ 0x1004196f2
    if (identity_.key.type == 0) return SaveAs(false, showMessages, saveConfiguration);
    if (!VerifyObjectsAreWithinLotBounds(true, true)) return false;
    if (lotConfigManager_ && WriteLotData()) {
        dataChanged_ = false;
        SetTitle();
        if (g_host) g_host->LotConfigurationWritten(identity_.key);
        if (showMessages) MessageBoxA(cRZString("Lot configuration saved."), kMsgBoxStyleOk);
        return true;
    }
    if (saveConfiguration) MessageBoxA(cRZString("Error saving lot configuration."), kMsgBoxStyleWarning);
    return false;
}

bool cSC4WinLotConfigurationEditor::SaveAs(bool, bool, bool) {
    // PARTIAL: the original (0x100419aba) builds a cGZWinFileBrowser dialog, asks the path with
    // GetIdentityInfoForLotConfigurationExemplarPath (0x10041a1fe) and then calls Save. Both are not recovered.
    return false;
}

bool cSC4WinLotConfigurationEditor::VerifyObjectsAreWithinLotBounds(bool oneSided, bool showMessages) {   // @ 0x1004198f8
    if (!configuration_) return true;
    for (const cSC4LotConfigurationObject& object : configuration_->objects_) {
        if (object.type >= 5 || (0x13u >> object.type & 1u) == 0) continue;
        if (IsObjectWithinLotBounds(&object, oneSided)) continue;
        if (!showMessages) return false;
        const std::uint32_t answer = MessageBoxA(
            cRZString("One or more lot objects don't fit within the lot bounds. Do you want to continue or cancel?"),
            kMsgBoxStyleOkCancel);
        return answer == kMsgBoxOK;
    }
    return true;
}

bool cSC4WinLotConfigurationEditor::Close(bool askToSave, bool saveConfiguration) {   // @ 0x1004194ca
    bool proceed = false;
    if (askToSave && dataChanged_) {
        const std::uint32_t answer = MessageBoxA(
            cRZString("Do you want to save this lot configuration?"), kMsgBoxStyleYesNoCancel);
        proceed = answer == kMsgBoxNo || answer == kMsgBoxYes;
        if (answer != kMsgBoxCancel && answer != kMsgBoxNo && saveConfiguration) {
            if (dataChanged_) proceed = Save(false, true);
        }
    } else if (!saveConfiguration) {
        proceed = true;
    } else {
        proceed = dataChanged_ ? Save(false, true) : true;
    }
    if (!proceed) return false;
    if (chooser_ && g_host) g_host->ChooserClosed(this);
    if (cIGZWin* parent = GetParentWin()) parent->ChildDelete(this);   // vslot 0x80
    return true;
}

void cSC4WinLotConfigurationEditor::Refresh3DView() {
    // PARTIAL: 0x10042e836 rebuilds the view's occupant filter from the configuration objects and writes it into
    // the 3D view. Needs cSC4OccupantFilter / cISC4View3DWin, neither recovered; only the redraw is kept.
    InvalidateSelfAndParents();
}

void cSC4WinLotConfigurationEditor::GoTo3DView() {   // @ 0x100419406
    std::uint32_t centreX = 3, centreZ = 2;   // DAT_100a20940 when there is no configuration
    if (configuration_) {
        centreX = std::uint32_t(configuration_->sizeX_ / 2) + 2;
        centreZ = std::uint32_t(configuration_->sizeZ_ / 2) + 2;
    }
    const float x = static_cast<float>(centreX) * kLotTileSize;
    const float z = static_cast<float>(centreZ) * kLotTileSize;
    const float y = g_host ? g_host->TerrainHeight(static_cast<int>(centreX), static_cast<int>(centreZ)) : 0.0f;
    if (g_host) g_host->SetCameraTargetPosition(x, y, z);
}

// ---- messages ---------------------------------------------------------------------------------------------------

bool cSC4WinLotConfigurationEditor::DoMessage(cGZMessage& message) {   // @ 0x10041a5b4
    if (message.type == kGZMsgCommand) {
        if (message.param1 == gz::kWinCommandSender && message.param2 == gz::kWinGenCloseId) Close(true, true);
        return true;
    }
    if (message.type == kLotEditorInitMessage) {
        PostInit();
        return true;
    }
    return cGZWinGen::DoMessage(message);
}

bool cSC4WinLotConfigurationEditor::DoMessage(cIGZWin* window, cGZMessage& message) {   // @ 0x10041a5fe
    if (message.type == kGZMsgCommand) {
        const std::uint64_t sender = message.param1;
        if (sender == gz::kWinCommandSender) {
            const std::uint64_t command = message.param2;
            // DoDialogBoxGridSort @ 0x10041a68a classifies the command; the grid itself is not recovered.
            if ((command >= 0xe9432dd7 && command - 0xe9432dd7 < 2) || command == 0x6987b3fa ||
                command == 0x97cefb2 || command == 0x97cefb3 || command == 0x69433011 ||
                command == 0x69433012) {
                if (g_host) g_host->DialogGridSort(window, static_cast<std::uint32_t>(command));
                return true;
            }
            return false;
        }
        if (static_cast<std::uint32_t>(sender) - 0x41u <= 0x19u) {
            if (g_host) g_host->DialogGridScrollToLetter(window, static_cast<char>(sender));
            return true;
        }
        return false;
    }
    if (message.type == kGZMsgKeyDown) {
        if (static_cast<std::uint32_t>(message.param1) - 0x41u <= 0x19u) {
            if (g_host) g_host->DialogGridScrollToLetter(window, static_cast<char>(message.param1));
            return true;
        }
    }
    return false;
}

int cSC4WinLotConfigurationEditor::RemoveOutOfBoundsLotConfigurationObjects() {   // @ 0x100418d7e
    if (!configuration_) return 0;
    const float width = float(configuration_->sizeX_) * kLotTileSize;
    const float depth = float(configuration_->sizeZ_) * kLotTileSize;
    int removed = 0;
    auto& objects = configuration_->objects_;
    for (auto it = objects.begin(); it != objects.end();) {
        const std::uint8_t type = it->type;
        const bool keep = type > 4 || (0x13u >> type & 1u) == 0 ||
                          (it->minX <= width && (it->maxZ > 0.0f || (it->maxZ == 0.0f && it->minZ <= depth)) &&
                           (it->maxX > 0.0f || it->maxX == 0.0f));
        if (keep) {
            ++it;
            continue;
        }
        it = objects.erase(it);
        ++removed;
    }
    auto& fences = configuration_->fenceSegments_;
    for (std::size_t i = 0; i + 3 < fences.size();) {
        if (int(configuration_->sizeX_) < int(fences[i + 2]) ||
            int(configuration_->sizeZ_) < int(fences[i + 3])) {
            fences.erase(fences.begin() + static_cast<std::ptrdiff_t>(i),
                         fences.begin() + static_cast<std::ptrdiff_t>(i + 4));
            ++removed;
            continue;
        }
        i += 4;
    }
    if (removed != 0) {
        dataChanged_ = true;
        SetTitle();
    }
    return removed;
}

// ---- keyboard ---------------------------------------------------------------------------------------------------

bool cSC4WinLotConfigurationEditor::GZOnKeyUp(std::uint32_t, std::uint32_t) { return true; }   // @ 0x10041b97a

bool cSC4WinLotConfigurationEditor::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {   // @ 0x10041a946
    if (GZAmCapturing()) return true;
    dragging_ = false;
    switch (key) {
        case 'B': DoAddNewBuilding(); break;
        case 'C':
            cycleIndex_ = static_cast<std::uint8_t>((cycleIndex_ + 1) % 7);
            Refresh3DView();
            break;
        case 'R': Refresh3DView(); break;
        case 'F':
            if ((modifiers & 3) == 0) {
                DoAddNewFlora();
                return true;
            }
            if (toolMode_ == 8) return true;
            toolMode_ = 8;
            SetCursor(cursors_[6], true);
            break;
        case 'G': GoTo3DView(); break;
        case 'P': DoAddNewProp((modifiers & 1) == 0, (modifiers & 2) == 0); break;
        case 'S':
            if (modifiers & 2) {
                if ((modifiers & 1) == 0) {
                    if ((modifiers & 4) == 0) {
                        Save(true, true);
                        return true;
                    }
                    SaveAs(true, true, true);
                } else {
                    SaveAs(false, true, true);
                }
            }
            break;
        case 'T': DoAddNewLotBaseTexture(); break;
        case 0x1b:   // Escape
            toolMode_ = 0;
            SetCursor(cursors_[0], true);
            break;
        case 0x70:   // 'p'
            DoShowHelpUI();
            return true;
        default: {
            cSC4LotConfigurationObject* object = selected_;
            if (!object) return true;
            switch (key) {
                case 0x25: MoveObject(object, 0x25, modifiers); return true;
                case 0x26: MoveObject(object, 0x26, modifiers); return true;
                case 0x27: MoveObject(object, 0x27, modifiers); return true;
                case 0x28: MoveObject(object, 0x28, modifiers); return true;
                case 0x21:
                case 0x22: RotateObject(object, key, modifiers); return true;
                case 0x24:   // Home: drop the object onto the ground
                    if (object->y == 0.0f) return true;
                    object->y = 0;
                    dataChanged_ = true;
                    SetTitle();
                    DrawCurrentLotConfiguration();
                    InvalidateSelfAndParents();
                    return true;
                case 0x08:   // Backspace
                case 0x2e:   // Delete
                    DeleteLotConfigurationObject(object, true);
                    return true;
                case 'D':
                    if (modifiers & 2) {
                        DuplicateObject(object, key, modifiers);
                        return true;
                    }
                    return true;
                case 'M': MirrorObject(object, key, modifiers); return true;
                case 'N':
                    // PARTIAL: CycleSelectedObject (0x10041b684) needs the lot / building development / flora
                    // managers; not recovered.
                    return true;
                default: return true;
            }
        }
    }
    return true;
}

// ---- mouse ------------------------------------------------------------------------------------------------------

bool cSC4WinLotConfigurationEditor::GZOnMouseDownL(int x, int y, std::uint32_t modifiers) {   // @ 0x10041b982
    cIGZWin* viewWindow = view_ ? view_->AsIGZWin() : nullptr;
    if (!viewWindow) return cGZWinGen::GZOnMouseDownL(x, y, modifiers);
    int vx = x, vy = y;
    WindowToWindowCoordinates(viewWindow, vx, vy);
    if (vx >= 0 && vy >= 0 && vx < viewWindow->GetW() && vy < viewWindow->GetH()) {
        DoEditMouseDownL(vx, vy, modifiers);
        return true;
    }
    viewWindow->WindowToWindowCoordinates(this, vx, vy);
    cGZWinGen::GZOnMouseDownL(vx, vy, modifiers);
    return true;
}

bool cSC4WinLotConfigurationEditor::DoEditMouseDownL(int x, int y, std::uint32_t modifiers) {   // @ 0x10041ba60
    cSC4LotConfigurationObject* object = nullptr;
    const std::uint32_t hit = DoHitTest(x, y, modifiers, &object);
    switch (hit) {
        case 0:
            break;
        case 1:
            if (toolMode_ != 0) {
                AddNewLotConfigurationObject(x, y, modifiers);
                return true;
            }
            break;
        case 2:
            if (toolMode_ != 0) return true;
            if (g_host) g_host->SetFoundationUI(this, LotFoundationIsPresent(), nullptr);
            SetSelectedLotConfigurationObject(nullptr);
            return true;
        case 3:
            if (toolMode_ == 8) {
                ToggleFence(x, y, modifiers);
                return true;
            }
            if (toolMode_ != 0) return true;
            break;
        default:
            if (hit < 100) return true;
            if (toolMode_ != 0) {
                AddNewLotConfigurationObject(x, y, modifiers);
                return true;
            }
            SetSelectedLotConfigurationObject(object);
            if ((modifiers & 2) == 0 && object->type < 8 && (0x97u >> object->type & 1u) != 0) {
                dragging_ = true;
                if (!GZAmCapturing()) GZSetCapture();
                dragOffsetX_ = static_cast<float>(x) * scale_ - object->x;
                dragOffsetY_ = static_cast<float>(y) * scale_ - object->z;
            }
            DrawCurrentLotConfiguration();
            InvalidateSelfAndParents();
            return true;
    }
    if (selected_) {
        SetSelectedLotConfigurationObject(nullptr);
        DrawCurrentLotConfiguration();
        InvalidateSelfAndParents();
    }
    return true;
}

bool cSC4WinLotConfigurationEditor::GZOnMouseDownR(int x, int y, std::uint32_t modifiers) {   // @ 0x10041bbf8
    cIGZWin* viewWindow = view_ ? view_->AsIGZWin() : nullptr;
    if (!viewWindow) {   // the original dereferences the view child; without one every click is "outside"
        DoOutsideAreaMenu(x, y);
        return true;
    }
    int vx = x, vy = y;
    WindowToWindowCoordinates(viewWindow, vx, vy);
    if (vx >= 0 && vy >= 0 && vx < viewWindow->GetW() && vy < viewWindow->GetH()) {
        DoEditMouseDownR(vx, vy, modifiers);
        return true;
    }
    viewWindow->WindowToWindowCoordinates(this, vx, vy);
    DoOutsideAreaMenu(vx, vy);
    return true;
}

bool cSC4WinLotConfigurationEditor::DoEditMouseDownR(int x, int y, std::uint32_t modifiers) {   // @ 0x10041bcd2
    if (toolMode_ != 0) return true;
    cSC4LotConfigurationObject* object = nullptr;
    const std::uint32_t hit = DoHitTest(x, y, modifiers, &object);
    switch (hit) {
        case 0:
        case 1: DoEditAreaMenu(x, y); break;
        case 2:
            if (g_host) g_host->FoundationMenu(this, x, y);
            break;
        case 3:
            if (g_host) g_host->FenceMenu(this, x, y);
            break;
        default:
            if (hit > 99 && g_host) g_host->LotConfigurationObjectMenu(this, x, y, object);
            break;
    }
    return true;
}

bool cSC4WinLotConfigurationEditor::GZOnMouseUpL(int x, int y, std::uint32_t modifiers) {   // @ 0x10041c3c4
    if (SizeMode() != 0) return cGZWinGen::GZOnMouseUpL(x, y, modifiers);
    cIGZWin* viewWindow = view_ ? view_->AsIGZWin() : nullptr;
    if (!viewWindow) return cGZWinGen::GZOnMouseUpL(x, y, modifiers);
    int vx = x, vy = y;
    WindowToWindowCoordinates(viewWindow, vx, vy);
    if (vx < 0 || vy < 0 || vx >= viewWindow->GetW() || vy >= viewWindow->GetH()) {
        viewWindow->WindowToWindowCoordinates(this, vx, vy);
        cGZWinGen::GZOnMouseUpL(vx, vy, modifiers);
        return true;
    }
    if (!dragging_) return true;
    dragging_ = false;
    if (GZAmCapturing()) GZReleaseCapture();
    return true;
}

bool cSC4WinLotConfigurationEditor::GZOnMouseMove(int x, int y, std::uint32_t modifiers) {   // @ 0x10041c4e8
    if (SizeMode() != 0) return cGZWinGen::GZOnMouseMove(x, y, modifiers);
    cIGZWin* viewWindow = view_ ? view_->AsIGZWin() : nullptr;
    if (!viewWindow) return cGZWinGen::GZOnMouseMove(x, y, modifiers);
    int vx = x, vy = y;
    WindowToWindowCoordinates(viewWindow, vx, vy);
    if (vx >= 0 && vy >= 0 && vx < viewWindow->GetW() && vy < viewWindow->GetH()) {
        DoEditMouseMove(vx, vy, modifiers);
        return true;
    }
    viewWindow->WindowToWindowCoordinates(this, vx, vy);
    cGZWinGen::GZOnMouseMove(vx, vy, modifiers);
    return true;
}

bool cSC4WinLotConfigurationEditor::DoEditMouseMove(int x, int y, std::uint32_t) {   // @ 0x10041c5ec
    if (dragging_ && GZAmCapturing() && selected_) {
        MoveObject(selected_, static_cast<float>(x) * scale_ - dragOffsetX_,
                   static_cast<float>(y) * scale_ - dragOffsetY_);
    }
    cRZString status;
    status.Sprintf("%08x: Pixel: (%d,%d) World: (%.1f,%.1f)", selected_ ? selected_->id : 0u, x, y,
                   static_cast<double>(static_cast<float>(x) * scale_),
                   static_cast<double>(static_cast<float>(y) * scale_));
    if (statusText_) statusText_->SetCaption(status);
    return true;
}

// ---- hit testing ------------------------------------------------------------------------------------------------

std::uint32_t cSC4WinLotConfigurationEditor::DoHitTest(int x, int y, std::uint32_t modifiers,
                                                       cSC4LotConfigurationObject** object) {   // @ 0x10041c728
    *object = nullptr;
    if ((modifiers & 2) == 0 && selected_) {
        const float scale = scale_;
        if (y <= static_cast<int>(selected_->maxZ / scale) && x <= static_cast<int>(selected_->maxX / scale) &&
            static_cast<int>(selected_->minX / scale) <= x && static_cast<int>(selected_->minZ / scale) <= y) {
            *object = selected_;
            return static_cast<std::uint32_t>(selectedIndex_ + 100);
        }
    }
    cSC4LotConfiguration* configuration = configuration_;
    if (configuration && !configuration->objects_.empty()) {
        const float scale = scale_;
        std::vector<std::uint32_t> hits;
        for (std::uint32_t i = 0; i < configuration->objects_.size(); ++i) {
            const cSC4LotConfigurationObject& candidate = configuration->objects_[i];
            if (y <= static_cast<int>(candidate.maxZ / scale) &&
                x <= static_cast<int>(candidate.maxX / scale) &&
                static_cast<int>(candidate.minX / scale) <= x && static_cast<int>(candidate.minZ / scale) <= y)
                hits.push_back(i);
        }
        if (!hits.empty()) {
            std::uint32_t index = hits[0];
            if (hits.size() >= 2 && (modifiers & 2) != 0) {
                std::vector<cRZString> labels;
                for (std::uint32_t hit : hits) {
                    const cSC4LotConfigurationObject& candidate = configuration->objects_[hit];
                    cRZString label;
                    label.Sprintf("%d: %s", hit, ObjectTypeName(candidate.type));
                    labels.push_back(label);
                }
                const int chosen = g_host ? g_host->PopupStringListMenu(this, x, y, labels) : -1;
                if (chosen < 0 || static_cast<std::size_t>(chosen) >= hits.size()) return 0;
                // (The original also asks the window manager at vslot 0x128 to re-activate the editor here.)
                index = hits[static_cast<std::size_t>(chosen)];
            }
            *object = &configuration->objects_[index];
            return index + 100;
        }
    }
    if (!configuration_) return 0;
    const float scale = scale_;
    const int width = static_cast<int>((float(configuration->sizeX_) * kLotTileSize) / scale);
    const int height = static_cast<int>((float(configuration->sizeZ_) * kLotTileSize) / scale);
    // Fence lines: tile boundaries within three pixels.
    for (std::uint32_t i = 0; i < configuration->sizeX_; ++i) {
        const int line = static_cast<int>((float(i) * kLotTileSize) / scale);
        if (line - 3 <= x && x <= line + 3 && y <= height) return 3;
    }
    for (std::uint32_t i = 0; i < configuration->sizeZ_; ++i) {
        const int line = static_cast<int>((float(i) * kLotTileSize) / scale);
        if (line - 3 <= y && y <= line + 3 && x <= width) return 3;
    }
    std::uint32_t hit = 2;
    if (height + 6 < y || width + 6 < x || x < width || y < 0) {
        if (x < 0) {
            hit = 0;
        } else if (height + 6 < y || width + 6 < x || y < height) {
            hit = (y <= height && x <= width && 0 <= y) ? 1 : 0;
        }
    }
    return hit;
}

// ---- selection / object editing ---------------------------------------------------------------------------------

void cSC4WinLotConfigurationEditor::SetSelectedLotConfigurationObject(cSC4LotConfigurationObject* object) {   // @ 0x10041ccbe
    if (selected_ == object) return;
    if (dragging_) {
        dragging_ = false;
        if (GZAmCapturing()) GZReleaseCapture();
    }
    selected_ = object;
    if (object && configuration_) {
        selectedIndex_ = -1;
        const auto& objects = configuration_->objects_;
        for (std::size_t i = 0; i < objects.size(); ++i) {
            if (&objects[i] == object) {
                selectedIndex_ = static_cast<int>(i);
                return;
            }
        }
    }
    selectedIndex_ = 0;
}

void cSC4WinLotConfigurationEditor::MoveObject(cSC4LotConfigurationObject* object, std::uint32_t key,
                                               std::uint32_t modifiers) {   // @ 0x10041af7e
    float step = MoveStep(modifiers);
    switch (key) {
        case 0x25:   // left
            object->x -= step;
            object->minX -= step;
            object->maxX -= step;
            break;
        case 0x26:   // up
            if (g_host && g_host->IsKeyDown(0x20)) {   // Shift: change the elevation of a prop
                if (object->type != 1) break;
                object->y += step;
                break;
            }
            object->z -= step;
            object->minZ -= step;
            object->maxZ -= step;
            break;
        case 0x27:   // right
            object->x += step;
            object->minX += step;
            object->maxX += step;
            break;
        case 0x28:   // down
            if (g_host && g_host->IsKeyDown(0x20)) {
                if (object->type != 1) break;
                object->y -= step;
                break;
            }
            object->z += step;
            object->minZ += step;
            object->maxZ += step;
            break;
        default: break;
    }
    MoveObjectToWithinLotBounds(object, object->type != 4);
    dataChanged_ = true;
    SetTitle();
    DrawCurrentLotConfiguration();
    InvalidateSelfAndParents();
}

void cSC4WinLotConfigurationEditor::MoveObject(cSC4LotConfigurationObject* object, float x, float z) {   // @ 0x1004243d8
    const float dx = x - object->x;
    const float dz = z - object->z;
    object->x += dx;
    object->z += dz;
    object->minX += dx;
    object->minZ += dz;
    object->maxX += dx;
    object->maxZ += dz;
    MoveObjectToWithinLotBounds(object, object->type != 4);
    dataChanged_ = true;
    SetTitle();
    DrawCurrentLotConfiguration();
    InvalidateSelfAndParents();
}

void cSC4WinLotConfigurationEditor::RotateObject(cSC4LotConfigurationObject* object, std::uint32_t,
                                                 std::uint32_t) {   // @ 0x10041b130
    object->orientation = static_cast<std::uint8_t>((object->orientation + 1) & 3);
    // Quirk kept: the extents swap axes on every turn (the half depth is applied to x and the half width to z).
    const float halfDepth = (object->maxZ - object->minZ) * 0.5f;
    const float halfWidth = (object->maxX - object->minX) * 0.5f;
    object->minX = object->x - halfDepth;
    object->maxX = object->x + halfDepth;
    object->minZ = object->z - halfWidth;
    object->maxZ = object->z + halfWidth;
    dataChanged_ = true;
    SetTitle();
    DrawCurrentLotConfiguration();
    InvalidateSelfAndParents();
}

void cSC4WinLotConfigurationEditor::MirrorObject(cSC4LotConfigurationObject* object, std::uint32_t,
                                                 std::uint32_t) {   // @ 0x10041b1c2
    if (object->type != 2) return;
    object->mirrored = object->mirrored == 0 ? 1 : 0;
    dataChanged_ = true;
    SetTitle();
    DrawCurrentLotConfiguration();
    InvalidateSelfAndParents();
}

void cSC4WinLotConfigurationEditor::DuplicateObject(cSC4LotConfigurationObject* object, std::uint32_t,
                                                    std::uint32_t) {   // @ 0x10041b208
    if (object->type > 4 || object->type == 3 || !configuration_) return;
    cSC4LotConfigurationObject copy = *object;
    copy.id = g_host ? g_host->NewObjectId() : 0;
    copy.x += 4.0f;
    copy.z += 4.0f;
    copy.maxX += 4.0f;
    copy.maxZ += 4.0f;
    copy.minX += 4.0f;
    copy.minZ += 4.0f;
    const float width = float(configuration_->sizeX_) * kLotTileSize;
    const float depth = float(configuration_->sizeZ_) * kLotTileSize;
    if (width < copy.minX || copy.maxZ < 0.0f || copy.maxX < 0.0f || depth < copy.minZ) {
        copy.x -= 8.0f;
        copy.z -= 8.0f;
        copy.maxX -= 8.0f;
        copy.maxZ -= 8.0f;
        copy.minX -= 8.0f;
        copy.minZ -= 8.0f;
    }
    if (width < copy.minX || copy.maxZ < 0.0f || copy.maxX < 0.0f || depth < copy.minZ) {
        copy.x += 4.0f;
        copy.maxX += 4.0f;
        copy.minX += 4.0f;
    }
    if (width < copy.minX || copy.maxZ < 0.0f || copy.maxX < 0.0f || depth < copy.minZ) {
        copy.x -= 8.0f;
        copy.maxX -= 8.0f;
        copy.minX -= 8.0f;
        copy.maxZ += 8.0f;
        copy.minZ += 8.0f;
    }
    configuration_->objects_.insert(configuration_->objects_.begin(), copy);
    SetSelectedLotConfigurationObject(&configuration_->objects_.front());
    dataChanged_ = true;
    SetTitle();
    DrawCurrentLotConfiguration();
    InvalidateSelfAndParents();
}

void cSC4WinLotConfigurationEditor::DeleteLotConfigurationObject(cSC4LotConfigurationObject* object,
                                                                 bool ask) {   // @ 0x10041b462
    if (ask) {
        cRZString name;
        GetLotConfigurationObjectName(object, name, false);
        cRZString text;
        text.Sprintf("Are you sure you want to delete this %s?", name.ToChar());
        if (MessageBoxA(text, kMsgBoxStyleYesNo) == kMsgBoxNo) return;
    }
    if (!configuration_) return;
    if (object->type == 0) lastObjectType_ = 0;
    auto& objects = configuration_->objects_;
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        if (&*it == object) {
            objects.erase(it);
            break;
        }
    }
    if (selected_ == object) selected_ = nullptr;
    // Quirk kept: the original neither marks the configuration changed nor updates the caption here.
    DrawCurrentLotConfiguration();
    InvalidateSelfAndParents();
}

bool cSC4WinLotConfigurationEditor::IsObjectWithinLotBounds(const cSC4LotConfigurationObject* object,
                                                            bool asymmetric) const {   // @ 0x10042e7c4
    if (!configuration_) return true;
    const float width = float(configuration_->sizeX_) * kLotTileSize;
    const float depth = float(configuration_->sizeZ_) * kLotTileSize;
    if (asymmetric) {
        if (object->minX < 0.0f || object->minZ < 0.0f || width < object->maxX) return false;
        return object->maxZ <= depth;
    }
    if (object->maxX < 0.0f || object->maxZ < 0.0f || width < object->minX) return false;
    return object->minZ <= depth;
}

void cSC4WinLotConfigurationEditor::MoveObjectToWithinLotBounds(cSC4LotConfigurationObject* object,
                                                                bool asymmetric) {   // @ 0x10042e1fc
    if (!configuration_) return;
    const float width = float(configuration_->sizeX_) * kLotTileSize;
    const float depth = float(configuration_->sizeZ_) * kLotTileSize;
    if (asymmetric) {
        if (object->minX < kLotBoundsMargin) {
            const float delta = kLotBoundsMargin - object->minX;
            object->x += delta;
            object->minX += delta;
            object->maxX += delta;
        }
        if (object->minZ < kLotBoundsMargin) {
            const float delta = kLotBoundsMargin - object->minZ;
            object->z += delta;
            object->minZ += delta;
            object->maxZ += delta;
        }
        const float limitX = width - kLotBoundsMargin;
        if (limitX < object->maxX) {
            const float delta = object->maxX - limitX;
            object->x -= delta;
            object->minX -= delta;
            object->maxX -= delta;
        }
        const float limitZ = depth - kLotBoundsMargin;
        if (limitZ < object->maxZ) {
            const float delta = object->maxZ - limitZ;
            object->z -= delta;
            object->minZ -= delta;
            object->maxZ -= delta;
        }
    } else {
        if (object->x < kLotBoundsMargin) {
            const float delta = kLotBoundsMargin - object->x;
            object->x += delta;
            object->minX += delta;
            object->maxX += delta;
        }
        if (object->z < kLotBoundsMargin) {
            const float delta = kLotBoundsMargin - object->z;
            object->z += delta;
            object->minZ += delta;
            object->maxZ += delta;
        }
        const float limitX = width - kLotBoundsMargin;
        if (limitX < object->x) {
            const float delta = object->x - limitX;
            object->x -= delta;
            object->minX -= delta;
            object->maxX -= delta;
        }
        const float limitZ = depth - kLotBoundsMargin;
        if (limitZ < object->z) {
            const float delta = object->z - limitZ;
            object->z -= delta;
            object->minZ -= delta;
            object->maxZ -= delta;
        }
    }
    // Quirk kept: a negative zero becomes a positive zero.
    if (object->x <= 0.0f && object->x != 0.0f) object->x = 0.0f;
}

std::uint32_t cSC4WinLotConfigurationEditor::LotFoundationIsPresent() const {   // @ 0x10041d3bc
    if (!configuration_) return 0;
    for (const cSC4LotConfigurationObject& object : configuration_->objects_)
        if (object.type == 3) return object.value;
    return 0;
}

void cSC4WinLotConfigurationEditor::AddLotFoundation(std::uint32_t id, bool add) {   // @ 0x10042b9d4
    if (!configuration_) return;
    auto& objects = configuration_->objects_;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (objects[i].type != 3) continue;
        if (add && objects[i].value == id) return;
        objects.erase(objects.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
    if (add) {
        cSC4LotConfigurationObject foundation;
        foundation.type = 3;
        foundation.value = id;
        objects.push_back(foundation);
    }
}

void cSC4WinLotConfigurationEditor::ToggleFence(const SC4FenceSegment& segment) {   // @ 0x10042e12e
    if (!configuration_) return;
    auto& segments = configuration_->fenceSegments_;
    const auto* bytes = reinterpret_cast<const std::int8_t*>(&segment);
    const std::size_t count = segments.size() / 4;
    for (std::size_t i = 0; i < count; ++i) {
        if (segments[i * 4 + 0] == bytes[0] && segments[i * 4 + 1] == bytes[1] &&
            segments[i * 4 + 2] == bytes[2] && segments[i * 4 + 3] == bytes[3]) {
            segments.erase(segments.begin() + static_cast<std::ptrdiff_t>(i * 4),
                           segments.begin() + static_cast<std::ptrdiff_t>(i * 4 + 4));
            DrawCurrentLotConfiguration();
            InvalidateSelfAndParents();
            return;
        }
    }
    segments.insert(segments.end(), bytes, bytes + 4);
    DrawCurrentLotConfiguration();
    InvalidateSelfAndParents();
}

void cSC4WinLotConfigurationEditor::ToggleFence(int x, int z, std::uint32_t) {   // @ 0x10041d826
    if (!configuration_) return;
    const float scale = scale_;
    const float toTile = scale * 0.0625f;   // DAT_100a158b0 = 0.0625
    auto findLine = [&](int coordinate, std::uint8_t extent, std::uint32_t& index) {
        int position = 0;
        index = 0;
        while (coordinate < static_cast<int>(float(position) / scale) - 3 ||
               static_cast<int>(float(position) / scale) + 3 < coordinate) {
            position += 0x10;
            const bool past = extent <= index;
            ++index;
            if (past) return false;
        }
        return true;
    };
    std::uint32_t line = 0;
    if (findLine(x, configuration_->sizeX_, line)) {
        SC4FenceSegment segment;
        segment.x0 = static_cast<std::int8_t>(line);
        segment.z0 = static_cast<std::int8_t>(static_cast<int>(scale * static_cast<float>(z) * 0.0625f));
        segment.x1 = segment.x0;
        segment.z1 = static_cast<std::int8_t>(segment.z0 + 1);
        (void)toTile;
        ToggleFence(segment);
        dataChanged_ = true;
        SetTitle();
        return;
    }
    if (findLine(z, configuration_->sizeZ_, line)) {
        SC4FenceSegment segment;
        segment.x0 = static_cast<std::int8_t>(static_cast<int>(scale * static_cast<float>(x) * 0.0625f));
        segment.z0 = static_cast<std::int8_t>(line);
        segment.x1 = static_cast<std::int8_t>(segment.x0 + 1);
        segment.z1 = segment.z0;
        ToggleFence(segment);
        dataChanged_ = true;
        SetTitle();
    }
}

// ---- adding objects ---------------------------------------------------------------------------------------------

void cSC4WinLotConfigurationEditor::AddNewLotConfigurationObject(int x, int y, std::uint32_t) {   // @ 0x10041cd80
    if (!configuration_) return;
    const std::uint32_t mode = toolMode_;
    if (mode < 1 || mode > 7) return;
    const float worldX = static_cast<float>(x) * scale_;
    const float worldZ = static_cast<float>(y) * scale_;
    cSC4LotConfigurationObject object;
    bool fromManager = false;
    switch (mode) {
        case 1: object.type = 0; object.extra = familyIds_; fromManager = true; break;
        case 2: object.type = 4; object.extra = familyIds_; fromManager = true; break;
        case 3: object.type = 1; object.extra = familyIds_; fromManager = true; break;
        case 4:
            object.type = 7;
            object.orientation = 2;
            object.extra = {0, 0, 0x2000000, 0};   // the original writes four words after resizing to three
            break;
        case 5: object.type = 2; object.extra = familyIds_; break;
        case 6: object.type = 5; object.extra = familyIds_; break;
        default: object.type = 6; object.extra = familyIds_; break;
    }
    if ((mode >= 1 && mode <= 3) || mode == 5) {
        if (familyIds_.empty()) return;   // nothing selected in the family dialog
    }
    object.x = worldX;
    object.z = worldZ;
    object.y = 0;
    object.value = 0;
    object.id = g_host ? g_host->NewObjectId() : 0;
    float footprint[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    if (fromManager && lotConfigManager_) {
        if (mode == 1) lotConfigManager_->GetMaxFootprintForBuildingFamilies(object.extra, footprint);
        else if (mode == 2) lotConfigManager_->GetMaxFootprintForFloraFamilies(object.extra, footprint);
        else lotConfigManager_->GetMaxFootprintForPropFamilies(object.extra, footprint);
        const float halfX = (footprint[2] - footprint[0]) * 0.5f;
        const float halfZ = (footprint[3] - footprint[1]) * 0.5f;
        object.minX = object.x - halfX;
        object.maxX = object.x + halfX;
        object.minZ = object.z - halfZ;
        object.maxZ = object.z + halfZ;
    } else {
        object.minX = object.x - kLotObjectHalfExtent;
        object.maxX = object.x + kLotObjectHalfExtent;
        object.minZ = object.z - kLotObjectHalfExtent;
        object.maxZ = object.z + kLotObjectHalfExtent;
    }
    MoveObjectToWithinLotBounds(&object, object.type != 4);
    if (object.type == 2)
        configuration_->objects_.push_back(object);
    else
        configuration_->objects_.insert(configuration_->objects_.begin(), object);
    SetSelectedLotConfigurationObject(&configuration_->objects_.front());
    dataChanged_ = true;
    SetTitle();
    DrawCurrentLotConfiguration();
    InvalidateSelfAndParents();
}

void cSC4WinLotConfigurationEditor::DoAddNewBuilding() {   // @ 0x10041ac52
    if (toolMode_ == 1) return;
    std::vector<std::uint32_t> before = familyIds_;
    if (g_host && g_host->SelectFamilies(1, familyIds_) && familyIds_ != before) {
        toolMode_ = 1;
        SetCursor(cursors_[1], true);
    }
}

void cSC4WinLotConfigurationEditor::DoAddNewLotBaseTexture() {   // @ 0x10041acca
    if (toolMode_ == 5) return;
    std::vector<std::uint32_t> before = familyIds_;
    if (g_host && g_host->SelectFamilies(5, familyIds_) && familyIds_ != before) {
        toolMode_ = 5;
        SetCursor(cursors_[5], true);
    }
}

void cSC4WinLotConfigurationEditor::DoAddNewProp(bool, bool) {   // @ 0x10041ad42
    if (toolMode_ == 3) return;
    std::vector<std::uint32_t> before = familyIds_;
    if (g_host && g_host->SelectFamilies(3, familyIds_) && familyIds_ != before) {
        toolMode_ = 3;
        SetCursor(cursors_[4], true);
    }
}

void cSC4WinLotConfigurationEditor::DoAddNewFlora() {   // @ 0x10041adc6
    if (toolMode_ == 2) return;
    std::vector<std::uint32_t> before = familyIds_;
    if (g_host && g_host->SelectFamilies(2, familyIds_) && familyIds_ != before) {
        toolMode_ = 2;
        SetCursor(cursors_[2], true);
    }
}

void cSC4WinLotConfigurationEditor::DoShowHelpUI() {   // @ 0x10041ae3e
    if (!g_host) return;
    g_host->MessageBox(cRZString("The answer to all your questions is:\nAsk Paul Pedriana"),
                       cRZString("Help System"), kMsgBoxStyleOk);
}

void cSC4WinLotConfigurationEditor::DoEditAreaMenu(int x, int y) {   // @ 0x100424af8
    if (g_host) g_host->EditAreaMenu(this, x, y);
}

void cSC4WinLotConfigurationEditor::DoOutsideAreaMenu(int x, int y) {   // @ 0x10041bd84
    if (g_host) g_host->OutsideAreaMenu(this, x, y);
}

void cSC4WinLotConfigurationEditor::DrawCurrentLotConfiguration() {
    // PARTIAL: 0x10042cc4e walks the objects and calls DrawLotConfigurationObject (0x10042d1c0) to rasterise the
    // preview into the view bitmap through cIGZDrawContext. The drawing itself is not recovered here.
}

// ---- object names -----------------------------------------------------------------------------------------------

void cSC4WinLotConfigurationEditor::GetLotConfigurationObjectName(const cSC4LotConfigurationObject* object,
                                                                  cRZString& out, bool brief) {   // @ 0x10042dd7e
    std::string name = ObjectTypeName(object ? object->type : 0);
    if (brief && !name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    out.AssignFromCString(name.c_str());
}

std::uint32_t cSC4WinLotConfigurationEditor::GetFoundationNameAndImplementationType(
    std::uint32_t id, cRZString& out) {   // @ 0x10042bc2e
    if (!g_host) return 0;
    if (!g_host->IsFoundationType(id) || !g_host->ExemplarName(0xa52160f5, id, out)) {
        out.AssignFromCString("<unknown>");   // the literal the original appends is dropped by Ghidra
        return 0;
    }
    if (!g_host->ExemplarHasProperty(0xa52160f5, id, 0x68fcff37)) return 1;
    return 3;
}

void cSC4WinLotConfigurationEditor::GetRetainingWallName(std::uint32_t id, cRZString& out) {   // @ 0x10042bdc6
    out.AssignFromCString("unknown");
    if (!g_host) return;
    cGZPersistResourceKey key;
    if (!g_host->RetainingWallExemplarKey(id, key)) return;
    g_host->ExemplarName(key.type, key.instance, out);
}

void cSC4WinLotConfigurationEditor::GetBuildingName(std::uint32_t id, cRZString& out) {   // @ 0x10042c0b8
    if (!g_host || !g_host->IsBuildingType(id)) {
        out.AssignFromCString("<Building>");
        return;
    }
    g_host->BuildingName(id, out);
}

void cSC4WinLotConfigurationEditor::GetBuildingFamilyName(std::uint32_t family,
                                                          cRZString& out) {   // @ 0x10042bee6
    auto it = g_buildingFamilyNames.find(family);
    if (it != g_buildingFamilyNames.end()) {
        out = it->second;
        return;
    }
    std::vector<std::uint32_t> members;
    const bool found = g_host && g_host->BuildingsInFamily(family, members);
    if (!found || members.empty()) {
        out.AssignFromCString("<Family>");
        return;
    }
    std::string text;
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (i == 8) {
            text += ", ...";
            break;
        }
        cRZString name;
        GetBuildingName(members[i], name);
        if (i != 0) text += ", ";
        text += name.str();
    }
    out.AssignFromCString(text.c_str());
}

void cSC4WinLotConfigurationEditor::GetFloraFamilyName(std::uint32_t family, cRZString& out) {   // @ 0x10042c17a
    auto it = g_floraFamilyNames.find(family);
    if (it != g_floraFamilyNames.end()) {
        out = it->second;
        return;
    }
    out.AssignFromCString("<Flora>");
}

void cSC4WinLotConfigurationEditor::GetPropName(std::uint32_t id, cRZString& out) {   // @ 0x10042c40a
    if (!g_host || !g_host->IsPropType(id)) {
        out.AssignFromCString("<Prop>");
        return;
    }
    g_host->PropName(id, out);
}

void cSC4WinLotConfigurationEditor::GetPropFamilyName(std::uint32_t family, cRZString& out) {   // @ 0x10042c230
    auto it = g_propFamilyNames.find(family);
    if (it != g_propFamilyNames.end()) {
        out = it->second;
        return;
    }
    std::vector<std::uint32_t> members;
    const bool found = g_host && g_host->PropsInFamily(family, members);
    if (!found || members.empty()) {
        out.AssignFromCString("<Family>");
        return;
    }
    std::string text;
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (i == 8) {
            text += ", ...";
            break;
        }
        cRZString name;
        GetPropName(members[i], name);
        if (i != 0) text += ", ";
        text += name.str();
    }
    out.AssignFromCString(text.c_str());
}

int cSC4WinLotConfigurationEditor::LotTextureSort(const cRZString& a, const cRZString& b) {   // @ 0x10042cbe4
    const std::uint32_t va = static_cast<std::uint32_t>(std::strtoul(a.ToChar(), nullptr, 16));
    const std::uint32_t vb = static_cast<std::uint32_t>(std::strtoul(b.ToChar(), nullptr, 16));
    const int byNibble = static_cast<int>(va & 0xf000) - static_cast<int>(vb & 0xf000);
    if (byNibble != 0) return byNibble;
    return vb < va ? 1 : -1;
}

}  // namespace sc4
