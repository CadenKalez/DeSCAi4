#include "gz/WinFileBrowser.h"

#include "gz/Buffer.h"

#include <algorithm>

#include "rz/cRZDirectory.h"
#include "rz/cRZFile.h"

namespace {
constexpr std::uint32_t kAllSets = 0xffffffffu;

void ReleaseIcon(cIGZBuffer*& icon) {
    if (icon) { static_cast<cIGZUnknown*>(icon)->Release(); icon = nullptr; }
}
}  // namespace

// cIGZBuffer is only forward declared here; every buffer is counted through its cIGZUnknown base.
static cIGZUnknown* AsUnknown(cIGZBuffer* b) { return static_cast<cIGZUnknown*>(b); }

cGZWinFileBrowser::cGZWinFileBrowser(cIGZWinMgr* mgr) : cGZWin(mgr) {
    parentText_.AssignFromCString("..");        // _SZ_DEFAULT_PARENT_DIRECTORY_TEXT
    newDirName_.AssignFromCString("New Folder");   // _SZ_DEFAULT_NEW_DIRECTORY_NAME
    columns_[0] = {true, 0, 10};
    columns_[1] = {true, 2, 10};
    columns_[5] = {true, 1, 2};
    for (int i : {2, 3, 4}) columns_[i] = {false, -1, 10};
    columns_[0].width = 10;
    columnCount_ = 0;
    rowHeight_ = 20;
    driveListSize_ = 8;
    // The grid's default CellFormat constructor is not recovered; the four formats start zeroed (see WINFB-4).
}

cGZWinFileBrowser::~cGZWinFileBrowser() {
    ClearFileList();
    ClearDirectoryList();
    ClearFilterList();
    for (cIGZBuffer*& b : icons_) ReleaseIcon(b);
    for (cIGZBuffer*& b : buttons_) ReleaseIcon(b);
    ReleaseIcon(backgroundImage_);
    for (FieldFormat* f : {&nameFormat_, &typeFormat_, &extraFormat_, &driveFormat_})
        if (f->font) { f->font->Release(); f->font = nullptr; }
}

bool cGZWinFileBrowser::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZWinFileBrowser) {
        *out = static_cast<cIGZWinFileBrowser*>(this);
        AddRef();
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

std::uint32_t cGZWinFileBrowser::Release() {
    if (refCount_ > 1) return --refCount_;
    FinalRelease();
    return 0;
}

void cGZWinFileBrowser::FinalRelease() {
    Shutdown();    // vslot 0x2e8
    delete this;   // deleting destructor (vslot 0x2d0)
}

void cGZWinFileBrowser::EndDialog(std::uint32_t result) {
    if (!GetFlag(0x1000)) {
        // Not modal: tell the parent to delete us.
        cGZWin::PostMsg(parent_, kGZMsgDeleteChild, std::uint64_t(reinterpret_cast<std::uintptr_t>(AsIGZWin())), 0, 0);
    } else {
        mgr_->EndDialog(0xf, AsIGZWin(), result);   // window manager vslot 0x48
    }
}

// ---- flags -------------------------------------------------------------------------------------------------------------
void cGZWinFileBrowser::SetFlag(int flag) {
    if ((flags_browser_ & flag) != 0) return;
    SetFlags(flags_browser_ | flag);
}

void cGZWinFileBrowser::ClearFlag(int flag) {
    if ((flags_browser_ & flag) == 0) return;
    SetFlags(flags_browser_ & ~flag);
}

void cGZWinFileBrowser::SetFlags(int flags) {
    if (flags_browser_ == flags) return;
    const unsigned changed = unsigned(flags_browser_) ^ unsigned(flags);
    flags_browser_ = flags;
    if (changed == 0) return;
    if (changed & 2) {
        // Bit 2 chooses between drawing into the parent (clear: 0x20000 and 0x40000) and a private buffer (set: 0x10000).
        if ((flags & 2) == 0) {
            SetFlag(std::uint32_t(0x20000), true);
            SetFlag(std::uint32_t(0x40000), true);
        } else {
            SetFlag(std::uint32_t(0x20000), false);
            SetFlag(std::uint32_t(0x40000), false);
            SetFlag(std::uint32_t(0x10000), true);
        }
        InvalidateSelfAndParents();
        return;
    }
    if (changed & 0xd) UpdatePath();
}

void cGZWinFileBrowser::SetIconBuffer(std::uint32_t which, cIGZBuffer* buffer) {
    if (which >= 7 || icons_[which] == buffer) return;
    if (buffer) AsUnknown(buffer)->AddRef();
    if (icons_[which]) AsUnknown(icons_[which])->Release();
    icons_[which] = buffer;
}

void cGZWinFileBrowser::SetButtonBuffer(std::uint32_t which, cIGZBuffer* buffer) {
    if (which >= 4 || buttons_[which] == buffer) return;
    if (buffer) AsUnknown(buffer)->AddRef();
    if (buttons_[which]) AsUnknown(buttons_[which])->Release();
    buttons_[which] = buffer;
    // The matching button (OK, Cancel, parent, new folder) would be handed the new image here (0x1002aa8c4 switch);
    // the button controls are not part of this recovery (WINFB-1).
}

void cGZWinFileBrowser::SetArea(int l, int t, int r, int b) {
    dirty_ = true;
    InvalidateSelfAndParents();
    cGZWin::SetArea(l, t, r, b);
}

// ---- filters -----------------------------------------------------------------------------------------------------------
bool cGZWinFileBrowser::GetFilter(std::uint32_t index, cIGZString& text, cIGZBuffer*& icon, std::uint32_t& id, std::uint32_t set) const {
    if (set == kAllSets) set = currentFilterSet_;
    if (set >= filterSets_.size()) return false;
    const auto& list = filterSets_[set];
    if (index >= list.size()) return false;
    const FilterListEntry* e = list[index];
    text.Copy(e->pattern);
    icon = e->icon;
    id = e->id;
    return true;
}

bool cGZWinFileBrowser::AddFilter(const cIGZString& pattern, cIGZBuffer* icon, std::uint32_t id, std::uint32_t set) {
    if (set == kAllSets) set = currentFilterSet_;
    if (filterSets_.size() <= set) filterSets_.resize(set + 1);
    if (setDescriptions_.size() <= set) setDescriptions_.resize(set + 1);
    auto* entry = new FilterListEntry;
    entry->pattern.AssignFromIGZString(pattern);
    entry->icon = icon;
    entry->id = id;
    if (icon) AsUnknown(icon)->AddRef();
    filterSets_[set].push_back(entry);
    dirty_ = true;
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinFileBrowser::AddFilterSetDescription(const cIGZString& text, std::uint32_t set) {
    if (set == kAllSets) set = currentFilterSet_;
    if (filterSets_.size() <= set) filterSets_.resize(set + 1);
    if (setDescriptions_.size() <= set) setDescriptions_.resize(set + 1);
    setDescriptions_[set].AssignFromIGZString(text);
    if (filterCombo_) filterCombo_->SetItemText(text, set);
    return true;
}

bool cGZWinFileBrowser::RemoveFilter(std::uint32_t id, std::uint32_t set) {
    if (set == kAllSets) set = currentFilterSet_;
    if (set >= filterSets_.size()) return false;
    auto& list = filterSets_[set];
    for (auto it = list.begin(); it != list.end(); ++it) {
        if ((*it)->id == id) {
            ReleaseIcon((*it)->icon);
            delete *it;
            list.erase(it);
            dirty_ = true;
            InvalidateSelfAndParents();
            return true;
        }
    }
    return false;
}

bool cGZWinFileBrowser::RemoveFilter(const cIGZString& pattern, std::uint32_t set) {
    if (set == kAllSets) set = currentFilterSet_;
    if (set >= filterSets_.size()) return false;
    auto& list = filterSets_[set];
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (pattern.IsEqualToStringRef((*it)->pattern, true)) {
            ReleaseIcon((*it)->icon);
            delete *it;
            list.erase(it);
            dirty_ = true;
            InvalidateSelfAndParents();
            return true;
        }
    }
    return false;
}

bool cGZWinFileBrowser::ClearFilterList() {
    for (auto& set : filterSets_) {
        for (FilterListEntry* e : set) {
            ReleaseIcon(e->icon);
            delete e;
        }
    }
    filterSets_.clear();     // the set descriptions are not cleared (kept)
    currentFilterSet_ = 0;
    return true;
}

bool cGZWinFileBrowser::SetCurrentFilterSet(std::uint32_t set) {
    if (set >= filterSets_.size()) return false;
    currentFilterSet_ = set;
    if (filterCombo_) filterCombo_->SetSelectedIndex(set, false);
    dirty_ = true;
    InvalidateSelfAndParents();
    return true;
}

// ---- directory location / selection ------------------------------------------------------------------------------------------
bool cGZWinFileBrowser::GetDefaultDirectoryLocation(cIGZString& out) const {
    out.AssignFromChars(currentDir_.Data(), currentDir_.Strlen());   // same body as GetCurrentDirectoryLocation (kept)
    return true;
}

bool cGZWinFileBrowser::SetDefaultDirectoryLocation(const cIGZString& path) {
    cRZString candidate(path);
    if (!cRZDirectory::DoesDirectoryExist(candidate)) {
        currentDir_ = defaultDir_;   // writes the CURRENT directory, not the default (kept)
        return false;
    }
    currentDir_ = candidate;
    return true;
}

bool cGZWinFileBrowser::GetCurrentDirectoryLocation(cIGZString& out) const {
    out.AssignFromChars(currentDir_.Data(), currentDir_.Strlen());
    return true;
}

bool cGZWinFileBrowser::SetCurrentDirectoryLocation(const cIGZString& path) {
    SetDefaultDirectoryLocation(path);
    dirty_ = true;
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinFileBrowser::SetDefaultSelection(std::uint32_t kind, const cIGZString& name, const cIGZString& extra) {
    defaultSelKind_ = kind;
    defaultSelName_.AssignFromIGZString(name);
    defaultSelName_.str().append(extra.ToChar(), extra.Strlen());   // the second part is appended to the same string
    dirty_ = true;
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinFileBrowser::GetDefaultSelection(std::uint32_t& kind, cIGZString& name, cIGZString&) const {
    kind = defaultSelKind_;
    name.Copy(defaultSelName_);   // the third argument is never written (kept)
    return true;
}

bool cGZWinFileBrowser::SetSelection(const cIGZString& path) {
    char drive[256], dir[256], name[256], ext[256];
    Splitpath(path.ToChar(), drive, dir, name, ext);
    cRZString directory;
    directory.AssignFromCString(drive);
    directory.str().append(dir);
    if (!cRZDirectory::DoesDirectoryExist(directory)) return false;
    selectionDir_ = directory;
    selectionName_.AssignFromCString(name);
    selectionName_.str().append(ext);
    selectionFull_ = selectionDir_;
    selectionFull_.str().append(selectionName_.str());
    selection_.kind = 2;
    currentDir_ = directory;
    dirty_ = true;
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinFileBrowser::GetSelectionInternal(FileBrowserSelection& out) {
    out.kind = selection_.kind;
    out.field = selection_.field;
    out.fullPath = &selectionFull_;
    out.directory = &selectionDir_;
    out.name = &selectionName_;
    return true;
}

bool cGZWinFileBrowser::GetSelection(FileBrowserSelection& out) {
    GetSelectionInternal(out);
    if (fileNameEdit_) {
        cRZString typed;
        fileNameEdit_->GetText(typed);
        if (selection_.kind == 2) {
            // A file was already chosen: the edit box content wins as its name.
            selectionName_ = typed;
            selectionFull_ = selectionDir_;
            selectionFull_.str().append(selectionName_.str());
        } else if (typed.Strlen() != 0) {
            // Typed a name without picking one: it becomes a file selection in the returned record only.
            selectionName_ = typed;
            selectionFull_ = selectionDir_;
            selectionFull_.str().append(selectionName_.str());
            out.kind = 2;
        }
    }
    return true;
}

bool cGZWinFileBrowser::SetNewDirectoryName(const cIGZString& name) {
    newDirName_.AssignFromIGZString(name);
    return true;
}

bool cGZWinFileBrowser::GoToParentDirectory() {
    char parts[2][256];
    int count = 2;
    SplitDirectory(currentDir_.ToChar(), parts, count);
    if (count < 2) return false;
    cRZString parent(parts[1]);
    ChangeDirectory(parent);
    return true;
}

bool cGZWinFileBrowser::ChangeDirectory(const cIGZString& path) {
    cRZString candidate(path);
    if (!cRZDirectory::DoesDirectoryExist(candidate)) return false;
    currentDir_ = candidate;
    UpdatePath();   // vslot 0x338
    return true;
}

int cGZWinFileBrowser::GetRowOfEntryByName(const cRZString& name) {
    if (!fileGrid_ || !columns_[1].enabled) return 0;
    const int column = columns_[1].position;
    cRZRect used;
    fileGrid_->GetCellUsedRange(used);
    const std::uint32_t bottom = std::uint32_t(used.bottom);
    const int rows = bottom < 0xfffffffeu ? int(bottom) + 1 : 0x400;
    cRZString cell;
    for (int row = 0; row < rows; ++row) {
        if (fileGrid_->GetCellTextValue(column, row, cell) && cell.str() == name.str()) return row;
    }
    return 0;
}

bool cGZWinFileBrowser::CreateNewDirectory() {
    cRZString name(newDirName_);
    std::string path = currentDir_.str() + name.str() + "/";
    if (cRZDirectory::DoesDirectoryExist(cRZString(path.c_str()))) {
        const std::string format = newDirName_.str() + " (%d)";
        auto make = [&](unsigned n) {
            char buf[512];
            std::snprintf(buf, sizeof buf, format.c_str(), int(n));
            name.AssignFromCString(buf);
            path = currentDir_.str() + name.str() + "/";
        };
        make(2);
        for (unsigned n = 3; cRZDirectory::DoesDirectoryExist(cRZString(path.c_str())) && n - 1 < 0x100; ++n) make(n);
    }
    if (!cRZDirectory::CreateDirectoryA(cRZString(path.c_str()))) return false;
    UpdateDirectoryList();   // vslot 0x348
    UpdateFileList();        // vslot 0x340
    if (fileGrid_) {
        const int row = GetRowOfEntryByName(name);
        fileGrid_->ScrollToRow(row);
        if (!fileGrid_->IsCellVisible(0, row)) fileGrid_->SetFirstVisibleCell(0, row);
    }
    return true;
}

bool cGZWinFileBrowser::DeleteDirectory(const cIGZString& path) {
    const std::string target(path.ToChar(), path.Strlen());
    const std::string& cur = currentDir_.str();
    if (target.compare(0, cur.size(), cur) != 0) return false;   // must lie under the current directory
    if (target.size() == cur.size()) return false;               // ... and not be it
    if (!cRZDirectory::RemoveDirectoryA(cRZString(target.c_str()), true)) return false;
    UpdatePath();
    return true;
}

// ---- sort / layout ---------------------------------------------------------------------------------------------------------
bool cGZWinFileBrowser::SetFileSort(std::uint32_t key, std::uint32_t order) {
    fileSortKey_ = int(key);
    fileSortOrder_ = int(order);
    dirty_ = true;
    InvalidateSelfAndParents();
    return true;
}
bool cGZWinFileBrowser::GetFileSort(std::uint32_t& key, std::uint32_t& order) const {
    key = std::uint32_t(fileSortKey_);
    order = std::uint32_t(fileSortOrder_);
    return true;
}
bool cGZWinFileBrowser::SetDirectorySort(std::uint32_t key) {
    dirSort_ = int(key);
    dirty_ = true;
    InvalidateSelfAndParents();
    return true;
}
bool cGZWinFileBrowser::GetDirectorySort(std::uint32_t& key) const {
    key = std::uint32_t(dirSort_);
    return true;
}

bool cGZWinFileBrowser::SetFileListLayout(const std::uint32_t* columns, std::uint32_t count) {
    for (ColumnSlot& c : columns_) { c.enabled = false; c.position = -1; }   // widths are kept
    for (std::uint32_t i = 0; i < count; ++i) {
        if (columns[i] < 6) { columns_[columns[i]].enabled = true; columns_[columns[i]].position = int(i); }
    }
    columnCount_ = count;
    dirty_ = true;
    if (count == 0 || !columns_[1].enabled) {
        // An unusable layout falls back to icon + name.
        columns_[0].enabled = true; columns_[0].position = 0;
        columns_[1].enabled = true; columns_[1].position = 1;
        columnCount_ = 2;
        InvalidateSelfAndParents();
        return false;
    }
    InvalidateSelfAndParents();
    return true;
}

void cGZWinFileBrowser::SetFileListLayout(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    const std::uint32_t cols[3] = {a, b, c};
    SetFileListLayout(cols, 3);
}

bool cGZWinFileBrowser::GetFileListLayout(std::uint32_t* columns, std::uint32_t& count) const {
    if (columnCount_ < count) count = columnCount_;
    std::uint32_t found = 0;
    // The original runs until `count` positions were found without bounding the slot index; the six slots are the bound here.
    for (std::uint32_t slot = 0; found < count && slot < 6; ++slot) {
        if (columns_[slot].position == int(found)) {
            columns[found] = slot;
            ++found;
        }
    }
    return true;
}

bool cGZWinFileBrowser::GetFileListLayout(std::uint32_t& a, std::uint32_t& b, std::uint32_t& c) const {
    std::uint32_t cols[3] = {5, 5, 5};
    std::uint32_t count = 3;
    GetFileListLayout(cols, count);
    a = cols[0]; b = cols[1]; c = cols[2];
    return true;
}

// ---- formats / colours / background -----------------------------------------------------------------------------------------------
bool cGZWinFileBrowser::SetFieldFont(FieldFormat& f, cIGZFont* font) {
    if (f.font != font) {
        if (font) font->AddRef();
        if (f.font) f.font->Release();
        f.font = font;
        dirty_ = true;
        InvalidateSelfAndParents();
    }
    return true;
}

bool cGZWinFileBrowser::SetHighlightColor(std::uint32_t fg, std::uint32_t bg) {
    for (FieldFormat* f : {&nameFormat_, &typeFormat_, &extraFormat_, &driveFormat_}) { f->colors[4] = fg; f->colors[5] = bg; }
    dirty_ = true;
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinFileBrowser::GetHighlightColor(std::uint32_t& fg, std::uint32_t& bg) const {
    fg = nameFormat_.colors[4];
    bg = nameFormat_.colors[5];
    return true;
}

bool cGZWinFileBrowser::GetParentDirectoryText(cIGZString& out) const {
    out.AssignFromChars(parentText_.Data(), parentText_.Strlen());
    return true;
}

bool cGZWinFileBrowser::SetParentDirectoryText(const cIGZString& text) {
    parentText_.AssignFromIGZString(text);
    dirty_ = true;
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinFileBrowser::SetOutlineUse(bool on) {
    if (on != outline_) {
        outline_ = on;
        dirty_ = true;
        InvalidateSelfAndParents();
    }
    return true;
}

bool cGZWinFileBrowser::SetBackgroundColor(std::uint32_t native) {
    std::uint8_t r = 0, g = 0, b = 0;
    if (host_) host_->NativeToRGB(native, r, g, b);   // graphic system vslot 0x120
    backgroundColor_ = 0xff000000u | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
    backgroundOpaque_ = true;
    InvalidateSelfAndParents();
    return true;
}

std::uint32_t cGZWinFileBrowser::GetBackgroundColor() const {
    // Byte +0x4ae (the red channel of the stored colour) is passed for red, green and blue alike (kept).
    const std::uint8_t r = std::uint8_t(backgroundColor_ >> 16);
    return host_ ? host_->MakeNativeColor(r, r, r) : 0;   // graphic system vslot 0x118
}

bool cGZWinFileBrowser::SetBackgroundImage(cIGZBuffer* image) {
    if (backgroundImage_ != image) {
        if (image) AsUnknown(image)->AddRef();
        if (backgroundImage_) AsUnknown(backgroundImage_)->Release();
        backgroundImage_ = image;
        backgroundOpaque_ = true;
        InvalidateSelfAndParents();
    }
    return true;
}

void cGZWinFileBrowser::SetBackgroundTransparent(bool transparent) {
    backgroundOpaque_ = !transparent;
    if (transparent) {
        SetFlag(std::uint32_t(0x20000), true);
        SetFlag(std::uint32_t(0x40000), true);
    }
}

// ---- directory list ----------------------------------------------------------------------------------------------------------------
void cGZWinFileBrowser::InitKnownInitialDirectory() {
    for (DriveListEntry* e : drives_) {
        if (e->kind == 0 && e->name.Strlen() > 1) {
            defaultDir_.str().assign(e->name.str(), 0, 2);
            defaultDir_.str().push_back('/');
            return;
        }
    }
    defaultDir_.AssignFromCString("/");   // no drives on this platform: the root
}

bool cGZWinFileBrowser::PullDriveListUp() {
    if (driveGrid_) {
        ChildDelete(driveGrid_->AsIGZWin());   // cGZWin vslot 0x80
        driveGrid_ = nullptr;
    }
    return true;
}

void cGZWinFileBrowser::FitStringIntoWidth(cRZString& text, int width, cIGZFont* font) {
    if (!font || text.Strlen() == 0) return;
    const std::uint32_t fits = font->CalculateCharsToFitInWidth(text.Data(), text.Strlen(), std::uint32_t(width), nullptr);   // vslot 0x150
    if (fits == 0) { text.str().clear(); return; }
    if (fits == text.Strlen()) return;
    const cIGZString* ellipsisSource = host_ ? host_->DefaultText(kWinCtrlTextEllipsis) : nullptr;
    cRZString ellipsis;
    if (ellipsisSource) ellipsis.Copy(*ellipsisSource);
    else ellipsis.AssignFromCString("...");   // _SZ_DEFAULT_ELLIPSIS_TEXT
    int ellipsisWidth = 0;
    if (ellipsis.Strlen() != 0) {
        cRZRect area;
        font->TextArea(ellipsis.Data(), ellipsis.Strlen(), area);   // vslot 0x178
        ellipsisWidth = area.right - area.left;
    }
    if (ellipsisWidth < width) {
        const std::uint32_t keep = font->CalculateCharsToFitInWidth(text.Data(), text.Strlen(), std::uint32_t(width - ellipsisWidth), nullptr);
        text.str().erase(keep);
        text.RTrim();
        text.str().append(ellipsis.str());
    } else {
        text.str().clear();
    }
}

void cGZWinFileBrowser::FillSelectionMessage(cGZMessage& m, std::uint32_t type, const FileBrowserSelection& sel) {
    m.type = type;
    m.param1 = GetID();
    m.param2 = std::uint64_t(reinterpret_cast<std::uintptr_t>(&sel));
    m.param3 = 0;
}

// Fills the selection record from a file-list row; false for an entry type that cannot be selected.
bool cGZWinFileBrowser::HandleSelectionChange(int row) {
    if (row < 0 || std::size_t(row) >= files_.size()) return false;
    const FileListEntry* e = files_[row];
    if (!e) return false;
    cRZString name(e->name);
    if (e->type == kFileEntryFile || e->type == kFileEntryFileAlt) {
        selection_.kind = 2;
        selection_.field = e->field;
        selectionDir_ = currentDir_;
        selectionName_ = e->name;
        selectionFull_ = currentDir_;
        selectionFull_.str().append(selectionName_.str());
        if (fileNameEdit_) fileNameEdit_->SetText(selectionName_.Data(), selectionName_.Strlen(), false);
    } else if (e->type == kFileEntryParent || e->type == kFileEntryDirectory) {
        if (name.Strlen() == 0 || name.str().back() != '/') name.str().push_back('/');
        selection_.kind = e->type == kFileEntryParent ? 0 : 1;
        selection_.field = 0;
        selectionDir_ = currentDir_;
        selectionName_ = name;
        selectionFull_ = currentDir_;
        selectionFull_.str().append(name.str());
    } else {
        return false;
    }
    selectedRow_ = row;
    FileBrowserSelection sel;
    GetSelectionInternal(sel);
    cGZMessage m;
    FillSelectionMessage(m, kFileBrowserMsgSelectionChanged, sel);
    if (notify_) SendMsg(notify_, m);   // cGZWin vslot 0x480 to the notification target
    return true;
}

bool cGZWinFileBrowser::HandleSelectionDoubleClick(int row) {
    if (row < 0 || std::size_t(row) >= files_.size()) return false;
    const FileListEntry* e = files_[row];
    if (!e) return false;
    cRZString name(e->name);
    if (e->type == kFileEntryFile || e->type == kFileEntryFileAlt) {
        selection_.kind = 2;
        selection_.field = e->field;
        selectionDir_ = currentDir_;
        selectionName_ = e->name;
        selectionFull_ = currentDir_;
        selectionFull_.str().append(selectionName_.str());
    } else if (e->type == kFileEntryParent || e->type == kFileEntryDirectory) {
        if (name.Strlen() == 0 || name.str().back() != '/') name.str().push_back('/');
        selection_.kind = e->type == kFileEntryParent ? 0 : 1;
        selection_.field = 0;
        selectionDir_ = currentDir_;
        selectionName_ = name;
        selectionFull_ = currentDir_;
        selectionFull_.str().append(name.str());
    } else {
        return false;
    }
    FileBrowserSelection sel;
    GetSelectionInternal(sel);
    cGZMessage m;
    FillSelectionMessage(m, kFileBrowserMsgSelectionChosen, sel);
    if (notify_) SendMsg(notify_, m);
    return true;
}

bool cGZWinFileBrowser::HandleDriveListClick(int index) {
    if (driveIndex_ == index) return true;
    if (index >= 0) {
        // Rebuild the path from the first entry up to the clicked one: each name plus a separator.
        int taken = -1;
        for (DriveListEntry* e : drives_) {
            std::string part = e->name.str() + "/";
            if (e->row == 0) currentDir_.str() = part;
            else currentDir_.str().append(part);
            if (++taken >= index) break;
        }
    }
    UpdatePath();   // vslot 0x338
    return true;
}

bool cGZWinFileBrowser::HandleFileListDoubleClick(int row) {
    if (row < 0 || std::size_t(row) >= files_.size()) return false;
    const FileListEntry* e = files_[row];
    if (e->type == kFileEntryDirectory) {
        cRZString path(currentDir_);
        path.str().append(e->name.str());
        path.str().push_back('/');
        ChangeDirectory(path);   // vslot 0xf8
    } else if (e->type == kFileEntryParent) {
        GoToParentDirectory();   // vslot 0xf0
    }
    return true;
}

bool cGZWinFileBrowser::UpdatePath() {
    if (!cRZDirectory::DoesDirectoryExist(currentDir_)) currentDir_.str() = defaultDir_.str();
    FileBrowserSelection unused;
    (void)unused;
    cGZMessage m;
    m.type = kFileBrowserMsgPathChanged;
    m.param1 = GetID();
    m.param2 = std::uint64_t(reinterpret_cast<std::uintptr_t>(&currentDir_));
    m.param3 = 0;
    if (notify_) SendMsg(notify_, m);
    UpdateDirectoryList();   // vslot 0x348
    UpdateFileList();        // vslot 0x340
    return true;
}

bool cGZWinFileBrowser::ClearFileList() {
    for (FileListEntry* e : files_) delete e;   // (the entry's icon belongs to the browser's icon table)
    files_.clear();
    return true;
}

bool cGZWinFileBrowser::ClearDirectoryList() {
    for (DriveListEntry* e : drives_) delete e;
    drives_.clear();
    return true;
}
