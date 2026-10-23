// cSC4WinLotConfigurationEditor — the lot editor window (107 symbols in the Ghidra export, 0x1003d6634..0x10042eeac): a cGZWinGen frame
// that edits one cSC4LotConfiguration (a "lot template": size, foundation, fences and the objects — building,
// props, base texture, flora, foundation — placed on its tiles) in a 3D preview.
//
// The window owns a 3D view bitmap (child 0x692352b5, cIGZWinBMP), a flat-rect frame around it (0x6946da25) and a
// status text (0x29235446, cIGZWinText). The mouse is routed to the view bitmap: the editor converts the click into
// the view's coordinates, and when it is inside, DoHitTest classifies it (overlap of an object / lot edge / fence
// line / empty lot) and the left button then selects/drags/creates, the right button opens a context menu.
// Keys (GZOnKeyDown): B new building, F flora (with a modifier: fence tool), P prop, T base texture, G go to the 3D
// view, C / R cycle-refresh the 3D view, S save (Cmd+Shift+S save as), p help, Escape cancel the tool, the arrows move
// the selected object (Shift moves a prop up / down, Control and Alt take bigger steps), Del / Backspace delete,
// Ctrl+D duplicate, M mirror, Home drop to the ground and N cycle the selected object (not recovered).
//
// Interfaces (vtable groups): +0 the cGZWin/cGZWinGen/cIGZWinGen virtuals followed by this class's own editor
// virtuals (DoMessage(cIGZWin*,...), SaveAs, Close, WriteLotData, CreateNewLotConfiguration,
// ReadLotConfigurationExemplar, GetDefaultPath, DoEditAreaMenu, Refresh3DView — vslots 0x5b0..0x5f0), +0x150 the
// cIGZWinGen view (iid 0x5386d516), +0x1c8 the cIGZWinMessageFilter view (iid 0xc6ae7085).
//
// Layout: +0x1d0 data-has-changed flag, +0x1d8 status font, +0x1e0 unused COM pointer (released by the destructor
// and Shutdown but never assigned here), +0x1e8 cIGZWinBMP view, +0x1f0 cIGZWinText status label, +0x1f8/+0x200/
// +0x208/+0x210/+0x218/+0x220/+0x228 the seven cursors, +0x230 chooser window, +0x238 lot configuration manager,
// +0x240 cSC4LotConfigurationIdentityInfo, +0x288 the configuration being edited, +0x290/+0x298 network manager
// objects, +0x2a0 current tool (0 none, 1 building, 2 flora, 3 prop, 4 foundation, 5 base texture, 6/7 other,
// 8 fence), +0x2a8 selected family ids, +0x2c0 pixels-to-world scale, +0x2c8 selected object, +0x2d0 its index,
// +0x2d4 dragging, +0x2d8/+0x2dc drag offset, +0x2e0 unfreed vector (never filled here), +0x2f8 vector of
// cIGZUnknown* released on shutdown, +0x310 cycle counter, +0x314 3D view slope, +0x318 last building / object type.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "gz/WinBMP.h"
#include "gz/cIGZPersistDBSegment.h"
#include "gz/WinGen.h"
#include "gz/WinText.h"
#include "rz/cRZString.h"
#include "sc4/LotConfiguration.h"

namespace sc4 {

constexpr std::uint32_t kLotEditorWindowId = 0x0920b687;         // the id the window manager is given in Init
constexpr std::uint32_t kLotEditorViewChildId = 0x692352b5;      // cIGZWinBMP child (3D view)
constexpr std::uint32_t kLotEditorFrameChildId = 0x6946da25;     // flat-rect frame around the view
constexpr std::uint32_t kLotEditorStatusChildId = 0x29235446;    // cIGZWinText status label
constexpr std::uint32_t kLotEditorCursorGroup = 0x46a006b0;
constexpr std::uint32_t kLotEditorCursorFirst = 0x4a5cdc3a;      // seven cursors, consecutive instances
constexpr std::uint32_t kLotEditorFontHeight = 0x0c;
constexpr std::uint32_t kLotEditorScreenCommand = 0x920b687;     // posted after the window is added
constexpr std::uint32_t kLotEditorInitMessage = 0x920b687;       // DoMessage: run PostInit

// Message box buttons (window manager GZMsgBox results, same ids as gz::cGZWinMsgBox).
constexpr std::uint32_t kMsgBoxAbort = 0x53018146, kMsgBoxCancel = 0x53018147, kMsgBoxIgnore = 0x53018148,
                        kMsgBoxNo = 0x53018149, kMsgBoxOK = 0x5301814a, kMsgBoxRetry = 0x5301814b,
                        kMsgBoxYes = 0x5301814c;

// Message box styles passed to MessageBoxA (GZMsgBox style word).
constexpr std::uint32_t kMsgBoxStyleOk = 0, kMsgBoxStyleOkCancel = 1, kMsgBoxStyleYesNo = 3,
                        kMsgBoxStyleYesNoCancel = 4, kMsgBoxStyleWarning = 0x10000;

// Tile grid helpers: the world is 16 units per lot tile.
constexpr float kLotTileSize = 16.0f;
constexpr float kLotObjectHalfExtent = 8.0f;   // new objects are 16x16 until the manager reports a footprint
constexpr float kLotBoundsMargin = 0.05f;

// The identity of the configuration being edited: the exemplar key plus its display name and the references the
// lookup kept. Layout 0x48 bytes (the editor embeds it at +0x240).
struct cSC4LotConfigurationIdentityInfo {
    std::uint32_t id = 0;                   // +0x00 instance id (duplicated from the key, as the original does)
    cGZPersistResourceKey key;              // +0x04 type / group / instance
    cRZString name;                         // +0x10 exemplar name (property 0x20)
    cIGZUnknown* exemplar = nullptr;        // +0x38 the resource object (one reference)
    cIGZUnknown* propertyHolder = nullptr;  // +0x40 quirk: the original AddRefs and then Releases, so no reference is kept
};

// A 4-byte fence segment: from tile (x0, z0) to (x1, z1), stored in cSC4LotConfiguration::fenceSegments_.
struct SC4FenceSegment {
    std::int8_t x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    friend bool operator==(const SC4FenceSegment&, const SC4FenceSegment&) = default;
};
static_assert(sizeof(SC4FenceSegment) == 4);

class cSC4LotConfigurationManager;

// What the editor needs from the systems whose classes are not recovered. One method per original call; the slot in
// the comment is the original vtable offset.
class IWinLotConfigurationEditorHost {
public:
    virtual ~IWinLotConfigurationEditorHost() = default;
    // cSLCursor(group, instance) @ 0x1000441e0: one reference, or null.
    virtual cIGZCursor* CreateCursor(std::uint32_t group, std::uint32_t instance) = 0;
    // RZ::FO() vslot 0x48 with the cRZString "Arial\n" and height 0xc: the status font.
    virtual cIGZFont* CreateStatusFont() = 0;
    // RZ::GS() vslot 0x20 then buffer vslot 0x28 (width, height, 0x1000000003): the 3D view bitmap. The editor keeps
    // no reference of its own (it hands it to cIGZWinBMP::SetBMP).
    virtual cIGZBuffer* CreateViewBuffer(int width, int height) = 0;
    // Window manager vslot 0x160: GZMsgBox(text, title, style, 1, 0) -> the button id.
    virtual std::uint32_t MessageBox(const cIGZString& text, const cIGZString& title, std::uint32_t style) = 0;
    // Window manager vslot 0x78: (window, 0x920b687, 0, 0, 0) after Init. The manager posts 0x920b687 back.
    virtual void RegisterWindow(cIGZWin* window) = 0;
    // Resource manager vslot 0xf8 + QI(0x856c8cf7) vslot 0x90 (key, &name): identity info for an exemplar key.
    virtual bool GetIdentityInfoForExemplarKey(const cGZPersistResourceKey& key, cSC4LotConfigurationIdentityInfo& out) = 0;
    // SL::DataDirectory() @ 0x100040506, and cISC4App::GetDataDirectory(out, -1) (vslot 0x198) for StaticInit.
    virtual std::string DataDirectory() = 0;
    // SL::spLotConfigurationManager: the city's lot configuration manager (borrowed; the editor takes its own
    // reference with AddRef).
    virtual cSC4LotConfigurationManager* LotConfigurationManager() = 0;
    // SL::NetworkManager() @ 0x100041188 vslot 0x28 (kind, 1): the two network objects the editor keeps.
    virtual cIGZUnknown* CreateNetworkObject(int kind) = 0;
    // cSC4LotConfigurationManager::WriteLotConfiguration @ 0x1006c4146 (key, configuration, segment).
    virtual bool WriteLotConfiguration(const cGZPersistResourceKey& key, cSC4LotConfiguration* configuration,
                                       cIGZPersistDBSegment* segment) = 0;
    // cSC4LotConfigurationManager::ReadLotConfiguration @ 0x1006c4076.
    virtual bool ReadLotConfiguration(const cGZPersistResourceKey& key, cSC4LotConfiguration*& out) = 0;
    // city vslot 0x128 -> vslot 0x30 with the key, after a successful save.
    virtual void LotConfigurationWritten(const cGZPersistResourceKey& key) = 0;
    // The chooser window's cSC4WinLotConfigurationChooser::DoLotConfigurationEditorClosed @ 0x10041780a.
    virtual void ChooserClosed(cSC4WinLotConfigurationEditor* editor) = 0;
    // SC4DoPopupStringListMenu @ 0x100039cb4 (owner, x, y, items, 0, 0): -1 when cancelled.
    virtual int PopupStringListMenu(cIGZWin* owner, int x, int y, const std::vector<cRZString>& items) = 0;
    // The dialog-box grid paths the editor's message filter forwards to (the family / texture chooser dialogs):
    // DoDialogBoxGridSort @ 0x10041a68a (command -> child 0x97cefb1 / 0x69433083 / 0x6987b3f9, iid 0xdaa6b9be)
    // and DoDialogBoxScrollToLetter @ 0x10041a814. The grid control itself (cIGZWinGrid) is not recovered.
    virtual void DialogGridSort(cIGZWin* dialog, std::uint32_t command) = 0;
    virtual void DialogGridScrollToLetter(cIGZWin* dialog, char letter) = 0;
    // The modal family / texture selectors opened by the tool keys; returns the selections in `inOut` and true when
    // the dialog was accepted. kind: 1 building, 2 flora, 3 prop, 5 lot base texture.
    virtual bool SelectFamilies(int kind, std::vector<std::uint32_t>& inOut) = 0;
    // The context / dialog builders that this slice leaves out (see PARTIAL in the source): they build window scripts.
    virtual void FoundationMenu(cSC4WinLotConfigurationEditor* editor, int x, int y) = 0;
    virtual void FenceMenu(cSC4WinLotConfigurationEditor* editor, int x, int y) = 0;
    virtual void LotConfigurationObjectMenu(cSC4WinLotConfigurationEditor* editor, int x, int y,
                                            cSC4LotConfigurationObject* object) = 0;
    virtual void EditAreaMenu(cSC4WinLotConfigurationEditor* editor, int x, int y) = 0;
    // DoOutsideAreaMenu @ 0x10041bd84: the menu for a right click outside the view (help / save / ...).
    virtual void OutsideAreaMenu(cSC4WinLotConfigurationEditor* editor, int x, int y) = 0;
    virtual void SetFoundationUI(cSC4WinLotConfigurationEditor* editor, std::uint32_t foundation,
                                 cSC4LotConfigurationObject* object) = 0;
    // cISC4BuildingDevelopmentSimulator vslot 0xa8 / 0x70 / 0x128 / 0x88.
    virtual bool IsFoundationType(std::uint32_t id) = 0;
    virtual bool IsBuildingType(std::uint32_t id) = 0;
    virtual bool BuildingName(std::uint32_t id, cRZString& out) = 0;
    virtual bool BuildingsInFamily(std::uint32_t family, std::vector<std::uint32_t>& out) = 0;
    // cISC4BuildingDevelopmentSimulator vslot 0xe0: the retaining wall's exemplar key.
    virtual bool RetainingWallExemplarKey(std::uint32_t id, cGZPersistResourceKey& out) = 0;
    // cISC4PropManager vslot 0x40 / 0x60 / 0x48.
    virtual bool IsPropType(std::uint32_t id) = 0;
    virtual bool PropName(std::uint32_t id, cRZString& out) = 0;
    virtual bool PropsInFamily(std::uint32_t family, std::vector<std::uint32_t>& out) = 0;
    // Resource manager: the name (property 0x20) and a property bit (vslot 0x18) of a lot-object exemplar.
    virtual bool ExemplarName(std::uint32_t exemplarType, std::uint32_t instance, cRZString& out) = 0;
    virtual bool ExemplarHasProperty(std::uint32_t exemplarType, std::uint32_t instance, std::uint32_t propertyId) = 0;
    // GZCOM() vslot 0x38.
    virtual std::uint32_t NewObjectId() = 0;
    // cRZKeyboard::IsKeyDownNowStatic @ 0x100241b76 (Shift is checked when an arrow key moves a prop vertically).
    virtual bool IsKeyDown(std::uint32_t key) = 0;
    // SL::spTerrain vslot 0x70 (tile heights, used for the camera target) and SL::spRenderer vslot 0x80 ->
    // cSC4CameraControl::SetViewTargetPosition @ 0x10076bf2a.
    virtual float TerrainHeight(int tileX, int tileZ) = 0;
    virtual void SetCameraTargetPosition(float x, float y, float z) = 0;
};
IWinLotConfigurationEditorHost* WinLotConfigurationEditorHost();
void SetWinLotConfigurationEditorHost(IWinLotConfigurationEditorHost* host);

// The process-wide family-name tables StaticInit fills from the editor ini file (sBuildingFamilyNameMap and
// friends in the original); the Add*FamilyName callbacks and the Get*FamilyName helpers use them.
const std::unordered_map<std::uint32_t, cRZString>& EditorBuildingFamilyNames();
const std::unordered_map<std::uint32_t, cRZString>& EditorFloraFamilyNames();
const std::unordered_map<std::uint32_t, cRZString>& EditorPropFamilyNames();

class cSC4WinLotConfigurationEditor : public gz::cGZWinGen, public cIGZWinMessageFilter {
public:
    explicit cSC4WinLotConfigurationEditor(cIGZWinMgr* mgr);   // @ 0x100417890 (+0x100417a08)
    ~cSC4WinLotConfigurationEditor() override;                 // @ 0x100417a12 (+0x100417cf2 / 0x100417cfc)

    bool QueryInterface(std::uint32_t iid, void** out) override;                 // @ 0x100417d30
    std::uint32_t AddRef() override { return cGZWinGen::AddRef(); }              // @ 0x10042ee84
    std::uint32_t Release() override { return cGZWinGen::Release(); }            // @ 0x1003d6634

    bool DoMessage(cGZMessage& message) override;                                // @ 0x10041a5b4
    bool Init() override;                                                        // @ 0x100417dac
    bool Shutdown() override;                                                    // @ 0x100418776
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;       // @ 0x10041a946
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;         // @ 0x10041b97a
    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;         // @ 0x10041b982
    bool GZOnMouseDownR(int x, int y, std::uint32_t modifiers) override;         // @ 0x10041bbf8
    bool GZOnMouseUpL(int x, int y, std::uint32_t modifiers) override;           // @ 0x10041c3c4
    bool GZOnMouseMove(int x, int y, std::uint32_t modifiers) override;          // @ 0x10041c4e8

    // --- cIGZWinMessageFilter (the view at +0x1c8)
    bool DoMessage(cIGZWin* window, cGZMessage& message) override;                // @ 0x10041a5fe (+ thunk 0x10041a934)

    // --- the editor interface, in vtable order (vslots 0x5b8..0x5f0)
    virtual bool SaveAs(bool askForPath, bool showMessages, bool saveConfiguration);            // 0x5b8 @ 0x100419aba (PARTIAL: file browser)
    virtual bool Close(bool askToSave, bool saveConfiguration);                                 // 0x5c0 @ 0x1004194ca
    virtual bool WriteLotData();                                                                // 0x5c8 @ 0x100419a30
    virtual bool CreateNewLotConfiguration();                                                   // 0x5d0 @ 0x10042d9be
    virtual bool ReadLotConfigurationExemplar(const cGZPersistResourceKey& key);                // 0x5d8 @ 0x10042db5a
    virtual void GetDefaultPath(cRZString& out);                                                // 0x5e0 @ 0x100419a66
    virtual void DoEditAreaMenu(int x, int y);                                                  // 0x5e8 @ 0x100424af8 (PARTIAL: menu script via seam)
    void DoOutsideAreaMenu(int x, int y);                                                       // @ 0x10041bd84 (PARTIAL: menu script via seam)
    virtual void Refresh3DView();                                                               // 0x5f0 @ 0x10042e836 (PARTIAL: redraw only)

    // --- non-virtual helpers
    bool Open(const cGZPersistResourceKey& key);                          // @ 0x100417602 (PARTIAL: SetLotSize/Prepare3DView missing)
    bool IsEditingExemplar(std::uint32_t id) const;                       // @ 0x100417880
    bool GetIdentityInfoForLotConfigurationType(std::uint32_t type, cSC4LotConfigurationIdentityInfo& out);   // @ 0x1004176a4
    bool GetIdentityInfoForLotConfigurationExemplarKey(const cGZPersistResourceKey& key,
                                                       cSC4LotConfigurationIdentityInfo& out);  // @ 0x10041891c
    void SetTitle();                                                      // @ 0x1004189d6
    void GoTo3DView();                                                    // @ 0x100419406
    void SetDataHasChanged(bool changed);                                 // @ 0x100419a1e
    int RemoveOutOfBoundsLotConfigurationObjects();                       // @ 0x100418d7e
    std::uint32_t MessageBoxA(const cRZString& text, std::uint32_t style);   // @ 0x10041963e
    bool Save(bool showMessages, bool saveConfiguration);                 // @ 0x1004196f2
    bool VerifyObjectsAreWithinLotBounds(bool oneSided, bool showMessages);   // @ 0x1004198f8
    static void StaticInit();                                             // @ 0x100417f5e
    void LoadCursors();                                                   // @ 0x100418198
    void PostInit();                                                      // @ 0x10041855e (PARTIAL: InitAlphaSystem missing)
    // cRZIniFile::EnumCallback: key -> display name, one per entry of the FamilyNames.ini sections.
    static void AddPropFamilyName(const cRZString& name, const cRZString& value, void* context);   // @ 0x10041849a
    static void AddBuildingFamilyName(const cRZString& name, const cRZString& value, void* context);   // @ 0x1004184da
    static void AddFloraFamilyName(const cRZString& name, const cRZString& value, void* context);  // @ 0x10041851a

    // --- editing
    bool DoEditMouseDownL(int x, int y, std::uint32_t modifiers);         // @ 0x10041ba60
    bool DoEditMouseDownR(int x, int y, std::uint32_t modifiers);         // @ 0x10041bcd2
    bool DoEditMouseMove(int x, int y, std::uint32_t modifiers);          // @ 0x10041c5ec
    std::uint32_t DoHitTest(int x, int y, std::uint32_t modifiers, cSC4LotConfigurationObject** object);   // @ 0x10041c728
    void SetSelectedLotConfigurationObject(cSC4LotConfigurationObject* object);   // @ 0x10041ccbe
    void AddNewLotConfigurationObject(int x, int y, std::uint32_t modifiers);     // @ 0x10041cd80
    void MoveObject(cSC4LotConfigurationObject* object, std::uint32_t key, std::uint32_t modifiers);   // @ 0x10041af7e
    void MoveObject(cSC4LotConfigurationObject* object, float x, float z);        // @ 0x1004243d8
    void RotateObject(cSC4LotConfigurationObject* object, std::uint32_t, std::uint32_t);   // @ 0x10041b130
    void MirrorObject(cSC4LotConfigurationObject* object, std::uint32_t, std::uint32_t);   // @ 0x10041b1c2
    void DuplicateObject(cSC4LotConfigurationObject* object, std::uint32_t, std::uint32_t);   // @ 0x10041b208
    void DeleteLotConfigurationObject(cSC4LotConfigurationObject* object, bool ask);   // @ 0x10041b462
    void MoveObjectToWithinLotBounds(cSC4LotConfigurationObject* object, bool asymmetric);   // @ 0x10042e1fc
    bool IsObjectWithinLotBounds(const cSC4LotConfigurationObject* object, bool asymmetric) const;   // @ 0x10042e7c4
    std::uint32_t LotFoundationIsPresent() const;                         // @ 0x10041d3bc
    void ToggleFence(int x, int y, std::uint32_t modifiers);              // @ 0x10041d826
    void ToggleFence(const SC4FenceSegment& segment);                     // @ 0x10042e12e
    void AddLotFoundation(std::uint32_t id, bool add);                    // @ 0x10042b9d4
    void DoShowHelpUI();                                                  // @ 0x10041ae3e
    void DoAddNewBuilding();                                              // @ 0x10041ac52
    void DoAddNewLotBaseTexture();                                        // @ 0x10041acca
    void DoAddNewProp(bool, bool);                                        // @ 0x10041ad42
    void DoAddNewFlora();                                                 // @ 0x10041adc6

    // The preview rasterisation of the configuration into the view bitmap (PARTIAL: see the source).
    void DrawCurrentLotConfiguration();                                  // @ 0x10042cc4e (PARTIAL: no rasterisation)

    // --- object names
    void GetLotConfigurationObjectName(const cSC4LotConfigurationObject* object, cRZString& out, bool brief);   // @ 0x10042dd7e
    std::uint32_t GetFoundationNameAndImplementationType(std::uint32_t id, cRZString& out);   // @ 0x10042bc2e
    void GetRetainingWallName(std::uint32_t id, cRZString& out);          // @ 0x10042bdc6
    void GetBuildingFamilyName(std::uint32_t family, cRZString& out);     // @ 0x10042bee6
    void GetBuildingName(std::uint32_t id, cRZString& out);               // @ 0x10042c0b8
    void GetFloraFamilyName(std::uint32_t family, cRZString& out);        // @ 0x10042c17a
    void GetPropFamilyName(std::uint32_t family, cRZString& out);         // @ 0x10042c230
    void GetPropName(std::uint32_t id, cRZString& out);                   // @ 0x10042c40a
    // cIGZWinGrid cell comparator (only the name field is used here).
    static int LotTextureSort(const cRZString& a, const cRZString& b);    // @ 0x10042cbe4 (the CellData::name field)

    // --- state (test / host access; the original has no getters)
    cSC4LotConfiguration* LotConfiguration() const { return configuration_; }
    void SetLotConfigurationForTest(cSC4LotConfiguration* c) { configuration_ = c; }
    cSC4LotConfigurationObject* SelectedObject() const { return selected_; }
    int SelectedIndex() const { return selectedIndex_; }
    bool DataHasChanged() const { return dataChanged_; }
    void SetLotConfigurationManagerForTest(cSC4LotConfigurationManager* m) { lotConfigManager_ = m; }
    std::uint32_t ToolMode() const { return toolMode_; }
    float Scale() const { return scale_; }
    cRZString& TitleBuffer() { return title_; }
    void SetChooserForTest(cIGZUnknown* chooser) { chooser_ = chooser; }
    // Test hook: SetLotSize (0x100418b44) also writes the configuration size but needs the 3D view; tests only need
    // the size the geometry helpers read.
    void SetLotSizeForTest(std::uint8_t x, std::uint8_t z) {
        if (configuration_) { configuration_->sizeX_ = x; configuration_->sizeZ_ = z; }
    }

private:
    void ReleaseInterface(cIGZUnknown*& p);

    // +0x1d0
    bool dataChanged_ = false;
    cIGZFont* font_ = nullptr;                 // +0x1d8
    cIGZUnknown* unused1e0_ = nullptr;             // +0x1e0 (released in Shutdown/dtor, never assigned here)
    gz::cIGZWinBMP* view_ = nullptr;                   // +0x1e8
    gz::cIGZWinText* statusText_ = nullptr;            // +0x1f0
    cIGZCursor* cursors_[7] = {};                  // +0x1f8, +0x200, +0x208, +0x210, +0x218, +0x220, +0x228
    cIGZUnknown* chooser_ = nullptr;               // +0x230
    cSC4LotConfigurationManager* lotConfigManager_ = nullptr;   // +0x238
    cSC4LotConfigurationIdentityInfo identity_;    // +0x240
    cSC4LotConfiguration* configuration_ = nullptr;    // +0x288
    cIGZUnknown* networkObject1_ = nullptr;        // +0x290
    cIGZUnknown* networkObject2_ = nullptr;        // +0x298
    std::uint32_t toolMode_ = 0;                   // +0x2a0
    std::vector<std::uint32_t> familyIds_;         // +0x2a8
    float scale_ = 1.0f;                           // +0x2c0
    cSC4LotConfigurationObject* selected_ = nullptr;   // +0x2c8
    int selectedIndex_ = 0;                        // +0x2d0
    bool dragging_ = false;                        // +0x2d4
    float dragOffsetX_ = 0, dragOffsetY_ = 0;      // +0x2d8 / +0x2dc
    std::vector<cRZString> unreleasedStrings_;     // +0x2e0 (never filled in this slice)
    std::vector<cIGZUnknown*> attachedObjects_;    // +0x2f8 (released on shutdown)
    std::uint8_t cycleIndex_ = 0;                  // +0x310
    [[maybe_unused]] std::uint32_t viewSlope_ = 0;   // +0x314 (Prepare3DView only)
    std::uint32_t lastObjectType_ = 0;             // +0x318
    cRZString title_;                              // the caption last built by SetTitle
};

}  // namespace sc4
