// cGZWinFileBrowser — the "open / save file" browser window (0x1002a6b00..0x1002b21f2, class cGZWinFileBrowser, interface
// cIGZWinFileBrowser iid 0x3aa52e64). It is a cGZWin (sub-object at +8) that owns a drive-path list, a file grid, a file
// name edit, a filter combo, OK / Cancel and parent / new-folder buttons, and the settings that style them.
//
// RECOVERED HERE: the whole settings / state model (flags, filters and filter sets, selection, directory location, sort
// and column layout, per-column fonts / colours / alignment, gutters, background), the directory operations
// (GoToParentDirectory, ChangeDirectory, CreateNewDirectory, DeleteDirectory), the selection handlers and the message
// protocol (0x7abe7203/4/5), FitStringIntoWidth, the reference counting and QueryInterface.
// NOT RECOVERED (blocked on cGZWinGrid / the control classes / the draw-context ABI, see notes/todo_pending):
// Init, AddControls, LoadDefaultIcon, CreateDefaultIcon, CreateDefaultButtonBuffer, CreateComboDownArrowBuffer,
// EnableChildControl, GetChildControl, GZPaint, Refresh, DoMessage, PullDriveListDown, UpdateDirectoryList, UpdateFileList.
// The controls the recovered functions touch are reached through the small seams below (IFileBrowserGrid /
// IFileBrowserCombo / IFileBrowserTextEdit); they are optional (null = the control was not created).
//
// Quirks kept: SetDefaultDirectoryLocation writes the CURRENT directory (0x1002ad5bc); GetDefaultDirectoryLocation and
// GetCurrentDirectoryLocation are the same function body; GetDefaultSelection never fills its last argument
// (0x1002ad894); SetDefaultSelection appends its third argument to the first (one string); GetBackgroundColor builds the
// colour from byte +0x4ae for red, green AND blue (0x1002af082); SetDriveListSize clamps to at least 4 and does not
// invalidate; GetFileListLayout(array) walks the six column slots without a bound check in the original (bounded here).
#pragma once
#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "gz/Font.h"
#include "gz/Win.h"
#include "gz/WinInterfaces.h"
#include "rz/cRZString.h"

constexpr std::uint32_t kGZIID_cIGZWinFileBrowser = 0x3aa52e64;

// Entry types of the file list (cGZWinFileBrowser::FileListEntry +0).
constexpr std::uint32_t kFileEntryParent = 0xbaa88501, kFileEntryDirectory = 0xbaa88502, kFileEntryFile = 0xbaa88503, kFileEntryFileAlt = 0xbaa88513;
// Messages sent to the notification target (p1 = window id).
constexpr std::uint32_t kFileBrowserMsgSelectionChanged = 0x7abe7203;   // p2 = &Selection
constexpr std::uint32_t kFileBrowserMsgSelectionChosen = 0x7abe7204;    // p2 = &Selection
constexpr std::uint32_t kFileBrowserMsgPathChanged = 0x7abe7205;        // p2 = &cRZString (current directory)
// Control ids (EnableChildControl takes id - 0xbaa55d50 as its index).
constexpr std::uint32_t kFileBrowserCtlDriveList = 0xbaa55d50 + 0, kFileBrowserCtlDriveButton = 0xbaa55d52, kFileBrowserCtlParentButton = 0xbaa55d53,
                        kFileBrowserCtlNewFolderButton = 0xbaa55d54, kFileBrowserCtlFileList = 0xbaa55d55, kFileBrowserCtlFileName = 0xbaa55d56,
                        kFileBrowserCtlFileType = 0xbaa55d57, kFileBrowserCtlOk = 0xbaa55d58, kFileBrowserCtlCancel = 0xbaa55d59;
// Browser flags (+0x254, initial 0xd).
enum FileBrowserFlags : int { kFBShowParent = 1, kFBFlag2 = 2, kFBShowDirectories = 4, kFBShowFiles = 8 };

class cIGZBuffer;

// cIGZWinFileBrowser::Selection: what the user picked.
struct FileBrowserSelection {
    std::uint32_t kind = 0;          // 0 parent directory, 1 directory, 2 file
    std::uint32_t field = 0;         // +4: the entry's second word
    const cRZString* fullPath = nullptr;     // +8 directory + name
    const cRZString* directory = nullptr;    // +0x10
    const cRZString* name = nullptr;         // +0x18
};

// ---- seams -------------------------------------------------------------------------------------------------------------
// The grid control (cGZWinGrid, iid 0xdaa6b9be): only the slots the recovered functions call.
class IFileBrowserGrid {
public:
    virtual ~IFileBrowserGrid() = default;
    virtual void GetCellUsedRange(cRZRect& out) = 0;                                  // vslot 0x190
    virtual bool GetCellTextValue(int column, int row, cRZString& out) = 0;          // vslot 0x378
    virtual void ScrollToRow(int row) = 0;                                            // vslot 0x120
    virtual bool IsCellVisible(int column, int row) = 0;                              // vslot 0x340
    virtual void SetFirstVisibleCell(int column, int row) = 0;                        // vslot 0x360
    virtual void ClearSelection() = 0;                                                // vslot 0x170
    virtual cIGZWin* AsIGZWin() = 0;                                                  // vslot 0x28
};
// The filter combo box (cGZWinCombo, iid 0x412ce496).
class IFileBrowserCombo {
public:
    virtual ~IFileBrowserCombo() = default;
    virtual void SetItemText(const cIGZString& text, std::uint32_t index) = 0;        // vslot 0xd8
    virtual void SetSelectedIndex(std::uint32_t index, bool notify) = 0;              // vslot 0xe0
};
// The file-name edit (cGZWinTextEdit, iid 0x231a1c57).
class IFileBrowserTextEdit {
public:
    virtual ~IFileBrowserTextEdit() = default;
    virtual void GetText(cRZString& out) = 0;                                         // vslot 0x170
    virtual void SetText(const char* text, std::uint32_t length, bool notify) = 0;    // vslot 0x180
};
// Engine services: graphic system colour conversion (RZ::GS() vslots 0x118 / 0x120) and the control manager's default
// text (RZ::WCM() vslot 0x118).
class IFileBrowserHost {
public:
    virtual ~IFileBrowserHost() = default;
    virtual std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;
    virtual void NativeToRGB(std::uint32_t native, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) = 0;
    virtual const cIGZString* DefaultText(std::uint32_t id) = 0;                      // null when the manager has none
};
constexpr std::uint32_t kWinCtrlTextEllipsis = 0x5377be31;   // default text id used by FitStringIntoWidth

// The interface in its original vtable order (slots 3..).
class cIGZWinFileBrowser : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;
    virtual void EndDialog(std::uint32_t result) = 0;
    virtual void SetFlag(int flag) = 0;
    virtual void SetFlags(int flags) = 0;
    virtual void ClearFlag(int flag) = 0;
    virtual bool IsFlagSet(int flag) = 0;
    virtual int GetFlags() = 0;
    virtual void* GetChildControl(std::uint32_t id) = 0;
    virtual void EnableChildControl(std::uint32_t id, bool enable) = 0;
    virtual void SetIconBuffer(std::uint32_t which, cIGZBuffer* buffer) = 0;
    virtual void SetButtonBuffer(std::uint32_t which, cIGZBuffer* buffer) = 0;
    virtual bool GetDefaultDirectoryLocation(cIGZString& out) const = 0;
    virtual bool SetDefaultDirectoryLocation(const cIGZString& path) = 0;
    virtual bool GetCurrentDirectoryLocation(cIGZString& out) const = 0;
    virtual bool SetCurrentDirectoryLocation(const cIGZString& path) = 0;
    virtual bool GetSelection(FileBrowserSelection& out) = 0;
    virtual bool SetSelection(const cIGZString& path) = 0;
    virtual bool GetFilter(std::uint32_t index, cIGZString& text, cIGZBuffer*& icon, std::uint32_t& id, std::uint32_t set) const = 0;
    virtual bool AddFilter(const cIGZString& pattern, cIGZBuffer* icon, std::uint32_t id, std::uint32_t set) = 0;
    virtual bool AddFilterSetDescription(const cIGZString& text, std::uint32_t set) = 0;
    virtual bool RemoveFilter(std::uint32_t id, std::uint32_t set) = 0;
    virtual bool RemoveFilter(const cIGZString& pattern, std::uint32_t set) = 0;
    virtual void RemoveAllFilters() = 0;
    virtual bool SetCurrentFilterSet(std::uint32_t set) = 0;
    virtual bool GetDefaultSelection(std::uint32_t& kind, cIGZString& name, cIGZString& unused) const = 0;
    virtual bool SetDefaultSelection(std::uint32_t kind, const cIGZString& name, const cIGZString& extra) = 0;
    virtual bool SetNewDirectoryName(const cIGZString& name) = 0;
    virtual bool GoToParentDirectory() = 0;
    virtual bool ChangeDirectory(const cIGZString& path) = 0;
    virtual bool CreateNewDirectory() = 0;
    virtual bool DeleteDirectory(const cIGZString& path) = 0;
};

class cGZWinFileBrowser : public cGZWin, public cIGZWinFileBrowser {
public:
    // One cell style (cIGZWinGrid::CellFormat, 0x28 bytes): font, six colours (index 4/5 are the highlight pair),
    // a flag byte and the alignment byte.
    struct FieldFormat {
        cIGZFont* font = nullptr;
        std::uint32_t colors[6] = {};
        bool flag = false;
        std::int8_t alignment = 0;
        std::int8_t extra = 0;
    };
    struct FilterListEntry {         // 0x38 bytes
        std::uint32_t id = 0;
        cIGZBuffer* icon = nullptr;
        cRZString pattern;
    };
    struct DriveListEntry {          // 0x40 bytes
        cRZString name;
        std::uint32_t kind = 0;      // +0x28
        cIGZBuffer* icon = nullptr;  // +0x30
        std::int32_t row = 0;        // +0x38
    };
    struct FileListEntry {           // 0x88 bytes
        std::uint32_t type = 0;
        std::uint32_t field = 0;     // +4
        cIGZBuffer* icon = nullptr;  // +8
        cRZString name;              // +0x10
    };
    struct ColumnSlot { bool enabled = false; std::int32_t position = -1; std::int32_t width = 10; };

    explicit cGZWinFileBrowser(cIGZWinMgr* mgr);              // @ 0x1002a6b00 (+ 0x1002a792c)
    ~cGZWinFileBrowser() override;                            // @ 0x1002a7936 (+ thunks and deleting forms)

    void SetHost(IFileBrowserHost* host) { host_ = host; }
    void SetGrid(IFileBrowserGrid* grid) { fileGrid_ = grid; }
    void SetDriveGrid(IFileBrowserGrid* grid) { driveGrid_ = grid; }
    void SetFilterCombo(IFileBrowserCombo* combo) { filterCombo_ = combo; }
    void SetFileNameEdit(IFileBrowserTextEdit* edit) { fileNameEdit_ = edit; }

    // unknown
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1002ac3b4
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x1002ac324
    std::uint32_t Release() override;                              // @ 0x1002ac340
    virtual void FinalRelease();                                   // @ 0x1002ac384
    cIGZWin* AsIGZWin() override { return static_cast<cGZWin*>(this); }   // @ 0x1002ac3fc
    void EndDialog(std::uint32_t result) override;                 // @ 0x1002ac406
    // flags
    std::uint32_t SetFlag(std::uint32_t flag, bool on) override { return cGZWin::SetFlag(flag, on); }   // @ 0x1002ac490
    void SetFlag(int flag) override;                               // @ 0x1002ac4b0
    void SetFlags(int flags) override;                             // @ 0x1002ac4ce
    void ClearFlag(int flag) override;                             // @ 0x1002ac576
    bool IsFlagSet(int flag) override { return (flags_browser_ & flag) != 0; }   // @ 0x1002ac596
    int GetFlags() override { return flags_browser_; }             // @ 0x1002ac5a6
    void* GetChildControl(std::uint32_t) override { return nullptr; }   // 0x1002ac5b2 is a tail call to cGZWin::GetChildWindowFromID; the id->control table is not recovered
    void EnableChildControl(std::uint32_t, bool) override {}        // 0x1002ac5c8 not recovered (see WINFB-1)
    void SetIconBuffer(std::uint32_t which, cIGZBuffer* buffer) override;     // @ 0x1002aa7b4
    void SetButtonBuffer(std::uint32_t which, cIGZBuffer* buffer) override;   // @ 0x1002aa80e
    void SetArea(int l, int t, int r, int b) override;             // @ 0x1002ad4d2 (+ thunk 0x1002ad52a)
    using cGZWin::SetArea;
    bool LayoutControls() { return true; }                         // @ 0x1002aa7ac
    // filters
    bool GetFilter(std::uint32_t index, cIGZString& text, cIGZBuffer*& icon, std::uint32_t& id, std::uint32_t set) const override;   // @ 0x1002acdb2
    bool AddFilter(const cIGZString& pattern, cIGZBuffer* icon, std::uint32_t id, std::uint32_t set) override;   // @ 0x1002ace60
    bool AddFilterSetDescription(const cIGZString& text, std::uint32_t set) override;   // @ 0x1002ad0ac
    bool RemoveFilter(std::uint32_t id, std::uint32_t set) override;   // @ 0x1002ad176
    bool RemoveFilter(const cIGZString& pattern, std::uint32_t set) override;   // @ 0x1002ad256
    void RemoveAllFilters() override { ClearFilterList(); }        // @ 0x1002ad35e
    virtual bool ClearFilterList();                                // @ 0x1002ad36c
    bool SetCurrentFilterSet(std::uint32_t set) override;          // @ 0x1002ad460
    // directory / selection
    bool GetDefaultDirectoryLocation(cIGZString& out) const override;   // @ 0x1002ad57a
    bool SetDefaultDirectoryLocation(const cIGZString& path) override; // @ 0x1002ad5bc
    bool GetCurrentDirectoryLocation(cIGZString& out) const override;   // @ 0x1002ad6d8
    bool SetCurrentDirectoryLocation(const cIGZString& path) override;  // @ 0x1002ad71a
    bool SetDefaultSelection(std::uint32_t kind, const cIGZString& name, const cIGZString& extra) override;   // @ 0x1002ad74a
    bool GetDefaultSelection(std::uint32_t& kind, cIGZString& name, cIGZString& unused) const override;       // @ 0x1002ad894
    bool SetSelection(const cIGZString& path) override;            // @ 0x1002ad8b4
    bool GetSelection(FileBrowserSelection& out) override;         // @ 0x1002adc46
    virtual bool GetSelectionInternal(FileBrowserSelection& out);  // @ 0x1002adde2
    bool SetNewDirectoryName(const cIGZString& name) override;     // @ 0x1002ade1c
    bool GoToParentDirectory() override;                           // @ 0x1002ade30
    bool ChangeDirectory(const cIGZString& path) override;         // @ 0x1002adfbc
    bool CreateNewDirectory() override;                            // @ 0x1002ae0da
    int GetRowOfEntryByName(const cRZString& name);                // @ 0x1002ae4a6
    bool DeleteDirectory(const cIGZString& path) override;         // @ 0x1002ae646
    // sort / layout
    bool SetFileSort(std::uint32_t key, std::uint32_t order);      // @ 0x1002ae840
    bool GetFileSort(std::uint32_t& key, std::uint32_t& order) const;   // @ 0x1002ae86a
    bool SetDirectorySort(std::uint32_t key);                      // @ 0x1002ae882
    bool GetDirectorySort(std::uint32_t& key) const;               // @ 0x1002ae8a6
    bool SetFileListLayout(const std::uint32_t* columns, std::uint32_t count);   // @ 0x1002ae8b6
    void SetFileListLayout(std::uint32_t a, std::uint32_t b, std::uint32_t c);   // @ 0x1002ae974
    bool GetFileListLayout(std::uint32_t* columns, std::uint32_t& count) const;  // @ 0x1002ae9c2
    bool GetFileListLayout(std::uint32_t& a, std::uint32_t& b, std::uint32_t& c) const;   // @ 0x1002aea06
    // field formats
    bool SetNameFieldFont(cIGZFont* f) { return SetFieldFont(nameFormat_, f); }          // @ 0x1002aea90
    bool SetNameFieldColors(int i, std::uint32_t c) { return SetFieldColor(nameFormat_, i, c); }   // @ 0x1002aeaf0
    bool SetNameFieldAlignment(int a) { return SetFieldAlignment(nameFormat_, a); }      // @ 0x1002aeb18
    std::uint32_t GetNameFieldColors(int i) const { return nameFormat_.colors[i]; }      // @ 0x1002aeb3c
    cIGZFont* GetNameFieldFont() const { return nameFormat_.font; }                      // @ 0x1002aeb4c
    int GetNameFieldAlignment() const { return nameFormat_.alignment; }                  // @ 0x1002aeb5a
    bool SetTypeFieldFont(cIGZFont* f) { return SetFieldFont(typeFormat_, f); }          // @ 0x1002aeb68
    bool SetTypeFieldColors(int i, std::uint32_t c) { return SetFieldColor(typeFormat_, i, c); }   // @ 0x1002aebc8
    bool SetTypeFieldAlignment(int a) { return SetFieldAlignment(typeFormat_, a); }      // @ 0x1002aebf0
    std::uint32_t GetTypeFieldColors(int i) const { return typeFormat_.colors[i]; }      // @ 0x1002aec14
    cIGZFont* GetTypeFieldFont() const { return typeFormat_.font; }                      // @ 0x1002aec24
    int GetTypeFieldAlignment() const { return typeFormat_.alignment; }                  // @ 0x1002aec32
    bool SetDriveFieldFont(cIGZFont* f) { return SetFieldFont(driveFormat_, f); }        // @ 0x1002aec40
    bool SetDriveFieldColors(int i, std::uint32_t c) { return SetFieldColor(driveFormat_, i, c); }   // @ 0x1002aeca0
    bool SetDriveFieldAlignment(int a) { return SetFieldAlignment(driveFormat_, a); }    // @ 0x1002aecc8
    std::uint32_t GetDriveFieldColors(int i) const { return driveFormat_.colors[i]; }    // @ 0x1002aecec
    cIGZFont* GetDriveFieldFont() const { return driveFormat_.font; }                    // @ 0x1002aecfc
    int GetDriveFieldAlignment() const { return driveFormat_.alignment; }                // @ 0x1002aed0a
    bool SetIconColumnWidth(std::uint32_t w) { columns_[0].width = int(w); return Dirty(); }   // @ 0x1002aed18
    std::uint32_t GetIconColumnWidth() const { return std::uint32_t(columns_[0].width); }      // @ 0x1002aed3c
    bool SetTypeColumnWidth(std::uint32_t w) { columns_[2].width = int(w); return Dirty(); }   // @ 0x1002aed48
    std::uint32_t GetTypeColumnWidth() const { return std::uint32_t(columns_[2].width); }      // @ 0x1002aed6c
    bool SetRowHeight(std::uint32_t h) { rowHeight_ = h; return Dirty(); }                     // @ 0x1002aed78
    std::uint32_t GetRowHeight() const { return rowHeight_; }                                  // @ 0x1002aed9c
    bool SetLineGrid(bool on, std::uint32_t color) { lineGrid_ = on ? color : 0x40000000u; return Dirty(); }   // @ 0x1002aeda8
    bool GetLineGrid(bool& on, std::uint32_t& color) const { color = lineGrid_; on = lineGrid_ != 0x40000000u; return true; }   // @ 0x1002aedd6
    bool SetColumnGrid(bool on, std::uint32_t color) { columnGrid_ = on ? color : 0x40000000u; return Dirty(); }   // @ 0x1002aedee
    bool GetColumnGrid(bool& on, std::uint32_t& color) const { color = columnGrid_; on = columnGrid_ != 0x40000000u; return true; }   // @ 0x1002aee1c
    bool SetHighlightColor(std::uint32_t fg, std::uint32_t bg);    // @ 0x1002aee34
    bool GetHighlightColor(std::uint32_t& fg, std::uint32_t& bg) const;   // @ 0x1002aee82
    bool SetGutters(std::uint32_t a, std::uint32_t b, std::uint32_t c) { gutters_[0] = a; gutters_[1] = b; gutters_[2] = c; return Dirty(); }   // @ 0x1002aee9a
    bool GetGutters(std::uint32_t& a, std::uint32_t& b, std::uint32_t& c) const { a = gutters_[0]; b = gutters_[1]; c = gutters_[2]; return true; }   // @ 0x1002aeeca
    bool SetDefaultFont(cIGZFont* f) { defaultFont_ = f; return Dirty(); }     // @ 0x1002aeeea (not reference counted)
    cIGZFont* GetDefaultFont() const { return defaultFont_; }                  // @ 0x1002aef0e
    bool GetParentDirectoryText(cIGZString& out) const;            // @ 0x1002aef1c
    bool SetParentDirectoryText(const cIGZString& text);           // @ 0x1002aef5e
    bool SetOutlineUse(bool on);                                   // @ 0x1002aef94
    bool GetOutlineUse() const { return outline_; }                // @ 0x1002aefc8
    bool SetDriveListSize(std::uint32_t n) { driveListSize_ = n < 4 ? 4 : n; return true; }   // @ 0x1002aefd4
    std::uint32_t GetDriveListSize() const { return driveListSize_; }   // @ 0x1002aefec
    bool SetBackgroundColor(std::uint32_t native);                 // @ 0x1002aeff8
    std::uint32_t GetBackgroundColor() const;                      // @ 0x1002af082
    bool SetBackgroundImage(cIGZBuffer* image);                    // @ 0x1002af0b2
    cIGZBuffer* GetBackgroundImage() const { return backgroundImage_; }   // @ 0x1002af112
    void SetBackgroundTransparent(bool transparent);               // @ 0x1002af120
    bool GetBackgroundTransparent() const { return !backgroundOpaque_; }   // @ 0x1002af172
    // directory list
    virtual void InitKnownInitialDirectory();                      // @ 0x1002af182
    virtual bool InitDriveList() { return true; }                  // @ 0x1002af228 (no drives on this platform)
    virtual bool PullDriveListUp();                                // @ 0x1002afb6a
    void FitStringIntoWidth(cRZString& text, int width, cIGZFont* font);   // @ 0x1002af834
    virtual bool HandleSelectionChange(int row);                   // @ 0x1002afbc6
    virtual bool HandleSelectionDoubleClick(int row);              // @ 0x1002aff80
    virtual bool HandleDriveListClick(int index);                  // @ 0x1002b02bc
    virtual bool HandleFileListDoubleClick(int row);               // @ 0x1002b042c
    virtual bool UpdatePath();                                     // @ 0x1002b0540
    virtual bool ClearFileList();                                  // @ 0x1002b2102
    virtual bool ClearDirectoryList();                             // @ 0x1002b218a
    // hooks for the unrecovered list builders (called by the recovered functions that the original routes there)
    virtual bool UpdateDirectoryList() { return true; }            // 0x1002b05da not recovered
    virtual bool UpdateFileList() { return true; }                 // 0x1002b0c58 not recovered

    // state for tests / the future control code
    std::vector<FileListEntry*>& FileEntries() { return files_; }
    std::list<DriveListEntry*>& DriveEntries() { return drives_; }
    bool NeedsRelayout() const { return dirty_; }
    void ClearRelayout() { dirty_ = false; }
    int SelectedRow() const { return selectedRow_; }
    int DriveIndex() const { return driveIndex_; }
    std::size_t FilterSetCount() const { return filterSets_.size(); }
    std::size_t FilterCount(std::size_t set) const { return set < filterSets_.size() ? filterSets_[set].size() : 0; }
    std::uint32_t CurrentFilterSet() const { return currentFilterSet_; }
    const std::string& CurrentDirectory() const { return currentDir_.str(); }
    cIGZBuffer* IconBuffer(int i) const { return icons_[i]; }
    cIGZBuffer* ButtonBuffer(int i) const { return buttons_[i]; }
    const ColumnSlot& Column(int i) const { return columns_[i]; }
    std::uint32_t ColumnCount() const { return columnCount_; }

private:
    bool Dirty() { dirty_ = true; InvalidateSelfAndParents(); return true; }
    bool SetFieldFont(FieldFormat& f, cIGZFont* font);
    bool SetFieldColor(FieldFormat& f, int index, std::uint32_t color) { f.colors[index] = color; return Dirty(); }
    bool SetFieldAlignment(FieldFormat& f, int a) { f.alignment = std::int8_t(a); return Dirty(); }
    void FillSelectionMessage(cGZMessage& m, std::uint32_t type, const FileBrowserSelection& sel);
    void SelectFromEntry(const FileListEntry& e);

    IFileBrowserHost* host_ = nullptr;
    IFileBrowserGrid *driveGrid_ = nullptr, *fileGrid_ = nullptr;
    IFileBrowserCombo* filterCombo_ = nullptr;
    IFileBrowserTextEdit* fileNameEdit_ = nullptr;
    std::vector<FileListEntry*> files_;                       // +0x158
    std::vector<std::vector<FilterListEntry*>> filterSets_;   // +0x200
    std::vector<cRZString> setDescriptions_;                  // +0x218
    std::uint32_t currentFilterSet_ = 0;                      // +0x230
    std::list<DriveListEntry*> drives_;                       // +0x238
    int fileSortKey_ = 0, fileSortOrder_ = 2, dirSort_ = 0;   // +0x1e8, +0x1ec, +0x1f0
    int flags_browser_ = 0xd;                                 // +0x254
    std::uint32_t gutters_[3] = {};                           // +0x258
    bool caseSensitiveFilters_ = false;                       // +0x250
    cRZString currentDir_;                                    // +0x268 (string +0x270)
    cRZString defaultDir_;                                    // +0x290
    std::uint32_t defaultSelKind_ = 0;                        // +0x2b8
    int driveIndex_ = 0;                                      // +0x2bc
    cRZString defaultSelName_;                                // +0x2c0
    cRZString parentText_;                                    // +0x2e8
    cRZString newDirName_;                                    // +0x310
    ColumnSlot columns_[6];                                   // +0x338
    std::uint32_t rowHeight_ = 20;                            // +0x380
    std::uint32_t columnCount_ = 0;                           // +0x384
    int selectedRow_ = 0;                                     // +0x388
    std::uint32_t driveListSize_ = 8;                         // +0x38c
    FieldFormat nameFormat_, typeFormat_, extraFormat_, driveFormat_;   // +0x398, +0x3c0, +0x3e8, +0x410
    cIGZFont* defaultFont_ = nullptr;                         // +0x438
    cIGZBuffer* icons_[7] = {};                               // +0x440
    cIGZBuffer* buttons_[4] = {};                             // +0x480
    std::uint32_t lineGrid_ = 0, columnGrid_ = 0;             // +0x4a0, +0x4a4
    bool outline_ = false;                                    // +0x4a8
    bool backgroundOpaque_ = false;                           // +0x4a9
    std::uint32_t backgroundColor_ = 0xffffffffu;             // +0x4ac
    cIGZBuffer* backgroundImage_ = nullptr;                   // +0x4b0
    bool dirty_ = false;                                      // +0x4c0
    FileBrowserSelection selection_;                          // kind +0x4c8, field +0x4cc
    cRZString selectionFull_, selectionDir_, selectionName_;  // +0x4d0, +0x4f8, +0x520
};
