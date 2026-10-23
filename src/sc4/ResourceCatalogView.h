// cSCResourceCatalogView — the resource catalog picker (33 functions at 0x100327af0..0x10032a6f0 plus
// SetFilterForType at 0x10052bff0; interface iid 0x7a368ee3): the window the "Add New Resource" / Ingred.ini
// editors show. A type combo (one entry per type/group pair registered in <data dir>Sys/Ingred.ini), a grid listing
// the available resources of the selected type, a per-type view window (cSCExemplarResourceView) for the selected
// entry, the selected key's file name, and the Done / Add New Resource / Remove Selected Resource / Cancel buttons.
//
// The object is the cISCResourceCatalogView interface at +0 (vtable 0x100a98160) followed by the cGZWin at +8
// (vtable 0x100a98218); cSCResCatalogPickerView (0x1005235f0) derives from it, which is what puts the interface at
// +0 and the window at +8 of that class too. Members after the window:
//   +0x158 bool readOnly        (SetReadOnly; 0 = the views may edit, the default)
//   +0x159 bool standalone      (SetStandalone; the ctor sets it, so Done and Cancel exist by default)
//   +0x15c uint32 filterForType (SetFilterForType; 0 = every type)
//   +0x160 uint32 filterForGroup(SetFilterForGroup; 0 = every group)
//   +0x168 std::map<uint32,uint32>  combo item index -> row in `types` (libc++ map: begin, end.left, size)
//   +0x180 uint32 selectedRow   index into `types`
//   +0x184 int dragOffsetX      set by GZOnMouseDownL (the original leaves +0x180/+0x184/+0x188
//                               uninitialised; the port zeroes them)
//   +0x188 int dragOffsetY
//   +0x190 std::vector<tTypeNameAssoc> types   the registered type/group pairs (0x38 each)
//   +0x1a8 IResourceTypeView* selectedView     the view currently shown (borrowed; Shutdown drops it without Release)
//   +0x1b0 IResourceCatalogGrid* instances     the instance grid (counted; released by Shutdown)
//
// tTypeNameAssoc (0x38): +0x00 uint32 type, +0x04 uint32 group, +0x08 cRZString name (the ini entry name),
// +0x30 cISCResourceView (counted; IResourceTypeView in the seam) — the view window that shows the resources of
// that type/group pair.
//
// Data flow: registerTypes() reads <data dir>Sys/Ingred.ini section [ExamplerCategories], each entry
// `Name=exemplarGroup[,ltextGroup]`, and makes one row for the exemplar type 0x6534284a and, when the second group
// is non-zero, one for the LTEXT type 0x05342861. Init() builds the controls and puts the rows that pass the
// type/group filters into the combo; selecting a combo entry (command 0xe8711255) maps back to a row and
// updateKeyList() fills the grid with the available resources of that row's type/group. Selecting a grid row puts
// that resource key into the row's view (setViewKey).
//
// Quirks kept on purpose (for the "Known quirks kept" table of docs/SEAMS.md):
//  * The ini section is spelled "ExamplerCategories" (18 chars, built inline at 0x100328f78; sic).
//  * The combo branch of GZOnCommand answers false (unhandled) but the missing-child branch answers true
//    (0x10032a0bb vs 0x10032a0ae); the "Remove Selected Resource" command does nothing and answers true
//    (0x10032a114 falls through to the shared true exit), and "Done" answers false after killing the window.
//  * doButtonPress("Add New Resource") falls through into the "Remove" exit, so it answers true.
//  * Shutdown() zeroes selectedView (+0x1a8) without releasing it (the row's counted view owns that reference).
//  * The grid kept in +0x1b0 owns its own reference; Init only creates it when +0x1b0 is still null (Shutdown
//    releases and clears it, so the next Init builds a fresh one).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gz/Win.h"
#include "gz/WinInterfaces.h"
#include "gz/cGZPersistResourceKey.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZString.h"

class cIGZBuffer;
namespace gz {
class IWinFont;
}

// IIDs / ids / colours the window uses.
constexpr std::uint32_t kGZIID_cISCResourceCatalogView = 0x7a368ee3;
constexpr std::uint32_t kResourceTypeComboID = 0x4c9303e;        // the type combo's window id
constexpr std::uint32_t kResourceInstanceGridID = 0x4c9303f;     // the instance grid's window id
constexpr std::uint32_t kResourceInstancesLabelID = 0x99b7a0e;
constexpr std::uint32_t kResourceFileNameLabelID = 0xc99b85f5;
constexpr std::uint32_t kResourceFileNameValueID = 0x90f3852;
constexpr std::uint32_t kResourceButtonImageGroup = 0x22dec92d;  // the button face sheet (group of the image keys)
constexpr std::uint32_t kComboChangedCommand = 0xe8711255;       // cGZWinCombo notification (p2 = the combo's id)
constexpr std::uint32_t kButtonActivatedCommand = 0x287259f6;    // cGZWinBtn notification (p2 = the button's command)
constexpr std::uint32_t kGridSelectionChangedMsg = 0x9a1552d2;   // posted by the grid (p1 = the grid)
constexpr std::uint32_t kGridKeyMsg = 0x9a1552d5;                // posted by the grid (p1 = the grid)
constexpr std::uint32_t kFocusChangeMsg = 0x4348b111;            // forwarded to the message server
constexpr std::uint32_t kResourceLabelFontStyle = 0xc8963c5c;   // RZ::WCM vslot 0x28 style id the labels ask for
constexpr std::uint32_t kResourceTextType = 0x05342861;          // LTEXT resources (the localized string of a name)
constexpr std::uint32_t kResourceExemplarType = 0x6534284a;      // EXMP resources (the exemplar's Name property)

// The button commands (doButtonPress) and the button images that go with them.
enum ResourceCatalogCommand : std::uint32_t {
    kResCmdDone = 0x24c93018,
    kResCmdAdd = 0x24c93019,
    kResCmdCancel = 0x24c9301a,
    kResCmdRemove = 0x24c9301b,
};
enum ResourceCatalogButtonImage : std::uint32_t {
    kResImgDone = 0x2001,
    kResImgAdd = 0x2005,
    kResImgRemove = 0x2008,
    kResImgCancel = 0x2007,
};

// ---- the interface the director hands out (iid 0x7a368ee3) ----------------------------------------------
// The slot order is the original's vtable order (0x100a98160): the six catalog entries, the destructor pair, then
// the window protocol the class re-declares (direct implementations in the object's first group, thunks in the
// cGZWin group at 0x100a98218).
class cISCResourceCatalogView : public cIGZUnknown {
public:
    virtual void SetStandalone(bool standalone) = 0;                    // [3]  0x18
    virtual void SetReadOnly(bool readOnly) = 0;                        // [4]  0x20
    virtual void SetFilterForType(std::uint32_t type) = 0;              // [5]  0x28
    virtual void SetFilterForGroup(std::uint32_t group) = 0;            // [6]  0x30
    virtual bool GetSelectedKey(cGZPersistResourceKey& key) = 0;        // [7]  0x38
    virtual void RefreshKeyList() = 0;                                  // [8]  0x40
    virtual ~cISCResourceCatalogView() = default;                       // [9],[10] 0x48 / 0x50
    virtual bool Init() = 0;                                            // [11] 0x58
    virtual bool Shutdown() = 0;                                        // [12] 0x60
    virtual bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) = 0;   // [13] 0x68
    virtual bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) = 0;     // [14] 0x70
    virtual bool GZOnCommand(std::uint32_t type, std::uint64_t param) = 0;      // [15] 0x78
    virtual bool GZPaint() = 0;                                                 // [16] 0x80
    virtual bool GZOnMouseDownL(int x, int y, std::uint32_t flags) = 0;         // [17] 0x88
    virtual bool GZOnMouseUpL(int x, int y, std::uint32_t flags) = 0;           // [18] 0x90
    virtual bool GZOnMouseMove(int x, int y, std::uint32_t flags) = 0;          // [19] 0x98
    virtual bool DoMessage(cGZMessage& message) = 0;                            // [20] 0xa0
};

// ---- seams ----------------------------------------------------------------------------------------------
// Every neighbour this class reaches is a seam: one of the small interfaces below, each method mirroring one
// original call (vslot / address in the comment). The test supplies doubles, the app supplies adapters over the
// real classes (cGZWinGrid, cGZWinCombo, cGZWinText, cGZWinBtn, cGZWinFlatRect, cSCExemplarResourceView,
// cSCResKeyInputView, cGZPersistResourceManager).

// The per-type view (cSCExemplarResourceView; its cISCResourceView interface sits at +0x160 of the view, vtable
// 0x100a9a748). Only the entries the catalog calls.
class IResourceTypeView {
public:
    virtual ~IResourceTypeView() = default;
    virtual cIGZWin* AsIGZWin() = 0;                                     // [3]  0x18
    virtual void SetKeyToView(const cGZPersistResourceKey& key) = 0;      // [4]  0x20
    virtual void PromptToSave() = 0;                                     // [5]  0x28
    virtual void SetReadOnly(bool readOnly) = 0;                         // [6]  0x30
    virtual void AddRef() = 0;                                           // [1]  0x08
    virtual void Release() = 0;                                          // [2]  0x10
};

// The instance grid (cGZWinGrid, iid 0xdaa6b9be, created by `new cGZWinGrid(flags)` @ 0x100250bfc). Only the slots
// Init / updateKeyList / GetSelectedKey / DoMessage call.
class IResourceCatalogGrid {
public:
    virtual ~IResourceCatalogGrid() = default;
    using CellCompare = int (*)(const void*, const void*);
    virtual cIGZWin* AsIGZWin() = 0;                                                            // [5]  0x28
    virtual void SetArea(int l, int t, int r, int b) = 0;                                       // [7]  0x38
    virtual void ClearFlag(int flag) = 0;                                                       // [12] 0x60
    virtual void SetDefaultFontColor(std::uint32_t native, int index) = 0;                      // [17] 0x88
    virtual void ActLikeAListbox(int, int, bool) = 0;                                           // [30] 0xf0
    virtual bool GetSelection(int& column, int& row) = 0;                                       // [41] 0x148
    virtual bool SetSelection(int column, int row) = 0;                                         // [43] 0x158
    virtual bool GetCellUsedRange(cRZRect& range) = 0;                                          // [50] 0x190
    virtual void SetColumnWidth(int column, int endColumn, int width) = 0;                      // [65] 0x208
    virtual void SetDefaultRowHeight(int height) = 0;                                           // [66] 0x210
    virtual void SetRowHeight(int row, int endRow, int height) = 0;                             // [67] 0x218
    virtual void ClearAllCells() = 0;                                                           // [98] 0x310
    virtual void SetCellTextValue(int column, int row, const char* text, std::uint32_t length, int flags, bool) = 0;  // [112] 0x380
    virtual void SetCellExtraData(int column, int row, void* data) = 0;                         // [119] 0x3b8
    virtual void* GetCellExtraData(int column, int row) = 0;                                    // [120] 0x3c0
    virtual void SortColumn(int column, int startRow, bool ascending, CellCompare compare) = 0; // [131] 0x418
    virtual void AddRef() = 0;                                                                  // [1]  0x08
    virtual void Release() = 0;                                                                 // [2]  0x10
};

// The type combo (cGZWinCombo, iid 0x412ce496, GZCOM class 0x59b).
class IResourceCatalogCombo {
public:
    virtual ~IResourceCatalogCombo() = default;
    virtual cIGZWin* AsIGZWin() = 0;                                      // [3]  0x18
    virtual void SetFontColor(int index, std::uint32_t native) = 0;       // [12] 0x60 (0 text, 2 selected)
    virtual void SetHighlightColor(std::uint32_t native) = 0;             // [16] 0x80
    virtual void SetEditable(bool editable) = 0;                          // [21] 0xa8
    virtual void InsertString(const cIGZString& text, int index) = 0;     // [27] 0xd8 (-2 appends)
    virtual void SetSelection(int index, bool notify) = 0;                // [28] 0xe0
    virtual int GetSelection() = 0;                                       // [29] 0xe8
    virtual void Release() = 0;                                           // [2]  0x10
};

// A push button (cIGZWinBtn from the window control manager, WCM vslot 0xa0 CreateFixedButton).
class IResourceCatalogButton {
public:
    virtual ~IResourceCatalogButton() = default;
    virtual cIGZWin* AsIGZWin() = 0;                                      // [3]  0x18
    virtual void SetBtnFlag(int flag, bool on) = 0;                       // [5]  0x28
    virtual void FitSizeToContent() = 0;                                  // [33] 0x108
    virtual void SetTipText(const cIGZString& text) = 0;                  // [35] 0x118
    virtual void Release() = 0;                                           // [2]  0x10
};

// A static label (cIGZWinText, GZCOM class 0x592, iid 0x212cdc1f).
class IResourceCatalogLabel {
public:
    virtual ~IResourceCatalogLabel() = default;
    virtual cIGZWin* AsIGZWin() = 0;                                      // [3]  0x18
    virtual void FitWindowToText(bool a, bool b) = 0;                     // [5]  0x28
    virtual void SetWinTextFlag(int flag, bool on) = 0;                   // [7]  0x38
    virtual void SetTextColor(std::uint32_t native) = 0;                  // [8]  0x40
    virtual void SetFont(gz::IWinFont* font) = 0;                         // [18] 0x90
    virtual void Release() = 0;                                           // [2]  0x10
};

// The flat-rect backdrop (cGZWinFlatRect, GZCOM class 0xc2afa76e, interface 0xc2afa76f).
class IResourceCatalogBackdrop {
public:
    virtual ~IResourceCatalogBackdrop() = default;
    virtual cIGZWin* AsIGZWin() = 0;                                      // [3]  0x18
    virtual void SetOutlineFlags(int flags) = 0;                          // [9]  0x48 (0xf = all four edges)
    virtual void SetOutlineColor(std::uint32_t native) = 0;               // [14] 0x70
    virtual void SetSizeMode(int mode) = 0;                               // [18] 0x90
    virtual void Release() = 0;                                           // [2]  0x10
};

// Everything the catalog reaches outside itself. Each entry names the original call it stands for.
class IResourceCatalogHost {
public:
    virtual ~IResourceCatalogHost() = default;

    // --- creation ---
    virtual IResourceCatalogLabel* CreateLabel() = 0;                     // GZCOM()->CreateInstance(0x592, 0x212cdc1f, &out) @0x100328d85
    virtual IResourceCatalogButton* CreateFixedButton(std::uint32_t id, const cIGZString& caption,
                                                      cIGZBuffer* image, const cRZRect* area) = 0;   // RZ::WCM vslot 0xa0 @0x100328c94
    virtual IResourceCatalogCombo* CreateTypeCombo() = 0;                 // GZCOM()->CreateInstance(0x59b, 0x412ce496, &out) @0x1003283fc
    virtual IResourceCatalogBackdrop* CreateBackdrop() = 0;               // GZCOM()->CreateInstance(0xc2afa76e, 0xc2afa76f, &out) @0x100327eea
    virtual IResourceCatalogGrid* CreateGrid(std::uint32_t flags) = 0;    // new cGZWinGrid(flags) @0x100250bfc
    virtual gz::IWinFont* SystemFont(std::uint32_t style) = 0;            // RZ::WCM()->vslot 0x28 @0x100328dd9
    virtual IResourceTypeView* CreateTypeView() = 0;                      // new cSCExemplarResourceView @0x10035c52c

    // --- data directory (RZ::DDir(cRZString&) @0x100127a12) ---
    virtual std::string DataDirectory() = 0;

    // --- persistence (SL::spRM) ---
    // spRM vslot 0x90 GetAvailableResourceListForType(&list, type) and the list's vslot 0x48 Size / 0x50 GetKey.
    // The keys stay valid until the next call (the grid points its cells' extra data at them, as the original points
    // into the manager's key list).
    virtual const std::vector<cGZPersistResourceKey>& AvailableResources(std::uint32_t type) = 0;
    // spRM vslot 0x18 GetResource(key, iid, &out): for the LTEXT path (iid 0x452160fb) and the exemplar path
    // (iid 0xa52160f5) the Name (property 0x20) as text. False when neither lookup yields a name.
    virtual bool ResourceName(const cGZPersistResourceKey& key, cRZString& name) = 0;
    virtual bool TestForKey(cGZPersistResourceKey& key) = 0;                        // spRM vslot 0x70 @0x10032a3af
    virtual bool CreateNewResource(const cGZPersistResourceKey& key) = 0;           // spRM vslot 0x28, iid 0x456b8f1d @0x10032a3d6
    // spRM vslot 0x100 FindDBSegment(group) + the segment's vslot 0x58 Save(key) @0x10032a3f1
    virtual bool SaveResourceToGroupSegment(const cGZPersistResourceKey& key) = 0;
    // spRM vslot 0xf8 FindDBSegment(key) + QueryInterface(0x856c8cf7) + that object's vslot 0x90(key, out)
    virtual bool ResourceFileName(const cGZPersistResourceKey& key, cRZString& out) = 0;

    // --- the resource key input dialog (cSCResKeyInputView @0x10035adac) ---
    // Creates the view for `seed`, centres it in the main window, runs it through the window manager's vslot 0x148
    // (RunPopup, 0 = accepted), reads the key back from +0x164/+0x168/+0x16c and the file name from +0x170, then
    // Shutdowns and releases it. False when the user cancelled.
    virtual bool RunKeyInputDialog(const cGZPersistResourceKey& seed, cGZPersistResourceKey& key, cRZString& fileName) = 0;

    // --- the message server (RZ::MS()->vslot 0x20 MessagePost(message, false) @0x100329bd2) ---
    virtual void PostMessage(cGZMessage& message) = 0;

    // cIGZWin::QueryInterface(0x412ce496, &out) on the type combo's window (0x10032a016..0x10032a025).
    virtual IResourceCatalogCombo* ComboFromWindow(cIGZWin* window) = 0;
};
IResourceCatalogHost* ResourceCatalogHost();
void SetResourceCatalogHost(IResourceCatalogHost* host);

// ---- the window -----------------------------------------------------------------------------------------
class cSCResourceCatalogView : public cISCResourceCatalogView, public cGZWin {
public:
    // tTypeNameAssoc, 0x38 bytes: one registered type/group pair.
    struct tTypeNameAssoc {
        std::uint32_t type = 0;                       // +0x00
        std::uint32_t group = 0;                      // +0x04
        cRZString name;                               // +0x08 (the ini entry name)
        cRZAutoRefCount<IResourceTypeView> view;      // +0x30
    };

    cSCResourceCatalogView();       // @ 0x100327af0 (complete-object ctor 0x100327b9c)
    ~cSCResourceCatalogView() override;   // @ 0x100327c30 (+ 0x100327cc6, 0x100327cde)

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x100327d1a (+ thunk 0x100327d3c)
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }   // @ 0x100327d62 (+ thunk 0x100327d70)
    std::uint32_t Release() override { return cGZWin::Release(); } // @ 0x100327d7a (+ thunk 0x100327d88)

    void SetStandalone(bool standalone) override { standalone_ = standalone; }   // @ 0x10032a6c8
    void SetReadOnly(bool readOnly) override { readOnly_ = readOnly; }           // @ 0x10032a6d6
    void SetFilterForType(std::uint32_t type) override { filterType_ = type; }   // @ 0x10052bff0
    void SetFilterForGroup(std::uint32_t group) override { filterGroup_ = group; }   // @ 0x10032a6f0
    bool GetSelectedKey(cGZPersistResourceKey& key) override;      // @ 0x100329ed4
    void RefreshKeyList() override;                                // @ 0x100329cc8

    bool Init() override;                                          // @ 0x100327d92 (+ thunk 0x100329412)
    bool Shutdown() override;                                      // @ 0x100329a1e (+ thunk 0x100329aec)
    bool DoMessage(cGZMessage& message) override;                  // @ 0x100329afa (+ thunk 0x100329cba)
    bool GZPaint() override;                                       // @ 0x100329f4a (+ thunk 0x100329fa4)
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;   // @ 0x100329d04 (+ thunk 0x100329d2e)
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;     // @ 0x100329d54 (+ thunk 0x100329d7e)
    bool GZOnCommand(std::uint32_t type, std::uint64_t param) override;      // @ 0x100329fb2 (+ thunk 0x10032a138)
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;         // @ 0x100329da4 (+ thunk 0x100329e1e)
    bool GZOnMouseUpL(int x, int y, std::uint32_t flags) override;           // @ 0x100329eb4 (+ thunk 0x100329ec6)
    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;          // @ 0x100329e30 (+ thunk 0x100329ea2)

    void updateKeyList(const cGZPersistResourceKey& current);       // @ 0x1003290b6
    void setViewKey(const cGZPersistResourceKey& key);              // @ 0x100329be4
    void createButton(IResourceCatalogButton*& button, std::uint32_t instance, std::uint32_t group,
                      std::uint32_t id, const cRZString& caption);  // @ 0x100328bce
    void createLabel(IResourceCatalogLabel*& label, const cRZString& caption, std::uint32_t color);   // @ 0x100328d66
    void registerTypes();                                           // @ 0x100328e86
    static void registerTypesFromIni(const cRZString& key, const cRZString& value, void* context);     // @ 0x100329424
    bool addResource();                                             // @ 0x10032a1b8
    bool doButtonPress(std::uint32_t command);                      // @ 0x10032a0c0
    void KillSelf(std::uint32_t result);                            // @ 0x10032a146

    // Test / debug accessors (not in the original).
    bool Standalone() const { return standalone_; }
    bool ReadOnly() const { return readOnly_; }
    std::uint32_t FilterForType() const { return filterType_; }
    std::uint32_t FilterForGroup() const { return filterGroup_; }
    std::uint32_t SelectedRow() const { return selectedRow_; }
    const std::vector<tTypeNameAssoc>& Types() const { return types_; }
    const std::map<std::uint32_t, std::uint32_t>& ComboRows() const { return comboRow_; }
    IResourceTypeView* SelectedView() const { return selectedView_; }
    IResourceCatalogGrid* Instances() const { return grid_; }

private:
    std::uint8_t readOnly_ = 0;        // +0x158
    std::uint8_t standalone_ = 1;      // +0x159 (the ctor stores 0x100 at +0x158: readOnly 0, standalone 1)
    std::uint32_t filterType_ = 0;     // +0x15c
    std::uint32_t filterGroup_ = 0;    // +0x160
    std::map<std::uint32_t, std::uint32_t> comboRow_;   // +0x168
    std::uint32_t selectedRow_ = 0;    // +0x180 (the original leaves it uninitialised until Init)
    int dragOffsetX_ = 0;              // +0x184 (the original only sets it on the first mouse-down)
    int dragOffsetY_ = 0;              // +0x188
    std::vector<tTypeNameAssoc> types_;    // +0x190
    IResourceTypeView* selectedView_ = nullptr;   // +0x1a8 (borrowed, no reference)
    IResourceCatalogGrid* grid_ = nullptr;        // +0x1b0 (counted)
    // The original keeps no combo pointer: the combo window is owned by the window tree and looked up again by id
    // (GetChildWindowFromID(0x4c9303e)) in GZOnCommand, so the object stays 0x1b8 bytes (cSCResCatalogPickerView
    // puts its cRZVariant at +0x1c0).
};
